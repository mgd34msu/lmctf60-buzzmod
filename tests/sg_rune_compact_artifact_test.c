#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "slipgate/sg_rune_compact_artifact.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Reuse the already-reviewed complete compact-wire fixture without copying a
 * second 400-line model construction into this boundary test. */
int sg_rune_compact_wire_fixture_main(int argc, char **argv);
#define main sg_rune_compact_wire_fixture_main
#include "sg_rune_compact_wire_test.c"
#undef main

#ifdef SG_RUNE_COMPACT_ARTIFACT_TEST_WRAP_CALLOC
static void FailNextCalloc(void)
{
	calloc_fail_after = calloc_count;
}
#endif

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #condition); \
			return 0; \
		} \
	} while (0)

typedef struct sink_s
{
	unsigned char *bytes;
	size_t capacity;
	size_t used;
	size_t maximum_write;
	int fail;
} sink_t;

static size_t SinkWrite(void *context, const unsigned char *bytes,
	size_t size, int *os_error_out)
{
	sink_t *sink = context;

	if (sink->fail)
	{
		*os_error_out = EIO;
		return 0U;
	}
	if (sink->maximum_write != 0U && size > sink->maximum_write)
		size = sink->maximum_write;
	if (size > sink->capacity - sink->used)
	{
		*os_error_out = ENOSPC;
		return 0U;
	}
	memcpy(sink->bytes + sink->used, bytes, size);
	sink->used += size;
	*os_error_out = 0;
	return size;
}

#ifndef _WIN32
static int WriteFile(const char *path, const unsigned char *bytes, size_t size)
{
	FILE *file = fopen(path, "wb");
	size_t written;
	int close_status;

	if (file == NULL)
		return 0;
	written = fwrite(bytes, 1U, size, file);
	close_status = fclose(file);
	return written == size && close_status == 0;
}

static int ReadFile(const char *path, unsigned char *bytes, size_t capacity,
	size_t *size_out)
{
	FILE *file = fopen(path, "rb");
	struct stat status;
	size_t size;
	size_t read_size;
	int close_status;

	if (file == NULL || fstat(fileno(file), &status) != 0 || status.st_size < 0 ||
		(uintmax_t)status.st_size > (uintmax_t)capacity)
	{
		if (file != NULL)
			(void)fclose(file);
		return 0;
	}
	size = (size_t)status.st_size;
	read_size = fread(bytes, 1U, size, file);
	close_status = fclose(file);
	if (read_size != size || close_status != 0)
		return 0;
	*size_out = size;
	return 1;
}
#endif

typedef struct fs_fault_s
{
	sg_rune_compact_artifact_fs_ops_t base;
	size_t maximum_write;
	int fail_sync;
	int fail_rename;
	char temporary_path[1024];
} fs_fault_t;

static void *FaultOpenTemp(void *context, const char *destination,
	char *temporary_path, size_t temporary_path_size, int *os_error_out)
{
	fs_fault_t *fault = context;
	void *file = fault->base.open_temp(fault->base.context, destination,
		temporary_path, temporary_path_size, os_error_out);

	if (file != NULL)
	{
		(void)snprintf(fault->temporary_path,
			sizeof(fault->temporary_path), "%s", temporary_path);
	}
	return file;
}

static size_t FaultWrite(void *context, void *file,
	const unsigned char *bytes, size_t size, int *os_error_out)
{
	fs_fault_t *fault = context;

	if (fault->maximum_write != 0U && size > fault->maximum_write)
		size = fault->maximum_write;
	return fault->base.write(fault->base.context, file, bytes, size,
		os_error_out);
}

static int FaultSync(void *context, void *file, int *os_error_out)
{
	fs_fault_t *fault = context;

	if (fault->fail_sync)
	{
		*os_error_out = EIO;
		return 0;
	}
	return fault->base.sync_file(fault->base.context, file, os_error_out);
}

static int FaultClose(void *context, void *file, int *os_error_out)
{
	fs_fault_t *fault = context;
	return fault->base.close_file(fault->base.context, file, os_error_out);
}

static int FaultRename(void *context, const char *temporary_path,
	const char *destination, int *os_error_out)
{
	fs_fault_t *fault = context;

	if (fault->fail_rename)
	{
		*os_error_out = EIO;
		return 0;
	}
	return fault->base.rename_file(fault->base.context, temporary_path,
		destination, os_error_out);
}

static int FaultRemove(void *context, const char *path, int *os_error_out)
{
	fs_fault_t *fault = context;
	return fault->base.remove_file(fault->base.context, path, os_error_out);
}

static int FaultDirectorySync(void *context, const char *destination,
	int *os_error_out)
{
	fs_fault_t *fault = context;
	return fault->base.sync_directory(fault->base.context, destination,
		os_error_out);
}

