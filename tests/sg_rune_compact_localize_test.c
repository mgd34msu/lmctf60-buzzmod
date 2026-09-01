#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_localize.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct localize_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_incidence_index_t cell_incidences[2];
	sg_rune_compact_model_t model;
} localize_fixture_t;

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static sg_rune_q8_vec3_t Point(int32_t x, int32_t y, int32_t z)
{
	const sg_rune_q8_vec3_t point = { { x, y, z } };

	return point;
}

static sg_rune_vec3_t FloatPoint(float x, float y, float z)
{
	const sg_rune_vec3_t point = { { x, y, z } };

	return point;
}

static void InitFixture(localize_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->cells[0].bounds.mins = Point(0, 0, 0);
	fixture->cells[0].bounds.maxs = Point(64, 64, 64);
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 1U };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].bounds.mins.value[0] = 64;
	fixture->cells[1].bounds.maxs.value[0] = 128;
	fixture->cells[1].incidences.first = 1U;
	fixture->cells[1].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;

	fixture->facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[0].plane.normal_bits[1] = Bits(0.0f);
	fixture->facets[0].plane.normal_bits[2] = Bits(0.0f);
	fixture->facets[0].plane.distance_bits = Bits(8.0f);

	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].facet.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[1] = fixture->incidences[0];
	fixture->incidences[1].cell.value = 1U;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->cell_incidences[0].value = 0U;
	fixture->cell_incidences[1].value = 1U;

	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 2U;
	fixture->model.facets = fixture->facets;
	fixture->model.facet_count = 1U;
	fixture->model.incidences = fixture->incidences;
	fixture->model.incidence_count = 2U;
	fixture->model.cell_incidences = fixture->cell_incidences;
	fixture->model.cell_incidence_count = 2U;
}

