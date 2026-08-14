/* Failure-atomic transaction tests for authenticated RUNE v3 sidecars. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* q_shared.h has no include guard; include it once before sidecar headers. */
#include "q_shared.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_wire.h"
#include "slipgate/sg_sidecar_loader.h"
#include "slipgate/sg_sidecar_store.h"
#include "slipgate/sg_sidecar_wire.h"

#define RUNE_GOLDEN_BYTES 248U
#define TEST_IMAGE_BYTES 4096U
#define TEST_PATHS SG_SIDECAR_STORE_TEMP_ATTEMPTS
#define TEST_TEMP_NONCE UINT64_C(0x0123456789abcdef)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef enum store_failure_e
{
	STORE_FAIL_NONE = 0,
	STORE_FAIL_OPEN,
	STORE_FAIL_WRITE,
	STORE_FAIL_WRITE_ZERO,
	STORE_FAIL_WRITE_OVERSIZE,
	STORE_FAIL_WRITE_PARTIAL_ERROR,
	STORE_FAIL_FLUSH,
	STORE_FAIL_FILE_SYNC,
	STORE_FAIL_CLOSE,
	STORE_FAIL_REVALIDATE_ERROR,
	STORE_FAIL_REVALIDATE_DRIFT,
	STORE_FAIL_REVALIDATE_INVALID,
	STORE_FAIL_RENAME,
	STORE_FAIL_DIRECTORY_SYNC,
	STORE_FAIL_CLEANUP
} store_failure_t;

typedef enum store_event_e
{
	STORE_EVENT_NONE = 0,
	STORE_EVENT_OPEN,
	STORE_EVENT_WRITE,
	STORE_EVENT_FLUSH,
	STORE_EVENT_FILE_SYNC,
	STORE_EVENT_CLOSE,
	STORE_EVENT_REVALIDATE,
	STORE_EVENT_RENAME,
	STORE_EVENT_DIRECTORY_SYNC,
	STORE_EVENT_CLEANUP
} store_event_t;

typedef struct store_fixture_s
{
	store_failure_t failure;
	store_event_t last_event;
	size_t sequence;
	size_t flush_sequence;
	size_t file_sync_sequence;
	size_t close_sequence;
	size_t revalidate_sequence;
	size_t replace_sequence;
	size_t directory_sync_sequence;
	unsigned int collision_count;
	size_t write_limit;
	size_t fail_write_call;
	int close_also_fails;
	int cleanup_also_fails;
	int open;
	int temp_exists;
	int replacement_adjacent;
	unsigned char temporary[TEST_IMAGE_BYTES];
	size_t temporary_size;
	unsigned char destination[TEST_IMAGE_BYTES];
	size_t destination_size;
	unsigned char original[32];
	size_t original_size;
	char expected_destination[MAX_OSPATH];
	char expected_directory[MAX_OSPATH];
	char current_temporary[MAX_OSPATH];
	char opened_paths[TEST_PATHS][MAX_OSPATH];
	size_t open_calls;
	size_t write_calls;
	size_t flush_calls;
	size_t file_sync_calls;
	size_t close_calls;
	size_t revalidate_calls;
	size_t replace_calls;
	size_t directory_sync_calls;
	size_t cleanup_calls;
} store_fixture_t;

static int HexDigit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int LoadHex(const char *path, unsigned char *bytes, size_t size)
{
	FILE *file = fopen(path, "rb");
	size_t count = 0;
	int high = -1;
	int c;

	if (!file)
		return 0;
	while ((c = fgetc(file)) != EOF)
	{
		int digit = HexDigit(c);

		if (digit < 0)
			continue;
		if (high < 0)
			high = digit;
		else
		{
			if (count >= size)
			{
				fclose(file);
				return 0;
			}
			bytes[count++] = (unsigned char)((high << 4) | digit);
			high = -1;
		}
	}
	return fclose(file) == 0 && count == size && high < 0;
}

static int GoldenRuneHeader(sg_rune_v3_header_t *rune)
{
	unsigned char encoded[RUNE_GOLDEN_BYTES];

	return LoadHex("tests/fixtures/rune_v3_wire_golden.hex", encoded,
		sizeof(encoded)) &&
		SG_RuneV3DecodeHeader(encoded, SG_RUNE_V3_HEADER_BYTES, rune) ==
		RLW_OK;
}

