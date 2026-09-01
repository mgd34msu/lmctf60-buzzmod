#include "../slipgate/sg_rune_compact_static_materializer.h"
#include "../slipgate/sg_rune_compact_binary32.h"
#include "../slipgate/sg_rune_compact_mechanisms.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(sg_rune_compact_static_mechanism_controller_t) == 52U,
	"compact controller provenance must remain 52 bytes");
_Static_assert(sizeof(sg_rune_compact_static_transition_t) == 248U,
	"compact transition must use the tagged 248-byte v11 projection");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

/* The production authority is opaque by design.  This fixture supplies the
 * owner/read seam without linking the authority implementation, allowing the
 * materializer tests to exercise the authentication boundary explicitly. */
struct sg_rune_compact_mechanisms_s
{
	sg_rune_compact_mechanisms_view_t view;
};

int SG_RuneCompactMechanismsRead(const sg_rune_compact_mechanisms_t *owner,
	sg_rune_compact_mechanisms_view_t *view_out)
{
	const struct sg_rune_compact_mechanisms_s *fixture_owner =
		(const struct sg_rune_compact_mechanisms_s *)owner;

	if (fixture_owner == NULL || view_out == NULL)
		return 0;
	*view_out = fixture_owner->view;
	return 1;
}

typedef struct materializer_fixture_s
{
	sg_rune_compact_identity_t compact_identity;
	sg_rune_model_identity_t identity;
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[2];
	sg_rune_compact_incidence_t incidences[3];
	sg_rune_compact_incidence_index_t cell_incidences[3];
	sg_rune_q8_vec3_t vertices[8];
	sg_rune_compact_portal_t portals[1];
	sg_configuration_semantic_region_t regions[1];
	sg_rune_compact_geometry_cell_span_t configuration_cell_spans[1];
	sg_rune_compact_cell_index_t configuration_cell_indices[2];
	sg_configuration_hook_surface_t hook_surfaces[3];
	sg_rune_vec3_t hook_vertices[12];
	sg_rune_compact_source_surface_t source_surfaces[3];
	sg_rune_q8_vec3_t source_surface_vertices[12];
	sg_static_visibility_surface_t visibility_surfaces[1];
	sg_bsp_entity_semantic_t entities[7];
	sg_bsp_entity_semantic_edge_t edges[3];
	sg_rune_compact_mechanism_authority_t authorities[4];
	sg_rune_compact_mechanism_controller_t controllers[3];
	sg_rune_compact_mechanism_topology_edge_t topology_edges[4];
	sg_rune_compact_mechanism_transition_t transitions[3];
	sg_rune_compact_mechanisms_view_t mechanism_view;
	struct sg_rune_compact_mechanisms_s mechanisms_owner;
	sg_bsp_entity_semantics_t entity_semantics;
	sg_configuration_semantics_t configuration;
	sg_static_visibility_t visibility;
	sg_rune_compact_static_materializer_input_t input;
	char strings[64];
} materializer_fixture_t;

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float FloatFromBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void TestStagedBinary32Law(void)
{
	const float local[3] = {
		FloatFromBits(UINT32_C(0xc183c4b4)),
		FloatFromBits(UINT32_C(0x3f77257c)),
		FloatFromBits(UINT32_C(0x408ef43a))
	};
	const float origin[3] = {
		FloatFromBits(UINT32_C(0x366fad94)), 0.0f, 0.0f
	};
	const float axis[3][3] = {
		{ FloatFromBits(UINT32_C(0x3a0ebddc)), 0.0f, 0.0f },
		{ FloatFromBits(UINT32_C(0x3a83afea)), 1.0f, 0.0f },
		{ FloatFromBits(UINT32_C(0x3aa3fedd)), 0.0f, 1.0f }
	};
	float world[3];

	CHECK(SG_RuneCompactBinary32TransformPoint(local, origin, axis, world));
	CHECK(Bits(world[0]) == UINT32_C(0xbb1daa77));
	CHECK(Bits(world[1]) == UINT32_C(0x3f77257c));
	CHECK(Bits(world[2]) == UINT32_C(0x408ef43a));
}

static void TestQ8TransportReplayUsesTheSameLaw(void)
{
	const sg_rune_q8_vec3_t local_q8 = { { -6484, 475, -4000 } };
	const float local[3] = { -810.5f, 59.375f, -500.0f };
	const uint32_t origin[3] = {
		UINT32_C(0xc771cb27), UINT32_C(0x47933c63), UINT32_C(0x46efdc2e)
	};
	const uint32_t axis[3][3] = {
		{ UINT32_C(0x3fa0b6ef), UINT32_C(0xbf6d3a04), UINT32_C(0x3fb494c5) },
		{ UINT32_C(0xbf90bdb2), UINT32_C(0xbf753470), UINT32_C(0xbfab1bca) },
		{ UINT32_C(0x3f4509ef), UINT32_C(0xbc5e14d2), UINT32_C(0x3f9d7f02) }
	};
	float origin_float[3];
	float axis_float[3][3];
	float world[3];
	uint32_t world_bits[3];
	uint32_t row;
	uint32_t column;

	for (column = 0U; column < 3U; column++)
		origin_float[column] = FloatFromBits(origin[column]);
	for (row = 0U; row < 3U; row++)
		for (column = 0U; column < 3U; column++)
			axis_float[row][column] = FloatFromBits(axis[row][column]);
	CHECK(SG_RuneCompactBinary32TransformPoint(local, origin_float,
		(const float (*)[3])axis_float, world));
	CHECK(Bits(world[0]) == UINT32_C(0xc77788c9));
	CHECK(Bits(world[1]) == UINT32_C(0x47949adf));
	CHECK(Bits(world[2]) == UINT32_C(0x46e1801c));
	CHECK(SG_RuneCompactStaticTransportDeriveWorldPointBits(&local_q8, origin,
		axis, world_bits));
	CHECK(world_bits[0] == UINT32_C(0xc77788c9));
	CHECK(world_bits[1] == UINT32_C(0x47949adf));
	CHECK(world_bits[2] == UINT32_C(0x46e1801c));
}

static void SetTransitionCells(
	sg_rune_compact_mechanism_transition_t *transition, uint32_t mechanism,
	uint32_t entry_cell, uint32_t exit_cell)
{
	memset(transition, 0, sizeof(*transition));
	transition->mechanism = mechanism;
	transition->entry_cell.value = entry_cell;
	transition->exit_cell.value = exit_cell;
	transition->source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	transition->destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	transition->elapsed_ms = 0U;
}

static void SetPortalTransition(
	sg_rune_compact_mechanism_transition_t *transition, uint32_t mechanism,
	uint32_t portal, uint32_t mover_model, uint32_t entry_cell,
	uint32_t exit_cell)
{
	SetTransitionCells(transition, mechanism, entry_cell, exit_cell);
	transition->kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	transition->value.portal_state.portal.value = portal;
	transition->value.portal_state.mover_model = mover_model;
	transition->value.portal_state.source_blocked = 1U;
	transition->value.portal_state.destination_blocked = 0U;
	transition->elapsed_ms = 100U;
}

static void SetTeleportTransition(
	sg_rune_compact_mechanism_transition_t *transition, uint32_t mechanism,
	uint32_t destination, uint32_t fanout_ordinal, uint32_t entry_cell,
	uint32_t exit_cell)
{
	SetTransitionCells(transition, mechanism, entry_cell, exit_cell);
	/* Teleport is stateless: the host does not expose an inactive/active
	 * transition for this fact.  Both endpoints are already active when the
	 * destination is selected. */
	transition->source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	transition->destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	transition->kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT;
	transition->value.teleport.destination.entity_ordinal = destination;
	transition->value.teleport.fanout_ordinal = fanout_ordinal;
	transition->value.teleport.approach_witness =
		(sg_rune_q8_vec3_t){ { 896, 256, 64 } };
	transition->value.teleport.entry_witness =
		(sg_rune_q8_vec3_t){ { 896, 256, 64 } };
	transition->value.teleport.exit_witness =
		(sg_rune_q8_vec3_t){ { 1536, 1280, 64 } };
}

static void SetPushTransition(
	sg_rune_compact_mechanism_transition_t *transition, uint32_t mechanism,
	uint32_t entry_cell, uint32_t exit_cell)
{
	SetTransitionCells(transition, mechanism, entry_cell, exit_cell);
	/* A push preserves the active state; its ballistic transport still carries
	 * the exact flight duration. */
	transition->source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	transition->destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	transition->kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH;
	transition->value.push.approach_witness =
		(sg_rune_q8_vec3_t){ { 768, 256, 64 } };
	transition->value.push.entry_witness =
		(sg_rune_q8_vec3_t){ { 768, 256, 64 } };
	transition->value.push.exit_witness =
		(sg_rune_q8_vec3_t){ { 1536, 1280, 64 } };
	transition->value.push.launch_velocity_bits[0] = Bits(256.0f);
	transition->value.push.launch_velocity_bits[1] = Bits(128.0f);
	transition->value.push.launch_velocity_bits[2] = Bits(64.0f);
	transition->value.push.gravity_bits = Bits(100.0f);
	transition->value.push.flight_ms = 750U;
	transition->elapsed_ms = transition->value.push.flight_ms;
}

static void SetLiftTransportTransition(
	sg_rune_compact_mechanism_transition_t *transition, uint32_t mechanism,
	uint32_t mover_model, uint32_t source_surface, uint32_t entry_cell,
	uint32_t exit_cell)
{
	SetTransitionCells(transition, mechanism, entry_cell, exit_cell);
	transition->kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
	transition->elapsed_ms = 5000U;
	transition->value.transport.mover_model = mover_model;
	transition->value.transport.source_surface_ordinal = source_surface;
	transition->value.transport.source_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	transition->value.transport.destination_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	transition->value.transport.fanout_ordinal = UINT32_MAX;
	/* The source catalog root is model-local (x=128, y=128..192,
	 * z=0..64).  Keep local support on that exact polygon while the world
	 * endpoint witnesses remain in the authenticated compact cell. */
	transition->value.transport.source_player_local =
		(sg_rune_q8_vec3_t){ { 1024, 1280, 280 } };
	transition->value.transport.destination_player_local =
		(sg_rune_q8_vec3_t){ { 1024, 1280, 280 } };
	transition->value.transport.source_support_local =
		(sg_rune_q8_vec3_t){ { 1024, 1280, 256 } };
	transition->value.transport.destination_support_local =
		(sg_rune_q8_vec3_t){ { 1024, 1280, 256 } };
	transition->value.transport.source_player_world_bits[0] = Bits(112.0f);
	transition->value.transport.source_player_world_bits[1] = Bits(32.0f);
	transition->value.transport.source_player_world_bits[2] = Bits(35.0f);
	transition->value.transport.destination_player_world_bits[0] = Bits(112.0f);
	transition->value.transport.destination_player_world_bits[1] = Bits(32.0f);
	transition->value.transport.destination_player_world_bits[2] = Bits(35.0f);
	transition->value.transport.source_support_world_bits[0] = Bits(112.0f);
	transition->value.transport.source_support_world_bits[1] = Bits(32.0f);
	transition->value.transport.source_support_world_bits[2] = Bits(32.0f);
	transition->value.transport.destination_support_world_bits[0] = Bits(112.0f);
	transition->value.transport.destination_support_world_bits[1] = Bits(32.0f);
	transition->value.transport.destination_support_world_bits[2] = Bits(32.0f);
	transition->value.transport.source_mover_origin_bits[0] = Bits(-16.0f);
	transition->value.transport.source_mover_origin_bits[1] = Bits(-128.0f);
	transition->value.transport.source_mover_origin_bits[2] = Bits(0.0f);
	transition->value.transport.destination_mover_origin_bits[0] =
		Bits(-16.0f);
	transition->value.transport.destination_mover_origin_bits[1] =
		Bits(-128.0f);
	transition->value.transport.destination_mover_origin_bits[2] = Bits(0.0f);
	transition->value.transport.source_mover_axis_bits[0][0] = Bits(1.0f);
	transition->value.transport.source_mover_axis_bits[1][1] = Bits(1.0f);
	transition->value.transport.source_mover_axis_bits[2][2] = Bits(1.0f);
	transition->value.transport.destination_mover_axis_bits[0][0] = Bits(1.0f);
	transition->value.transport.destination_mover_axis_bits[1][1] = Bits(1.0f);
	transition->value.transport.destination_mover_axis_bits[2][2] = Bits(1.0f);
	transition->value.transport.swept_static_clear = 1U;
	transition->value.transport.start_supported = 1U;
	transition->value.transport.end_supported = 1U;
}

static uint64_t DeriveBspContentId(const uint8_t digest[32])
{
	static const char domain[] = "lmctf.compact.bsp-content.v1";
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t index;

	for (index = 0U; index < sizeof(domain) - 1U; index++)
		hash = (hash ^ (uint64_t)(uint8_t)domain[index]) *
			UINT64_C(1099511628211);
	for (index = 0U; index < 32U; index++)
		hash = (hash ^ (uint64_t)digest[index]) * UINT64_C(1099511628211);
	if (hash == 0U)
		return UINT64_C(1);
	if (hash == UINT64_MAX)
		return UINT64_MAX - UINT64_C(1);
	return hash;
}

