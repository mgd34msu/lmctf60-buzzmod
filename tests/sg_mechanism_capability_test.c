#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_mechanism_capability_internal.h"

#define ENTITY_COUNT UINT32_C(10)
#define TRACE_COUNT UINT32_C(13)
#define PHASE_COUNT UINT32_C(3)

typedef struct mechanism_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[7];
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaves[3];
	uint32_t leaf_brush;
	sg_bsp_model_t models[2];
	sg_bsp_brush_t brush;
	sg_bsp_brush_side_t brush_sides[6];
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *configuration_semantics;
	sg_bsp_entity_semantic_t entities[ENTITY_COUNT];
	sg_bsp_entity_semantic_edge_t edges[4];
	sg_bsp_entity_semantics_t entity_semantics;
	sg_rune_phase_basis_t phases[PHASE_COUNT];
	sg_host_collision_instance_t inactive_instance;
	sg_host_collision_instance_t active_instance;
	sg_mechanism_capability_candidate_t candidates[TRACE_COUNT];
	sg_mechanism_host_trace_t traces[TRACE_COUNT];
	sg_mechanism_host_trace_catalog_t catalog;
	sg_mechanism_capability_source_t source;
	sg_mechanism_capability_owner_t *capability_owner;
} mechanism_fixture_t;

static int failures;

static void FixtureDestroy(mechanism_fixture_t *fixture);

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CAPABILITY(fixture_value, handle) \
	SG_MechanismCapabilityOwnerPayload((fixture_value).capability_owner, \
		(handle))

static sg_rune_model_identity_t MakeIdentity(float gravity)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x101);
	identity.entity_semantics_id = UINT64_C(0x202);
	identity.physics_abi_id = UINT64_C(0x303);
	identity.source_set_identity = UINT64_C(0x404);
	identity.schema_id = UINT64_C(0x505);
	identity.producer_identity = UINT64_C(0x606);
	identity.standing_hull.mins.value[0] = -16.0f;
	identity.standing_hull.mins.value[1] = -16.0f;
	identity.standing_hull.mins.value[2] = -24.0f;
	identity.standing_hull.maxs.value[0] = 16.0f;
	identity.standing_hull.maxs.value[1] = 16.0f;
	identity.standing_hull.maxs.value[2] = 32.0f;
	identity.crouching_hull.mins.value[0] = -16.0f;
	identity.crouching_hull.mins.value[1] = -16.0f;
	identity.crouching_hull.mins.value[2] = -24.0f;
	identity.crouching_hull.maxs.value[0] = 16.0f;
	identity.crouching_hull.maxs.value[1] = 16.0f;
	identity.crouching_hull.maxs.value[2] = 4.0f;
	identity.physics.gravity = gravity;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 8.0f;
	identity.physics.external_acceleration = 3.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 10U;
	return identity;
}

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	Set3(plane->normal.value, x, y, z);
	plane->distance = distance;
	if (x == 1.0f)
		plane->type = 0;
	else if (y == 1.0f)
		plane->type = 1;
	else if (z == 1.0f)
		plane->type = 2;
	else if (x == -1.0f)
		plane->type = 3;
	else if (y == -1.0f)
		plane->type = 4;
	else
		plane->type = 5;
}

static void InitWorld(mechanism_fixture_t *fixture)
{
	uint32_t side;

	SetPlane(&fixture->planes[0], 1.0f, 0.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[1], 1.0f, 0.0f, 0.0f, 2.0f);
	SetPlane(&fixture->planes[2], -1.0f, 0.0f, 0.0f, 2.0f);
	SetPlane(&fixture->planes[3], 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[4], 0.0f, -1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[5], 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture->planes[6], 0.0f, 0.0f, -1.0f, 64.0f);
	fixture->node.plane = 0U;
	fixture->node.children[0] = -1;
	fixture->node.children[1] = -2;
	fixture->leaves[0].cluster = 0;
	fixture->leaves[0].area = 1U;
	fixture->leaves[1].cluster = 1;
	fixture->leaves[1].area = 2U;
	fixture->leaves[2].contents = SG_HOST_CONTENTS_SOLID;
	fixture->leaves[2].cluster = -1;
	fixture->leaves[2].first_leaf_brush = 0U;
	fixture->leaves[2].leaf_brush_count = 1U;
	fixture->leaf_brush = 0U;
	fixture->brush.first_side = 0U;
	fixture->brush.side_count = 6U;
	fixture->brush.contents = SG_HOST_CONTENTS_SOLID;
	for (side = 0U; side < 6U; side++)
	{
		fixture->brush_sides[side].plane = side + 1U;
		fixture->brush_sides[side].texinfo = -1;
	}
	fixture->models[0].headnode = 0;
	Set3(fixture->models[0].mins.value, -4096.0f, -4096.0f, -4096.0f);
	Set3(fixture->models[0].maxs.value, 4095.875f, 4095.875f, 4095.875f);
	fixture->models[1].headnode = -3;
	Set3(fixture->models[1].mins.value, -2.0f, -64.0f, -64.0f);
	Set3(fixture->models[1].maxs.value, 2.0f, 64.0f, 64.0f);
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = 7U;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 3U;
	fixture->world.leaf_brushes = &fixture->leaf_brush;
	fixture->world.leaf_brush_count = 1U;
	fixture->world.models = fixture->models;
	fixture->world.model_count = 2U;
	fixture->world.brushes = &fixture->brush;
	fixture->world.brush_count = 1U;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = 6U;
}

static sg_rune_mechanism_ref_t MechanismRef(const mechanism_fixture_t *fixture,
	uint32_t entity)
{
	const sg_bsp_entity_semantic_t *semantic = &fixture->entities[entity];
	sg_rune_canonical_order_input_t input;
	sg_rune_order_key_t order;
	sg_rune_mechanism_ref_t reference;

	memset(&input, 0, sizeof(input));
	input.domain = SG_RUNE_ORDER_MECHANISM;
	input.source_index = semantic->source_entity_ordinal;
	input.canonical_ordinal = semantic->canonical_ordinal;
	input.variant = (uint32_t)semantic->mechanism_kind;
	input.source_set_identity = fixture->authority.identity.source_set_identity;
	input.source_set_count = ENTITY_COUNT;
	input.source_set_complete = 1U;
	CHECK(SG_RuneModelOrderKeyDerive(&input, &order) ==
		SG_RUNE_ORDER_DERIVATION_OK);
	reference.value = SG_RuneModelStableIdFromOrderKey(&order);
	return reference;
}

