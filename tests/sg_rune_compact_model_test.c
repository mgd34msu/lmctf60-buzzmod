#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_mechanisms.h"
#include "../slipgate/sg_rune_compact_static.h"
#include "../slipgate/sg_rune_compact_source_surface_catalog.h"
#include "../slipgate/sg_rune_compact_weapon_field.h"
#include "../slipgate/sg_weapon_effect_profile.h"
#include "../slipgate/sg_weapon_effect_profile.c"

static int failures;

#if defined(SG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC)
static int model_calloc_fail;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (model_calloc_fail != 0) {
		model_calloc_fail = 0;
		return NULL;
	}
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

/* The fixture covers every canonical profile-family pair.  This follows the
 * catalog shape and static-law representation rather than an old reference
 * total. */
#define TEST_CATALOG_WEAPON_FUNCTION_REF_CAPACITY \
	(SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT * \
	 SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT * \
	 (SG_RUNE_WEAPON_STATIC_LAW_COUNT + \
	  SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT * 2U))

int SG_RuneCompactModelTestTeleportStateValid(
	const sg_rune_compact_movement_state_t *state);
int SG_RuneCompactModelTestControllerStateValid(
	const sg_rune_compact_movement_state_t *state);

typedef struct compact_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[3];
	sg_rune_compact_incidence_t incidences[4];
	sg_rune_compact_incidence_index_t cell_incidences[4];
	sg_rune_q8_vec3_t vertices[5];
	sg_rune_compact_portal_t portals[1];
	sg_rune_compact_source_surface_t source_surfaces[3];
	sg_rune_q8_vec3_t source_surface_vertices[12];
	sg_rune_movement_capability_t movement_capabilities[2];
	sg_rune_compact_movement_state_t movement_states[2];
	sg_rune_compact_movement_fiber_t movement_fibers[2];
	sg_rune_compact_movement_hook_target_t movement_hook_targets[1];
	sg_rune_analytic_function_index_t movement_fiber_function_refs[24];
	sg_rune_compact_response_fragment_t response_source_fragments[2];
	sg_rune_compact_response_patch_t response_target_patches[2];
	sg_rune_q8_vec3_t response_target_vertices[6];
	sg_rune_compact_response_split_t response_splits[2];
	sg_rune_compact_response_fact_t response_facts[2];
	sg_rune_compact_response_candidate_group_t response_candidates[1];
	sg_rune_compact_response_endpoint_group_t response_source_groups[1];
	sg_rune_compact_response_endpoint_group_t response_target_groups[1];
	uint32_t response_source_members[2];
	uint32_t response_target_members[2];
	sg_rune_compact_static_occluder_t response_occluders[1];
	sg_rune_weapon_profile_t weapon_profiles[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT];
	sg_rune_weapon_response_kernel_t weapon_kernels[32];
	sg_rune_weapon_function_ref_t weapon_function_refs[
		TEST_CATALOG_WEAPON_FUNCTION_REF_CAPACITY];
	sg_rune_compact_weapon_field_attachment_t weapon_attachments[4];
	sg_rune_compact_weapon_relation_span_t weapon_relation_spans[4];
	sg_rune_compact_response_ref_t weapon_relation_refs[4];
	uint32_t weapon_kernel_count;
	uint32_t weapon_function_ref_count;
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

static uint32_t WeaponFunctionForOutput(
	sg_rune_analytic_output_meaning_t output)
{
	switch (output) {
	case SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS:
		return 1U;
	case SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z:
		return 2U;
	case SG_RUNE_ANALYTIC_OUTPUT_DAMAGE:
		return 3U;
	case SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS:
		return 5U;
	case SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS:
		return 6U;
	case SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE:
		return 7U;
	default:
		return SG_RUNE_COMPACT_INDEX_NONE;
	}
}

static void InitWeaponFixture(compact_fixture_t *fixture)
{
	sg_weapon_law_input_t law = { 0 };
	uint32_t reference_cursor = 0U;
	uint32_t kernel_cursor = 0U;
	uint32_t profile_index;

	law.build_identity = UINT64_C(0x21);
	law.physics_abi_id = UINT64_C(0x303);
	law.weapon_balance_compiled = (uint8_t)SG_WEAPON_BALANCE_COMPILED;
	law.deathmatch_active = 1U;
	for (profile_index = 0U;
		profile_index < SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
		profile_index++) {
		sg_rune_weapon_profile_t *profile =
			&fixture->weapon_profiles[profile_index];
		sg_weapon_profile_t resolved;
		const sg_weapon_profile_id_t id =
			(sg_weapon_profile_id_t)(profile_index + 1U);

		memset(profile, 0, sizeof(*profile));
		memset(&resolved, 0, sizeof(resolved));
		CHECK(SG_WeaponProfileResolve(id, &law, &resolved));
		profile->source_profile = (uint32_t)id;
		profile->response_families =
			SG_RuneCompactWeaponCanonicalProfileMask(profile->source_profile);
		profile->projectile_count_min = resolved.projectile_count_min;
		profile->projectile_count_max = resolved.projectile_count_max;
		profile->auxiliary_trace_count = resolved.auxiliary_trace_count;
		profile->direct_response_count =
			Bits(resolved.direct_damage) == Bits(resolved.direct_damage_max) ?
			2U : 1U;
	}

	for (profile_index = 0U;
		profile_index < SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
		profile_index++) {
		const sg_rune_weapon_profile_t *profile =
			&fixture->weapon_profiles[profile_index];
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++) {
			const uint32_t bit = SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);
			sg_rune_weapon_response_kernel_t *kernel;
			uint32_t reference_count;
			uint32_t ordinal;

			if ((profile->response_families & bit) == 0U)
				continue;
			CHECK(kernel_cursor < 32U);
			kernel = &fixture->weapon_kernels[kernel_cursor++];
			memset(kernel, 0, sizeof(*kernel));
			kernel->profile = profile_index;
			kernel->family = (sg_rune_weapon_response_family_t)family;
			kernel->functions.first = reference_cursor;
			CHECK(SG_RuneCompactWeaponCanonicalEventLaw(
				profile->source_profile, kernel->family,
				&kernel->event_law));
			CHECK(SG_RuneCompactWeaponKernelReferenceCount(profile,
				kernel->family, &reference_count));
			kernel->functions.count = reference_count;
			for (ordinal = 0U; ordinal < reference_count; ordinal++) {
				sg_rune_weapon_effect_channel_t channel;
				uint32_t instance;
				sg_rune_analytic_output_meaning_t output;

				CHECK(SG_RuneCompactWeaponFunctionRefExpected(profile,
					kernel->family, ordinal, &channel, &instance, &output));
				CHECK(reference_cursor <
					(uint32_t)(sizeof(fixture->weapon_function_refs) /
						sizeof(fixture->weapon_function_refs[0])));
				fixture->weapon_function_refs[reference_cursor++] =
					(sg_rune_weapon_function_ref_t){
						{ WeaponFunctionForOutput(output) }, channel, instance };
			}
		}
	}
	fixture->weapon_kernel_count = kernel_cursor;
	fixture->weapon_function_ref_count = reference_cursor;
	CHECK(kernel_cursor == 28U);
	CHECK(reference_cursor != 0U);
}

