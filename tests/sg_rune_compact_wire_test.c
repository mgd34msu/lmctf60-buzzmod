#include "slipgate/sg_rune_compact_wire.h"
#include "slipgate/sg_rune_compact_mechanisms.h"
#include "slipgate/sg_rune_compact_source_surface_catalog.h"
#include "slipgate/sg_rune_compact_weapon_field.h"
#include "slipgate/sg_weapon_effect_profile.h"

#include "../slipgate/sg_rune_model.c"
#include "../slipgate/sg_weapon_effect_profile.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t calloc_fail_after = SIZE_MAX;
static size_t calloc_count;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

#if defined(SG_RUNE_COMPACT_WIRE_TESTING)
int SG_RuneCompactWireTestTeleportStateValid(const uint8_t *state);
int SG_RuneCompactWireTestControllerStateValid(const uint8_t *state);
#endif

void *__wrap_calloc(size_t count, size_t size)
{
	if (calloc_count++ == calloc_fail_after)
		return NULL;
	return __real_calloc(count, size);
}

#define TEST_HEADER_FIXED UINT32_C(48)
#define TEST_DESCRIPTOR_SIZE UINT32_C(24)
#define TEST_CHECKSUM_OFFSET UINT32_C(24)

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #condition); \
			return 0; \
		} \
	} while (0)

#define TEST_CATALOG_WEAPON_FUNCTION_REF_CAPACITY \
	(SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT * \
	 SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT * \
	 (SG_RUNE_WEAPON_STATIC_LAW_COUNT + \
	  SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT * 2U))

typedef struct test_fixture_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[2];
	sg_rune_compact_incidence_t incidences[3];
	sg_rune_compact_incidence_index_t cell_incidences[3];
	sg_rune_q8_vec3_t vertices[4];
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
	sg_rune_analytic_function_t functions[8];
	sg_rune_analytic_constant_t constants[8];
	sg_rune_compact_mechanism_t mechanisms[2];
	sg_rune_compact_static_mechanism_controller_t mechanism_controllers[2];
	sg_rune_compact_mechanism_edge_t mechanism_edges[2];
	sg_rune_compact_static_transition_t transitions[1];
	sg_rune_compact_landmark_t landmarks[2];
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_facet_annotation_t annotations[1];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[1];
	sg_rune_compact_mechanism_authority_t mechanism_authorities[1];
	sg_rune_compact_mechanism_controller_t mechanism_authority_controllers[1];
	sg_rune_compact_mechanism_topology_edge_t mechanism_authority_topology_edges[1];
	sg_rune_compact_mechanism_transition_t mechanism_authority_transitions[1];
	uint32_t mechanism_authority_transition_static_indices[1];
	uint32_t static_transition_authority_indices[1];
} test_fixture_t;

static uint32_t float_bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static uint32_t weapon_function_for_output(
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

static void init_weapon_fixture(test_fixture_t *fixture)
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
		if (!SG_WeaponProfileResolve(id, &law, &resolved))
			abort();
		profile->source_profile = profile_index + 1U;
		profile->response_families =
			SG_RuneCompactWeaponCanonicalProfileMask(profile_index + 1U);
		profile->projectile_count_min = resolved.projectile_count_min;
		profile->projectile_count_max = resolved.projectile_count_max;
		profile->auxiliary_trace_count = resolved.auxiliary_trace_count;
		profile->direct_response_count =
			float_bits(resolved.direct_damage) ==
			float_bits(resolved.direct_damage_max) ? 2U : 1U;
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
			if (kernel_cursor >= (uint32_t)(sizeof(fixture->weapon_kernels) /
				sizeof(fixture->weapon_kernels[0])))
				abort();
			kernel = &fixture->weapon_kernels[kernel_cursor++];
			memset(kernel, 0, sizeof(*kernel));
			kernel->profile = profile_index;
			kernel->family = (sg_rune_weapon_response_family_t)family;
			kernel->functions.first = reference_cursor;
			if (!SG_RuneCompactWeaponCanonicalEventLaw(
				profile->source_profile, kernel->family, &kernel->event_law))
				abort();
			if (!SG_RuneCompactWeaponKernelReferenceCount(profile,
				kernel->family, &reference_count))
				abort();
			kernel->functions.count = reference_count;
			for (ordinal = 0U; ordinal < reference_count; ordinal++) {
				sg_rune_weapon_effect_channel_t channel;
				uint32_t instance;
				sg_rune_analytic_output_meaning_t output;

				if (!SG_RuneCompactWeaponFunctionRefExpected(profile,
					kernel->family, ordinal, &channel, &instance,
					&output) || reference_cursor >= (uint32_t)(sizeof(
					fixture->weapon_function_refs) /
					sizeof(fixture->weapon_function_refs[0])))
					abort();
				fixture->weapon_function_refs[reference_cursor++] =
					(sg_rune_weapon_function_ref_t){
						{ weapon_function_for_output(output) }, channel, instance };
			}
		}
	}
	fixture->weapon_kernel_count = kernel_cursor;
	fixture->weapon_function_ref_count = reference_cursor;
}

static sg_rune_compact_source_t bsp_plane_source(uint32_t plane)
{
	sg_rune_compact_source_t source;
	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	source.value.bsp_plane.model = 0;
	source.value.bsp_plane.leaf = 1;
	source.value.bsp_plane.plane = plane;
	return source;
}

static sg_rune_compact_source_t split_source(uint32_t parent, uint32_t ordinal)
{
	sg_rune_compact_source_t source;
	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_SPLIT;
	source.value.split.parent_facet.value = parent;
	source.value.split.ordinal = ordinal;
	return source;
}

static void put_u32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
	p[2] = (unsigned char)(value >> 16);
	p[3] = (unsigned char)(value >> 24);
}

static uint32_t get_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#if defined(SG_RUNE_COMPACT_WIRE_TESTING)
static int test_teleport_state_wire_law(void)
{
	uint8_t state[24] = { 0U };

	state[0] = SG_RUNE_STANCE_VALID_STANDING;
	put_u32(state + 4U, SG_RUNE_MOVEMENT_SUPPORT_STATIC);
	put_u32(state + 8U, SG_RUNE_MOVEMENT_WATER_DRY);
	put_u32(state + 12U, SG_HOST_HOOK_IDLE);
	put_u32(state + 20U, SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(SG_RuneCompactWireTestTeleportStateValid(state));

	put_u32(state + 4U, SG_RUNE_MOVEMENT_SUPPORT_NONE);
	put_u32(state + 8U, SG_RUNE_MOVEMENT_WATER_SUBMERGED);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE);
	CHECK(SG_RuneCompactWireTestTeleportStateValid(state));
	put_u32(state + 8U, SG_RUNE_MOVEMENT_WATER_PARTIAL);
	CHECK(!SG_RuneCompactWireTestTeleportStateValid(state));
	put_u32(state + 8U, SG_RUNE_MOVEMENT_WATER_SUBMERGED);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE |
		SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE);
	CHECK(!SG_RuneCompactWireTestTeleportStateValid(state));
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE);
	put_u32(state + 20U, 0U);
	CHECK(!SG_RuneCompactWireTestTeleportStateValid(state));
	put_u32(state + 20U, SG_RUNE_COMPACT_INDEX_NONE);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE |
		SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE);
	CHECK(!SG_RuneCompactWireTestTeleportStateValid(state));
	put_u32(state + 16U, 0U);
	CHECK(!SG_RuneCompactWireTestTeleportStateValid(state));
	return 1;
}

