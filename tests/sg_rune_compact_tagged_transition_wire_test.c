#include "../slipgate/sg_rune_compact_source_surface_catalog.h"
#include "../slipgate/sg_rune_compact_mechanisms.h"
#include "../slipgate/sg_rune_compact_wire.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIRE_HEADER_FIXED UINT32_C(48)
#define WIRE_DESCRIPTOR_SIZE UINT32_C(24)
#define WIRE_CHECKSUM_OFFSET UINT32_C(24)
#define TRANSITION_WIRE_SIZE UINT32_C(248)
#define MECHANISM_WIRE_SIZE UINT32_C(152)
#define WEAPON_FUNCTION_REF_CAPACITY \
	(SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT * \
	 SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT * \
	 (SG_RUNE_WEAPON_STATIC_LAW_COUNT + \
	  SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT * 2U))

#define CHECK(expression) \
	do { \
		if (!(expression)) { \
			fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #expression); \
			return 0; \
		} \
	} while (0)

typedef struct tagged_transition_fixture_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_cell_t cells[1];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_incidence_t incidences[1];
	sg_rune_compact_incidence_index_t cell_incidences[1];
	sg_rune_compact_portal_t portals[1];
	sg_rune_compact_source_surface_t source_surfaces[1];
	sg_rune_q8_vec3_t source_surface_vertices[3];
	sg_rune_weapon_profile_t weapon_profiles[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT];
	sg_rune_weapon_response_kernel_t weapon_kernels[32];
	sg_rune_weapon_function_ref_t weapon_function_refs[
		WEAPON_FUNCTION_REF_CAPACITY];
	sg_rune_analytic_function_t functions[6];
	sg_rune_analytic_constant_t constants[6];
	uint32_t weapon_kernel_count;
	uint32_t weapon_function_ref_count;
	sg_rune_compact_mechanism_t mechanisms[4];
	sg_rune_compact_mechanism_edge_t mechanism_edges[2];
	sg_rune_compact_static_transition_t transitions[4];
	sg_rune_compact_mechanism_authority_t mechanism_authorities[4];
	sg_rune_compact_mechanism_transition_t authority_transitions[4];
	uint32_t authority_transition_static_indices[4];
	uint32_t static_transition_authority_indices[4];
	sg_rune_compact_landmark_t landmarks[1];
	sg_rune_compact_cell_index_t landmark_cells[1];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[1];
} tagged_transition_fixture_t;

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static uint32_t WeaponFunctionForOutput(
	sg_rune_analytic_output_meaning_t output)
{
	switch (output)
	{
	case SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS:
		return 0U;
	case SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z:
		return 1U;
	case SG_RUNE_ANALYTIC_OUTPUT_DAMAGE:
		return 2U;
	case SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS:
		return 3U;
	case SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS:
		return 4U;
	case SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE:
		return 5U;
	default:
		return SG_RUNE_COMPACT_INDEX_NONE;
	}
}

static void InitWeaponFixture(tagged_transition_fixture_t *fixture)
{
	uint32_t reference_cursor = 0U;
	uint32_t kernel_cursor = 0U;
	uint32_t profile_index;

	for (profile_index = 0U;
		profile_index < SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
		profile_index++)
	{
		const sg_rune_weapon_profile_t *profile =
			&fixture->weapon_profiles[profile_index];
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++)
		{
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
			kernel->profile = profile_index;
			kernel->family = (sg_rune_weapon_response_family_t)family;
			kernel->functions.first = reference_cursor;
			if (!SG_RuneCompactWeaponCanonicalEventLaw(profile->source_profile,
				kernel->family, &kernel->event_law) ||
				!SG_RuneCompactWeaponKernelReferenceCount(profile, kernel->family,
					&reference_count))
				abort();
			kernel->functions.count = reference_count;
			for (ordinal = 0U; ordinal < reference_count; ordinal++)
			{
				sg_rune_weapon_effect_channel_t channel;
				uint32_t instance;
				sg_rune_analytic_output_meaning_t output;

				if (!SG_RuneCompactWeaponFunctionRefExpected(profile,
					kernel->family, ordinal, &channel, &instance, &output) ||
					reference_cursor >= (uint32_t)(sizeof(
						fixture->weapon_function_refs) /
						sizeof(fixture->weapon_function_refs[0])))
					abort();
				fixture->weapon_function_refs[reference_cursor++] =
					(sg_rune_weapon_function_ref_t){
						{ WeaponFunctionForOutput(output) }, channel, instance };
			}
		}
	}
	fixture->weapon_kernel_count = kernel_cursor;
	fixture->weapon_function_ref_count = reference_cursor;
}

