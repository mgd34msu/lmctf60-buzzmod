#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_static.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct compact_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_incidence_index_t cell_incidences[2];
	sg_rune_q8_vec3_t vertices[5];
	sg_rune_compact_portal_t portals[1];
	sg_rune_movement_field_attachment_t movement_fields[2];
	sg_rune_weapon_response_region_t weapon_regions[2];
	sg_rune_weapon_profile_t weapon_profiles[12];
	sg_rune_weapon_response_kernel_t weapon_kernels[26];
	sg_rune_analytic_function_index_t analytic_function_refs[114];
	sg_rune_analytic_function_t analytic_functions[8];
	sg_rune_analytic_constant_t analytic_constants[8];
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_landmark_t landmarks[2];
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_facet_annotation_t facet_annotations[1];
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
} compact_fixture_t;

static sg_rune_compact_source_t BspPlaneSource(uint32_t plane)
{
	sg_rune_compact_source_t source;

	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	source.value.bsp_plane.model = 0U;
	source.value.bsp_plane.leaf = 1U;
	source.value.bsp_plane.plane = plane;
	return source;
}

static sg_rune_compact_source_t SplitSource(uint32_t parent,
	uint32_t ordinal)
{
	sg_rune_compact_source_t source;

	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_SPLIT;
	source.value.split.parent_facet.value = parent;
	source.value.split.ordinal = ordinal;
	return source;
}

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void InitFixture(compact_fixture_t *fixture)
{
	sg_rune_compact_model_t *model;

	memset(fixture, 0, sizeof(*fixture));
	fixture->vertices[0] = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->vertices[1] = (sg_rune_q8_vec3_t){ { 64, 64, 0 } };
	fixture->vertices[2] = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->vertices[3] = (sg_rune_q8_vec3_t){ { 64, 0, 64 } };

	fixture->cells[0].source = (sg_rune_compact_cell_source_t){
		0U, 1U, 2U, 3, 0U
	};
	fixture->cells[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	fixture->cells[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING |
		SG_RUNE_STANCE_VALID_CROUCHING;
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 1U };
	fixture->cells[0].movement_fields =
		(sg_rune_movement_field_span_t){ 0U, 1U };
	fixture->cells[0].weapon_regions =
		(sg_rune_weapon_response_region_span_t){ 0U, 1U };

	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].source.leaf = 2U;
	fixture->cells[1].bounds.mins.value[0] = 64;
	fixture->cells[1].bounds.maxs.value[0] = 128;
	fixture->cells[1].incidences.first = 1U;
	fixture->cells[1].movement_fields.first = 1U;
	fixture->cells[1].weapon_regions =
		(sg_rune_weapon_response_region_span_t){ 1U, 1U };

	fixture->facets[0].source = BspPlaneSource(4U);
	fixture->facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[0].plane.normal_bits[1] = Bits(0.0f);
	fixture->facets[0].plane.normal_bits[2] = Bits(0.0f);
	fixture->facets[0].plane.distance_bits = Bits(8.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 4U };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 2U };

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

	fixture->portals[0].source = SplitSource(0U, 0U);
	fixture->portals[0].facet.value = 0U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].clearance_q8 = 32U;

	fixture->movement_fields[0].cell.value = 0U;
	fixture->movement_fields[0].boundary_portal.value = 0U;
	fixture->movement_fields[0].family = SG_RUNE_MOVEMENT_FIELD_HOOK;
	fixture->movement_fields[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_fields[0].functions =
		(sg_rune_analytic_function_span_t){ 0U, 3U };
	fixture->movement_fields[1] = fixture->movement_fields[0];
	fixture->movement_fields[1].cell.value = 1U;
	fixture->movement_fields[1].boundary_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fields[1].functions =
		(sg_rune_analytic_function_span_t){ 3U, 3U };
	fixture->analytic_function_refs[0].value = 0U;
	fixture->analytic_function_refs[1].value = 1U;
	fixture->analytic_function_refs[2].value = 5U;
	fixture->analytic_function_refs[3].value = 0U;
	fixture->analytic_function_refs[4].value = 1U;
	fixture->analytic_function_refs[5].value = 5U;

	fixture->weapon_regions[0].cell.value = 0U;
	fixture->weapon_regions[0].boundary_incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 1U };
	fixture->weapon_regions[0].kernels =
		(sg_rune_weapon_response_kernel_span_t){ 0U, 13U };
	fixture->weapon_regions[1] = fixture->weapon_regions[0];
	fixture->weapon_regions[1].cell.value = 1U;
	fixture->weapon_regions[1].boundary_incidences =
		(sg_rune_compact_cell_incidence_span_t){ 1U, 1U };
	fixture->weapon_regions[1].kernels =
		(sg_rune_weapon_response_kernel_span_t){ 13U, 13U };
	{
		uint32_t reference_cursor = 6U;
		uint32_t kernel_cursor = 0U;
		uint32_t region;
		uint32_t profile;

		for (profile = 0U; profile < 12U; profile++) {
			fixture->weapon_profiles[profile].source_profile = profile + 1U;
			fixture->weapon_profiles[profile].response_families =
				SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(profile);
		}
		fixture->weapon_profiles[SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT].
			response_families |= SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);

		for (region = 0U; region < 2U; region++) {
			uint32_t profile_index;

			for (profile_index = 0U; profile_index < 12U; profile_index++) {
				uint32_t family;

				for (family = 0U;
					family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
					family++) {
					const int grenade =
						family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT ||
						family == SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE;
					const uint32_t bit =
						SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);

					if ((fixture->weapon_profiles[profile_index].response_families &
						bit) == 0U)
						continue;
					fixture->weapon_kernels[kernel_cursor].region.value = region;
					fixture->weapon_kernels[kernel_cursor].profile = profile_index;
					fixture->weapon_kernels[kernel_cursor].family =
						(sg_rune_weapon_response_family_t)family;
					fixture->weapon_kernels[kernel_cursor].functions.first =
						reference_cursor;
					fixture->weapon_kernels[kernel_cursor].functions.count =
						grenade ? 5U : 4U;
					fixture->analytic_function_refs[reference_cursor++].value = 1U;
					fixture->analytic_function_refs[reference_cursor++].value = 2U;
					fixture->analytic_function_refs[reference_cursor++].value = 3U;
					fixture->analytic_function_refs[reference_cursor++].value = 4U;
					if (grenade)
						fixture->analytic_function_refs[reference_cursor++].value = 6U;
					kernel_cursor++;
				}
			}
		}
	}

	for (uint32_t index = 0U; index < 7U; index++) {
		fixture->analytic_functions[index].definition = index;
		fixture->analytic_functions[index].form =
			SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->analytic_constants[index].value.bits = Bits((float)index + 1.0f);
	}
	fixture->analytic_functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->analytic_functions[1].output =
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->analytic_functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->analytic_functions[3].output =
		SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY;
	fixture->analytic_functions[4].output =
		SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION;
	fixture->analytic_functions[5].output =
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->analytic_functions[6].output =
		SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->analytic_functions;
	fixture->analytic.function_count = 7U;
	fixture->analytic.constants = fixture->analytic_constants;
	fixture->analytic.constant_count = 7U;

	fixture->landmarks[0].source.entity_ordinal = 20U;
	fixture->landmarks[0].cells =
		(sg_rune_compact_landmark_cell_span_t){ 0U, 1U };
	fixture->landmarks[0].mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->landmarks[0].origin = (sg_rune_q8_vec3_t){ { 16, 16, 8 } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 8, 8, 0 } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 24, 24, 16 } };
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[1] = fixture->landmarks[0];
	fixture->landmarks[1].source.entity_ordinal = 21U;
	fixture->landmarks[1].cells =
		(sg_rune_compact_landmark_cell_span_t){ 1U, 1U };
	fixture->landmarks[1].origin.value[0] = 80;
	fixture->landmarks[1].bounds.mins.value[0] = 72;
	fixture->landmarks[1].bounds.maxs.value[0] = 88;
	fixture->landmark_cells[0].value = 0U;
	fixture->landmark_cells[1].value = 1U;
	fixture->facet_annotations[0].facet.value = 0U;
	fixture->facet_annotations[0].attributes = SG_RUNE_COMPACT_FACET_HOOKABLE;
	fixture->facet_annotations[0].hookable_stances =
		SG_RUNE_STANCE_VALID_STANDING | SG_RUNE_STANCE_VALID_CROUCHING;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 2U;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 2U;
	fixture->static_data.facet_annotations = fixture->facet_annotations;
	fixture->static_data.facet_annotation_count = 1U;

	model = &fixture->model;
	model->version = SG_RUNE_COMPACT_MODEL_VERSION;
	model->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model->identity.bsp_sha256[0] = UINT8_C(0x5a);
	model->identity.bsp_bytes = UINT64_C(1024);
	model->identity.bsp_checksum = UINT32_C(0x101);
	model->identity.entity_crc32 = UINT32_C(0x102);
	model->identity.entity_semantics_id = UINT64_C(0x202);
	model->identity.physics_abi_id = UINT64_C(0x303);
	model->identity.collision_law_id = UINT64_C(0x3031);
	model->identity.pmove_law_id = UINT64_C(0x3032);
	model->identity.gravity_law_id = UINT64_C(0x3033);
	model->identity.hook_law_id = UINT64_C(0x3034);
	model->identity.mechanism_law_id = UINT64_C(0x304);
	model->identity.weapon_law_id = UINT64_C(0x305);
	model->identity.construction_id = UINT64_C(0x306);
	model->identity.schema_id = UINT64_C(0x404);
	model->identity.producer_identity = UINT64_C(0x50524f4455434552);
	model->identity.source_counts = (sg_rune_compact_source_counts_t){
		1U, 3U, 4U, 5U, 1U, 1U, 32U
	};
	model->identity.standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 256 } };
	model->identity.crouching_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.crouching_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 128 } };
	model->identity.physics.gravity_bits = Bits(100.0f);
	model->identity.physics.ground_acceleration_bits = Bits(10.0f);
	model->identity.physics.air_acceleration_bits = Bits(1.0f);
	model->identity.physics.water_acceleration_bits = Bits(4.0f);
	model->identity.physics.hook_acceleration_bits = Bits(1000.0f);
	model->identity.physics.external_acceleration_bits = Bits(1200.0f);
	model->identity.physics.water_drag_bits = Bits(0.5f);
	model->identity.physics.max_velocity_bits = Bits(800.0f);
	model->identity.physics.frame_ms = 8U;
	model->identity.physics.substep_ms = 1U;
	model->cells = fixture->cells;
	model->cell_count = 2U;
	model->facets = fixture->facets;
	model->facet_count = 1U;
	model->incidences = fixture->incidences;
	model->incidence_count = 2U;
	model->cell_incidences = fixture->cell_incidences;
	model->cell_incidence_count = 2U;
	model->vertices = fixture->vertices;
	model->vertex_count = 4U;
	model->portals = fixture->portals;
	model->portal_count = 1U;
	model->movement_fields = fixture->movement_fields;
	model->movement_field_count = 2U;
	model->weapon_regions = fixture->weapon_regions;
	model->weapon_region_count = 2U;
	model->weapon_profiles = fixture->weapon_profiles;
	model->weapon_profile_count = 12U;
	model->weapon_kernels = fixture->weapon_kernels;
	model->weapon_kernel_count = 26U;
	model->analytic_function_refs = fixture->analytic_function_refs;
	model->analytic_function_ref_count = 114U;
	model->analytic = &fixture->analytic;
	model->static_data = &fixture->static_data;
}

