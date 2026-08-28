#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_mechanism_capability.h"

#define ENTITY_COUNT UINT32_C(10)
#define TRACE_COUNT UINT32_C(13)
#define PHASE_COUNT UINT32_C(3)

typedef struct mechanism_fixture_s
{
	sg_bsp_world_t world;
	sg_host_collision_authority_t authority;
	sg_configuration_cell_t cells[2];
	sg_configuration_space_t configuration;
	sg_configuration_semantic_region_t regions[2];
	sg_configuration_semantic_face_t faces[12];
	sg_configuration_semantics_t configuration_semantics;
	sg_bsp_entity_semantic_t entities[ENTITY_COUNT];
	sg_bsp_entity_semantic_edge_t edges[4];
	sg_bsp_entity_semantics_t entity_semantics;
	sg_bsp_completeness_result_t completeness;
	sg_rune_phase_basis_t phases[PHASE_COUNT];
	sg_mechanism_capability_candidate_t candidates[TRACE_COUNT];
	sg_mechanism_host_trace_t traces[TRACE_COUNT];
	sg_mechanism_host_trace_catalog_t catalog;
	sg_mechanism_capability_source_t source;
} mechanism_fixture_t;

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

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

static void SetCubeFaces(sg_configuration_semantic_face_t *faces,
	float minimum_x, float maximum_x)
{
	static const float normals[6][3] = {
		{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
	};
	uint32_t face;

	for (face = 0U; face < 6U; face++)
		memcpy(faces[face].normal, normals[face], sizeof(normals[face]));
	faces[0].distance = maximum_x;
	faces[1].distance = -minimum_x;
	faces[2].distance = 10.0f;
	faces[3].distance = 10.0f;
	faces[4].distance = 10.0f;
	faces[5].distance = 10.0f;
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

static int Conditional(sg_mechanism_capability_kind_t kind)
{
	return kind == SG_MECHANISM_CAPABILITY_DOOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE;
}

static void SetTrace(mechanism_fixture_t *fixture, uint32_t index,
	uint32_t controller, uint32_t mechanism,
	sg_mechanism_capability_kind_t kind,
	sg_mechanism_activation_t activation)
{
	sg_mechanism_host_trace_t *trace = &fixture->traces[index];
	sg_mechanism_capability_candidate_t *candidate =
		&fixture->candidates[index];

	memset(trace, 0, sizeof(*trace));
	trace->candidate_identity = UINT64_C(100) + index;
	trace->trace_identity = UINT64_C(1000) + index;
	trace->source_set_identity =
		fixture->authority.identity.source_set_identity;
	trace->bsp_content_id = fixture->authority.identity.bsp_content_id;
	trace->physics_abi_id = fixture->authority.identity.physics_abi_id;
	trace->controller_entity = controller;
	trace->mechanism_entity = mechanism;
	trace->source_region = 0U;
	trace->destination_region = 1U;
	trace->source_phase = 0U;
	trace->destination_phase = 0U;
	trace->kind = kind;
	trace->source_state = SG_MECHANISM_STATE_INACTIVE;
	trace->destination_state = SG_MECHANISM_STATE_ACTIVE;
	trace->activation = activation;
	trace->recovery = SG_MECHANISM_RECOVERY_NONE;
	trace->entry_witness.value[0] = -5.0f;
	trace->exit_witness.value[0] = 5.0f;
	trace->observed_displacement.value[0] = 10.0f;
	trace->observed_velocity.value[0] = 100.0f;
	if (Traverses(kind))
	{
		trace->travel_ms = 100.0f;
		trace->active_transition.source_valid = 1;
		trace->active_transition.destination_valid = 1;
		trace->active_transition.clear = 1;
		trace->active_transition.sweep.fraction = 1.0f;
		trace->active_transition.sweep.end[0] = 5.0f;
	}
	if (Conditional(kind))
	{
		trace->inactive_transition.source_valid = 1;
		trace->inactive_transition.destination_valid = 1;
		trace->inactive_transition.sweep.fraction = 0.5f;
	}
	if (controller == 2U)
	{
		trace->delay_ms = 250.0f;
		trace->dwell_ms = 500.0f;
	}
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

static void FixtureInit(mechanism_fixture_t *fixture)
{
	static const sg_rune_mechanism_kind_t kinds[8] = {
		SG_RUNE_MECHANISM_DOOR, SG_RUNE_MECHANISM_BUTTON,
		SG_RUNE_MECHANISM_TRIGGER, SG_RUNE_MECHANISM_LIFT,
		SG_RUNE_MECHANISM_TRAIN, SG_RUNE_MECHANISM_ROTATOR,
		SG_RUNE_MECHANISM_PUSH, SG_RUNE_MECHANISM_TELEPORT
	};
	uint32_t entity;

	memset(fixture, 0, sizeof(*fixture));
	fixture->authority.world = &fixture->world;
	fixture->authority.identity = MakeIdentity(800.0f);
	fixture->configuration.identity = fixture->authority.identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = 2U;
	fixture->configuration_semantics.identity = fixture->authority.identity;
	fixture->configuration_semantics.regions = fixture->regions;
	fixture->configuration_semantics.region_count = 2U;
	fixture->configuration_semantics.faces = fixture->faces;
	fixture->configuration_semantics.face_count = 12U;
	fixture->regions[0].first_face = 0U;
	fixture->regions[0].face_count = 6U;
	fixture->regions[1].first_face = 6U;
	fixture->regions[1].face_count = 6U;
	SetCubeFaces(&fixture->faces[0], -10.0f, 0.0f);
	SetCubeFaces(&fixture->faces[6], 0.0f, 10.0f);
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
		if (entity < 8U)
		{
			fixture->entities[entity].flags |=
				SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND;
			fixture->entities[entity].mechanism_kind = kinds[entity];
		}
	}
	fixture->entities[0].flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED;
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
		SG_BSP_ENTITY_TOUCH_ACTIVATED;
	fixture->entities[9].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
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
	fixture->completeness.code = SG_BSP_COMPLETENESS_OK;
	fixture->completeness.expected_cells = 2U;
	fixture->completeness.represented_cells = 2U;
	fixture->completeness.proved_cells = 2U;
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
	fixture->traces[10].reset_ms = 700.0f;
	fixture->candidates[10].recovery =
		SG_MECHANISM_RECOVERY_WAIT_FOR_RESET;
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
	fixture->source.configuration = &fixture->configuration;
	fixture->source.configuration_semantics =
		&fixture->configuration_semantics;
	fixture->source.entity_semantics = &fixture->entity_semantics;
	fixture->source.completeness = &fixture->completeness;
	fixture->source.phases = fixture->phases;
	fixture->source.phase_count = PHASE_COUNT;
	fixture->source.host_traces = &fixture->catalog;
}

static int Build(mechanism_fixture_t *fixture,
	sg_mechanism_capability_set_t **set,
	sg_mechanism_capability_error_t *error)
{
	return SG_MechanismCapabilityBuild(&fixture->source, set, error);
}

static void ExpectFailure(mechanism_fixture_t *fixture,
	sg_mechanism_capability_error_code_t expected)
{
	sg_mechanism_capability_set_t *set =
		(sg_mechanism_capability_set_t *)(uintptr_t)UINT32_C(1);
	sg_mechanism_capability_error_t error;

	CHECK(!Build(fixture, &set, &error));
	CHECK(set == NULL);
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

	FixtureInit(&fixture);
	snapshot = fixture;
	CHECK(Build(&fixture, &first, &error));
	CHECK(error.code == SG_MECHANISM_CAPABILITY_ERROR_NONE);
	CHECK(first != NULL && first->fact_count == TRACE_COUNT);
	if (!first)
		return;
	CHECK(first->topology_edge_count == 3U);
	CHECK(first->topology_edge_visits == 10U);
	CHECK(first->topology_relation_count == 12U);
	CHECK(first->candidate_verifier_identity == UINT64_C(0xcafe));
	CHECK(first->trace_verifier_identity == UINT64_C(0xbeef));
	for (index = 0U; index < first->fact_count; index++)
	{
		kind_mask |= UINT32_C(1) << (uint32_t)first->facts[index].kind;
		CHECK(first->facts[index].order == index);
		CHECK(first->facts[index].parameters.gravity == 800.0f);
		if (first->facts[index].kind == SG_MECHANISM_CAPABILITY_PUSH)
			CHECK(first->facts[index].mechanism_direction.value[0] == 1.0f);
		if (first->facts[index].kind ==
			SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING)
			CHECK(first->facts[index].mechanism_angles.value[2] == 90.0f);
		if (first->facts[index].controller_entity == 1U &&
			first->facts[index].mechanism_entity == 0U)
		{
			if (shared_facts == 0U)
			{
				shared_first = first->facts[index].first_topology_edge;
				shared_count = first->facts[index].topology_edge_count;
			}
			CHECK(first->facts[index].first_topology_edge == shared_first);
			CHECK(first->facts[index].topology_edge_count == shared_count);
			shared_facts++;
		}
	}
	for (index = 0U; index < first->topology_relation_count; index++)
		if (first->topology_relations[index].controller_entity == 1U &&
			first->topology_relations[index].mechanism_entity == 0U)
			shared_relations++;
	CHECK(shared_facts == 2U);
	CHECK(shared_relations == 1U);
	CHECK(kind_mask == (UINT32_C(1) << SG_MECHANISM_CAPABILITY_KIND_COUNT) -
		UINT32_C(1));
	CHECK(SG_MechanismCapabilityAudit(&fixture.source, first, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_OK);
	CHECK(audit.proved_facts == TRACE_COUNT);
	CHECK(audit.lookup_comparisons <= (uint64_t)TRACE_COUNT * UINT64_C(6));
	CHECK(memcmp(&fixture, &snapshot, sizeof(fixture)) == 0);
	reordered = fixture;
	for (index = 0U; index < TRACE_COUNT; index++)
	{
		reordered.traces[index] = fixture.traces[TRACE_COUNT - 1U - index];
		reordered.candidates[index] =
			fixture.candidates[TRACE_COUNT - 1U - index];
	}
	reordered.catalog.traces = reordered.traces;
	reordered.catalog.candidates = reordered.candidates;
	reordered.source.authority = &reordered.authority;
	reordered.source.configuration = &reordered.configuration;
	reordered.source.configuration_semantics =
		&reordered.configuration_semantics;
	reordered.source.entity_semantics = &reordered.entity_semantics;
	reordered.source.completeness = &reordered.completeness;
	reordered.source.phases = reordered.phases;
	reordered.source.host_traces = &reordered.catalog;
	CHECK(Build(&reordered, &second, &error));
	CHECK(second != NULL && second->fact_count == first->fact_count);
	if (second)
	{
		CHECK(memcmp(second->facts, first->facts,
			(size_t)first->fact_count * sizeof(*first->facts)) == 0);
		CHECK(memcmp(second->topology_edges, first->topology_edges,
			(size_t)first->topology_edge_count *
				sizeof(*first->topology_edges)) == 0);
		CHECK(memcmp(second->topology_relations, first->topology_relations,
			(size_t)first->topology_relation_count *
				sizeof(*first->topology_relations)) == 0);
	}
	SG_MechanismCapabilityDestroy(first);
	SG_MechanismCapabilityDestroy(second);
}

static void TestIdentityAndLimits(void)
{
	mechanism_fixture_t fixture;
	FixtureInit(&fixture);
	fixture.authority.identity.schema_id = 0U;
	fixture.configuration.identity.schema_id = 0U;
	fixture.configuration_semantics.identity.schema_id = 0U;
	fixture.catalog.identity.schema_id = 0U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	FixtureInit(&fixture);
	fixture.authority.identity.physics.gravity = NAN;
	fixture.configuration.identity.physics.gravity = NAN;
	fixture.configuration_semantics.identity.physics.gravity = NAN;
	fixture.catalog.identity.physics.gravity = NAN;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	FixtureInit(&fixture);
	fixture.authority.identity.standing_hull.maxs.value[0] = -16.0f;
	fixture.configuration.identity = fixture.authority.identity;
	fixture.configuration_semantics.identity = fixture.authority.identity;
	fixture.catalog.identity = fixture.authority.identity;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	FixtureInit(&fixture);
	fixture.entity_semantics.entity_count = UINT32_MAX;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW);
}

static void TestAuthenticatedEvidence(void)
{
	mechanism_fixture_t fixture;

	FixtureInit(&fixture);
	fixture.catalog.trace_verifier_identity =
		fixture.catalog.candidate_verifier_identity;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_ARGUMENT);
	FixtureInit(&fixture);
	fixture.traces[0].candidate_identity = UINT64_C(99999);
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE);
	FixtureInit(&fixture);
	fixture.candidates[1].candidate_identity =
		fixture.candidates[0].candidate_identity;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE);
	FixtureInit(&fixture);
	fixture.candidates[0].destination_region = 0U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE);
	FixtureInit(&fixture);
	fixture.traces[0].active_transition.sweep.fraction = 0.5f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	FixtureInit(&fixture);
	fixture.traces[0].exit_witness.value[0] = 50.0f;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT);
}