static int test_controller_state_wire_law(void)
{
	uint8_t state[24] = { 0U };

	state[0] = SG_RUNE_STANCE_VALID_STANDING;
	put_u32(state + 4U, SG_RUNE_MOVEMENT_SUPPORT_STATIC);
	put_u32(state + 8U, SG_RUNE_MOVEMENT_WATER_DRY);
	put_u32(state + 12U, SG_HOST_HOOK_IDLE);
	put_u32(state + 20U, SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(SG_RuneCompactWireTestControllerStateValid(state));

	put_u32(state + 4U, SG_RUNE_MOVEMENT_SUPPORT_NONE);
	put_u32(state + 8U, SG_RUNE_MOVEMENT_WATER_SUBMERGED);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE);
	CHECK(SG_RuneCompactWireTestControllerStateValid(state));
	put_u32(state + 4U, SG_RUNE_MOVEMENT_SUPPORT_MOVER);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE);
	put_u32(state + 20U, 0U);
	CHECK(!SG_RuneCompactWireTestControllerStateValid(state));
	put_u32(state + 4U, SG_RUNE_MOVEMENT_SUPPORT_NONE);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE);
	put_u32(state + 20U, SG_RUNE_COMPACT_INDEX_NONE);
	put_u32(state + 12U, SG_HOST_HOOK_ATTACHED);
	CHECK(!SG_RuneCompactWireTestControllerStateValid(state));
	put_u32(state + 12U, SG_HOST_HOOK_IDLE);
	put_u32(state + 16U, SG_RUNE_MOVEMENT_STATE_AIRBORNE |
		SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE);
	CHECK(!SG_RuneCompactWireTestControllerStateValid(state));
	put_u32(state + 16U, 0U);
	CHECK(!SG_RuneCompactWireTestControllerStateValid(state));
	return 1;
}
#endif

static void init_response_fixture(test_fixture_t *fixture)
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
		fragment->witness = (sg_rune_q8_vec3_t){
			{ 16 + 64 * (int32_t)cell, 16, 8 } };
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
			1U, 0U, { 0U, 0U },
			0U };
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
	fixture->model.response.seal.compact_cell_count = fixture->model.cell_count;
	fixture->model.response.seal.compact_source_surface_count =
		fixture->model.source_surface_count;
	fixture->model.response.seal.compact_source_surface_vertex_count =
		fixture->model.source_surface_vertex_count;
	fixture->model.response.seal.source_surface_catalog_seal =
		SG_RuneCompactSourceSurfaceCatalogSeal(
			fixture->model.source_surfaces, fixture->model.source_surface_count,
			fixture->model.source_surface_vertices,
			fixture->model.source_surface_vertex_count);
}