static sg_rune_compact_artifact_fs_ops_t FaultOps(fs_fault_t *fault)
{
	sg_rune_compact_artifact_fs_ops_t ops;

	SG_RuneCompactArtifactDefaultFsOps(&fault->base);
	memset(&ops, 0, sizeof(ops));
	ops.context = fault;
	ops.open_temp = FaultOpenTemp;
	ops.write = FaultWrite;
	ops.sync_file = FaultSync;
	ops.close_file = FaultClose;
	ops.rename_file = FaultRename;
	ops.remove_file = FaultRemove;
	ops.sync_directory = FaultDirectorySync;
	return ops;
}

typedef struct virtual_load_s
{
	const unsigned char *bytes;
	size_t bytes_size;
	size_t available_size;
	size_t offset;
	int64_t reported_size;
	int regular;
	int fail_open;
	int fail_stat;
	int fail_read;
	int fail_probe;
	int fail_close;
	int grow_during_read;
	int shrink_during_read;
	int read_mutated;
	int probe_extra;
	int close_calls;
} virtual_load_t;

static void *VirtualLoadOpen(void *context, const char *path,
	int *os_error_out)
{
	virtual_load_t *load = context;

	(void)path;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (load->fail_open)
	{
		if (os_error_out != NULL)
			*os_error_out = ENOENT;
		return NULL;
	}
	return load;
}

static int VirtualLoadStat(void *context, void *file, int64_t *size_out,
	int *regular_out, int *os_error_out)
{
	virtual_load_t *load = context;

	if (size_out != NULL)
		*size_out = load->reported_size;
	if (regular_out != NULL)
		*regular_out = load->regular;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != load || size_out == NULL || regular_out == NULL ||
		load->fail_stat)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	return 1;
}

static size_t VirtualLoadRead(void *context, void *file,
	unsigned char *output, size_t output_size, int *os_error_out)
{
	virtual_load_t *load = context;
	size_t remaining;
	size_t count;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != load || output == NULL || output_size == 0U || load->fail_read)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0U;
	}
	if (!load->read_mutated)
	{
		load->read_mutated = 1;
		if (load->grow_during_read)
		{
			if (load->bytes_size < SIZE_MAX)
				load->available_size = load->bytes_size + 1U;
			else
				load->probe_extra = 1;
		}
		if (load->shrink_during_read)
			load->available_size = load->bytes_size == 0U
				? 0U : load->bytes_size - 1U;
	}
	remaining = load->offset < load->available_size
		? load->available_size - load->offset : 0U;
	count = remaining < output_size ? remaining : output_size;
	if (count > load->bytes_size - load->offset)
		count = load->bytes_size - load->offset;
	memcpy(output, load->bytes + load->offset, count);
	load->offset += count;
	return count;
}

static int VirtualLoadProbe(void *context, void *file, int *has_extra_out,
	int *os_error_out)
{
	virtual_load_t *load = context;

	if (has_extra_out != NULL)
		*has_extra_out = 0;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != load || has_extra_out == NULL || load->fail_probe)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	*has_extra_out = load->probe_extra || load->offset < load->available_size;
	return 1;
}

static int VirtualLoadClose(void *context, void *file, int *os_error_out)
{
	virtual_load_t *load = context;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != load)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0;
	}
	load->close_calls++;
	if (load->fail_close)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	return 1;
}

static sg_rune_compact_artifact_load_ops_t VirtualLoadOps(void)
{
	sg_rune_compact_artifact_load_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.open_read = VirtualLoadOpen;
	ops.stat_file = VirtualLoadStat;
	ops.read = VirtualLoadRead;
	ops.probe = VirtualLoadProbe;
	ops.close_file = VirtualLoadClose;
	return ops;
}

#define FAKE_PUBLICATION_CAPACITY 65536U

typedef struct fake_publication_s
{
	unsigned char destination[FAKE_PUBLICATION_CAPACITY];
	unsigned char temporary[FAKE_PUBLICATION_CAPACITY];
	size_t destination_size;
	size_t temporary_size;
	size_t maximum_write;
	unsigned int event;
	unsigned int open_event;
	unsigned int write_event;
	unsigned int sync_event;
	unsigned int close_event;
	unsigned int rename_event;
	unsigned int directory_event;
	int temporary_exists;
	int handle_open;
	int fail_open;
	int fail_write;
	int zero_write;
	int oversized_write;
	int fail_sync;
	int fail_close;
	int fail_rename;
	int fail_remove;
	int fail_directory_sync;
	unsigned int open_calls;
	unsigned int write_calls;
	unsigned int sync_calls;
	unsigned int close_calls;
	unsigned int rename_calls;
	unsigned int remove_calls;
	unsigned int directory_calls;
} fake_publication_t;