static void SetPhase(mechanism_fixture_t *fixture, uint32_t phase_index,
	uint32_t mover_entity)
{
	sg_rune_phase_basis_t *phase = &fixture->phases[phase_index];

	memset(phase, 0, sizeof(*phase));
	phase->order.source_set_identity =
		fixture->authority.identity.source_set_identity;
	phase->order.domain = SG_RUNE_ORDER_PHASE;
	phase->order.source_index = phase_index;
	phase->order.local_ordinal = phase_index;
	phase->order.variant = 0U;
	phase->id.value = SG_RuneModelStableIdFromOrderKey(&phase->order);
	phase->stance = SG_RUNE_STANCE_STANDING;
	phase->motion = SG_RUNE_MOTION_SUPPORTED;
	phase->support = mover_entity == UINT32_MAX ? SG_RUNE_SUPPORT_SUPPORTED :
		SG_RUNE_SUPPORT_MOVER;
	phase->medium = SG_RUNE_MEDIUM_DRY;
	phase->void_relation = SG_RUNE_VOID_CLEAR;
	phase->reference_frame = mover_entity == UINT32_MAX ?
		SG_RUNE_FRAME_WORLD : SG_RUNE_FRAME_MOVER_RELATIVE;
	phase->mover = mover_entity == UINT32_MAX ? SG_RUNE_MECHANISM_REF_NONE :
		MechanismRef(fixture, mover_entity);
	phase->time_quantum_ms = 10U;
	phase->time_horizon_ms = 1000U;
}

static int RegionContains(const mechanism_fixture_t *fixture,
	uint32_t region_index, const float point[3], sg_rune_stance_t stance)
{
	const sg_configuration_semantic_region_t *region =
		&fixture->configuration_semantics->regions[region_index];
	uint32_t face;

	if (fixture->configuration->cells[region->cell].stance !=
		stance)
		return 0;
	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *plane =
			&fixture->configuration_semantics->faces[face];
		float dot = point[0] * plane->normal[0] +
			point[1] * plane->normal[1] + point[2] * plane->normal[2];

		if (dot > plane->distance)
			return 0;
	}
	return 1;
}

static uint32_t FindRegionForStance(const mechanism_fixture_t *fixture,
	const float point[3], sg_rune_stance_t stance)
{
	uint32_t region;

	for (region = 0U;
		region < fixture->configuration_semantics->region_count; region++)
		if (RegionContains(fixture, region, point, stance))
			return region;
	return UINT32_MAX;
}

static uint32_t FindRegion(const mechanism_fixture_t *fixture,
	const float point[3])
{
	return FindRegionForStance(fixture, point, SG_RUNE_STANCE_STANDING);
}

static int Traverses(sg_mechanism_capability_kind_t kind)
{
	return kind == SG_MECHANISM_CAPABILITY_DOOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
		kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE ||
		kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_PUSH ||
		kind == SG_MECHANISM_CAPABILITY_TELEPORT;
}

static void SetTimeline(sg_mechanism_host_trace_t *trace,
	uint32_t delay_ms, uint32_t dwell_ms, uint32_t travel_ms,
	uint32_t wait_ms, uint32_t reset_ms)
{
	trace->active_time_ms = trace->activation_time_ms +
		(uint64_t)delay_ms;
	trace->exit_time_ms = trace->active_time_ms +
		(uint64_t)travel_ms + (uint64_t)dwell_ms;
	trace->reset_time_ms = trace->exit_time_ms +
		(uint64_t)wait_ms + (uint64_t)reset_ms;
}

static void SetExecutionState(sg_mech_execution_state_t *state,
	uint16_t controller_kind, uint16_t node_kind)
{
	memset(state, 0, sizeof(*state));
	state->controller_kind = controller_kind;
	state->node_kind = node_kind;
	state->think_role = SG_MECH_EXEC_THINK_SEALED;
	state->end_role = SG_MECH_EXEC_END_NONE;
	state->motion_state = SG_MECH_MOTION_AT_ORIGIN;
	state->fixed_callbacks_match = 1;
	state->touch_matches = 1;
	state->stopped = 1;
}

static uint16_t DoorController(const mechanism_fixture_t *fixture,
	uint32_t controller, uint32_t mechanism)
{
	if (controller == mechanism)
		return SG_MECHANISM_CONTROLLER_AUTO_DOOR;
	if (fixture->entities[controller].mechanism_role == SG_MECH_NODE_TRIGGER)
		return SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	return SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
}

static void SetExecutionTransition(const mechanism_fixture_t *fixture,
	sg_mechanism_host_trace_t *trace)
{
	uint16_t controller = SG_MECHANISM_CONTROLLER_NONE;
	uint16_t node = SG_MECH_NODE_OTHER_MOVER;

	switch (trace->kind)
	{
	case SG_MECHANISM_CAPABILITY_DOOR_CROSSING:
	case SG_MECHANISM_CAPABILITY_DWELL:
	case SG_MECHANISM_CAPABILITY_RESET:
		controller = DoorController(fixture, trace->controller_entity,
			trace->mechanism_entity);
		node = SG_MECH_NODE_DOOR_MASTER;
		break;
	case SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION:
		controller = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
		node = SG_MECH_NODE_BUTTON;
		break;
	case SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION:
		controller = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
		node = SG_MECH_NODE_TRIGGER;
		break;
	case SG_MECHANISM_CAPABILITY_LIFT_RIDE:
		controller = SG_MECHANISM_CONTROLLER_PLATFORM;
		node = SG_MECH_NODE_PLATFORM;
		break;
	case SG_MECHANISM_CAPABILITY_TRAIN_RIDE:
		controller = SG_MECHANISM_CONTROLLER_TRAIN;
		node = SG_MECH_NODE_TRAIN;
		break;
	case SG_MECHANISM_CAPABILITY_PUSH:
		controller = SG_MECHANISM_CONTROLLER_PUSH;
		node = SG_MECH_NODE_PUSH;
		break;
	case SG_MECHANISM_CAPABILITY_TELEPORT:
		controller = SG_MECHANISM_CONTROLLER_TELEPORT;
		node = SG_MECH_NODE_TELEPORTER;
		break;
	case SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE:
		node = SG_MECH_NODE_AREAPORTAL;
		break;
	case SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING:
		node = SG_MECH_NODE_OTHER_MOVER;
		break;
	case SG_MECHANISM_CAPABILITY_KIND_COUNT:
		break;
	}
	SetExecutionState(&trace->source_execution, controller, node);
	SetExecutionState(&trace->destination_execution, controller, node);
	if (trace->kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE)
	{
		trace->source_execution.platform_profile =
			SG_MECH_PLATFORM_PROFILE_STOCK;
		trace->destination_execution.platform_profile =
			SG_MECH_PLATFORM_PROFILE_STOCK;
	}
	switch (trace->kind)
	{
	case SG_MECHANISM_CAPABILITY_DOOR_CROSSING:
		trace->destination_execution.think_role =
			SG_MECH_EXEC_THINK_LINEAR_DONE;
		trace->destination_execution.end_role =
			SG_MECH_EXEC_END_DOOR_DESTINATION;
		trace->destination_execution.motion_state =
			SG_MECH_MOTION_AT_DESTINATION;
		break;
	case SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION:
		trace->destination_execution.think_role =
			SG_MECH_EXEC_THINK_LINEAR_DONE;
		trace->destination_execution.end_role =
			SG_MECH_EXEC_END_BUTTON_DESTINATION;
		trace->destination_execution.motion_state =
			SG_MECH_MOTION_AT_DESTINATION;
		break;
	case SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION:
		trace->destination_execution.think_role =
			SG_MECH_EXEC_THINK_MULTI_WAIT;
		trace->destination_execution.nextthink_pending = 1;
		break;
	case SG_MECHANISM_CAPABILITY_DWELL:
		trace->source_execution.think_role = SG_MECH_EXEC_THINK_LINEAR_DONE;
		trace->source_execution.end_role =
			SG_MECH_EXEC_END_DOOR_DESTINATION;
		trace->source_execution.motion_state =
			SG_MECH_MOTION_AT_DESTINATION;
		trace->destination_execution.think_role =
			SG_MECH_EXEC_THINK_DOOR_RETURN;
		trace->destination_execution.end_role =
			SG_MECH_EXEC_END_DOOR_DESTINATION;
		trace->destination_execution.motion_state =
			SG_MECH_MOTION_AT_DESTINATION;
		trace->destination_execution.nextthink_pending = 1;
		break;
	case SG_MECHANISM_CAPABILITY_RESET:
		trace->source_execution.think_role = SG_MECH_EXEC_THINK_LINEAR_BEGIN;
		trace->source_execution.end_role = SG_MECH_EXEC_END_DOOR_ORIGIN;
		trace->source_execution.motion_state = SG_MECH_MOTION_TO_ORIGIN;
		trace->source_execution.stopped = 0;
		trace->destination_execution.think_role =
			SG_MECH_EXEC_THINK_LINEAR_DONE;
		trace->destination_execution.end_role = SG_MECH_EXEC_END_DOOR_ORIGIN;
		break;
	case SG_MECHANISM_CAPABILITY_LIFT_RIDE:
		trace->destination_execution.think_role =
			SG_MECH_EXEC_THINK_LINEAR_DONE;
		trace->destination_execution.end_role =
			SG_MECH_EXEC_END_PLATFORM_DESTINATION;
		trace->destination_execution.motion_state =
			SG_MECH_MOTION_AT_DESTINATION;
		break;
	case SG_MECHANISM_CAPABILITY_TRAIN_RIDE:
	case SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING:
	case SG_MECHANISM_CAPABILITY_PUSH:
	case SG_MECHANISM_CAPABILITY_TELEPORT:
	case SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE:
	case SG_MECHANISM_CAPABILITY_KIND_COUNT:
		break;
	}
}