static uint64_t get_u64(const unsigned char *p)
{
	return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

static uint32_t test_checksum(const unsigned char *data, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t i;
	for (i = 0; i < size; ++i)
	{
		uint32_t octet = (uint32_t)data[i];
		uint32_t bit;
		if (i >= TEST_CHECKSUM_OFFSET && i < TEST_CHECKSUM_OFFSET + 4)
			octet = 0;
		crc ^= octet;
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^
				(UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
	}
	return ~crc;
}

static void refresh_checksum(unsigned char *image, size_t size)
{
	put_u32(image + TEST_CHECKSUM_OFFSET, test_checksum(image, size));
}

static uint64_t section_offset(const unsigned char *image, uint32_t section)
{
	return get_u64(image + TEST_HEADER_FIXED +
		section * TEST_DESCRIPTOR_SIZE + 16);
}

static void put_u64(unsigned char *p, uint64_t value)
{
	put_u32(p, (uint32_t)value);
	put_u32(p + 4U, (uint32_t)(value >> 32U));
}

static int repack_weapon_projection_sections(const unsigned char *source,
	size_t source_size, unsigned char *destination, size_t destination_capacity,
	uint32_t attachment_count, uint32_t span_count, uint32_t reference_count,
	size_t *written_out)
{
	const uint32_t section_count = get_u32(source + 12U);
	const size_t header_size = (size_t)TEST_HEADER_FIXED +
		(size_t)section_count * (size_t)TEST_DESCRIPTOR_SIZE;
	size_t cursor = header_size;
	uint32_t section;

	if (section_count != (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT ||
		header_size > source_size || header_size > destination_capacity)
		return 0;
	memset(destination, 0, destination_capacity);
	memcpy(destination, source, header_size);
	for (section = 0U; section < section_count; section++) {
		const unsigned char *source_descriptor = source + TEST_HEADER_FIXED +
			(size_t)section * (size_t)TEST_DESCRIPTOR_SIZE;
		unsigned char *destination_descriptor = destination +
			TEST_HEADER_FIXED + (size_t)section *
			(size_t)TEST_DESCRIPTOR_SIZE;
		const uint32_t old_count = get_u32(source_descriptor + 8U);
		const uint32_t wire_size = get_u32(source_descriptor + 4U);
		const uint64_t source_offset = get_u64(source_descriptor + 16U);
		uint32_t count = old_count;
		size_t bytes;
		size_t copy_bytes;

		if (section == SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS)
			count = attachment_count;
		else if (section ==
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS)
			count = span_count;
		else if (section ==
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS)
			count = reference_count;
		cursor = (cursor + (size_t)7U) & ~(size_t)7U;
		bytes = (size_t)count * (size_t)wire_size;
		copy_bytes = (size_t)(count < old_count ? count : old_count) *
			(size_t)wire_size;
		if (cursor > destination_capacity ||
			bytes > destination_capacity - cursor ||
			source_offset > (uint64_t)source_size ||
			copy_bytes > source_size - (size_t)source_offset)
			return 0;
		put_u32(destination_descriptor + 8U, count);
		put_u64(destination_descriptor + 16U, (uint64_t)cursor);
		memcpy(destination + cursor, source + (size_t)source_offset,
			copy_bytes);
		cursor += bytes;
	}
	cursor = (cursor + (size_t)7U) & ~(size_t)7U;
	if (cursor > destination_capacity)
		return 0;
	put_u64(destination + 16U, (uint64_t)cursor);
	put_u32(destination + TEST_CHECKSUM_OFFSET, 0U);
	*written_out = cursor;
	return 1;
}

typedef enum weapon_projection_omission_e
{
	WEAPON_PROJECTION_OMIT_RAIL,
	WEAPON_PROJECTION_OMIT_DIRECT_IMPACT,
	WEAPON_PROJECTION_OMIT_IMPACT_FACT
} weapon_projection_omission_t;

static int make_weapon_projection_omission(const unsigned char *source,
	size_t source_size, unsigned char *destination, size_t destination_capacity,
	weapon_projection_omission_t omission, size_t *written_out)
{
	const uint64_t source_attachments = section_offset(source,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS);
	const uint64_t destination_attachments = section_offset(destination,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS);
	const uint64_t destination_spans = section_offset(destination,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS);
	const uint64_t destination_references = section_offset(destination,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS);
	const uint32_t attachment_size = UINT32_C(32);
	const uint32_t span_size = UINT32_C(8);
	const uint32_t reference_size = UINT32_C(8);
	const uint32_t fact_indices[3] = { 0U, 0U, 1U };
	uint32_t source_indices[3];
	uint32_t index;

	if (!repack_weapon_projection_sections(source, source_size, destination,
		destination_capacity, 3U, 3U, 3U, written_out))
		return 0;
	if (omission == WEAPON_PROJECTION_OMIT_RAIL) {
		source_indices[0] = 0U;
		source_indices[1] = 2U;
		source_indices[2] = 3U;
	} else if (omission == WEAPON_PROJECTION_OMIT_DIRECT_IMPACT) {
		source_indices[0] = 0U;
		source_indices[1] = 1U;
		source_indices[2] = 3U;
	} else {
		source_indices[0] = 0U;
		source_indices[1] = 1U;
		source_indices[2] = 2U;
	}
	for (index = 0U; index < 3U; index++) {
		unsigned char *attachment = destination +
			(size_t)destination_attachments + (size_t)index * attachment_size;
		unsigned char *span = destination + (size_t)destination_spans +
			(size_t)index * span_size;
		unsigned char *reference = destination +
			(size_t)destination_references + (size_t)index * reference_size;

		memcpy(attachment, source + (size_t)source_attachments +
			(size_t)source_indices[index] * attachment_size, attachment_size);
		put_u32(attachment + 16U, index);
		put_u32(attachment + 24U, index);
		put_u32(span, index);
		put_u32(span + 4U, 1U);
		put_u32(reference, SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
		put_u32(reference + 4U, omission ==
			WEAPON_PROJECTION_OMIT_IMPACT_FACT ? 0U : fact_indices[index]);
	}
	refresh_checksum(destination, *written_out);
	return 1;
}

static int make_duplicate_weapon_projection_reference(
	const unsigned char *source, size_t source_size, unsigned char *destination,
	size_t destination_capacity, size_t *written_out)
{
	const uint64_t attachments = section_offset(destination,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS);
	const uint64_t spans = section_offset(destination,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS);
	const uint64_t references = section_offset(destination,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS);
	uint32_t index;

	if (!repack_weapon_projection_sections(source, source_size, destination,
		destination_capacity, 4U, 4U, 5U, written_out))
		return 0;
	for (index = 0U; index < 4U; index++) {
		unsigned char *span = destination + (size_t)spans +
			(size_t)index * 8U;
		unsigned char *attachment = destination + (size_t)attachments +
			(size_t)index * 32U;
		const uint32_t count = index == 2U ? 2U : 1U;
		const uint32_t first = index < 3U ? index : 4U;

		put_u32(span, first);
		put_u32(span + 4U, count);
		put_u32(attachment + 16U, first);
		put_u32(attachment + 20U, count);
	}
	for (index = 0U; index < 5U; index++) {
		unsigned char *reference = destination + (size_t)references +
			(size_t)index * 8U;

		put_u32(reference, SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
		put_u32(reference + 4U, index == 4U ? 1U : 0U);
	}
	refresh_checksum(destination, *written_out);
	return 1;
}

static void init_fixture(test_fixture_t *fixture)
{
	sg_rune_compact_model_t *model;
	uint32_t i;
	memset(fixture, 0, sizeof(*fixture));

	fixture->vertices[0] = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->vertices[1] = (sg_rune_q8_vec3_t){ { 64, 64, 0 } };
	fixture->vertices[2] = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->vertices[3] = (sg_rune_q8_vec3_t){ { 64, 0, 64 } };

	fixture->cells[0].source = (sg_rune_compact_cell_source_t){ 0, 1, 2, 3, 0 };
	fixture->cells[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0, 1 };
	fixture->cells[0].movement_fields =
		(sg_rune_movement_field_span_t){ 0, 1 };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].source.leaf = 2;
	fixture->cells[1].bounds.mins.value[0] = 64;
	fixture->cells[1].bounds.maxs.value[0] = 128;
	fixture->cells[1].incidences.first = 1;
	fixture->cells[1].incidences.count = 2;
	fixture->cells[1].movement_fields.first = 1;

	fixture->facets[0].source = bsp_plane_source(4);
	fixture->facets[0].source.kind =
		SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE;
	fixture->facets[0].source.value.brush_side =
		(sg_rune_compact_brush_side_source_t){ 0U, 0U, 0U, 0U };
	fixture->facets[0].plane.normal_bits[0] = float_bits(1.0f);
	fixture->facets[0].plane.distance_bits = float_bits(8.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0, 4 };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0, 2 };
	fixture->facets[0].kind = SG_RUNE_COMPACT_FACET_POLYGON;
	fixture->incidences[0].cell.value = 0;
	fixture->incidences[0].facet.value = 0;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[1] = fixture->incidences[0];
	fixture->incidences[1].cell.value = 1;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->facets[1].source = bsp_plane_source(4);
	fixture->facets[1].source.kind =
		SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE;
	fixture->facets[1].source.value.brush_side =
		(sg_rune_compact_brush_side_source_t){ 0U, 1U, 1U, 1U };
	fixture->facets[1].plane.normal_bits[0] = float_bits(1.0f);
	fixture->facets[1].plane.distance_bits = float_bits(8.0f);
	fixture->facets[1].vertices =
		(sg_rune_compact_vertex_span_t){ 4, 0 };
	fixture->facets[1].incidences =
		(sg_rune_compact_incidence_span_t){ 2, 1 };
	fixture->facets[1].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->facets[1].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture->incidences[2].cell.value = 1;
	fixture->incidences[2].facet.value = 1;
	fixture->incidences[2].cell_ordinal = 1;
	fixture->incidences[2].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[2].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->cell_incidences[0].value = 0;
	fixture->cell_incidences[1].value = 1;
	fixture->cell_incidences[2].value = 2;

	fixture->portals[0].source = split_source(0, 0);
	fixture->portals[0].facet.value = 0;
	fixture->portals[0].negative_incidence.value = 0;
	fixture->portals[0].positive_incidence.value = 1;
	fixture->portals[0].clearance_q8 = 32;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	for (i = 0U; i < 3U; i++) {
		const uint32_t vertex_base = i * 4U;

		fixture->source_surface_vertices[vertex_base] =
			(sg_rune_q8_vec3_t){ { 64, 0, 0 } };
		fixture->source_surface_vertices[vertex_base + 1U] =
			(sg_rune_q8_vec3_t){ { 64, 64, 0 } };
		fixture->source_surface_vertices[vertex_base + 2U] =
			(sg_rune_q8_vec3_t){ { 64, 64, 64 } };
		fixture->source_surface_vertices[vertex_base + 3U] =
			(sg_rune_q8_vec3_t){ { 64, 0, 64 } };
		fixture->source_surfaces[i].plane = fixture->facets[0].plane;
		fixture->source_surfaces[i].vertices =
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

	fixture->movement_capabilities[0] = (sg_rune_movement_capability_t){
		{ 0U }, { 0U }, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		{ 0U, 0U }, { 0U, 1U } };
	fixture->movement_capabilities[1] = (sg_rune_movement_capability_t){
		{ 1U }, { SG_RUNE_COMPACT_INDEX_NONE },
		SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT, SG_RUNE_STANCE_VALID_ALL,
		SG_RUNE_STANCE_VALID_ALL, { 0U, 0U }, { 1U, 1U } };
	fixture->movement_states[0] = (sg_rune_compact_movement_state_t){
		SG_RUNE_STANCE_VALID_STANDING, { 0U, 0U, 0U },
		SG_RUNE_MOVEMENT_SUPPORT_NONE, SG_RUNE_MOVEMENT_WATER_DRY,
		SG_HOST_HOOK_IDLE, 0U, SG_RUNE_COMPACT_INDEX_NONE };
	fixture->movement_states[1] = (sg_rune_compact_movement_state_t){
		SG_RUNE_STANCE_VALID_CROUCHING, { 0U, 0U, 0U },
		SG_RUNE_MOVEMENT_SUPPORT_NONE, SG_RUNE_MOVEMENT_WATER_DRY,
		SG_HOST_HOOK_IN_FLIGHT, 0U, SG_RUNE_COMPACT_INDEX_NONE };
	fixture->movement_fibers[0] = (sg_rune_compact_movement_fiber_t){
		{ 0U }, SG_RUNE_MOVEMENT_FIBER_PMOVE,
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
			SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_SUPPORT |
			SG_RUNE_MOVEMENT_STATE_TIME,
		{ 0U }, { 0U }, { 0U, 3U }, { 0U, 0U },
		{ SG_RUNE_COMPACT_INDEX_NONE }, SG_RUNE_COMPACT_INDEX_NONE,
		{ SG_RUNE_COMPACT_INDEX_NONE }, { SG_RUNE_COMPACT_INDEX_NONE } };
	fixture->movement_fibers[1] = (sg_rune_compact_movement_fiber_t){
		{ 1U }, SG_RUNE_MOVEMENT_FIBER_HOOK,
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
			SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_HOOK |
			SG_RUNE_MOVEMENT_STATE_TIME,
		{ 0U }, { 1U }, { 3U, 3U }, { 0U, 1U },
		{ SG_RUNE_COMPACT_INDEX_NONE }, SG_RUNE_COMPACT_INDEX_NONE,
		{ SG_RUNE_COMPACT_INDEX_NONE }, { SG_RUNE_COMPACT_INDEX_NONE } };
	fixture->movement_hook_targets[0] =
		(sg_rune_compact_movement_hook_target_t){
			.fiber = { 1U },
			.target_kind = SG_HOST_HOOK_TARGET_WORLD,
			.provenance =
				SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE,
			.response = { SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP, 0U },
			.visibility_class = SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
			.source_stances = SG_RUNE_STANCE_VALID_ALL,
			.target_stances = SG_RUNE_STANCE_VALID_ALL,
			.reserved = { 0U, 0U },
			.functions = { { 6U, 3U }, { 9U, 3U }, { 12U, 3U },
				{ 15U, 3U }, { 18U, 3U }, { 21U, 3U } } };
	for (i = 0U; i < 8U; i++) {
		fixture->movement_fiber_function_refs[i * 3U].value = 0U;
		fixture->movement_fiber_function_refs[i * 3U + 1U].value = 1U;
		fixture->movement_fiber_function_refs[i * 3U + 2U].value = 4U;
	}
	init_weapon_fixture(fixture);
	for (i = 0; i < 8; ++i)
	{
		fixture->functions[i].definition = i;
		fixture->functions[i].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->constants[i].value.bits = float_bits((float)i + 1.0f);
	}
	fixture->functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z;
	fixture->functions[3].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->functions[4].output = SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->functions[5].output =
		SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	fixture->functions[6].output = SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
	fixture->functions[7].output =
		SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE;
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 8;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 8;

	fixture->mechanisms[0].source.entity_ordinal = 7;
	fixture->mechanisms[0].entry_cell.value = 0;
	fixture->mechanisms[0].exit_cell.value = 1;
	fixture->mechanisms[0].activation_landmark.value = 0;
	fixture->mechanisms[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanisms[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 48 } };
	fixture->mechanisms[0].controllers =
		(sg_rune_compact_mechanism_controller_span_t){ 0U, 1U };
	fixture->mechanisms[0].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 0, 2 };
	fixture->mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 1U };
	fixture->mechanisms[0].dwell_ms = 250;
	fixture->mechanisms[0].travel_ms = 1000;
	fixture->mechanisms[0].reset_ms = 2000;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	fixture->mechanisms[0].initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].activated_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->mechanisms[0].reset_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].recovery =
		SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET;
	fixture->mechanisms[0].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH;
	fixture->mechanisms[0].required_item = UINT32_MAX;
	fixture->mechanisms[0].transition_destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[0].transition_fanout_ordinal = UINT32_MAX;
	fixture->mechanisms[1] = fixture->mechanisms[0];
	fixture->mechanisms[1].source.entity_ordinal = 11;
	fixture->mechanisms[1].entry_cell.value = 0;
	fixture->mechanisms[1].exit_cell.value = 0;
	fixture->mechanisms[1].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 2, 0 };
	fixture->mechanisms[1].controllers =
		(sg_rune_compact_mechanism_controller_span_t){ 1U, 1U };
	fixture->mechanisms[1].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 1U, 0U };
	fixture->mechanisms[1].dwell_ms = 0;
	fixture->mechanisms[1].travel_ms = 0;
	fixture->mechanisms[1].reset_ms = 0;
	fixture->mechanisms[1].kind = SG_RUNE_COMPACT_MECHANISM_BUTTON;
	fixture->mechanisms[1].recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture->mechanisms[1].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_USE;
	fixture->mechanism_controllers[0].controller.entity_ordinal = 7U;
	fixture->mechanism_controllers[0].topology_edge = 0U;
	fixture->mechanism_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->mechanism_controllers[0].activation_cell.value = 0U;
	fixture->mechanism_controllers[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->mechanism_controllers[0].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanism_controllers[0].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->mechanism_controllers[1].controller.entity_ordinal = 11U;
	fixture->mechanism_controllers[1].topology_edge =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanism_controllers[1].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->mechanism_controllers[1].activation_cell.value = 0U;
	fixture->mechanism_controllers[1].activation_witness =
		(sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->mechanism_controllers[1].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanism_controllers[1].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->mechanism_edges[0].source.entity_ordinal = 7;
	fixture->mechanism_edges[0].destination.entity_ordinal = 11;
	fixture->mechanism_edges[0].kind = SG_RUNE_COMPACT_MECHANISM_EDGE_OWNER;
	fixture->mechanism_edges[1] = fixture->mechanism_edges[0];
	fixture->mechanism_edges[1].kind = SG_RUNE_COMPACT_MECHANISM_EDGE_ENEMY;
	fixture->transitions[0].mechanism.value = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 1U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 1000U;
	fixture->transitions[0].value.portal_state.portal.value = 0U;
	fixture->transitions[0].value.portal_state.mover_model = 0U;
	fixture->transitions[0].value.portal_state.dwell_ms = 250U;
	fixture->transitions[0].value.portal_state.travel_ms = 1000U;
	fixture->transitions[0].value.portal_state.recovery_ms = 2000U;
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;

	fixture->landmarks[0].source.entity_ordinal = 11;
	fixture->landmarks[0].cells =
		(sg_rune_compact_landmark_cell_span_t){ 0, 1 };
	fixture->landmarks[0].mechanism.value = 1;
	fixture->landmarks[0].origin = (sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_BUTTON;
	fixture->landmarks[1] = fixture->landmarks[0];
	fixture->landmarks[1].source.entity_ordinal = 19;
	fixture->landmarks[1].cells.first = 1;
	fixture->landmarks[1].mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->landmarks[1].origin = (sg_rune_q8_vec3_t){ { 96, 24, 8 } };
	fixture->landmarks[1].bounds.mins =
		(sg_rune_q8_vec3_t){ { 88, 16, 0 } };
	fixture->landmarks[1].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 104, 32, 16 } };
	fixture->landmarks[1].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[1].variant = 1;
	fixture->landmark_cells[0].value = 0;
	fixture->landmark_cells[1].value = 1;
	fixture->annotations[0].facet.value = 0;
	fixture->annotations[0].attributes = SG_RUNE_COMPACT_FACET_HOOKABLE;
	fixture->annotations[0].hookable_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->annotations[0].source_surface = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->annotations[0].source_frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	fixture->portal_mechanisms[0].portal.value = 0;
	fixture->portal_mechanisms[0].mechanism.value = 0;
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture->mechanism_authorities[0].source.entity_ordinal = 7U;
	fixture->mechanism_authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture->mechanism_authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	fixture->mechanism_authorities[0].activation_cell.value = 0U;
	fixture->mechanism_authorities[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->mechanism_authorities[0].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanism_authorities[0].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 48 } };
	fixture->mechanism_authorities[0].controllers =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanism_authorities[0].topology =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanism_authorities[0].transitions =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanism_authorities[0].delay_ms = 0U;
	fixture->mechanism_authorities[0].dwell_ms = 250U;
	fixture->mechanism_authorities[0].pause_ms = 0U;
	fixture->mechanism_authorities[0].travel_ms = 1000U;
	fixture->mechanism_authorities[0].required_item =
		SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanism_authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanism_authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanism_authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanism_authorities[0].recovery_ms = 2000U;
	fixture->mechanism_authority_controllers[0].mechanism = 0U;
	fixture->mechanism_authority_controllers[0].controller.entity_ordinal = 7U;
	fixture->mechanism_authority_controllers[0].topology_edge = 0U;
	fixture->mechanism_authority_controllers[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	fixture->mechanism_authority_controllers[0].required_item =
		SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanism_authority_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->mechanism_authority_controllers[0].activation_cell.value = 0U;
	fixture->mechanism_authority_controllers[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->mechanism_authority_controllers[0].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanism_authority_controllers[0].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 48 } };
	fixture->mechanism_authority_topology_edges[0].source.entity_ordinal = 7U;
	fixture->mechanism_authority_topology_edges[0].destination.entity_ordinal =
		11U;
	fixture->mechanism_authority_topology_edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->mechanism_authority_topology_edges[0].fanout_ordinal = 0U;
	fixture->mechanism_authority_transitions[0].mechanism = 0U;
	fixture->mechanism_authority_transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	fixture->mechanism_authority_transitions[0].entry_cell.value = 0U;
	fixture->mechanism_authority_transitions[0].exit_cell.value = 1U;
	fixture->mechanism_authority_transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanism_authority_transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanism_authority_transitions[0].elapsed_ms = 1000U;
	fixture->mechanism_authority_transitions[0].value.portal_state.portal.value =
		0U;
	fixture->mechanism_authority_transitions[0].value.portal_state.mover_model =
		0U;
	fixture->mechanism_authority_transitions[0].value.portal_state.dwell_ms =
		250U;
	fixture->mechanism_authority_transitions[0].value.portal_state.travel_ms =
		1000U;
	fixture->mechanism_authority_transitions[0].value.portal_state.recovery_ms =
		2000U;
	fixture->mechanism_authority_transitions[0].value.portal_state.source_blocked =
		1U;
	fixture->mechanism_authority_transitions[0].value.portal_state.destination_blocked =
		0U;

	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 2;
	fixture->static_data.mechanism_controllers = fixture->mechanism_controllers;
	fixture->static_data.mechanism_controller_count = 2;
	fixture->static_data.mechanism_edges = fixture->mechanism_edges;
	fixture->static_data.mechanism_edge_count = 2;
	fixture->static_data.transitions = fixture->transitions;
	fixture->static_data.transition_count = 1;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 2;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 2;
	fixture->static_data.facet_annotations = fixture->annotations;
	fixture->static_data.facet_annotation_count = 1;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->static_data.portal_mechanism_count = 1;
	model = &fixture->model;
	model->version = SG_RUNE_COMPACT_MODEL_VERSION;
	model->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model->identity.bsp_sha256[0] = UINT8_C(0x5a);
	model->identity.bsp_bytes = 1024;
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
	model->identity.source_counts =
		(sg_rune_compact_source_counts_t){ 2, 3, 4, 5, 2, 2, 32 };
	model->identity.standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 256 } };
	model->identity.crouching_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.crouching_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 128 } };
	model->identity.physics.gravity_bits = float_bits(100.0f);
	model->identity.physics.ground_acceleration_bits = float_bits(10.0f);
	model->identity.physics.air_acceleration_bits = float_bits(1.0f);
	model->identity.physics.water_acceleration_bits = float_bits(4.0f);
	model->identity.physics.hook_acceleration_bits = float_bits(1000.0f);
	model->identity.physics.external_acceleration_bits = float_bits(1200.0f);
	model->identity.physics.water_drag_bits = float_bits(0.5f);
	model->identity.physics.max_velocity_bits = float_bits(800.0f);
	model->identity.physics.frame_ms = 8;
	model->identity.physics.substep_ms = 1;
	model->cells = fixture->cells;
	model->cell_count = 2;
	model->facets = fixture->facets;
	model->facet_count = 2;
	model->incidences = fixture->incidences;
	model->incidence_count = 3;
	model->cell_incidences = fixture->cell_incidences;
	model->cell_incidence_count = 3;
	model->vertices = fixture->vertices;
	model->vertex_count = 4;
	model->portals = fixture->portals;
	model->portal_count = 1;
	model->source_surfaces = fixture->source_surfaces;
	model->source_surface_count = 3;
	model->source_surface_vertices = fixture->source_surface_vertices;
	model->source_surface_vertex_count = 12;
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
	model->movement_pmove_abi.version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	model->movement_pmove_abi.game_api_version = 1U;
	model->movement_pmove_abi.import_size = 1U;
	model->movement_pmove_abi.pmove_offset = 1U;
	model->movement_pmove_abi.pmove_size = (uint32_t)sizeof(pmove_t);
	model->movement_pmove_abi.state_size = (uint32_t)sizeof(pmove_state_t);
	model->movement_pmove_abi.command_size = (uint32_t)sizeof(usercmd_t);
	model->movement_pmove_abi.fraction_bits =
		SG_HOST_ENGINE_PMOVE_FRACTION_BITS;
	model->movement_pmove_abi.substep_ms =
		model->identity.physics.substep_ms;
	model->movement_pmove_abi.identity = model->identity.physics_abi_id;
	model->movement_pmove_behavior_fingerprint = UINT64_C(0x701);
	model->movement_host_level_generation = UINT64_C(0x702);
	model->movement_physics_abi_id = model->identity.physics_abi_id;
	model->movement_collision_law_id = model->identity.collision_law_id;
	model->movement_pmove_law_id = model->identity.pmove_law_id;
	model->movement_gravity_law_id = model->identity.gravity_law_id;
	model->movement_hook_law_id = model->identity.hook_law_id;
	model->movement_mechanism_law_id = model->identity.mechanism_law_id;
	init_response_fixture(fixture);
	model->weapon_profiles = fixture->weapon_profiles;
	model->weapon_profile_count = SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
	if (!SG_RuneCompactWeaponProfileCatalogId(model->weapon_profiles,
			model->weapon_profile_count,
			&model->identity.weapon_profile_catalog_id))
		abort();
	model->weapon_kernels = fixture->weapon_kernels;
	model->weapon_kernel_count = fixture->weapon_kernel_count;
	fixture->weapon_attachments[0] =
		(sg_rune_compact_weapon_field_attachment_t){
			.cell = { 0U },
			.source_surface = 0U,
			.relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT,
			.relations = { 0U, 1U },
			.relation_span = 0U
		};
	fixture->weapon_attachments[1] =
		(sg_rune_compact_weapon_field_attachment_t){
			.cell = { 0U },
			.source_surface = 0U,
			.relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_RAIL,
			.relations = { 1U, 1U },
			.relation_span = 1U
		};
	fixture->weapon_attachments[2] =
		(sg_rune_compact_weapon_field_attachment_t){
			.cell = { 0U },
			.source_surface = 0U,
			.relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT,
			.relations = { 2U, 1U },
			.relation_span = 2U
		};
	fixture->weapon_attachments[3] =
		(sg_rune_compact_weapon_field_attachment_t){
			.cell = { 1U },
			.source_surface = 0U,
			.relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT,
			.relations = { 3U, 1U },
			.relation_span = 3U
		};
	fixture->weapon_relation_spans[0].references =
		(sg_rune_compact_response_ref_span_t){ 0U, 1U };
	fixture->weapon_relation_spans[1].references =
		(sg_rune_compact_response_ref_span_t){ 1U, 1U };
	fixture->weapon_relation_spans[2].references =
		(sg_rune_compact_response_ref_span_t){ 2U, 1U };
	fixture->weapon_relation_spans[3].references =
		(sg_rune_compact_response_ref_span_t){ 3U, 1U };
	fixture->weapon_relation_refs[0] =
		(sg_rune_compact_response_ref_t){
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U };
	fixture->weapon_relation_refs[1] = fixture->weapon_relation_refs[0];
	fixture->weapon_relation_refs[2] = fixture->weapon_relation_refs[0];
	fixture->weapon_relation_refs[3] =
		(sg_rune_compact_response_ref_t){
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 1U };
	model->weapon_attachments = fixture->weapon_attachments;
	model->weapon_attachment_count = 4U;
	model->weapon_relation_spans = fixture->weapon_relation_spans;
	model->weapon_relation_span_count = 4U;
	model->weapon_relation_refs = fixture->weapon_relation_refs;
	model->weapon_relation_ref_count = 4U;
	model->weapon_function_refs = fixture->weapon_function_refs;
	model->weapon_function_ref_count = fixture->weapon_function_ref_count;
	model->mechanism_authorities = fixture->mechanism_authorities;
	model->mechanism_authority_count = 1U;
	model->mechanism_authority_controllers =
		fixture->mechanism_authority_controllers;
	model->mechanism_authority_controller_count = 1U;
	model->mechanism_authority_topology_edges =
		fixture->mechanism_authority_topology_edges;
	model->mechanism_authority_topology_edge_count = 1U;
	model->mechanism_authority_transitions =
		fixture->mechanism_authority_transitions;
	model->mechanism_authority_transition_count = 1U;
	model->mechanism_authority_transition_static_indices =
		fixture->mechanism_authority_transition_static_indices;
	model->static_transition_authority_indices =
		fixture->static_transition_authority_indices;
	model->analytic = &fixture->analytic;
	model->static_data = &fixture->static_data;
}

