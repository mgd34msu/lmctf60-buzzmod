#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_belief_contract.h"
#include "slipgate/sg_destination_field.h"
#include "slipgate/sg_strategy_contract.h"
#include "slipgate/sg_tactic_contract.h"
#include "slipgate/sg_weapon_contract.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_destination_handle_t Destination(sg_destination_kind_t kind,
	uint64_t id, uint32_t phase_id, uint32_t cell_id)
{
	return (sg_destination_handle_t){
		.id = id,
		.generation = 1U,
		.kind = kind,
		.motion = SG_DESTINATION_STATIC,
		.valid = 1U,
		.pose = {
			.phase = { phase_id, cell_id },
			.position = { 10.0f, 20.0f, 30.0f }
		}
	};
}

static void TestPhaseSpaceFieldContract(void)
{
	int cells;
	sg_phase_coordinate_t phases[3] = {
		{ 0U, 0U }, { 1U, 0U }, { 2U, 1U }
	};
	sg_rune_runtime_snapshot_t snapshot = {
		.identity = 99U,
		.topology_revision = 7U,
		.cell_count = 2U,
		.phase_count = 3U,
		.cells = &cells,
		.phases = phases
	};
	sg_field_sample_t samples[3] = {
		{
			.phase = { 0U, 0U },
			.next_phase = { 1U, 0U },
			.cost_ms = 200U,
			.capability_mask = 1U,
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 1.0f, 0.0f },
			.finite = 1U
		},
		{
			.phase = { 1U, 0U },
			.next_phase = { 2U, 1U },
			.cost_ms = 100U,
			.capability_mask = 2U,
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 0.5f, 0.0f },
			.finite = 1U
		},
		{
			.phase = { 2U, 1U },
			.next_phase = { 2U, 1U },
			.cost_ms = 0U,
			.capability_mask = 1U,
			.finite = 1U
		}
	};
	sg_destination_field_t field = {
		.rune_identity = 99U,
		.topology_revision = 7U,
		.generation = 1U,
		.computed_at_ms = 100U,
		.destination = Destination(SG_DESTINATION_WAYPOINT, 1U, 2U, 1U),
		.samples = samples,
		.sample_count = 3U,
		.complete = 1U
	};

	CHECK(SG_DestinationFieldValid(&snapshot, &field));
	CHECK(samples[0].phase.cell_id == samples[1].phase.cell_id);
	field.sample_count = snapshot.cell_count;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	field.sample_count = snapshot.phase_count;
	samples[1].phase.phase_id = samples[0].phase.phase_id;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	samples[1].phase.phase_id = 1U;
	field.destination.pose.phase.phase_id = 3U;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	field.destination.pose.phase = (sg_phase_coordinate_t){ 1U, 1U };
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
}

static sg_strategy_goal_t StrategyGoal(uint32_t id,
	uint64_t authority_generation)
{
	sg_strategy_goal_t goal;

	memset(&goal, 0, sizeof(goal));
	goal.id = id;
	goal.kind = SG_STRATEGY_GOAL_DESTINATION;
	goal.failure_policy = SG_STRATEGY_FAILURE_ABORT;
	goal.authority_generation = authority_generation;
	goal.destination = Destination(SG_DESTINATION_WAYPOINT, id, 0U, 0U);
	return goal;
}

static void StrategyState(sg_strategy_state_t *state)
{
	memset(state, 0, sizeof(*state));
	state->owned_items[0] = StrategyGoal(1U, 1U);
	state->queue = (sg_strategy_queue_t){
		.items = state->owned_items,
		.count = 1U,
		.capacity = SG_STRATEGY_MAX_GOALS,
		.plan_id = 10U,
		.generation = 1U
	};
	state->lifecycle = SG_STRATEGY_READY;
	state->last_event = SG_STRATEGY_EVENT_START;
	state->last_failure = SG_STRATEGY_FAILURE_UNKNOWN;
	state->authority = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
	state->revision = 5U;
	state->authority_generation = 1U;
	state->last_event_at_ms = 100U;
}