static void SetTrace(mechanism_fixture_t *fixture, uint32_t index,
	uint32_t controller, uint32_t mechanism,
	sg_mechanism_capability_kind_t kind,
	sg_mechanism_activation_t activation)
{
	sg_mechanism_host_trace_t *trace = &fixture->traces[index];
	sg_mechanism_capability_candidate_t *candidate =
		&fixture->candidates[index];
	uint32_t delay_ms = 0U;
	uint32_t dwell_ms = 0U;
	uint32_t travel_ms = Traverses(kind) ? 100U : 0U;

	memset(trace, 0, sizeof(*trace));
	trace->candidate_identity = UINT64_C(100) + index;
	trace->trace_identity = UINT64_C(1000) + index;
	trace->source_set_identity =
		fixture->authority.identity.source_set_identity;
	trace->bsp_content_id = fixture->authority.identity.bsp_content_id;
	trace->physics_abi_id = fixture->authority.identity.physics_abi_id;
	trace->controller_entity = controller;
	trace->mechanism_entity = mechanism;
	trace->source_region = FindRegion(fixture,
		(const float[3]){ -25.0f, 0.0f, 0.0f });
	trace->destination_region = FindRegion(fixture,
		(const float[3]){ 25.0f, 0.0f, 0.0f });
	trace->source_phase = 0U;
	trace->destination_phase = 0U;
	trace->kind = kind;
	trace->source_state = SG_MECHANISM_STATE_INACTIVE;
	trace->destination_state = SG_MECHANISM_STATE_ACTIVE;
	trace->activation = activation;
	trace->recovery = SG_MECHANISM_RECOVERY_NONE;
	trace->entry_witness.value[0] = -25.0f;
	trace->exit_witness.value[0] = 25.0f;
	trace->observed_displacement.value[0] = 50.0f;
	trace->observed_velocity.value[0] = 100.0f;
	trace->inactive_scene.instances = &fixture->inactive_instance;
	trace->inactive_scene.instance_count = 1U;
	trace->active_scene.instances = &fixture->active_instance;
	trace->active_scene.instance_count = 1U;
	if (fixture->entities[mechanism].flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL)
		trace->mechanism_instance_id = UINT64_C(77);
	if (kind == SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION ||
		kind == SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION)
		trace->destination_state = SG_MECHANISM_STATE_ACTIVATING;
	if (kind == SG_MECHANISM_CAPABILITY_DWELL)
	{
		trace->source_state = SG_MECHANISM_STATE_ACTIVE;
		trace->destination_state = SG_MECHANISM_STATE_DWELLING;
		dwell_ms = 300U;
		trace->recovery = SG_MECHANISM_RECOVERY_WAIT_FOR_CYCLE;
	}
	if (kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
		kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE ||
		kind == SG_MECHANISM_CAPABILITY_PUSH ||
		kind == SG_MECHANISM_CAPABILITY_TELEPORT)
	{
		trace->source_state = SG_MECHANISM_STATE_ACTIVE;
		trace->destination_state = SG_MECHANISM_STATE_ACTIVE;
	}
	if ((fixture->entities[controller].flags &
		SG_BSP_ENTITY_DELAY_DEFINED) != 0U)
		delay_ms = (uint32_t)fixture->entities[controller].delay_ms;
	if ((fixture->entities[controller].flags &
		SG_BSP_ENTITY_DWELL_DEFINED) != 0U &&
		fixture->entities[controller].dwell_ms >= 0.0f)
		dwell_ms = (uint32_t)fixture->entities[controller].dwell_ms;
	SetExecutionTransition(fixture, trace);
	trace->activation_time_ms = UINT64_C(1000) + (uint64_t)index *
		UINT64_C(10000);
	SetTimeline(trace, delay_ms, dwell_ms, travel_ms, 0U, 0U);
	candidate->candidate_identity = trace->candidate_identity;
	candidate->source_set_identity = trace->source_set_identity;
	candidate->controller_entity = trace->controller_entity;
	candidate->mechanism_entity = trace->mechanism_entity;
	candidate->source_region = trace->source_region;
	candidate->destination_region = trace->destination_region;
	candidate->source_phase = trace->source_phase;
	candidate->destination_phase = trace->destination_phase;
	candidate->kind = trace->kind;
	candidate->source_state = trace->source_state;
	candidate->destination_state = trace->destination_state;
	candidate->activation = trace->activation;
	candidate->recovery = trace->recovery;
}

