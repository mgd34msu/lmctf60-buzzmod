/* One-open v12 sidecar loader fault tests. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_sidecar_loader.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct io_fixture_s
{
	const unsigned char *initial;
	const unsigned char *replacement;
	size_t image_size;
	size_t position;
	int use_replacement;
	int open_error;
	int grow_after_snapshot;
	int fail_allocate;
	unsigned int close_calls;
	unsigned int deallocate_calls;
} io_fixture_t;

static void InitInfo(sg_rune_compact_wire_info_t *info)
{
	uint32_t index;

	memset(info, 0, sizeof(*info));
	info->wire_version = SG_RUNE_COMPACT_WIRE_VERSION;
	info->model_version = SG_RUNE_COMPACT_MODEL_VERSION;
	info->analytic_version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	info->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	info->image_bytes = UINT64_C(1024);
	info->checksum = UINT32_C(0x10203040);
	for (index = 0U; index < 32U; index++)
		info->identity.bsp_sha256[index] = (uint8_t)(index + 3U);
	info->identity.bsp_bytes = UINT64_C(777);
	info->identity.entity_semantics_id = UINT64_C(9);
	info->identity.physics.frame_ms = 100U;
	info->identity.physics.substep_ms = 8U;
}

static int Encode(const sg_rune_compact_wire_info_t *info,
	unsigned char *image, size_t image_capacity, size_t *image_size_out)
{
	static const unsigned char payload[] = { 5U, 7U, 9U, 11U };

	return SG_SidecarEncode(SG_SIDECAR_HUMAN, info, payload,
		sizeof(payload), image, image_capacity, image_size_out) == SCD_OK;
}

static void *OpenRead(void *context, const char *path, int *os_error_out)
{
	io_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = fixture->open_error;
	if (fixture->open_error != 0 || path == NULL || strcmp(path, "sidecar") != 0)
		return NULL;
	fixture->position = 0U;
	fixture->use_replacement = 0;
	return fixture;
}

static size_t Read(void *context, void *file, unsigned char *output,
	size_t output_size, int *os_error_out)
{
	io_fixture_t *fixture = context;
	const unsigned char *source;
	size_t remaining;
	size_t count;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fixture || output == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0U;
	}
	source = fixture->use_replacement && fixture->replacement != NULL ?
		fixture->replacement : fixture->initial;
	if (fixture->position >= fixture->image_size)
	{
		if (fixture->grow_after_snapshot)
		{
			output[0] = UINT8_C(0xff);
			return 1U;
		}
		return 0U;
	}
	remaining = fixture->image_size - fixture->position;
	count = output_size < remaining ? output_size : remaining;
	memcpy(output, source + fixture->position, count);
	fixture->position += count;
	return count;
}

static int Seek(void *context, void *file, sg_sidecar_seek_origin_t origin,
	size_t offset, int *os_error_out)
{
	io_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fixture || offset != 0U)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	if (origin == SG_SIDECAR_SEEK_END)
		fixture->position = fixture->image_size;
	else if (origin == SG_SIDECAR_SEEK_BEGIN)
	{
		fixture->position = 0U;
		fixture->use_replacement = fixture->replacement != NULL;
	}
	else
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	return 0;
}

static int Tell(void *context, void *file, size_t *offset_out,
	int *os_error_out)
{
	io_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fixture || offset_out == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	*offset_out = fixture->position;
	return 0;
}

static int Close(void *context, void *file, int *os_error_out)
{
	io_fixture_t *fixture = context;

	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file != fixture)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	fixture->close_calls++;
	return 0;
}

static void *Allocate(void *context, size_t size)
{
	io_fixture_t *fixture = context;

	if (fixture->fail_allocate)
		return NULL;
	return malloc(size);
}

static void Deallocate(void *context, void *allocation)
{
	io_fixture_t *fixture = context;

	fixture->deallocate_calls++;
	free(allocation);
}

static sg_sidecar_load_ops_t TestOps(io_fixture_t *fixture)
{
	sg_sidecar_load_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.context = fixture;
	ops.open_read = OpenRead;
	ops.read = Read;
	ops.seek = Seek;
	ops.tell = Tell;
	ops.close_file = Close;
	ops.allocate = Allocate;
	ops.deallocate = Deallocate;
	return ops;
}

static void CheckFailure(const sg_sidecar_load_result_t *result,
	sg_sidecar_diagnostic_t diagnostic, sg_sidecar_stage_t stage,
	unsigned char *payload, size_t payload_size)
{
	CHECK(result->diagnostic == diagnostic);
	CHECK(result->stage == stage);
	CHECK(payload == NULL);
	CHECK(payload_size == 0U);
}

int main(void)
{
	unsigned char image[SG_SIDECAR_HEADER_BYTES + 4U];
	unsigned char changed[sizeof(image)];
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_info_t mismatched;
	io_fixture_t fixture;
	sg_sidecar_load_ops_t ops;
	sg_sidecar_load_result_t result;
	unsigned char *payload;
	size_t image_size = 0U;
	size_t payload_size;

	InitInfo(&info);
	CHECK(Encode(&info, image, sizeof(image), &image_size));
	memset(&fixture, 0, sizeof(fixture));
	fixture.initial = image;
	fixture.image_size = image_size;
	ops = TestOps(&fixture);
	payload = NULL;
	payload_size = 0U;
	result = SG_SidecarLoadFile("sidecar", SG_SIDECAR_HUMAN, &info,
		&payload, &payload_size, &ops);
	CHECK(result.diagnostic == SCD_OK);
	CHECK(result.stage == SCS_DONE);
	CHECK(payload != NULL && payload_size == 4U);
	CHECK(payload[0] == 5U && payload[3] == 11U);
	ops.deallocate(ops.context, payload);

	memcpy(changed, image, sizeof(changed));
	changed[0] ^= UINT8_C(1);
	fixture.replacement = changed;
	payload = NULL;
	payload_size = 0U;
	result = SG_SidecarLoadFile("sidecar", SG_SIDECAR_HUMAN, &info,
		&payload, &payload_size, &ops);
	CheckFailure(&result, SCD_STATE_DRIFT, SCS_RECHECK, payload, payload_size);
	fixture.replacement = NULL;

	fixture.grow_after_snapshot = 1;
	payload = NULL;
	payload_size = 0U;
	result = SG_SidecarLoadFile("sidecar", SG_SIDECAR_HUMAN, &info,
		&payload, &payload_size, &ops);
	CheckFailure(&result, SCD_BAD_FILE_SIZE, SCS_PAYLOAD_READ, payload,
		payload_size);
	fixture.grow_after_snapshot = 0;

	fixture.fail_allocate = 1;
	payload = NULL;
	payload_size = 0U;
	result = SG_SidecarLoadFile("sidecar", SG_SIDECAR_HUMAN, &info,
		&payload, &payload_size, &ops);
	CheckFailure(&result, SCD_ALLOCATION_FAILED, SCS_ALLOCATION, payload,
		payload_size);
	fixture.fail_allocate = 0;

	mismatched = info;
	mismatched.checksum ^= UINT32_C(1);
	payload = NULL;
	payload_size = 0U;
	result = SG_SidecarLoadFile("sidecar", SG_SIDECAR_HUMAN, &mismatched,
		&payload, &payload_size, &ops);
	CheckFailure(&result, SCD_RUNE_CHECKSUM_MISMATCH, SCS_RUNE_BINDING,
		payload, payload_size);

	fixture.open_error = ENOENT;
	payload = NULL;
	payload_size = 0U;
	result = SG_SidecarLoadFile("sidecar", SG_SIDECAR_HUMAN, &info,
		&payload, &payload_size, &ops);
	CheckFailure(&result, SCD_ABSENT, SCS_OPEN, payload, payload_size);
	CHECK(fixture.close_calls >= 5U);
	CHECK(fixture.deallocate_calls >= 2U);
	if (failures != 0)
	{
		fprintf(stderr, "sg_sidecar_loader_test: %d failure(s)\\n", failures);
		return 1;
	}
	puts("sg_sidecar_loader_test: PASS");
	return 0;
}