static void CheckLocation(const sg_rune_compact_model_t *model,
	sg_rune_q8_vec3_t point, uint32_t cell,
	sg_rune_stance_validity_t stances)
{
	sg_rune_compact_location_t location;

	CHECK(SG_RuneCompactLocalize(model, &point, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(location.cell.value == cell);
	CHECK(location.valid_stances == stances);
	CHECK(location.reserved[0] == 0U);
	CHECK(location.reserved[1] == 0U);
	CHECK(location.reserved[2] == 0U);
}

static void TestInteriorsAndStances(void)
{
	localize_fixture_t fixture;

	InitFixture(&fixture);
	CheckLocation(&fixture.model, Point(32, 16, 16), 0U,
		SG_RUNE_STANCE_VALID_ALL);
	CheckLocation(&fixture.model, Point(96, 16, 16), 1U,
		SG_RUNE_STANCE_VALID_CROUCHING);
}

static void TestHalfOpenSharedBoundary(void)
{
	localize_fixture_t fixture;
	sg_rune_compact_location_t location;
	sg_rune_compact_cell_index_t reversed[2] = { { 1U }, { 0U } };
	const sg_rune_q8_vec3_t boundary = Point(64, 16, 16);

	InitFixture(&fixture);
	CheckLocation(&fixture.model, boundary, 0U, SG_RUNE_STANCE_VALID_ALL);
	CHECK(SG_RuneCompactLocalizeIndexed(&fixture.model, &boundary, reversed,
		2U, &location) == SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(location.cell.value == 0U);

	fixture.incidences[0].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture.incidences[1].boundary = SG_RUNE_BOUNDARY_CLOSED;
	CheckLocation(&fixture.model, boundary, 1U,
		SG_RUNE_STANCE_VALID_CROUCHING);
}

static void TestFacetHalfspaceRejectsOverlappingBounds(void)
{
	localize_fixture_t fixture;

	InitFixture(&fixture);
	fixture.cells[0].bounds.maxs.value[0] = 128;
	fixture.cells[1].bounds.mins.value[0] = 0;
	fixture.facets[0].plane.normal_bits[1] = Bits(1.0f);
	CheckLocation(&fixture.model, Point(16, 16, 16), 0U,
		SG_RUNE_STANCE_VALID_ALL);
	CheckLocation(&fixture.model, Point(48, 48, 16), 1U,
		SG_RUNE_STANCE_VALID_CROUCHING);
	CheckLocation(&fixture.model, Point(32, 32, 16), 0U,
		SG_RUNE_STANCE_VALID_ALL);
}

static void TestExactSubnormalBoundary(void)
{
	localize_fixture_t fixture;
	const sg_rune_q8_vec3_t boundary = Point(8, 16, 16);

	InitFixture(&fixture);
	fixture.cells[0].bounds.maxs.value[0] = 8;
	fixture.cells[1].bounds.mins.value[0] = 8;
	fixture.facets[0].plane.normal_bits[0] = UINT32_C(1);
	fixture.facets[0].plane.distance_bits = UINT32_C(1);
	CheckLocation(&fixture.model, boundary, 0U, SG_RUNE_STANCE_VALID_ALL);
}

static void TestExactLargePlaneArithmetic(void)
{
	localize_fixture_t fixture;
	const sg_rune_q8_vec3_t axial_boundary = Point(8, 16, 16);
	const int32_t coordinate = INT32_MAX - 1;
	const sg_rune_q8_vec3_t oblique_boundary =
		Point(coordinate, coordinate, 16);

	InitFixture(&fixture);
	fixture.cells[0].bounds.maxs.value[0] = 8;
	fixture.cells[1].bounds.mins.value[0] = 8;
	fixture.facets[0].plane.normal_bits[0] = UINT32_C(0x7f7fffff);
	fixture.facets[0].plane.distance_bits = UINT32_C(0x7f7fffff);
	CheckLocation(&fixture.model, axial_boundary, 0U,
		SG_RUNE_STANCE_VALID_ALL);

	fixture.cells[0].bounds.mins = Point(INT32_MAX - 2, INT32_MAX - 2, 0);
	fixture.cells[0].bounds.maxs = Point(INT32_MAX, INT32_MAX, 64);
	fixture.cells[1].bounds = fixture.cells[0].bounds;
	fixture.facets[0].plane.normal_bits[1] = UINT32_C(0xff7fffff);
	fixture.facets[0].plane.distance_bits = Bits(0.0f);
	CheckLocation(&fixture.model, oblique_boundary, 0U,
		SG_RUNE_STANCE_VALID_ALL);
	CheckLocation(&fixture.model, Point(coordinate - 1, coordinate, 16), 0U,
		SG_RUNE_STANCE_VALID_ALL);
	CheckLocation(&fixture.model, Point(coordinate, coordinate - 1, 16), 1U,
		SG_RUNE_STANCE_VALID_CROUCHING);
}

static void TestBinary32Localization(void)
{
	localize_fixture_t fixture;
	sg_rune_compact_location_t first;
	sg_rune_compact_location_t second;
	sg_rune_vec3_t point = FloatPoint(4.125f, 2.0f, 2.0f);

	InitFixture(&fixture);
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model, &point, &first) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(first.cell.value == 0U &&
		first.valid_stances == SG_RUNE_STANCE_VALID_ALL);
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model,
		&(sg_rune_vec3_t){ { 8.0f, 2.0f, 2.0f } }, &first) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(first.cell.value == 0U);
	fixture.incidences[0].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture.incidences[1].boundary = SG_RUNE_BOUNDARY_CLOSED;
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model,
		&(sg_rune_vec3_t){ { 8.0f, 2.0f, 2.0f } }, &first) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(first.cell.value == 1U &&
		first.valid_stances == SG_RUNE_STANCE_VALID_CROUCHING);
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model, &point, &first) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model, &point, &second) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
}

