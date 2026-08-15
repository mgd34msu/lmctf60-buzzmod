/* Transaction, failure-injection, and path-boundary tests for RUNE v3. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* q_shared.h has no include guard; include it once before SLIPGATE headers. */
#include "q_shared.h"
#include "slipgate/sg_rune_install.h"
#include "slipgate/sg_rune_wire.h"

#define TEST_FILE_BYTES (SG_RUNE_V3_HEADER_BYTES + \
	2U * SG_RUNE_V3_SEED_BYTES + 2U * SG_RUNE_V3_LINK_BYTES)

static const unsigned char old_rune[] = "old-rune";
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct writer_fixture_s
{
	sg_rune_v3_identity_t identity;
	rune_seed_t seeds[2];
	rune_link_t links[2];
	uint64_t keys[2];
	uint8_t marks[2];
	sg_rune_v3_workspace_t workspace;
} writer_fixture_t;

typedef struct filesystem_fixture_s
{
	char root[MAX_OSPATH];
	char maps[MAX_OSPATH];
	char destination[MAX_OSPATH];
} filesystem_fixture_t;

typedef enum injected_failure_e
{
	INJECT_NONE = 0,
	INJECT_OPEN,
	INJECT_WRITE,
	INJECT_FLUSH,
	INJECT_SYNC,
	INJECT_CLOSE,
	INJECT_RENAME
} injected_failure_t;

typedef struct test_io_s
{
	const sg_rune_install_ops_t *base;
	injected_failure_t failure;
	size_t failed_write_call;
	size_t open_calls;
	size_t write_calls;
	size_t flush_calls;
	size_t sync_calls;
	size_t close_calls;
	size_t rename_calls;
	size_t remove_calls;
	unsigned long process_id;
	int fail_remove;
	writer_fixture_t *mutate_writer;
	size_t mutate_write_call;
} test_io_t;

typedef struct revalidate_fixture_s
{
	test_io_t *io;
	uint32_t captured_identity;
	uint32_t active_identity;
	float captured_gravity;
	float active_gravity;
	size_t calls;
} revalidate_fixture_t;

typedef struct forged_stream_s
{
	writer_fixture_t *writer;
	int calls;
	int forge_call;
	int forge_index;
} forged_stream_t;

typedef struct swapping_stream_s
{
	writer_fixture_t *writer;
	int calls;
} swapping_stream_t;

static int Format(char *output, size_t output_size, const char *format,
	const char *first, const char *second)
{
	int written = snprintf(output, output_size, format, first, second);

	return written >= 0 && (size_t)written < output_size;
}

static void SetMap(char output[SG_RUNE_V3_MAP_NAME_BYTES],
	const char *map_name)
{
	size_t length = strlen(map_name);

	memset(output, 0, SG_RUNE_V3_MAP_NAME_BYTES);
	CHECK(length < SG_RUNE_V3_MAP_NAME_BYTES);
	if (length < SG_RUNE_V3_MAP_NAME_BYTES)
		memcpy(output, map_name, length);
}

static void InitWriter(writer_fixture_t *writer)
{
	memset(writer, 0, sizeof(*writer));
	SetMap(writer->identity.map_name, "lmctf07");
	writer->identity.bsp_checksum = UINT32_C(0x12345678);
	writer->identity.entity_crc32 = UINT32_C(0x9abcdef0);
	writer->identity.physics_flags =
		SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED;
	writer->identity.gravity = 650.0f;
	writer->identity.airaccelerate = 0.0f;
	writer->identity.maxvelocity = 2000.0f;
	writer->identity.pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	writer->identity.server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	writer->identity.host_physics_id = 1;

	writer->seeds[0].origin[0] = 0.0f;
	writer->seeds[0].origin[1] = 0.0f;
	writer->seeds[0].origin[2] = 0.0f;
	writer->seeds[0].area_hint = 7;
	writer->seeds[1].origin[0] = 64.0f;
	writer->seeds[1].origin[1] = 0.0f;
	writer->seeds[1].origin[2] = 0.0f;
	writer->seeds[1].area_hint = 255;

	writer->links[0].from = 0;
	writer->links[0].to = 1;
	writer->links[0].action = RL_RUN;
	writer->links[0].provenance = RL_PROVEN;
	writer->links[0].heading_slack = 255;
	writer->links[0].cost_ms = 100;
	writer->links[1].from = 1;
	writer->links[1].to = 0;
	writer->links[1].action = RL_RUN;
	writer->links[1].provenance = RL_PROVEN;
	writer->links[1].heading_slack = 255;
	writer->links[1].cost_ms = 125;

	writer->workspace.link_keys = writer->keys;
	writer->workspace.link_key_capacity = 2;
	writer->workspace.source_marks = writer->marks;
	writer->workspace.source_mark_capacity = 2;
}