static void CheckValid(const sg_rune_compact_model_t *model)
{
	sg_rune_compact_error_t error;

	CHECK(SG_RuneCompactModelValidate(model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONE);
}

static void CheckIdentityRejected(const compact_fixture_t *fixture,
	const sg_rune_compact_identity_t *expected)
{
	sg_rune_compact_error_t error;

	CHECK(!SG_RuneCompactIdentityMatches(&fixture->model.identity, expected));
	CHECK(!SG_RuneCompactModelValidateBound(&fixture->model, expected, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH);
}

#define CHECK_IDENTITY_MUTATION(fixture, expected, mutation) do { \
	(expected) = (fixture).model.identity; \
	mutation; \
	CheckIdentityRejected(&(fixture), &(expected)); \
} while (0)

static void TestExpectedIdentityBinding(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_identity_t expected;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	expected = fixture.model.identity;
	CHECK(SG_RuneCompactIdentityMatches(&fixture.model.identity, &expected));
	CHECK(SG_RuneCompactModelValidateBound(&fixture.model, &expected, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONE);
	CHECK(!SG_RuneCompactIdentityMatches(NULL, &expected));
	CHECK(!SG_RuneCompactIdentityMatches(&fixture.model.identity, NULL));
	CHECK(!SG_RuneCompactModelValidateBound(&fixture.model, NULL, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT);

	CHECK_IDENTITY_MUTATION(fixture, expected, expected.bsp_sha256[31] ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.bsp_bytes ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.bsp_checksum ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.entity_crc32 ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.entity_semantics_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.physics_abi_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.collision_law_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.pmove_law_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.gravity_law_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.hook_law_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.mechanism_law_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.weapon_law_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.construction_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.schema_id ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.producer_identity ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.model_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.leaf_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.area_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.plane_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.brush_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.brush_side_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.source_counts.entity_count ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.standing_hull.mins.value[0] ^= 1);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.standing_hull.maxs.value[2] ^= 1);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.crouching_hull.mins.value[1] ^= 1);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.crouching_hull.maxs.value[2] ^= 1);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.gravity_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.ground_acceleration_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.air_acceleration_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.water_acceleration_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.hook_acceleration_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.external_acceleration_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.water_drag_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.max_velocity_bits ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected, expected.physics.frame_ms ^= 1U);
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.physics.substep_ms ^= 1U);

	InitFixture(&fixture);
	expected = fixture.model.identity;
	fixture.cells[0].valid_stances = 0U;
	CHECK(!SG_RuneCompactModelValidateBound(&fixture.model, &expected, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_STANCE);
}