static void PutU32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8U);
	p[2] = (uint8_t)(value >> 16U);
	p[3] = (uint8_t)(value >> 24U);
}

static uint32_t GetU32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
		((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint64_t GetU64(const uint8_t *p)
{
	return (uint64_t)GetU32(p) | ((uint64_t)GetU32(p + 4U) << 32U);
}

static uint32_t Checksum(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		uint32_t value = data[index];
		uint32_t bit;

		if (index >= WIRE_CHECKSUM_OFFSET &&
			index < WIRE_CHECKSUM_OFFSET + sizeof(uint32_t))
			value = 0U;
		crc ^= value;
		for (bit = 0U; bit < 8U; bit++)
			crc = (crc >> 1U) ^
				(UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void RefreshChecksum(uint8_t *image, size_t size)
{
	PutU32(image + WIRE_CHECKSUM_OFFSET, Checksum(image, size));
}

static uint64_t SectionOffset(const uint8_t *image, uint32_t section)
{
	return GetU64(image + WIRE_HEADER_FIXED +
		(uint64_t)section * WIRE_DESCRIPTOR_SIZE + 16U);
}

static uint8_t AllZero(const uint8_t *p, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
		if (p[index] != 0U)
			return 0U;
	return 1U;
}

static uint8_t CanonicalFiniteBits(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static void InitFixture(tagged_transition_fixture_t *fixture)
{
	uint32_t index;
	uint32_t row;
	uint32_t axis;

	memset(fixture, 0, sizeof(*fixture));
	fixture->model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	fixture->model.identity.source_counts.model_count = 2U;
	fixture->model.identity.source_counts.leaf_count = 1U;
	fixture->model.identity.source_counts.area_count = 1U;
	fixture->model.identity.source_counts.plane_count = 1U;
	fixture->model.identity.source_counts.brush_count = 1U;
	fixture->model.identity.source_counts.brush_side_count = 1U;
	fixture->model.identity.source_counts.entity_count = 8U;
	fixture->model.identity.standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -16, -16, -24 } };
	fixture->model.identity.standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 16, 16, 32 } };
	fixture->model.identity.crouching_hull =
		fixture->model.identity.standing_hull;
	fixture->model.identity.physics.gravity_bits = FloatBits(800.0f);
	fixture->model.identity.physics.frame_ms = 8U;
	fixture->model.identity.physics.substep_ms = 1U;

	fixture->cells[0].bounds = (sg_rune_q8_bounds_t){
		{ { 0, 0, 0 } }, { { 64, 64, 64 } }
	};
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 1U };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cell_incidences[0].value = 0U;

	fixture->facets[0].source.kind = SG_RUNE_COMPACT_SOURCE_DOMAIN;
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 0U };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 1U };
	fixture->facets[0].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->facets[0].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].facet.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;

	fixture->portals[0].source.kind = SG_RUNE_COMPACT_SOURCE_DOMAIN;
	fixture->portals[0].facet.value = 0U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 0U;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;

	fixture->source_surfaces[0].source =
		(sg_rune_compact_brush_side_source_t){ 1U, 0U, 0U, 0U };
	fixture->source_surfaces[0].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	fixture->source_surfaces[0].cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].split_ordinal = 0U;
	fixture->source_surfaces[0].plane.normal_bits[0] = FloatBits(0.0f);
	fixture->source_surfaces[0].plane.normal_bits[1] = FloatBits(0.0f);
	fixture->source_surfaces[0].plane.normal_bits[2] = FloatBits(1.0f);
	fixture->source_surfaces[0].plane.distance_bits = FloatBits(0.0f);
	fixture->source_surfaces[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 3U };
	fixture->source_surface_vertices[0] =
		(sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	fixture->source_surface_vertices[1] =
		(sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->source_surface_vertices[2] =
		(sg_rune_q8_vec3_t){ { 0, 64, 0 } };

	for (index = 0U; index < SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
		index++)
	{
		sg_rune_weapon_profile_t *profile = &fixture->weapon_profiles[index];

		profile->source_profile = index + 1U;
		profile->response_families =
			SG_RuneCompactWeaponCanonicalProfileMask(profile->source_profile);
		profile->projectile_count_min = 1U;
		profile->projectile_count_max = 1U;
		profile->direct_response_count = 1U;
	}
	if (!SG_RuneCompactWeaponProfileCatalogId(fixture->weapon_profiles,
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT,
		&fixture->model.identity.weapon_profile_catalog_id))
		abort();
	InitWeaponFixture(fixture);
	for (index = 0U; index < 6U; index++)
	{
		fixture->functions[index].definition = index;
		fixture->functions[index].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->constants[index].value.bits = FloatBits((float)index + 1.0f);
	}
	fixture->functions[0].output =
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->functions[3].output =
		SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	fixture->functions[4].output = SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
	fixture->functions[5].output =
		SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE;

	for (index = 0U; index < 4U; index++)
	{
		sg_rune_compact_mechanism_t *mechanism = &fixture->mechanisms[index];

		mechanism->source.entity_ordinal = index;
		mechanism->entry_cell.value = 0U;
		mechanism->exit_cell.value = 0U;
		mechanism->activation_landmark.value =
			SG_RUNE_COMPACT_INDEX_NONE;
		mechanism->transitions =
			(sg_rune_compact_mechanism_transition_span_t){ index, 1U };
		mechanism->activation_mask =
			SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO;
		mechanism->required_item = UINT32_MAX;
		mechanism->transition_destination.entity_ordinal =
			SG_RUNE_COMPACT_INDEX_NONE;
		mechanism->transition_fanout_ordinal = UINT32_MAX;
		mechanism->kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
		mechanism->initial_state =
			SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
		mechanism->activated_state =
			SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
		mechanism->reset_state =
			SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	}
	fixture->mechanisms[0].delay_ms = 11U;
	fixture->mechanisms[0].dwell_ms = 22U;
	fixture->mechanisms[0].wait_ms = 33U;
	fixture->mechanisms[0].travel_ms = 44U;
	fixture->mechanisms[0].reset_ms = 55U;
	fixture->mechanisms[0].transition_destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[0].transition_fanout_ordinal = UINT32_MAX;
	fixture->mechanisms[1].kind = SG_RUNE_COMPACT_MECHANISM_TELEPORT;
	fixture->mechanisms[1].transition_destination.entity_ordinal = 4U;
	fixture->mechanisms[1].transition_fanout_ordinal = 0U;
	fixture->mechanisms[1].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 0U, 1U };
	fixture->mechanisms[2].kind = SG_RUNE_COMPACT_MECHANISM_PUSH;
	fixture->mechanisms[2].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 1U, 0U };
	fixture->mechanisms[2].gravity_bits = FloatBits(100.0f);
	fixture->mechanisms[2].flight_ms = 750U;
	fixture->mechanisms[2].launch_velocity_bits[0] = FloatBits(256.0f);
	fixture->mechanisms[2].launch_velocity_bits[1] = FloatBits(-128.0f);
	fixture->mechanisms[2].launch_velocity_bits[2] = FloatBits(64.0f);
	fixture->mechanisms[3].kind = SG_RUNE_COMPACT_MECHANISM_TRAIN;
	fixture->mechanisms[3].transition_destination.entity_ordinal = 5U;
	fixture->mechanisms[3].transition_fanout_ordinal = 0U;
	fixture->mechanisms[3].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 1U, 1U };

	memset(fixture->transitions, 0, sizeof(fixture->transitions));
	fixture->transitions[0].mechanism.value = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 0U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 44U;
	fixture->transitions[0].value.portal_state.portal.value = 0U;
	fixture->transitions[0].value.portal_state.mover_model = 0U;
	fixture->transitions[0].value.portal_state.delay_ms = 11U;
	fixture->transitions[0].value.portal_state.dwell_ms = 22U;
	fixture->transitions[0].value.portal_state.pause_ms = 33U;
	fixture->transitions[0].value.portal_state.travel_ms = 44U;
	fixture->transitions[0].value.portal_state.recovery_ms = 55U;
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;

	fixture->transitions[1].mechanism.value = 1U;
	fixture->transitions[1].kind =
		SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT;
	fixture->transitions[1].entry_cell.value = 0U;
	fixture->transitions[1].exit_cell.value = 0U;
	fixture->transitions[1].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[1].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[1].value.teleport.destination.entity_ordinal = 4U;
	fixture->transitions[1].value.teleport.fanout_ordinal = 0U;

	fixture->transitions[2].mechanism.value = 2U;
	fixture->transitions[2].kind =
		SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH;
	fixture->transitions[2].entry_cell.value = 0U;
	fixture->transitions[2].exit_cell.value = 0U;
	fixture->transitions[2].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[2].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[2].elapsed_ms = 750U;
	fixture->transitions[2].value.push.launch_velocity_bits[0] =
		FloatBits(256.0f);
	fixture->transitions[2].value.push.launch_velocity_bits[1] =
		FloatBits(-128.0f);
	fixture->transitions[2].value.push.launch_velocity_bits[2] =
		FloatBits(64.0f);
	fixture->transitions[2].value.push.gravity_bits = FloatBits(100.0f);
	fixture->transitions[2].value.push.flight_ms = 750U;

	fixture->transitions[3].mechanism.value = 3U;
	fixture->transitions[3].kind =
		SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT;
	fixture->transitions[3].entry_cell.value = 0U;
	fixture->transitions[3].exit_cell.value = 0U;
	fixture->transitions[3].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[3].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[3].elapsed_ms = 5000U;
	fixture->transitions[3].value.transport.mover_model = 1U;
	fixture->transitions[3].value.transport.source_surface_ordinal = 0U;
	fixture->transitions[3].value.transport.source_endpoint.entity_ordinal = 3U;
	fixture->transitions[3].value.transport.destination_endpoint.entity_ordinal = 5U;
	fixture->transitions[3].value.transport.fanout_ordinal = 0U;
	fixture->transitions[3].value.transport.swept_static_clear = 1U;
	fixture->transitions[3].value.transport.start_supported = 1U;
	fixture->transitions[3].value.transport.end_supported = 1U;
	fixture->transitions[3].value.transport.source_player_local =
		(sg_rune_q8_vec3_t){ { 16, 16, 24 } };
	fixture->transitions[3].value.transport.destination_player_local =
		(sg_rune_q8_vec3_t){ { 16, 16, 24 } };
	fixture->transitions[3].value.transport.source_support_local =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->transitions[3].value.transport.destination_support_local =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->transitions[3].value.transport.source_player_world_bits[0] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.source_player_world_bits[1] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.source_player_world_bits[2] =
		FloatBits(3.0f);
	fixture->transitions[3].value.transport.destination_player_world_bits[0] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.destination_player_world_bits[1] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.destination_player_world_bits[2] =
		FloatBits(3.0f);
	fixture->transitions[3].value.transport.source_support_world_bits[0] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.source_support_world_bits[1] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.source_support_world_bits[2] =
		FloatBits(0.0f);
	fixture->transitions[3].value.transport.destination_support_world_bits[0] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.destination_support_world_bits[1] =
		FloatBits(2.0f);
	fixture->transitions[3].value.transport.destination_support_world_bits[2] =
		FloatBits(0.0f);
	for (axis = 0U; axis < 3U; axis++)
	{
		fixture->transitions[3].value.transport.source_mover_origin_bits[axis] =
			FloatBits(0.0f);
		fixture->transitions[3].value.transport.destination_mover_origin_bits[axis] =
			FloatBits(0.0f);
		for (row = 0U; row < 3U; row++)
		{
			uint32_t value = row == axis ? FloatBits(1.0f) : FloatBits(0.0f);

			fixture->transitions[3].value.transport.source_mover_axis_bits[row][axis] =
				value;
			fixture->transitions[3].value.transport.destination_mover_axis_bits[row][axis] =
				value;
		}
	}

	fixture->mechanism_edges[0] = (sg_rune_compact_mechanism_edge_t){
		{ 1U }, { 4U }, 0U, SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET };
	fixture->mechanism_edges[1] = (sg_rune_compact_mechanism_edge_t){
		{ 3U }, { 5U }, 0U, SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET };
	fixture->landmark_cells[0].value = 0U;
	fixture->landmarks[0].source.entity_ordinal = 6U;
	fixture->landmarks[0].cells =
		(sg_rune_compact_landmark_cell_span_t){ 0U, 1U };
	fixture->landmarks[0].mechanism.value = 3U;
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
	fixture->portal_mechanisms[0].portal.value = 0U;
	fixture->portal_mechanisms[0].mechanism.value = 0U;
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	for (index = 0U; index < 4U; index++) {
		sg_rune_compact_mechanism_authority_t *authority =
			&fixture->mechanism_authorities[index];

		authority->source.entity_ordinal =
			fixture->mechanisms[index].source.entity_ordinal;
		authority->kind = (sg_rune_compact_mechanism_authority_kind_t)
			fixture->mechanisms[index].kind;
		authority->activation =
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
		authority->activation_cell.value = 0U;
		authority->activation_bounds = fixture->cells[0].bounds;
		authority->transitions =
			(sg_rune_compact_mechanism_span_t){ index, 1U };
		authority->initial_state =
			(sg_rune_compact_mechanism_authority_state_t)
				fixture->mechanisms[index].initial_state;
		authority->activated_state =
			(sg_rune_compact_mechanism_authority_state_t)
				fixture->mechanisms[index].activated_state;
		authority->reset_state =
			(sg_rune_compact_mechanism_authority_state_t)
				fixture->mechanisms[index].reset_state;
		memcpy(&fixture->authority_transitions[index],
			&fixture->transitions[index], sizeof(fixture->transitions[index]));
		fixture->authority_transitions[index].mechanism = index;
		fixture->authority_transition_static_indices[index] = index;
		fixture->static_transition_authority_indices[index] = index;
	}
	fixture->mechanism_authorities[0].delay_ms = 11U;
	fixture->mechanism_authorities[0].dwell_ms = 22U;
	fixture->mechanism_authorities[0].pause_ms = 33U;
	fixture->mechanism_authorities[0].travel_ms = 44U;
	fixture->mechanism_authorities[0].recovery_ms = 55U;

	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 6U;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 6U;
	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 4U;
	fixture->static_data.mechanism_edges = fixture->mechanism_edges;
	fixture->static_data.mechanism_edge_count = 2U;
	fixture->static_data.transitions = fixture->transitions;
	fixture->static_data.transition_count = 4U;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 1U;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 1U;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->static_data.portal_mechanism_count = 1U;

	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 1U;
	fixture->model.facets = fixture->facets;
	fixture->model.facet_count = 1U;
	fixture->model.incidences = fixture->incidences;
	fixture->model.incidence_count = 1U;
	fixture->model.cell_incidences = fixture->cell_incidences;
	fixture->model.cell_incidence_count = 1U;
	fixture->model.portals = fixture->portals;
	fixture->model.portal_count = 1U;
	fixture->model.source_surfaces = fixture->source_surfaces;
	fixture->model.source_surface_count = 1U;
	fixture->model.source_surface_vertices = fixture->source_surface_vertices;
	fixture->model.source_surface_vertex_count = 3U;
	fixture->model.weapon_profiles = fixture->weapon_profiles;
	fixture->model.weapon_profile_count =
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
	fixture->model.weapon_kernels = fixture->weapon_kernels;
	fixture->model.weapon_kernel_count = fixture->weapon_kernel_count;
	fixture->model.weapon_function_refs = fixture->weapon_function_refs;
	fixture->model.weapon_function_ref_count =
		fixture->weapon_function_ref_count;
	fixture->model.mechanism_authorities = fixture->mechanism_authorities;
	fixture->model.mechanism_authority_count = 4U;
	fixture->model.mechanism_authority_transitions =
		fixture->authority_transitions;
	fixture->model.mechanism_authority_transition_count = 4U;
	fixture->model.mechanism_authority_transition_static_indices =
		fixture->authority_transition_static_indices;
	fixture->model.static_transition_authority_indices =
		fixture->static_transition_authority_indices;
	fixture->model.analytic = &fixture->analytic;
	fixture->model.static_data = &fixture->static_data;
	fixture->model.response.seal.version =
		SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION;
	fixture->model.response.seal.flags = SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED;
	fixture->model.response.seal.compact_facet_count = 1U;
	fixture->model.response.seal.compact_cell_count = 1U;
	fixture->model.response.seal.compact_source_surface_count = 1U;
	fixture->model.response.seal.compact_source_surface_vertex_count = 3U;
	fixture->model.response.seal.source_surface_catalog_seal =
		SG_RuneCompactSourceSurfaceCatalogSeal(fixture->source_surfaces, 1U,
			fixture->source_surface_vertices, 3U);
	fixture->model.response.exact_live_prefire_trace_required = 1U;
}