static void InitResponseFixture(compact_fixture_t *fixture)
{
	uint32_t cell;
	uint32_t vertex;
	uint32_t axis;

	for (cell = 0U; cell < 2U; cell++) {
		const sg_rune_compact_cell_t *compact_cell = &fixture->cells[cell];
		const uint64_t partition_id = ((uint64_t)cell << 32U) | cell;
		const uint32_t vertex_base = cell * 3U;
		sg_rune_compact_response_fragment_t *fragment =
			&fixture->response_source_fragments[cell];
		sg_rune_compact_response_patch_t *patch =
			&fixture->response_target_patches[cell];

		fragment->parent_cell.value = cell;
		fragment->boundary_incidences =
			(sg_rune_compact_cell_incidence_span_t){
				compact_cell->incidences.first, 1U };
		fragment->static_partition_id = partition_id;
		fragment->configuration_region = cell;
		fragment->configuration_cell = cell;
		fragment->bounds = compact_cell->bounds;
		fragment->witness = (sg_rune_q8_vec3_t){ { 16 + 64 * (int32_t)cell,
			16, 8 } };
		fragment->bsp_leaf = compact_cell->source.leaf;
		fragment->bsp_area = compact_cell->source.area;
		fragment->bsp_cluster = (uint32_t)compact_cell->source.cluster;
		fragment->valid_stances = compact_cell->valid_stances;

		patch->visibility_surface_id = (uint64_t)cell + 1U;
		patch->model = 0U;
		patch->brush = 0U;
		patch->brush_side = 0U;
		patch->source_surface = 0U;
		patch->source_frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
		patch->parent_facet.value = 0U;
		patch->target_cell.value = cell;
		patch->boundary_incidences =
			(sg_rune_compact_incidence_span_t){
				compact_cell->incidences.first, 1U };
		patch->static_partition_id = partition_id;
		patch->configuration_region = cell;
		patch->configuration_cell = cell;
		patch->plane = fixture->facets[0].plane;
		patch->first_vertex = vertex_base;
		patch->vertex_count = 3U;
		patch->bounds = compact_cell->bounds;
		patch->bsp_leaf = compact_cell->source.leaf;
		patch->bsp_area = compact_cell->source.area;
		patch->bsp_cluster = (uint32_t)compact_cell->source.cluster;
		patch->valid_stances = compact_cell->valid_stances;

		fixture->response_target_vertices[vertex_base] = fixture->vertices[0];
		fixture->response_target_vertices[vertex_base + 1U] =
			fixture->vertices[1];
		fixture->response_target_vertices[vertex_base + 2U] =
			fixture->vertices[2];
		patch->bounds.mins = fixture->response_target_vertices[vertex_base];
		patch->bounds.maxs = fixture->response_target_vertices[vertex_base];
		for (vertex = 1U; vertex < patch->vertex_count; vertex++)
			for (axis = 0U; axis < 3U; axis++) {
				const int32_t value = fixture->response_target_vertices[
					vertex_base + vertex].value[axis];

				if (value < patch->bounds.mins.value[axis])
					patch->bounds.mins.value[axis] = value;
				if (value > patch->bounds.maxs.value[axis])
					patch->bounds.maxs.value[axis] = value;
			}
	}

	fixture->response_source_groups[0] =
		(sg_rune_compact_response_endpoint_group_t){
			3U, SG_RUNE_COMPACT_RESPONSE_INDEX_NONE, 0U, 0U, 2U };
	fixture->response_target_groups[0] =
		(sg_rune_compact_response_endpoint_group_t){
			3U, SG_RUNE_COMPACT_RESPONSE_INDEX_NONE, 0U, 0U, 2U };
	fixture->response_source_members[0] = 0U;
	fixture->response_source_members[1] = 1U;
	fixture->response_target_members[0] = 0U;
	fixture->response_target_members[1] = 1U;
	fixture->response_candidates[0] =
		(sg_rune_compact_response_candidate_group_t){
			0U, 0U, SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL,
			SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED,
			1U, 0U, { 0U, 0U }, 0U };
	fixture->response_occluders[0] =
		(sg_rune_compact_static_occluder_t){
			0U, 1U, SG_RUNE_COMPACT_CONTENTS_SOLID, 0U };
	fixture->response_splits[0].plane = fixture->facets[0].plane;
	fixture->response_splits[0].kind =
		SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE;
	fixture->response_splits[0].target_surface_id =
		fixture->response_target_patches[0].visibility_surface_id;
	fixture->response_splits[0].occluder = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->response_splits[0].edge = 0U;
	fixture->response_splits[0].brush_side = SG_HOST_COLLISION_BRUSH_NONE;
	fixture->response_splits[1] = fixture->response_splits[0];
	fixture->response_splits[1].kind =
		SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE;
	fixture->response_splits[1].target_surface_id = UINT64_MAX;
	fixture->response_splits[1].occluder = 0U;
	fixture->response_splits[1].brush_side = 1U;
	fixture->response_facts[0].source_fragment = 0U;
	fixture->response_facts[0].target_patch = 0U;
	fixture->response_facts[0].flags =
		SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
	fixture->response_facts[0].visibility =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	fixture->response_facts[0].visibility_reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->response_facts[0].requires_exact_ray = 1U;
	fixture->response_facts[0].target_witness =
		fixture->response_target_vertices[0];
	fixture->response_facts[0].certificate_split =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	fixture->response_facts[0].occluders =
		(sg_rune_compact_static_occluder_span_t){ 0U, 0U };
	fixture->response_facts[0].trace.fraction = 1.0f;
	fixture->response_facts[0].trace.end[0] = 8.0f;
	fixture->response_facts[0].trace.texinfo =
		SG_HOST_COLLISION_TEXINFO_NONE;
	fixture->response_facts[0].trace.brush = SG_HOST_COLLISION_BRUSH_NONE;
	fixture->response_facts[0].trace.brush_side =
		SG_HOST_COLLISION_BRUSH_NONE;
	fixture->response_facts[1].source_fragment = 1U;
	fixture->response_facts[1].target_patch = 1U;
	fixture->response_facts[1].flags =
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
	fixture->response_facts[1].visibility =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	fixture->response_facts[1].visibility_reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->response_facts[1].requires_exact_ray = 1U;
	fixture->response_facts[1].target_witness =
		fixture->response_target_vertices[3];
	fixture->response_facts[1].certificate_split = 1U;
	fixture->response_facts[1].occluders =
		(sg_rune_compact_static_occluder_span_t){ 0U, 1U };
	fixture->response_facts[1].trace.fraction = 1.0f;
	fixture->response_facts[1].trace.end[0] = 8.0f;
	fixture->response_facts[1].trace.plane.normal[0] = 1.0f;
	fixture->response_facts[1].trace.plane.distance = 8.0f;
	fixture->response_facts[1].trace.contents = SG_HOST_CONTENTS_SOLID;
	fixture->response_facts[1].trace.brush = 1U;
	fixture->response_facts[1].trace.brush_side = 1U;

	fixture->model.response.source_fragments =
		fixture->response_source_fragments;
	fixture->model.response.source_fragment_count = 2U;
	fixture->model.response.source_halfspaces = NULL;
	fixture->model.response.source_halfspace_count = 0U;
	fixture->model.response.target_patches = fixture->response_target_patches;
	fixture->model.response.target_patch_count = 2U;
	fixture->model.response.target_vertices = fixture->response_target_vertices;
	fixture->model.response.target_vertex_count = 6U;
	fixture->model.response.splits = fixture->response_splits;
	fixture->model.response.split_count = 2U;
	fixture->model.response.facts = fixture->response_facts;
	fixture->model.response.fact_count = 2U;
	fixture->model.response.candidate_groups = fixture->response_candidates;
	fixture->model.response.candidate_group_count = 1U;
	fixture->model.response.source_endpoint_groups =
		fixture->response_source_groups;
	fixture->model.response.source_endpoint_group_count = 1U;
	fixture->model.response.source_endpoint_members =
		fixture->response_source_members;
	fixture->model.response.source_endpoint_member_count = 2U;
	fixture->model.response.target_endpoint_groups =
		fixture->response_target_groups;
	fixture->model.response.target_endpoint_group_count = 1U;
	fixture->model.response.target_endpoint_members =
		fixture->response_target_members;
	fixture->model.response.target_endpoint_member_count = 2U;
	fixture->model.response.occluders = fixture->response_occluders;
	fixture->model.response.occluder_count = 1U;
	fixture->model.response.exact_live_prefire_trace_required = 1U;
	fixture->model.response.seal.version =
		SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION;
	fixture->model.response.seal.flags =
		SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED;
	fixture->model.response.seal.source_fragment_count = 2U;
	fixture->model.response.seal.target_patch_count = 2U;
	fixture->model.response.seal.split_count = 2U;
	fixture->model.response.seal.response_pair_count = 2U;
	fixture->model.response.seal.certified_direct_pair_count = 1U;
	fixture->model.response.seal.certified_static_impact_pair_count = 1U;
	fixture->model.response.seal.unresolved_response_pair_count = 0U;
	fixture->model.response.seal.unresolved_candidate_group_count = 1U;
	fixture->model.response.seal.source_endpoint_group_count = 1U;
	fixture->model.response.seal.target_endpoint_group_count = 1U;
	fixture->model.response.seal.source_endpoint_member_count = 2U;
	fixture->model.response.seal.target_endpoint_member_count = 2U;
	fixture->model.response.seal.static_occluder_count = 1U;
	fixture->model.response.seal.compact_facet_count =
		fixture->model.facet_count;
	fixture->model.response.seal.compact_cell_count =
		fixture->model.cell_count;
	fixture->model.response.seal.compact_source_surface_count =
		fixture->model.source_surface_count;
	fixture->model.response.seal.compact_source_surface_vertex_count =
		fixture->model.source_surface_vertex_count;
	fixture->model.response.seal.source_surface_catalog_seal =
		SG_RuneCompactSourceSurfaceCatalogSeal(fixture->model.source_surfaces,
			fixture->model.source_surface_count,
			fixture->model.source_surface_vertices,
			fixture->model.source_surface_vertex_count);
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

	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].source.leaf = 2U;
	fixture->cells[1].bounds.mins.value[0] = 64;
	fixture->cells[1].bounds.maxs.value[0] = 128;
	fixture->cells[1].incidences.first = 1U;
	fixture->cells[1].movement_fields.first = 1U;

	fixture->facets[0].source = BspPlaneSource(4U);
	fixture->facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[0].plane.normal_bits[1] = Bits(0.0f);
	fixture->facets[0].plane.normal_bits[2] = Bits(0.0f);
	fixture->facets[0].plane.distance_bits = Bits(8.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 4U };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 2U };
	fixture->facets[0].kind = SG_RUNE_COMPACT_FACET_POLYGON;

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

	for (uint32_t index = 0U; index < 3U; index++) {
		const uint32_t vertex_base = index * 4U;

		fixture->source_surface_vertices[vertex_base] =
			(sg_rune_q8_vec3_t){ { 64, 0, 0 } };
		fixture->source_surface_vertices[vertex_base + 1U] =
			(sg_rune_q8_vec3_t){ { 64, 64, 0 } };
		fixture->source_surface_vertices[vertex_base + 2U] =
			(sg_rune_q8_vec3_t){ { 64, 64, 64 } };
		fixture->source_surface_vertices[vertex_base + 3U] =
			(sg_rune_q8_vec3_t){ { 64, 0, 64 } };
		fixture->source_surfaces[index].plane = fixture->facets[0].plane;
		fixture->source_surfaces[index].vertices =
			(sg_rune_compact_vertex_span_t){ vertex_base, 4U };
	}
	fixture->source_surfaces[0].source =
		(sg_rune_compact_brush_side_source_t){ 0U, 0U, 0U, 0U };
	fixture->source_surfaces[0].frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	fixture->source_surfaces[0].cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[1] = fixture->source_surfaces[0];
	fixture->source_surfaces[1].vertices.first = 4U;
	fixture->source_surfaces[1].cell.value = 1U;
	fixture->source_surfaces[1].parent_surface = 0U;
	fixture->source_surfaces[1].split_ordinal = 1U;
	fixture->source_surfaces[2] = fixture->source_surfaces[0];
	fixture->source_surfaces[2].vertices.first = 8U;
	fixture->source_surfaces[2].source =
		(sg_rune_compact_brush_side_source_t){ 1U, 1U, 1U, 1U };
	fixture->source_surfaces[2].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;

	fixture->movement_states[0].stance = SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_states[0].support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	fixture->movement_states[0].water = SG_RUNE_MOVEMENT_WATER_DRY;
	fixture->movement_states[0].hook_phase = SG_HOST_HOOK_IDLE;
	fixture->movement_states[0].mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_states[1] = fixture->movement_states[0];
	fixture->movement_states[1].hook_phase = SG_HOST_HOOK_IN_FLIGHT;

	fixture->movement_capabilities[0].cell.value = 0U;
	fixture->movement_capabilities[0].boundary_portal.value = 0U;
	fixture->movement_capabilities[0].kind =
		SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;
	fixture->movement_capabilities[0].source_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_capabilities[0].destination_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_capabilities[0].fibers =
		(sg_rune_movement_fiber_span_t){ 0U, 1U };
	fixture->movement_capabilities[1] = fixture->movement_capabilities[0];
	fixture->movement_capabilities[1].cell.value = 1U;
	fixture->movement_capabilities[1].boundary_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_capabilities[1].kind = SG_RUNE_MOVEMENT_CAPABILITY_WALK;
	fixture->movement_capabilities[1].fibers =
		(sg_rune_movement_fiber_span_t){ 1U, 1U };

	fixture->movement_fibers[0].capability.value = 0U;
	fixture->movement_fibers[0].kind = SG_RUNE_MOVEMENT_FIBER_HOOK;
	fixture->movement_fibers[0].state_variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_HOOK |
		SG_RUNE_MOVEMENT_STATE_TIME;
	fixture->movement_fibers[0].source_state.value = 0U;
	fixture->movement_fibers[0].destination_state.value = 1U;
	fixture->movement_fibers[0].functions =
		(sg_rune_analytic_function_span_t){ 0U, 3U };
	fixture->movement_fibers[0].hook_targets =
		(sg_rune_movement_hook_target_span_t){ 0U, 1U };
	fixture->movement_fibers[0].mechanism_transition.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].angular_schedule =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].controller_action_controller.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].controller_action_target.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].capability.value = 1U;
	fixture->movement_fibers[1].kind = SG_RUNE_MOVEMENT_FIBER_PMOVE;
	fixture->movement_fibers[1].state_variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_SUPPORT |
		SG_RUNE_MOVEMENT_STATE_TIME;
	fixture->movement_fibers[1].source_state.value = 0U;
	fixture->movement_fibers[1].destination_state.value = 0U;
	fixture->movement_fibers[1].functions =
		(sg_rune_analytic_function_span_t){ 3U, 3U };
	fixture->movement_fibers[1].hook_targets =
		(sg_rune_movement_hook_target_span_t){ 1U, 0U };
	fixture->movement_fibers[1].mechanism_transition.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].angular_schedule =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].controller_action_controller.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].controller_action_target.value =
		SG_RUNE_COMPACT_INDEX_NONE;

	fixture->movement_hook_targets[0].fiber.value = 0U;
	fixture->movement_hook_targets[0].target_kind = SG_HOST_HOOK_TARGET_WORLD;
	fixture->movement_hook_targets[0].provenance =
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
	fixture->movement_hook_targets[0].response =
		(sg_rune_compact_response_ref_t){
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U };
	fixture->movement_hook_targets[0].visibility_class =
		SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL;
	fixture->movement_hook_targets[0].source_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_hook_targets[0].target_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_hook_targets[0].functions.bolt =
		(sg_rune_analytic_function_span_t){ 6U, 3U };
	fixture->movement_hook_targets[0].functions.body =
		(sg_rune_analytic_function_span_t){ 9U, 3U };
	fixture->movement_hook_targets[0].functions.pull =
		(sg_rune_analytic_function_span_t){ 12U, 3U };
	fixture->movement_hook_targets[0].functions.release =
		(sg_rune_analytic_function_span_t){ 15U, 3U };
	fixture->movement_hook_targets[0].functions.coast =
		(sg_rune_analytic_function_span_t){ 18U, 3U };
	fixture->movement_hook_targets[0].functions.relaunch =
		(sg_rune_analytic_function_span_t){ 21U, 3U };
	for (uint32_t offset = 0U; offset < 24U; offset += 3U) {
		fixture->movement_fiber_function_refs[offset].value = 0U;
		fixture->movement_fiber_function_refs[offset + 1U].value = 1U;
		fixture->movement_fiber_function_refs[offset + 2U].value = 4U;
	}

	InitWeaponFixture(fixture);

	for (uint32_t index = 0U; index < 8U; index++) {
		fixture->analytic_functions[index].definition = index;
		fixture->analytic_functions[index].form =
			SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->analytic_constants[index].value.bits = Bits((float)index + 1.0f);
	}
	fixture->analytic_functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->analytic_functions[1].output =
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->analytic_functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z;
	fixture->analytic_functions[3].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->analytic_functions[4].output =
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->analytic_functions[5].output =
		SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	fixture->analytic_functions[6].output =
		SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
	fixture->analytic_functions[7].output =
		SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE;
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->analytic_functions;
	fixture->analytic.function_count = 8U;
	fixture->analytic.constants = fixture->analytic_constants;
	fixture->analytic.constant_count = 8U;

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
	fixture->facet_annotations[0].source_surface =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->facet_annotations[0].source_frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
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
		2U, 3U, 4U, 5U, 2U, 2U, 32U
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
	model->movement_pmove_abi.version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	model->movement_pmove_abi.game_api_version = 1U;
	model->movement_pmove_abi.import_size = 1U;
	model->movement_pmove_abi.pmove_offset = 1U;
	model->movement_pmove_abi.pmove_size = (uint32_t)sizeof(pmove_t);
	model->movement_pmove_abi.state_size =
		(uint32_t)sizeof(pmove_state_t);
	model->movement_pmove_abi.command_size = (uint32_t)sizeof(usercmd_t);
	model->movement_pmove_abi.fraction_bits = SG_HOST_ENGINE_PMOVE_FRACTION_BITS;
	model->movement_pmove_abi.substep_ms = model->identity.physics.substep_ms;
	model->movement_pmove_abi.identity = model->identity.physics_abi_id;
	model->movement_pmove_behavior_fingerprint = UINT64_C(0x701);
	model->movement_host_level_generation = UINT64_C(0x702);
	model->movement_physics_abi_id = model->identity.physics_abi_id;
	model->movement_collision_law_id = model->identity.collision_law_id;
	model->movement_pmove_law_id = model->identity.pmove_law_id;
	model->movement_gravity_law_id = model->identity.gravity_law_id;
	model->movement_hook_law_id = model->identity.hook_law_id;
	model->movement_mechanism_law_id = model->identity.mechanism_law_id;
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
	model->source_surfaces = fixture->source_surfaces;
	model->source_surface_count = 3U;
	model->source_surface_vertices = fixture->source_surface_vertices;
	model->source_surface_vertex_count = 12U;
	model->movement_capabilities = fixture->movement_capabilities;
	model->movement_capability_count = 2U;
	model->movement_states = fixture->movement_states;
	model->movement_state_count = 2U;
	model->movement_fibers = fixture->movement_fibers;
	model->movement_fiber_count = 2U;
	model->movement_hook_targets = fixture->movement_hook_targets;
	model->movement_hook_target_count = 1U;
	model->movement_fiber_function_refs =
		fixture->movement_fiber_function_refs;
	model->movement_fiber_function_ref_count = 24U;
	model->movement_angular_schedules = NULL;
	model->movement_angular_schedule_count = 0U;
	model->mechanism_authorities = NULL;
	model->mechanism_authority_count = 0U;
	model->mechanism_authority_controllers = NULL;
	model->mechanism_authority_controller_count = 0U;
	model->mechanism_authority_topology_edges = NULL;
	model->mechanism_authority_topology_edge_count = 0U;
	model->mechanism_authority_transitions = NULL;
	model->mechanism_authority_transition_count = 0U;
	model->weapon_profiles = fixture->weapon_profiles;
	model->weapon_profile_count =
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
	CHECK(SG_RuneCompactWeaponProfileCatalogId(model->weapon_profiles,
		model->weapon_profile_count,
		&model->identity.weapon_profile_catalog_id));
	model->weapon_kernels = fixture->weapon_kernels;
	model->weapon_kernel_count = fixture->weapon_kernel_count;
	fixture->weapon_attachments[0] =
		(sg_rune_compact_weapon_field_attachment_t){
			{ 0U }, 0U, SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT,
			0U, { 0U, 1U }, 0U, 0U };
	fixture->weapon_attachments[1] =
		(sg_rune_compact_weapon_field_attachment_t){
			{ 0U }, 0U, SG_RUNE_COMPACT_WEAPON_RELATION_RAIL,
			0U, { 1U, 1U }, 1U, 0U };
	fixture->weapon_attachments[2] =
		(sg_rune_compact_weapon_field_attachment_t){
			{ 0U }, 0U, SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT,
			0U, { 2U, 1U }, 2U, 0U };
	fixture->weapon_attachments[3] =
		(sg_rune_compact_weapon_field_attachment_t){
			{ 1U }, 0U, SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT,
			0U, { 3U, 1U }, 3U, 0U };
	fixture->weapon_relation_spans[0].references =
		fixture->weapon_attachments[0].relations;
	fixture->weapon_relation_spans[1].references =
		fixture->weapon_attachments[1].relations;
	fixture->weapon_relation_spans[2].references =
		fixture->weapon_attachments[2].relations;
	fixture->weapon_relation_spans[3].references =
		fixture->weapon_attachments[3].relations;
	fixture->weapon_relation_refs[0] = (sg_rune_compact_response_ref_t){
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U };
	fixture->weapon_relation_refs[1] = fixture->weapon_relation_refs[0];
	fixture->weapon_relation_refs[2] = fixture->weapon_relation_refs[0];
	fixture->weapon_relation_refs[3] = (sg_rune_compact_response_ref_t){
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 1U };
	model->weapon_attachments = fixture->weapon_attachments;
	model->weapon_attachment_count = 4U;
	model->weapon_relation_spans = fixture->weapon_relation_spans;
	model->weapon_relation_span_count = 4U;
	model->weapon_relation_refs = fixture->weapon_relation_refs;
	model->weapon_relation_ref_count = 4U;
	model->weapon_function_refs = fixture->weapon_function_refs;
	model->weapon_function_ref_count = fixture->weapon_function_ref_count;
	model->analytic = &fixture->analytic;
	model->static_data = &fixture->static_data;
	InitResponseFixture(fixture);
}

