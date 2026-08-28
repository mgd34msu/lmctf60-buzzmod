/* Transaction and hostile-file tests for the RUNE v2 artifact loader. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_v2_artifact_loader.h"

#include "support/sg_rune_v2_fixture.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	sg_rune_v2_wire_diagnostic_t actual_ = (expression); \
	if (actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: expected diagnostic %d, got %d: %s\n", \
			__FILE__, __LINE__, (int)(expected), (int)actual_, \
			#expression); \
		failures++; \
	} \
} while (0)

typedef struct loader_fixture_s
{
	const unsigned char *file_bytes;
	size_t file_size;
	size_t readable_size;
	size_t file_position;
	size_t allocation_calls;
	size_t fail_allocation_call;
	size_t live_allocations;
	size_t read_calls;
	size_t fail_read_call;
	int close_fails;
	int is_open;
} loader_fixture_t;

static void *TestAllocate(void *context, size_t size)
{
	loader_fixture_t *fixture = context;
	void *allocation;
	fixture->allocation_calls++;
	if (fixture->fail_allocation_call == fixture->allocation_calls)
		return NULL;
	allocation = malloc(size);
	if (allocation)
		fixture->live_allocations++;
	return allocation;
}

static void TestDeallocate(void *context, void *allocation)
{
	loader_fixture_t *fixture = context;
	if (!allocation)
		return;
	CHECK(fixture->live_allocations != 0U);
	fixture->live_allocations--;
	free(allocation);
}

static void *TestOpen(void *context, const char *path, int *error_out)
{
	loader_fixture_t *fixture = context;
	(void)path;
	fixture->file_position = 0U;
	fixture->read_calls = 0U;
	fixture->is_open = 1;
	*error_out = 0;
	return fixture;
}

static size_t TestRead(void *context, void *file, unsigned char *output,
	size_t output_size, int *error_out)
{
	loader_fixture_t *fixture = context;
	size_t available;
	(void)file;
	fixture->read_calls++;
	if (fixture->fail_read_call == fixture->read_calls)
	{
		*error_out = EIO;
		return 0U;
	}
	available = fixture->readable_size - fixture->file_position;
	if (output_size > available)
		output_size = available;
	memcpy(output, fixture->file_bytes + fixture->file_position, output_size);
	fixture->file_position += output_size;
	*error_out = 0;
	return output_size;
}

static int TestSeek(void *context, void *file,
	sg_rune_v2_loader_seek_origin_t origin, size_t offset, int *error_out)
{
	loader_fixture_t *fixture = context;
	(void)file;
	if (offset != 0U)
	{
		*error_out = EINVAL;
		return 0;
	}
	fixture->file_position = origin == SG_RUNE_V2_LOADER_SEEK_END
		? fixture->file_size : 0U;
	*error_out = 0;
	return 1;
}

static int TestTell(void *context, void *file, size_t *offset_out,
	int *error_out)
{
	loader_fixture_t *fixture = context;
	(void)file;
	*offset_out = fixture->file_position;
	*error_out = 0;
	return 1;
}

static int TestClose(void *context, void *file, int *error_out)
{
	loader_fixture_t *fixture = context;
	(void)file;
	CHECK(fixture->is_open);
	fixture->is_open = 0;
	if (fixture->close_fails)
	{
		*error_out = EIO;
		return 0;
	}
	*error_out = 0;
	return 1;
}

static sg_rune_v2_artifact_loader_ops_t TestOps(loader_fixture_t *fixture)
{
	sg_rune_v2_artifact_loader_ops_t ops;
	memset(&ops, 0, sizeof(ops));
	ops.context = fixture;
	ops.open_read = TestOpen;
	ops.read = TestRead;
	ops.seek = TestSeek;
	ops.tell = TestTell;
	ops.close_file = TestClose;
	ops.allocate = TestAllocate;
	ops.deallocate = TestDeallocate;
	return ops;
}

static int BindingEqualForTest(const sg_rune_v2_artifact_binding_t *left,
	const sg_rune_v2_wire_binding_t *right)
{
	return left->generation == right->generation &&
		SG_RuneV2ContentIdEqual(&left->bsp_identity, &right->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&left->schema_identity, &right->schema_identity);
}

static sg_rune_v2_artifact_binding_t ArtifactBinding(
	const sg_rune_v2_test_model_fixture_t *fixture,
	sg_rune_v2_content_id_t artifact_identity)
{
	sg_rune_v2_artifact_binding_t binding;
	binding.generation = fixture->binding.generation;
	binding.bsp_identity = fixture->binding.bsp_identity;
	binding.schema_identity = fixture->binding.schema_identity;
	binding.artifact_identity = artifact_identity;
	return binding;
}

static int SnapshotMatches(const sg_rune_v2_artifact_snapshot_t *snapshot,
	const sg_rune_v2_test_model_fixture_t *fixture)
{
	const sg_rune_model_t *model;
	if (!snapshot || !BindingEqualForTest(&snapshot->binding, &fixture->binding) ||
		!SG_RuneV2ContentIdValid(&snapshot->binding.artifact_identity) ||
		memcmp(&snapshot->evidence, &fixture->evidence,
			sizeof(snapshot->evidence)) != 0)
		return 0;
	model = &snapshot->model;
#define MATCH(member, count_member) \
	(model->count_member == fixture->model.count_member && \
	 memcmp(model->member, fixture->model.member, \
		(size_t)model->count_member * sizeof(model->member[0])) == 0)
	return MATCH(planes, plane_count) &&
		MATCH(portal_vertices, portal_vertex_count) &&
		MATCH(phases, phase_count) &&
		MATCH(phase_transitions, phase_transition_count) &&
		MATCH(cells, cell_count) && MATCH(portals, portal_count) &&
		MATCH(surfaces, surface_count) &&
		MATCH(affordances, affordance_count) &&
		MATCH(kernels, kernel_count) && MATCH(landmarks, landmark_count) &&
		MATCH(mechanisms, mechanism_count);
#undef MATCH
}

static void CheckOldUnchanged(sg_rune_v2_artifact_loader_t *loader,
	const sg_rune_v2_artifact_snapshot_t *old,
	const sg_rune_v2_artifact_snapshot_t *old_copy,
	const sg_rune_v2_test_model_fixture_t *old_fixture)
{
	CHECK(SG_RuneV2ArtifactLoaderSnapshot(loader) == old);
	CHECK(memcmp(old, old_copy, sizeof(*old)) == 0);
	CHECK(SnapshotMatches(old, old_fixture));
}

static void Encode(sg_rune_v2_test_model_fixture_t *fixture, unsigned char *image,
	size_t *size_out)
{
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture->binding, &fixture->model,
			&fixture->evidence, image, TEST_IMAGE_CAPACITY, size_out));
}

static void TestByteTransactionAndFaults(void)
{
	sg_rune_v2_test_model_fixture_t old_fixture;
	sg_rune_v2_test_model_fixture_t new_fixture;
	loader_fixture_t memory = { 0 };
	sg_rune_v2_artifact_loader_ops_t ops = TestOps(&memory);
	sg_rune_v2_artifact_loader_t loader =
		SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER;
	const sg_rune_v2_artifact_snapshot_t *old;
	sg_rune_v2_artifact_snapshot_t old_copy;
	unsigned char old_image[TEST_IMAGE_CAPACITY];
	unsigned char new_image[TEST_IMAGE_CAPACITY];
	unsigned char bad[TEST_IMAGE_CAPACITY];
	size_t old_size = 0U;
	size_t new_size = 0U;
	size_t allocations_before;
	size_t first_failure;
	size_t allocation;
	sg_rune_v2_artifact_load_result_t result;
	uint64_t kernel_offset;
	uint64_t model_offset;
	sg_rune_v2_content_id_t old_exact = ContentId(129U);
	sg_rune_v2_content_id_t new_exact = ContentId(161U);
	sg_rune_v2_content_id_t wrong_exact = ContentId(193U);
	sg_rune_v2_artifact_binding_t old_binding;
	sg_rune_v2_artifact_binding_t new_binding;
	sg_rune_v2_artifact_binding_t wrong_binding;

	SG_RuneV2TestFixtureInit(&old_fixture);
	old_binding = ArtifactBinding(&old_fixture, old_exact);
	Encode(&old_fixture, old_image, &old_size);
	CHECK(SG_RuneV2ArtifactLoaderInit(&loader, &ops));
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, old_image, old_size,
		&old_binding, &old_exact);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_OK);
	old = SG_RuneV2ArtifactLoaderSnapshot(&loader);
	CHECK(SnapshotMatches(old, &old_fixture));
	CHECK(SG_RuneV2ContentIdEqual(&old->binding.artifact_identity, &old_exact));
	old_copy = *old;
	CHECK(!SG_RuneV2ArtifactLoaderInit(&loader, &ops));
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	allocations_before = memory.allocation_calls;
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, old_image,
		SG_RUNE_V2_HEADER_BYTES - 1U, &old_binding, &old_exact);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_WIRE_REJECTED);
	CHECK(result.wire_diagnostic == SG_RUNE_V2_WIRE_TRUNCATED);
	CHECK(memory.allocation_calls == allocations_before);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	wrong_binding = old_binding;
	wrong_binding.schema_identity.bytes[0] ^= UINT8_C(0x55);
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, old_image, old_size,
		&wrong_binding, &old_exact);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_BINDING_MISMATCH);
	CHECK(memory.allocation_calls == allocations_before);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, old_image, old_size,
		&old_binding, &wrong_exact);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_BINDING_MISMATCH);
	CHECK(memory.allocation_calls == allocations_before);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	memcpy(bad, old_image, old_size);
	SG_RuneV2WirePutU16(bad + SG_RUNE_V2_HEADER_VERSION_OFFSET, 3U);
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, bad, old_size,
		&old_binding, &old_exact);
	CHECK(result.wire_diagnostic == SG_RUNE_V2_WIRE_BAD_VERSION);
	CHECK(memory.allocation_calls == allocations_before);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	memcpy(bad, old_image, old_size);
	bad[SG_RUNE_V2_HEADER_CRC_OFFSET] ^= 1U;
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, bad, old_size,
		&old_binding, &old_exact);
	CHECK(result.wire_diagnostic == SG_RUNE_V2_WIRE_BAD_HEADER_CRC);
	CHECK(memory.allocation_calls == allocations_before);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	memcpy(bad, old_image, old_size);
	kernel_offset = SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(bad,
		SG_RUNE_V2_SECTION_KERNELS - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	memset(bad + (size_t)kernel_offset + SG_RUNE_V2_KERNEL_SOURCE_CELL_OFFSET,
		0xff, SG_RUNE_V2_STABLE_ID_BYTES);
	SG_RuneV2TestFixChecksums(bad, old_size);
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, bad, old_size,
		&old_binding, &old_exact);
	CHECK(result.wire_diagnostic == SG_RUNE_V2_WIRE_BAD_REFERENCE);
	CHECK(result.stage == SG_RUNE_V2_LOADER_STAGE_DECODE);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	memcpy(bad, old_image, old_size);
	model_offset = SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(bad,
		SG_RUNE_V2_SECTION_MODEL - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	SG_RuneV2WirePutU32(bad + (size_t)model_offset +
		SG_RUNE_V2_MODEL_PROVED_CELLS_OFFSET, 0U);
	SG_RuneV2TestFixChecksums(bad, old_size);
	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, bad, old_size,
		&old_binding, &old_exact);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_WIRE_REJECTED);
	CHECK(result.stage == SG_RUNE_V2_LOADER_STAGE_DECODE);
	CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);

	SG_RuneV2TestFixtureInit(&new_fixture);
	new_fixture.binding.generation++;
	new_binding = ArtifactBinding(&new_fixture, new_exact);
	Encode(&new_fixture, new_image, &new_size);
	first_failure = memory.allocation_calls + 1U;
	for (allocation = 0U; allocation < 23U; allocation++)
	{
		memory.fail_allocation_call = first_failure + allocation;
		result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, new_image, new_size,
			&new_binding, &new_exact);
		CHECK(result.diagnostic == SG_RUNE_V2_LOADER_ALLOCATION_FAILED);
		CheckOldUnchanged(&loader, old, &old_copy, &old_fixture);
		memory.fail_allocation_call = 0U;
		first_failure = memory.allocation_calls + 1U;
	}

	result = SG_RuneV2ArtifactLoaderLoadBytes(&loader, new_image, new_size,
		&new_binding, &new_exact);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_OK);
	CHECK(SG_RuneV2ArtifactLoaderSnapshot(&loader) != old);
	CHECK(SnapshotMatches(SG_RuneV2ArtifactLoaderSnapshot(&loader), &new_fixture));
	CHECK(SG_RuneV2ContentIdEqual(
		&SG_RuneV2ArtifactLoaderSnapshot(&loader)->binding.artifact_identity,
		&new_exact));
	SG_RuneV2ArtifactLoaderReset(&loader);
	CHECK(SG_RuneV2ArtifactLoaderSnapshot(&loader) == NULL);
	CHECK(memory.live_allocations == 0U);
	SG_RuneV2ArtifactLoaderDestroy(&loader);
	CHECK(memory.live_allocations == 0U);
}

static void TestFileTransactionAndFaults(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	loader_fixture_t io = { 0 };
	sg_rune_v2_artifact_loader_ops_t ops = TestOps(&io);
	sg_rune_v2_artifact_loader_t loader =
		SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER;
	const sg_rune_v2_artifact_snapshot_t *old;
	sg_rune_v2_artifact_snapshot_t old_copy;
	unsigned char image[TEST_IMAGE_CAPACITY];
	size_t image_size = 0U;
	size_t fail_call;
	sg_rune_v2_artifact_load_result_t result;
	sg_rune_v2_content_id_t exact_identity = ContentId(129U);
	sg_rune_v2_artifact_binding_t binding;

	SG_RuneV2TestFixtureInit(&fixture);
	binding = ArtifactBinding(&fixture, exact_identity);
	Encode(&fixture, image, &image_size);
	io.file_bytes = image;
	io.file_size = image_size;
	io.readable_size = image_size;
	CHECK(SG_RuneV2ArtifactLoaderInit(&loader, &ops));
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, "fixture.rune2",
		&binding, &exact_identity);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_OK);
	CHECK(result.bytes_read == image_size);
	CHECK(!io.is_open);
	old = SG_RuneV2ArtifactLoaderSnapshot(&loader);
	old_copy = *old;

	io.file_size = SG_RUNE_V2_HEADER_BYTES - 1U;
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, "truncated.rune2",
		&binding, &exact_identity);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_BAD_FILE_SIZE);
	CheckOldUnchanged(&loader, old, &old_copy, &fixture);

	io.file_size = image_size;
	io.readable_size = image_size - 1U;
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, "short-read.rune2",
		&binding, &exact_identity);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_IO_ERROR);
	CHECK(result.bytes_read == image_size - 1U);
	CheckOldUnchanged(&loader, old, &old_copy, &fixture);
	io.readable_size = image_size;

	io.fail_read_call = 1U;
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, "read-fail.rune2",
		&binding, &exact_identity);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_IO_ERROR);
	CHECK(result.stage == SG_RUNE_V2_LOADER_STAGE_FILE_READ);
	CheckOldUnchanged(&loader, old, &old_copy, &fixture);
	io.fail_read_call = 0U;

	fail_call = io.allocation_calls + 1U;
	io.fail_allocation_call = fail_call;
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, "alloc-fail.rune2",
		&binding, &exact_identity);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_ALLOCATION_FAILED);
	CHECK(result.stage == SG_RUNE_V2_LOADER_STAGE_FILE_ALLOCATION);
	CheckOldUnchanged(&loader, old, &old_copy, &fixture);
	io.fail_allocation_call = 0U;

	io.close_fails = 1;
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, "close-fail.rune2",
		&binding, &exact_identity);
	CHECK(result.diagnostic == SG_RUNE_V2_LOADER_IO_ERROR);
	CHECK(result.stage == SG_RUNE_V2_LOADER_STAGE_CLOSE);
	CheckOldUnchanged(&loader, old, &old_copy, &fixture);
	io.close_fails = 0;

	SG_RuneV2ArtifactLoaderDestroy(&loader);
	CHECK(io.live_allocations == 0U);
}

int main(void)
{
	failures = 0;
	TestByteTransactionAndFaults();
	TestFileTransactionAndFaults();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_v2_artifact_loader_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_v2_artifact_loader_test: ok");
	return 0;
}