#undef CHECK_IDENTITY_MUTATION

static void TestFacetPolygonGeometry(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;
	sg_rune_q8_vec3_t first;

	InitFixture(&fixture);
	fixture.vertices[3] = fixture.vertices[1];
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	fixture.vertices[2].value[0] += 2;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	fixture.vertices[0] = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture.vertices[1] = (sg_rune_q8_vec3_t){ { 64, 16, 0 } };
	fixture.vertices[2] = (sg_rune_q8_vec3_t){ { 64, 32, 0 } };
	fixture.vertices[3] = (sg_rune_q8_vec3_t){ { 64, 48, 0 } };
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	first = fixture.vertices[1];
	fixture.vertices[1] = fixture.vertices[3];
	fixture.vertices[3] = first;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	first = fixture.vertices[0];
	fixture.vertices[0] = fixture.vertices[1];
	fixture.vertices[1] = fixture.vertices[2];
	fixture.vertices[2] = fixture.vertices[3];
	fixture.vertices[3] = first;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER);

	InitFixture(&fixture);
	fixture.vertices[2] = (sg_rune_q8_vec3_t){ { 64, 16, 16 } };
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	fixture.vertices[0] = (sg_rune_q8_vec3_t){ { 64, -81, -59 } };
	fixture.vertices[1] = (sg_rune_q8_vec3_t){ { 64, 100, 0 } };
	fixture.vertices[2] = (sg_rune_q8_vec3_t){ { 64, -81, 59 } };
	fixture.vertices[3] = (sg_rune_q8_vec3_t){ { 64, 31, -95 } };
	fixture.vertices[4] = (sg_rune_q8_vec3_t){ { 64, 31, 95 } };
	fixture.facets[0].vertices.count = 5U;
	fixture.model.vertex_count = 5U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);
}