static int EncodeFixture(tagged_transition_fixture_t *fixture,
	uint8_t **image_out, size_t *size_out)
{
	sg_rune_compact_wire_error_t error;
	size_t size;
	size_t written;
	uint8_t *image;
	uint32_t transition;

	for (transition = 0U; transition < 4U; transition++) {
		memcpy(&fixture->authority_transitions[transition],
			&fixture->transitions[transition],
			sizeof(fixture->transitions[transition]));
		fixture->authority_transitions[transition].mechanism = transition;
	}

	if (!SG_RuneCompactWireMeasure(&fixture->model, &size, &error))
	{
		fprintf(stderr, "measure failed: code=%d section=%d record=%u\n",
			(int)error.code, (int)error.section, error.record);
		return 0;
	}
	image = malloc(size);
	if (image == NULL)
		return 0;
	if (!SG_RuneCompactWireEncode(&fixture->model, image, size, &written,
		&error) || written != size)
	{
		fprintf(stderr, "encode failed: code=%d section=%d record=%u\n",
			(int)error.code, (int)error.section, error.record);
		free(image);
		return 0;
	}
	*image_out = image;
	*size_out = size;
	return 1;
}

static int ExpectByteMutation(tagged_transition_fixture_t *fixture,
	uint32_t section, size_t offset, uint8_t value,
	sg_rune_compact_wire_error_code_t expected);