static void SetMoverTrace(mechanism_fixture_t *fixture, uint32_t trace_index,
	uint32_t phase_index)
{
	sg_mechanism_host_trace_t *trace = &fixture->traces[trace_index];
	sg_mechanism_capability_candidate_t *candidate =
		&fixture->candidates[trace_index];

	trace->source_phase = phase_index;
	trace->destination_phase = phase_index;
	candidate->source_phase = phase_index;
	candidate->destination_phase = phase_index;
}

static int FixtureInit(mechanism_fixture_t *fixture)
{
	static const sg_rune_mechanism_kind_t kinds[8] = {
		SG_RUNE_MECHANISM_DOOR, SG_RUNE_MECHANISM_BUTTON,
		SG_RUNE_MECHANISM_TRIGGER, SG_RUNE_MECHANISM_LIFT,
		SG_RUNE_MECHANISM_TRAIN, SG_RUNE_MECHANISM_ROTATOR,
		SG_RUNE_MECHANISM_PUSH, SG_RUNE_MECHANISM_TELEPORT
	};
	sg_rune_model_identity_t identity = MakeIdentity(800.0f);
	sg_host_collision_error_t host_error;
	sg_configuration_error_t configuration_error;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t semantics_limits;
	uint32_t entity;

	memset(fixture, 0, sizeof(*fixture));
	if (!SG_MechanismCapabilityOwnerCreate(&fixture->capability_owner))
		return 0;
	InitWorld(fixture);
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world, &identity,
			&host_error) ||
		!SG_ConfigurationBuild(&fixture->authority, NULL,
			&fixture->configuration, &configuration_error))
	{
		fprintf(stderr, "fixture configuration construction failed\n");
		exit(1);
	}
	SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture->authority,
		fixture->configuration, &semantics_limits,
		&fixture->configuration_semantics, &semantics_error))
	{
		SG_ConfigurationDestroy(fixture->configuration);
		fixture->configuration = NULL;
		fprintf(stderr, "fixture semantic construction failed\n");
		exit(1);
	}
	fixture->entity_semantics.source_set_identity =
		fixture->authority.identity.source_set_identity;
	fixture->entity_semantics.entities = fixture->entities;
	fixture->entity_semantics.entity_count = ENTITY_COUNT;
	fixture->entity_semantics.edges = fixture->edges;
	fixture->entity_semantics.edge_count = 3U;
	for (entity = 0U; entity < ENTITY_COUNT; entity++)
	{
		fixture->entities[entity].source_set_identity =
			fixture->authority.identity.source_set_identity;
		fixture->entities[entity].source_entity_ordinal = entity;
		fixture->entities[entity].canonical_ordinal = entity;
		fixture->entities[entity].flags = SG_BSP_ENTITY_HAS_MECHANISM |
			SG_BSP_ENTITY_USE_ACTIVATED;
		fixture->entities[entity].bsp_model = SG_BSP_ENTITY_MODEL_NONE;
		if (entity < 8U)
		{
			fixture->entities[entity].flags |=
				SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND;
			fixture->entities[entity].mechanism_kind = kinds[entity];
		}
	}
	fixture->entities[0].flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED |
		SG_BSP_ENTITY_DWELL_DEFINED;
	fixture->entities[0].dwell_ms = 300.0f;
	for (entity = 0U; entity < 8U; entity++)
	{
		fixture->entities[entity].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
		fixture->entities[entity].bsp_model = 1U;
	}
	fixture->entities[1].mechanism_role = SG_MECH_NODE_BUTTON;
	fixture->entities[2].mechanism_role = SG_MECH_NODE_TRIGGER;
	fixture->entities[2].flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED |
		SG_BSP_ENTITY_DELAY_DEFINED | SG_BSP_ENTITY_DWELL_DEFINED;
	fixture->entities[2].delay_ms = 250.0f;
	fixture->entities[2].dwell_ms = 500.0f;
	fixture->entities[8].mechanism_role = SG_MECH_NODE_AREAPORTAL;
	fixture->entities[5].move_angles.value[2] = 90.0f;
	fixture->entities[6].move_direction.value[0] = 1.0f;
	fixture->entities[9].flags |= SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND |
		SG_BSP_ENTITY_TOUCH_ACTIVATED | SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture->entities[9].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
	fixture->entities[9].bsp_model = 1U;
	fixture->edges[0].source = 1U;
	fixture->edges[0].destination = 0U;
	fixture->edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[0].fanout_ordinal = 0U;
	fixture->edges[1].source = 2U;
	fixture->edges[1].destination = 0U;
	fixture->edges[1].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[1].fanout_ordinal = 0U;
	fixture->edges[2].source = 0U;
	fixture->edges[2].destination = 9U;
	fixture->edges[2].kind = SG_MECH_EDGE_TEAM;
	fixture->edges[2].fanout_ordinal = 0U;
	fixture->inactive_instance.instance_id = UINT64_C(77);
	fixture->inactive_instance.model_index = 1U;
	fixture->active_instance.instance_id = UINT64_C(77);
	fixture->active_instance.model_index = 1U;
	fixture->active_instance.transform.origin[0] = 50.0f;
	fixture->active_instance.transform.angles[2] = 90.0f;
	SetPhase(fixture, 0U, UINT32_MAX);
	SetPhase(fixture, 1U, 3U);
	SetPhase(fixture, 2U, 4U);
	SetTrace(fixture, 0U, 0U, 0U,
		SG_MECHANISM_CAPABILITY_DOOR_CROSSING,
		SG_MECHANISM_ACTIVATION_TOUCH);
	SetTrace(fixture, 1U, 1U, 1U,
		SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION,
		SG_MECHANISM_ACTIVATION_USE);
	SetTrace(fixture, 2U, 2U, 2U,
		SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION,
		SG_MECHANISM_ACTIVATION_TOUCH);
	SetTrace(fixture, 3U, 1U, 0U, SG_MECHANISM_CAPABILITY_DWELL,
		SG_MECHANISM_ACTIVATION_USE);
	SetTrace(fixture, 4U, 3U, 3U, SG_MECHANISM_CAPABILITY_LIFT_RIDE,
		SG_MECHANISM_ACTIVATION_USE);
	SetMoverTrace(fixture, 4U, 1U);
	SetTrace(fixture, 5U, 4U, 4U, SG_MECHANISM_CAPABILITY_TRAIN_RIDE,
		SG_MECHANISM_ACTIVATION_USE);
	SetMoverTrace(fixture, 5U, 2U);
	SetTrace(fixture, 6U, 5U, 5U,
		SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING,
		SG_MECHANISM_ACTIVATION_USE);
	SetTrace(fixture, 7U, 6U, 6U, SG_MECHANISM_CAPABILITY_PUSH,
		SG_MECHANISM_ACTIVATION_USE);
	SetTrace(fixture, 8U, 7U, 7U, SG_MECHANISM_CAPABILITY_TELEPORT,
		SG_MECHANISM_ACTIVATION_USE);
	SetTrace(fixture, 9U, 8U, 8U,
		SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE,
		SG_MECHANISM_ACTIVATION_USE);
	SetTrace(fixture, 10U, 1U, 0U, SG_MECHANISM_CAPABILITY_RESET,
		SG_MECHANISM_ACTIVATION_USE);
	fixture->traces[10].recovery = SG_MECHANISM_RECOVERY_WAIT_FOR_RESET;
	fixture->traces[10].source_state = SG_MECHANISM_STATE_RETURNING;
	fixture->traces[10].destination_state = SG_MECHANISM_STATE_RESET;
	fixture->candidates[10].recovery =
		SG_MECHANISM_RECOVERY_WAIT_FOR_RESET;
	fixture->candidates[10].source_state = SG_MECHANISM_STATE_RETURNING;
	fixture->candidates[10].destination_state = SG_MECHANISM_STATE_RESET;
	SetTimeline(&fixture->traces[10], 0U, 0U, 0U, 300U, 700U);
	SetTrace(fixture, 11U, 2U, 0U,
		SG_MECHANISM_CAPABILITY_DOOR_CROSSING,
		SG_MECHANISM_ACTIVATION_TOUCH);
	SetTrace(fixture, 12U, 0U, 9U,
		SG_MECHANISM_CAPABILITY_DOOR_CROSSING,
		SG_MECHANISM_ACTIVATION_TOUCH);
	fixture->catalog.identity = fixture->authority.identity;
	fixture->catalog.candidates = fixture->candidates;
	fixture->catalog.candidate_count = TRACE_COUNT;
	fixture->catalog.traces = fixture->traces;
	fixture->catalog.trace_count = TRACE_COUNT;
	fixture->catalog.candidate_verifier_identity = UINT64_C(0xcafe);
	fixture->catalog.trace_verifier_identity = UINT64_C(0xbeef);
	fixture->source.authority = &fixture->authority;
	fixture->source.configuration = fixture->configuration;
	fixture->source.configuration_semantics =
		fixture->configuration_semantics;
	fixture->source.entity_semantics = &fixture->entity_semantics;
	fixture->source.phases = fixture->phases;
	fixture->source.phase_count = PHASE_COUNT;
	fixture->source.host_traces = &fixture->catalog;
	if (fixture->traces[0].source_region == UINT32_MAX ||
		fixture->traces[0].destination_region == UINT32_MAX)
	{
		FixtureDestroy(fixture);
		fprintf(stderr, "fixture regions were not reconstructed\n");
		exit(1);
	}
	return 1;
}