static void RebuildCellIncidences(compact_fixture_t *fixture)
{
	uint32_t cell;
	uint32_t cursor = 0U;

	for (cell = 0U; cell < fixture->model.cell_count; cell++) {
		uint32_t incidence;
		uint32_t ordinal = 0U;

		fixture->cells[cell].incidences.first = cursor;
		for (incidence = 0U; incidence < fixture->model.incidence_count;
			incidence++) {
			if (fixture->incidences[incidence].cell.value != cell)
				continue;
			fixture->cell_incidences[cursor++].value = incidence;
			fixture->incidences[incidence].cell_ordinal = ordinal++;
		}
		fixture->cells[cell].incidences.count = ordinal;
		fixture->response_source_fragments[cell].boundary_incidences.first =
			fixture->cells[cell].incidences.first;
		fixture->response_target_patches[cell].boundary_incidences.first =
			fixture->cells[cell].incidences.first;
	}
	fixture->model.cell_incidence_count = cursor;
}

static void AddConstraintFacet(compact_fixture_t *fixture, uint32_t cell)
{
	const uint32_t facet_index = fixture->model.facet_count;
	const uint32_t incidence_index = fixture->model.incidence_count;
	sg_rune_compact_facet_t *facet = &fixture->facets[facet_index];
	sg_rune_compact_incidence_t *incidence =
		&fixture->incidences[incidence_index];

	*facet = fixture->facets[0];
	facet->vertices = (sg_rune_compact_vertex_span_t){
		fixture->model.vertex_count, 0U
	};
	facet->incidences = (sg_rune_compact_incidence_span_t){
		incidence_index, 1U
	};
	facet->portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	facet->kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;

	*incidence = fixture->incidences[cell];
	incidence->cell.value = cell;
	incidence->facet.value = facet_index;

	fixture->model.facet_count++;
	fixture->model.incidence_count++;
	fixture->model.response.seal.compact_facet_count =
		fixture->model.facet_count;
	RebuildCellIncidences(fixture);
}