static sg_rune_write_result_t StreamWriter(void *context,
	sg_rune_write_sink_fn sink, void *sink_context)
{
	writer_fixture_t *writer = context;

	return SG_RuneV3Write(&writer->identity, writer->seeds, 2,
		writer->links, 2, &writer->workspace, sink, sink_context);
}

static sg_rune_write_result_t StreamForged(void *context,
	sg_rune_write_sink_fn sink, void *sink_context)
{
	forged_stream_t *forged = context;
	sg_rune_write_result_t result = StreamWriter(forged->writer, sink,
		sink_context);

	forged->calls++;
	if (forged->calls == forged->forge_call && result.diagnostic == RLW_OK)
	{
		if (forged->forge_index)
			result.index = 0;
		else
			result.stage = SG_RUNE_WRITE_STAGE_VERIFY;
	}
	return result;
}

static sg_rune_write_result_t StreamSwapping(void *context,
	sg_rune_write_sink_fn sink, void *sink_context)
{
	swapping_stream_t *swapping = context;
	sg_rune_write_result_t result = StreamWriter(swapping->writer, sink,
		sink_context);

	swapping->calls++;
	if (swapping->calls == 1 && result.diagnostic == RLW_OK)
		swapping->writer->links[0].cost_ms += 25;
	return result;
}

static int WriteFile(const char *path, const void *data, size_t data_size)
{
	FILE *file = fopen(path, "wb");
	int ok;

	if (!file)
		return 0;
	ok = fwrite(data, 1, data_size, file) == data_size &&
	     fflush(file) == 0;
	if (fclose(file) != 0)
		ok = 0;
	return ok;
}

static size_t ReadFile(const char *path, unsigned char *data,
	size_t data_capacity)
{
	FILE *file = fopen(path, "rb");
	size_t count;

	if (!file)
		return 0;
	count = fread(data, 1, data_capacity, file);
	if (ferror(file) || fclose(file) != 0)
		return 0;
	return count;
}

static int InitFilesystem(filesystem_fixture_t *fixture)
{
	char template_path[] = "/tmp/sg-rune-install-XXXXXX";
	char *created = mkdtemp(template_path);

	memset(fixture, 0, sizeof(*fixture));
	if (!created || strlen(created) >= sizeof(fixture->root))
		return 0;
	memcpy(fixture->root, created, strlen(created) + 1U);
	if (!Format(fixture->maps, sizeof(fixture->maps), "%s/%s",
	    fixture->root, "maps") || mkdir(fixture->maps, 0700) != 0)
		return 0;
	if (!SG_RuneInstallDestinationPath(fixture->destination,
	    sizeof(fixture->destination), fixture->root, "lmctf07"))
		return 0;
	return WriteFile(fixture->destination, old_rune, sizeof(old_rune) - 1U);
}

static void CleanupFilesystem(const filesystem_fixture_t *fixture)
{
	DIR *directory = opendir(fixture->maps);
	struct dirent *entry;

	if (directory)
	{
		while ((entry = readdir(directory)) != NULL)
		{
			char path[MAX_OSPATH];

			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0)
				continue;
			if (Format(path, sizeof(path), "%s/%s", fixture->maps,
			    entry->d_name))
				(void)remove(path);
		}
		(void)closedir(directory);
	}
	(void)rmdir(fixture->maps);
	(void)rmdir(fixture->root);
}