static void FixtureDestroy(mechanism_fixture_t *fixture)
{
	SG_MechanismCapabilityOwnerDestroy(fixture->capability_owner);
	SG_ConfigurationSemanticsDestroy(fixture->configuration_semantics);
	SG_ConfigurationDestroy(fixture->configuration);
	fixture->configuration_semantics = NULL;
	fixture->configuration = NULL;
	fixture->capability_owner = NULL;
}

static int Build(mechanism_fixture_t *fixture,
	sg_mechanism_capability_set_t **set,
	sg_mechanism_capability_error_t *error)
{
	return SG_MechanismCapabilityBuild(fixture->capability_owner,
		&fixture->source, set, error);
}

static void DestroySet(mechanism_fixture_t *fixture,
	sg_mechanism_capability_set_t *set)
{
	SG_MechanismCapabilityDestroy(fixture->capability_owner, set);
}

static void ExpectFailure(mechanism_fixture_t *fixture,
	sg_mechanism_capability_error_code_t expected)
{
	sg_mechanism_capability_set_t *set =
		(sg_mechanism_capability_set_t *)(uintptr_t)UINT32_C(1);
	sg_mechanism_capability_error_t error;

	CHECK(!Build(fixture, &set, &error));
	CHECK(set == NULL);
	if (error.code != expected)
		fprintf(stderr, "expected error %d, got %d at %u\n", (int)expected,
			(int)error.code, error.source_index);
	CHECK(error.code == expected);
}