static sg_strategy_event_t StrategyEvent(sg_strategy_event_kind_t kind)
{
	return (sg_strategy_event_t){
		.kind = kind,
		.actor = SG_STRATEGY_ACTOR_STRATEGY,
		.authority = SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		.expected_revision = 5U,
		.expected_authority_generation = 1U,
		.at_ms = 110U,
		.valid_until_ms = 120U
	};
}

static void TestStrategyBindings(void)
{
	sg_strategy_state_t state;
	sg_strategy_event_t event = StrategyEvent(SG_STRATEGY_EVENT_START);
	sg_strategy_goal_t replacement_goal = StrategyGoal(2U, 1U);
	sg_strategy_queue_t replacement = {
		.items = &replacement_goal,
		.count = 1U,
		.capacity = 1U,
		.plan_id = 11U,
		.generation = 1U
	};

	StrategyState(&state);
	CHECK(SG_StrategyStateValid(&state));
	CHECK(SG_StrategyEventBoundToState(&state, &event, 115U));
	event.expected_revision = 0U;
	CHECK(!SG_StrategyEventValid(&event));
	event.expected_revision = state.revision;
	event.expected_authority_generation = 2U;
	CHECK(!SG_StrategyEventBoundToState(&state, &event, 115U));
	event.expected_authority_generation = state.authority_generation;
	CHECK(!SG_StrategyEventBoundToState(&state, &event, 121U));
	event.at_ms = 90U;
	event.valid_until_ms = 120U;
	CHECK(!SG_StrategyEventBoundToState(&state, &event, 100U));
	event.at_ms = 110U;
	event.valid_until_ms = 120U;
	state.owned_items[0].authority_generation = 2U;
	CHECK(!SG_StrategyStateValid(&state));
	state.owned_items[0].authority_generation = 1U;
	event.kind = SG_STRATEGY_EVENT_REPLACED;
	event.actor = SG_STRATEGY_ACTOR_HUMAN;
	event.authority = SG_STRATEGY_AUTHORITY_HUMAN_ORDER;
	event.detail.replacement.queue = &replacement;
	CHECK(!SG_StrategyEventBoundToState(&state, &event, 115U));
	replacement_goal.authority_generation = 2U;
	CHECK(SG_StrategyEventBoundToState(&state, &event, 115U));
}

static sg_learning_parameters_t LearningParameters(uint64_t generation)
{
	return (sg_learning_parameters_t){
		.rune_identity = 99U,
		.topology_revision = 7U,
		.bsp_identity = 700U,
		.physics_identity = 800U,
		.generation = generation
	};
}

static sg_learning_update_t LearningUpdate(void)
{
	return (sg_learning_update_t){
		.evidence = {
			.evidence_id = 100U,
			.rune_identity = 99U,
			.topology_revision = 7U,
			.bsp_identity = 700U,
			.physics_identity = 800U,
			.trace_identity = 900U,
			.captured_at_ms = 1000U,
			.authenticated_at_ms = 1001U,
			.authenticated = 1U,
			.exact_bound = 1U,
			.host_verified = 1U,
			.post_match = 1U
		},
		.kind = SG_LEARNING_UPDATE_COST,
		.value.cost = { 1U, 20 }
	};
}

static void TestLearningTransactionIdentity(void)
{
	sg_learning_parameters_t parameters = LearningParameters(5U);
	sg_learning_parameters_t foreign;
	sg_learning_transaction_t transaction = {
		.transaction_id = 1U,
		.expected_generation = 4U,
		.applied_generation = 5U,
		.evidence_id = 100U,
		.state = SG_LEARNING_TRANSACTION_APPLIED,
		.before = LearningParameters(4U),
		.authorized_update = LearningUpdate()
	};

	CHECK(SG_LearningTransactionMayCommit(&parameters, &transaction));
	CHECK(SG_LearningTransactionMayRollback(&parameters, &transaction));
	foreign = parameters;
	foreign.rune_identity++;
	CHECK(!SG_LearningTransactionMayCommit(&foreign, &transaction));
	foreign = parameters;
	foreign.topology_revision++;
	CHECK(!SG_LearningTransactionMayRollback(&foreign, &transaction));
	foreign = parameters;
	foreign.bsp_identity++;
	CHECK(!SG_LearningTransactionMayCommit(&foreign, &transaction));
	foreign = parameters;
	foreign.physics_identity++;
	CHECK(!SG_LearningTransactionMayRollback(&foreign, &transaction));
}