static int expect_error(const unsigned char *image, size_t size,
	sg_rune_compact_wire_error_code_t expected)
{
	sg_rune_compact_wire_error_t error;
	CHECK(!SG_RuneCompactWireInspect(image, size, NULL, &error));
	CHECK(error.code == expected);
	return 1;
}

static int expect_decode_wire_error(const unsigned char *image, size_t size,
	const sg_rune_compact_identity_t *identity,
	sg_rune_compact_wire_error_code_t expected)
{
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;

	CHECK(!SG_RuneCompactWireDecode(image, size, identity, &decoded,
		&error));
	CHECK(decoded == NULL);
	CHECK(error.code == expected);
	return 1;
}

static int test_round_trip(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	const sg_rune_compact_model_t *model;
	unsigned char *first;
	unsigned char *second;
	size_t size;
	size_t written;
	sg_rune_compact_error_t model_error;
	init_fixture(&fixture);
	if (!SG_RuneCompactModelValidateBound(&fixture.model,
		&fixture.model.identity, &model_error))
	{
		fprintf(stderr, "fixture model error: code=%d domain=%d record=%u\n",
			(int)model_error.code, (int)model_error.domain,
			model_error.record);
		return 0;
	}
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	CHECK(size > (size_t)(TEST_HEADER_FIXED +
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT * TEST_DESCRIPTOR_SIZE));
	first = malloc(size);
	second = malloc(size);
	CHECK(first != NULL && second != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, first, size, &written,
		&error));
	CHECK(written == size);
	CHECK(SG_RuneCompactWireInspect(first, size, &info, &error));
	CHECK(info.image_bytes == size);
	CHECK(info.wire_version == SG_RUNE_COMPACT_WIRE_VERSION);
	CHECK(info.model_version == SG_RUNE_COMPACT_MODEL_VERSION);
	CHECK(info.analytic_version == SG_RUNE_COMPACT_ANALYTIC_VERSION);
	CHECK(info.checksum == test_checksum(first, size));
	CHECK(memcmp(&info.identity, &fixture.model.identity,
		sizeof(info.identity)) == 0);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_CELLS] == 2);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_FACETS] == 2);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES] == 3);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES] == 12);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES] ==
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS] ==
		fixture.weapon_kernel_count);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS] ==
		fixture.weapon_function_ref_count);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES] == 2U);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES] == 2U);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS] == 2U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS] == 1U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS] == 24U);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS] == 4U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS] == 4U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS] == 4U);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS] == 2);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS] == 2);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES] == 2);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS] == 1);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES] ==
		1U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS] == 1U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES] == 1U);
	CHECK(info.counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS] == 1U);
	CHECK(SG_RuneCompactWireDecode(first, size, &fixture.model.identity,
		&decoded, &error));
	model = SG_RuneCompactWireModel(decoded);
	CHECK(model != NULL && model != &fixture.model);
	CHECK(model->analytic != fixture.model.analytic);
	CHECK(model->static_data != fixture.model.static_data);
	CHECK(model->identity.producer_identity ==
		fixture.model.identity.producer_identity);
	CHECK(model->facets[0].source.kind ==
		SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE);
	CHECK(model->facets[0].source.value.brush_side.plane == 0U);
	CHECK(model->facets[0].kind == SG_RUNE_COMPACT_FACET_POLYGON);
	CHECK(model->facets[1].kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY);
	CHECK(model->facets[1].vertices.count == 0);
	CHECK(model->facets[1].portal.value == SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(model->source_surface_count == 3);
	CHECK(model->source_surface_vertex_count == 12);
	CHECK(model->source_surfaces[1].parent_surface == 0U);
	CHECK(model->source_surfaces[2].frame ==
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL);
	CHECK(model->static_data->facet_annotations[0].source_surface ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(model->static_data->facet_annotations[0].source_frame ==
		SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD);
	CHECK(model->movement_capabilities[0].kind ==
		SG_RUNE_MOVEMENT_CAPABILITY_WALK);
	CHECK(model->movement_capabilities[1].kind ==
		SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT);
	CHECK(model->movement_fibers[1].kind == SG_RUNE_MOVEMENT_FIBER_HOOK);
	CHECK(model->movement_hook_targets[0].response.kind ==
		SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP);
	CHECK(model->movement_fiber_function_ref_count == 24U);
	CHECK(model->weapon_profiles[13].source_profile == 14);
	CHECK(model->weapon_kernels[model->weapon_kernel_count - 1U].profile == 13);
	CHECK(model->weapon_attachments[0].cell.value == 0U);
	CHECK(model->weapon_attachments[0].source_surface == 0U);
	CHECK(model->weapon_attachments[0].relation_class ==
		SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT);
	CHECK(model->weapon_attachments[0].relation_span == 0U);
	CHECK(model->weapon_attachments[1].relation_class ==
		SG_RUNE_COMPACT_WEAPON_RELATION_RAIL);
	CHECK(model->weapon_attachments[2].relation_class ==
		SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT);
	CHECK(model->weapon_attachments[3].cell.value == 1U);
	CHECK(model->weapon_relation_span_count == 4U);
	CHECK(model->weapon_relation_spans[0].references.first == 0U);
	CHECK(model->weapon_relation_spans[0].references.count == 1U);
	CHECK(model->weapon_relation_refs[0].kind ==
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
	CHECK(model->weapon_relation_refs[0].index == 0U);
	CHECK(model->weapon_relation_refs[1].index == 0U);
	CHECK(model->weapon_relation_refs[2].index == 0U);
	CHECK(model->weapon_relation_refs[3].index == 1U);
	CHECK(model->response.source_fragment_count == 2U);
	CHECK(model->response.fact_count == 2U);
	CHECK(model->response.facts[0].trace.brush ==
		SG_HOST_COLLISION_BRUSH_NONE);
	CHECK(model->response.facts[0].trace.brush_side ==
		SG_HOST_COLLISION_BRUSH_NONE);
	CHECK(model->response.facts[1].trace.brush == 1U);
	CHECK(model->response.facts[1].trace.brush_side == 1U);
	CHECK(model->response.splits[1].brush_side == 1U);
	CHECK(model->response.seal.source_surface_catalog_seal ==
		fixture.model.response.seal.source_surface_catalog_seal);
	CHECK(model->static_data->mechanisms[0].reset_ms == 2000);
	CHECK(model->static_data->mechanisms[0].required_item == UINT32_MAX);
	CHECK(model->static_data->mechanism_edges[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_EDGE_OWNER);
	CHECK(model->static_data->mechanism_edges[1].kind ==
		SG_RUNE_COMPACT_MECHANISM_EDGE_ENEMY);
	CHECK(model->mechanism_authorities[0].source.entity_ordinal == 7U);
	CHECK(model->mechanism_authority_controllers[0].topology_edge == 0U);
	CHECK(model->mechanism_authority_topology_edges[0].kind ==
		SG_MECH_EDGE_TARGET);
	CHECK(model->mechanism_authority_transitions[0].value.portal_state.portal.value ==
		0U);
	CHECK(model->mechanism_authority_transitions[0].value.portal_state.source_blocked ==
		1U);
	CHECK(model->mechanism_authority_transitions[0].value.portal_state.destination_blocked ==
		0U);
	CHECK(SG_RuneCompactModelValidateBound(model, &fixture.model.identity,
		&model_error));
	CHECK(SG_RuneCompactWireEncode(model, second, size, &written, &error));
	CHECK(written == size);
	CHECK(memcmp(first, second, size) == 0);
	SG_RuneCompactWireDestroy(decoded);
	free(second);
	free(first);
	return 1;
}

static int test_semantic_and_identity_rejection(void)
{
	test_fixture_t fixture;
	sg_rune_compact_identity_t expected;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	unsigned char *image;
	unsigned char *copy;
	size_t size;
	size_t written;
	uint64_t offset;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = malloc(size);
	copy = malloc(size);
	CHECK(image != NULL && copy != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written, &error));
	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS);
	put_u32(copy + offset + 96U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS);
	put_u32(copy + offset + 36U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS);
	put_u32(copy + offset + 16U,
		SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS);
	put_u32(copy + offset + 28U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS);
	put_u32(copy + offset + 32U, 3U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS) + 40U;
	put_u32(copy + offset + 36U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS) + 124U;
	put_u32(copy + offset + 120U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS) + 124U;
	put_u32(copy + offset + 116U, SG_HOST_COLLISION_BRUSH_NONE);
	put_u32(copy + offset + 120U, SG_HOST_COLLISION_BRUSH_NONE);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS) + 124U;
	put_u32(copy + offset + 84U, float_bits(7.0f));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS) + 124U;
	put_u32(copy + offset + 116U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS);
	put_u32(copy + offset + 4U, SG_HOST_HOOK_TARGET_OTHER);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS);
	put_u32(copy + offset + 4U, SG_HOST_HOOK_TARGET_FUNC);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS);
	put_u32(copy + offset + 4U, SG_HOST_HOOK_TARGET_PLAYER);
	put_u32(copy + offset + 8U,
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC);
	put_u32(copy + offset + 12U, SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT);
	put_u32(copy + offset + 16U, SG_RUNE_COMPACT_INDEX_NONE);
	put_u32(copy + offset + 20U,
		SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL);
	refresh_checksum(copy, size);
	CHECK(SG_RuneCompactWireInspect(copy, size, NULL, &error));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS);
	put_u32(copy + offset + 8U,
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS);
	copy[offset + 8U] = SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS);
	copy[offset + 32U] = SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
	put_u32(copy + offset,
		fixture.model.identity.source_counts.model_count);
	refresh_checksum(copy, size);
	CHECK(SG_RuneCompactWireInspect(copy, size, NULL, &error));
	CHECK(!SG_RuneCompactWireDecode(copy, size, &fixture.model.identity,
		&decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL);
	CHECK(error.model_error.code == SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE);
	CHECK(error.model_error.domain == SG_RUNE_COMPACT_RECORD_CELL);
	CHECK(error.model_error.record == 0);

	expected = fixture.model.identity;
	expected.gravity_law_id ^= 1;
	CHECK(!SG_RuneCompactWireDecode(image, size, &expected, &decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH);
	CHECK(error.model_error.code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH);

	free(copy);
	free(image);
	return 1;
}

static int test_finite_rotator_portal_wire(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	unsigned char *image;
	unsigned char *copy;
	size_t size;
	size_t written;
	uint64_t offset;

	init_fixture(&fixture);
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	fixture.mechanisms[0].flags =
		SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE |
		SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR;
	fixture.mechanism_authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = malloc(size);
	copy = malloc(size);
	CHECK(image != NULL && copy != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written,
		&error));
	CHECK(SG_RuneCompactWireInspect(image, size, NULL, &error));
	CHECK(SG_RuneCompactWireDecode(image, size, &fixture.model.identity,
		&decoded, &error));
	SG_RuneCompactWireDestroy(decoded);

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS);
	copy[offset + 148U] &= (unsigned char)~
		SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(expect_decode_wire_error(copy, size, &fixture.model.identity,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	copy[offset + 148U] |= UINT8_C(0x80);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	free(copy);
	free(image);
	return 1;
}

static int test_hostile_images(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	unsigned char *image;
	unsigned char *copy;
	size_t size;
	size_t written;
	uint64_t offset;
	init_fixture(&fixture);
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = malloc(size);
	copy = malloc(size);
	CHECK(image != NULL && copy != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written, &error));
	CHECK(expect_error(image, size - 1,
		SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED));

	memcpy(copy, image, size);
	copy[section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_VERTICES)] ^= 1;
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH));

	memcpy(copy, image, size);
	copy[8] = 1;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_UNSUPPORTED_VERSION));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS);
	put_u32(copy + offset + 8U,
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_TIME);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS);
	put_u32(copy + offset + 44U, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES);
	put_u32(copy + offset + 4,
		SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL |
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS);
	put_u32(copy + offset + 20, SG_RUNE_WEAPON_EVENT_LAW_KIND_COUNT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS);
	put_u32(copy + offset + 12, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	/* Attachments may share a compact relation span, but the serialized span
	 * must remain an exact, nonempty cover of the shared reference table. */
	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS);
	put_u32(copy + offset, 1U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(expect_decode_wire_error(copy, size, &fixture.model.identity,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS);
	put_u32(copy + offset + 24U, 1U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS);
	put_u32(copy + offset + 8U,
		SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	/* Raw inspection must reject an attachment whose certified fact belongs to
	 * another source cell; this cannot wait for decoded-model validation. */
	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS);
	put_u32(copy + offset, 1U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES);
	put_u32(copy + offset + 88, 1U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	/* Response patches are bound to the retained source-surface plane, not
	 * merely to an in-range surface index. */
	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES);
	put_u32(copy + offset + 64U, UINT32_C(0x3f800000));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	/* Keep the closed bounds coherent so this is specifically an off-plane
	 * target-vertex rejection, not a bounds-only failure. */
	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES);
	put_u32(copy + offset + 0U, 8U);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES);
	put_u32(copy + offset + 84U + 0U, 8U);
	put_u32(copy + offset + 96U + 0U, 8U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES);
	put_u32(copy + offset + 0U, 8U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS);
	put_u32(copy + offset + 12U,
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	put_u32(copy + TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_CELLS * TEST_DESCRIPTOR_SIZE + 8,
		SG_RUNE_COMPACT_MAX_CELLS + 1);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_FACETS * TEST_DESCRIPTOR_SIZE + 16,
		(uint32_t)(offset + 8));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SECTION));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + offset + 56, SG_RUNE_COMPACT_FACET_KIND_COUNT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + offset + 40, 2);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + offset + 56, SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + offset + 56, SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY);
	put_u32(copy + offset + 40, 0);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + offset + 56, SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY);
	put_u32(copy + offset + 40, 0);
	put_u32(copy + offset + 48, 1);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
	put_u32(copy + offset + 44, UINT32_MAX);
	put_u32(copy + offset + 48, 2);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES);
	put_u32(copy + offset, fixture.model.incidence_count);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
	copy[offset + 77] = 1;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS);
	copy[offset + 12] = 1;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS);
	copy[offset + 15] = 1;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES);
	put_u32(copy + offset + 20, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES);
	put_u32(copy + offset + 56 + 16,
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES);
	put_u32(copy + offset + 56 + 24, 1U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES);
	put_u32(copy + offset + 56 + 48, 5U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS);
	put_u32(copy + offset + 40, 1U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES);
	put_u32(copy + offset + 12,
		SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS);
	put_u32(copy + offset + 4, fixture.static_data.mechanism_edge_count);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS);
	put_u32(copy + offset + 12, fixture.model.cell_count);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS);
	put_u32(copy + offset + 28, 32U);
	put_u32(copy + offset + 40, 16U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS);
	copy[offset + 88] = 1U;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS);
	put_u32(copy + offset + 96, 0U);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS);
	put_u32(copy + offset + 4,
		SG_RUNE_WEAPON_EFFECT_CHANNEL_COUNT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES);
	put_u32(copy + offset + 6 * 16 + 4,
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS);
	put_u32(copy + offset + 7U * UINT32_C(24) + 4U,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	put_u32(copy + offset + 7U * UINT32_C(24) + 8U,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	free(copy);
	free(image);
	return 1;
}

static int test_api_errors(void)
{
	test_fixture_t fixture;
	sg_rune_compact_model_t oversized;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded =
		(sg_rune_compact_wire_decoded_t *)(uintptr_t)1;
	size_t size = 99;
	size_t written = 99;
	CHECK(SG_RuneCompactWireImageLimit() ==
		SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES);
	init_fixture(&fixture);
	oversized = fixture.model;
	oversized.movement_hook_target_count =
		SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS;
	oversized.movement_fiber_count = SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS;
	oversized.movement_fiber_function_ref_count =
		SG_RUNE_COMPACT_MAX_MOVEMENT_FIBER_FUNCTION_REFS;
	oversized.weapon_attachment_count =
		SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS;
	oversized.weapon_relation_ref_count =
		SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS;
	oversized.weapon_function_ref_count =
		SG_RUNE_COMPACT_MAX_WEAPON_FUNCTION_REFS;
	CHECK(!SG_RuneCompactWireMeasure(&oversized, &size, &error));
	CHECK(size == 0U);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
	CHECK(!SG_RuneCompactWireEncode(&oversized, NULL, 0U, &written, &error));
	CHECK(written == 0U);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
	if (SIZE_MAX > SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES) {
		const size_t over_limit =
			(size_t)SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES + 1U;

		CHECK(!SG_RuneCompactWireInspect(&fixture, over_limit, NULL, &error));
		CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
		CHECK(!SG_RuneCompactWireDecode(&fixture, over_limit,
			&fixture.model.identity, &decoded, &error));
		CHECK(decoded == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
	}
	CHECK(!SG_RuneCompactWireMeasure(NULL, &size, &error));
	CHECK(size == 0);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT);
	CHECK(!SG_RuneCompactWireEncode(&fixture.model, NULL, 0, &written, &error));
	CHECK(written == 0);
	CHECK(!SG_RuneCompactWireDecode(NULL, 0, &fixture.model.identity, NULL,
		&error));
	CHECK(!SG_RuneCompactWireDecode(NULL, 0, NULL, &decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT);
	CHECK(strcmp(SG_RuneCompactWireErrorString(
		SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH),
		"checksum mismatch") == 0);
	CHECK(strcmp(SG_RuneCompactWireErrorString(
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL),
		"invalid compact model") == 0);
	CHECK(strcmp(SG_RuneCompactWireErrorString(
		SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH),
		"compact model identity mismatch") == 0);
	return 1;
}

static int test_hook_lifecycle_wire_rejection(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	unsigned char *image;
	size_t size;
	size_t written;
	uint64_t offset;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = malloc(size);
	CHECK(image != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written,
		&error));
	offset = section_offset(image,
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES);
	put_u32(image + (size_t)offset + UINT32_C(24) + UINT32_C(12),
		(uint32_t)SG_HOST_HOOK_ATTACHED);
	refresh_checksum(image, size);
	CHECK(!SG_RuneCompactWireInspect(image, size, NULL, &error));
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE);
	CHECK(error.section == SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS);
	free(image);
	return 1;
}