static size_t CountTemporaries(const filesystem_fixture_t *fixture)
{
	DIR *directory = opendir(fixture->maps);
	struct dirent *entry;
	size_t count = 0;

	if (!directory)
		return SIZE_MAX;
	while ((entry = readdir(directory)) != NULL)
	{
		size_t length = strlen(entry->d_name);

		if (strncmp(entry->d_name, ".rune.", 6) == 0 &&
		    length >= 4 && strcmp(entry->d_name + length - 4, ".tmp") == 0)
			count++;
	}
	CHECK(closedir(directory) == 0);
	return count;
}

static int PriorRunePreserved(const filesystem_fixture_t *fixture)
{
	unsigned char data[sizeof(old_rune)];
	size_t count = ReadFile(fixture->destination, data, sizeof(data));

	return count == sizeof(old_rune) - 1U &&
	       memcmp(data, old_rune, sizeof(old_rune) - 1U) == 0;
}

static FILE *TestOpen(void *context, const char *path)
{
	test_io_t *io = context;

	io->open_calls++;
	if (io->failure == INJECT_OPEN)
	{
		errno = EACCES;
		return NULL;
	}
	return io->base->open_exclusive(io->base->context, path);
}

static size_t TestWrite(void *context, FILE *file, const void *data,
	size_t data_size)
{
	test_io_t *io = context;

	io->write_calls++;
	if (io->mutate_writer && io->write_calls == io->mutate_write_call)
		io->mutate_writer->links[0].cost_ms += 25;
	if (io->failure == INJECT_WRITE &&
	    io->write_calls == io->failed_write_call)
	{
		size_t partial = data_size > 1 ? data_size - 1U : 0;
		size_t written = io->base->write(io->base->context, file, data,
			partial);

		errno = EIO;
		return written;
	}
	return io->base->write(io->base->context, file, data, data_size);
}

static int TestFlush(void *context, FILE *file)
{
	test_io_t *io = context;
	int result;

	io->flush_calls++;
	result = io->base->flush(io->base->context, file);
	if (result == 0 && io->failure == INJECT_FLUSH)
	{
		errno = EIO;
		return -1;
	}
	return result;
}

static int TestSync(void *context, FILE *file)
{
	test_io_t *io = context;
	int result;

	io->sync_calls++;
	result = io->base->sync_file(io->base->context, file);
	if (result == 0 && io->failure == INJECT_SYNC)
	{
		errno = EIO;
		return -1;
	}
	return result;
}

static int TestClose(void *context, FILE *file)
{
	test_io_t *io = context;
	int result;

	io->close_calls++;
	result = io->base->close_file(io->base->context, file);
	if (result == 0 && io->failure == INJECT_CLOSE)
	{
		errno = EIO;
		return -1;
	}
	return result;
}

static int TestRename(void *context, const char *temporary_path,
	const char *destination_path)
{
	test_io_t *io = context;

	io->rename_calls++;
	if (io->failure == INJECT_RENAME)
	{
		errno = EIO;
		return -1;
	}
	return io->base->rename_replace(io->base->context, temporary_path,
		destination_path);
}

static int TestRemove(void *context, const char *path)
{
	test_io_t *io = context;

	io->remove_calls++;
	if (io->fail_remove)
	{
		errno = EACCES;
		return -1;
	}
	return io->base->remove_path(io->base->context, path);
}

static unsigned long TestProcessId(void *context)
{
	test_io_t *io = context;

	return io->process_id;
}

static sg_rune_install_ops_t TestOps(test_io_t *io)
{
	sg_rune_install_ops_t ops;

	memset(io, 0, sizeof(*io));
	io->base = SG_RuneInstallDefaultOps();
	io->process_id = 4242;
	memset(&ops, 0, sizeof(ops));
	ops.context = io;
	ops.open_exclusive = TestOpen;
	ops.write = TestWrite;
	ops.flush = TestFlush;
	ops.sync_file = TestSync;
	ops.close_file = TestClose;
	ops.rename_replace = TestRename;
	ops.remove_path = TestRemove;
	ops.process_id = TestProcessId;
	return ops;
}