static void TestBinary32ExactCancellation(void)
{
	localize_fixture_t fixture;
	sg_rune_compact_location_t location;

	InitFixture(&fixture);
	fixture.model.cell_count = 1U;
	fixture.cells[0].bounds.mins = Point(0, 0, 0);
	fixture.cells[0].bounds.maxs = Point(64, 64, 64);
	fixture.facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture.facets[0].plane.normal_bits[1] = Bits(1.0e30f);
	fixture.facets[0].plane.normal_bits[2] = Bits(0.0f);
	fixture.facets[0].plane.distance_bits = Bits(1.0e30f);
	fixture.incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture.incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model,
		&(sg_rune_vec3_t){ { 1.0f, 1.0f, 0.0f } }, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND);

	fixture.facets[0].plane.normal_bits[0] = UINT32_C(1);
	fixture.facets[0].plane.normal_bits[1] = Bits(0.0f);
	fixture.facets[0].plane.distance_bits = Bits(0.0f);
	CHECK(SG_RuneCompactLocalizeBinary32(&fixture.model,
		&(sg_rune_vec3_t){ { 1.40129846e-45f, 0.0f, 0.0f } },
		&location) == SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND);
}

static void TestIndexedCandidates(void)
{
	localize_fixture_t fixture;
	sg_rune_compact_location_t location;
	const sg_rune_q8_vec3_t point = Point(96, 16, 16);
	sg_rune_compact_cell_index_t candidates[3] = { { 1U }, { 1U }, { 0U } };

	InitFixture(&fixture);
	CHECK(SG_RuneCompactLocalizeIndexed(&fixture.model, &point, candidates,
		3U, &location) == SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(location.cell.value == 1U);
	CHECK(SG_RuneCompactLocalizeIndexed(&fixture.model, &point, candidates + 2,
		1U, &location) == SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND);
	CHECK(location.cell.value == SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(location.valid_stances == 0U);
	CHECK(SG_RuneCompactLocalizeIndexed(&fixture.model, &point, NULL, 0U,
		&location) == SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND);

	candidates[2].value = 2U;
	CHECK(SG_RuneCompactLocalizeIndexed(&fixture.model, &point, candidates,
		3U, &location) == SG_RUNE_COMPACT_LOCALIZE_INVALID_CANDIDATE);
	CHECK(location.cell.value == SG_RUNE_COMPACT_INDEX_NONE);
}

static void TestFailuresClearOutput(void)
{
	localize_fixture_t fixture;
	sg_rune_compact_location_t location = { { 7U }, 3U, { 1U, 1U, 1U } };
	const sg_rune_q8_vec3_t outside = Point(-1, 16, 16);

	InitFixture(&fixture);
	CHECK(SG_RuneCompactLocalize(&fixture.model, &outside, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND);
	CHECK(location.cell.value == SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(location.valid_stances == 0U);
	fixture.cell_incidences[0].value = 2U;
	CHECK(SG_RuneCompactLocalize(&fixture.model, &(sg_rune_q8_vec3_t)
		{ { 32, 16, 16 } }, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL);
	CHECK(location.cell.value == SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(SG_RuneCompactLocalize(NULL, &outside, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT);
	CHECK(SG_RuneCompactLocalize(&fixture.model, &outside, NULL) ==
		SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT);
	fixture.model.cells = NULL;
	CHECK(SG_RuneCompactLocalize(&fixture.model, &outside, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL);
	CHECK(strcmp(SG_RuneCompactLocalizeStatusString(
		SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND), "not found") == 0);
}

int main(void)
{
	TestInteriorsAndStances();
	TestHalfOpenSharedBoundary();
	TestFacetHalfspaceRejectsOverlappingBounds();
	TestExactSubnormalBoundary();
	TestExactLargePlaneArithmetic();
	TestBinary32Localization();
	TestBinary32ExactCancellation();
	TestIndexedCandidates();
	TestFailuresClearOutput();
	if (failures != 0) {
		fprintf(stderr, "sg_rune_compact_localize_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_compact_localize_test: ok");
	return 0;
}