static void TestSharedCellsAndAttachments(void)
{
	compact_fixture_t fixture;
	uint32_t cursor = 0U;
	uint32_t region;

	InitFixture(&fixture);
	CheckValid(&fixture.model);
	CHECK(fixture.weapon_profiles[SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT].
		response_families ==
		(SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT) |
		 SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH)));
	for (region = 0U; region < fixture.model.weapon_region_count; region++) {
		uint32_t profile;

		for (profile = 0U; profile < fixture.model.weapon_profile_count;
			profile++) {
			uint32_t family;

			for (family = 0U;
				family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
				family++) {
				const uint32_t bit =
					SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);

				if ((fixture.weapon_profiles[profile].response_families &
					bit) == 0U)
					continue;
				CHECK(cursor < fixture.model.weapon_kernel_count);
				CHECK(fixture.weapon_kernels[cursor].region.value == region);
				CHECK(fixture.weapon_kernels[cursor].profile == profile);
				CHECK((uint32_t)fixture.weapon_kernels[cursor].family == family);
				cursor++;
			}
		}
	}
	CHECK(cursor == fixture.model.weapon_kernel_count);
	CHECK(fixture.model.weapon_kernel_count == 26U);
	CHECK(fixture.model.analytic_function_ref_count == 114U);

	CHECK(fixture.cells[0].movement_fields.count == 1U);
	CHECK(fixture.cells[0].weapon_regions.count == 1U);
	CHECK(fixture.weapon_regions[0].cell.value ==
		fixture.movement_fields[0].cell.value);
	CHECK(SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT == 12);
}