static void *FakeOpenTemp(void *context, const char *destination,
	char *temporary_path, size_t temporary_path_size, int *os_error_out)
{
	fake_publication_t *fake = context;
	int written;

	(void)destination;
	fake->open_calls++;
	fake->open_event = ++fake->event;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (temporary_path == NULL || temporary_path_size == 0U)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return NULL;
	}
	written = snprintf(temporary_path, temporary_path_size, "fake-temp");
	if (written < 0 || (size_t)written >= temporary_path_size)
	{
		if (os_error_out != NULL)
			*os_error_out = ENAMETOOLONG;
		return NULL;
	}
	fake->temporary_exists = 1;
	fake->temporary_size = 0U;
	fake->handle_open = !fake->fail_open;
	if (fake->fail_open)
	{
		if (os_error_out != NULL)
			*os_error_out = EACCES;
		return NULL;
	}
	return fake;
}

static size_t FakeWrite(void *context, void *file,
	const unsigned char *bytes, size_t size, int *os_error_out)
{
	fake_publication_t *fake = context;
	size_t count;

	fake->write_calls++;
	if (fake->write_event == 0U)
		fake->write_event = ++fake->event;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fake || !fake->handle_open || bytes == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0U;
	}
	if (fake->fail_write)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0U;
	}
	if (fake->zero_write)
		return 0U;
	if (fake->oversized_write)
		return size == SIZE_MAX ? size : size + 1U;
	count = fake->maximum_write != 0U && size > fake->maximum_write
		? fake->maximum_write : size;
	if (count > FAKE_PUBLICATION_CAPACITY - fake->temporary_size)
	{
		if (os_error_out != NULL)
			*os_error_out = ENOSPC;
		return 0U;
	}
	memcpy(fake->temporary + fake->temporary_size, bytes, count);
	fake->temporary_size += count;
	return count;
}

static int FakeSync(void *context, void *file, int *os_error_out)
{
	fake_publication_t *fake = context;

	fake->sync_calls++;
	fake->sync_event = ++fake->event;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fake || !fake->handle_open || fake->fail_sync)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	return 1;
}

static int FakeClose(void *context, void *file, int *os_error_out)
{
	fake_publication_t *fake = context;

	fake->close_calls++;
	fake->close_event = ++fake->event;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fake || !fake->handle_open)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0;
	}
	fake->handle_open = 0;
	if (fake->fail_close)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	return 1;
}

static int FakeRename(void *context, const char *temporary_path,
	const char *destination, int *os_error_out)
{
	fake_publication_t *fake = context;

	(void)temporary_path;
	(void)destination;
	fake->rename_calls++;
	fake->rename_event = ++fake->event;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (!fake->temporary_exists || fake->fail_rename)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	memcpy(fake->destination, fake->temporary, fake->temporary_size);
	fake->destination_size = fake->temporary_size;
	fake->temporary_exists = 0;
	return 1;
}

static int FakeRemove(void *context, const char *path, int *os_error_out)
{
	fake_publication_t *fake = context;

	(void)path;
	fake->remove_calls++;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (fake->fail_remove)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	fake->temporary_exists = 0;
	fake->temporary_size = 0U;
	return 1;
}

static int FakeDirectorySync(void *context, const char *destination,
	int *os_error_out)
{
	fake_publication_t *fake = context;

	(void)destination;
	fake->directory_calls++;
	fake->directory_event = ++fake->event;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (fake->fail_directory_sync)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0;
	}
	return 1;
}

static sg_rune_compact_artifact_fs_ops_t FakePublicationOps(void)
{
	sg_rune_compact_artifact_fs_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.open_temp = FakeOpenTemp;
	ops.write = FakeWrite;
	ops.sync_file = FakeSync;
	ops.close_file = FakeClose;
	ops.rename_file = FakeRename;
	ops.remove_file = FakeRemove;
	ops.sync_directory = FakeDirectorySync;
	return ops;
}

static void InitVirtualLoad(virtual_load_t *load,
	const unsigned char *bytes, size_t size)
{
	memset(load, 0, sizeof(*load));
	load->bytes = bytes;
	load->bytes_size = size;
	load->available_size = size;
	load->reported_size = (int64_t)size;
	load->regular = 1;
}

static int TestInjectedLoader(void)
{
	test_fixture_t fixture;
	sg_rune_compact_artifact_loader_t loader =
		SG_RUNE_COMPACT_ARTIFACT_LOADER_INITIALIZER;
	sg_rune_compact_artifact_load_ops_t ops = VirtualLoadOps();
	sg_rune_compact_artifact_load_result_t result;
	sg_rune_compact_wire_error_t error;
	const sg_rune_compact_model_t *published;
	virtual_load_t load;
	unsigned char *image = NULL;
	size_t size = 0U;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactArtifactEncode(&fixture.model, &image, &size,
		&error));
	CHECK(image != NULL && size > 0U);
	CHECK(SG_RuneCompactArtifactLoaderInit(&loader));
	ops.context = &load;
	InitVirtualLoad(&load, image, size);
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_OK);
	published = SG_RuneCompactArtifactLoaderSnapshot(&loader);
	CHECK(published != NULL);