static void CheckValid(const sg_rune_compact_model_t *model)
{
	sg_rune_compact_error_t error;
	const int valid = SG_RuneCompactModelValidate(model, &error);

	if (!valid)
		fprintf(stderr, "unexpected model error: code=%d domain=%d record=%u\n",
			(int)error.code, (int)error.domain, error.record);
	CHECK(valid);
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
	CHECK_IDENTITY_MUTATION(fixture, expected,
		expected.weapon_profile_catalog_id ^= 1U);
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

static void TestConstraintOnlyFacets(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	AddConstraintFacet(&fixture, 0U);
	CheckValid(&fixture.model);

	InitFixture(&fixture);
	fixture.facets[0].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture.facets[0].vertices.count = 0U;
	fixture.facets[0].incidences.count = 1U;
	fixture.facets[0].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture.facets[1] = fixture.facets[0];
	fixture.facets[1].incidences.first = 1U;
	fixture.incidences[1].facet.value = 1U;
	fixture.model.facet_count = 2U;
	fixture.model.vertex_count = 0U;
	fixture.model.portal_count = 0U;
	fixture.movement_capabilities[0].boundary_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	RebuildCellIncidences(&fixture);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);

	InitFixture(&fixture);
	fixture.facets[0].kind = SG_RUNE_COMPACT_FACET_KIND_COUNT;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	AddConstraintFacet(&fixture, 0U);
	fixture.facets[1].vertices.count = 1U;
	fixture.model.vertex_count++;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY);

	InitFixture(&fixture);
	AddConstraintFacet(&fixture, 0U);
	fixture.facets[1].incidences.count = 2U;
	fixture.incidences[3] = fixture.incidences[1];
	fixture.incidences[3].facet.value = 1U;
	fixture.model.incidence_count++;
	RebuildCellIncidences(&fixture);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY);

	InitFixture(&fixture);
	AddConstraintFacet(&fixture, 0U);
	fixture.facets[1].portal.value = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY);
}