static int EncodeImage(sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune, const uint8_t *marks,
	size_t marks_size, unsigned char fill,
	unsigned char *encoded, size_t capacity, size_t *encoded_size_out)
{
	size_t file_size;
	size_t payload_size;
	unsigned char payload[TEST_IMAGE_BYTES];

	if (SG_SidecarV3FileSize(kind, rune, &file_size) != SCD_OK ||
	    file_size < SG_SIDECAR_V3_HEADER_BYTES || file_size > capacity)
		return 0;
	payload_size = file_size - SG_SIDECAR_V3_HEADER_BYTES;
	memset(payload, fill, payload_size);
	return SG_SidecarV3Encode(kind, rune, marks, marks_size, payload,
		payload_size, encoded, capacity, encoded_size_out) == SCD_OK;
}

static void StoreEvent(store_fixture_t *io, store_event_t event)
{
	io->last_event = event;
	io->sequence++;
}

static int HasExpectedTempSuffix(const store_fixture_t *io,
	const char *path, unsigned int attempt)
{
	char expected[MAX_OSPATH];
	int written = snprintf(expected, sizeof(expected),
		"%s.tmp.%016llx.%02u", io->expected_destination,
		(unsigned long long)TEST_TEMP_NONCE, attempt);

	return written >= 0 && (size_t)written < sizeof(expected) &&
	       strcmp(path, expected) == 0;
}

static uint64_t TestTempNonce(void *context)
{
	(void)context;
	return TEST_TEMP_NONCE;
}

static void *TestOpenExclusive(void *context, const char *path,
	int *os_error_out)
{
	store_fixture_t *io = context;
	unsigned int attempt = (unsigned int)io->open_calls;

	StoreEvent(io, STORE_EVENT_OPEN);
	*os_error_out = 0;
	CHECK(io->open_calls < TEST_PATHS);
	CHECK(HasExpectedTempSuffix(io, path, attempt));
	CHECK(strchr(path + strlen(io->expected_destination), '/') == NULL);
	CHECK(strchr(path + strlen(io->expected_destination), '\\') == NULL);
	if (io->open_calls < TEST_PATHS)
		(void)snprintf(io->opened_paths[io->open_calls],
			sizeof(io->opened_paths[io->open_calls]), "%s", path);
	io->open_calls++;
	if (io->open_calls <= io->collision_count)
	{
		*os_error_out = EEXIST;
		return NULL;
	}
	if (io->failure == STORE_FAIL_OPEN)
	{
		*os_error_out = EACCES;
		return NULL;
	}
	CHECK(!io->open);
	CHECK(!io->temp_exists);
	io->open = 1;
	io->temp_exists = 1;
	io->temporary_size = 0;
	(void)snprintf(io->current_temporary,
		sizeof(io->current_temporary), "%s", path);
	return io;
}

static size_t TestWrite(void *context, void *handle,
	const unsigned char *data, size_t data_size, int *os_error_out)
{
	store_fixture_t *io = context;
	size_t count = data_size;

	StoreEvent(io, STORE_EVENT_WRITE);
	io->write_calls++;
	*os_error_out = 0;
	CHECK(handle == io);
	CHECK(io->open);
	CHECK(io->temp_exists);
	if (io->failure == STORE_FAIL_WRITE &&
	    io->write_calls == io->fail_write_call)
	{
		*os_error_out = ENOSPC;
		return 0;
	}
	if (io->failure == STORE_FAIL_WRITE_ZERO &&
	    io->write_calls == io->fail_write_call)
		return 0;
	if (io->failure == STORE_FAIL_WRITE_OVERSIZE &&
	    io->write_calls == io->fail_write_call)
		return data_size + 1U;
	if (io->write_limit > 0 && count > io->write_limit)
		count = io->write_limit;
	CHECK(io->temporary_size + count <= sizeof(io->temporary));
	if (io->temporary_size + count <= sizeof(io->temporary))
	{
		memcpy(io->temporary + io->temporary_size, data, count);
		io->temporary_size += count;
	}
	if (io->failure == STORE_FAIL_WRITE_PARTIAL_ERROR &&
	    io->write_calls == io->fail_write_call)
		*os_error_out = EIO;
	return count;
}