static int Revalidate(void *context)
{
	revalidate_fixture_t *fixture = context;
	uint32_t captured_bits;
	uint32_t active_bits;

	fixture->calls++;
	memcpy(&captured_bits, &fixture->captured_gravity,
		sizeof(captured_bits));
	memcpy(&active_bits, &fixture->active_gravity, sizeof(active_bits));
	return fixture->io->close_calls == 1 &&
	       fixture->captured_identity == fixture->active_identity &&
	       captured_bits == active_bits;
}

static void InitRevalidate(revalidate_fixture_t *revalidate, test_io_t *io)
{
	memset(revalidate, 0, sizeof(*revalidate));
	revalidate->io = io;
	revalidate->captured_identity = UINT32_C(0x12345678);
	revalidate->active_identity = revalidate->captured_identity;
	revalidate->captured_gravity = 650.0f;
	revalidate->active_gravity = revalidate->captured_gravity;
}

static sg_rune_install_result_t Install(filesystem_fixture_t *filesystem,
	writer_fixture_t *writer, test_io_t *io, sg_rune_install_ops_t *ops,
	revalidate_fixture_t *revalidate, char temporary[MAX_OSPATH])
{
	char destination[MAX_OSPATH];
	sg_rune_install_result_t result = SG_RuneInstallV3(filesystem->root,
		"lmctf07", destination, sizeof(destination), temporary, MAX_OSPATH,
		StreamWriter, writer, Revalidate, revalidate, ops);

	CHECK(strcmp(destination, filesystem->destination) == 0);
	(void)io;
	return result;
}

static void CheckInstalled(const filesystem_fixture_t *filesystem,
	const writer_fixture_t *writer)
{
	unsigned char data[TEST_FILE_BYTES];
	sg_rune_v3_header_t header;
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t links[2];
	uint64_t keys[2];
	uint8_t marks[2];
	sg_rune_v3_workspace_t workspace;
	size_t count = ReadFile(filesystem->destination, data, sizeof(data));

	workspace.link_keys = keys;
	workspace.link_key_capacity = 2;
	workspace.source_marks = marks;
	workspace.source_mark_capacity = 2;
	CHECK(count == sizeof(data));
	if (count != sizeof(data))
		return;
	CHECK(SG_RuneV3Decode(data, sizeof(data), &writer->identity, &header,
		seeds, 2, links, 2, &workspace) == RLW_OK);
	CHECK(header.num_seeds == 2 && header.num_links == 2);
	CHECK(header.gravity == 650.0f);
	CHECK(links[0].source == 0 && links[0].destination == 1);
	CHECK(links[1].source == 1 && links[1].destination == 0);
}

static void TestPathBoundaries(void)
{
	char map_name[64];
	char game_directory[105];
	char path[MAX_OSPATH];
	char small[16];

	memset(map_name, 'm', sizeof(map_name));
	map_name[sizeof(map_name) - 1U] = '\0';
	memset(game_directory, 'g', sizeof(game_directory));
	game_directory[53] = '\0';
	CHECK(SG_RuneInstallDestinationPath(path, sizeof(path),
		game_directory, map_name));
	CHECK(strlen(path) == MAX_OSPATH - 1U);
	game_directory[53] = 'g';
	game_directory[54] = '\0';
	memset(path, 0xa5, sizeof(path));
	CHECK(!SG_RuneInstallDestinationPath(path, sizeof(path),
		game_directory, map_name));
	CHECK(path[0] == '\0');

	memset(game_directory, 'g', sizeof(game_directory));
	game_directory[103] = '\0';
	CHECK(SG_RuneInstallTemporaryPath(path, sizeof(path), game_directory,
		99999UL, 63));
	CHECK(strlen(path) == MAX_OSPATH - 1U);
	game_directory[103] = 'g';
	game_directory[104] = '\0';
	CHECK(!SG_RuneInstallTemporaryPath(path, sizeof(path), game_directory,
		99999UL, 63));
	CHECK(path[0] == '\0');

	memset(small, 0xa5, sizeof(small));
	CHECK(!SG_RuneInstallDestinationPath(small, sizeof(small), ".",
		"lmctf07"));
	CHECK(small[0] == '\0');
	CHECK(!SG_RuneInstallDestinationPath(path, sizeof(path), ".", "../x"));
	CHECK(path[0] == '\0');
}

