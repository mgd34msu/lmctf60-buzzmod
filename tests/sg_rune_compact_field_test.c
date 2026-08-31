#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_compact_field.h"

static int failures;

#if defined(SG_RUNE_COMPACT_FIELD_TEST_WRAP_CALLOC)
static int fail_calloc_after = -1;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (fail_calloc_after == 0) {
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

enum
{
	CELL_COUNT = 4,
	FACET_COUNT = 4,
	INCIDENCE_COUNT = 7,
	VERTEX_COUNT = 16,
	PORTAL_COUNT = 3,
	MOVEMENT_FIELD_COUNT = 8,
	WEAPON_PROFILE_COUNT = 12,
	WEAPON_KERNELS_PER_REGION = 13,
	WEAPON_KERNEL_COUNT = CELL_COUNT * WEAPON_KERNELS_PER_REGION,
	MOVEMENT_REFERENCE_COUNT = MOVEMENT_FIELD_COUNT * 3,
	WEAPON_REFERENCE_COUNT = CELL_COUNT * 54,
	REFERENCE_COUNT = MOVEMENT_REFERENCE_COUNT + WEAPON_REFERENCE_COUNT,
	FUNCTION_COUNT = 7,
	CONSTANT_COUNT = 5,
	AFFINE_COUNT = 2,
	INPUT_COUNT = SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT + 1,
	SLOPE_COUNT = SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT + 1
};

typedef struct field_fixture_s
{
	sg_rune_compact_cell_t cells[CELL_COUNT];
	sg_rune_compact_facet_t facets[FACET_COUNT];
	sg_rune_compact_incidence_t incidences[INCIDENCE_COUNT];
	sg_rune_compact_incidence_index_t cell_incidences[INCIDENCE_COUNT];
	sg_rune_q8_vec3_t vertices[VERTEX_COUNT];
	sg_rune_compact_portal_t portals[PORTAL_COUNT];
	sg_rune_movement_field_attachment_t
		movement_fields[MOVEMENT_FIELD_COUNT];
	sg_rune_weapon_response_region_t weapon_regions[CELL_COUNT];
	sg_rune_weapon_profile_t weapon_profiles[WEAPON_PROFILE_COUNT];
	sg_rune_weapon_response_kernel_t weapon_kernels[WEAPON_KERNEL_COUNT];
	sg_rune_analytic_function_index_t analytic_refs[REFERENCE_COUNT];
	sg_rune_analytic_function_t functions[FUNCTION_COUNT];
	sg_rune_analytic_input_dimension_t input_dimensions[INPUT_COUNT];
	sg_rune_analytic_constant_t constants[CONSTANT_COUNT];
	sg_rune_analytic_affine_t affines[AFFINE_COUNT];
	sg_rune_analytic_scalar_bits_t slopes[SLOPE_COUNT];
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_landmark_t landmark;
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_mechanism_t mechanisms[2];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[4];
	sg_rune_compact_field_mechanism_phase_t mechanism_phases[2];
	sg_rune_compact_field_mechanism_snapshot_t mechanism_snapshot;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
} field_fixture_t;

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

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

static sg_rune_compact_source_t SplitSource(uint32_t parent)
{
	sg_rune_compact_source_t source;

	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_SPLIT;
	source.value.split.parent_facet.value = parent;
	return source;
}

static void InitIdentity(sg_rune_compact_identity_t *identity)
{
	identity->bsp_sha256[0] = UINT8_C(0x5a);
	identity->bsp_bytes = UINT64_C(1024);
	identity->bsp_checksum = UINT32_C(0x101);
	identity->entity_crc32 = UINT32_C(0x102);
	identity->entity_semantics_id = UINT64_C(0x202);
	identity->physics_abi_id = UINT64_C(0x303);
	identity->collision_law_id = UINT64_C(0x3031);
	identity->pmove_law_id = UINT64_C(0x3032);
	identity->gravity_law_id = UINT64_C(0x3033);
	identity->hook_law_id = UINT64_C(0x3034);
	identity->mechanism_law_id = UINT64_C(0x304);
	identity->weapon_law_id = UINT64_C(0x305);
	identity->construction_id = UINT64_C(0x306);
	identity->schema_id = UINT64_C(0x404);
	identity->producer_identity = UINT64_C(0x50524f4455434552);
	identity->source_counts = (sg_rune_compact_source_counts_t){
		1U, 5U, 5U, 4U, 1U, 1U, 32U
	};
	identity->standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	identity->standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 256 } };
	identity->crouching_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	identity->crouching_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 128 } };
	identity->physics.gravity_bits = Bits(100.0f);
	identity->physics.ground_acceleration_bits = Bits(10.0f);
	identity->physics.air_acceleration_bits = Bits(1.0f);
	identity->physics.water_acceleration_bits = Bits(4.0f);
	identity->physics.hook_acceleration_bits = Bits(1000.0f);
	identity->physics.external_acceleration_bits = Bits(1200.0f);
	identity->physics.water_drag_bits = Bits(0.5f);
	identity->physics.max_velocity_bits = Bits(800.0f);
	identity->physics.frame_ms = 8U;
	identity->physics.substep_ms = 1U;
}