static sg_tactic_request_t TacticRequest(void)
{
	return (sg_tactic_request_t){
		.live = {
			.rune_identity = 99U,
			.pose_revision = 4U,
			.now_ms = 500U,
			.phase_coordinate = { 1U, 0U },
			.phase = SG_TACTIC_PHASE_GROUND,
			.supported = 1U
		},
		.gradient = {
			.rune_identity = 99U,
			.field_generation = 3U,
			.pose_revision = 4U,
			.sampled_at_ms = 500U,
			.phase_coordinate = { 1U, 0U },
			.next_phase_coordinate = { 2U, 1U },
			.phase = SG_TACTIC_PHASE_GROUND,
			.cost_ms = 50U,
			.capability_mask = SG_TACTIC_CAPABILITY_BIT(
				SG_TACTIC_CAPABILITY_WALK),
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 1.0f, 0.0f },
			.finite = 1U
		},
		.legal_capability_mask = SG_TACTIC_CAPABILITY_BIT(
			SG_TACTIC_CAPABILITY_WALK)
	};
}

static void TestTacticBindingsAndResults(void)
{
	sg_tactic_request_t request = TacticRequest();
	sg_tactic_result_t result = {
		.status = SG_TACTIC_RESULT_PROGRESS,
		.failure = SG_TACTIC_FAILURE_NONE,
		.capability = SG_TACTIC_CAPABILITY_WALK,
		.target_phase = { 2U, 1U },
		.expected_cost_ms = 60U,
		.progress = 0.5f
	};

	CHECK(SG_TacticRequestValid(&request));
	request.gradient.pose_revision++;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.sampled_at_ms++;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.phase = SG_TACTIC_PHASE_AIR;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.phase_coordinate.phase_id++;
	CHECK(!SG_TacticRequestValid(&request));
	CHECK(SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_LIVE_STATE;
	CHECK(!SG_TacticResultValid(&result));
	result.status = SG_TACTIC_RESULT_RETRY;
	result.expected_cost_ms = SG_DESTINATION_FIELD_INF;
	result.progress = 0.0f;
	CHECK(SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_NONE;
	CHECK(!SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_LIVE_STATE;
	result.expected_cost_ms = 10U;
	CHECK(!SG_TacticResultValid(&result));
}

static sg_belief_observation_t NegativeObservation(void)
{
	return (sg_belief_observation_t){
		.auth = {
			.authenticated = 1U,
			.issuer_kind = SG_BELIEF_ISSUER_BOT,
			.issuer_team = 1U,
			.audience_team = 1U,
			.issuer_client = 1U,
			.observation_id = 1U,
			.authenticated_at_ms = 99U
		},
		.source = SG_BELIEF_SOURCE_VISUAL,
		.shape = SG_BELIEF_SHAPE_NEGATIVE,
		.target_team = 2U,
		.target_client = 3U,
		.movement_state = SG_BELIEF_MOTION_GROUND,
		.observed_at_ms = 100U,
		.valid_until_ms = 200U,
		.cell_id = 3U,
		.position = { 10.0f, 20.0f, 30.0f },
		.spread_radius = 64.0f,
		.confidence = 0.8f
	};
}

static void TestNegativeEvidenceSupport(void)
{
	sg_belief_observation_t observation = NegativeObservation();

	CHECK(SG_BeliefObservationValidForTeam(&observation, 1U));
	observation.confidence = 0.0f;
	CHECK(!SG_BeliefObservationValidForTeam(&observation, 1U));
	observation = NegativeObservation();
	observation.spread_radius = 0.0f;
	CHECK(!SG_BeliefObservationValidForTeam(&observation, 1U));
}

static sg_weapon_effect_query_t WeaponQuery(sg_belief_state_t *target)
{
	static const sg_weapon_profile_t profile = {
		.id = 1U,
		.family = SG_WEAPON_FAMILY_HITSCAN,
		.effects = SG_WEAPON_EFFECT_HITSCAN,
		.max_range = 1000.0f,
		.direct_damage = 100.0f,
		.ammo_cost = 1U,
		.requires_live_trace = 1U
	};
	static const sg_weapon_affordance_t affordance = {
		.rune_identity = 99U,
		.visibility_revision = 2U,
		.source_cell_id = 1U,
		.target_cell_id = 3U,
		.allowed_effects = SG_WEAPON_EFFECT_HITSCAN,
		.visibility_probability = 1.0f,
		.exact_live_trace_required = 1U
	};

	return (sg_weapon_effect_query_t){
		.profile = &profile,
		.affordance = &affordance,
		.target_belief = target,
		.ammo_available = 1U,
		.shooter_client = 1U,
		.target_client = 3U,
		.audience_team = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.shooter_cell_id = 1U,
		.target_cell_id = 3U,
		.rune_identity = 99U,
		.now_ms = 500U,
		.prediction_time_ms = 700U,
		.teammate_snapshot_revision = 1U,
		.teammate_evidence_complete = 1U,
		.shooter_health = 100.0f
	};
}

static void TestWeaponObservationAndClientBindings(void)
{
	sg_belief_particle_t particle = {
		.cell_id = 3U,
		.movement_state = SG_BELIEF_MOTION_GROUND,
		.future_time_ms = 500U,
		.position = { 100.0f, 0.0f, 0.0f },
		.weight = 1.0f
	};
	sg_belief_state_t target = {
		.audience_team = 1U,
		.target_team = 2U,
		.target_client = 3U,
		.particle_count = 1U,
		.particle_capacity = 1U,
		.generation = 1U,
		.updated_at_ms = 500U,
		.total_weight = 1.0f,
		.particles = &particle
	};
	sg_weapon_effect_query_t query = WeaponQuery(&target);
	sg_weapon_prefire_request_t request = {
		.shot_id = 7U,
		.shot_revision = 8U,
		.rune_identity = 99U,
		.pose_revision = 4U,
		.fired_at_ms = 500U,
		.prediction_time_ms = 700U,
		.source_cell_id = 1U,
		.target_cell_id = 3U,
		.shooter_client = 1U,
		.target_client = 3U,
		.profile_id = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.audience_team = 1U,
		.exact_required = 1U,
		.muzzle_origin = { 10.0f, 20.0f, 30.0f },
		.aim_direction = { 1.0f, 0.0f, 0.0f },
		.intended_impact = { 100.0f, 20.0f, 30.0f }
	};
	sg_weapon_prefire_validation_t validation = {
		.shot_id = 7U,
		.shot_revision = 8U,
		.rune_identity = 99U,
		.pose_revision = 4U,
		.fired_at_ms = 500U,
		.prediction_time_ms = 700U,
		.source_cell_id = 1U,
		.target_cell_id = 3U,
		.shooter_client = 1U,
		.target_client = 3U,
		.profile_id = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.audience_team = 1U,
		.trace_status = SG_WEAPON_TRACE_ACCEPTED,
		.muzzle_clear = 1U,
		.host_agrees = 1U,
		.authenticated = 1U,
		.authorization_id = 1U,
		.muzzle_origin = { 10.0f, 20.0f, 30.0f },
		.aim_direction = { 1.0f, 0.0f, 0.0f },
		.intended_impact = { 100.0f, 20.0f, 30.0f }
	};

	CHECK(SG_WeaponEffectQueryValid(&query));
	target.updated_at_ms = 501U;
	particle.future_time_ms = 501U;
	CHECK(SG_BeliefStateValid(&target));
	CHECK(!SG_WeaponEffectQueryValid(&query));
	target.updated_at_ms = 500U;
	particle.future_time_ms = 500U;
	target.target_client = query.shooter_client;
	query.target_client = query.shooter_client;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	CHECK(SG_WeaponPrefireShotMatches(&request, &validation));
	validation.pose_revision++;
	CHECK(!SG_WeaponPrefireShotMatches(&request, &validation));
	request.target_client = request.shooter_client;
	CHECK(!SG_WeaponPrefireRequestValid(&request));
}

int main(void)
{
	TestPhaseSpaceFieldContract();
	TestStrategyBindings();
	TestLearningTransactionIdentity();
	TestTacticBindingsAndResults();
	TestNegativeEvidenceSupport();
	TestWeaponObservationAndClientBindings();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_runtime_contract_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_runtime_contract_test: ok");
	return 0;
}