static void TestTopologyTimingPhaseAndCompleteness(void)
{
	mechanism_fixture_t fixture;

	FixtureInit(&fixture);
	fixture.edges[3] = fixture.edges[0];
	fixture.entity_semantics.edge_count = 4U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY);
	FixtureInit(&fixture);
	fixture.edges[3].source = 0U;
	fixture.edges[3].destination = 1U;
	fixture.edges[3].kind = SG_MECH_EDGE_OWNER;
	fixture.edges[3].fanout_ordinal = 0U;
	fixture.entity_semantics.edge_count = 4U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY);
	FixtureInit(&fixture);
	fixture.edges[3].source = 1U;
	fixture.edges[3].destination = 2U;
	fixture.edges[3].kind = SG_MECH_EDGE_OWNER;
	fixture.edges[3].fanout_ordinal = 0U;
	fixture.entity_semantics.edge_count = 4U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY);
	FixtureInit(&fixture);
	fixture.traces[0].travel_ms = -1.0f;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureInit(&fixture);
	fixture.traces[0].delay_ms = 1000000000.0f;
	fixture.traces[0].dwell_ms = 1000000000.0f;
	fixture.traces[0].travel_ms = 1000000000.0f;
	fixture.traces[0].wait_ms = 1000000000.0f;
	fixture.traces[0].reset_ms = 1000000000.0f;
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_TIMING);
	FixtureInit(&fixture);
	fixture.phases[1].mover = MechanismRef(&fixture, 4U);
	ExpectFailure(&fixture, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE);
	FixtureInit(&fixture);
	fixture.completeness.omitted_cells = 1U;
	ExpectFailure(&fixture,
		SG_MECHANISM_CAPABILITY_ERROR_INCOMPLETE_CONFIGURATION);
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

	FixtureInit(&fixture);
	fixture.authority.identity.physics.gravity = 321.0f;
	fixture.configuration.identity = fixture.authority.identity;
	fixture.configuration_semantics.identity = fixture.authority.identity;
	fixture.catalog.identity = fixture.authority.identity;
	CHECK(Build(&fixture, &set, &error));
	if (!set)
		return;
	CHECK(set->facts[0].parameters.gravity == 321.0f);
	saved_trace = set->facts[0].trace_identity;
	set->facts[0].trace_identity++;
	CHECK(!SG_MechanismCapabilityAudit(&fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_OMITTED_FACT ||
		audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	set->facts[0].trace_identity = saved_trace;
	saved_edge = set->topology_edges[0];
	set->topology_edges[0] = 3U;
	CHECK(!SG_MechanismCapabilityAudit(&fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT);
	set->topology_edges[0] = saved_edge;
	saved_verifier = set->candidate_verifier_identity;
	set->candidate_verifier_identity++;
	CHECK(!SG_MechanismCapabilityAudit(&fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
	set->candidate_verifier_identity = saved_verifier;
	saved_region = fixture.candidates[0].destination_region;
	fixture.candidates[0].destination_region = 0U;
	CHECK(!SG_MechanismCapabilityAudit(&fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	fixture.candidates[0].destination_region = saved_region;
	fixture.authority.identity.schema_id = 0U;
	fixture.configuration.identity.schema_id = 0U;
	fixture.configuration_semantics.identity.schema_id = 0U;
	fixture.catalog.identity.schema_id = 0U;
	set->identity.schema_id = 0U;
	CHECK(!SG_MechanismCapabilityAudit(&fixture.source, set, &audit));
	CHECK(audit.code == SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
	SG_MechanismCapabilityDestroy(set);
}

int main(void)
{
	TestCompleteModel();
	TestIdentityAndLimits();
	TestAuthenticatedEvidence();
	TestTopologyTimingPhaseAndCompleteness();
	TestGravityAndAudit();
	if (failures != 0)
	{
		fprintf(stderr, "mechanism capability failures: %d\n", failures);
		return 1;
	}
	puts("mechanism capability checks passed");
	return 0;
}