static void TestWeaponSchemaRejections(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.weapon_profiles[0].response_families |=
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.weapon_kernels[6].profile =
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL);
	CHECK(error.record == 6U);

	InitFixture(&fixture);
	fixture.weapon_profiles[SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH].
		response_families = SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	CHECK((fixture.weapon_profiles[fixture.weapon_kernels[7].profile].
		response_families & SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			fixture.weapon_kernels[7].family)) == 0U);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL);
	CHECK(error.record == 7U);

	InitFixture(&fixture);
	fixture.weapon_kernels[7].profile =
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT;
	fixture.weapon_kernels[7].family =
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL);
	CHECK(error.record == 7U);
}

static void TestHalfOpenPortal(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.incidences[1].boundary = SG_RUNE_BOUNDARY_CLOSED;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY);
	fixture.incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture.incidences[1].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY);
}

static void TestStanceOwnership(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING |
		SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.cells[1].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_STANCE);
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.cells[0].valid_stances = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_STANCE);
	InitFixture(&fixture);
	fixture.movement_fields[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
}

static void TestCanonicalReservedAndPortalOwnership(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.cells[0].reserved[1] = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED);
	InitFixture(&fixture);
	fixture.portals[0].reserved[2] = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED);
	InitFixture(&fixture);
	fixture.portals[0].facet.value = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY);
}

static void TestCanonicalOrderingAndProvenance(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.cells[1].source = fixture.cells[0].source;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER);
	InitFixture(&fixture);
	fixture.portals[0].source.value.split.parent_facet.value = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE);
	InitFixture(&fixture);
	fixture.cells[0].source.leaf = fixture.model.identity.source_counts.leaf_count;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE);
	InitFixture(&fixture);
	fixture.facets[0].source.value.bsp_plane.plane =
		fixture.model.identity.source_counts.plane_count;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE);
}

static void TestAnalyticFields(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.analytic_functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.analytic_constants[3].value.bits = UINT32_C(0x7f800000);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.weapon_kernels[1].region.value = 2U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.weapon_profiles[1].source_profile =
		fixture.weapon_profiles[0].source_profile;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER);
	InitFixture(&fixture);
	fixture.analytic_functions[7] = fixture.analytic_functions[6];
	fixture.analytic_functions[7].definition = 7U;
	fixture.analytic_constants[7].value.bits = Bits(100.0f);
	fixture.analytic.function_count = 8U;
	fixture.analytic.constant_count = 8U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER);
}

static void TestRequiredCoverage(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.model.movement_field_count = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED);
	InitFixture(&fixture);
	fixture.cells[1].weapon_regions.count = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.movement_fields[1].family = SG_RUNE_MOVEMENT_FIELD_AIR;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.analytic_function_refs[6].value = 3U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.static_data.landmark_count = 0U;
	fixture.static_data.landmarks = NULL;
	fixture.static_data.landmark_cell_count = 0U;
	fixture.static_data.landmark_cells = NULL;
	fixture.static_data.facet_annotation_count = 0U;
	fixture.static_data.facet_annotations = NULL;
	CheckValid(&fixture.model);
	InitFixture(&fixture);
	fixture.model.static_data = NULL;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT);
}

int main(void)
{
	TestSharedCellsAndAttachments();
	TestWeaponSchemaRejections();
	TestHalfOpenPortal();
	TestStanceOwnership();
	TestCanonicalReservedAndPortalOwnership();
	TestCanonicalOrderingAndProvenance();
	TestAnalyticFields();
	TestRequiredCoverage();
	TestExpectedIdentityBinding();
	TestFacetPolygonGeometry();
	if (failures != 0) {
		fprintf(stderr, "%d compact RUNE model checks failed\n", failures);
		return 1;
	}
	puts("compact RUNE model checks passed");
	return 0;
}