static void TestCompleteModel(void)
{
	mechanism_fixture_t fixture;
	mechanism_fixture_t snapshot;
	mechanism_fixture_t reordered;
	sg_mechanism_capability_set_t *first = NULL;
	sg_mechanism_capability_set_t *second = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	uint32_t kind_mask = 0U;
	uint32_t index;
	uint32_t shared_facts = 0U;
	uint32_t shared_first = UINT32_MAX;
	uint32_t shared_count = UINT32_MAX;
	uint32_t shared_relations = 0U;

	CHECK(FixtureInit(&fixture));
	if (!fixture.configuration)
		return;
	snapshot = fixture;
	CHECK(Build(&fixture, &first, &error));
	if (!first)
		fprintf(stderr, "complete build error %d at %u\n", (int)error.code,
			error.source_index);
	CHECK(error.code == SG_MECHANISM_CAPABILITY_ERROR_NONE);
	CHECK(first != NULL && CAPABILITY(fixture, first)->fact_count == TRACE_COUNT);
	if (!first)
	{
		FixtureDestroy(&fixture);
		return;
	}
	CHECK(CAPABILITY(fixture, first)->topology_edge_count == 3U);
	CHECK(CAPABILITY(fixture, first)->topology_edge_visits == 10U);
	CHECK(CAPABILITY(fixture, first)->topology_relation_count == 12U);
	CHECK(CAPABILITY(fixture, first)->candidate_verifier_identity == UINT64_C(0xcafe));
	CHECK(CAPABILITY(fixture, first)->trace_verifier_identity == UINT64_C(0xbeef));
	for (index = 0U; index < CAPABILITY(fixture, first)->fact_count; index++)
	{
		kind_mask |= UINT32_C(1) << (uint32_t)CAPABILITY(fixture, first)->facts[index].kind;
		CHECK(CAPABILITY(fixture, first)->facts[index].order == index);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.gravity == 800.0f);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.fixed_latency_ms ==
			CAPABILITY(fixture, first)->facts[index].delay_ms);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.dwell_ms ==
			CAPABILITY(fixture, first)->facts[index].dwell_ms);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.duration_ms ==
			CAPABILITY(fixture, first)->facts[index].travel_ms);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.wait_ms ==
			CAPABILITY(fixture, first)->facts[index].wait_ms);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.reset_ms ==
			CAPABILITY(fixture, first)->facts[index].reset_ms);
		CHECK(CAPABILITY(fixture, first)->facts[index].parameters.total_ms ==
			(uint64_t)CAPABILITY(fixture, first)->facts[index].delay_ms +
			(uint64_t)CAPABILITY(fixture, first)->facts[index].dwell_ms +
			(uint64_t)CAPABILITY(fixture, first)->facts[index].travel_ms +
			(uint64_t)CAPABILITY(fixture, first)->facts[index].wait_ms +
			(uint64_t)CAPABILITY(fixture, first)->facts[index].reset_ms);
		if (CAPABILITY(fixture, first)->facts[index].kind == SG_MECHANISM_CAPABILITY_PUSH)
			CHECK(CAPABILITY(fixture, first)->facts[index].mechanism_direction.value[0] == 1.0f);
		if (CAPABILITY(fixture, first)->facts[index].kind ==
			SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING)
			CHECK(CAPABILITY(fixture, first)->facts[index].mechanism_angles.value[2] == 90.0f);
		if (CAPABILITY(fixture, first)->facts[index].controller_entity == 1U &&
			CAPABILITY(fixture, first)->facts[index].mechanism_entity == 0U)
		{
			if (shared_facts == 0U)
			{
				shared_first = CAPABILITY(fixture, first)->facts[index].first_topology_edge;
				shared_count = CAPABILITY(fixture, first)->facts[index].topology_edge_count;
			}
			CHECK(CAPABILITY(fixture, first)->facts[index].first_topology_edge == shared_first);
			CHECK(CAPABILITY(fixture, first)->facts[index].topology_edge_count == shared_count);
			shared_facts++;
		}
	}
	for (index = 0U; index < CAPABILITY(fixture, first)->topology_relation_count; index++)
		if (CAPABILITY(fixture, first)->topology_relations[index].controller_entity == 1U &&
			CAPABILITY(fixture, first)->topology_relations[index].mechanism_entity == 0U)
			shared_relations++;
	CHECK(shared_facts == 2U);
	CHECK(shared_relations == 1U);
	CHECK(kind_mask == (UINT32_C(1) << SG_MECHANISM_CAPABILITY_KIND_COUNT) -
		UINT32_C(1));
	CHECK(SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, first, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_OK);
	CHECK(audit.proved_facts == TRACE_COUNT);
	CHECK(audit.lookup_comparisons <= (uint64_t)TRACE_COUNT * UINT64_C(6));
	CHECK(memcmp(&fixture, &snapshot, sizeof(fixture)) == 0);
	CHECK(FixtureInit(&reordered));
	if (!reordered.configuration)
	{
		DestroySet(&fixture, first);
		FixtureDestroy(&fixture);
		return;
	}
	for (index = 0U; index < TRACE_COUNT; index++)
	{
		reordered.traces[index] = fixture.traces[TRACE_COUNT - 1U - index];
		reordered.traces[index].inactive_scene.instances =
			&reordered.inactive_instance;
		reordered.traces[index].active_scene.instances =
			&reordered.active_instance;
		reordered.candidates[index] =
			fixture.candidates[TRACE_COUNT - 1U - index];
	}
	reordered.catalog.traces = reordered.traces;
	reordered.catalog.candidates = reordered.candidates;
	CHECK(Build(&reordered, &second, &error));
	CHECK(second != NULL && CAPABILITY(reordered, second)->fact_count == CAPABILITY(fixture, first)->fact_count);
	if (second)
	{
		CHECK(memcmp(CAPABILITY(reordered, second)->facts, CAPABILITY(fixture, first)->facts,
			(size_t)CAPABILITY(fixture, first)->fact_count * sizeof(*CAPABILITY(fixture, first)->facts)) == 0);
		CHECK(memcmp(CAPABILITY(reordered, second)->topology_edges, CAPABILITY(fixture, first)->topology_edges,
			(size_t)CAPABILITY(fixture, first)->topology_edge_count *
				sizeof(*CAPABILITY(fixture, first)->topology_edges)) == 0);
		CHECK(memcmp(CAPABILITY(reordered, second)->topology_relations, CAPABILITY(fixture, first)->topology_relations,
			(size_t)CAPABILITY(fixture, first)->topology_relation_count *
				sizeof(*CAPABILITY(fixture, first)->topology_relations)) == 0);
	}
	DestroySet(&fixture, first);
	DestroySet(&reordered, second);
	FixtureDestroy(&reordered);
	FixtureDestroy(&fixture);
}

static void TestIdentityAndLimits(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_error_t error;

	CHECK(FixtureInit(&fixture));
	CHECK(!SG_MechanismCapabilityBuild(fixture.capability_owner, &fixture.source, NULL, &error));
	CHECK(error.code == SG_MECHANISM_CAPABILITY_ERROR_INVALID_ARGUMENT);
	CHECK(error.source_index == SG_MECHANISM_CAPABILITY_INDEX_NONE);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.authority.identity.schema_id = 0U;
	fixture.configuration->identity.schema_id = 0U;
	fixture.configuration_semantics->identity.schema_id = 0U;
	fixture.catalog.identity.schema_id = 0U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.authority.identity.physics.gravity = NAN;
	fixture.configuration->identity.physics.gravity = NAN;
	fixture.configuration_semantics->identity.physics.gravity = NAN;
	fixture.catalog.identity.physics.gravity = NAN;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.authority.identity.standing_hull.maxs.value[0] = -16.0f;
	fixture.configuration->identity = fixture.authority.identity;
	fixture.configuration_semantics->identity = fixture.authority.identity;
	fixture.catalog.identity = fixture.authority.identity;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entity_semantics.entity_count = UINT32_MAX;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW);
	FixtureDestroy(&fixture);
}

static void TestAuthenticatedEvidence(void)
{
	mechanism_fixture_t fixture;

	CHECK(FixtureInit(&fixture));
	fixture.catalog.trace_verifier_identity =
		fixture.catalog.candidate_verifier_identity;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_ARGUMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[0].candidate_identity = UINT64_C(99999);
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.candidates[1].candidate_identity =
		fixture.candidates[0].candidate_identity;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.candidates[0].destination_region = UINT32_MAX;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.inactive_instance.transform.origin[0] = 50.0f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.active_instance.transform.origin[0] = 0.0f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[0].mechanism_instance_id = UINT64_C(88);
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[0].exit_witness.value[0] = 50.0f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[0].observed_displacement.value[0] = 9.0f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[2].destination_execution.nextthink_pending = 0;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
}