static int TestFlush(void *context, void *handle, int *os_error_out)
{
	store_fixture_t *io = context;

	StoreEvent(io, STORE_EVENT_FLUSH);
	io->flush_calls++;
	io->flush_sequence = io->sequence;
	*os_error_out = 0;
	CHECK(handle == io);
	CHECK(io->open);
	if (io->failure == STORE_FAIL_FLUSH)
	{
		*os_error_out = EIO;
		return -1;
	}
	return 0;
}

static int TestSyncFile(void *context, void *handle, int *os_error_out)
{
	store_fixture_t *io = context;

	StoreEvent(io, STORE_EVENT_FILE_SYNC);
	io->file_sync_calls++;
	io->file_sync_sequence = io->sequence;
	*os_error_out = 0;
	CHECK(handle == io);
	CHECK(io->open);
	if (io->failure == STORE_FAIL_FILE_SYNC)
	{
		*os_error_out = EIO;
		return -1;
	}
	return 0;
}

static int TestClose(void *context, void *handle, int *os_error_out)
{
	store_fixture_t *io = context;

	StoreEvent(io, STORE_EVENT_CLOSE);
	io->close_calls++;
	io->close_sequence = io->sequence;
	*os_error_out = 0;
	CHECK(handle == io);
	CHECK(io->open);
	io->open = 0;
	if (io->failure == STORE_FAIL_CLOSE || io->close_also_fails)
	{
		*os_error_out = EBADF;
		return -1;
	}
	return 0;
}

static sg_sidecar_revalidate_t TestRevalidate(void *context,
	const sg_rune_v3_header_t *rune, int *os_error_out)
{
	store_fixture_t *io = context;

	(void)rune;
	StoreEvent(io, STORE_EVENT_REVALIDATE);
	io->revalidate_calls++;
	io->revalidate_sequence = io->sequence;
	*os_error_out = 0;
	if (io->failure == STORE_FAIL_REVALIDATE_ERROR)
	{
		*os_error_out = ESTALE;
		return SG_SIDECAR_REVALIDATE_ERROR;
	}
	if (io->failure == STORE_FAIL_REVALIDATE_DRIFT)
		return SG_SIDECAR_REVALIDATE_DRIFT;
	if (io->failure == STORE_FAIL_REVALIDATE_INVALID)
		return (sg_sidecar_revalidate_t)7;
	return SG_SIDECAR_REVALIDATE_MATCH;
}

static int TestReplace(void *context, const char *temporary_path,
	const char *destination_path, int *os_error_out)
{
	store_fixture_t *io = context;
	store_event_t prior = io->last_event;

	StoreEvent(io, STORE_EVENT_RENAME);
	io->replace_calls++;
	io->replace_sequence = io->sequence;
	io->replacement_adjacent = prior == STORE_EVENT_REVALIDATE &&
		io->replace_sequence == io->revalidate_sequence + 1U;
	*os_error_out = 0;
	CHECK(strcmp(temporary_path, io->current_temporary) == 0);
	CHECK(strcmp(destination_path, io->expected_destination) == 0);
	CHECK(io->temp_exists);
	CHECK(!io->open);
	if (io->failure == STORE_FAIL_RENAME)
	{
		*os_error_out = EXDEV;
		return -1;
	}
	memcpy(io->destination, io->temporary, io->temporary_size);
	io->destination_size = io->temporary_size;
	io->temp_exists = 0;
	return 0;
}

static int TestSyncDirectory(void *context, const char *directory_path,
	int *os_error_out)
{
	store_fixture_t *io = context;

	StoreEvent(io, STORE_EVENT_DIRECTORY_SYNC);
	io->directory_sync_calls++;
	io->directory_sync_sequence = io->sequence;
	*os_error_out = 0;
	CHECK(strcmp(directory_path, io->expected_directory) == 0);
	CHECK(io->replace_calls == 1);
	if (io->failure == STORE_FAIL_DIRECTORY_SYNC)
	{
		*os_error_out = EIO;
		return -1;
	}
	return 0;
}

static int TestRemove(void *context, const char *path, int *os_error_out)
{
	store_fixture_t *io = context;

	StoreEvent(io, STORE_EVENT_CLEANUP);
	io->cleanup_calls++;
	*os_error_out = 0;
	CHECK(strcmp(path, io->current_temporary) == 0);
	CHECK(io->temp_exists);
	CHECK(!io->open);
	if (io->failure == STORE_FAIL_CLEANUP || io->cleanup_also_fails)
	{
		*os_error_out = EBUSY;
		return -1;
	}
	io->temp_exists = 0;
	return 0;
}