static void InitGeometry(field_fixture_t *fixture)
{
	uint32_t cell;

	fixture->vertices[0] = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->vertices[1] = (sg_rune_q8_vec3_t){ { 64, 64, 0 } };
	fixture->vertices[2] = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->vertices[3] = (sg_rune_q8_vec3_t){ { 64, 0, 64 } };
	fixture->vertices[4] = (sg_rune_q8_vec3_t){ { 128, 0, 0 } };
	fixture->vertices[5] = (sg_rune_q8_vec3_t){ { 128, 64, 0 } };
	fixture->vertices[6] = (sg_rune_q8_vec3_t){ { 128, 64, 64 } };
	fixture->vertices[7] = (sg_rune_q8_vec3_t){ { 128, 0, 64 } };
	fixture->vertices[8] = (sg_rune_q8_vec3_t){ { 96, 0, 0 } };
	fixture->vertices[9] = (sg_rune_q8_vec3_t){ { 96, 64, 0 } };
	fixture->vertices[10] = (sg_rune_q8_vec3_t){ { 96, 64, 64 } };
	fixture->vertices[11] = (sg_rune_q8_vec3_t){ { 96, 0, 64 } };
	fixture->vertices[12] = (sg_rune_q8_vec3_t){ { 320, 0, 0 } };
	fixture->vertices[13] = (sg_rune_q8_vec3_t){ { 320, 64, 0 } };
	fixture->vertices[14] = (sg_rune_q8_vec3_t){ { 320, 64, 64 } };
	fixture->vertices[15] = (sg_rune_q8_vec3_t){ { 320, 0, 64 } };
	for (cell = 0U; cell < CELL_COUNT; cell++) {
		fixture->cells[cell].source = (sg_rune_compact_cell_source_t){
			0U, cell + 1U, cell + 1U, (int32_t)cell, 0U
		};
		fixture->cells[cell].incidences =
			(sg_rune_compact_cell_incidence_span_t){ cell, 1U };
		fixture->cells[cell].weapon_regions =
			(sg_rune_weapon_response_region_span_t){ cell, 1U };
		fixture->cells[cell].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	}
	fixture->cells[0].bounds = (sg_rune_q8_bounds_t){
		{ { 0, 0, 0 } }, { { 64, 64, 64 } }
	};
	fixture->cells[1].bounds = (sg_rune_q8_bounds_t){
		{ { 64, 0, 0 } }, { { 128, 64, 64 } }
	};
	fixture->cells[2].bounds = (sg_rune_q8_bounds_t){
		{ { 128, 0, 0 } }, { { 192, 64, 64 } }
	};
	fixture->cells[3].bounds = (sg_rune_q8_bounds_t){
		{ { 256, 0, 0 } }, { { 320, 64, 64 } }
	};
	fixture->cells[0].movement_fields =
		(sg_rune_movement_field_span_t){ 0U, 3U };
	fixture->cells[1].movement_fields =
		(sg_rune_movement_field_span_t){ 3U, 2U };
	fixture->cells[2].movement_fields =
		(sg_rune_movement_field_span_t){ 5U, 2U };
	fixture->cells[3].movement_fields =
		(sg_rune_movement_field_span_t){ 7U, 1U };
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 2U };
	fixture->cells[1].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 2U, 2U };
	fixture->cells[2].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 4U, 2U };
	fixture->cells[3].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 6U, 1U };
	fixture->facets[0].source = BspPlaneSource(0U);
	fixture->facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[0].plane.distance_bits = Bits(8.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 4U };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 2U };
	fixture->facets[0].portal.value = 0U;
	fixture->facets[1].source = BspPlaneSource(1U);
	fixture->facets[1].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[1].plane.distance_bits = Bits(16.0f);
	fixture->facets[1].vertices =
		(sg_rune_compact_vertex_span_t){ 4U, 4U };
	fixture->facets[1].incidences =
		(sg_rune_compact_incidence_span_t){ 2U, 2U };
	fixture->facets[1].portal.value = 1U;
	fixture->facets[2].source = BspPlaneSource(2U);
	fixture->facets[2].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[2].plane.distance_bits = Bits(12.0f);
	fixture->facets[2].vertices =
		(sg_rune_compact_vertex_span_t){ 8U, 4U };
	fixture->facets[2].incidences =
		(sg_rune_compact_incidence_span_t){ 4U, 2U };
	fixture->facets[2].portal.value = 2U;
	fixture->facets[3].source = BspPlaneSource(3U);
	fixture->facets[3].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[3].plane.distance_bits = Bits(40.0f);
	fixture->facets[3].vertices =
		(sg_rune_compact_vertex_span_t){ 12U, 4U };
	fixture->facets[3].incidences =
		(sg_rune_compact_incidence_span_t){ 6U, 1U };
	fixture->facets[3].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].facet.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[1] = fixture->incidences[0];
	fixture->incidences[1].cell.value = 1U;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->incidences[2] = fixture->incidences[0];
	fixture->incidences[2].cell.value = 1U;
	fixture->incidences[2].facet.value = 1U;
	fixture->incidences[2].cell_ordinal = 1U;
	fixture->incidences[3] = fixture->incidences[2];
	fixture->incidences[3].cell.value = 2U;
	fixture->incidences[3].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[3].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->incidences[3].cell_ordinal = 0U;
	fixture->incidences[4] = fixture->incidences[0];
	fixture->incidences[4].cell.value = 0U;
	fixture->incidences[4].facet.value = 2U;
	fixture->incidences[4].cell_ordinal = 1U;
	fixture->incidences[5] = fixture->incidences[4];
	fixture->incidences[5].cell.value = 2U;
	fixture->incidences[5].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[5].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->incidences[6] = fixture->incidences[0];
	fixture->incidences[6].cell.value = 3U;
	fixture->incidences[6].facet.value = 3U;
	fixture->cell_incidences[0].value = 0U;
	fixture->cell_incidences[1].value = 4U;
	fixture->cell_incidences[2].value = 1U;
	fixture->cell_incidences[3].value = 2U;
	fixture->cell_incidences[4].value = 3U;
	fixture->cell_incidences[5].value = 5U;
	fixture->cell_incidences[6].value = 6U;
	fixture->portals[0].source = SplitSource(0U);
	fixture->portals[0].facet.value = 0U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].clearance_q8 = 32U;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portals[1].source = SplitSource(1U);
	fixture->portals[1].facet.value = 1U;
	fixture->portals[1].negative_incidence.value = 2U;
	fixture->portals[1].positive_incidence.value = 3U;
	fixture->portals[1].clearance_q8 = 32U;
	fixture->portals[1].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[1].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portals[2].source = SplitSource(2U);
	fixture->portals[2].facet.value = 2U;
	fixture->portals[2].negative_incidence.value = 4U;
	fixture->portals[2].positive_incidence.value = 5U;
	fixture->portals[2].clearance_q8 = 32U;
	fixture->portals[2].direction =
		SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE;
	fixture->portals[2].valid_stances = SG_RUNE_STANCE_VALID_ALL;
}