static int ExpectTailBytesRejected(tagged_transition_fixture_t *fixture,
	uint32_t transition_index, size_t tail_start)
{
	sg_rune_compact_wire_error_t error;
	uint8_t *image;
	size_t size;
	uint64_t section_offset;
	size_t tail_offset;

	CHECK(EncodeFixture(fixture, &image, &size));
	section_offset = SectionOffset(image,
		SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS);
	for (tail_offset = tail_start;
		tail_offset < (size_t)TRANSITION_WIRE_SIZE; tail_offset++)
	{
		size_t offset = (size_t)section_offset +
			(size_t)transition_index * (size_t)TRANSITION_WIRE_SIZE +
			tail_offset;

		image[offset] = 1U;
		RefreshChecksum(image, size);
		CHECK(!SG_RuneCompactWireInspect(image, size, NULL, &error));
		CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED);
		image[offset] = 0U;
		RefreshChecksum(image, size);
	}
	free(image);
	return 1;
}

static int ExpectByteMutation(tagged_transition_fixture_t *fixture,
	uint32_t section, size_t offset, uint8_t value,
	sg_rune_compact_wire_error_code_t expected)
{
	sg_rune_compact_wire_error_t error;
	uint8_t *image;
	size_t size;

	CHECK(EncodeFixture(fixture, &image, &size));
	image[(size_t)SectionOffset(image, section) + offset] = value;
	RefreshChecksum(image, size);
	CHECK(!SG_RuneCompactWireInspect(image, size, NULL, &error));
	CHECK(error.code == expected);
	free(image);
	return 1;
}