#ifdef SG_RUNE_COMPACT_ARTIFACT_TEST_WRAP_CALLOC
	/* Decode allocates only after inspect; fail that allocation deterministically. */
	FailNextCalloc();
	result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, image, size,
		&fixture.model.identity);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_LOAD_ALLOCATION_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_WIRE);
	CHECK(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	FailNextCalloc();
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_LOAD_ALLOCATION_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_WIRE);
	CHECK(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
	CHECK(load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
#endif

	/* The stat bound is exceeded by a real post-stat growth observed by probe. */
	InitVirtualLoad(&load, image, size);
	load.grow_during_read = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_GREW);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_GROWTH);
	CHECK(load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.grow_during_read = 1;
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_GREW);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	/* A post-stat shrink terminates the exact-bound read without publication. */
	InitVirtualLoad(&load, image, size);
	load.shrink_during_read = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ);
	CHECK(result.bytes_read == size - 1U);
	CHECK(load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.shrink_during_read = 1;
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ);
	CHECK(result.bytes_read == size - 1U);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	/* Non-regular handles are rejected before allocation or reading. */
	InitVirtualLoad(&load, image, size);
	load.regular = 0;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_KIND);
	CHECK(load.offset == 0U && load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.regular = 0;
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_KIND);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	/* The four-GiB cap is checked before malloc; this never allocates it. */
	InitVirtualLoad(&load, image, size);
	load.reported_size = (int64_t)(SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES +
		UINT64_C(1));
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_SIZE);
	CHECK(load.offset == 0U && load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.reported_size = (int64_t)(SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES +
		UINT64_C(1));
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_SIZE);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	InitVirtualLoad(&load, image, size);
	load.fail_open = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_OPEN);

	InitVirtualLoad(&load, image, size);
	load.fail_stat = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_STAT);
	CHECK(load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.fail_stat = 1;
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_STAT);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	InitVirtualLoad(&load, image, size);
	load.fail_read = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ);
	CHECK(result.bytes_read == 0U && load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.fail_read = 1;
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	InitVirtualLoad(&load, image, size);
	load.fail_probe = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ);
	CHECK(result.bytes_read == size && load.close_calls == 1);
	CHECK(result.close_error == 0);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	InitVirtualLoad(&load, image, size);
	load.fail_probe = 1;
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	InitVirtualLoad(&load, image, size);
	load.fail_close = 1;
	result = SG_RuneCompactArtifactLoaderLoadFileWithOps(&loader, "virtual",
		&fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_CLOSE);
	CHECK(result.close_error == EIO);
	CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

	if ((uint64_t)SIZE_MAX > SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES)
	{
		result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, image,
			(size_t)(SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES + UINT64_C(1)),
			&fixture.model.identity);
		CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_SIZE);
		CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_SIZE);
		CHECK(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	}

	SG_RuneCompactArtifactLoaderDestroy(&loader);
	free(image);
	return 1;
}

static int InitFakePublication(fake_publication_t *fake,
	const unsigned char *bytes, size_t size)
{
	memset(fake, 0, sizeof(*fake));
	if (size > FAKE_PUBLICATION_CAPACITY)
		return 0;
	memcpy(fake->destination, bytes, size);
	fake->destination_size = size;
	return 1;
}

static int FakeDestinationMatches(const fake_publication_t *fake,
	const unsigned char *bytes, size_t size)
{
	return fake->destination_size == size && memcmp(fake->destination, bytes,
		size) == 0;
}

static int TestInjectedPublication(void)
{
	test_fixture_t old_fixture;
	test_fixture_t new_fixture;
	sg_rune_compact_artifact_fs_ops_t ops = FakePublicationOps();
	sg_rune_compact_artifact_publication_result_t result;
	sg_rune_compact_wire_error_t error;
	fake_publication_t fake;
	unsigned char *old_image = NULL;
	unsigned char *new_image = NULL;
	size_t old_size = 0U;
	size_t new_size = 0U;

	init_fixture(&old_fixture);
	init_fixture(&new_fixture);
	new_fixture.model.identity.bsp_sha256[0] ^= UINT8_C(1);
	CHECK(SG_RuneCompactArtifactEncode(&old_fixture.model, &old_image,
		&old_size, &error));
	CHECK(SG_RuneCompactArtifactEncode(&new_fixture.model, &new_image,
		&new_size, &error));
	CHECK(old_size <= FAKE_PUBLICATION_CAPACITY &&
		new_size <= FAKE_PUBLICATION_CAPACITY);
	ops.context = &fake;

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.maximum_write = 3U;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_OK);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DONE);
	CHECK(result.published == 1 && result.durable == 1);
	CHECK(FakeDestinationMatches(&fake, new_image, new_size));
	CHECK(fake.temporary_exists == 0 && fake.write_calls > 1U);
	CHECK(fake.open_event < fake.write_event &&
		fake.write_event < fake.sync_event &&
		fake.sync_event < fake.close_event &&
		fake.close_event < fake.rename_event &&
		fake.rename_event < fake.directory_event);