static void SetPlane(sg_rune_binary32_plane_t *plane, float normal_x,
	float normal_y, float normal_z, float distance)
{
	memset(plane, 0, sizeof(*plane));
	plane->normal_bits[0] = Bits(normal_x);
	plane->normal_bits[1] = Bits(normal_y);
	plane->normal_bits[2] = Bits(normal_z);
	plane->distance_bits = Bits(distance);
}

static void SetEntity(sg_bsp_entity_semantic_t *entity, uint32_t ordinal,
	sg_rune_vec3_t origin, uint32_t flags, sg_rune_mechanism_kind_t kind,
	sg_mech_node_kind_t role)
{
	memset(entity, 0, sizeof(*entity));
	entity->source_set_identity = 0U;
	entity->source_entity_ordinal = ordinal;
	entity->canonical_ordinal = ordinal;
	entity->classname = SG_BSP_ENTITY_STRING_NONE;
	entity->targetname = SG_BSP_ENTITY_STRING_NONE;
	entity->required_item = SG_BSP_ENTITY_STRING_NONE;
	entity->spawned_classname = SG_BSP_ENTITY_STRING_NONE;
	entity->destination_map = SG_BSP_ENTITY_STRING_NONE;
	entity->bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	entity->flags = flags;
	entity->landmark_kind = SG_RUNE_LANDMARK_KIND_COUNT;
	entity->mechanism_kind = kind;
	entity->mechanism_role = role;
	entity->origin = origin;
}