static void TestSuccessAndExclusiveCollision(void)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char temporary[MAX_OSPATH];
	char collision[MAX_OSPATH];
	char collision_target[MAX_OSPATH];
	sg_rune_install_result_t result;
	unsigned char sentinel[4] = { 'b', 'u', 's', 'y' };
	unsigned char readback[4];
	struct stat collision_stat;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	CHECK(SG_RuneInstallTemporaryPath(collision, sizeof(collision),
		filesystem.root, io.process_id, 0));
	CHECK(Format(collision_target, sizeof(collision_target), "%s/%s",
		filesystem.root, "collision-target"));
	CHECK(WriteFile(collision_target, sentinel, sizeof(sentinel)));
	CHECK(symlink(collision_target, collision) == 0);
	result = Install(&filesystem, &writer, &io, &ops, &revalidate,
		temporary);
	CHECK(result.status == SG_RUNE_INSTALL_OK);
	CHECK(result.writer_called == 2);
	CHECK(result.writer.diagnostic == RLW_OK);
	CHECK(result.writer.bytes_written == TEST_FILE_BYTES);
	CHECK(result.writer.file_size == TEST_FILE_BYTES);
	CHECK(result.temp_attempt == 1);
	CHECK(io.open_calls == 2 && io.rename_calls == 1);
	CHECK(revalidate.calls == 1);
	CHECK(lstat(collision, &collision_stat) == 0 &&
		S_ISLNK(collision_stat.st_mode));
	CHECK(ReadFile(collision, readback, sizeof(readback)) == sizeof(readback));
	CHECK(memcmp(readback, sentinel, sizeof(sentinel)) == 0);
	CHECK(CountTemporaries(&filesystem) == 1);
	CheckInstalled(&filesystem, &writer);
	CHECK(remove(collision) == 0);
	CHECK(remove(collision_target) == 0);
	CHECK(CountTemporaries(&filesystem) == 0);
	CleanupFilesystem(&filesystem);
}

static void TestAllTemporaryNamesCollide(void)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char destination[MAX_OSPATH];
	char temporary[MAX_OSPATH];
	char collisions[SG_RUNE_INSTALL_TEMP_ATTEMPTS][MAX_OSPATH];
	unsigned char expected[SG_RUNE_INSTALL_TEMP_ATTEMPTS];
	sg_rune_install_result_t result;
	unsigned int attempt;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	for (attempt = 0; attempt < SG_RUNE_INSTALL_TEMP_ATTEMPTS; attempt++)
	{
		expected[attempt] = (unsigned char)attempt;
		CHECK(SG_RuneInstallTemporaryPath(collisions[attempt],
			sizeof(collisions[attempt]), filesystem.root, io.process_id,
			attempt));
		CHECK(WriteFile(collisions[attempt], &expected[attempt], 1));
	}
	result = SG_RuneInstallV3(filesystem.root, "lmctf07", destination,
		sizeof(destination), temporary, sizeof(temporary), StreamWriter,
		&writer, Revalidate, &revalidate, &ops);
	CHECK(result.status == SG_RUNE_INSTALL_TEMP_EXHAUSTED);
	CHECK(result.writer_called == 1);
	CHECK(io.open_calls == SG_RUNE_INSTALL_TEMP_ATTEMPTS);
	CHECK(io.write_calls == 0 && io.remove_calls == 0 &&
		io.rename_calls == 0);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) ==
		SG_RUNE_INSTALL_TEMP_ATTEMPTS);
	for (attempt = 0; attempt < SG_RUNE_INSTALL_TEMP_ATTEMPTS; attempt++)
	{
		unsigned char actual = 0xff;

		CHECK(ReadFile(collisions[attempt], &actual, 1) == 1);
		CHECK(actual == expected[attempt]);
	}
	CleanupFilesystem(&filesystem);
}

