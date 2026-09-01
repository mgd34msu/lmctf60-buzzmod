/* v12 sidecar store atomicity and retry tests. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_sidecar_store.h"

#define IMAGE_CAPACITY (SG_SIDECAR_HEADER_BYTES + 8U)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef enum failure_point_e
{
	FAIL_NONE = 0,
	FAIL_OPEN,
	FAIL_WRITE,
	FAIL_FLUSH,
	FAIL_SYNC,
	FAIL_CLOSE,
	FAIL_RENAME,
	FAIL_DIRECTORY_SYNC
} failure_point_t;

typedef struct store_fixture_s
{
	unsigned char temporary[IMAGE_CAPACITY];
	size_t temporary_size;
	unsigned char destination[IMAGE_CAPACITY];
	size_t destination_size;
	int open_collisions;
	failure_point_t failure;
	unsigned int open_calls;
	unsigned int remove_calls;
	unsigned int revalidate_calls;
} store_fixture_t;

typedef struct revalidate_fixture_s
{
	store_fixture_t *store;
	const sg_rune_compact_wire_info_t *expected;
	sg_sidecar_revalidate_t result;
} revalidate_fixture_t;

static void InitInfo(sg_rune_compact_wire_info_t *info)
{
	uint32_t index;

	memset(info, 0, sizeof(*info));
	info->wire_version = SG_RUNE_COMPACT_WIRE_VERSION;
	info->model_version = SG_RUNE_COMPACT_MODEL_VERSION;
	info->analytic_version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	info->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	info->image_bytes = UINT64_C(4096);
	info->checksum = UINT32_C(0xdeadbeef);
	for (index = 0U; index < 32U; index++)
		info->identity.bsp_sha256[index] = (uint8_t)(index + 4U);
	info->identity.bsp_bytes = UINT64_C(12345);
	info->identity.entity_semantics_id = UINT64_C(17);
	info->identity.physics.frame_ms = 100U;
	info->identity.physics.substep_ms = 8U;
}

static int Encode(const sg_rune_compact_wire_info_t *info,
	const unsigned char *payload, size_t payload_size, unsigned char *image,
	size_t *image_size_out)
{
	return SG_SidecarEncode(SG_SIDECAR_HUMAN, info, payload, payload_size,
		image, IMAGE_CAPACITY, image_size_out) == SCD_OK;
}

static uint64_t TempNonce(void *context)
{
	(void)context;
	return UINT64_C(0x0123456789abcdef);
}

static void *OpenExclusive(void *context, const char *path,
	int *os_error_out)
{
	store_fixture_t *fixture = context;

	fixture->open_calls++;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (path == NULL || strstr(path, ".tmp.") == NULL ||
		fixture->failure == FAIL_OPEN)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return NULL;
	}
	if (fixture->open_collisions > 0)
	{
		fixture->open_collisions--;
		if (os_error_out != NULL)
			*os_error_out = EEXIST;
		return NULL;
	}
	fixture->temporary_size = 0U;
	return fixture;
}

static size_t Write(void *context, void *file, const unsigned char *data,
	size_t data_size, int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fixture || data == NULL ||
		data_size > sizeof(fixture->temporary) - fixture->temporary_size ||
		fixture->failure == FAIL_WRITE)
	{
		if (os_error_out != NULL)
			*os_error_out = EIO;
		return 0U;
	}
	memcpy(fixture->temporary + fixture->temporary_size, data, data_size);
	fixture->temporary_size += data_size;
	return data_size;
}

static int Flush(void *context, void *file, int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = fixture->failure == FAIL_FLUSH ? EIO : 0;
	return file == fixture && fixture->failure != FAIL_FLUSH ? 0 : -1;
}

static int SyncFile(void *context, void *file, int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = fixture->failure == FAIL_SYNC ? EIO : 0;
	return file == fixture && fixture->failure != FAIL_SYNC ? 0 : -1;
}

static int Close(void *context, void *file, int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = fixture->failure == FAIL_CLOSE ? EIO : 0;
	return file == fixture && fixture->failure != FAIL_CLOSE ? 0 : -1;
}

static int Replace(void *context, const char *temporary_path,
	const char *destination_path, int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = fixture->failure == FAIL_RENAME ? EIO : 0;
	if (temporary_path == NULL || destination_path == NULL ||
		fixture->failure == FAIL_RENAME)
		return -1;
	memcpy(fixture->destination, fixture->temporary, fixture->temporary_size);
	fixture->destination_size = fixture->temporary_size;
	fixture->temporary_size = 0U;
	return 0;
}

static int SyncDirectory(void *context, const char *directory_path,
	int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = fixture->failure == FAIL_DIRECTORY_SYNC ? EIO : 0;
	return directory_path != NULL &&
		(strcmp(directory_path, "bundle") == 0 ||
			strcmp(directory_path, ".") == 0) &&
		fixture->failure != FAIL_DIRECTORY_SYNC ? 0 : -1;
}

static int Remove(void *context, const char *path, int *os_error_out)
{
	store_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (path == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	fixture->temporary_size = 0U;
	fixture->remove_calls++;
	return 0;
}

static sg_sidecar_store_ops_t TestOps(store_fixture_t *fixture)
{
	sg_sidecar_store_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.context = fixture;
	ops.temp_nonce = TempNonce;
	ops.open_exclusive = OpenExclusive;
	ops.write = Write;
	ops.flush = Flush;
	ops.sync_file = SyncFile;
	ops.close_file = Close;
	ops.replace_file = Replace;
	ops.sync_directory = SyncDirectory;
	ops.remove_file = Remove;
	return ops;
}

static sg_sidecar_revalidate_t Revalidate(void *context,
	const sg_rune_compact_wire_info_t *info, int *os_error_out)
{
	revalidate_fixture_t *fixture = context;

	fixture->store->revalidate_calls++;
	if (os_error_out != NULL)
		*os_error_out = fixture->result == SG_SIDECAR_REVALIDATE_ERROR ? EIO : 0;
	if (info != fixture->expected ||
		info->checksum != fixture->expected->checksum ||
		memcmp(&info->identity, &fixture->expected->identity,
			sizeof(info->identity)) != 0)
		return SG_SIDECAR_REVALIDATE_DRIFT;
	return fixture->result;
}

static void InitStore(store_fixture_t *fixture, const unsigned char *old,
	size_t old_size)
{
	memset(fixture, 0, sizeof(*fixture));
	memcpy(fixture->destination, old, old_size);
	fixture->destination_size = old_size;
}

static void CheckOld(const store_fixture_t *fixture, const unsigned char *old,
	size_t old_size)
{
	CHECK(fixture->destination_size == old_size);
	CHECK(memcmp(fixture->destination, old, old_size) == 0);
}

int main(void)
{
	static const unsigned char old_payload[] = { 1U, 2U, 3U };
	static const unsigned char new_payload[] = { 4U, 5U, 6U };
	unsigned char old[IMAGE_CAPACITY];
	unsigned char updated[IMAGE_CAPACITY];
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_info_t wrong_info;
	store_fixture_t fixture;
	revalidate_fixture_t revalidation;
	sg_sidecar_store_ops_t ops;
	sg_sidecar_store_result_t result;
	size_t old_size = 0U;
	size_t updated_size = 0U;
	failure_point_t failure;

	InitInfo(&info);
	CHECK(Encode(&info, old_payload, sizeof(old_payload), old, &old_size));
	CHECK(Encode(&info, new_payload, sizeof(new_payload), updated,
		&updated_size));
	InitStore(&fixture, old, old_size);
	fixture.open_collisions = 2;
	revalidation.store = &fixture;
	revalidation.expected = &info;
	revalidation.result = SG_SIDECAR_REVALIDATE_MATCH;
	ops = TestOps(&fixture);
	result = SG_SidecarStoreFile("bundle/sidecar.hmn", SG_SIDECAR_HUMAN,
		&info, updated, updated_size, Revalidate, &revalidation, &ops);
	CHECK(result.diagnostic == SCD_OK && result.stage == SCS_DONE);
	CHECK(result.temp_attempts == 3U && result.replacement_complete);
	CHECK(result.durability_complete && fixture.revalidate_calls == 1U);
	CHECK(fixture.destination_size == updated_size);
	CHECK(memcmp(fixture.destination, updated, updated_size) == 0);

	/* A bare filename is atomically replaced in the current directory. */
	InitStore(&fixture, old, old_size);
	revalidation.store = &fixture;
	revalidation.expected = &info;
	revalidation.result = SG_SIDECAR_REVALIDATE_MATCH;
	ops = TestOps(&fixture);
	result = SG_SidecarStoreFile("sidecar.hmn", SG_SIDECAR_HUMAN, &info,
		updated, updated_size, Revalidate, &revalidation, &ops);
	CHECK(result.diagnostic == SCD_OK && result.stage == SCS_DONE);
	CHECK(fixture.destination_size == updated_size);
	CHECK(memcmp(fixture.destination, updated, updated_size) == 0);

	for (failure = FAIL_WRITE; failure <= FAIL_RENAME; failure++)
	{
		InitStore(&fixture, old, old_size);
		fixture.failure = failure;
		revalidation.store = &fixture;
		revalidation.expected = &info;
		revalidation.result = SG_SIDECAR_REVALIDATE_MATCH;
		ops = TestOps(&fixture);
		result = SG_SidecarStoreFile("bundle/sidecar.hmn",
			SG_SIDECAR_HUMAN, &info, updated, updated_size, Revalidate,
			&revalidation, &ops);
		CHECK(result.diagnostic == SCD_IO_ERROR);
		CHECK(result.stage == (failure == FAIL_WRITE ? SCS_WRITE :
			failure == FAIL_FLUSH ? SCS_FLUSH :
			failure == FAIL_SYNC ? SCS_FILE_SYNC :
			failure == FAIL_CLOSE ? SCS_CLOSE : SCS_RENAME));
		CheckOld(&fixture, old, old_size);
		CHECK(!result.replacement_complete);
		CHECK(result.cleanup_attempted);
	}

	InitStore(&fixture, old, old_size);
	revalidation.store = &fixture;
	revalidation.expected = &info;
	revalidation.result = SG_SIDECAR_REVALIDATE_DRIFT;
	ops = TestOps(&fixture);
	result = SG_SidecarStoreFile("bundle/sidecar.hmn", SG_SIDECAR_HUMAN,
		&info, updated, updated_size, Revalidate, &revalidation, &ops);
	CHECK(result.diagnostic == SCD_STATE_DRIFT && result.stage == SCS_RECHECK);
	CheckOld(&fixture, old, old_size);
	CHECK(result.cleanup_attempted && !result.replacement_complete);

	InitStore(&fixture, old, old_size);
	fixture.failure = FAIL_DIRECTORY_SYNC;
	revalidation.store = &fixture;
	revalidation.expected = &info;
	revalidation.result = SG_SIDECAR_REVALIDATE_MATCH;
	ops = TestOps(&fixture);
	result = SG_SidecarStoreFile("bundle/sidecar.hmn", SG_SIDECAR_HUMAN,
		&info, updated, updated_size, Revalidate, &revalidation, &ops);
	CHECK(result.diagnostic == SCD_IO_ERROR &&
		result.stage == SCS_DIRECTORY_SYNC);
	CHECK(result.replacement_complete && !result.durability_complete);
	CHECK(memcmp(fixture.destination, updated, updated_size) == 0);
	/* A restart retry converges after a post-rename durability failure. */
	fixture.failure = FAIL_NONE;
	result = SG_SidecarStoreFile("bundle/sidecar.hmn", SG_SIDECAR_HUMAN,
		&info, updated, updated_size, Revalidate, &revalidation, &ops);
	CHECK(result.diagnostic == SCD_OK && result.durability_complete);
	CHECK(memcmp(fixture.destination, updated, updated_size) == 0);

	wrong_info = info;
	wrong_info.checksum ^= UINT32_C(1);
	InitStore(&fixture, old, old_size);
	revalidation.store = &fixture;
	revalidation.expected = &wrong_info;
	revalidation.result = SG_SIDECAR_REVALIDATE_MATCH;
	ops = TestOps(&fixture);
	result = SG_SidecarStoreFile("bundle/sidecar.hmn", SG_SIDECAR_HUMAN,
		&wrong_info, updated, updated_size, Revalidate, &revalidation, &ops);
	CHECK(result.diagnostic == SCD_RUNE_CHECKSUM_MISMATCH);
	CHECK(result.stage == SCS_RUNE_BINDING && fixture.open_calls == 0U);
	CheckOld(&fixture, old, old_size);

	if (failures != 0)
	{
		fprintf(stderr, "sg_sidecar_store_test: %d failure(s)\\n", failures);
		return 1;
	}
	puts("sg_sidecar_store_test: PASS");
	return 0;
}