static void InitFixture(materializer_fixture_t *fixture)
{
	sg_rune_bounds_t entity_bounds;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	memset(fixture->strings, 0, sizeof(fixture->strings));
	memcpy(fixture->strings, "misc_teleporter\0misc_teleporter_dest",
		sizeof("misc_teleporter\0misc_teleporter_dest") - 1U);
	for (index = 0U; index < 32U; index++)
		fixture->compact_identity.bsp_sha256[index] = (uint8_t)(index + 1U);
	fixture->compact_identity.bsp_bytes = 123U;
	fixture->compact_identity.bsp_checksum = 456U;
	fixture->compact_identity.entity_crc32 = 789U;
	fixture->compact_identity.source_counts.model_count = 4U;
	fixture->compact_identity.source_counts.leaf_count = 2U;
	fixture->compact_identity.source_counts.area_count = 2U;
	fixture->compact_identity.source_counts.plane_count = 6U;
	fixture->compact_identity.source_counts.brush_count = 3U;
	fixture->compact_identity.source_counts.brush_side_count = 4U;
	fixture->compact_identity.source_counts.entity_count = 7U;
	fixture->compact_identity.entity_semantics_id = 11U;
	fixture->compact_identity.physics_abi_id = 12U;
	fixture->compact_identity.collision_law_id = 15U;
	fixture->compact_identity.pmove_law_id = 16U;
	fixture->compact_identity.gravity_law_id = 17U;
	fixture->compact_identity.hook_law_id = 18U;
	fixture->compact_identity.mechanism_law_id = 19U;
	fixture->compact_identity.weapon_law_id = 20U;
	fixture->compact_identity.construction_id = 21U;
	fixture->compact_identity.schema_id = 13U;
	fixture->compact_identity.producer_identity = 14U;
	fixture->compact_identity.standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -16, -16, -24 } };
	fixture->compact_identity.standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 16, 16, 56 } };
	fixture->compact_identity.crouching_hull.mins =
		(sg_rune_q8_vec3_t){ { -16, -16, -24 } };
	fixture->compact_identity.crouching_hull.maxs =
		(sg_rune_q8_vec3_t){ { 16, 16, 32 } };
	fixture->compact_identity.physics.gravity_bits = Bits(800.0f);
	fixture->compact_identity.physics.ground_acceleration_bits = Bits(10.0f);
	fixture->compact_identity.physics.air_acceleration_bits = Bits(1.0f);
	fixture->compact_identity.physics.water_acceleration_bits = Bits(4.0f);
	fixture->compact_identity.physics.hook_acceleration_bits = Bits(5.0f);
	fixture->compact_identity.physics.external_acceleration_bits = Bits(6.0f);
	fixture->compact_identity.physics.water_drag_bits = Bits(1.0f);
	fixture->compact_identity.physics.max_velocity_bits = Bits(320.0f);
	fixture->compact_identity.physics.frame_ms = 10U;
	fixture->compact_identity.physics.substep_ms = 5U;

	fixture->identity.bsp_content_id =
		DeriveBspContentId(fixture->compact_identity.bsp_sha256);
	fixture->identity.entity_semantics_id = 11U;
	fixture->identity.physics_abi_id = 12U;
	fixture->identity.source_set_identity = 0U;
	fixture->identity.schema_id = 13U;
	fixture->identity.producer_identity = 14U;
	fixture->identity.standing_hull.mins =
		(sg_rune_vec3_t){ { -2.0f, -2.0f, -3.0f } };
	fixture->identity.standing_hull.maxs =
		(sg_rune_vec3_t){ { 2.0f, 2.0f, 7.0f } };
	fixture->identity.crouching_hull.mins =
		(sg_rune_vec3_t){ { -2.0f, -2.0f, -3.0f } };
	fixture->identity.crouching_hull.maxs =
		(sg_rune_vec3_t){ { 2.0f, 2.0f, 4.0f } };
	fixture->identity.physics.gravity = 800.0f;
	fixture->identity.physics.ground_acceleration = 10.0f;
	fixture->identity.physics.air_acceleration = 1.0f;
	fixture->identity.physics.water_acceleration = 4.0f;
	fixture->identity.physics.hook_acceleration = 5.0f;
	fixture->identity.physics.external_acceleration = 6.0f;
	fixture->identity.physics.water_drag = 1.0f;
	fixture->identity.physics.max_velocity = 320.0f;
	fixture->identity.physics.frame_ms = 10U;
	fixture->identity.physics.substep_ms = 5U;

	fixture->cells[0].source = (sg_rune_compact_cell_source_t){ 0U, 0U,
		0U, 0, 0U };
	fixture->cells[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	fixture->cells[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 1024, 4096, 512 } };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cells[0].semantics = SG_RUNE_COMPACT_CELL_HAZARD;
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 2U };
	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].source.leaf = 1U;
	fixture->cells[1].source.area = 1U;
	fixture->cells[1].bounds.mins.value[0] = 1024;
	fixture->cells[1].bounds.maxs.value[0] = 2048;
	fixture->cells[1].bounds.maxs.value[1] = 4096;
	fixture->cells[1].semantics = 0U;
	fixture->cells[1].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 2U, 1U };

	fixture->vertices[0] = (sg_rune_q8_vec3_t){ { 1024, 0, 0 } };
	fixture->vertices[1] = (sg_rune_q8_vec3_t){ { 1024, 512, 0 } };
	fixture->vertices[2] = (sg_rune_q8_vec3_t){ { 1024, 512, 512 } };
	fixture->vertices[3] = (sg_rune_q8_vec3_t){ { 1024, 0, 512 } };
	fixture->facets[0].source.kind = SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE;
	fixture->facets[0].source.value.brush_side.model = 0U;
	fixture->facets[0].source.value.brush_side.brush = 2U;
	fixture->facets[0].source.value.brush_side.brush_side = 3U;
	fixture->facets[0].source.value.brush_side.plane = 4U;
	SetPlane(&fixture->facets[0].plane, 1.0f, 0.0f, 0.0f, 128.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 4U };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 2U };
	fixture->facets[0].portal.value = 0U;
	fixture->facets[0].kind = SG_RUNE_COMPACT_FACET_POLYGON;
	fixture->facets[1].source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	fixture->facets[1].source.value.bsp_plane.model = 0U;
	fixture->facets[1].source.value.bsp_plane.leaf = 0U;
	fixture->facets[1].source.value.bsp_plane.plane = 5U;
	SetPlane(&fixture->facets[1].plane, 0.0f, 1.0f, 0.0f, 64.0f);
	fixture->facets[1].incidences =
		(sg_rune_compact_incidence_span_t){ 2U, 1U };
	fixture->facets[1].vertices.first = 4U;
	fixture->facets[1].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->facets[1].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].facet.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[1] = fixture->incidences[0];
	fixture->incidences[1].cell.value = 1U;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->incidences[2] = fixture->incidences[0];
	fixture->incidences[2].facet.value = 1U;
	fixture->incidences[2].cell.value = 0U;
	fixture->incidences[0].cell_ordinal = 0U;
	fixture->incidences[1].cell_ordinal = 0U;
	fixture->incidences[2].cell_ordinal = 1U;
	fixture->cell_incidences[0].value = 0U;
	fixture->cell_incidences[1].value = 2U;
	fixture->cell_incidences[2].value = 1U;
	fixture->portals[0].source = fixture->facets[0].source;
	fixture->portals[0].facet.value = 0U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portals[0].clearance_q8 = 128U;

	fixture->hook_surfaces[0].id = 7U;
	fixture->hook_surfaces[0].model = 0U;
	fixture->hook_surfaces[0].brush = 2U;
	fixture->hook_surfaces[0].brush_side = 3U;
	fixture->hook_surfaces[0].normal[0] = 1.0f;
	fixture->hook_surfaces[0].distance = 128.0f;
	fixture->hook_surfaces[0].first_vertex = 0U;
	fixture->hook_surfaces[0].vertex_count = 4U;
	fixture->hook_surfaces[0].bounds.mins =
		(sg_rune_vec3_t){ { 128.0f, 0.0f, 0.0f } };
	fixture->hook_surfaces[0].bounds.maxs =
		(sg_rune_vec3_t){ { 128.125f, 64.0f, 64.0f } };
	fixture->hook_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
	fixture->hook_vertices[0] = (sg_rune_vec3_t){ { 128.0f, 0.0f, 0.0f } };
	fixture->hook_vertices[1] = (sg_rune_vec3_t){ { 128.0f, 64.0f, 0.0f } };
	fixture->hook_vertices[2] = (sg_rune_vec3_t){ { 128.0f, 64.0f, 64.0f } };
	fixture->hook_vertices[3] = (sg_rune_vec3_t){ { 128.0f, 0.0f, 64.0f } };
	fixture->hook_surfaces[1] = fixture->hook_surfaces[0];
	fixture->hook_surfaces[1].id = 8U;
	fixture->hook_surfaces[1].model = 1U;
	fixture->hook_surfaces[1].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
		SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	fixture->hook_surfaces[1].first_vertex = 4U;
	fixture->hook_surfaces[1].bounds.mins =
		(sg_rune_vec3_t){ { 128.0f, 128.0f, 0.0f } };
	fixture->hook_surfaces[1].bounds.maxs =
		(sg_rune_vec3_t){ { 128.125f, 192.0f, 64.0f } };
	fixture->hook_vertices[4] = (sg_rune_vec3_t){ { 128.0f, 128.0f, 0.0f } };
	fixture->hook_vertices[5] = (sg_rune_vec3_t){ { 128.0f, 192.0f, 0.0f } };
	fixture->hook_vertices[6] = (sg_rune_vec3_t){ { 128.0f, 192.0f, 64.0f } };
	fixture->hook_vertices[7] = (sg_rune_vec3_t){ { 128.0f, 128.0f, 64.0f } };
	fixture->hook_surfaces[2] = fixture->hook_surfaces[0];
	fixture->hook_surfaces[2].id = 9U;
	fixture->hook_surfaces[2].model = 3U;
	fixture->hook_surfaces[2].first_vertex = 8U;
	fixture->hook_surfaces[2].flags = SG_CONFIGURATION_HOOK_SURFACE_SKY;
	fixture->hook_vertices[8] = (sg_rune_vec3_t){ { 128.0f, 128.0f, 0.0f } };
	fixture->hook_vertices[9] = (sg_rune_vec3_t){ { 128.0f, 192.0f, 0.0f } };
	fixture->hook_vertices[10] = (sg_rune_vec3_t){ { 128.0f, 192.0f, 64.0f } };
	fixture->hook_vertices[11] = (sg_rune_vec3_t){ { 128.0f, 128.0f, 64.0f } };
	for (index = 0U; index < 3U; index++)
	{
		const sg_configuration_hook_surface_t *hook =
			&fixture->hook_surfaces[index];
		sg_rune_compact_source_surface_t *source =
			&fixture->source_surfaces[index];
		uint32_t vertex;
		uint32_t coordinate;

		memset(source, 0, sizeof(*source));
		source->source.model = hook->model;
		source->source.brush = hook->brush;
		source->source.brush_side = hook->brush_side;
		source->source.plane = 4U;
		source->frame = hook->model == 0U ?
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
			SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
		source->cell.value = SG_RUNE_COMPACT_INDEX_NONE;
		source->parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
		source->split_ordinal = 0U;
		SetPlane(&source->plane, hook->normal[0], hook->normal[1],
			hook->normal[2], hook->distance);
		source->vertices.first = index * 4U;
		source->vertices.count = 4U;
		for (vertex = 0U; vertex < 4U; vertex++)
			for (coordinate = 0U; coordinate < 3U; coordinate++)
				fixture->source_surface_vertices[index * 4U + vertex].value[
					coordinate] = (int32_t)(fixture->hook_vertices[index * 4U +
						vertex].value[coordinate] * 8.0f);
	}
	fixture->visibility_surfaces[0].id = 7U;
	fixture->visibility_surfaces[0].semantic_surface = 0U;
	fixture->visibility_surfaces[0].model = 0U;
	fixture->visibility_surfaces[0].brush = 2U;
	fixture->visibility_surfaces[0].brush_side = 3U;
	fixture->visibility_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;

	entity_bounds.mins = (sg_rune_vec3_t){ { 24.0f, 24.0f, 0.0f } };
	entity_bounds.maxs = (sg_rune_vec3_t){ { 40.0f, 40.0f, 16.0f } };
	SetEntity(&fixture->entities[0], 0U,
		(sg_rune_vec3_t){ { 32.0f, 32.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_MECHANISM | SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND |
		SG_BSP_ENTITY_HAS_BRUSH_MODEL | SG_BSP_ENTITY_HAS_BOUNDS |
		SG_BSP_ENTITY_DWELL_DEFINED | SG_BSP_ENTITY_SPEED_DEFINED,
		SG_RUNE_MECHANISM_DOOR, SG_MECH_NODE_DOOR_MASTER);
	fixture->entities[0].bsp_model = 1U;
	fixture->entities[0].bounds = entity_bounds;
	fixture->entities[0].dwell_ms = 250.0f;
	fixture->entities[0].speed = 100.0f;
	SetEntity(&fixture->entities[1], 1U,
		(sg_rune_vec3_t){ { 64.0f, 32.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_MECHANISM | SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND |
		SG_BSP_ENTITY_USE_ACTIVATED,
		SG_RUNE_MECHANISM_BUTTON, SG_MECH_NODE_BUTTON);
	SetEntity(&fixture->entities[2], 2U,
		(sg_rune_vec3_t){ { 64.0f, 32.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_MECHANISM | SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND,
		SG_RUNE_MECHANISM_LIFT, SG_MECH_NODE_PLATFORM);
	SetEntity(&fixture->entities[3], 3U,
		(sg_rune_vec3_t){ { 64.0f, 32.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_MECHANISM | SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND,
		SG_RUNE_MECHANISM_TELEPORT, SG_MECH_NODE_TELEPORTER);
	fixture->entities[3].classname = 0U;
	SetEntity(&fixture->entities[4], 4U,
		(sg_rune_vec3_t){ { 64.0f, 32.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_LANDMARK | SG_BSP_ENTITY_FLAG_BLUE,
		SG_RUNE_MECHANISM_KIND_COUNT, SG_MECH_NODE_CONTEXTUAL);
	fixture->entities[4].landmark_kind = SG_RUNE_LANDMARK_FLAG_STAND;
	SetEntity(&fixture->entities[5], 5U,
		(sg_rune_vec3_t){ { 64.0f, 32.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_LANDMARK,
		SG_RUNE_MECHANISM_KIND_COUNT, SG_MECH_NODE_CONTEXTUAL);
	fixture->entities[5].landmark_kind = SG_RUNE_LANDMARK_ITEM;
	SetEntity(&fixture->entities[6], 6U,
		(sg_rune_vec3_t){ { 192.0f, 160.0f, 8.0f } },
		SG_BSP_ENTITY_HAS_MECHANISM | SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND,
		SG_RUNE_MECHANISM_TELEPORT, SG_MECH_NODE_TELEPORT_DEST);
	fixture->entities[6].classname = (uint32_t)sizeof("misc_teleporter");
	fixture->edges[0].source = 1U;
	fixture->edges[0].destination = 0U;
	fixture->edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[0].name = SG_BSP_ENTITY_STRING_NONE;
	fixture->edges[0].fanout_ordinal = 0U;
	fixture->edges[1].source = 3U;
	fixture->edges[1].destination = 6U;
	fixture->edges[1].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[1].name = SG_BSP_ENTITY_STRING_NONE;
	fixture->edges[1].fanout_ordinal = 0U;
	memset(fixture->authorities, 0, sizeof(fixture->authorities));
	memset(fixture->controllers, 0, sizeof(fixture->controllers));
	memset(fixture->topology_edges, 0, sizeof(fixture->topology_edges));
	memset(fixture->transitions, 0, sizeof(fixture->transitions));
	for (index = 0U; index < 4U; index++)
		fixture->authorities[index].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->authorities[0].source.entity_ordinal = 0U;
	fixture->authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture->authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	fixture->authorities[0].activation_cell.value = 0U;
	fixture->authorities[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 256, 256, 64 } };
	fixture->authorities[0].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 192, 192, 0 } };
	fixture->authorities[0].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 320, 320, 128 } };
	fixture->authorities[0].controllers =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->authorities[0].topology =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->authorities[0].transitions =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->authorities[0].dwell_ms = 250U;
	fixture->authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;

	fixture->authorities[1].source.entity_ordinal = 1U;
	fixture->authorities[1].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON;
	fixture->authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	fixture->authorities[1].activation_cell.value = 0U;
	fixture->authorities[1].activation_witness =
		(sg_rune_q8_vec3_t){ { 512, 256, 64 } };
	fixture->authorities[1].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 480, 224, 32 } };
	fixture->authorities[1].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 544, 288, 96 } };
	fixture->authorities[1].controllers =
		(sg_rune_compact_mechanism_span_t){ 1U, 0U };
	fixture->authorities[1].topology =
		(sg_rune_compact_mechanism_span_t){ 1U, 1U };
	fixture->authorities[1].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[1].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[1].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;

	fixture->authorities[2].source.entity_ordinal = 2U;
	fixture->authorities[2].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT;
	fixture->authorities[2].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	fixture->authorities[2].activation_cell.value = 0U;
	fixture->authorities[2].activation_witness =
		(sg_rune_q8_vec3_t){ { 768, 256, 64 } };
	fixture->authorities[2].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 704, 192, 0 } };
	fixture->authorities[2].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 832, 320, 128 } };
	fixture->authorities[2].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[2].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[2].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;

	fixture->authorities[3].source.entity_ordinal = 3U;
	fixture->authorities[3].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT;
	fixture->authorities[3].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	fixture->authorities[3].activation_cell.value = 0U;
	fixture->authorities[3].activation_witness =
		(sg_rune_q8_vec3_t){ { 896, 256, 64 } };
	fixture->authorities[3].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 832, 192, 0 } };
	fixture->authorities[3].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 960, 320, 128 } };
	fixture->authorities[3].topology =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	fixture->authorities[3].transitions =
		(sg_rune_compact_mechanism_span_t){ 1U, 1U };
	fixture->authorities[3].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[3].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[3].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;

	fixture->controllers[0].mechanism = 0U;
	fixture->controllers[0].controller.entity_ordinal = 1U;
	fixture->controllers[0].topology_edge = 0U;
	fixture->controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->controllers[0].activation_cell.value = 0U;
	fixture->controllers[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 512, 256, 64 } };
	fixture->controllers[0].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 480, 224, 32 } };
	fixture->controllers[0].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 544, 288, 96 } };
	fixture->controllers[1].mechanism = 1U;
	fixture->controllers[1].controller.entity_ordinal = 2U;
	fixture->controllers[1].topology_edge = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->controllers[1].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->controllers[1].activation_cell.value = 0U;
	fixture->controllers[1].activation_witness =
		(sg_rune_q8_vec3_t){ { 768, 256, 64 } };
	fixture->controllers[1].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 704, 192, 0 } };
	fixture->controllers[1].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 832, 320, 128 } };
	fixture->controllers[2].mechanism = 1U;
	fixture->controllers[2].controller.entity_ordinal = 1U;
	fixture->controllers[2].topology_edge = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->controllers[2].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->controllers[2].activation_cell.value = 0U;
	fixture->controllers[2].activation_witness =
		(sg_rune_q8_vec3_t){ { 512, 256, 64 } };
	fixture->controllers[2].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 480, 224, 32 } };
	fixture->controllers[2].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 544, 288, 96 } };

	fixture->topology_edges[0].source.entity_ordinal = 1U;
	fixture->topology_edges[0].destination.entity_ordinal = 0U;
	fixture->topology_edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->topology_edges[0].fanout_ordinal = 0U;
	fixture->topology_edges[1] = fixture->topology_edges[0];
	fixture->topology_edges[2].source.entity_ordinal = 3U;
	fixture->topology_edges[2].destination.entity_ordinal = 6U;
	fixture->topology_edges[2].kind = SG_MECH_EDGE_TARGET;
	fixture->topology_edges[2].fanout_ordinal = 0U;
	/* Slot three is reserved for a train's authenticated path-corner TARGET
	 * fact.  It is not part of the default authorities' spans. */
	fixture->topology_edges[3].source.entity_ordinal = 4U;
	fixture->topology_edges[3].destination.entity_ordinal = 5U;
	fixture->topology_edges[3].kind = SG_MECH_EDGE_TARGET;
	fixture->topology_edges[3].fanout_ordinal = 0U;

	SetPortalTransition(&fixture->transitions[0], 0U, 0U, 1U, 0U, 1U);
	SetTeleportTransition(&fixture->transitions[1], 3U, 6U, 0U, 0U, 1U);

	fixture->mechanism_view.identity = fixture->compact_identity;
	fixture->mechanism_view.mechanisms = fixture->authorities;
	fixture->mechanism_view.mechanism_count = 4U;
	fixture->mechanism_view.controllers = fixture->controllers;
	fixture->mechanism_view.controller_count = 3U;
	fixture->mechanism_view.topology_edges = fixture->topology_edges;
	fixture->mechanism_view.topology_edge_count = 3U;
	fixture->mechanism_view.transitions = fixture->transitions;
	fixture->mechanism_view.transition_count = 2U;
	fixture->entity_semantics.source_set_identity = 0U;
	fixture->entity_semantics.world.source_set_identity = 0U;
	fixture->entity_semantics.entity_count = 7U;
	fixture->entity_semantics.entities = fixture->entities;
	fixture->entity_semantics.edge_count = 2U;
	fixture->entity_semantics.edges = fixture->edges;
	fixture->entity_semantics.strings = fixture->strings;
	fixture->entity_semantics.string_bytes = (uint32_t)sizeof(fixture->strings);

	fixture->configuration.identity = fixture->identity;
	fixture->configuration.hook_surfaces = fixture->hook_surfaces;
	fixture->configuration.hook_surface_count = 3U;
	fixture->configuration.hook_vertices = fixture->hook_vertices;
	fixture->configuration.hook_vertex_count = 12U;
	fixture->visibility.identity = fixture->identity;
	fixture->visibility.surfaces = fixture->visibility_surfaces;
	fixture->visibility.surface_count = 1U;

	fixture->input.geometry.identity = fixture->compact_identity;
	fixture->input.geometry.cells = fixture->cells;
	fixture->input.geometry.cell_count = 2U;
	fixture->input.geometry.facets = fixture->facets;
	fixture->input.geometry.facet_count = 2U;
	fixture->input.geometry.incidences = fixture->incidences;
	fixture->input.geometry.incidence_count = 3U;
	fixture->input.geometry.cell_incidences = fixture->cell_incidences;
	fixture->input.geometry.cell_incidence_count = 3U;
	fixture->input.geometry.vertices = fixture->vertices;
	fixture->input.geometry.vertex_count = 4U;
	fixture->input.geometry.portals = fixture->portals;
	fixture->input.geometry.portal_count = 1U;
	fixture->input.geometry.source_surfaces = fixture->source_surfaces;
	fixture->input.geometry.source_surface_count = 3U;
	fixture->input.geometry.source_surface_vertices =
		fixture->source_surface_vertices;
	fixture->input.geometry.source_surface_vertex_count = 12U;
	fixture->input.entities = &fixture->entity_semantics;
	fixture->input.configuration = &fixture->configuration;
	fixture->input.visibility = &fixture->visibility;
	fixture->mechanisms_owner.view = fixture->mechanism_view;
	fixture->input.mechanisms = &fixture->mechanisms_owner;
	for (index = 0U; index < 2U; index++)
		fixture->cells[index].valid_stances = SG_RUNE_STANCE_VALID_ALL;
}

static int Build(materializer_fixture_t *fixture,
	sg_rune_compact_static_materializer_t **materializer_out,
	sg_rune_compact_static_materializer_error_t *error_out)
{
	fixture->mechanisms_owner.view = fixture->mechanism_view;
	return SG_RuneCompactStaticMaterializerBuild(&fixture->input,
		materializer_out, error_out);
}