static void TestInjectedFailure(injected_failure_t failure,
	size_t failed_write_call, sg_rune_install_status_t expected_status)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	io.failure = failure;
	io.failed_write_call = failed_write_call;
	result = Install(&filesystem, &writer, &io, &ops, &revalidate,
		temporary);
	CHECK(result.status == expected_status);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 0);
	if (failure == INJECT_OPEN)
	{
		CHECK(result.writer_called == 1);
		CHECK(io.write_calls == 0 && io.close_calls == 0);
	}
	else
	{
		CHECK(result.writer_called == 2);
		CHECK(io.close_calls == 1);
	}
	if (failure == INJECT_WRITE)
	{
		CHECK(result.writer.diagnostic == RLW_IO_ERROR);
		if (failed_write_call == 1)
			CHECK(result.writer.stage == SG_RUNE_WRITE_STAGE_EMIT_HEADER);
		else if (failed_write_call <= 3)
		{
			CHECK(result.writer.stage == SG_RUNE_WRITE_STAGE_EMIT_SEED);
			CHECK(result.writer.index == failed_write_call - 2U);
		}
		else
		{
			CHECK(result.writer.stage == SG_RUNE_WRITE_STAGE_EMIT_LINK);
			CHECK(result.writer.index == failed_write_call - 4U);
		}
	}
	CleanupFilesystem(&filesystem);
}

static void TestCleanupFailureIsSurfaced(void)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	io.failure = INJECT_WRITE;
	io.failed_write_call = 1;
	io.fail_remove = 1;
	result = Install(&filesystem, &writer, &io, &ops, &revalidate,
		temporary);
	CHECK(result.status == SG_RUNE_INSTALL_WRITER_FAILED);
	CHECK(result.cleanup_error == EACCES);
	CHECK(io.remove_calls == 1 && io.rename_calls == 0);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 1);
	io.fail_remove = 0;
	CHECK(remove(temporary) == 0);
	CHECK(CountTemporaries(&filesystem) == 0);
	CleanupFilesystem(&filesystem);
}

static void TestMalformedPreflightHasNoFilesystemAccess(void)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	writer.links[0].action = RL_ROCKETJUMP;
	result = Install(&filesystem, &writer, &io, &ops, &revalidate,
		temporary);
	CHECK(result.status == SG_RUNE_INSTALL_WRITER_FAILED);
	CHECK(result.writer_called == 1);
	CHECK(result.writer.diagnostic == RLW_BAD_LINK_RECORD);
	CHECK(result.writer.reason == RLR_ACTION_DISABLED);
	CHECK(result.writer.stage == SG_RUNE_WRITE_STAGE_ADAPT_LINK);
	CHECK(result.writer.index == 0);
	CHECK(result.writer.bytes_written == 0);
	CHECK(io.open_calls == 0 && io.write_calls == 0 && io.remove_calls == 0);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 0);
	CleanupFilesystem(&filesystem);
}

static void TestRevalidationDrift(int identity_drift)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	if (identity_drift)
		revalidate.active_identity ^= UINT32_C(1);
	else
		revalidate.active_gravity = 800.0f;
	result = Install(&filesystem, &writer, &io, &ops, &revalidate,
		temporary);
	CHECK(result.status == SG_RUNE_INSTALL_REVALIDATE_FAILED);
	CHECK(revalidate.calls == 1 && io.rename_calls == 0);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 0);
	CleanupFilesystem(&filesystem);
}

static void TestPassTwoMutation(void)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	io.mutate_writer = &writer;
	io.mutate_write_call = 1; /* after header, before pass-two records */
	result = Install(&filesystem, &writer, &io, &ops, &revalidate,
		temporary);
	CHECK(result.status == SG_RUNE_INSTALL_WRITER_FAILED);
	CHECK(result.writer.diagnostic == RLW_BAD_PAYLOAD_CRC);
	CHECK(result.writer.stage == SG_RUNE_WRITE_STAGE_VERIFY);
	CHECK(revalidate.calls == 0 && io.rename_calls == 0);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 0);
	CleanupFilesystem(&filesystem);
}