static sg_sidecar_store_ops_t TestOps(store_fixture_t *io)
{
	sg_sidecar_store_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.context = io;
	ops.temp_nonce = TestTempNonce;
	ops.open_exclusive = TestOpenExclusive;
	ops.write = TestWrite;
	ops.flush = TestFlush;
	ops.sync_file = TestSyncFile;
	ops.close_file = TestClose;
	ops.replace_file = TestReplace;
	ops.sync_directory = TestSyncDirectory;
	ops.remove_file = TestRemove;
	return ops;
}

static void TestDefaultTempNonces(void)
{
	sg_sidecar_store_ops_t ops;
	uint64_t first;
	uint64_t second;

	SG_SidecarV3DefaultStoreOps(&ops);
	CHECK(ops.temp_nonce != NULL);
	if (!ops.temp_nonce)
		return;
	first = ops.temp_nonce(ops.context);
	second = ops.temp_nonce(ops.context);
	CHECK(first != 0 && second != 0);
	CHECK(first != second);
}

static void InitFixture(store_fixture_t *io, const char *game_directory,
	sg_sidecar_kind_t kind, const sg_rune_v3_header_t *rune)
{
	const unsigned char old[] = { 'o', 'l', 'd' };
	char *separator;

	memset(io, 0, sizeof(*io));
	memcpy(io->original, old, sizeof(old));
	io->original_size = sizeof(old);
	memcpy(io->destination, old, sizeof(old));
	io->destination_size = sizeof(old);
	CHECK(SG_SidecarV3Path(io->expected_destination,
		sizeof(io->expected_destination), game_directory, kind, rune) ==
		SCD_OK);
	(void)snprintf(io->expected_directory, sizeof(io->expected_directory),
		"%s", io->expected_destination);
	separator = strrchr(io->expected_directory, '/');
	CHECK(separator != NULL);
	if (separator)
		*separator = '\0';
}

static int DestinationIsOld(const store_fixture_t *io)
{
	return io->destination_size == io->original_size &&
	       memcmp(io->destination, io->original, io->original_size) == 0;
}

static sg_sidecar_store_result_t RunInjected(store_fixture_t *io,
	const sg_rune_v3_header_t *rune, sg_sidecar_kind_t kind,
	const uint8_t *marks, size_t mark_count,
	const unsigned char *encoded, size_t encoded_size)
{
	sg_sidecar_store_ops_t ops = TestOps(io);

	return SG_SidecarV3StoreFile("game", kind, rune, marks, mark_count,
		encoded, encoded_size, TestRevalidate, io, &ops);
}

static void CheckPrecommitFailure(const store_fixture_t *io,
	const sg_sidecar_store_result_t *result, sg_sidecar_stage_t stage)
{
	CHECK(result->stage == stage);
	CHECK(!result->replacement_complete);
	CHECK(!result->durability_complete);
	CHECK(DestinationIsOld(io));
}

static void TestSuccessAndShortWrites(const sg_rune_v3_header_t *rune,
	const unsigned char *encoded, size_t encoded_size)
{
	store_fixture_t io;
	sg_sidecar_store_result_t result;

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_OK);
	CHECK(result.stage == SCS_DONE);
	CHECK(result.expected_file_size == encoded_size);
	CHECK(result.bytes_written == encoded_size);
	CHECK(result.temp_attempts == 1);
	CHECK(result.temp_created);
	CHECK(!result.cleanup_attempted);
	CHECK(result.replacement_complete);
	CHECK(result.durability_complete);
	CHECK(io.destination_size == encoded_size);
	CHECK(memcmp(io.destination, encoded, encoded_size) == 0);
	CHECK(io.flush_calls == 1 && io.file_sync_calls == 1);
	CHECK(io.close_calls == 1 && io.revalidate_calls == 1);
	CHECK(io.replace_calls == 1 && io.directory_sync_calls == 1);
	CHECK(io.replacement_adjacent);
	CHECK(io.flush_sequence < io.file_sync_sequence);
	CHECK(io.file_sync_sequence < io.close_sequence);
	CHECK(io.close_sequence < io.revalidate_sequence);
	CHECK(io.replace_sequence < io.directory_sync_sequence);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.write_limit = 3;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_OK);
	CHECK(result.bytes_written == encoded_size);
	CHECK(io.write_calls > 1);
	CHECK(io.destination_size == encoded_size);
	CHECK(memcmp(io.destination, encoded, encoded_size) == 0);
}