static void TestTopologyTimingPhaseAndCompleteness(void)
{
	mechanism_fixture_t fixture;

	CHECK(FixtureInit(&fixture));
	fixture.edges[3] = fixture.edges[0];
	fixture.entity_semantics.edge_count = 4U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.edges[3].source = 0U;
	fixture.edges[3].destination = 1U;
	fixture.edges[3].kind = SG_MECH_EDGE_OWNER;
	fixture.edges[3].fanout_ordinal = 0U;
	fixture.entity_semantics.edge_count = 4U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.edges[3].source = 1U;
	fixture.edges[3].destination = 2U;
	fixture.edges[3].kind = SG_MECH_EDGE_OWNER;
	fixture.edges[3].fanout_ordinal = 0U;
	fixture.entity_semantics.edge_count = 4U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[0].exit_time_ms = fixture.traces[0].active_time_ms - 1U;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[1].active_time_ms = fixture.traces[1].activation_time_ms +
		(uint64_t)UINT32_MAX + UINT64_C(1);
	fixture.traces[1].exit_time_ms = fixture.traces[1].active_time_ms;
	fixture.traces[1].reset_time_ms = fixture.traces[1].exit_time_ms;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[1].activation_time_ms = UINT64_MAX - UINT64_C(3);
	fixture.traces[1].active_time_ms = UINT64_C(2);
	fixture.traces[1].exit_time_ms = UINT64_C(2);
	fixture.traces[1].reset_time_ms = UINT64_C(2);
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[2].active_time_ms++;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[1].destination_state = SG_MECHANISM_STATE_DWELLING;
	fixture.candidates[1].destination_state = SG_MECHANISM_STATE_DWELLING;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[10].recovery = SG_MECHANISM_RECOVERY_NONE;
	fixture.candidates[10].recovery = SG_MECHANISM_RECOVERY_NONE;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[10].flags = SG_MECHANISM_HOST_TRACE_ONE_SHOT;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.phases[1].mover = MechanismRef(&fixture, 4U);
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.configuration->cell_count--;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INCOMPLETE_CONFIGURATION);
	fixture.configuration->cell_count++;
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	{
		sg_bsp_world_t empty_world;

		memset(&empty_world, 0, sizeof(empty_world));
		fixture.authority.world = &empty_world;
		ExpectFailure(&fixture,
			SG_MECHANISM_CAPABILITY_ERROR_INCOMPLETE_CONFIGURATION);
	}
	FixtureDestroy(&fixture);
}

static void TestGravityAndAudit(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *set = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	uint32_t saved_edge;
	uint64_t saved_trace;
	uint64_t saved_verifier;
	uint32_t saved_region;
	float saved_active_origin;
	uint32_t saved_fact_timing;
	float saved_fact_transform;
	sg_mechanism_state_t saved_fact_state;
	int saved_callbacks_match;

	CHECK(FixtureInit(&fixture));
	CHECK(Build(&fixture, &set, &error));
	if (!set)
		fprintf(stderr, "audit build error %d at %u\n", (int)error.code,
			error.source_index);
	if (!set)
	{
		FixtureDestroy(&fixture);
		return;
	}
	CHECK(CAPABILITY(fixture, set)->facts[0].parameters.gravity == 800.0f);
	saved_trace = CAPABILITY(fixture, set)->facts[0].trace_identity;
	CAPABILITY(fixture, set)->facts[0].trace_identity++;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_OMITTED_FACT ||
		audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	CAPABILITY(fixture, set)->facts[0].trace_identity = saved_trace;
	saved_edge = CAPABILITY(fixture, set)->topology_edges[0];
	CAPABILITY(fixture, set)->topology_edges[0] = 3U;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT);
	CAPABILITY(fixture, set)->topology_edges[0] = saved_edge;
	saved_verifier = CAPABILITY(fixture, set)->candidate_verifier_identity;
	CAPABILITY(fixture, set)->candidate_verifier_identity++;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
	CAPABILITY(fixture, set)->candidate_verifier_identity = saved_verifier;
	saved_fact_timing = CAPABILITY(fixture, set)->facts[0].dwell_ms;
	CAPABILITY(fixture, set)->facts[0].dwell_ms++;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	CAPABILITY(fixture, set)->facts[0].dwell_ms = saved_fact_timing;
	saved_fact_state = CAPABILITY(fixture, set)->facts[0].destination_state;
	CAPABILITY(fixture, set)->facts[0].destination_state = SG_MECHANISM_STATE_RESET;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	CAPABILITY(fixture, set)->facts[0].destination_state = saved_fact_state;
	saved_fact_transform =
		CAPABILITY(fixture, set)->facts[0].active_mechanism_transform.origin[0];
	CAPABILITY(fixture, set)->facts[0].active_mechanism_transform.origin[0]++;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	CAPABILITY(fixture, set)->facts[0].active_mechanism_transform.origin[0] =
		saved_fact_transform;
	saved_region = fixture.candidates[0].destination_region;
	fixture.candidates[0].destination_region = UINT32_MAX;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	fixture.candidates[0].destination_region = saved_region;
	saved_active_origin = fixture.active_instance.transform.origin[0];
	fixture.active_instance.transform.origin[0] = 0.0f;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	fixture.active_instance.transform.origin[0] = saved_active_origin;
	saved_callbacks_match =
		fixture.traces[1].destination_execution.fixed_callbacks_match;
	fixture.traces[1].destination_execution.fixed_callbacks_match = 0;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	fixture.traces[1].destination_execution.fixed_callbacks_match =
		saved_callbacks_match;
	fixture.authority.identity.schema_id = 0U;
	fixture.configuration->identity.schema_id = 0U;
	fixture.configuration_semantics->identity.schema_id = 0U;
	fixture.catalog.identity.schema_id = 0U;
	CAPABILITY(fixture, set)->identity.schema_id = 0U;
	CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
	DestroySet(&fixture, set);
	FixtureDestroy(&fixture);
}