static void TestConstraintOnlyCanonicalOrder(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	AddConstraintFacet(&fixture, 0U);
	AddConstraintFacet(&fixture, 1U);
	CheckValid(&fixture.model);

	InitFixture(&fixture);
	AddConstraintFacet(&fixture, 1U);
	AddConstraintFacet(&fixture, 0U);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER);
}

static void TestSharedCellsAndAttachments(void)
{
	compact_fixture_t fixture;
	uint32_t cursor = 0U;
	uint32_t profile;

	InitFixture(&fixture);
	CheckValid(&fixture.model);
	CHECK(fixture.weapon_profiles[SG_WEAPON_PROFILE_BFG - 1U].
		response_families == SG_RuneCompactWeaponCanonicalProfileMask(
			SG_WEAPON_PROFILE_BFG));
	for (profile = 0U; profile < fixture.model.weapon_profile_count;
		profile++) {
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++) {
			const uint32_t bit = SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);

			if ((fixture.weapon_profiles[profile].response_families & bit) == 0U)
				continue;
			CHECK(cursor < fixture.model.weapon_kernel_count);
			CHECK(fixture.weapon_kernels[cursor].profile == profile);
			CHECK((uint32_t)fixture.weapon_kernels[cursor].family == family);
			cursor++;
		}
	}
	CHECK(cursor == fixture.model.weapon_kernel_count);
	CHECK(fixture.model.weapon_kernel_count == fixture.weapon_kernel_count);
	CHECK(fixture.model.movement_fiber_function_ref_count == 24U);
	CHECK(fixture.model.weapon_function_ref_count ==
		fixture.weapon_function_ref_count);
	CHECK(fixture.model.response.source_fragment_count == 2U);
	CHECK(fixture.model.response.target_patch_count == 2U);
	CHECK(fixture.model.response.candidate_group_count == 1U);
	CHECK(fixture.model.response.source_endpoint_member_count == 2U);
	CHECK(fixture.model.response.target_endpoint_member_count == 2U);
	CHECK(fixture.model.response.fact_count == 2U);
	CHECK(fixture.model.response.facts[0].flags ==
		SG_RUNE_COMPACT_STATIC_RELATION_DIRECT);
	CHECK(fixture.model.response.facts[1].flags ==
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT);
	CHECK(fixture.model.response.facts[1].occluders.count == 1U);
	CHECK(fixture.model.response.facts[1].certificate_split == 1U);
	CHECK(fixture.model.response.facts[1].trace.brush == 1U);
	CHECK(fixture.model.response.facts[1].trace.brush_side == 1U);
	CHECK(fixture.model.response.splits[1].brush_side == 1U);
	CHECK(fixture.cells[0].movement_fields.count == 1U);
	CHECK(fixture.model.response.source_endpoint_groups[0].member_count == 2U);
	CHECK(SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT == 12);
}