static void TestCollisions(const sg_rune_v3_header_t *rune,
	const unsigned char *encoded, size_t encoded_size)
{
	store_fixture_t io;
	sg_sidecar_store_result_t result;

	/* EEXIST is also the required behavior for a symlink collision. */
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.collision_count = 2;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_OK);
	CHECK(result.temp_attempts == 3);
	CHECK(io.cleanup_calls == 0);
	CHECK(io.open_calls == 3);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.collision_count = SG_SIDECAR_STORE_TEMP_ATTEMPTS;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_TEMP_EXHAUSTED);
	CHECK(result.stage == SCS_OPEN);
	CHECK(result.os_error == EEXIST);
	CHECK(result.temp_attempts == SG_SIDECAR_STORE_TEMP_ATTEMPTS);
	CHECK(!result.temp_created && !result.cleanup_attempted);
	CHECK(io.cleanup_calls == 0);
	CHECK(DestinationIsOld(&io));
}

static void TestWriteFailures(const sg_rune_v3_header_t *rune,
	const unsigned char *encoded, size_t encoded_size)
{
	store_fixture_t io;
	sg_sidecar_store_result_t result;

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_WRITE;
	io.fail_write_call = 1;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == ENOSPC);
	CheckPrecommitFailure(&io, &result, SCS_WRITE);
	CHECK(result.cleanup_attempted && result.cleanup_complete);
	CHECK(io.close_calls == 1 && io.cleanup_calls == 1);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_WRITE_ZERO;
	io.write_limit = 4;
	io.fail_write_call = 2;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == EIO);
	CHECK(result.bytes_written == 4);
	CheckPrecommitFailure(&io, &result, SCS_WRITE);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_WRITE_PARTIAL_ERROR;
	io.write_limit = 5;
	io.fail_write_call = 1;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == EIO);
	CHECK(result.bytes_written == 5);
	CheckPrecommitFailure(&io, &result, SCS_WRITE);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_WRITE_OVERSIZE;
	io.fail_write_call = 1;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_INTERNAL_ERROR);
	CheckPrecommitFailure(&io, &result, SCS_WRITE);
}

static void TestFailureStages(const sg_rune_v3_header_t *rune,
	const unsigned char *encoded, size_t encoded_size)
{
	store_fixture_t io;
	sg_sidecar_store_result_t result;

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_OPEN;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == EACCES);
	CheckPrecommitFailure(&io, &result, SCS_OPEN);
	CHECK(!result.temp_created && !result.cleanup_attempted);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_FLUSH;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR);
	CheckPrecommitFailure(&io, &result, SCS_FLUSH);
	CHECK(io.file_sync_calls == 0 && result.cleanup_complete);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_FILE_SYNC;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR);
	CheckPrecommitFailure(&io, &result, SCS_FILE_SYNC);
	CHECK(result.cleanup_complete);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_CLOSE;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.close_error == EBADF);
	CheckPrecommitFailure(&io, &result, SCS_CLOSE);
	CHECK(result.cleanup_complete);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_REVALIDATE_ERROR;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == ESTALE);
	CheckPrecommitFailure(&io, &result, SCS_RECHECK);
	CHECK(result.cleanup_complete && io.replace_calls == 0);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_REVALIDATE_DRIFT;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_STATE_DRIFT && result.os_error == 0);
	CheckPrecommitFailure(&io, &result, SCS_RECHECK);
	CHECK(result.cleanup_complete && io.replace_calls == 0);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_REVALIDATE_INVALID;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_INTERNAL_ERROR);
	CheckPrecommitFailure(&io, &result, SCS_RECHECK);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_RENAME;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == EXDEV);
	CheckPrecommitFailure(&io, &result, SCS_RENAME);
	CHECK(io.replacement_adjacent && result.cleanup_complete);

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_DIRECTORY_SYNC;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_IO_ERROR && result.os_error == EIO);
	CHECK(result.stage == SCS_DIRECTORY_SYNC);
	CHECK(result.replacement_complete && !result.durability_complete);
	CHECK(!result.cleanup_attempted && io.cleanup_calls == 0);
	CHECK(io.destination_size == encoded_size);
	CHECK(memcmp(io.destination, encoded, encoded_size) == 0);
}