static void TestForgedNonterminalResult(int forge_call, int forge_index)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	forged_stream_t forged;
	char destination[MAX_OSPATH];
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	memset(&forged, 0, sizeof(forged));
	forged.writer = &writer;
	forged.forge_call = forge_call;
	forged.forge_index = forge_index;
	result = SG_RuneInstallV3(filesystem.root, "lmctf07", destination,
		sizeof(destination), temporary, sizeof(temporary), StreamForged,
		&forged, Revalidate, &revalidate, &ops);
	CHECK(result.status == SG_RUNE_INSTALL_WRITER_FAILED);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 0);
	if (forge_call == 1)
		CHECK(io.open_calls == 0);
	else
		CHECK(io.open_calls == 1 && io.rename_calls == 0);
	CleanupFilesystem(&filesystem);
}

static void TestBetweenPassValidMutation(void)
{
	filesystem_fixture_t filesystem;
	writer_fixture_t writer;
	test_io_t io;
	sg_rune_install_ops_t ops = TestOps(&io);
	revalidate_fixture_t revalidate;
	swapping_stream_t swapping;
	char destination[MAX_OSPATH];
	char temporary[MAX_OSPATH];
	sg_rune_install_result_t result;

	CHECK(InitFilesystem(&filesystem));
	InitWriter(&writer);
	InitRevalidate(&revalidate, &io);
	memset(&swapping, 0, sizeof(swapping));
	swapping.writer = &writer;
	result = SG_RuneInstallV3(filesystem.root, "lmctf07", destination,
		sizeof(destination), temporary, sizeof(temporary), StreamSwapping,
		&swapping, Revalidate, &revalidate, &ops);
	CHECK(result.status == SG_RUNE_INSTALL_WRITER_FAILED);
	CHECK(result.writer.diagnostic == RLW_BAD_PAYLOAD_CRC);
	CHECK(result.writer.stage == SG_RUNE_WRITE_STAGE_VERIFY);
	CHECK(result.writer.index == SG_RUNE_WRITE_INDEX_NONE);
	CHECK(io.open_calls == 1 && io.rename_calls == 0);
	CHECK(PriorRunePreserved(&filesystem));
	CHECK(CountTemporaries(&filesystem) == 0);
	CleanupFilesystem(&filesystem);
}

int main(void)
{
	size_t write_call;

	TestPathBoundaries();
	TestSuccessAndExclusiveCollision();
	TestAllTemporaryNamesCollide();
	TestInjectedFailure(INJECT_OPEN, 0,
		SG_RUNE_INSTALL_TEMP_OPEN_FAILED);
	for (write_call = 1; write_call <= 5; write_call++)
		TestInjectedFailure(INJECT_WRITE, write_call,
			SG_RUNE_INSTALL_WRITER_FAILED);
	TestInjectedFailure(INJECT_FLUSH, 0,
		SG_RUNE_INSTALL_FLUSH_FAILED);
	TestInjectedFailure(INJECT_SYNC, 0,
		SG_RUNE_INSTALL_SYNC_FAILED);
	TestInjectedFailure(INJECT_CLOSE, 0,
		SG_RUNE_INSTALL_CLOSE_FAILED);
	TestInjectedFailure(INJECT_RENAME, 0,
		SG_RUNE_INSTALL_RENAME_FAILED);
	TestCleanupFailureIsSurfaced();
	TestMalformedPreflightHasNoFilesystemAccess();
	TestRevalidationDrift(1);
	TestRevalidationDrift(0);
	TestPassTwoMutation();
	TestForgedNonterminalResult(1, 0);
	TestForgedNonterminalResult(1, 1);
	TestForgedNonterminalResult(2, 0);
	TestForgedNonterminalResult(2, 1);
	TestBetweenPassValidMutation();

	if (failures)
	{
		fprintf(stderr, "sg_rune_install_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_install_test: ok");
	return 0;
}
