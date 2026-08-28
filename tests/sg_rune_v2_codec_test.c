/* Canonical-model round-trip and fail-closed tests for the RUNE v2 codec. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_v2_codec.h"
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

#define CHECK_U32(expected, actual) do { \
	uint32_t actual_ = (actual); \
	if (actual_ != UINT32_C(expected)) { \
		fprintf(stderr, "%s:%d: expected 0x%08x, got 0x%08x: %s\n", \
			__FILE__, __LINE__, (unsigned int)UINT32_C(expected), \
			(unsigned int)actual_, #actual); \
		failures++; \
	} \
} while (0)

typedef struct decode_fixture_s
{
	sg_rune_plane_t planes[8];
	sg_rune_vec3_t portal_vertices[3];
	sg_rune_phase_basis_t phases[3];
	sg_rune_phase_transition_t phase_transitions[1];
	sg_rune_cell_t cells[2];
	sg_rune_portal_t portals[1];
	sg_rune_surface_t surfaces[1];
	sg_rune_affordance_t affordances[1];
	sg_rune_capability_kernel_t kernels[1];
	sg_rune_landmark_t landmarks[1];
	sg_rune_mechanism_t mechanisms[1];
	sg_rune_v2_codec_storage_t storage;
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
	sg_rune_v2_wire_binding_t binding;
} decode_fixture_t;

static void DecodeFixtureInit(decode_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->storage.planes = fixture->planes;
	fixture->storage.plane_capacity = 8U;
	fixture->storage.portal_vertices = fixture->portal_vertices;
	fixture->storage.portal_vertex_capacity = 3U;
	fixture->storage.phases = fixture->phases;
	fixture->storage.phase_capacity = 3U;
	fixture->storage.phase_transitions = fixture->phase_transitions;
	fixture->storage.phase_transition_capacity = 1U;
	fixture->storage.cells = fixture->cells;
	fixture->storage.cell_capacity = 2U;
	fixture->storage.portals = fixture->portals;
	fixture->storage.portal_capacity = 1U;
	fixture->storage.surfaces = fixture->surfaces;
	fixture->storage.surface_capacity = 1U;
	fixture->storage.affordances = fixture->affordances;
	fixture->storage.affordance_capacity = 1U;
	fixture->storage.kernels = fixture->kernels;
	fixture->storage.kernel_capacity = 1U;
	fixture->storage.landmarks = fixture->landmarks;
	fixture->storage.landmark_capacity = 1U;
	fixture->storage.mechanisms = fixture->mechanisms;
	fixture->storage.mechanism_capacity = 1U;
}

static void DecodeFixturePoison(decode_fixture_t *fixture)
{
	DecodeFixtureInit(fixture);
	memset(fixture->planes, 0xa1, sizeof(fixture->planes));
	memset(fixture->portal_vertices, 0xa2, sizeof(fixture->portal_vertices));
	memset(fixture->phases, 0xa3, sizeof(fixture->phases));
	memset(fixture->phase_transitions, 0xa4,
		sizeof(fixture->phase_transitions));
	memset(fixture->cells, 0xa5, sizeof(fixture->cells));
	memset(fixture->portals, 0xa6, sizeof(fixture->portals));
	memset(fixture->surfaces, 0xa7, sizeof(fixture->surfaces));
	memset(fixture->affordances, 0xa8, sizeof(fixture->affordances));
	memset(fixture->kernels, 0xa9, sizeof(fixture->kernels));
	memset(fixture->landmarks, 0xaa, sizeof(fixture->landmarks));
	memset(fixture->mechanisms, 0xab, sizeof(fixture->mechanisms));
	memset(&fixture->binding, 0xac, sizeof(fixture->binding));
	memset(&fixture->model, 0xad, sizeof(fixture->model));
	memset(&fixture->evidence, 0xae, sizeof(fixture->evidence));
}

#define CHECK_DECODE_FIELD_UNCHANGED(field) \
	CHECK(memcmp(&actual->field, &before->field, sizeof(actual->field)) == 0)

static void CheckDecodeUnchanged(const decode_fixture_t *actual,
	const decode_fixture_t *before)
{
	CHECK_DECODE_FIELD_UNCHANGED(planes);
	CHECK_DECODE_FIELD_UNCHANGED(portal_vertices);
	CHECK_DECODE_FIELD_UNCHANGED(phases);
	CHECK_DECODE_FIELD_UNCHANGED(phase_transitions);
	CHECK_DECODE_FIELD_UNCHANGED(cells);
	CHECK_DECODE_FIELD_UNCHANGED(portals);
	CHECK_DECODE_FIELD_UNCHANGED(surfaces);
	CHECK_DECODE_FIELD_UNCHANGED(affordances);
	CHECK_DECODE_FIELD_UNCHANGED(kernels);
	CHECK_DECODE_FIELD_UNCHANGED(landmarks);
	CHECK_DECODE_FIELD_UNCHANGED(mechanisms);
	CHECK_DECODE_FIELD_UNCHANGED(binding);
	CHECK_DECODE_FIELD_UNCHANGED(model);
	CHECK_DECODE_FIELD_UNCHANGED(evidence);
}

#undef CHECK_DECODE_FIELD_UNCHANGED

#define CHECK_ARRAY(field, count) \
	CHECK(memcmp(actual->model.field, expected->model.field, \
		(size_t)(count) * sizeof(expected->field[0])) == 0)

static void CheckExactModel(const sg_rune_v2_test_model_fixture_t *expected,
	const decode_fixture_t *actual)
{
	CHECK(actual->model.version == expected->model.version);
	CHECK(actual->model.reserved == expected->model.reserved);
	CHECK(actual->model.schema_tag == expected->model.schema_tag);
	CHECK(actual->model.flags == expected->model.flags);
	CHECK(memcmp(&actual->model.identity, &expected->model.identity,
		sizeof(expected->model.identity)) == 0);
	CHECK(memcmp(&actual->model.completeness, &expected->model.completeness,
		sizeof(expected->model.completeness)) == 0);
	CHECK(memcmp(&actual->evidence, &expected->evidence,
		sizeof(expected->evidence)) == 0);
	CHECK_ARRAY(planes, expected->model.plane_count);
	CHECK_ARRAY(portal_vertices, expected->model.portal_vertex_count);
	CHECK_ARRAY(phases, expected->model.phase_count);
	CHECK_ARRAY(phase_transitions, expected->model.phase_transition_count);
	CHECK_ARRAY(cells, expected->model.cell_count);
	CHECK_ARRAY(portals, expected->model.portal_count);
	CHECK_ARRAY(surfaces, expected->model.surface_count);
	CHECK_ARRAY(affordances, expected->model.affordance_count);
	CHECK_ARRAY(kernels, expected->model.kernel_count);
	CHECK_ARRAY(landmarks, expected->model.landmark_count);
	CHECK_ARRAY(mechanisms, expected->model.mechanism_count);
}

static int RejectDecodedCandidate(
	const sg_rune_v2_wire_binding_t *binding,
	const sg_rune_model_t *candidate,
	const sg_rune_validation_evidence_t *evidence,
	void *context)
{
	unsigned int *calls = (unsigned int *)context;

	CHECK(binding != NULL);
	CHECK(candidate != NULL);
	CHECK(evidence != NULL);
	(*calls)++;
	return 0;
}

static void TestValidatedRejectionIsTransactional(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	decode_fixture_t scratch;
	decode_fixture_t published;
	decode_fixture_t published_before;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	unsigned int calls = 0U;
	int accepted = 1;

	SG_RuneV2TestFixtureInit(&fixture);
	DecodeFixtureInit(&scratch);
	DecodeFixturePoison(&published);
	published_before = published;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, encoded, sizeof(encoded), &encoded_size));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecDecodeValidated(encoded, encoded_size, &scratch.storage,
			&published.storage, RejectDecodedCandidate, &calls, &accepted,
			&published.binding, &published.model, &published.evidence));
	CHECK(calls == 1U);
	CHECK(accepted == 0);
	CheckDecodeUnchanged(&published, &published_before);
}

static void TestCanonicalRoundTrip(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	decode_fixture_t scratch;
	decode_fixture_t decoded;
	sg_rune_v2_wire_view_t view = { 0 };
	unsigned char first[TEST_IMAGE_CAPACITY];
	unsigned char second[TEST_IMAGE_CAPACITY];
	size_t expected_size = 0U;
	size_t first_size = 0U;
	size_t second_size = 0U;
	uint32_t index;
	static const uint32_t expected_counts[] = {
		1U, 8U, 3U, 3U, 1U, 2U, 1U, 1U, 1U, 1U, 1U, 1U, 1U
	};
	static const uint64_t expected_offsets[] = {
		480U, 736U, 1248U, 1288U, 1696U, 1832U, 2160U,
		2336U, 2472U, 2576U, 2912U, 3104U, 3264U
	};

	SG_RuneV2TestFixtureInit(&fixture);
	DecodeFixtureInit(&scratch);
	DecodeFixtureInit(&decoded);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncodedSize(&fixture.model, &fixture.evidence,
			&expected_size));
	CHECK(expected_size == TEST_IMAGE_BYTES);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, first, sizeof(first), &first_size));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, second, sizeof(second), &second_size));
	CHECK(first_size == TEST_IMAGE_BYTES && second_size == first_size);
	CHECK(memcmp(first, second, first_size) == 0);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2WireInspect(first, first_size, &view));
	for (index = 0U; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		CHECK(view.section[index].count == expected_counts[index]);
		CHECK(view.section[index].offset == expected_offsets[index]);
	}
	CHECK_U32(0x4d5e8d67, view.header.payload_crc32);
	CHECK_U32(0x3255c58b, SG_RuneV2WireGetU32(first +
		SG_RUNE_V2_HEADER_CRC_OFFSET));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecDecode(first, first_size, &scratch.storage,
			&decoded.storage,
			&decoded.binding, &decoded.model, &decoded.evidence));
	CHECK(decoded.binding.generation == fixture.binding.generation);
	CHECK(SG_RuneV2ContentIdEqual(&decoded.binding.bsp_identity,
		&fixture.binding.bsp_identity));
	CHECK(SG_RuneV2ContentIdEqual(&decoded.binding.schema_identity,
		&fixture.binding.schema_identity));
	CheckExactModel(&fixture, &decoded);
	CHECK(SG_RuneModelValidate(&decoded.model, &decoded.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&decoded.binding, &decoded.model,
			&decoded.evidence, second, sizeof(second), &second_size));
	CHECK(second_size == first_size && memcmp(first, second, first_size) == 0);
}

static void CheckDifferential(const unsigned char *encoded, size_t encoded_size,
	sg_rune_v2_wire_diagnostic_t expected)
{
	decode_fixture_t scratch;
	decode_fixture_t decoded;
	decode_fixture_t before;
	sg_rune_v2_wire_view_t view;
	sg_rune_v2_wire_diagnostic_t inspected;
	sg_rune_v2_wire_diagnostic_t decoded_result;

	DecodeFixtureInit(&scratch);
	DecodeFixturePoison(&decoded);
	before = decoded;
	inspected = SG_RuneV2WireInspect(encoded, encoded_size, &view);
	decoded_result = SG_RuneV2CodecDecode(encoded, encoded_size,
		&scratch.storage, &decoded.storage, &decoded.binding, &decoded.model,
		&decoded.evidence);
	CHECK(inspected == expected);
	CHECK(decoded_result == inspected);
	if (decoded_result != SG_RUNE_V2_WIRE_OK)
		CheckDecodeUnchanged(&decoded, &before);
}

static void TestMalformedInputs(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	decode_fixture_t scratch;
	decode_fixture_t decoded;
	unsigned char valid[TEST_IMAGE_CAPACITY];
	unsigned char bad[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	uint64_t kernel_offset;
	decode_fixture_t before;
	decode_fixture_t scratch_before;
	uint64_t cell_offset;
	uint64_t phase_offset;

	SG_RuneV2TestFixtureInit(&fixture);
	DecodeFixtureInit(&scratch);
	DecodeFixtureInit(&decoded);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, valid, sizeof(valid), &encoded_size));
	CheckDifferential(valid, SG_RUNE_V2_HEADER_BYTES - 1U,
		SG_RUNE_V2_WIRE_TRUNCATED);
	memcpy(bad, valid, encoded_size);
	bad[480U] ^= 1U;
	CheckDifferential(bad, encoded_size, SG_RUNE_V2_WIRE_BAD_SECTION_CRC);
	memcpy(bad, valid, encoded_size);
	SG_RuneV2WirePutU32(SG_RuneV2TestSectionEntry(bad, SG_RUNE_V2_SECTION_KERNELS - 1U) +
		SG_RUNE_V2_SECTION_COUNT_OFFSET, SG_RUNE_MODEL_MAX_KERNELS + 1U);
	CheckDifferential(bad, encoded_size, SG_RUNE_V2_WIRE_HOSTILE_COUNT);
	memcpy(bad, valid, encoded_size);
	SG_RuneV2WirePutU64(SG_RuneV2TestSectionEntry(bad, SG_RUNE_V2_SECTION_PLANES - 1U) +
		SG_RUNE_V2_SECTION_OFFSET_OFFSET, UINT64_MAX - 3U);
	CheckDifferential(bad, encoded_size, SG_RUNE_V2_WIRE_BAD_SECTION);
	memcpy(bad, valid, encoded_size);
	kernel_offset = SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(bad,
		SG_RUNE_V2_SECTION_KERNELS - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	memset(bad + (size_t)kernel_offset + SG_RUNE_V2_KERNEL_SOURCE_CELL_OFFSET,
		0xff, SG_RUNE_V2_STABLE_ID_BYTES);
	SG_RuneV2TestFixChecksums(bad, encoded_size);
	DecodeFixturePoison(&scratch);
	DecodeFixturePoison(&decoded);
	before = decoded;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_REFERENCE,
		SG_RuneV2CodecDecode(bad, encoded_size, &scratch.storage,
			&decoded.storage,
			&decoded.binding, &decoded.model, &decoded.evidence));
	CheckDecodeUnchanged(&decoded, &before);

	memcpy(bad, valid, encoded_size);
	SG_RuneV2WirePutU32(bad + (size_t)kernel_offset +
		SG_RUNE_V2_KERNEL_GRAVITY_OFFSET, UINT32_C(0x7fc00000));
	SG_RuneV2TestFixChecksums(bad, encoded_size);
	DecodeFixturePoison(&decoded);
	before = decoded;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_RECORD,
		SG_RuneV2CodecDecode(bad, encoded_size, &scratch.storage,
			&decoded.storage,
			&decoded.binding, &decoded.model, &decoded.evidence));
	CheckDecodeUnchanged(&decoded, &before);

	memcpy(bad, valid, encoded_size);
	cell_offset = SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(bad,
		SG_RUNE_V2_SECTION_CELLS - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	SG_RuneV2WirePutU32(bad + (size_t)cell_offset +
		SG_RUNE_V2_CELL_KERNELS_OFFSET + SG_RUNE_V2_SPAN_COUNT_OFFSET, 2U);
	SG_RuneV2TestFixChecksums(bad, encoded_size);
	DecodeFixturePoison(&decoded);
	before = decoded;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_REFERENCE,
		SG_RuneV2CodecDecode(bad, encoded_size, &scratch.storage,
			&decoded.storage,
			&decoded.binding, &decoded.model, &decoded.evidence));
	CheckDecodeUnchanged(&decoded, &before);

	memcpy(bad, valid, encoded_size);
	phase_offset = SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(bad,
		SG_RUNE_V2_SECTION_PHASES - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	SG_RuneV2WirePutU32(bad + (size_t)phase_offset +
		SG_RUNE_V2_PHASE_STANCE_OFFSET, SG_RUNE_STANCE_COUNT);
	SG_RuneV2TestFixChecksums(bad, encoded_size);
	DecodeFixturePoison(&decoded);
	before = decoded;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_RECORD,
		SG_RuneV2CodecDecode(bad, encoded_size, &scratch.storage,
			&decoded.storage,
			&decoded.binding, &decoded.model, &decoded.evidence));
	CheckDecodeUnchanged(&decoded, &before);

	DecodeFixturePoison(&scratch);
	scratch_before = scratch;
	DecodeFixturePoison(&decoded);
	before = decoded;
	decoded.storage.kernel_capacity = 0U;
	before.storage.kernel_capacity = 0U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_SIZE,
		SG_RuneV2CodecDecode(valid, encoded_size, &scratch.storage,
			&decoded.storage,
			&decoded.binding, &decoded.model, &decoded.evidence));
	CheckDecodeUnchanged(&decoded, &before);
	CheckDecodeUnchanged(&scratch, &scratch_before);

	DecodeFixturePoison(&decoded);
	before = decoded;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_INVALID_ARGUMENT,
		SG_RuneV2CodecDecode(valid, encoded_size, &decoded.storage,
			&decoded.storage, &decoded.binding, &decoded.model,
			&decoded.evidence));
	CheckDecodeUnchanged(&decoded, &before);
}

static void TestPublishedTailOverlapRejected(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	decode_fixture_t scratch;
	decode_fixture_t scratch_before;
	decode_fixture_t published;
	decode_fixture_t published_before;
	sg_rune_capability_kernel_t published_kernels[2];
	sg_rune_capability_kernel_t kernels_before[2];
	unsigned char valid[TEST_IMAGE_CAPACITY];
	unsigned char bad[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	uint64_t kernel_offset;

	SG_RuneV2TestFixtureInit(&fixture);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, valid, sizeof(valid), &encoded_size));
	memcpy(bad, valid, encoded_size);
	kernel_offset = SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(bad,
		SG_RUNE_V2_SECTION_KERNELS - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	memset(bad + (size_t)kernel_offset + SG_RUNE_V2_KERNEL_SOURCE_CELL_OFFSET,
		0xff, SG_RUNE_V2_STABLE_ID_BYTES);
	SG_RuneV2TestFixChecksums(bad, encoded_size);

	DecodeFixturePoison(&scratch);
	DecodeFixturePoison(&published);
	memset(published_kernels, 0x5a, sizeof(published_kernels));
	published.storage.kernels = published_kernels;
	published.storage.kernel_capacity = 2U;
	published.model.kernels = published_kernels;
	published.model.kernel_count = 2U;
	scratch.storage.kernels = published_kernels + 1;
	scratch.storage.kernel_capacity = 1U;
	scratch_before = scratch;
	published_before = published;
	memcpy(kernels_before, published_kernels, sizeof(kernels_before));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_INVALID_ARGUMENT,
		SG_RuneV2CodecDecode(bad, encoded_size, &scratch.storage,
			&published.storage, &published.binding, &published.model,
			&published.evidence));
	CheckDecodeUnchanged(&scratch, &scratch_before);
	CheckDecodeUnchanged(&published, &published_before);
	CHECK(memcmp(published_kernels, kernels_before,
		sizeof(published_kernels)) == 0);

	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_INVALID_ARGUMENT,
		SG_RuneV2CodecDecode(valid, encoded_size, &scratch.storage,
			&published.storage, &published.binding, &published.model,
			&published.evidence));
	CheckDecodeUnchanged(&scratch, &scratch_before);
	CheckDecodeUnchanged(&published, &published_before);
	CHECK(memcmp(published_kernels, kernels_before,
		sizeof(published_kernels)) == 0);
}

static void TestSingleBitCorruption(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	decode_fixture_t scratch;
	decode_fixture_t decoded;
	unsigned char valid[TEST_IMAGE_CAPACITY];
	unsigned char bad[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	size_t byte_index;
	unsigned int bit;

	SG_RuneV2TestFixtureInit(&fixture);
	DecodeFixtureInit(&scratch);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, valid, sizeof(valid), &encoded_size));
	for (byte_index = 0U; byte_index < encoded_size; byte_index++)
		for (bit = 0U; bit < 8U; bit++)
		{
			sg_rune_v2_wire_view_t view;
			sg_rune_v2_wire_diagnostic_t inspected;
			sg_rune_v2_wire_diagnostic_t decoded_result;

			memcpy(bad, valid, encoded_size);
			bad[byte_index] ^= (unsigned char)(1U << bit);
			DecodeFixtureInit(&decoded);
			inspected = SG_RuneV2WireInspect(bad, encoded_size, &view);
			decoded_result = SG_RuneV2CodecDecode(bad, encoded_size,
				&scratch.storage, &decoded.storage, &decoded.binding, &decoded.model,
				&decoded.evidence);
			if (inspected == SG_RUNE_V2_WIRE_OK || decoded_result != inspected)
			{
				fprintf(stderr,
					"%s:%d: byte %lu bit %u inspect=%d decode=%d\n",
					__FILE__, __LINE__, (unsigned long)byte_index, bit,
					(int)inspected, (int)decoded_result);
				failures++;
			}
		}
}

static void TestRepairedSemanticBitMutations(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	decode_fixture_t scratch;
	decode_fixture_t decoded;
	decode_fixture_t before;
	unsigned char valid[TEST_IMAGE_CAPACITY];
	unsigned char bad[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	size_t byte_index;
	size_t first_record;
	size_t binding_record;
	unsigned int bit;

	SG_RuneV2TestFixtureInit(&fixture);
	DecodeFixtureInit(&scratch);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
			&fixture.evidence, valid, sizeof(valid), &encoded_size));
	first_record = (size_t)SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(valid,
		SG_RUNE_V2_SECTION_MODEL - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	binding_record = (size_t)SG_RuneV2WireGetU64(SG_RuneV2TestSectionEntry(valid,
		SG_RUNE_V2_SECTION_BINDING - 1U) + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	for (byte_index = first_record; byte_index < binding_record; byte_index++)
		for (bit = 0U; bit < 8U; bit++)
		{
			sg_rune_v2_wire_diagnostic_t result;

			memcpy(bad, valid, encoded_size);
			bad[byte_index] ^= (unsigned char)(1U << bit);
			SG_RuneV2TestFixChecksums(bad, encoded_size);
			DecodeFixturePoison(&decoded);
			before = decoded;
			result = SG_RuneV2CodecDecode(bad, encoded_size, &scratch.storage,
				&decoded.storage, &decoded.binding, &decoded.model,
				&decoded.evidence);
			if (result == SG_RUNE_V2_WIRE_OK)
			{
				if (SG_RuneModelValidate(&decoded.model, &decoded.evidence) !=
					SG_RUNE_FAILURE_NONE)
				{
					fprintf(stderr,
						"%s:%d: repaired byte %lu bit %u accepted invalid model\n",
						__FILE__, __LINE__, (unsigned long)byte_index, bit);
					failures++;
				}
			}
			else
				CheckDecodeUnchanged(&decoded, &before);
		}
}

int main(void)
{
	TestValidatedRejectionIsTransactional();
	TestCanonicalRoundTrip();
	TestMalformedInputs();
	TestPublishedTailOverlapRejected();
	TestSingleBitCorruption();
	TestRepairedSemanticBitMutations();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_v2_codec_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_v2_codec_test: ok");
	return 0;
}