static void TestSecondaryErrors(const sg_rune_v3_header_t *rune,
	const unsigned char *encoded, size_t encoded_size)
{
	store_fixture_t io;
	sg_sidecar_store_result_t result;

	/* A write primary remains authoritative when close also fails. */
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_WRITE;
	io.fail_write_call = 1;
	io.close_also_fails = 1;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.stage == SCS_WRITE && result.os_error == ENOSPC);
	CHECK(result.close_error == EBADF);
	CHECK(result.cleanup_attempted && result.cleanup_complete);
	CHECK(DestinationIsOld(&io));

	/* Cleanup failure is always secondary and leaves its precise OS error. */
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	io.failure = STORE_FAIL_REVALIDATE_DRIFT;
	io.cleanup_also_fails = 1;
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		encoded, encoded_size);
	CHECK(result.diagnostic == SCD_STATE_DRIFT);
	CHECK(result.stage == SCS_RECHECK && result.cleanup_error == EBUSY);
	CHECK(result.cleanup_attempted && !result.cleanup_complete);
	CHECK(io.temp_exists);
	CHECK(DestinationIsOld(&io));
}

static void TestPrevalidation(const sg_rune_v3_header_t *rune,
	const unsigned char *hmn, size_t hmn_size,
	const unsigned char *dpo, size_t dpo_size,
	const uint8_t *all_live, size_t seed_count)
{
	store_fixture_t io;
	sg_sidecar_store_result_t result;
	unsigned char corrupt[TEST_IMAGE_BYTES];
	uint8_t marks[TEST_IMAGE_BYTES];
	char game_directory[MAX_OSPATH];
	size_t fixed_path;
	size_t game_length;

	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		hmn, hmn_size - 1U);
	CHECK(result.diagnostic == SCD_BAD_FILE_SIZE);
	CHECK(result.stage == SCS_FILE_SIZE && io.open_calls == 0);
	CHECK(DestinationIsOld(&io));

	memcpy(corrupt, hmn, hmn_size);
	corrupt[hmn_size - 1U] ^= 1U;
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		corrupt, hmn_size);
	CHECK(result.diagnostic == SCD_BAD_PAYLOAD_CRC);
	CHECK(result.stage == SCS_PAYLOAD_CRC && io.open_calls == 0);

	memcpy(corrupt, hmn, hmn_size);
	corrupt[14] ^= 1U;
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_HUMAN, NULL, 0,
		corrupt, hmn_size);
	CHECK(result.diagnostic == SCD_BAD_HEADER_CRC);
	CHECK(result.stage == SCS_HEADER_CRC && io.open_calls == 0);

	InitFixture(&io, "game", SG_SIDECAR_ESCAPE, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_ESCAPE, NULL, 0,
		hmn, hmn_size);
	CHECK(result.diagnostic == SCD_BAD_MAGIC);
	CHECK(result.stage == SCS_HEADER && io.open_calls == 0);

	memset(marks, 1, seed_count);
	marks[0] = 0;
	InitFixture(&io, "game", SG_SIDECAR_DEFENSE, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_DEFENSE, marks,
		seed_count, dpo, dpo_size);
	CHECK(result.diagnostic == SCD_BAD_PAYLOAD_VALUE);
	CHECK(result.stage == SCS_PAYLOAD_VALUE);
	CHECK(result.plane == 0 && result.index == 0);
	CHECK(io.open_calls == 0);

	InitFixture(&io, "game", SG_SIDECAR_DEFENSE, rune);
	result = RunInjected(&io, rune, SG_SIDECAR_DEFENSE, NULL, 0,
		dpo, dpo_size);
	CHECK(result.diagnostic == SCD_INVALID_ARGUMENT);
	CHECK(result.stage == SCS_ARGUMENT && io.open_calls == 0);

	/* Destination fits exactly, but the mandatory temp suffix cannot. */
	fixed_path = strlen("/maps/") + strlen(rune->map_name) +
		strlen(SG_SidecarKindExtension(SG_SIDECAR_HUMAN));
	CHECK((size_t)MAX_OSPATH > fixed_path + 1U);
	game_length = (size_t)MAX_OSPATH - 1U - fixed_path;
	memset(game_directory, 'g', game_length);
	game_directory[game_length] = '\0';
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	{
		sg_sidecar_store_ops_t ops = TestOps(&io);

		result = SG_SidecarV3StoreFile(game_directory,
			SG_SIDECAR_HUMAN, rune, NULL, 0, hmn, hmn_size,
			TestRevalidate, &io, &ops);
	}
	CHECK(result.diagnostic == SCD_PATH_TOO_LONG);
	CHECK(result.stage == SCS_PATH && io.open_calls == 0);

	/* Mandatory callback and complete ops are rejected before open. */
	InitFixture(&io, "game", SG_SIDECAR_HUMAN, rune);
	{
		sg_sidecar_store_ops_t ops = TestOps(&io);

		result = SG_SidecarV3StoreFile("game", SG_SIDECAR_HUMAN,
			rune, NULL, 0, hmn, hmn_size, NULL, &io, &ops);
		CHECK(result.diagnostic == SCD_INVALID_ARGUMENT);
		CHECK(result.stage == SCS_ARGUMENT && io.open_calls == 0);
		ops.replace_file = NULL;
		result = SG_SidecarV3StoreFile("game", SG_SIDECAR_HUMAN,
			rune, NULL, 0, hmn, hmn_size, TestRevalidate, &io,
			&ops);
		CHECK(result.diagnostic == SCD_INVALID_ARGUMENT);
		CHECK(io.open_calls == 0);
	}
	(void)all_live;
}