#ifdef SG_RUNE_COMPACT_ARTIFACT_TEST_WRAP_CALLOC
	CHECK(InitFakePublication(&fake, old_image, old_size));
	FailNextCalloc();
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED);
	CHECK(result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE);
	CHECK(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
	CHECK(result.published == 0 && fake.open_calls == 0U);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));
#endif

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.fail_open = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_TEMP_OPEN_FAILED);
	CHECK(result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_TEMP_OPEN);
	CHECK(result.published == 0 && fake.remove_calls == 1U &&
		fake.temporary_exists == 0);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.fail_write = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE);
	CHECK(result.published == 0 && fake.rename_calls == 0U &&
		fake.temporary_exists == 0);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.zero_write = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE);
	CHECK(result.published == 0 && fake.temporary_exists == 0);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.oversized_write = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE);
	CHECK(result.published == 0 && fake.temporary_exists == 0);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.fail_close = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_FILE_CLOSE_FAILED);
	CHECK(result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_FILE_CLOSE);
	CHECK(result.published == 0 && fake.temporary_exists == 0);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.fail_write = 1;
	fake.fail_remove = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE);
	CHECK(result.cleanup_error == EIO && fake.temporary_exists == 1);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	CHECK(InitFakePublication(&fake, old_image, old_size));
	fake.fail_directory_sync = 1;
	result = SG_RuneCompactArtifactPublish("virtual", new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_DIRECTORY_SYNC_FAILED);
	CHECK(result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DIRECTORY_SYNC);
	CHECK(result.published == 1 && result.durable == 0);
	CHECK(fake.temporary_exists == 0 &&
		FakeDestinationMatches(&fake, new_image, new_size));

	free(new_image);
	free(old_image);
	return 1;
}

static int TestStageDiagnostics(void)
{
	test_fixture_t fixture;
	unsigned char sink_bytes[FAKE_PUBLICATION_CAPACITY];
	sink_t sink;
	sg_rune_compact_artifact_write_result_t write_result;
	sg_rune_compact_artifact_publication_result_t publish_result;
	sg_rune_compact_artifact_fs_ops_t ops = FakePublicationOps();
	sg_rune_compact_wire_error_t error;
	fake_publication_t fake;
	unsigned char *old_image = NULL;
	size_t old_size = 0U;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactArtifactEncode(&fixture.model, &old_image,
		&old_size, &error));
	CHECK(old_size <= FAKE_PUBLICATION_CAPACITY);
	memset(&sink, 0, sizeof(sink));
	sink.bytes = sink_bytes;
	sink.capacity = sizeof(sink_bytes);

#ifdef SG_RUNE_COMPACT_ARTIFACT_TEST_WRAP_CALLOC
	/* WriteModel maps validation decode OOM without mislabeling it as corrupt. */
	FailNextCalloc();
	write_result = SG_RuneCompactArtifactWriteModel(&fixture.model,
		SinkWrite, &sink);
	CHECK(write_result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_WRITE_ALLOCATION_FAILED);
	CHECK(write_result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_VALIDATE);
	CHECK(write_result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
	CHECK(sink.used == 0U);
	CHECK(InitFakePublication(&fake, old_image, old_size));
	ops.context = &fake;
	FailNextCalloc();
	publish_result = SG_RuneCompactArtifactPublishModel("virtual",
		&fixture.model, &ops);
	CHECK(publish_result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED);
	CHECK(publish_result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE);
	CHECK(publish_result.wire_error.code ==
		SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
	CHECK(publish_result.published == 0 && fake.open_calls == 0U);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));