static int TestActivePayloadRoundTrips(void)
{
	tagged_transition_fixture_t fixture;
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_error_t error;
	uint8_t *image;
	size_t size;
	uint64_t offset;
	uint32_t index;

	InitFixture(&fixture);
	CHECK(EncodeFixture(&fixture, &image, &size));
	CHECK(SG_RuneCompactWireInspect(image, size, &info, &error));
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS] == 4U);
	offset = SectionOffset(image,
		SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS);
	CHECK(GetU32(image + WIRE_HEADER_FIXED +
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS *
		WIRE_DESCRIPTOR_SIZE + 4U) == TRANSITION_WIRE_SIZE);
	for (index = 0U; index < 4U; index++)
		CHECK(GetU32(image + offset + (uint64_t)index * TRANSITION_WIRE_SIZE + 4U) ==
			(uint32_t)fixture.transitions[index].kind);

	{
		const uint8_t *portal = image + offset;
		const uint8_t *teleport = portal + TRANSITION_WIRE_SIZE;
		const uint8_t *push = teleport + TRANSITION_WIRE_SIZE;
		const uint8_t *transport = push + TRANSITION_WIRE_SIZE;

		CHECK(GetU32(portal + 32U) == 0U);
		CHECK(GetU32(portal + 40U) == 11U);
		CHECK(GetU32(portal + 44U) == 22U);
		CHECK(GetU32(portal + 48U) == 33U);
		CHECK(GetU32(portal + 52U) == 44U);
		CHECK(GetU32(portal + 56U) == 55U);
		CHECK(portal[60U] == 1U && portal[61U] == 0U &&
			portal[62U] == 0U && portal[63U] == 0U);
		CHECK(GetU32(teleport + 32U) == 4U);
		CHECK(GetU32(teleport + 36U) == 0U);
		CHECK(GetU32(push + 68U) == FloatBits(256.0f));
		CHECK(GetU32(push + 72U) == FloatBits(-128.0f));
		CHECK(GetU32(push + 76U) == FloatBits(64.0f));
		CHECK(GetU32(push + 80U) == FloatBits(100.0f));
		CHECK(GetU32(push + 84U) == 750U);
		CHECK(GetU32(transport + 32U) == 1U);
		CHECK(GetU32(transport + 36U) == 0U);
		CHECK(GetU32(transport + 88U) == FloatBits(2.0f));
		CHECK(GetU32(transport + 100U) == FloatBits(2.0f));
		CHECK(GetU32(transport + 112U) == FloatBits(2.0f));
		CHECK(GetU32(transport + 124U) == FloatBits(2.0f));
		CHECK(GetU32(transport + 136U) == FloatBits(0.0f));
		CHECK(GetU32(transport + 148U) == FloatBits(1.0f));
		CHECK(GetU32(transport + 184U) == FloatBits(0.0f));
		CHECK(GetU32(transport + 196U) == FloatBits(1.0f));
		CHECK(GetU32(transport + 232U) == 3U);
		CHECK(GetU32(transport + 236U) == 5U);
		CHECK(GetU32(transport + 240U) == 0U);
		CHECK(transport[244U] == 1U && transport[245U] == 1U &&
			transport[246U] == 1U && transport[247U] == 0U);
		for (index = 0U; index < 36U; index++)
			CHECK(CanonicalFiniteBits(GetU32(transport + 88U +
				(size_t)index * 4U)));
		CHECK(AllZero(portal + 64U, 184U));
		CHECK(AllZero(teleport + 88U, 160U));
		CHECK(AllZero(push + 88U, 160U));
	}
	free(image);
	return 1;
}