static void TestWeaponSchemaRejections(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	/* Every production-certified response fact contributes every relation class
	 * implied by its static flags.  This remains structurally sound after the
	 * rail key is removed, so it specifically proves membership completeness. */
	InitFixture(&fixture);
	fixture.weapon_attachments[1] = fixture.weapon_attachments[2];
	fixture.weapon_attachments[1].relation_span = 1U;
	fixture.weapon_attachments[1].relations =
		(sg_rune_compact_response_ref_span_t){ 1U, 1U };
	fixture.weapon_attachments[2] = fixture.weapon_attachments[3];
	fixture.weapon_attachments[2].relation_span = 2U;
	fixture.weapon_attachments[2].relations =
		(sg_rune_compact_response_ref_span_t){ 2U, 1U };
	fixture.weapon_relation_spans[1].references =
		fixture.weapon_attachments[1].relations;
	fixture.weapon_relation_spans[2].references =
		fixture.weapon_attachments[2].relations;
	fixture.weapon_relation_refs[1] = fixture.weapon_relation_refs[2];
	fixture.weapon_relation_refs[2] = fixture.weapon_relation_refs[3];
	fixture.model.weapon_attachment_count = 3U;
	fixture.model.weapon_relation_span_count = 3U;
	fixture.model.weapon_relation_ref_count = 3U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT);

	InitFixture(&fixture);
	fixture.weapon_profiles[0].response_families |=
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.weapon_kernels[6].family =
		SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL);
	CHECK(error.record == 6U);

	InitFixture(&fixture);
	fixture.weapon_profiles[1].response_families =
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.weapon_kernels[1] = fixture.weapon_kernels[0];
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL);
	CHECK(error.record == 1U);

	InitFixture(&fixture);
	fixture.response_facts[1].trace.brush_side = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 1U);

	InitFixture(&fixture);
	fixture.response_splits[0].brush_side = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_splits[0].kind =
		SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_splits[0].occluder = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_splits[0].edge =
		fixture.response_target_patches[0].vertex_count;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_facts[1].trace.brush_side =
		SG_HOST_COLLISION_BRUSH_NONE;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 1U);

	/* Certified direct facts carry collision's complete canonical no-hit
	 * record; a target surface cannot be smuggled into that trace. */
	InitFixture(&fixture);
	fixture.response_facts[0].trace.texinfo = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_facts[0].trace.plane.normal[0] = 1.0f;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_facts[0].trace.fraction = 0.5f;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	/* An impact is bound to both its published split plane and the exact
	 * static occluder brush; non-NONE provenance alone is insufficient. */
	InitFixture(&fixture);
	fixture.response_facts[1].trace.brush = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 1U);

	InitFixture(&fixture);
	fixture.response_facts[1].trace.plane.distance = 7.0f;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 1U);

	InitFixture(&fixture);
	fixture.response_facts[1].trace.contents = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 1U);

	/* An empty physical target boundary cannot describe a response patch. */
	InitFixture(&fixture);
	fixture.response_target_patches[0].boundary_incidences.count = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_source_fragments[0].configuration_region = UINT32_MAX;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_target_patches[0].bsp_leaf =
		fixture.model.identity.source_counts.leaf_count;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_candidates[0].reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_PVS;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_source_members[1] = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.response_facts[1].occluders.count = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 1U);

	/* The identity must bind the complete canonical profile catalog. */
	InitFixture(&fixture);
	fixture.model.identity.weapon_profile_catalog_id ^= UINT64_C(1);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.weapon_kernels[0].event_law.requirements = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL);
	CHECK(error.record == 0U);

	/* A class attachment owns its same-index span: no span aliasing or
	 * unreferenced span is representable in a sealed v12 model. */
	InitFixture(&fixture);
	fixture.model.weapon_attachment_count = 2U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT);

	InitFixture(&fixture);
	fixture.weapon_attachments[0].relation_span = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.weapon_attachments[0].cell.value = 1U;
	fixture.weapon_relation_refs[0].index = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT);
	CHECK(error.record == 0U);
}