#endif

	/* A missing counted array fails during measure, before allocation or I/O. */
	{
		test_fixture_t invalid = fixture;
		invalid.model.cells = NULL;
		write_result = SG_RuneCompactArtifactWriteModel(&invalid.model,
			SinkWrite, &sink);
		CHECK(write_result.diagnostic ==
			SG_RUNE_COMPACT_ARTIFACT_WRITE_WIRE_REJECTED);
		CHECK(write_result.stage ==
			SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_MEASURE);
		CHECK(write_result.wire_error.code ==
			SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT);
		CHECK(InitFakePublication(&fake, old_image, old_size));
		ops.context = &fake;
		publish_result = SG_RuneCompactArtifactPublishModel("virtual",
			&invalid.model, &ops);
		CHECK(publish_result.diagnostic ==
			SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED);
		CHECK(publish_result.stage ==
			SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_MEASURE);
		CHECK(fake.open_calls == 0U);
		CHECK(FakeDestinationMatches(&fake, old_image, old_size));
	}

	/* Wire-representable reserved data fails in the encode/inspect stage. */
	fixture.cells[0].reserved[0] = 1U;
	write_result = SG_RuneCompactArtifactWriteModel(&fixture.model,
		SinkWrite, &sink);
	CHECK(write_result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_WRITE_WIRE_REJECTED);
	CHECK(write_result.stage == SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ENCODE);
	CHECK(write_result.wire_error.code ==
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED);
	CHECK(InitFakePublication(&fake, old_image, old_size));
	ops.context = &fake;
	publish_result = SG_RuneCompactArtifactPublishModel("virtual",
		&fixture.model, &ops);
	CHECK(publish_result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED);
	CHECK(publish_result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ENCODE);
	CHECK(fake.open_calls == 0U);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));
	fixture.cells[0].reserved[0] = 0U;

	fixture.cells[0].bounds.maxs.value[0] = -1;
	write_result = SG_RuneCompactArtifactWriteModel(&fixture.model,
		SinkWrite, &sink);
	CHECK(write_result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_WRITE_WIRE_REJECTED);
	CHECK(write_result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ENCODE);
	CHECK(write_result.wire_error.code ==
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT);
	CHECK(InitFakePublication(&fake, old_image, old_size));
	ops.context = &fake;
	publish_result = SG_RuneCompactArtifactPublishModel("virtual",
		&fixture.model, &ops);
	CHECK(publish_result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED);
	CHECK(publish_result.stage ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ENCODE);
	CHECK(fake.open_calls == 0U);
	CHECK(FakeDestinationMatches(&fake, old_image, old_size));

	free(old_image);
	return 1;
}

static int TestEncodeAndSink(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_artifact_write_result_t result;
	unsigned char *encoded = NULL;
	unsigned char *streamed;
	size_t encoded_size = 0U;
	sink_t sink;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactArtifactEncode(&fixture.model, &encoded,
		&encoded_size, &error));
	CHECK(encoded != NULL && encoded_size > 0U);
	streamed = malloc(encoded_size);
	CHECK(streamed != NULL);
	memset(&sink, 0, sizeof(sink));
	sink.bytes = streamed;
	sink.capacity = encoded_size;
	sink.maximum_write = 7U;
	result = SG_RuneCompactArtifactWriteModel(&fixture.model, SinkWrite,
		&sink);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_WRITE_OK);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_SINK);
	CHECK(result.image_size == encoded_size);
	CHECK(result.bytes_transferred == encoded_size);
	CHECK(sink.used == encoded_size);
	CHECK(memcmp(encoded, streamed, encoded_size) == 0);
	memset(streamed, 0xa5, encoded_size);
	sink.used = 0U;
	sink.fail = 1;
	result = SG_RuneCompactArtifactWriteModel(&fixture.model, SinkWrite,
		&sink);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_WRITE_SINK_FAILED);
	CHECK(result.stage == SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_SINK);
	CHECK(result.bytes_transferred == 0U);
	CHECK(sink.used == 0U);
	free(streamed);
	free(encoded);
	return 1;
}