#ifndef _WIN32
typedef struct real_revalidate_s
{
	sg_sidecar_revalidate_t result;
	size_t calls;
} real_revalidate_t;

static sg_sidecar_revalidate_t RealRevalidate(void *context,
	const sg_rune_v3_header_t *rune, int *os_error_out)
{
	real_revalidate_t *state = context;

	(void)rune;
	state->calls++;
	*os_error_out = 0;
	return state->result;
}

static int WriteBytes(const char *path, const unsigned char *data,
	size_t size)
{
	FILE *file = fopen(path, "wb");

	return file && fwrite(data, 1, size, file) == size &&
	       fclose(file) == 0;
}

static int ReadBytes(const char *path, unsigned char *data, size_t capacity,
	size_t *size_out)
{
	FILE *file = fopen(path, "rb");
	size_t count;
	int trailing;

	if (!file)
		return 0;
	count = fread(data, 1, capacity, file);
	trailing = fgetc(file);
	if (fclose(file) != 0 || trailing != EOF)
		return 0;
	*size_out = count;
	return 1;
}

static void TestRealFilesystem(const sg_rune_v3_header_t *rune,
	const unsigned char *encoded, size_t encoded_size)
{
	char root[] = "/tmp/sg-sidecar-store-XXXXXX";
	char maps[MAX_OSPATH];
	char destination[MAX_OSPATH];
	char collision[MAX_OSPATH];
	char sentinel[MAX_OSPATH];
	unsigned char observed[TEST_IMAGE_BYTES];
	const unsigned char old[] = { 'o', 'l', 'd' };
	const unsigned char guard[] = { 'g', 'u', 'a', 'r', 'd' };
	size_t observed_size = 0;
	sg_sidecar_store_result_t result;
	sg_sidecar_store_ops_t ops;
	real_revalidate_t authority;
	struct stat status;

	CHECK(mkdtemp(root) != NULL);
	CHECK(snprintf(maps, sizeof(maps), "%s/maps", root) > 0);
	CHECK(mkdir(maps, 0700) == 0);
	CHECK(SG_SidecarV3Path(destination, sizeof(destination), root,
		SG_SIDECAR_HUMAN, rune) == SCD_OK);
	SG_SidecarV3DefaultStoreOps(&ops);
	ops.temp_nonce = TestTempNonce;
	CHECK(snprintf(collision, sizeof(collision), "%s.tmp.%016llx.00",
		destination, (unsigned long long)TEST_TEMP_NONCE) > 0);
	CHECK(snprintf(sentinel, sizeof(sentinel), "%s/sentinel", root) > 0);
	CHECK(WriteBytes(destination, old, sizeof(old)));
	CHECK(WriteBytes(sentinel, guard, sizeof(guard)));
	CHECK(symlink(sentinel, collision) == 0);
	authority.result = SG_SIDECAR_REVALIDATE_MATCH;
	authority.calls = 0;
	result = SG_SidecarV3StoreFile(root, SG_SIDECAR_HUMAN, rune,
		NULL, 0, encoded, encoded_size, RealRevalidate, &authority, &ops);
	CHECK(result.diagnostic == SCD_OK && result.stage == SCS_DONE);
	CHECK(result.temp_attempts == 2);
	CHECK(result.replacement_complete && result.durability_complete);
	CHECK(authority.calls == 1);
	CHECK(ReadBytes(destination, observed, sizeof(observed), &observed_size));
	CHECK(observed_size == encoded_size);
	CHECK(memcmp(observed, encoded, encoded_size) == 0);
	CHECK(lstat(collision, &status) == 0 && S_ISLNK(status.st_mode));
	CHECK(ReadBytes(sentinel, observed, sizeof(observed), &observed_size));
	CHECK(observed_size == sizeof(guard));
	CHECK(memcmp(observed, guard, sizeof(guard)) == 0);
	CHECK(snprintf(maps, sizeof(maps), "%s.tmp.%016llx.01",
		destination, (unsigned long long)TEST_TEMP_NONCE) > 0);
	CHECK(lstat(maps, &status) != 0 && errno == ENOENT);

	/* A real drift removes only its owned .01 temp and preserves destination. */
	CHECK(WriteBytes(destination, old, sizeof(old)));
	authority.result = SG_SIDECAR_REVALIDATE_DRIFT;
	result = SG_SidecarV3StoreFile(root, SG_SIDECAR_HUMAN, rune,
		NULL, 0, encoded, encoded_size, RealRevalidate, &authority, &ops);
	CHECK(result.diagnostic == SCD_STATE_DRIFT);
	CHECK(result.temp_attempts == 2);
	CHECK(result.cleanup_attempted && result.cleanup_complete);
	CHECK(!result.replacement_complete);
	CHECK(ReadBytes(destination, observed, sizeof(observed), &observed_size));
	CHECK(observed_size == sizeof(old));
	CHECK(memcmp(observed, old, sizeof(old)) == 0);
	CHECK(lstat(maps, &status) != 0 && errno == ENOENT);

	CHECK(unlink(collision) == 0);
	CHECK(unlink(destination) == 0);
	CHECK(unlink(sentinel) == 0);
	CHECK(snprintf(maps, sizeof(maps), "%s/maps", root) > 0);
	CHECK(rmdir(maps) == 0);
	CHECK(rmdir(root) == 0);
}
#endif