static void TestOneShotTransaction(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *set = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	const uint32_t traces[2] = { 2U, 11U };
	uint32_t index;

	CHECK(FixtureInit(&fixture));
	fixture.entities[2].dwell_ms = -1000.0f;
	for (index = 0U; index < 2U; index++)
	{
		fixture.traces[traces[index]].flags =
			SG_MECHANISM_HOST_TRACE_ONE_SHOT;
	}
	SetTimeline(&fixture.traces[2], 250U, 0U, 0U, 0U, 0U);
	SetTimeline(&fixture.traces[11], 250U, 0U, 100U, 0U, 0U);
	CHECK(Build(&fixture, &set, &error));
	CHECK(set != NULL);
	if (set)
	{
		CHECK(SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
		DestroySet(&fixture, set);
	}
	FixtureDestroy(&fixture);
}

static void TestEntityTimingValidation(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *set = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	const uint32_t exact_delay = UINT32_C(16777218);
	uint32_t index;
	uint32_t matched = 0U;

	CHECK(FixtureInit(&fixture));
	fixture.entities[2].delay_ms = 250.5f;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entities[0].dwell_ms = 300.5f;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entities[2].delay_ms = NAN;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entities[2].dwell_ms = NAN;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entities[2].delay_ms = 4294967296.0f;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entities[2].dwell_ms = -500.0f;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.entities[2].delay_ms = (float)exact_delay;
	SetTimeline(&fixture.traces[2], exact_delay, 500U, 0U, 0U, 0U);
	SetTimeline(&fixture.traces[11], exact_delay, 500U, 100U, 0U, 0U);
	CHECK(Build(&fixture, &set, &error));
	CHECK(set != NULL);
	if (set)
	{
		for (index = 0U; index < CAPABILITY(fixture, set)->fact_count; index++)
			if (CAPABILITY(fixture, set)->facts[index].controller_entity == 2U)
			{
				matched++;
				CHECK(CAPABILITY(fixture, set)->facts[index].delay_ms == exact_delay);
				CHECK(CAPABILITY(fixture, set)->facts[index].parameters.fixed_latency_ms ==
					exact_delay);
			}
		CHECK(matched == 2U);
		CHECK(SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
	}
	DestroySet(&fixture, set);
	FixtureDestroy(&fixture);
}

static void TestExactMillisecondTiming(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *set = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	const uint32_t exact_delta = UINT32_C(16777217);
	uint32_t index;
	uint32_t found = UINT32_MAX;

	CHECK(FixtureInit(&fixture));
	fixture.traces[1].activation_time_ms = UINT64_C(1000);
	fixture.traces[1].active_time_ms =
		fixture.traces[1].activation_time_ms + exact_delta;
	fixture.traces[1].exit_time_ms = fixture.traces[1].active_time_ms;
	fixture.traces[1].reset_time_ms = fixture.traces[1].exit_time_ms;
	CHECK(Build(&fixture, &set, &error));
	CHECK(set != NULL);
	if (set)
	{
		for (index = 0U; index < CAPABILITY(fixture, set)->fact_count; index++)
			if (CAPABILITY(fixture, set)->facts[index].trace_identity ==
				fixture.traces[1].trace_identity)
			{
				found = index;
				CHECK(CAPABILITY(fixture, set)->facts[index].delay_ms == exact_delta);
				CHECK(CAPABILITY(fixture, set)->facts[index].parameters.fixed_latency_ms ==
					exact_delta);
				CHECK(CAPABILITY(fixture, set)->facts[index].parameters.duration_ms == 0U);
				CHECK(CAPABILITY(fixture, set)->facts[index].parameters.total_ms == exact_delta);
			}
		CHECK(found != UINT32_MAX);
		CHECK(SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
		if (found != UINT32_MAX)
		{
			CAPABILITY(fixture, set)->facts[found].parameters.fixed_latency_ms--;
			CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
			CHECK(audit.code ==
				SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
			CAPABILITY(fixture, set)->facts[found].parameters.fixed_latency_ms++;
			fixture.traces[1].active_time_ms++;
			CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
			CHECK(audit.code ==
				SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
		}
	}
	DestroySet(&fixture, set);
	FixtureDestroy(&fixture);
}

static void TestPhaseStanceBinding(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *set = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	uint32_t crouching_region;

	CHECK(FixtureInit(&fixture));
	fixture.phases[0].stance = SG_RUNE_STANCE_CROUCHING;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	crouching_region = FindRegionForStance(&fixture,
		fixture.traces[0].exit_witness.value, SG_RUNE_STANCE_CROUCHING);
	CHECK(crouching_region != UINT32_MAX);
	if (crouching_region != UINT32_MAX)
	{
		fixture.traces[0].destination_region = crouching_region;
		fixture.candidates[0].destination_region = crouching_region;
		ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE);
	}
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	CHECK(Build(&fixture, &set, &error));
	CHECK(set != NULL);
	if (set)
	{
		fixture.phases[0].stance = SG_RUNE_STANCE_CROUCHING;
		CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
		CHECK(audit.code ==
			SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	}
	DestroySet(&fixture, set);
	FixtureDestroy(&fixture);
}

static void TestScheduledParameterValidation(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *set = NULL;
	sg_mechanism_capability_error_t error;
	sg_mechanism_capability_audit_result_t audit;
	sg_rune_interval_t saved_speed;

	CHECK(FixtureInit(&fixture));
	fixture.traces[1].observed_velocity.value[0] = FLT_MAX;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.traces[1].observed_velocity.value[0] = 1500.0f;
	fixture.traces[1].observed_velocity.value[1] = 1500.0f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	fixture.authority.identity.physics.max_velocity = FLT_MAX;
	fixture.configuration->identity.physics.max_velocity = FLT_MAX;
	fixture.configuration_semantics->identity.physics.max_velocity = FLT_MAX;
	fixture.catalog.identity.physics.max_velocity = FLT_MAX;
	fixture.traces[1].observed_velocity.value[0] = FLT_MAX;
	fixture.traces[1].observed_velocity.value[1] = FLT_MAX;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureDestroy(&fixture);
	CHECK(FixtureInit(&fixture));
	CHECK(Build(&fixture, &set, &error));
	CHECK(set != NULL);
	if (set)
	{
		saved_speed = CAPABILITY(fixture, set)->facts[0].parameters.speed;
		CAPABILITY(fixture, set)->facts[0].parameters.speed.min_value = INFINITY;
		CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
		CHECK(audit.code ==
			SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
		CAPABILITY(fixture, set)->facts[0].parameters.speed = saved_speed;
		CAPABILITY(fixture, set)->facts[0].parameters.speed.min_value =
			CAPABILITY(fixture, set)->facts[0].parameters.speed.max_value + 1.0f;
		CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
		CHECK(audit.code ==
			SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
		CAPABILITY(fixture, set)->facts[0].parameters.speed = saved_speed;
		fixture.traces[1].observed_velocity.value[0] = FLT_MAX;
		CHECK(!SG_MechanismCapabilityAudit(fixture.capability_owner, &fixture.source, set, &audit));
		CHECK(audit.code ==
			SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	}
	DestroySet(&fixture, set);
	FixtureDestroy(&fixture);
}

static void TestCanonicalIdentityIgnoresAbiPadding(void)
{
	sg_mechanism_capability_fact_t first;
	sg_mechanism_capability_fact_t second;
	sg_rune_model_identity_t first_identity = MakeIdentity(800.0f);
	sg_rune_model_identity_t second_identity = first_identity;
	unsigned char *bytes = (unsigned char *)&second;
	size_t index;

	memset(&first, 0, sizeof(first));
	second = first;
	CHECK(offsetof(sg_mechanism_capability_fact_t, trace_identity) >
		sizeof(first.order));
	for (index = sizeof(first.order);
		index < offsetof(sg_mechanism_capability_fact_t, trace_identity);
		index++)
		bytes[index] = UINT8_C(0xa5);
	CHECK(SG_MechanismCapabilityFactIdentity(&first) ==
		SG_MechanismCapabilityFactIdentity(&second));
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	first.entry_witness.value[0] = -0.0f;
	second.entry_witness.value[0] = 0.0f;
	CHECK(SG_MechanismCapabilityFactIdentity(&first) ==
		SG_MechanismCapabilityFactIdentity(&second));
	first_identity.physics.ground_acceleration = 0.0f;
	second_identity.physics.ground_acceleration = -0.0f;
	CHECK(SG_MechanismModelIdentityValue(&first_identity) ==
		SG_MechanismModelIdentityValue(&second_identity));
}

int main(void)
{
	TestCompleteModel();
	TestIdentityAndLimits();
	TestAuthenticatedEvidence();
	TestTopologyTimingPhaseAndCompleteness();
	TestGravityAndAudit();
	TestOneShotTransaction();
	TestEntityTimingValidation();
	TestExactMillisecondTiming();
	TestPhaseStanceBinding();
	TestScheduledParameterValidation();
	TestCanonicalIdentityIgnoresAbiPadding();
	if (failures != 0)
	{
		fprintf(stderr, "mechanism capability failures: %d\n", failures);
		return 1;
	}
	puts("mechanism capability checks passed");
	return 0;
}