static int TestLoaderTransactionalFailures(void)
{
	test_fixture_t fixture;
	sg_rune_compact_artifact_loader_t loader =
		SG_RUNE_COMPACT_ARTIFACT_LOADER_INITIALIZER;
	sg_rune_compact_artifact_load_result_t result;
	const sg_rune_compact_model_t *published;
	unsigned char *image = NULL;
	unsigned char *copy = NULL;
	size_t size;
	size_t written;
	sg_rune_compact_wire_error_t error;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactArtifactLoaderInit(&loader));
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	CHECK(size > 800U);
	image = malloc(size);
	copy = malloc(size + 1U);
	if (image == NULL || copy == NULL)
	{
		free(copy);
		free(image);
		return 0;
	}
	#define CHECK_ALLOCATED(condition) \
		do { \
			if (!(condition)) { \
				fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
					__FILE__, __LINE__, #condition); \
				SG_RuneCompactArtifactLoaderDestroy(&loader); \
				free(copy); \
				free(image); \
				return 0; \
			} \
		} while (0)
	CHECK_ALLOCATED(SG_RuneCompactWireEncode(&fixture.model, image, size, &written,
		&error));
	result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, image, size,
		&fixture.model.identity);
	if (result.diagnostic != SG_RUNE_COMPACT_ARTIFACT_LOAD_OK)
	{
		SG_RuneCompactArtifactLoaderDestroy(&loader);
		free(copy);
		free(image);
		return 0;
	}
	published = SG_RuneCompactArtifactLoaderSnapshot(&loader);
	CHECK_ALLOCATED(published != NULL);
	memcpy(copy, image, size);
	result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, copy, size - 1U,
		&fixture.model.identity);
	CHECK_ALLOCATED(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_WIRE_REJECTED);
	CHECK_ALLOCATED(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED);
	CHECK_ALLOCATED(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	memcpy(copy, image, size);
	copy[0] ^= 1U;
	result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, copy, size,
		&fixture.model.identity);
	CHECK_ALLOCATED(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT);
	CHECK_ALLOCATED(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	memcpy(copy, image, size);
	copy[size] = 0U;
	result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, copy, size + 1U,
		&fixture.model.identity);
	CHECK_ALLOCATED(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT);
	CHECK_ALLOCATED(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	memcpy(copy, image, size);
	copy[TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_COUNT * TEST_DESCRIPTOR_SIZE] ^= 1U;
	result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, copy, size,
		&fixture.model.identity);
	CHECK_ALLOCATED(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH);
	CHECK_ALLOCATED(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
	memcpy(copy, image, size);
	{
		sg_rune_compact_identity_t wrong = fixture.model.identity;
		wrong.gravity_law_id ^= 1U;
		result = SG_RuneCompactArtifactLoaderLoadBytes(&loader, copy, size,
			&wrong);
	}
	CHECK_ALLOCATED(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH);
	CHECK_ALLOCATED(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);

#ifndef _WIN32
	{
		char file_path[] = "/tmp/sg-rune-compact-loader-XXXXXX";
		int file_descriptor;
		FILE *file;

		file_descriptor = mkstemp(file_path);
		CHECK_ALLOCATED(file_descriptor >= 0);
		CHECK_ALLOCATED(close(file_descriptor) == 0);
		CHECK_ALLOCATED(WriteFile(file_path, image, size));
		result = SG_RuneCompactArtifactLoaderLoadFile(&loader, file_path,
			&fixture.model.identity);
		CHECK_ALLOCATED(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_OK);
		published = SG_RuneCompactArtifactLoaderSnapshot(&loader);
		CHECK_ALLOCATED(published != NULL);
		file = fopen(file_path, "ab");
		CHECK_ALLOCATED(file != NULL);
		CHECK_ALLOCATED(fputc(0, file) != EOF);
		CHECK_ALLOCATED(fclose(file) == 0);
		result = SG_RuneCompactArtifactLoaderLoadFile(&loader, file_path,
			&fixture.model.identity);
		CHECK_ALLOCATED(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_WIRE_REJECTED);
		CHECK_ALLOCATED(result.wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT);
		CHECK_ALLOCATED(SG_RuneCompactArtifactLoaderSnapshot(&loader) == published);
		CHECK_ALLOCATED(unlink(file_path) == 0);
	}
#endif
	#undef CHECK_ALLOCATED
	SG_RuneCompactArtifactLoaderDestroy(&loader);
	free(copy);
	free(image);
	return 1;
}