static void TestWeaponFamilyHelpersRejectOutOfRange(void)
{
	compact_fixture_t fixture;
	sg_rune_weapon_event_law_t law;
	uint32_t count;
	const sg_rune_weapon_response_family_t invalid_family =
		(sg_rune_weapon_response_family_t)UINT32_MAX;

	InitFixture(&fixture);
	memset(&law, 0, sizeof(law));
	count = 0U;
	CHECK(!SG_RuneCompactWeaponCanonicalEventLaw(
		fixture.weapon_profiles[0].source_profile, invalid_family, &law));
	CHECK(!SG_RuneCompactWeaponKernelReferenceCount(
		&fixture.weapon_profiles[0], invalid_family, &count));
}

static void TestWeaponStanceAxes(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.response_source_fragments[0].valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture.response_target_patches[0].valid_stances =
		SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.response_source_fragments[1].valid_stances =
		SG_RUNE_STANCE_VALID_CROUCHING;
	fixture.response_target_patches[1].valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	CHECK(SG_RuneCompactModelValidate(&fixture.model, &error));

	InitFixture(&fixture);
	fixture.response_source_fragments[0].valid_stances = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
	CHECK(error.record == 0U);
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
	fixture.movement_capabilities[0].source_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.cells[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_STANCE);
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
	fixture.weapon_kernels[1].profile = fixture.model.weapon_profile_count;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.weapon_profiles[1].source_profile =
		fixture.weapon_profiles[0].source_profile;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.analytic_functions[6].definition = 7U;
	fixture.analytic_functions[7].definition = 6U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
}

static void TestRequiredCoverage(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.model.movement_capability_count = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED);
	InitFixture(&fixture);
	fixture.response_target_groups[0].member_count = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.movement_fiber_function_refs[3].value = 2U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.movement_fibers[1].state_variables &=
		~(uint32_t)SG_RUNE_MOVEMENT_STATE_SUPPORT;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD);
	InitFixture(&fixture);
	fixture.movement_fibers[1].kind = SG_RUNE_MOVEMENT_FIBER_HOOK;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.movement_fibers[1].controller_action_controller.value = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.model.movement_fiber_function_ref_count = 23U;
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

static void TestSourceSurfaceGrammar(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	CheckValid(&fixture.model);

	InitFixture(&fixture);
	fixture.source_surfaces[0].cell.value = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);

	InitFixture(&fixture);
	fixture.source_surfaces[1].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);

	InitFixture(&fixture);
	fixture.source_surfaces[1].parent_surface = 1U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);

	InitFixture(&fixture);
	fixture.source_surfaces[1].split_ordinal = 0U;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);

	InitFixture(&fixture);
	fixture.source_surfaces[1].vertices.first++;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);

	InitFixture(&fixture);
	fixture.source_surfaces[2].source = fixture.source_surfaces[0].source;
	fixture.source_surfaces[2].frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);

	InitFixture(&fixture);
	fixture.source_surface_vertices[4] = fixture.source_surface_vertices[5];
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE);
}

static void TestResponsePatchSurfaceProvenance(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	/* A response patch is authenticated by the retained source-surface
	 * catalog. A compact facet is optional provenance, not a required second
	 * surface representation. */
	InitFixture(&fixture);
	fixture.response_target_patches[0].parent_facet.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture.response_target_patches[0].boundary_incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 0U };
	CheckValid(&fixture.model);

	InitFixture(&fixture);
	fixture.response_target_patches[1].source_surface = 2U;
	fixture.weapon_attachments[3].source_surface = 2U;
	fixture.response_target_patches[1].source_frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	fixture.response_target_patches[1].model =
		fixture.source_surfaces[2].source.model;
	fixture.response_target_patches[1].brush =
		fixture.source_surfaces[2].source.brush;
	fixture.response_target_patches[1].brush_side =
		fixture.source_surfaces[2].source.brush_side;
	fixture.response_target_patches[1].parent_facet.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture.response_target_patches[1].boundary_incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 0U };
	CheckValid(&fixture.model);

	InitFixture(&fixture);
	fixture.response_target_patches[0].parent_facet.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);

	InitFixture(&fixture);
	fixture.response_target_patches[0].flags =
		SG_RUNE_COMPACT_RESPONSE_PATCH_SKY |
		SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);

	InitFixture(&fixture);
	fixture.response_target_patches[0].plane.normal_bits[1] = Bits(1.0f);
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);

	InitFixture(&fixture);
	for (uint32_t vertex = 0U; vertex < 3U; vertex++)
		fixture.response_target_vertices[vertex].value[0] = 8;
	fixture.response_target_patches[0].bounds.mins.value[0] = 8;
	fixture.response_target_patches[0].bounds.maxs.value[0] = 8;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_RESPONSE);
}