static int TestInactivePayloadRejection(void)
{
	tagged_transition_fixture_t fixture;

	InitFixture(&fixture);
	CHECK(ExpectTailBytesRejected(&fixture, 0U, 64U));
	CHECK(ExpectTailBytesRejected(&fixture, 1U, 88U));
	CHECK(ExpectTailBytesRejected(&fixture, 2U, 88U));
	CHECK(ExpectByteMutation(&fixture,
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS,
		3U * (size_t)TRANSITION_WIRE_SIZE + 247U,
		(uint8_t)SG_RUNE_STANCE_COUNT,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	return 1;
}

static int TestPortalPayloadValidation(void)
{
	tagged_transition_fixture_t fixture;
	const uint32_t transitions =
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS;

	InitFixture(&fixture);
	CHECK(ExpectByteMutation(&fixture, transitions, 60U, 2U,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectByteMutation(&fixture, transitions, 61U, 1U,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectByteMutation(&fixture, transitions, 61U, 2U,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectByteMutation(&fixture, transitions, 62U, 1U,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));
	CHECK(ExpectByteMutation(&fixture, transitions, 63U, 1U,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));
	return 1;
}

static int ExpectU32Mutation(tagged_transition_fixture_t *fixture,
	uint32_t section, size_t offset, uint32_t value,
	sg_rune_compact_wire_error_code_t expected)
{
	sg_rune_compact_wire_error_t error;
	uint8_t *image;
	uint8_t *copy;
	size_t size;

	CHECK(EncodeFixture(fixture, &image, &size));
	copy = malloc(size);
	if (copy == NULL) {
		free(image);
		return 0;
	}
	memcpy(copy, image, size);
	free(image);
	PutU32(copy + SectionOffset(copy, section) + offset, value);
	RefreshChecksum(copy, size);
	if (SG_RuneCompactWireInspect(copy, size, NULL, &error) ||
		error.code != expected) {
		free(copy);
		return 0;
	}
	free(copy);
	return 1;
}

static int ExpectTwoU32Mutations(tagged_transition_fixture_t *fixture,
	uint32_t section, size_t first_offset, uint32_t first_value,
	size_t second_offset, uint32_t second_value,
	sg_rune_compact_wire_error_code_t expected)
{
	sg_rune_compact_wire_error_t error;
	uint8_t *image;
	uint8_t *copy;
	size_t size;
	uint64_t section_offset;

	CHECK(EncodeFixture(fixture, &image, &size));
	copy = malloc(size);
	if (copy == NULL) {
		free(image);
		return 0;
	}
	memcpy(copy, image, size);
	free(image);
	section_offset = SectionOffset(copy, section);
	PutU32(copy + section_offset + first_offset, first_value);
	PutU32(copy + section_offset + second_offset, second_value);
	RefreshChecksum(copy, size);
	if (SG_RuneCompactWireInspect(copy, size, NULL, &error) ||
		error.code != expected) {
		free(copy);
		return 0;
	}
	free(copy);
	return 1;
}

static int TestLargeOriginTransportRoundTrip(void)
{
	tagged_transition_fixture_t fixture;
	uint8_t *image;
	size_t size;
	uint64_t offset;
	const uint8_t *transport;
	uint32_t index;
	uint32_t row;
	uint32_t axis;
	sg_rune_compact_static_transport_t *value;

	InitFixture(&fixture);
	value = &fixture.transitions[3].value.transport;
	value->source_player_local = (sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	value->destination_player_local =
		(sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	value->source_support_local = (sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	value->destination_support_local =
		(sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	value->source_mover_origin_bits[0] = FloatBits(16777216.0f);
	value->destination_mover_origin_bits[0] = FloatBits(16777218.0f);
	value->source_mover_origin_bits[1] = FloatBits(0.0f);
	value->source_mover_origin_bits[2] = FloatBits(0.0f);
	value->destination_mover_origin_bits[1] = FloatBits(0.0f);
	value->destination_mover_origin_bits[2] = FloatBits(0.0f);
	for (axis = 0U; axis < 3U; axis++)
	{
		value->source_player_world_bits[axis] =
			value->source_mover_origin_bits[axis];
		value->destination_player_world_bits[axis] =
			value->destination_mover_origin_bits[axis];
		value->source_support_world_bits[axis] =
			value->source_mover_origin_bits[axis];
		value->destination_support_world_bits[axis] =
			value->destination_mover_origin_bits[axis];
		for (row = 0U; row < 3U; row++)
		{
			uint32_t bits = row == axis ? FloatBits(1.0f) : FloatBits(0.0f);

			value->source_mover_axis_bits[row][axis] = bits;
			value->destination_mover_axis_bits[row][axis] = bits;
		}
	}
	CHECK(FloatBits(16777218.0f) ==
		FloatBits(16777216.0f) + UINT32_C(1));
	CHECK(EncodeFixture(&fixture, &image, &size));
	CHECK(SG_RuneCompactWireInspect(image, size, NULL, NULL));
	offset = SectionOffset(image,
		SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS);
	transport = image + offset + 3U * (uint64_t)TRANSITION_WIRE_SIZE;
	CHECK(GetU32(transport + 136U) == FloatBits(16777216.0f));
	CHECK(GetU32(transport + 184U) == FloatBits(16777218.0f));
	CHECK(GetU32(transport + 148U) == FloatBits(1.0f));
	CHECK(GetU32(transport + 196U) == FloatBits(1.0f));
	for (index = 0U; index < 36U; index++)
		CHECK(CanonicalFiniteBits(GetU32(transport + 88U +
			(size_t)index * 4U)));
	free(image);
	return 1;
}

static int TestOwnerJoinRejection(void)
{
	tagged_transition_fixture_t fixture;
	const uint32_t transitions =
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS;
	const uint32_t mechanisms =
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS;
	const uint32_t authorities =
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES;
	const uint32_t authority_to_static = (uint32_t)
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES;
	const uint32_t static_to_authority = (uint32_t)
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES;
	const size_t transition0 = 0U * TRANSITION_WIRE_SIZE;
	const size_t transition1 = 1U * TRANSITION_WIRE_SIZE;
	const size_t transition2 = 2U * TRANSITION_WIRE_SIZE;
	const size_t transition3 = 3U * TRANSITION_WIRE_SIZE;
	const size_t mechanism0 = 0U * MECHANISM_WIRE_SIZE;
	const size_t mechanism1 = 1U * MECHANISM_WIRE_SIZE;
	const size_t mechanism2 = 2U * MECHANISM_WIRE_SIZE;
	const size_t mechanism3 = 3U * MECHANISM_WIRE_SIZE;

	InitFixture(&fixture);
	CHECK(ExpectU32Mutation(&fixture, authority_to_static, 0U,
		1U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, static_to_authority, 0U,
		1U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition0,
		3U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, mechanisms, mechanism0 + 128U,
		SG_RUNE_COMPACT_MECHANISM_TELEPORT,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition0 + 16U,
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, authorities, 104U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition0 + 40U,
		12U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition0 + 52U,
		45U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition0 + 24U,
		0U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, mechanisms, mechanism1 + 128U,
		SG_RUNE_COMPACT_MECHANISM_DOOR,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition1 + 16U,
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));
	CHECK(ExpectU32Mutation(&fixture, mechanisms, mechanism2 + 128U,
		SG_RUNE_COMPACT_MECHANISM_DOOR,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition2 + 16U,
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectU32Mutation(&fixture, mechanisms, mechanism2 + 124U,
		751U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectU32Mutation(&fixture, mechanisms, mechanism3 + 128U,
		SG_RUNE_COMPACT_MECHANISM_LIFT,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectTwoU32Mutations(&fixture, mechanisms, mechanism3 + 84U,
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH, mechanism3 + 16U,
		0U, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition3 + 16U,
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition3 + 88U,
		UINT32_C(0x7fc00000), SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	CHECK(ExpectU32Mutation(&fixture, transitions, transition3 + 148U,
		UINT32_C(0x80000000), SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));
	return 1;
}

int main(void)
{
	int success = 1;

	success = TestActivePayloadRoundTrips() && success;
	success = TestInactivePayloadRejection() && success;
	success = TestPortalPayloadValidation() && success;
	success = TestLargeOriginTransportRoundTrip() && success;
	success = TestOwnerJoinRejection() && success;
	if (!success)
		return EXIT_FAILURE;
	puts("tagged transition wire tests passed");
	return EXIT_SUCCESS;
}