static void InitModelForFixture(const materializer_fixture_t *fixture,
	sg_rune_compact_model_t *model)
{
	memset(model, 0, sizeof(*model));
	model->identity = fixture->compact_identity;
	model->cells = fixture->cells;
	model->cell_count = 2U;
	model->facets = fixture->facets;
	model->facet_count = 2U;
	model->incidences = fixture->incidences;
	model->incidence_count = 3U;
	model->cell_incidences = fixture->cell_incidences;
	model->cell_incidence_count = 3U;
	model->vertices = fixture->vertices;
	model->vertex_count = fixture->input.geometry.vertex_count;
	model->portals = fixture->portals;
	model->portal_count = 1U;
	model->source_surfaces = fixture->source_surfaces;
	model->source_surface_count = 3U;
	model->source_surface_vertices = fixture->source_surface_vertices;
	model->source_surface_vertex_count = 12U;
}

static void CheckOutput(const materializer_fixture_t *fixture,
	const sg_rune_compact_static_t *static_data)
{
	sg_rune_compact_model_t model;
	sg_rune_compact_static_error_t error;

	InitModelForFixture(fixture, &model);
	CHECK(SG_RuneCompactStaticValidate(&model, static_data, &error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_NONE);
}

static const sg_rune_compact_mechanism_t *FindStaticMechanism(
	const sg_rune_compact_static_t *static_data, uint32_t source)
{
	uint32_t index;

	if (static_data == NULL)
		return NULL;
	for (index = 0U; index < static_data->mechanism_count; index++)
		if (static_data->mechanisms[index].source.entity_ordinal == source)
			return &static_data->mechanisms[index];
	return NULL;
}

static const sg_rune_compact_landmark_t *FindStaticLandmark(
	const sg_rune_compact_static_t *static_data, uint32_t source,
	sg_rune_compact_landmark_kind_t kind)
{
	uint32_t index;

	if (static_data == NULL)
		return NULL;
	for (index = 0U; index < static_data->landmark_count; index++)
		if (static_data->landmarks[index].source.entity_ordinal == source &&
			static_data->landmarks[index].kind == kind)
			return &static_data->landmarks[index];
	return NULL;
}

static const sg_rune_compact_facet_annotation_t *FindStaticFacetAnnotation(
	const sg_rune_compact_static_t *static_data, uint32_t facet)
{
	uint32_t index;

	if (static_data == NULL)
		return NULL;
	for (index = 0U; index < static_data->facet_annotation_count; index++)
		if (static_data->facet_annotations[index].facet.value == facet)
			return &static_data->facet_annotations[index];
	return NULL;
}

static void ConfigureAngularRotator(materializer_fixture_t *fixture,
	sg_bsp_entity_angular_mover_kind_t angular_kind)
{
	fixture->entities[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture->entities[0].angular_mover.kind = angular_kind;
	fixture->authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
}

static void TestAngularRotatorPortalAuthority(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	ConfigureAngularRotator(&fixture,
		SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR);
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		CHECK(static_data.mechanisms[0].kind ==
			SG_RUNE_COMPACT_MECHANISM_ROTATOR);
		CHECK((static_data.mechanisms[0].flags &
			SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U);
		CHECK(static_data.portal_mechanism_count == 1U);
		CHECK(static_data.portal_mechanisms[0].kind ==
			SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS);
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	InitFixture(&fixture);
	ConfigureAngularRotator(&fixture,
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR);
	materializer = NULL;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

}

static void TestButtonPortalBinding(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;

	InitFixture(&fixture);
	/* A brush button can be the authenticated controller and the physical
	 * portal-state owner.  It must therefore retain a BLOCKS binding just like
	 * a door; an ordinary point button with no transition remains landmark-only. */
	fixture.entities[0].mechanism_kind = SG_RUNE_MECHANISM_BUTTON;
	fixture.entities[0].mechanism_role = SG_MECH_NODE_BUTTON;
	fixture.authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON;
	fixture.authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	mechanism = FindStaticMechanism(&static_data, 0U);
	CHECK(mechanism != NULL);
	CHECK(static_data.portal_mechanism_count == 1U);
	if (static_data.portal_mechanism_count == 1U && mechanism != NULL)
	{
		CHECK(static_data.portal_mechanisms[0].portal.value == 0U);
		CHECK(static_data.portal_mechanisms[0].mechanism.value ==
			(uint32_t)(mechanism - static_data.mechanisms));
		CHECK(static_data.portal_mechanisms[0].kind ==
			SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestPortalTransitionBindingBijection(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_t mutated;
	sg_rune_compact_static_error_t static_error;
	sg_rune_compact_static_materializer_error_t error;
	sg_rune_compact_portal_mechanism_t bindings[2];
	sg_rune_compact_static_transition_t transitions[2];
	sg_rune_compact_model_t model;

	InitFixture(&fixture);
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	InitModelForFixture(&fixture, &model);
	CHECK(SG_RuneCompactStaticValidate(&model, &static_data, &static_error));
	CHECK(static_data.transitions[0].value.portal_state.source_blocked == 1U);
	CHECK(static_data.transitions[0].value.portal_state.destination_blocked == 0U);

	/* Occupancy is an exact two-state fact, not a phase inference.  Reject
	 * malformed booleans and nonzero reserved bytes at the public boundary. */
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[0].value.portal_state.source_blocked = 2U;
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[0].value.portal_state.destination_blocked = 1U;
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[0].value.portal_state.reserved[0] = 1U;
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* Removing the sole binding leaves a portal transition with no terminal
	 * binding.  Public validation must catch the dropped projection. */
	mutated = static_data;
	mutated.portal_mechanisms = NULL;
	mutated.portal_mechanism_count = 0U;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* A binding that names another root is not equivalent to the first root's
	 * fact.  With one root in the fixture, the extra binding is rejected. */
	bindings[0] = static_data.portal_mechanisms[0];
	bindings[1] = bindings[0];
	bindings[1].mechanism.value = 1U;
	mutated = static_data;
	mutated.portal_mechanisms = bindings;
	mutated.portal_mechanism_count = 2U;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* A binding that points at another valid mechanism is not an equivalent
	 * portal fact: the transition and binding must name the same owner. */
	bindings[0] = static_data.portal_mechanisms[0];
	bindings[0].mechanism.value = 1U;
	mutated = static_data;
	mutated.portal_mechanisms = bindings;
	mutated.portal_mechanism_count = 1U;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	SG_RuneCompactStaticMaterializerDestroy(materializer);
	materializer = NULL;

	/* Independent mechanism roots may legitimately change the same portal.
	 * The pair key is (mechanism, portal), so both records survive and are
	 * sorted by mechanism first rather than being collapsed by portal. */
	InitFixture(&fixture);
	fixture.entities[2].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL |
		SG_BSP_ENTITY_HAS_BOUNDS;
	fixture.entities[2].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
	fixture.entities[2].mechanism_role = SG_MECH_NODE_DOOR_MASTER;
	fixture.entities[2].bsp_model = 1U;
	fixture.entities[2].bounds.mins =
		(sg_rune_vec3_t){ { 24.0f, 24.0f, 0.0f } };
	fixture.entities[2].bounds.maxs =
		(sg_rune_vec3_t){ { 40.0f, 40.0f, 16.0f } };
	fixture.authorities[2].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture.authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	SetPortalTransition(&fixture.transitions[2], 2U, 0U, 1U, 0U, 1U);
	fixture.mechanism_view.transition_count = 3U;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		InitModelForFixture(&fixture, &model);
		CHECK(SG_RuneCompactStaticValidate(&model, &static_data, &static_error));
		CHECK(static_data.portal_mechanism_count == 2U);
		if (static_data.portal_mechanism_count == 2U)
		{
			CHECK(static_data.portal_mechanisms[0].portal.value == 0U);
			CHECK(static_data.portal_mechanisms[1].portal.value == 0U);
			CHECK(static_data.portal_mechanisms[0].mechanism.value <
				static_data.portal_mechanisms[1].mechanism.value);
			CHECK(static_data.portal_mechanisms[0].kind ==
				SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS);
			CHECK(static_data.portal_mechanisms[1].kind ==
				SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS);
		}
		CheckOutput(&fixture, &static_data);

		/* Duplicate the first exact pair: a second binding for the same
		 * (mechanism, portal) key must fail closed. */
		bindings[0] = static_data.portal_mechanisms[0];
		bindings[1] = bindings[0];
		mutated = static_data;
		mutated.portal_mechanisms = bindings;
		mutated.portal_mechanism_count = 2U;
		CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

		/* Removing one of the two pair bindings must also fail closed. */
		mutated = static_data;
		mutated.portal_mechanisms = bindings;
		mutated.portal_mechanism_count = 1U;
		CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}
}

static void TestStaticMaterialization(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_identity_t bound_identity;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	/* Speed is units/second, not a duration.  The static schema has no
	 * authoritative travel observation, so a fractional speed must not be
	 * rejected or copied into travel_ms. */
	fixture.entities[0].speed = 100.5f;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_NONE);
	if (materializer == NULL)
	{
		fprintf(stderr, "materializer build error: %s (%u) domain=%u record=%u\n",
			SG_RuneCompactStaticMaterializerErrorString(error.code),
			(unsigned int)error.code, (unsigned int)error.domain,
			(unsigned int)error.record);
		return;
	}
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(SG_RuneCompactStaticMaterializerReadBound(materializer,
		&bound_identity, &static_data));
	CHECK(bound_identity.bsp_bytes == fixture.compact_identity.bsp_bytes);
	CHECK(bound_identity.schema_id == fixture.compact_identity.schema_id);
	CHECK(static_data.mechanism_count == 4U);
	CHECK(static_data.mechanism_edge_count == 3U);
	CHECK(static_data.landmark_count == 5U);
	CHECK(static_data.landmark_cell_count == 5U);
	CHECK(static_data.facet_annotation_count == 1U);
	CHECK(static_data.portal_mechanism_count == 1U);
	CHECK(static_data.mechanisms[0].required_item ==
		SG_BSP_ENTITY_STRING_NONE);
	CHECK(static_data.mechanisms[1].required_item ==
		SG_BSP_ENTITY_STRING_NONE);
	CHECK(static_data.mechanisms[2].required_item ==
		SG_BSP_ENTITY_STRING_NONE);
	CHECK(static_data.mechanisms[3].required_item ==
		SG_BSP_ENTITY_STRING_NONE);
	CHECK(static_data.mechanisms[0].dwell_ms == 250U);
	CHECK(static_data.mechanisms[0].travel_ms == 0U);
	CHECK(static_data.mechanisms[0].activation_landmark.value ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(static_data.mechanisms[2].kind == SG_RUNE_COMPACT_MECHANISM_LIFT);
	CHECK(static_data.mechanisms[3].kind == SG_RUNE_COMPACT_MECHANISM_TELEPORT);
	CHECK(static_data.landmarks[4].kind ==
		SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION);
	CHECK(static_data.landmarks[4].mechanism.value !=
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(static_data.landmarks[2].kind == SG_RUNE_COMPACT_LANDMARK_FLAG);
	CHECK(static_data.landmarks[2].mechanism.value ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(static_data.landmarks[3].kind == SG_RUNE_COMPACT_LANDMARK_AMMO);
	CHECK(static_data.landmarks[3].mechanism.value ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(static_data.facet_annotations[0].facet.value == 0U);
	CHECK((static_data.facet_annotations[0].attributes &
		SG_RUNE_COMPACT_FACET_HOOKABLE) != 0U);
	CHECK((static_data.facet_annotations[0].attributes &
		SG_RUNE_COMPACT_FACET_HAZARD) != 0U);
	CHECK((static_data.facet_annotations[0].attributes &
		SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY) != 0U);
	CHECK(static_data.portal_mechanisms[0].kind ==
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS);
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void ConfigureTeamPanelFixture(materializer_fixture_t *fixture)
{
	InitFixture(fixture);
	/* The master owns model 1; the selected brush panel is a distinct model
	 * four.  Both the entity semantics and the host topology must authenticate
	 * the same TEAM relation before the portal transition can select it. */
	fixture->compact_identity.source_counts.model_count = 5U;
	fixture->input.geometry.identity = fixture->compact_identity;
	fixture->mechanism_view.identity = fixture->compact_identity;
	fixture->entities[1].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture->entities[1].bsp_model = 4U;
	fixture->edges[2].source = 1U;
	fixture->edges[2].destination = 0U;
	fixture->edges[2].kind = SG_MECH_EDGE_TEAM;
	fixture->edges[2].name = SG_BSP_ENTITY_STRING_NONE;
	fixture->edges[2].fanout_ordinal = 0U;
	fixture->entity_semantics.edge_count = 3U;
	fixture->topology_edges[2] = fixture->topology_edges[0];
	fixture->topology_edges[1].source.entity_ordinal = 1U;
	fixture->topology_edges[1].destination.entity_ordinal = 0U;
	fixture->topology_edges[1].kind = SG_MECH_EDGE_TEAM;
	fixture->topology_edges[1].fanout_ordinal = 0U;
	fixture->topology_edges[2].source.entity_ordinal = 1U;
	fixture->topology_edges[2].destination.entity_ordinal = 0U;
	fixture->topology_edges[2].kind = SG_MECH_EDGE_TARGET;
	fixture->topology_edges[2].fanout_ordinal = 0U;
	fixture->topology_edges[3] = fixture->topology_edges[1];
	fixture->topology_edges[3].source.entity_ordinal = 3U;
	fixture->topology_edges[3].destination.entity_ordinal = 6U;
	fixture->topology_edges[3].kind = SG_MECH_EDGE_TARGET;
	fixture->authorities[0].topology =
		(sg_rune_compact_mechanism_span_t){ 0U, 2U };
	fixture->authorities[1].topology =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	fixture->authorities[3].topology =
		(sg_rune_compact_mechanism_span_t){ 3U, 1U };
	fixture->mechanism_view.topology_edge_count = 4U;
	fixture->transitions[0].value.portal_state.mover_model = 4U;
}

static void TestTeamPanelPortalModelJoin(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;

	ConfigureTeamPanelFixture(&fixture);
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		CHECK(static_data.transitions[0].value.portal_state.mover_model == 4U);
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	/* A model in range is not enough: without a TEAM member in the exact host
	 * span, the root-to-panel join must fail closed. */
	ConfigureTeamPanelFixture(&fixture);
	fixture.transitions[0].value.portal_state.mover_model = 2U;
	materializer = NULL;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* The semantic TEAM edge alone is not an authenticated host echo. */
	ConfigureTeamPanelFixture(&fixture);
	fixture.topology_edges[1].kind = SG_MECH_EDGE_TARGET;
	materializer = NULL;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
}

static void TestDeterminism(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *left = NULL;
	sg_rune_compact_static_materializer_t *right = NULL;
	sg_rune_compact_static_t a;
	sg_rune_compact_static_t b;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	CHECK(Build(&fixture, &left, &error));
	CHECK(Build(&fixture, &right, &error));
	if (left == NULL || right == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(left, &a));
	CHECK(SG_RuneCompactStaticMaterializerRead(right, &b));
	CHECK(a.mechanism_count == b.mechanism_count);
	CHECK(a.landmark_count == b.landmark_count);
	CHECK(a.facet_annotation_count == b.facet_annotation_count);
	CHECK(a.portal_mechanism_count == b.portal_mechanism_count);
	CHECK(memcmp(a.mechanisms, b.mechanisms,
		(size_t)a.mechanism_count * sizeof(*a.mechanisms)) == 0);
	CHECK(memcmp(a.landmarks, b.landmarks,
		(size_t)a.landmark_count * sizeof(*a.landmarks)) == 0);
	CHECK(memcmp(a.landmark_cells, b.landmark_cells,
		(size_t)a.landmark_cell_count * sizeof(*a.landmark_cells)) == 0);
	CHECK(memcmp(a.facet_annotations, b.facet_annotations,
		(size_t)a.facet_annotation_count * sizeof(*a.facet_annotations)) == 0);
	CHECK(memcmp(a.portal_mechanisms, b.portal_mechanisms,
		(size_t)a.portal_mechanism_count * sizeof(*a.portal_mechanisms)) == 0);
	CheckOutput(&fixture, &a);
	CheckOutput(&fixture, &b);
	SG_RuneCompactStaticMaterializerDestroy(left);
	SG_RuneCompactStaticMaterializerDestroy(right);
}

static void TestCanonicalEntityReferencesSurviveInhibitedGaps(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	uint32_t index;

	InitFixture(&fixture);
	/* Entity semantics retain source declaration ordinals across inhibited
	 * records; compact refs are dense canonical ordinals. */
	for (index = 0U; index < fixture.entity_semantics.entity_count; index++)
		fixture.entities[index].source_entity_ordinal = 10U + index * 3U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.mechanisms[0].source.entity_ordinal == 0U);
	CHECK(static_data.mechanisms[1].source.entity_ordinal == 1U);
	CHECK(static_data.landmarks[2].source.entity_ordinal == 4U);
	CHECK(static_data.mechanism_edges[0].source.entity_ordinal == 1U);
	CHECK(static_data.mechanism_edges[0].destination.entity_ordinal == 0U);
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestInhibitedEntityIsAbsent(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	uint32_t index;

	InitFixture(&fixture);
	/* The final item declaration was inhibited by the host.  The compact
	 * identity and effective semantic count therefore contain five entities,
	 * while source declaration ordinals still retain a gap. */
	fixture.entity_semantics.entity_count = 5U;
	fixture.entity_semantics.edge_count = 1U;
	fixture.compact_identity.source_counts.entity_count = 5U;
	fixture.input.geometry.identity.source_counts.entity_count = 5U;
	fixture.mechanism_view.identity = fixture.compact_identity;
	fixture.mechanism_view.mechanism_count = 3U;
	fixture.authorities[1].topology =
		(sg_rune_compact_mechanism_span_t){ 1U, 0U };
	fixture.authorities[2].topology =
		(sg_rune_compact_mechanism_span_t){ 1U, 0U };
	fixture.mechanism_view.topology_edge_count = 1U;
	fixture.mechanism_view.transition_count = 1U;
	/* The teleporter source's destination is the inhibited sixth record.  It
	 * is not a valid mechanism in this reduced effective entity set. */
	fixture.entities[3].flags = 0U;
	for (index = 0U; index < fixture.entity_semantics.entity_count; index++)
		fixture.entities[index].source_entity_ordinal = index == 0U ? 0U :
			(index + 1U) * 2U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.landmark_count == 2U);
	for (index = 0U; index < static_data.landmark_count; index++)
		CHECK(static_data.landmarks[index].kind != SG_RUNE_COMPACT_LANDMARK_AMMO);
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestSplitConfigurationCellMapping(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	fixture.cells[0].semantics = 0U;
	fixture.cells[1].semantics = 0U;
	fixture.regions[0].cell = 0U;
	fixture.regions[0].flags = SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	fixture.configuration.regions = fixture.regions;
	fixture.configuration.region_count = 1U;
	fixture.configuration_cell_spans[0].first = 0U;
	fixture.configuration_cell_spans[0].count = 2U;
	fixture.configuration_cell_indices[0].value = 0U;
	fixture.configuration_cell_indices[1].value = 1U;
	fixture.input.geometry.compact_cells_for_configuration_cell =
		fixture.configuration_cell_spans;
	fixture.input.geometry.compact_cells_for_configuration_cell_count = 1U;
	fixture.input.geometry.configuration_cell_compact_cells =
		fixture.configuration_cell_indices;
	fixture.input.geometry.configuration_cell_compact_cell_count = 2U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.facet_annotation_count == 1U);
	CHECK((static_data.facet_annotations[0].attributes &
		SG_RUNE_COMPACT_FACET_HAZARD) != 0U);
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestOneShotDwellSentinel(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	fixture.entities[2].mechanism_kind = SG_RUNE_MECHANISM_TRIGGER;
	fixture.entities[2].mechanism_role = SG_MECH_NODE_TRIGGER;
	fixture.authorities[2].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRIGGER;
	fixture.authorities[2].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	fixture.authorities[2].activation_witness =
		(sg_rune_q8_vec3_t){ { 512, 256, 64 } };
	fixture.authorities[2].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 480, 224, 32 } };
	fixture.authorities[2].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 544, 288, 96 } };
	fixture.authorities[2].controllers =
		(sg_rune_compact_mechanism_span_t){ 1U, 1U };
	fixture.authorities[2].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_DISABLED;
	fixture.controllers[1].mechanism = 2U;
	fixture.controllers[1].controller.entity_ordinal = 2U;
	fixture.mechanism_view.controller_count = 3U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.mechanisms[2].kind == SG_RUNE_COMPACT_MECHANISM_TRIGGER);
	CHECK(static_data.mechanisms[2].dwell_ms == 0U);
	CHECK((static_data.mechanisms[2].flags &
		SG_RUNE_COMPACT_MECHANISM_ONE_SHOT) != 0U);
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestMalformedAndAmbiguous(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	fixture.portals[0].negative_incidence.value = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
	InitFixture(&fixture);
	fixture.regions[0].cell = 0U;
	fixture.configuration.regions = fixture.regions;
	fixture.configuration.region_count = 1U;
	fixture.configuration_cell_spans[0] =
		(sg_rune_compact_geometry_cell_span_t){ 0U, 1U };
	fixture.configuration_cell_indices[0].value = 2U;
	fixture.input.geometry.compact_cells_for_configuration_cell =
		fixture.configuration_cell_spans;
	fixture.input.geometry.compact_cells_for_configuration_cell_count = 1U;
	fixture.input.geometry.configuration_cell_compact_cells =
		fixture.configuration_cell_indices;
	fixture.input.geometry.configuration_cell_compact_cell_count = 1U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
}

static void TestInlineBrushActivationWitness(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;
	const sg_rune_compact_landmark_t *landmark;

	InitFixture(&fixture);
	fixture.entities[0].origin = (sg_rune_vec3_t){ { 0.0f, 0.0f, 0.0f } };
	fixture.authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	mechanism = FindStaticMechanism(&static_data, 0U);
	CHECK(mechanism != NULL);
	if (mechanism == NULL)
	{
		SG_RuneCompactStaticMaterializerDestroy(materializer);
		return;
	}
	CHECK(mechanism->activation_landmark.value != SG_RUNE_COMPACT_INDEX_NONE);
	if (mechanism->activation_landmark.value != SG_RUNE_COMPACT_INDEX_NONE)
	{
		landmark = &static_data.landmarks[mechanism->activation_landmark.value];
		CHECK(landmark->origin.value[0] == fixture.authorities[0].activation_witness.value[0]);
		CHECK(landmark->origin.value[1] == fixture.authorities[0].activation_witness.value[1]);
		CHECK(landmark->origin.value[2] == fixture.authorities[0].activation_witness.value[2]);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestReversedPortalUsesAuthenticatedDirection(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;

	InitFixture(&fixture);
	fixture.portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE;
	fixture.transitions[0].entry_cell.value = 1U;
	fixture.transitions[0].exit_cell.value = 0U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	mechanism = FindStaticMechanism(&static_data, 0U);
	CHECK(mechanism != NULL);
	if (mechanism != NULL)
	{
		CHECK(mechanism->entry_cell.value == 1U);
		CHECK(mechanism->exit_cell.value == 0U);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestLiftTransportPreservesElapsedAndSupport(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;
	const sg_rune_compact_static_transition_t *transition;

	InitFixture(&fixture);
	fixture.entities[2].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture.entities[2].bsp_model = 1U;
	fixture.authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	SetLiftTransportTransition(&fixture.transitions[2], 2U, 1U, 1U, 0U, 0U);
	fixture.mechanism_view.transition_count = 3U;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	mechanism = FindStaticMechanism(&static_data, 2U);
	CHECK(mechanism != NULL);
	if (mechanism != NULL)
	{
		CHECK(mechanism->transitions.count == 1U);
		transition = &static_data.transitions[mechanism->transitions.first];
		CHECK(transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT);
		CHECK(transition->entry_cell.value == 0U);
		CHECK(transition->exit_cell.value == 0U);
		CHECK(transition->elapsed_ms == 5000U);
		CHECK(transition->value.transport.mover_model == 1U);
		CHECK(transition->value.transport.source_surface_ordinal == 1U);
		CHECK(transition->value.transport.swept_static_clear == 1U);
		CHECK(transition->value.transport.start_supported == 1U);
		CHECK(transition->value.transport.end_supported == 1U);
		CHECK(transition->value.transport.fanout_ordinal == UINT32_MAX);
		CHECK(transition->value.transport.source_endpoint.entity_ordinal ==
			SG_RUNE_COMPACT_INDEX_NONE);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestTrainTransportPreservesFanout(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;
	const sg_rune_compact_static_transition_t *transition;

	InitFixture(&fixture);
	fixture.entities[2].mechanism_kind = SG_RUNE_MECHANISM_TRAIN;
	fixture.entities[2].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture.entities[2].bsp_model = 1U;
	fixture.authorities[2].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN;
	fixture.entities[4].mechanism_role = SG_MECH_NODE_PATH_CORNER;
	fixture.entities[5].mechanism_role = SG_MECH_NODE_PATH_CORNER;
	fixture.authorities[2].topology =
		(sg_rune_compact_mechanism_span_t){ 3U, 1U };
	fixture.authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	SetLiftTransportTransition(&fixture.transitions[2], 2U, 1U, 1U, 0U, 0U);
	fixture.transitions[2].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[2].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[2].value.transport.source_endpoint.entity_ordinal = 4U;
	fixture.transitions[2].value.transport.destination_endpoint.entity_ordinal =
		5U;
	fixture.transitions[2].value.transport.fanout_ordinal = 0U;
	fixture.mechanism_view.topology_edge_count = 4U;
	fixture.mechanism_view.transition_count = 3U;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	mechanism = FindStaticMechanism(&static_data, 2U);
	CHECK(mechanism != NULL);
	if (mechanism != NULL)
	{
		transition = &static_data.transitions[mechanism->transitions.first];
		CHECK(transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT);
		CHECK(transition->elapsed_ms == 5000U);
		CHECK(transition->value.transport.fanout_ordinal == 0U);
		CHECK(transition->value.transport.source_endpoint.entity_ordinal == 4U);
		CHECK(transition->value.transport.destination_endpoint.entity_ordinal ==
			5U);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);

	/* Support evidence is mandatory for every transport tag, regardless of
	 * whether the mover is a lift or a train. */
	InitFixture(&fixture);
	fixture.entities[2].mechanism_kind = SG_RUNE_MECHANISM_TRAIN;
	fixture.entities[2].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture.entities[2].bsp_model = 1U;
	fixture.authorities[2].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN;
	fixture.entities[4].mechanism_role = SG_MECH_NODE_PATH_CORNER;
	fixture.entities[5].mechanism_role = SG_MECH_NODE_PATH_CORNER;
	fixture.authorities[2].topology =
		(sg_rune_compact_mechanism_span_t){ 3U, 1U };
	fixture.authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	SetLiftTransportTransition(&fixture.transitions[2], 2U, 1U, 1U, 0U, 0U);
	fixture.transitions[2].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[2].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[2].value.transport.source_endpoint.entity_ordinal = 4U;
	fixture.transitions[2].value.transport.destination_endpoint.entity_ordinal =
		5U;
	fixture.transitions[2].value.transport.fanout_ordinal = 0U;
	fixture.mechanism_view.topology_edge_count = 4U;
	fixture.transitions[2].value.transport.start_supported = 0U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
}

static void TestTransportRejectsNegativeZeroWorldBits(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_materializer_error_t error;

	InitFixture(&fixture);
	fixture.entities[2].mechanism_kind = SG_RUNE_MECHANISM_TRAIN;
	fixture.entities[2].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture.entities[2].bsp_model = 1U;
	fixture.authorities[2].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN;
	fixture.entities[4].mechanism_role = SG_MECH_NODE_PATH_CORNER;
	fixture.entities[5].mechanism_role = SG_MECH_NODE_PATH_CORNER;
	fixture.authorities[2].topology =
		(sg_rune_compact_mechanism_span_t){ 3U, 1U };
	fixture.authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	SetLiftTransportTransition(&fixture.transitions[2], 2U, 1U, 1U, 0U, 0U);
	fixture.transitions[2].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[2].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[2].value.transport.source_endpoint.entity_ordinal = 4U;
	fixture.transitions[2].value.transport.destination_endpoint.entity_ordinal =
		5U;
	fixture.transitions[2].value.transport.fanout_ordinal = 0U;
	fixture.mechanism_view.topology_edge_count = 4U;
	fixture.mechanism_view.transition_count = 3U;
	fixture.transitions[2].value.transport.source_player_world_bits[0] =
		UINT32_C(0x80000000);
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
}

static int FindTransportTransition(const sg_rune_compact_static_t *static_data,
	uint32_t *index_out)
{
	uint32_t index;

	if (static_data == NULL || index_out == NULL)
		return 0;
	for (index = 0U; index < static_data->transition_count; index++)
		if (static_data->transitions[index].kind ==
			SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT)
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static void TestTransportEndpointValidation(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_t mutated;
	sg_rune_compact_static_error_t static_error;
	sg_rune_compact_static_materializer_error_t error;
	sg_rune_compact_model_t model;
	sg_rune_compact_static_transition_t transitions[3];
	sg_rune_compact_source_surface_t source_surfaces[3];
	uint32_t transition_index;
	uint32_t coordinate;
	uint32_t row;
	uint32_t column;

	InitFixture(&fixture);
	fixture.entities[2].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture.entities[2].bsp_model = 1U;
	fixture.authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	SetLiftTransportTransition(&fixture.transitions[2], 2U, 1U, 1U, 0U, 0U);
	fixture.mechanism_view.transition_count = 3U;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(FindTransportTransition(&static_data, &transition_index));
	if (!FindTransportTransition(&static_data, &transition_index))
	{
		SG_RuneCompactStaticMaterializerDestroy(materializer);
		return;
	}
	InitModelForFixture(&fixture, &model);
	CHECK(SG_RuneCompactStaticValidate(&model, &static_data, &static_error));

	/* Every transform word is part of the direct static boundary.  Reject
	 * signed zero in either mover origin or either 3x3 axis matrix even when
	 * the resulting world witness would be numerically unchanged. */
	for (coordinate = 0U; coordinate < 3U; coordinate++)
	{
		memcpy(transitions, static_data.transitions, sizeof(transitions));
		transitions[transition_index].value.transport.source_mover_origin_bits[
			coordinate] = UINT32_C(0x80000000);
		mutated = static_data;
		mutated.transitions = transitions;
		CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

		memcpy(transitions, static_data.transitions, sizeof(transitions));
		transitions[transition_index].value.transport.destination_mover_origin_bits[
			coordinate] = UINT32_C(0x80000000);
		mutated = static_data;
		mutated.transitions = transitions;
		CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));
	}
	for (row = 0U; row < 3U; row++)
		for (column = 0U; column < 3U; column++)
		{
			memcpy(transitions, static_data.transitions, sizeof(transitions));
			transitions[transition_index].value.transport.source_mover_axis_bits[
				row][column] = UINT32_C(0x80000000);
			mutated = static_data;
			mutated.transitions = transitions;
			CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

			memcpy(transitions, static_data.transitions, sizeof(transitions));
			transitions[transition_index].value.transport.destination_mover_axis_bits[
				row][column] = UINT32_C(0x80000000);
			mutated = static_data;
			mutated.transitions = transitions;
			CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));
		}

	/* A finite world endpoint in the neighboring/overlapping cell is not a
	 * valid terminal witness for this transition's entry/exit cell. */
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[transition_index].value.transport.source_player_world_bits[0] =
		Bits(160.0f);
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* A transform mutation is independently invalid even when the serialized
	 * world witnesses are left unchanged.  The receiver must authenticate the
	 * exact mover transform, rather than trust a copied world point. */
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[transition_index].value.transport.source_mover_origin_bits[0] =
		Bits(-15.0f);
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* Local player XY must be the supported pose, not merely a point in the
	 * same local mover surface.  Preserve source/destination equality while
	 * moving both player witnesses away from the support witness. */
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[transition_index].value.transport.source_player_local.value[0] += 8;
	transitions[transition_index].value.transport.source_player_local.value[1] += 8;
	transitions[transition_index].value.transport.destination_player_local.value[0] += 8;
	transitions[transition_index].value.transport.destination_player_local.value[1] += 8;
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* Local support must remain on the exact authenticated mover-local source
	 * polygon; an integer Q8 point alone is not sufficient evidence. */
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	transitions[transition_index].value.transport.source_support_local.value[0] =
		0;
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	/* A world-root/child-looking source with the same model and plane must not
	 * satisfy a mover transport join. */
	memcpy(source_surfaces, fixture.source_surfaces, sizeof(source_surfaces));
	source_surfaces[1].frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	model.source_surfaces = source_surfaces;
	memcpy(transitions, static_data.transitions, sizeof(transitions));
	mutated = static_data;
	mutated.transitions = transitions;
	CHECK(!SG_RuneCompactStaticValidate(&model, &mutated, &static_error));

	memcpy(source_surfaces, fixture.source_surfaces, sizeof(source_surfaces));
	source_surfaces[1].cell.value = 0U;
	model.source_surfaces = source_surfaces;
	CHECK(!SG_RuneCompactStaticValidate(&model, &static_data, &static_error));

	memcpy(source_surfaces, fixture.source_surfaces, sizeof(source_surfaces));
	source_surfaces[1].split_ordinal = 1U;
	model.source_surfaces = source_surfaces;
	CHECK(!SG_RuneCompactStaticValidate(&model, &static_data, &static_error));

	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestTransportTransformDerivation(void)
{
	const sg_rune_q8_vec3_t support = { { 0, 0, 256 } };
	const sg_rune_q8_vec3_t player = { { 0, 0, 280 } };
	const uint32_t origin[3] = {
		Bits(0.0f), Bits(0.0f), UINT32_C(0x4b7ffff3)
	};
	const uint32_t axis[3][3] = {
		{ Bits(1.0f), Bits(0.0f), Bits(0.0f) },
		{ Bits(0.0f), Bits(1.0f), Bits(0.0f) },
		{ Bits(0.0f), Bits(0.0f), Bits(1.0f) }
	};
	uint32_t support_world[3];
	uint32_t player_world[3];

	/* At this origin, independently transforming local 32 and 35 produces
	 * 16777236 and 16777238.  Reusing rounded support + 3 produces 16777240,
	 * a one-ULP error at the binary32 precision boundary. */
	CHECK(SG_RuneCompactStaticTransportDeriveWorldPointBits(&support, origin,
		axis, support_world));
	CHECK(SG_RuneCompactStaticTransportDeriveWorldPointBits(&player, origin,
		axis, player_world));
	CHECK(support_world[2] == Bits(16777236.0f));
	CHECK(player_world[2] == Bits(16777238.0f));
	CHECK(player_world[2] != Bits(FloatFromBits(support_world[2]) + 3.0f));

	/* Transform provenance rejects negative zero, which is not a canonical
	 * origin/angle fact even though launch vectors preserve signed zero. */
	{
		uint32_t invalid_origin[3] = {
			UINT32_C(0x80000000), origin[1], origin[2]
		};

		CHECK(!SG_RuneCompactStaticTransportDeriveWorldPointBits(&support,
			invalid_origin, axis, support_world));
	}
}

static void ConfigurePushFixture(materializer_fixture_t *fixture)
{
	InitFixture(fixture);
	fixture->entities[2].mechanism_kind = SG_RUNE_MECHANISM_PUSH;
	fixture->entities[2].mechanism_role = SG_MECH_NODE_PUSH;
	fixture->authorities[2].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH;
	fixture->authorities[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	fixture->mechanism_view.transition_count = 3U;
	SetPushTransition(&fixture->transitions[2], 2U, 0U, 1U);
}

static void TestPushLandingUsesExitWitness(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_landmark_t *landmark;
	const sg_rune_compact_static_transition_t *transition;

	ConfigurePushFixture(&fixture);
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.transition_count == 3U);
	transition = &static_data.transitions[1];
	CHECK(transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH);
	CHECK(transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH);
	CHECK(transition->value.push.launch_velocity_bits[0] == Bits(256.0f));
	CHECK(transition->value.push.launch_velocity_bits[1] == Bits(128.0f));
	CHECK(transition->value.push.launch_velocity_bits[2] == Bits(64.0f));
	CHECK(transition->value.push.gravity_bits == Bits(100.0f));
	CHECK(transition->value.push.flight_ms == 750U);
	landmark = FindStaticLandmark(&static_data, 2U,
		SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING);
	CHECK(landmark != NULL);
	if (landmark != NULL)
	{
		CHECK(landmark->origin.value[0] == 1536);
		CHECK(landmark->origin.value[1] == 1280);
		CHECK(landmark->origin.value[2] == 64);
		CHECK(landmark->cells.count == 1U);
		CHECK(static_data.landmark_cells[landmark->cells.first].value == 1U);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestYaw17NegativeZeroLaunchComponent(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_static_transition_t *transition;

	ConfigurePushFixture(&fixture);
	/* Stock AngleVectors at yaw 17 preserves a signed-zero Z component. */
	fixture.transitions[2].value.push.launch_velocity_bits[2] =
		UINT32_C(0x80000000);
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.transition_count == 3U);
	transition = &static_data.transitions[1];
	CHECK(transition->value.push.launch_velocity_bits[2] ==
		UINT32_C(0x80000000));
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestActivationFactsRemainIndependent(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;

	InitFixture(&fixture);
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH |
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE |
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE |
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
	fixture.authorities[1].damage = 25;
	fixture.authorities[1].health = 100;
	fixture.entities[1].required_item = 0U;
	fixture.authorities[1].required_item = 0U;
	fixture.authorities[1].dwell_ms = 500U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 1U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
		{
			CHECK(mechanism->activation_mask ==
				(SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH |
				 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_USE |
				 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE |
				 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY));
			CHECK(mechanism->dwell_ms == 500U);
			CHECK(mechanism->damage == 25);
			CHECK(mechanism->health == 100);
			CHECK(mechanism->required_item == 0U);
		}
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	InitFixture(&fixture);
	fixture.entities[2].mechanism_kind = SG_RUNE_MECHANISM_TRIGGER;
	fixture.entities[2].mechanism_role = SG_MECH_NODE_TRIGGER;
	fixture.authorities[2].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRIGGER;
	fixture.authorities[2].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	fixture.authorities[2].activation_witness =
		(sg_rune_q8_vec3_t){ { 512, 256, 64 } };
	fixture.authorities[2].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 480, 224, 32 } };
	fixture.authorities[2].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 544, 288, 96 } };
	fixture.authorities[2].controllers =
		(sg_rune_compact_mechanism_span_t){ 1U, 1U };
	fixture.controllers[1].mechanism = 2U;
	fixture.controllers[1].controller.entity_ordinal = 2U;
	fixture.authorities[2].dwell_ms = 500U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 2U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
		{
			CHECK(mechanism->activation_mask ==
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH);
			CHECK(mechanism->dwell_ms == 500U);
		}
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	InitFixture(&fixture);
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE;
	fixture.authorities[1].damage = 25;
	fixture.authorities[1].health = 100;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 1U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
		{
			CHECK(mechanism->activation_mask ==
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE);
			CHECK(mechanism->damage == 25);
		}
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	InitFixture(&fixture);
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
	fixture.entities[1].required_item = 0U;
	fixture.authorities[1].required_item = 0U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 1U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
		{
			CHECK(mechanism->activation_mask ==
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY);
			CHECK(mechanism->required_item == 0U);
		}
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}
}

static void TestRequiredItemOffsetAndSentinel(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;

	/* Offset zero is a real string reference, not the absence sentinel. */
	InitFixture(&fixture);
	fixture.entities[1].required_item = 0U;
	fixture.authorities[1].required_item = 0U;
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 1U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
			CHECK(mechanism->required_item == 0U);
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	/* UINT32_MAX is a valid absence sentinel for a mechanism with no inventory
	 * gate; it must survive materialization unchanged. */
	InitFixture(&fixture);
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	CHECK(fixture.entities[1].required_item == SG_BSP_ENTITY_STRING_NONE);
	CHECK(fixture.authorities[1].required_item == SG_BSP_ENTITY_STRING_NONE);
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 1U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
			CHECK(mechanism->required_item == SG_BSP_ENTITY_STRING_NONE);
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	/* An absent item is UINT32_MAX, and zero must not be accepted as absent. */
	InitFixture(&fixture);
	fixture.entities[1].required_item = 0U;
	fixture.authorities[1].required_item = 0U;
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* Inventory activation without a string reference is also invalid. */
	InitFixture(&fixture);
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* A non-sentinel offset must point at a terminated entity string. */
	InitFixture(&fixture);
	fixture.entities[1].required_item = fixture.entity_semantics.string_bytes;
	fixture.authorities[1].required_item = fixture.entity_semantics.string_bytes;
	fixture.authorities[1].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
}

static void TestExactLandmarkCellLocalization(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_landmark_t *landmark;

	InitFixture(&fixture);
	fixture.entities[5].flags |= SG_BSP_ENTITY_HAS_BOUNDS;
	fixture.entities[5].origin = (sg_rune_vec3_t){ { 128.0f, 32.0f, 8.0f } };
	fixture.entities[5].bounds.mins =
		(sg_rune_vec3_t){ { 120.0f, 16.0f, 0.0f } };
	fixture.entities[5].bounds.maxs =
		(sg_rune_vec3_t){ { 136.0f, 48.0f, 16.0f } };
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		landmark = FindStaticLandmark(&static_data, 5U,
			SG_RUNE_COMPACT_LANDMARK_AMMO);
		CHECK(landmark != NULL);
		if (landmark != NULL)
		{
			CHECK(landmark->cells.count == 2U);
			CHECK(static_data.landmark_cells[landmark->cells.first].value == 0U);
			CHECK(static_data.landmark_cells[landmark->cells.first + 1U].value == 1U);
		}
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	InitFixture(&fixture);
	fixture.entities[5].flags |= SG_BSP_ENTITY_HAS_BOUNDS;
	fixture.entities[5].origin = (sg_rune_vec3_t){ { 64.0f, 128.0f, 8.0f } };
	fixture.entities[5].bounds.mins =
		(sg_rune_vec3_t){ { 32.0f, 100.0f, 0.0f } };
	fixture.entities[5].bounds.maxs =
		(sg_rune_vec3_t){ { 96.0f, 200.0f, 16.0f } };
	materializer = NULL;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);
}

static void TestTopologyKindsRemainDistinct(void)
{
	static const sg_mech_edge_kind_t source_kinds[] = {
		SG_MECH_EDGE_TARGET,
		SG_MECH_EDGE_TARGET_ENT,
		SG_MECH_EDGE_MOVE_TARGET,
		SG_MECH_EDGE_ROUTE_TARGET,
		SG_MECH_EDGE_PATH_TARGET
	};
	static const sg_rune_compact_mechanism_edge_kind_t compact_kinds[] = {
		SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET,
		SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET_ENT,
		SG_RUNE_COMPACT_MECHANISM_EDGE_MOVE_TARGET,
		SG_RUNE_COMPACT_MECHANISM_EDGE_ROUTE_TARGET,
		SG_RUNE_COMPACT_MECHANISM_EDGE_PATH_TARGET
	};
	uint32_t index;

	for (index = 0U; index < sizeof(source_kinds) / sizeof(source_kinds[0]);
		index++)
	{
		materializer_fixture_t fixture;
		sg_rune_compact_static_materializer_t *materializer = NULL;
		sg_rune_compact_static_t static_data;
		sg_rune_compact_static_materializer_error_t error;

		InitFixture(&fixture);
		fixture.topology_edges[0].kind = source_kinds[index];
		CHECK(Build(&fixture, &materializer, &error));
		CHECK(materializer != NULL);
		if (materializer == NULL)
			continue;
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		CHECK(static_data.mechanism_edge_count == 3U);
		CHECK(static_data.mechanism_edges[0].kind == compact_kinds[index]);
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	/* Distinct source facts with identical endpoints/fanout are not collapsed. */
	{
		materializer_fixture_t fixture;
		sg_rune_compact_static_materializer_t *materializer = NULL;
		sg_rune_compact_static_t static_data;
		sg_rune_compact_static_materializer_error_t error;

		InitFixture(&fixture);
		fixture.authorities[0].topology =
			(sg_rune_compact_mechanism_span_t){ 0U, 2U };
		fixture.authorities[0].controllers =
			(sg_rune_compact_mechanism_span_t){ 0U, 2U };
		fixture.topology_edges[0].kind = SG_MECH_EDGE_TARGET_ENT;
		fixture.topology_edges[1].kind = SG_MECH_EDGE_MOVE_TARGET;
		fixture.controllers[1].mechanism = 0U;
		fixture.controllers[1].controller.entity_ordinal = 1U;
		fixture.controllers[1].topology_edge = 1U;
		CHECK(Build(&fixture, &materializer, &error));
		CHECK(materializer != NULL);
		if (materializer != NULL)
		{
			CHECK(SG_RuneCompactStaticMaterializerRead(materializer,
				&static_data));
			CHECK(static_data.mechanism_edge_count == 4U);
			CHECK(static_data.mechanisms[0].topology.count == 2U);
			CHECK(static_data.mechanism_edges[0].kind ==
				SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET_ENT);
			CHECK(static_data.mechanism_edges[1].kind ==
				SG_RUNE_COMPACT_MECHANISM_EDGE_MOVE_TARGET);
			CheckOutput(&fixture, &static_data);
			SG_RuneCompactStaticMaterializerDestroy(materializer);
		}
	}
}

static void TestControllerMultiplicityAndProvenance(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_mechanism_t *mechanism;
	const sg_rune_compact_static_mechanism_controller_t *first;
	const sg_rune_compact_static_mechanism_controller_t *second;

	InitFixture(&fixture);
	fixture.authorities[0].controllers =
		(sg_rune_compact_mechanism_span_t){ 0U, 2U };
	fixture.authorities[0].topology =
		(sg_rune_compact_mechanism_span_t){ 0U, 2U };
	fixture.authorities[1].controllers =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	fixture.authorities[1].topology =
		(sg_rune_compact_mechanism_span_t){ 1U, 0U };
	fixture.controllers[1].mechanism = 0U;
	fixture.controllers[1].controller.entity_ordinal = 1U;
	fixture.controllers[1].topology_edge = 1U;
	fixture.controllers[1].activation_witness.value[0] = 520;
	fixture.controllers[1].activation_bounds =
		fixture.controllers[0].activation_bounds;
	fixture.controllers[2].mechanism = 1U;
	fixture.controllers[2].controller.entity_ordinal = 2U;
	fixture.controllers[2].topology_edge = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	mechanism = FindStaticMechanism(&static_data, 0U);
	CHECK(mechanism != NULL);
	if (mechanism != NULL)
	{
		CHECK(mechanism->controllers.count == 2U);
		first = &static_data.mechanism_controllers[mechanism->controllers.first];
		second = &static_data.mechanism_controllers[mechanism->controllers.first + 1U];
		CHECK(first->controller.entity_ordinal == 1U);
		CHECK(second->controller.entity_ordinal == 1U);
		CHECK(first->topology_edge == 0U);
		CHECK(second->topology_edge == 1U);
		CHECK(first->activation_cell.value ==
			fixture.controllers[0].activation_cell.value);
		CHECK(second->activation_cell.value ==
			fixture.controllers[1].activation_cell.value);
		CHECK(first->activation_witness.value[0] ==
			fixture.controllers[0].activation_witness.value[0]);
		CHECK(second->activation_witness.value[0] == 520);
		CHECK(first->activation_bounds.mins.value[0] ==
			fixture.controllers[0].activation_bounds.mins.value[0]);
		CHECK(second->activation_bounds.maxs.value[2] ==
			fixture.controllers[1].activation_bounds.maxs.value[2]);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestControllerTopologyEdgeMustBeAuthenticated(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_error_t static_error;
	sg_rune_compact_static_materializer_error_t error;
	sg_rune_compact_model_t model;
	sg_rune_compact_static_mechanism_controller_t controllers[3];

	InitFixture(&fixture);
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CheckOutput(&fixture, &static_data);

	/* A controller topology ordinal is not a free-form annotation.  It must
	 * name an edge in this mechanism's authenticated topology span. */
	CHECK(static_data.mechanism_controller_count <=
		(uint32_t)(sizeof(controllers) / sizeof(controllers[0])));
	memcpy(controllers, static_data.mechanism_controllers,
		(size_t)static_data.mechanism_controller_count * sizeof(controllers[0]));
	static_data.mechanism_controllers = controllers;
	controllers[0].topology_edge = UINT32_C(0xdeadbeef);
	memset(&model, 0, sizeof(model));
	model.identity = fixture.compact_identity;
	model.cells = fixture.cells;
	model.cell_count = 2U;
	model.facets = fixture.facets;
	model.facet_count = 2U;
	model.incidences = fixture.incidences;
	model.incidence_count = 3U;
	model.cell_incidences = fixture.cell_incidences;
	model.cell_incidence_count = 3U;
	model.vertices = fixture.vertices;
	model.vertex_count = fixture.input.geometry.vertex_count;
	model.portals = fixture.portals;
	model.portal_count = 1U;
	model.source_surfaces = fixture.source_surfaces;
	model.source_surface_count = 3U;
	model.source_surface_vertices = fixture.source_surface_vertices;
	model.source_surface_vertex_count = 12U;
	CHECK(!SG_RuneCompactStaticValidate(&model, &static_data, &static_error));
	CHECK(static_error.domain ==
		SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_CONTROLLER);
	SG_RuneCompactStaticMaterializerDestroy(materializer);
}

static void TestDisjointCoplanarSurfacesDoNotLeak(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_static_materializer_error_t error;
	const sg_rune_compact_facet_annotation_t *untrusted_annotation;

	InitFixture(&fixture);
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	CHECK(static_data.facet_annotation_count == 1U);
	CHECK((static_data.facet_annotations[0].attributes &
		SG_RUNE_COMPACT_FACET_HOOKABLE) != 0U);
	CHECK((static_data.facet_annotations[0].attributes &
		SG_RUNE_COMPACT_FACET_SKY) == 0U);
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);

	/* A polygon reconstructed only from a BSP plane must not inherit a hook,
	 * sky, visibility, or mover identity merely because it overlaps the same
	 * configured plane. */
	InitFixture(&fixture);
	fixture.vertices[4] = fixture.vertices[0];
	fixture.vertices[5] = fixture.vertices[1];
	fixture.vertices[6] = fixture.vertices[2];
	fixture.vertices[7] = fixture.vertices[3];
	fixture.input.geometry.vertex_count = 8U;
	fixture.facets[1] = fixture.facets[0];
	fixture.facets[1].source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	fixture.facets[1].source.value.bsp_plane.model = 0U;
	fixture.facets[1].source.value.bsp_plane.leaf = 0U;
	fixture.facets[1].source.value.bsp_plane.plane = 4U;
	fixture.facets[1].incidences =
		(sg_rune_compact_incidence_span_t){ 2U, 1U };
	fixture.facets[1].vertices =
		(sg_rune_compact_vertex_span_t){ 4U, 4U };
	fixture.facets[1].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer == NULL)
		return;
	CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
	untrusted_annotation = FindStaticFacetAnnotation(&static_data, 1U);
	CHECK(untrusted_annotation != NULL);
	if (untrusted_annotation != NULL)
	{
		CHECK((untrusted_annotation->attributes &
			SG_RUNE_COMPACT_FACET_HOOKABLE) == 0U);
		CHECK((untrusted_annotation->attributes &
			SG_RUNE_COMPACT_FACET_SKY) == 0U);
		CHECK((untrusted_annotation->attributes &
			SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY) == 0U);
	}
	CheckOutput(&fixture, &static_data);
	SG_RuneCompactStaticMaterializerDestroy(materializer);

	/* Portal bindings come from the authenticated PORTAL_STATE transition, not
	 * from coincident facet/source geometry.  An unrelated facet with an
	 * untrusted lineage therefore must not suppress the typed binding. */
	InitFixture(&fixture);
	fixture.facets[0].source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	fixture.facets[0].source.value.bsp_plane.model = 0U;
	fixture.facets[0].source.value.bsp_plane.leaf = 0U;
	fixture.facets[0].source.value.bsp_plane.plane = 4U;
	fixture.portals[0].source = fixture.facets[0].source;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		CHECK(static_data.portal_mechanism_count == 1U);
		CHECK(static_data.transition_count == 2U);
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}
}

static void TestSourceSurfaceIdentityGuards(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_materializer_error_t error;

	/* A source tuple is not valid merely because its plane is valid. */
	InitFixture(&fixture);
	fixture.source_surfaces[0].source.brush =
		fixture.compact_identity.source_counts.brush_count;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* Model-local roots must not be treated as world roots. */
	InitFixture(&fixture);
	fixture.source_surfaces[1].frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* The materializer rejects a child root as a semantic binding target. */
	InitFixture(&fixture);
	fixture.source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE - 1U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* A configured surface with no exact authenticated root cannot fall back to
	 * a coplanar root with a different brush-side identity. */
	InitFixture(&fixture);
	fixture.source_surfaces[0].source.brush_side = 2U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_AMBIGUOUS_BINDING);
}

static void TestAuthorityRangesAndTeleporterFanout(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_materializer_error_t error;
	int build_result;

	InitFixture(&fixture);
	fixture.authorities[0].activation_cell.value = 2U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	InitFixture(&fixture);
	fixture.authorities[0].flags = UINT32_C(4);
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	InitFixture(&fixture);
	fixture.authorities[0].initial_state =
		(sg_rune_compact_mechanism_authority_state_t)
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* A portal fact must use one of the authority's exact lifecycle pairs;
	 * an otherwise valid but unrelated state change is not admissible. */
	InitFixture(&fixture);
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	/* An autonomous reversible mover returns from ACTIVE to its reset state. */
	InitFixture(&fixture);
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.transitions[0].value.portal_state.destination_blocked = 1U;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
	{
		sg_rune_compact_static_t static_data;
		sg_rune_compact_static_error_t static_error;
		sg_rune_compact_model_t model;

		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		InitModelForFixture(&fixture, &model);
		CHECK(SG_RuneCompactStaticValidate(&model, &static_data,
			&static_error));
		SG_RuneCompactStaticMaterializerDestroy(materializer);
		materializer = NULL;
	}

	/* A toggle has no separate reset state; its return is ACTIVE -> INITIAL. */
	InitFixture(&fixture);
	fixture.authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.transitions[0].value.portal_state.destination_blocked = 1U;
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	if (materializer != NULL)
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	materializer = NULL;

	/* A one-shot mover may not publish either reverse lifecycle pair. */
	InitFixture(&fixture);
	fixture.authorities[0].flags =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT;
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.transitions[0].value.portal_state.destination_blocked = 1U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	InitFixture(&fixture);
	fixture.controllers[0].mechanism = 1U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	InitFixture(&fixture);
	fixture.transitions[0].value.portal_state.mover_model =
		SG_BSP_ENTITY_MODEL_NONE;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	InitFixture(&fixture);
	fixture.authorities[3].transitions =
		(sg_rune_compact_mechanism_span_t){ 1U, 2U };
	fixture.transitions[2] = fixture.transitions[1];
	fixture.mechanism_view.transition_count = 3U;
	CHECK(!Build(&fixture, &materializer, &error));
	CHECK(materializer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE);

	InitFixture(&fixture);
	fixture.authorities[0].controllers =
		(sg_rune_compact_mechanism_span_t){ 0U, 2U };
	fixture.controllers[1].mechanism = 0U;
	fixture.controllers[1].controller.entity_ordinal = 2U;
	CHECK(Build(&fixture, &materializer, &error));
	if (materializer != NULL)
	{
		sg_rune_compact_static_t static_data;
		const sg_rune_compact_mechanism_t *mechanism;

		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		mechanism = FindStaticMechanism(&static_data, 0U);
		CHECK(mechanism != NULL);
		if (mechanism != NULL)
		{
			CHECK(mechanism->controllers.count == 2U);
			CHECK(static_data.mechanism_controllers[
				mechanism->controllers.first].controller.entity_ordinal == 1U);
			CHECK(static_data.mechanism_controllers[
				mechanism->controllers.first + 1U].controller.entity_ordinal == 2U);
		}
		CheckOutput(&fixture, &static_data);
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}

	InitFixture(&fixture);
	fixture.authorities[3].transitions =
		(sg_rune_compact_mechanism_span_t){ 1U, 2U };
	fixture.transitions[2] = fixture.transitions[1];
	fixture.transitions[1].exit_cell.value = 0U;
	fixture.transitions[1].value.teleport.exit_witness =
		(sg_rune_q8_vec3_t){ { 896, 256, 64 } };
	fixture.transitions[2].value.teleport.fanout_ordinal = 1U;
	fixture.transitions[2].entry_cell.value = 0U;
	fixture.transitions[2].exit_cell.value = 0U;
	fixture.transitions[2].value.teleport.exit_witness =
		(sg_rune_q8_vec3_t){ { 896, 256, 64 } };
	fixture.topology_edges[3] = fixture.topology_edges[2];
	fixture.topology_edges[3].fanout_ordinal = 1U;
	fixture.authorities[3].topology =
		(sg_rune_compact_mechanism_span_t){ 2U, 2U };
	fixture.mechanism_view.topology_edge_count = 4U;
	fixture.mechanism_view.transition_count = 3U;
	build_result = Build(&fixture, &materializer, &error);
	CHECK(build_result);
	if (materializer != NULL)
	{
		sg_rune_compact_static_t static_data;
		sg_rune_compact_static_error_t static_error;
		sg_rune_compact_model_t model;
		sg_rune_compact_static_transition_t transitions[3];
			uint32_t mechanism_index;
			uint32_t transition_index;
			uint32_t mapped_static_transition;
			uint32_t mapped_authority_mechanism;
			uint32_t fanout_count = 0U;

		CHECK(SG_RuneCompactStaticMaterializerRead(materializer, &static_data));
		CHECK(static_data.mechanism_count == 5U);
		CHECK(static_data.transition_count == 3U);
		/* Static mechanism records are canonicalized independently from their
		 * authority root.  The owner-held provenance is the only valid join. */
		for (transition_index = 0U;
			transition_index < fixture.mechanism_view.transition_count;
			transition_index++) {
			CHECK(SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
				materializer, transition_index, &mapped_static_transition));
			if (mapped_static_transition < static_data.transition_count) {
				CHECK(static_data.transitions[mapped_static_transition].kind ==
					(sg_rune_compact_static_transition_kind_t)
					fixture.transitions[transition_index].kind);
				if (fixture.transitions[transition_index].kind ==
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
					CHECK(static_data.transitions[mapped_static_transition].value.
						teleport.fanout_ordinal == fixture.transitions[
							transition_index].value.teleport.fanout_ordinal);
			}
		}
		CHECK(!SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
			materializer, fixture.mechanism_view.transition_count,
			&mapped_static_transition));
		for (mechanism_index = 0U;
			mechanism_index < static_data.mechanism_count; mechanism_index++)
		{
			const sg_rune_compact_mechanism_t *mechanism =
				&static_data.mechanisms[mechanism_index];

			CHECK(SG_RuneCompactStaticMaterializerStaticMechanismAuthorityIndex(
				materializer, mechanism_index, &mapped_authority_mechanism));
			CHECK(mapped_authority_mechanism <
				fixture.mechanism_view.mechanism_count);

			if (mechanism->source.entity_ordinal != 3U)
				continue;
			CHECK(mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TELEPORT);
			CHECK(mapped_authority_mechanism == 3U);
			CHECK(mechanism->transitions.count == 1U);
			CHECK(mechanism->entry_cell.value == 0U);
			CHECK(mechanism->exit_cell.value == 0U);
			CHECK(mechanism->transition_fanout_ordinal == fanout_count);
			CHECK(static_data.transitions[mechanism->transitions.first].value.teleport.fanout_ordinal ==
				fanout_count);
			fanout_count++;
		}
		CHECK(!SG_RuneCompactStaticMaterializerStaticMechanismAuthorityIndex(
			materializer, static_data.mechanism_count,
			&mapped_authority_mechanism));
		CHECK(fanout_count == 2U);
		CheckOutput(&fixture, &static_data);
		memset(&model, 0, sizeof(model));
		model.identity = fixture.compact_identity;
		model.cells = fixture.cells;
		model.cell_count = 2U;
		model.facets = fixture.facets;
		model.facet_count = 2U;
		model.incidences = fixture.incidences;
		model.incidence_count = 3U;
		model.cell_incidences = fixture.cell_incidences;
		model.cell_incidence_count = 3U;
		model.vertices = fixture.vertices;
		model.vertex_count = fixture.input.geometry.vertex_count;
		model.portals = fixture.portals;
		model.portal_count = 1U;
		model.source_surfaces = fixture.source_surfaces;
		model.source_surface_count = 3U;
		model.source_surface_vertices = fixture.source_surface_vertices;
		model.source_surface_vertex_count = 12U;
		memcpy(transitions, static_data.transitions, sizeof(transitions));
		static_data.transitions = transitions;
		/* A destination that is merely in range is not a valid teleporter
		 * pairing.  The static topology span must retain the exact TARGET fact. */
		for (transition_index = 0U;
			transition_index < static_data.transition_count; transition_index++)
			if (transitions[transition_index].kind ==
				SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT)
			{
				transitions[transition_index].value.teleport.destination
					.entity_ordinal = 5U;
				CHECK(!SG_RuneCompactStaticValidate(&model,
					&static_data, &static_error));
				CHECK(static_error.code ==
					SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
				break;
			}
		SG_RuneCompactStaticMaterializerDestroy(materializer);
	}
}

static void TestAllocationFailure(void)
{
	materializer_fixture_t fixture;
	sg_rune_compact_static_materializer_t *materializer = NULL;
	sg_rune_compact_static_materializer_error_t error;
	size_t allocation_count;
	size_t failure;

	InitFixture(&fixture);
	SG_RuneCompactStaticMaterializerTestFailAfter(SIZE_MAX);
	CHECK(Build(&fixture, &materializer, &error));
	CHECK(materializer != NULL);
	allocation_count = SG_RuneCompactStaticMaterializerTestAllocationCount();
	CHECK(allocation_count != 0U);
	if (materializer != NULL)
		SG_RuneCompactStaticMaterializerDestroy(materializer);

	/* Every allocation site in the successful build is fault-injected in turn.
	 * This is finite fault-site coverage, not a work limit: a deterministic
	 * build must fail closed at each ordinal and publish no partial owner. */
	for (failure = 0U; failure < allocation_count; failure++)
	{
		materializer = NULL;
		SG_RuneCompactStaticMaterializerTestFailAfter(failure);
		CHECK(!Build(&fixture, &materializer, &error));
		CHECK(materializer == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY);
	}
	SG_RuneCompactStaticMaterializerTestFailAfter(SIZE_MAX);
}

int main(void)
{
	TestAngularRotatorPortalAuthority();
	TestButtonPortalBinding();
	TestPortalTransitionBindingBijection();
	TestStaticMaterialization();
	TestTeamPanelPortalModelJoin();
	TestDeterminism();
	TestCanonicalEntityReferencesSurviveInhibitedGaps();
	TestInhibitedEntityIsAbsent();
	TestSplitConfigurationCellMapping();
	TestOneShotDwellSentinel();
	TestMalformedAndAmbiguous();
	TestInlineBrushActivationWitness();
	TestReversedPortalUsesAuthenticatedDirection();
	TestLiftTransportPreservesElapsedAndSupport();
	TestTrainTransportPreservesFanout();
	TestTransportRejectsNegativeZeroWorldBits();
	TestTransportEndpointValidation();
	TestTransportTransformDerivation();
	TestStagedBinary32Law();
	TestQ8TransportReplayUsesTheSameLaw();
	TestPushLandingUsesExitWitness();
	TestYaw17NegativeZeroLaunchComponent();
	TestActivationFactsRemainIndependent();
	TestRequiredItemOffsetAndSentinel();
	TestExactLandmarkCellLocalization();
	TestTopologyKindsRemainDistinct();
	TestControllerMultiplicityAndProvenance();
	TestControllerTopologyEdgeMustBeAuthenticated();
	TestDisjointCoplanarSurfacesDoNotLeak();
	TestSourceSurfaceIdentityGuards();
	TestAuthorityRangesAndTeleporterFanout();
	TestAllocationFailure();
	if (failures != 0)
	{
		fprintf(stderr, "%d compact static materializer checks failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("compact static materializer checks passed");
	return EXIT_SUCCESS;
}