static int test_model_validation_allocation_failure(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded =
		(sg_rune_compact_wire_decoded_t *)(uintptr_t)1;
	unsigned char *image;
	size_t size;
	size_t written;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = calloc(1U, size);
	CHECK(image != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written,
		&error));
	calloc_count = 0U;
	calloc_fail_after = 1U;
	CHECK(!SG_RuneCompactWireDecode(image, written, &fixture.model.identity,
		&decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
	CHECK(error.model_error.code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY);
	calloc_fail_after = SIZE_MAX;
	free(image);
	return 1;
}

static int emit_fixture(const char *path, int invalid_provenance)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	unsigned char *image = NULL;
	FILE *stream = NULL;
	size_t size;
	size_t written;
	int ok = 0;

	if (path == NULL)
		return 0;
	init_fixture(&fixture);
	if (!SG_RuneCompactWireMeasure(&fixture.model, &size, &error))
		goto done;
	image = malloc(size);
	if (image == NULL || !SG_RuneCompactWireEncode(&fixture.model, image,
		size, &written, &error) || written != size)
		goto done;
	if (invalid_provenance)
	{
		uint64_t offset = section_offset(image,
			SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
		put_u32(image + offset,
			fixture.model.identity.source_counts.model_count);
		refresh_checksum(image, size);
	}
	stream = fopen(path, "wb");
	if (stream == NULL || fwrite(image, 1U, size, stream) != size ||
		fclose(stream) != 0)
		goto done;
	stream = NULL;
	ok = 1;

done:
	if (stream != NULL)
		(void)fclose(stream);
	free(image);
	return ok;
}

static int verify_fixture(const char *path)
{
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	FILE *stream = NULL;
	unsigned char *image = NULL;
	long file_bytes;
	size_t image_bytes;
	int ok = 0;

	if (path == NULL)
		return 0;
	stream = fopen(path, "rb");
	if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0 ||
		(file_bytes = ftell(stream)) < 0 ||
		(uintmax_t)file_bytes > (uintmax_t)SIZE_MAX ||
		fseek(stream, 0L, SEEK_SET) != 0)
		goto done;
	image_bytes = (size_t)file_bytes;
	image = malloc(image_bytes == 0U ? 1U : image_bytes);
	if (image == NULL || fread(image, 1U, image_bytes, stream) != image_bytes ||
		fclose(stream) != 0) {
		stream = NULL;
		goto done;
	}
	stream = NULL;
	if (!SG_RuneCompactWireInspect(image, image_bytes, &info, &error) ||
		!SG_RuneCompactWireDecode(image, image_bytes, &info.identity, &decoded,
			&error))
		goto done;
	ok = 1;

done:
	SG_RuneCompactWireDestroy(decoded);
	if (stream != NULL)
		(void)fclose(stream);
	free(image);
	return ok;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--emit") == 0)
		return emit_fixture(argv[2], 0) ? 0 : 1;
	if (argc == 3 && strcmp(argv[1], "--emit-invalid-provenance") == 0)
		return emit_fixture(argv[2], 1) ? 0 : 1;
	if (argc == 3 && strcmp(argv[1], "--verify") == 0)
		return verify_fixture(argv[2]) ? 0 : 1;
	if (argc != 1)
		return 1;
#if defined(SG_RUNE_COMPACT_WIRE_TESTING)
	if (!test_teleport_state_wire_law() ||
		!test_controller_state_wire_law())
		return 1;
#endif
	if (!test_round_trip() || !test_hostile_images() ||
		!test_semantic_and_identity_rejection() ||
		!test_finite_rotator_portal_wire() ||
		!test_hook_lifecycle_wire_rejection() || !test_api_errors() ||
		!test_model_validation_allocation_failure())
		return 1;
	puts("sg_rune_compact_wire_test: ok");
	return 0;
}