static void InitAnalytics(field_fixture_t *fixture)
{
	uint32_t dimension;
	uint32_t field;

	fixture->constants[0].value.bits = Bits(2.5f);
	fixture->constants[1].value.bits = Bits(3.0f);
	fixture->constants[2].value.bits = Bits(0.75f);
	fixture->constants[3].value.bits = Bits(1.0f);
	fixture->constants[4].value.bits = Bits(4.0f);
	fixture->functions[0].definition = 0U;
	fixture->functions[0].output =
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->functions[1].definition = 1U;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->functions[2].definition = 2U;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY;
	fixture->functions[3].definition = 3U;
	fixture->functions[3].output =
		SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION;
	fixture->functions[4].definition = 4U;
	fixture->functions[4].output =
		SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	for (field = 0U; field < CONSTANT_COUNT; field++)
		fixture->functions[field].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->functions[5].inputs =
		(sg_rune_analytic_input_span_t){
			0U, SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT
		};
	fixture->functions[5].definition = 0U;
	fixture->functions[5].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[5].form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	fixture->functions[6].inputs =
		(sg_rune_analytic_input_span_t){
			SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT, 1U
		};
	fixture->functions[6].definition = 1U;
	fixture->functions[6].output =
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->functions[6].form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	for (dimension = 0U;
		dimension < (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
		dimension++) {
		fixture->input_dimensions[dimension] =
			(sg_rune_analytic_input_dimension_t)dimension;
		fixture->slopes[dimension].bits = Bits((float)(dimension + 1U));
	}
	fixture->input_dimensions[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT] =
		SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE;
	fixture->slopes[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT].bits = Bits(1.0f);
	fixture->affines[0].bias.bits = Bits(100.0f);
	fixture->affines[0].slopes =
		(sg_rune_analytic_affine_slope_span_t){
			0U, SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT
		};
	fixture->affines[1].bias.bits = Bits(0.0f);
	fixture->affines[1].slopes =
		(sg_rune_analytic_affine_slope_span_t){
			SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT, 1U
		};
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = FUNCTION_COUNT;
	fixture->analytic.input_dimensions = fixture->input_dimensions;
	fixture->analytic.input_dimension_count = INPUT_COUNT;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = CONSTANT_COUNT;
	fixture->analytic.affines = fixture->affines;
	fixture->analytic.affine_count = AFFINE_COUNT;
	fixture->analytic.affine_slopes = fixture->slopes;
	fixture->analytic.affine_slope_count = SLOPE_COUNT;
	for (field = 0U; field < MOVEMENT_FIELD_COUNT; field++) {
		static const uint32_t cells[MOVEMENT_FIELD_COUNT] = {
			0U, 0U, 0U, 1U, 1U, 2U, 2U, 3U
		};
		static const uint32_t portals[MOVEMENT_FIELD_COUNT] = {
			0U, 0U, 2U, 0U, 1U, 1U, 2U,
			SG_RUNE_COMPACT_INDEX_NONE
		};

		fixture->movement_fields[field].cell.value = cells[field];
		fixture->movement_fields[field].boundary_portal.value = portals[field];
		fixture->movement_fields[field].family = field == 0U ?
			SG_RUNE_MOVEMENT_FIELD_GROUND : SG_RUNE_MOVEMENT_FIELD_HOOK;
		fixture->movement_fields[field].valid_stances =
			SG_RUNE_STANCE_VALID_ALL;
		fixture->movement_fields[field].functions =
			(sg_rune_analytic_function_span_t){ field * 3U, 3U };
		fixture->analytic_refs[field * 3U].value = 5U;
		fixture->analytic_refs[field * 3U + 1U].value = 0U;
		fixture->analytic_refs[field * 3U + 2U].value = 6U;
	}
}

static void InitWeapons(field_fixture_t *fixture)
{
	uint32_t reference_cursor = MOVEMENT_REFERENCE_COUNT;
	uint32_t kernel_cursor = 0U;
	uint32_t profile;
	uint32_t region;

	for (profile = 0U; profile < WEAPON_PROFILE_COUNT; profile++) {
		fixture->weapon_profiles[profile].source_profile = profile + 1U;
		fixture->weapon_profiles[profile].response_families =
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(profile);
	}
	fixture->weapon_profiles[SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT].
		response_families |= SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);
	for (region = 0U; region < CELL_COUNT; region++) {
		fixture->weapon_regions[region].cell.value = region;
		fixture->weapon_regions[region].boundary_incidences =
			(sg_rune_compact_cell_incidence_span_t){
				fixture->cells[region].incidences.first, 1U
			};
		fixture->weapon_regions[region].kernels =
			(sg_rune_weapon_response_kernel_span_t){
				region * WEAPON_KERNELS_PER_REGION,
				WEAPON_KERNELS_PER_REGION
			};
		for (profile = 0U; profile < WEAPON_PROFILE_COUNT; profile++) {
			uint32_t family;

			for (family = 0U;
				family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
				family++) {
				const uint32_t bit =
					SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);
				const int grenade =
					family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT ||
					family == SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE;
				sg_rune_weapon_response_kernel_t *kernel;

				if ((fixture->weapon_profiles[profile].response_families &
					bit) == 0U)
					continue;
				kernel = &fixture->weapon_kernels[kernel_cursor++];
				kernel->region.value = region;
				kernel->profile = profile;
				kernel->family = (sg_rune_weapon_response_family_t)family;
				kernel->functions.first = reference_cursor;
				kernel->functions.count = grenade ? 5U : 4U;
				fixture->analytic_refs[reference_cursor++].value = 0U;
				fixture->analytic_refs[reference_cursor++].value = 1U;
				fixture->analytic_refs[reference_cursor++].value = 2U;
				fixture->analytic_refs[reference_cursor++].value = 3U;
				if (grenade)
					fixture->analytic_refs[reference_cursor++].value = 4U;
			}
		}
	}
	CHECK(kernel_cursor == WEAPON_KERNEL_COUNT);
	CHECK(reference_cursor == REFERENCE_COUNT);
}