#ifndef _WIN32
static int TestPublication(void)
{
	test_fixture_t old_fixture;
	test_fixture_t new_fixture;
	sg_rune_compact_artifact_publication_result_t result;
	sg_rune_compact_artifact_load_result_t load_result;
	sg_rune_compact_identity_t accepted_identity;
	sg_rune_compact_artifact_loader_t loader =
		SG_RUNE_COMPACT_ARTIFACT_LOADER_INITIALIZER;
	sg_rune_compact_wire_error_t error;
	unsigned char *old_image = NULL;
	unsigned char *new_image = NULL;
	unsigned char *observed;
	size_t observed_capacity;
	unsigned char stale[] = "stale temporary";
	size_t old_size = 0U;
	size_t new_size = 0U;
	size_t observed_size = 0U;
	char directory[] = "/tmp/sg-rune-compact-artifact-XXXXXX";
	char destination[512];
	char stale_path[512];
	fs_fault_t fault;
	sg_rune_compact_artifact_fs_ops_t ops;

	CHECK(mkdtemp(directory) != NULL);
	CHECK(snprintf(destination, sizeof(destination), "%s/current.rune",
		directory) > 0);
	CHECK(snprintf(stale_path, sizeof(stale_path), "%s.tmp.stale",
		destination) > 0);
	init_fixture(&old_fixture);
	init_fixture(&new_fixture);
	new_fixture.model.identity.bsp_sha256[0] ^= UINT8_C(1);
	CHECK(SG_RuneCompactArtifactEncode(&old_fixture.model, &old_image,
		&old_size, &error));
	CHECK(SG_RuneCompactArtifactEncode(&new_fixture.model, &new_image,
		&new_size, &error));
	observed_capacity = old_size > new_size ? old_size : new_size;
	observed = malloc(observed_capacity);
	CHECK(observed != NULL);
	CHECK(WriteFile(destination, old_image, old_size));
	CHECK(WriteFile(stale_path, stale, sizeof(stale)));
	result = SG_RuneCompactArtifactPublish(destination, new_image, new_size,
		&new_fixture.model.identity, NULL);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_OK);
	CHECK(result.published == 1 && result.durable == 1);
	CHECK(ReadFile(destination, observed, observed_capacity, &observed_size));
	CHECK(observed_size == new_size);
	CHECK(memcmp(observed, new_image, new_size) == 0);

	/* Candidate validation happens before any temp file is opened. */
	CHECK(new_size > 800U);
	new_image[TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_COUNT * TEST_DESCRIPTOR_SIZE] ^= 1U;
	result = SG_RuneCompactArtifactPublish(destination, new_image, new_size,
		&new_fixture.model.identity, NULL);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED);
	CHECK(!result.published);
	new_image[TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_COUNT * TEST_DESCRIPTOR_SIZE] ^= 1U;
	CHECK(ReadFile(destination, observed, observed_capacity, &observed_size));
	CHECK(observed_size == new_size);
	CHECK(memcmp(observed, new_image, new_size) == 0);
	/* The destination is checked through the exact-bound loader below. */
	CHECK(SG_RuneCompactArtifactLoaderInit(&loader));
	load_result = SG_RuneCompactArtifactLoaderLoadFile(&loader, destination,
		&new_fixture.model.identity);
	CHECK(load_result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_OK);
	SG_RuneCompactArtifactLoaderDestroy(&loader);
	memset(&accepted_identity, 0, sizeof(accepted_identity));
	CHECK(SG_RuneCompactArtifactLoaderInit(&loader));
	load_result = SG_RuneCompactArtifactLoaderLoadAcceptedFile(&loader,
		destination, &accepted_identity);
	CHECK(load_result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_OK);
	CHECK(SG_RuneCompactIdentityMatches(&accepted_identity,
		&new_fixture.model.identity));
	CHECK(ReadFile(stale_path, observed, sizeof(stale), &observed_size));
	CHECK(observed_size == sizeof(stale));
	CHECK(memcmp(observed, stale, sizeof(stale)) == 0);
	SG_RuneCompactArtifactLoaderDestroy(&loader);

	/* A short-write implementation must still produce one exact image. */
	CHECK(WriteFile(destination, old_image, old_size));
	memset(&fault, 0, sizeof(fault));
	ops = FaultOps(&fault);
	fault.maximum_write = 5U;
	result = SG_RuneCompactArtifactPublish(destination, new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_OK);
	CHECK(result.bytes_transferred == new_size);
	CHECK(ReadFile(destination, observed, observed_capacity, &observed_size));
	CHECK(observed_size == new_size);
	CHECK(memcmp(observed, new_image, new_size) == 0);

	/* File synchronization failure is before rename; the old artifact wins. */
	CHECK(WriteFile(destination, old_image, old_size));
	memset(&fault, 0, sizeof(fault));
	ops = FaultOps(&fault);
	fault.fail_sync = 1;
	result = SG_RuneCompactArtifactPublish(destination, new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_FILE_SYNC_FAILED);
	CHECK(result.published == 0);
	CHECK(fault.temporary_path[0] != '\0');
	CHECK(access(fault.temporary_path, F_OK) != 0);
	CHECK(ReadFile(destination, observed, observed_capacity, &observed_size));
	CHECK(observed_size == old_size);
	CHECK(memcmp(observed, old_image, old_size) == 0);

	/* Rename failure also leaves the old destination untouched. */
	CHECK(WriteFile(destination, old_image, old_size));
	memset(&fault, 0, sizeof(fault));
	ops = FaultOps(&fault);
	fault.fail_rename = 1;
	result = SG_RuneCompactArtifactPublish(destination, new_image, new_size,
		&new_fixture.model.identity, &ops);
	CHECK(result.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_RENAME_FAILED);
	CHECK(result.published == 0);
	CHECK(access(fault.temporary_path, F_OK) != 0);
	CHECK(ReadFile(destination, observed, observed_capacity, &observed_size));
	CHECK(observed_size == old_size);
	CHECK(memcmp(observed, old_image, old_size) == 0);

	free(observed);
	free(new_image);
	free(old_image);
	(void)unlink(stale_path);
	(void)unlink(destination);
	(void)rmdir(directory);
	return 1;
}
#endif

int main(void)
{
	CHECK(TestInjectedLoader());
	CHECK(TestInjectedPublication());
	CHECK(TestStageDiagnostics());
	CHECK(TestEncodeAndSink());
	CHECK(TestLoaderTransactionalFailures());
#ifndef _WIN32
	CHECK(TestPublication());
#endif
	puts("sg_rune_compact_artifact_test: ok");
	return 0;
}