int main(void)
{
	sg_rune_v3_header_t rune;
	unsigned char hmn[TEST_IMAGE_BYTES];
	unsigned char dpo[TEST_IMAGE_BYTES];
	uint8_t all_live[TEST_IMAGE_BYTES];
	size_t hmn_size = 0;
	size_t dpo_size = 0;

	CHECK(GoldenRuneHeader(&rune));
	TestDefaultTempNonces();
	CHECK(rune.num_seeds <= sizeof(all_live));
	memset(all_live, 1, rune.num_seeds);
	CHECK(EncodeImage(SG_SIDECAR_HUMAN, &rune, NULL, 0, 7,
		hmn, sizeof(hmn), &hmn_size));
	CHECK(EncodeImage(SG_SIDECAR_DEFENSE, &rune, all_live,
		rune.num_seeds, 7, dpo, sizeof(dpo), &dpo_size));

	TestSuccessAndShortWrites(&rune, hmn, hmn_size);
	TestCollisions(&rune, hmn, hmn_size);
	TestWriteFailures(&rune, hmn, hmn_size);
	TestFailureStages(&rune, hmn, hmn_size);
	TestSecondaryErrors(&rune, hmn, hmn_size);
	TestPrevalidation(&rune, hmn, hmn_size, dpo, dpo_size,
		all_live, rune.num_seeds);
#ifndef _WIN32
	TestRealFilesystem(&rune, hmn, hmn_size);
#endif

	if (failures)
	{
		fprintf(stderr, "sg_sidecar_store_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_sidecar_store_test: ok");
	return 0;
}