static void InitFixture(field_fixture_t *fixture)
{
	sg_rune_compact_model_t *model;

	memset(fixture, 0, sizeof(*fixture));
	InitGeometry(fixture);
	InitAnalytics(fixture);
	InitWeapons(fixture);
	fixture->landmark.source.entity_ordinal = 20U;
	fixture->landmark.cells =
		(sg_rune_compact_landmark_cell_span_t){ 0U, 2U };
	fixture->landmark.mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->landmark.origin = (sg_rune_q8_vec3_t){ { 64, 16, 16 } };
	fixture->landmark.bounds = (sg_rune_q8_bounds_t){
		{ { 56, 8, 8 } }, { { 72, 24, 24 } }
	};
	fixture->landmark.kind = SG_RUNE_COMPACT_LANDMARK_POWERUP;
	fixture->landmark_cells[0].value = 0U;
	fixture->landmark_cells[1].value = 1U;
	fixture->static_data.landmarks = &fixture->landmark;
	fixture->static_data.landmark_count = 1U;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 2U;
	fixture->mechanisms[0].source.entity_ordinal = 10U;
	fixture->mechanisms[0].controller.entity_ordinal = 10U;
	fixture->mechanisms[0].entry_cell.value = 0U;
	fixture->mechanisms[0].exit_cell.value = 1U;
	fixture->mechanisms[0].activation_landmark.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[0].bounds = fixture->cells[0].bounds;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	fixture->mechanisms[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_AUTOMATIC;
	fixture->mechanisms[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture->mechanisms[1] = fixture->mechanisms[0];
	fixture->mechanisms[1].source.entity_ordinal = 11U;
	fixture->mechanisms[1].controller.entity_ordinal = 11U;
	fixture->mechanisms[1].entry_cell.value = 1U;
	fixture->mechanisms[1].exit_cell.value = 2U;
	fixture->mechanisms[1].bounds = fixture->cells[1].bounds;
	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 2U;
	fixture->portal_mechanisms[0].portal.value = 0U;
	fixture->portal_mechanisms[0].mechanism.value = 0U;
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture->portal_mechanisms[1].portal.value = 1U;
	fixture->portal_mechanisms[1].mechanism.value = 1U;
	fixture->portal_mechanisms[1].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture->portal_mechanisms[2].portal.value = 2U;
	fixture->portal_mechanisms[2].mechanism.value = 0U;
	fixture->portal_mechanisms[2].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->static_data.portal_mechanism_count = 3U;
	model = &fixture->model;
	model->version = SG_RUNE_COMPACT_MODEL_VERSION;
	model->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	InitIdentity(&model->identity);
	model->cells = fixture->cells;
	model->cell_count = CELL_COUNT;
	model->facets = fixture->facets;
	model->facet_count = FACET_COUNT;
	model->incidences = fixture->incidences;
	model->incidence_count = INCIDENCE_COUNT;
	model->cell_incidences = fixture->cell_incidences;
	model->cell_incidence_count = INCIDENCE_COUNT;
	model->vertices = fixture->vertices;
	model->vertex_count = VERTEX_COUNT;
	model->portals = fixture->portals;
	model->portal_count = PORTAL_COUNT;
	model->movement_fields = fixture->movement_fields;
	model->movement_field_count = MOVEMENT_FIELD_COUNT;
	model->weapon_regions = fixture->weapon_regions;
	model->weapon_region_count = CELL_COUNT;
	model->weapon_profiles = fixture->weapon_profiles;
	model->weapon_profile_count = WEAPON_PROFILE_COUNT;
	model->weapon_kernels = fixture->weapon_kernels;
	model->weapon_kernel_count = WEAPON_KERNEL_COUNT;
	model->analytic_function_refs = fixture->analytic_refs;
	model->analytic_function_ref_count = REFERENCE_COUNT;
	model->analytic = &fixture->analytic;
	model->static_data = &fixture->static_data;
	fixture->mechanism_phases[0].mechanism.value = 0U;
	fixture->mechanism_phases[0].phase = 2.0f;
	fixture->mechanism_phases[1].mechanism.value = 1U;
	fixture->mechanism_phases[1].phase = 7.0f;
	fixture->mechanism_snapshot.model_identity = &model->identity;
	fixture->mechanism_snapshot.phases = fixture->mechanism_phases;
	fixture->mechanism_snapshot.phase_count = 2U;
}

static sg_rune_compact_field_local_context_t Context(
	const field_fixture_t *fixture, uint32_t cell)
{
	sg_rune_compact_field_local_context_t context;

	memset(&context, 0, sizeof(context));
	context.origin = cell == 0U ?
		(sg_rune_q8_vec3_t){ { 32, 16, 16 } } :
		(cell == 1U ? (sg_rune_q8_vec3_t){ { 96, 16, 16 } } :
		 (cell == 2U ? (sg_rune_q8_vec3_t){ { 160, 16, 16 } } :
		  (sg_rune_q8_vec3_t){ { 288, 16, 16 } }));
	context.stance = SG_RUNE_COMPACT_FIELD_STANDING;
	context.velocity[0] = 1.0f;
	context.velocity[1] = 2.0f;
	context.velocity[2] = 3.0f;
	context.direction[0] = 0.25f;
	context.direction[1] = 0.5f;
	context.direction[2] = 0.75f;
	context.time_seconds = 4.0f;
	context.distance = 5.0f;
	context.support_distance = 6.0f;
	context.fluid_fraction = 0.5f;
	context.hook_length = 7.0f;
	context.target_radius = 8.0f;
	context.mechanisms = &fixture->mechanism_snapshot;
	return context;
}

static sg_rune_compact_field_t *CreateField(field_fixture_t *fixture)
{
	sg_rune_compact_error_t error;
	sg_rune_compact_field_t *field = NULL;

	CHECK(SG_RuneCompactModelValidate(&fixture->model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONE);
	CHECK(SG_RuneCompactFieldCreate(&fixture->model, &fixture->model.identity,
		&field, &error) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CHECK(field != NULL);
	return field;
}

static sg_rune_compact_destination_plan_t *CreatePlan(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination)
{
	sg_rune_compact_destination_plan_t *plan = NULL;

	CHECK(SG_RuneCompactFieldPlanCreate(field, destination, &plan) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CHECK(plan != NULL);
	return plan;
}

static sg_rune_compact_field_result_t Query(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context)
{
	sg_rune_compact_field_result_t result;

	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactFieldQuery(plan, context, &result) ==
		SG_RUNE_COMPACT_FIELD_OK);
	return result;
}

static sg_rune_compact_field_portal_step_t PortalStep(
	const sg_rune_compact_field_result_t *result)
{
	CHECK(result->kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result->value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL);
	return result->value.step.value.portal;
}

static float ExpectedCost(const sg_rune_compact_field_local_context_t *context,
	float mover_phase)
{
	const float values[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT] = {
		(float)context->origin.value[0] / 8.0f,
		(float)context->origin.value[1] / 8.0f,
		(float)context->origin.value[2] / 8.0f,
		context->velocity[0], context->velocity[1], context->velocity[2],
		context->direction[0], context->direction[1], context->direction[2],
		context->time_seconds, context->distance, context->support_distance,
		context->fluid_fraction, mover_phase, context->hook_length,
		context->target_radius
	};
	float cost = 100.0f;
	uint32_t dimension;

	for (dimension = 0U;
		dimension < (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
		dimension++)
		cost += (float)(dimension + 1U) * values[dimension];
	return cost;
}

static sg_rune_compact_destination_t CellDestination(uint32_t cell)
{
	sg_rune_compact_destination_t destination;

	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	destination.value.cell.value = cell;
	return destination;
}

static void TestAllDestinations(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;

	InitFixture(&fixture);
	field = CreateField(&fixture);
	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
	destination.value.point = (sg_rune_q8_vec3_t){ { 104, 24, 24 } };
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 1U);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION);
	CHECK(result.value.destination.kind == SG_RUNE_COMPACT_DESTINATION_POINT);
	CHECK(memcmp(&result.value.destination.value.point,
		&destination.value.point, sizeof(destination.value.point)) == 0);
	CHECK(memcmp(&context.origin, &destination.value.point,
		sizeof(context.origin)) != 0);
	SG_RuneCompactFieldPlanDestroy(plan);
	destination = CellDestination(1U);
	plan = CreatePlan(field, &destination);
	CHECK(Query(plan, &context).kind ==
		SG_RUNE_COMPACT_FIELD_CELL_DESTINATION);
	SG_RuneCompactFieldPlanDestroy(plan);
	destination.kind = SG_RUNE_COMPACT_DESTINATION_SURFACE;
	destination.value.surface.value = 1U;
	plan = CreatePlan(field, &destination);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION);
	CHECK(result.value.destination.value.surface.value == 1U);
	SG_RuneCompactFieldPlanDestroy(plan);
	destination.kind = SG_RUNE_COMPACT_DESTINATION_ITEM;
	destination.value.item.value = 0U;
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	CHECK(Query(plan, &context).kind ==
		SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION);
	context = Context(&fixture, 1U);
	CHECK(Query(plan, &context).kind ==
		SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestCandidateSpecificMechanismPhase(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	sg_rune_compact_field_portal_step_t step;

	InitFixture(&fixture);
	field = CreateField(&fixture);
	destination = CellDestination(1U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	step = PortalStep(&result);
	CHECK(step.next_cell.value == 1U);
	CHECK(step.next_portal.value == 0U);
	CHECK(step.mechanism.value == 0U);
	CHECK(fabsf(step.local_cost - ExpectedCost(&context, 2.0f)) < 0.001f);
	CHECK(fabsf(step.travel_time_seconds - 2.5f) < 0.0001f);
	SG_RuneCompactFieldPlanDestroy(plan);
	destination = CellDestination(2U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 1U);
	result = Query(plan, &context);
	step = PortalStep(&result);
	CHECK(step.next_portal.value == 1U);
	CHECK(step.mechanism.value == 1U);
	CHECK(fabsf(step.local_cost - ExpectedCost(&context, 7.0f)) < 0.001f);
	context = Context(&fixture, 3U);
	CHECK(Query(plan, &context).kind == SG_RUNE_COMPACT_FIELD_DISCONNECTED);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void ChangeDimension(sg_rune_compact_field_local_context_t *context,
	uint32_t dimension)
{
	switch ((sg_rune_analytic_input_dimension_t)dimension) {
	case SG_RUNE_ANALYTIC_INPUT_WORLD_X:
		context->origin.value[0]++;
		break;
	case SG_RUNE_ANALYTIC_INPUT_WORLD_Y:
		context->origin.value[1]++;
		break;
	case SG_RUNE_ANALYTIC_INPUT_WORLD_Z:
		context->origin.value[2]++;
		break;
	case SG_RUNE_ANALYTIC_INPUT_VELOCITY_X:
	case SG_RUNE_ANALYTIC_INPUT_VELOCITY_Y:
	case SG_RUNE_ANALYTIC_INPUT_VELOCITY_Z:
		context->velocity[dimension -
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_VELOCITY_X] += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_DIRECTION_X:
	case SG_RUNE_ANALYTIC_INPUT_DIRECTION_Y:
	case SG_RUNE_ANALYTIC_INPUT_DIRECTION_Z:
		context->direction[dimension -
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIRECTION_X] += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS:
		context->time_seconds += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_DISTANCE:
		context->distance += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_SUPPORT_DISTANCE:
		context->support_distance += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_FLUID_FRACTION:
		context->fluid_fraction += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE:
		break;
	case SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH:
		context->hook_length += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_TARGET_RADIUS:
		context->target_radius += 1.0f;
		break;
	case SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT:
		break;
	}
}

static void TestEveryLocalInput(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t base;
	sg_rune_compact_field_result_t base_result;
	float base_cost;
	uint32_t dimension;

	InitFixture(&fixture);
	field = CreateField(&fixture);
	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	destination.value.cell.value = 1U;
	plan = CreatePlan(field, &destination);
	base = Context(&fixture, 0U);
	base_result = Query(plan, &base);
	base_cost = PortalStep(&base_result).local_cost;
	for (dimension = 0U;
		dimension < (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
		dimension++) {
		sg_rune_compact_field_local_context_t changed = base;
		sg_rune_compact_field_result_t result;
		sg_rune_compact_field_mechanism_phase_t phases[2];
		sg_rune_compact_field_mechanism_snapshot_t snapshot;
		float expected_delta = (float)(dimension + 1U);

		if (dimension == (uint32_t)SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE) {
			memcpy(phases, fixture.mechanism_phases, sizeof(phases));
			phases[0].phase += 1.0f;
			snapshot = fixture.mechanism_snapshot;
			snapshot.phases = phases;
			changed.mechanisms = &snapshot;
		} else {
			ChangeDimension(&changed, dimension);
			if (dimension <= (uint32_t)SG_RUNE_ANALYTIC_INPUT_WORLD_Z)
				expected_delta /= 8.0f;
		}
		result = Query(plan, &changed);
		CHECK(fabsf(PortalStep(&result).local_cost - base_cost - expected_delta) <
			0.001f);
	}
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestDirectionAndStanceTransition(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;

	InitFixture(&fixture);
	fixture.portals[0].direction =
		SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE;
	fixture.portals[2].direction =
		SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE;
	field = CreateField(&fixture);
	destination = CellDestination(0U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 1U);
	CHECK(Query(plan, &context).kind == SG_RUNE_COMPACT_FIELD_DISCONNECTED);
	SG_RuneCompactFieldPlanDestroy(plan);
	destination = CellDestination(1U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	CHECK(Query(plan, &context).kind == SG_RUNE_COMPACT_FIELD_STEP);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);

	InitFixture(&fixture);
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.movement_fields[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.movement_fields[1].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.movement_fields[3].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	field = CreateField(&fixture);
	destination = CellDestination(1U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE);
	CHECK(result.value.step.target_stance ==
		SG_RUNE_COMPACT_FIELD_CROUCHING);
	CHECK(result.value.step.source_rank == 2U);
	CHECK(result.value.step.target_rank == 1U);
	context.stance = SG_RUNE_COMPACT_FIELD_CROUCHING;
	result = Query(plan, &context);
	CHECK(PortalStep(&result).next_portal.value == 0U);
	CHECK(result.value.step.target_stance ==
		SG_RUNE_COMPACT_FIELD_CROUCHING);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestStrictRankAndZeroCost(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	uint32_t slope;
	uint32_t repeat;

	InitFixture(&fixture);
	fixture.affines[0].bias.bits = Bits(0.0f);
	for (slope = 0U;
		slope < (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT; slope++)
		fixture.slopes[slope].bits = Bits(0.0f);
	field = CreateField(&fixture);
	destination = CellDestination(2U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(PortalStep(&result).local_cost == 0.0f);
	CHECK(result.value.step.source_rank == 2U);
	CHECK(result.value.step.target_rank == 1U);
	for (repeat = 0U; repeat < 16U; repeat++) {
		const sg_rune_compact_field_result_t repeated = Query(plan, &context);

		CHECK(repeated.value.step.value.portal.next_cell.value ==
			result.value.step.value.portal.next_cell.value);
		CHECK(repeated.value.step.value.portal.next_portal.value ==
			result.value.step.value.portal.next_portal.value);
		CHECK(repeated.value.step.value.portal.movement_field ==
			result.value.step.value.portal.movement_field);
		CHECK(repeated.value.step.target_rank <
			repeated.value.step.source_rank);
	}
	context = Context(&fixture, 1U);
	result = Query(plan, &context);
	CHECK(PortalStep(&result).next_cell.value == 2U);
	CHECK(result.value.step.source_rank == 1U);
	CHECK(result.value.step.target_rank == 0U);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);

	InitFixture(&fixture);
	fixture.portals[2].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	field = CreateField(&fixture);
	destination = CellDestination(2U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(PortalStep(&result).next_portal.value == 2U);
	CHECK(result.value.step.source_rank == 1U);
	CHECK(result.value.step.target_rank == 0U);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void CheckPortalZeroStatus(field_fixture_t *fixture,
	sg_rune_compact_field_status_t expected)
{
	sg_rune_compact_field_t *field = CreateField(fixture);
	sg_rune_compact_destination_t destination = CellDestination(1U);
	sg_rune_compact_destination_plan_t *plan = CreatePlan(field, &destination);
	sg_rune_compact_field_local_context_t context = Context(fixture, 0U);
	sg_rune_compact_field_result_t result;
	sg_rune_compact_field_result_t sentinel;

	memset(&result, 0xa5, sizeof(result));
	sentinel = result;
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) == expected);
	CHECK(memcmp(&result, &sentinel, sizeof(result)) == 0);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestMechanismFailures(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_mechanism_phase_t phase;
	sg_rune_compact_identity_t wrong_identity;

	InitFixture(&fixture);
	phase = fixture.mechanism_phases[1];
	fixture.mechanism_snapshot.phases = &phase;
	fixture.mechanism_snapshot.phase_count = 1U;
	CheckPortalZeroStatus(&fixture,
		SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED);

	InitFixture(&fixture);
	fixture.mechanism_phases[0].phase = NAN;
	CheckPortalZeroStatus(&fixture,
		SG_RUNE_COMPACT_FIELD_INVALID_MECHANISM_SNAPSHOT);

	InitFixture(&fixture);
	wrong_identity = fixture.model.identity;
	wrong_identity.construction_id++;
	fixture.mechanism_snapshot.model_identity = &wrong_identity;
	CheckPortalZeroStatus(&fixture,
		SG_RUNE_COMPACT_FIELD_INVALID_MECHANISM_SNAPSHOT);

	InitFixture(&fixture);
	fixture.portal_mechanisms[0] = fixture.portal_mechanisms[1];
	fixture.portal_mechanisms[1] = fixture.portal_mechanisms[2];
	fixture.static_data.portal_mechanism_count = 2U;
	CheckPortalZeroStatus(&fixture,
		SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED);

	InitFixture(&fixture);
	fixture.portal_mechanisms[3] = fixture.portal_mechanisms[2];
	fixture.portal_mechanisms[2] = fixture.portal_mechanisms[1];
	fixture.portal_mechanisms[1] = fixture.portal_mechanisms[0];
	fixture.portal_mechanisms[1].mechanism.value = 1U;
	fixture.static_data.portal_mechanism_count = 4U;
	CheckPortalZeroStatus(&fixture,
		SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED);

	InitFixture(&fixture);
	fixture.movement_fields[4].boundary_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	{
		sg_rune_compact_field_t *field = CreateField(&fixture);
		sg_rune_compact_destination_t destination = CellDestination(2U);
		sg_rune_compact_destination_plan_t *plan = CreatePlan(field, &destination);
		sg_rune_compact_field_local_context_t context = Context(&fixture, 1U);
		sg_rune_compact_field_result_t result;
		sg_rune_compact_field_result_t sentinel;

		memset(&result, 0xa5, sizeof(result));
		sentinel = result;
		CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
			SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED);
		CHECK(memcmp(&result, &sentinel, sizeof(result)) == 0);
		SG_RuneCompactFieldPlanDestroy(plan);
		SG_RuneCompactFieldDestroy(field);
	}
}

static void TestInvalidBoundariesAndAllocation(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_field_t *field_sentinel =
		(sg_rune_compact_field_t *)(uintptr_t)1U;
	sg_rune_compact_error_t error;
	sg_rune_compact_identity_t expected;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_destination_plan_t *plan_sentinel =
		(sg_rune_compact_destination_plan_t *)(uintptr_t)1U;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	sg_rune_compact_field_result_t result_sentinel;

	InitFixture(&fixture);
	fixture.cells[0].reserved[0] = 1U;
	CHECK(SG_RuneCompactFieldCreate(&fixture.model, &fixture.model.identity,
		&field_sentinel, &error) == SG_RUNE_COMPACT_FIELD_INVALID_MODEL);
	CHECK(field_sentinel == (sg_rune_compact_field_t *)(uintptr_t)1U);
	InitFixture(&fixture);
	expected = fixture.model.identity;
	expected.schema_id++;
	CHECK(SG_RuneCompactFieldCreate(&fixture.model, &expected,
		&field_sentinel, &error) == SG_RUNE_COMPACT_FIELD_INVALID_MODEL);
	CHECK(field_sentinel == (sg_rune_compact_field_t *)(uintptr_t)1U);

	field = CreateField(&fixture);
	destination = CellDestination(CELL_COUNT);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &plan_sentinel) ==
		SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION);
	CHECK(plan_sentinel ==
		(sg_rune_compact_destination_plan_t *)(uintptr_t)1U);
	destination.kind = SG_RUNE_COMPACT_DESTINATION_KIND_COUNT;
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &plan_sentinel) ==
		SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION);
	destination = CellDestination(1U);
	plan = CreatePlan(field, &destination);
	context = Context(&fixture, 0U);
	memset(&result, 0xa5, sizeof(result));
	result_sentinel = result;
	context.stance = SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT);
	CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);
	context = Context(&fixture, 0U);
	context.velocity[0] = INFINITY;
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT);
	CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);
	context = Context(&fixture, 0U);
	context.origin = (sg_rune_q8_vec3_t){ { 1000, 1000, 1000 } };
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_LOCALIZATION_FAILED);
	CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);
	CHECK(SG_RuneCompactFieldQuery(NULL, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);

	InitFixture(&fixture);
	fixture.landmark.kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	field = CreateField(&fixture);
	destination.kind = SG_RUNE_COMPACT_DESTINATION_ITEM;
	destination.value.item.value = 0U;
	plan_sentinel = (sg_rune_compact_destination_plan_t *)(uintptr_t)1U;
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &plan_sentinel) ==
		SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION);
	CHECK(plan_sentinel ==
		(sg_rune_compact_destination_plan_t *)(uintptr_t)1U);
	SG_RuneCompactFieldDestroy(field);

#if defined(SG_RUNE_COMPACT_FIELD_TEST_WRAP_CALLOC)
	InitFixture(&fixture);
	fail_calloc_after = 0;
	field_sentinel = (sg_rune_compact_field_t *)(uintptr_t)1U;
	CHECK(SG_RuneCompactFieldCreate(&fixture.model, &fixture.model.identity,
		&field_sentinel, &error) == SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED);
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED);
	CHECK(field_sentinel == (sg_rune_compact_field_t *)(uintptr_t)1U);
	InitFixture(&fixture);
	fail_calloc_after = 1;
	field_sentinel = (sg_rune_compact_field_t *)(uintptr_t)1U;
	CHECK(SG_RuneCompactFieldCreate(&fixture.model, &fixture.model.identity,
		&field_sentinel, &error) == SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED);
	CHECK(field_sentinel == (sg_rune_compact_field_t *)(uintptr_t)1U);
	field = CreateField(&fixture);
	destination = CellDestination(1U);
	fail_calloc_after = 0;
	plan_sentinel = (sg_rune_compact_destination_plan_t *)(uintptr_t)1U;
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &plan_sentinel) ==
		SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED);
	CHECK(plan_sentinel ==
		(sg_rune_compact_destination_plan_t *)(uintptr_t)1U);
	SG_RuneCompactFieldDestroy(field);
#endif
	SG_RuneCompactFieldDestroy(NULL);
	SG_RuneCompactFieldPlanDestroy(NULL);
}

int main(void)
{
	TestAllDestinations();
	TestCandidateSpecificMechanismPhase();
	TestEveryLocalInput();
	TestDirectionAndStanceTransition();
	TestStrictRankAndZeroCost();
	TestMechanismFailures();
	TestInvalidBoundariesAndAllocation();
	CHECK(strcmp(SG_RuneCompactFieldStatusString(
		SG_RUNE_COMPACT_FIELD_INVALID_MODEL), "invalid model") == 0);
	CHECK(strcmp(SG_RuneCompactFieldStatusString(
		SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED),
		"mechanism phase required") == 0);
	CHECK(strcmp(SG_RuneCompactFieldStatusString(
		(sg_rune_compact_field_status_t)UINT32_MAX),
		"unknown compact field status") == 0);
	if (failures != 0) {
		fprintf(stderr, "%d compact field tests failed\n", failures);
		return 1;
	}
	puts("sg_rune_compact_field_test: PASS");
	return 0;
}