static void TestResponsePatchClosedBounds(void)
{
	compact_fixture_t fixture;

	InitFixture(&fixture);
	/* Target patches are planar. Their bounds close on the plane normal. */
	fixture.response_target_patches[0].bounds.mins.value[0] = 64;
	fixture.response_target_patches[0].bounds.maxs.value[0] = 64;
	CheckValid(&fixture.model);
}

static void TestHookLifecycleEndpoints(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fixture.movement_states[1].hook_phase = SG_HOST_HOOK_ATTACHED;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD);

	InitFixture(&fixture);
	fixture.movement_hook_targets[0].target_kind = SG_HOST_HOOK_TARGET_PLAYER;
	fixture.movement_hook_targets[0].provenance =
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC;
	fixture.movement_hook_targets[0].response.kind =
		SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT;
	fixture.movement_hook_targets[0].response.index =
		SG_RUNE_COMPACT_INDEX_NONE;
	CheckValid(&fixture.model);

	fixture.movement_hook_targets[0].target_kind = SG_HOST_HOOK_TARGET_WORLD;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD);

	InitFixture(&fixture);
	fixture.movement_hook_targets[0].target_kind = SG_HOST_HOOK_TARGET_OTHER;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD);

	InitFixture(&fixture);
	fixture.movement_capabilities[0].kind =
		SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE;
	fixture.movement_fibers[0].state_variables |=
		SG_RUNE_MOVEMENT_STATE_SUPPORT;
	fixture.movement_states[0].hook_phase = SG_HOST_HOOK_ATTACHED;
	fixture.movement_states[0].support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	fixture.movement_states[1].hook_phase = SG_HOST_HOOK_COAST;
	fixture.movement_states[1].support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	CHECK(!SG_RuneCompactModelValidate(&fixture.model, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD);
}

#if defined(SG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC)
static void TestTeleportStateLaw(void)
{
	sg_rune_compact_movement_state_t state;

	memset(&state, 0, sizeof(state));
	state.stance = SG_RUNE_STANCE_VALID_STANDING;
	state.support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	state.water = SG_RUNE_MOVEMENT_WATER_DRY;
	state.hook_phase = SG_HOST_HOOK_IDLE;
	state.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(SG_RuneCompactModelTestTeleportStateValid(&state));

	state.support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	state.water = SG_RUNE_MOVEMENT_WATER_SUBMERGED;
	state.flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	CHECK(SG_RuneCompactModelTestTeleportStateValid(&state));

	state.water = SG_RUNE_MOVEMENT_WATER_PARTIAL;
	CHECK(!SG_RuneCompactModelTestTeleportStateValid(&state));
	state.water = SG_RUNE_MOVEMENT_WATER_SUBMERGED;
	state.flags |= SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
	CHECK(!SG_RuneCompactModelTestTeleportStateValid(&state));
	state.flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	state.mover_mechanism = 0U;
	CHECK(!SG_RuneCompactModelTestTeleportStateValid(&state));
	state.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	state.flags = SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE |
		SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	CHECK(!SG_RuneCompactModelTestTeleportStateValid(&state));
	state.flags = 0U;
	CHECK(!SG_RuneCompactModelTestTeleportStateValid(&state));
}

static void TestControllerStateLaw(void)
{
	sg_rune_compact_movement_state_t state;

	memset(&state, 0, sizeof(state));
	state.stance = SG_RUNE_STANCE_VALID_STANDING;
	state.support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	state.water = SG_RUNE_MOVEMENT_WATER_DRY;
	state.hook_phase = SG_HOST_HOOK_IDLE;
	state.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(SG_RuneCompactModelTestControllerStateValid(&state));

	state.support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	state.water = SG_RUNE_MOVEMENT_WATER_SUBMERGED;
	state.flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	CHECK(SG_RuneCompactModelTestControllerStateValid(&state));
	state.support = SG_RUNE_MOVEMENT_SUPPORT_MOVER;
	state.flags = SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
	state.mover_mechanism = 0U;
	CHECK(!SG_RuneCompactModelTestControllerStateValid(&state));
	state.support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	state.flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	state.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	state.hook_phase = SG_HOST_HOOK_ATTACHED;
	CHECK(!SG_RuneCompactModelTestControllerStateValid(&state));
	state.hook_phase = SG_HOST_HOOK_IDLE;
	state.flags |= SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE;
	CHECK(!SG_RuneCompactModelTestControllerStateValid(&state));
	state.flags = 0U;
	CHECK(!SG_RuneCompactModelTestControllerStateValid(&state));
}
#endif

#if defined(SG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC)
static void TestAnalyticUseAllocationFailure(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	model_calloc_fail = 1;
	CHECK(!SG_RuneCompactModelValidateBound(&fixture.model,
		&fixture.model.identity, &error));
	CHECK(error.code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY);
	CHECK(error.domain == SG_RUNE_COMPACT_RECORD_MODEL);
}
#endif

int main(void)
{
	TestSharedCellsAndAttachments();
	TestWeaponSchemaRejections();
	TestWeaponFamilyHelpersRejectOutOfRange();
	TestWeaponStanceAxes();
	TestHalfOpenPortal();
	TestStanceOwnership();
	TestCanonicalReservedAndPortalOwnership();
	TestCanonicalOrderingAndProvenance();
	TestAnalyticFields();
	TestRequiredCoverage();
	TestSourceSurfaceGrammar();
	TestResponsePatchSurfaceProvenance();
	TestResponsePatchClosedBounds();
	TestHookLifecycleEndpoints();
	TestExpectedIdentityBinding();
	TestFacetPolygonGeometry();
	TestConstraintOnlyFacets();
	TestConstraintOnlyCanonicalOrder();
#if defined(SG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC)
	TestTeleportStateLaw();
	TestControllerStateLaw();
	TestAnalyticUseAllocationFailure();
#endif
	if (failures != 0) {
		fprintf(stderr, "%d compact RUNE model checks failed\n", failures);
		return 1;
	}
	puts("compact RUNE model checks passed");
	return 0;
}
