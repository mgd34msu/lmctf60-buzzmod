#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_strategy_contract.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_strategy_goal_spec_t Goal(uint32_t id)
{
	sg_strategy_goal_spec_t goal;

	memset(&goal, 0, sizeof(goal));
	goal.id = id;
	goal.kind = SG_STRATEGY_GOAL_WAIT;
	goal.failure.max_attempts_per_choice = 1U;
	goal.failure.exhausted = SG_STRATEGY_FAILURE_FAIL_PLAN;
	return goal;
}

static sg_destination_ref_t Waypoint(uint64_t id)
{
	sg_destination_ref_t ref;

	memset(&ref, 0, sizeof(ref));
	ref.kind = SG_DESTINATION_WAYPOINT;
	ref.value.point.point_id = id;
	return ref;
}

static sg_strategy_goal_spec_t DestinationGoal(uint32_t id, uint32_t target,
	uint64_t point)
{
	sg_strategy_goal_spec_t goal = Goal(id);

	goal.kind = SG_STRATEGY_GOAL_DESTINATION;
	goal.choice_count = 1U;
	goal.choices[0].id = target;
	goal.choices[0].destination = Waypoint(point);
	goal.unavailable = SG_STRATEGY_UNAVAILABLE_WAIT;
	return goal;
}

static sg_strategy_authority_stamp_t Authority(sg_strategy_authority_rank_t rank,
	sg_strategy_principal_kind_t kind, uint32_t id, uint64_t epoch)
{
	sg_strategy_authority_stamp_t stamp;

	memset(&stamp, 0, sizeof(stamp));
	stamp.rank = rank;
	stamp.principal.kind = kind;
	stamp.principal.id = id;
	stamp.epoch = epoch;
	return stamp;
}

static sg_strategy_frame_t Frame(uint64_t sequence, uint64_t revision,
	uint64_t at_ms)
{
	sg_strategy_frame_t frame;

	memset(&frame, 0, sizeof(frame));
	frame.sequence = sequence;
	frame.expected_revision = revision;
	frame.at_ms = at_ms;
	return frame;
}

static sg_destination_handle_t Handle(sg_destination_kind_t kind, uint64_t id,
	uint64_t generation)
{
	sg_destination_handle_t handle;

	memset(&handle, 0, sizeof(handle));
	handle.id = id;
	handle.generation = generation;
	handle.kind = kind;
	handle.motion = SG_DESTINATION_MOVING;
	handle.valid = 1U;
	handle.pose.phase.phase_id = 1U;
	handle.pose.phase.cell_id = 1U;
	handle.pose.sample_time_ms = 1U;
	return handle;
}

static sg_strategy_destination_observation_t Observation(uint64_t plan_id,
	uint32_t goal_id, uint32_t target_id, uint64_t revision, uint64_t at_ms,
	sg_strategy_destination_status_t status, uint32_t cost, uint64_t handle_id)
{
	sg_strategy_destination_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.plan_id = plan_id;
	observation.goal_id = goal_id;
	observation.target_id = target_id;
	observation.observation_revision = revision;
	observation.observed_at_ms = at_ms;
	observation.valid_until_ms = at_ms + 10000U;
	observation.status = status;
	observation.cost_ms = cost;
	if (status != SG_STRATEGY_DESTINATION_UNOBSERVED)
	{
		observation.pose_revision = revision;
		observation.handle = Handle(SG_DESTINATION_WAYPOINT, handle_id,
			revision);
	}
	return observation;
}

static int Compile(const sg_strategy_plan_spec_t *spec,
	sg_strategy_plan_t *plan)
{
	sg_strategy_compile_error_t error;
	return SG_StrategyPlanCompile(spec, plan, &error);
}

static void Begin(sg_strategy_state_t *state, const sg_strategy_plan_t *plan,
	const sg_strategy_destination_observation_t *observations,
	uint16_t observation_count, sg_strategy_reduction_t *reduction)
{
	sg_strategy_policy_t policy = { 100U };
	sg_strategy_frame_t frame;

	CHECK(SG_StrategyStateInit(state, &policy));
	frame = Frame(1U, state->revision, 100U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_HUMAN,
		SG_STRATEGY_PRINCIPAL_HUMAN, 9U, 1U);
	frame.directive.replacement = plan;
	frame.life.present = 1U;
	frame.life.alive = 1U;
	frame.life.observation_revision = 1U;
	frame.life.life_id = 1U;
	frame.destinations = observations;
	frame.destination_count = observation_count;
	CHECK(SG_StrategyReduce(state, &frame, reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
}

static void BindTactical(sg_strategy_frame_t *frame,
	const sg_strategy_state_t *state, uint64_t observation_revision,
	uint8_t blocked, sg_strategy_tactical_block_reason_t reason)
{
	frame->tactical.present = 1U;
	frame->tactical.blocked = blocked;
	frame->tactical.observation_revision = observation_revision;
	frame->tactical.activation = state->activation;
	frame->tactical.reason = reason;
}

static void TestForwardDag(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_compile_error_t error;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 10U;
	spec.goal_count = 3U;
	spec.goals[0] = Goal(30U);
	spec.goals[0].dependency_count = 1U;
	spec.goals[0].dependencies[0].goal_id = 20U;
	spec.goals[0].dependencies[0].accept = SG_STRATEGY_DEPENDENCY_SUCCESS;
	spec.goals[1] = Goal(10U);
	spec.goals[2] = Goal(20U);
	spec.goals[2].dependency_count = 1U;
	spec.goals[2].dependencies[0].goal_id = 10U;
	spec.goals[2].dependencies[0].accept = SG_STRATEGY_DEPENDENCY_SUCCESS;

	CHECK(SG_StrategyPlanCompile(&spec, &plan, &error));
	CHECK(plan.goals[0].dependencies[0].goal_index == 2U);
	CHECK(plan.goals[2].dependencies[0].goal_index == 1U);
	CHECK(plan.topological_order[0] == 1U);
	CHECK(plan.topological_order[1] == 2U);
	CHECK(plan.topological_order[2] == 0U);
}

static void TestCycleRejected(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_plan_t before;
	sg_strategy_compile_error_t error;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 11U;
	spec.goal_count = 2U;
	spec.goals[0] = Goal(1U);
	spec.goals[1] = Goal(2U);
	spec.goals[0].dependency_count = 1U;
	spec.goals[0].dependencies[0].goal_id = 2U;
	spec.goals[0].dependencies[0].accept = SG_STRATEGY_DEPENDENCY_SUCCESS;
	spec.goals[1].dependency_count = 1U;
	spec.goals[1].dependencies[0].goal_id = 1U;
	spec.goals[1].dependencies[0].accept = SG_STRATEGY_DEPENDENCY_SUCCESS;

	memset(&plan, 0xa5, sizeof(plan));
	before = plan;
	CHECK(!SG_StrategyPlanCompile(&spec, &plan, &error));
	CHECK(error.code == SG_STRATEGY_COMPILE_CYCLE);
	CHECK(memcmp(&plan, &before, sizeof(plan)) == 0);
}

static void TestCompilerFailuresAndCanonicalBytes(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_spec_t poisoned;
	sg_strategy_plan_t first;
	sg_strategy_plan_t second;
	sg_strategy_plan_t before;
	sg_strategy_compile_error_t error;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 12U;
	spec.goal_count = 1U;
	spec.goals[0] = Goal(1U);
	spec.goals[0].dependency_count = 1U;
	spec.goals[0].dependencies[0].goal_id = 99U;
	memset(&first, 0xa5, sizeof(first));
	before = first;
	CHECK(!SG_StrategyPlanCompile(&spec, &first, &error));
	CHECK(error.code == SG_STRATEGY_COMPILE_MISSING_DEPENDENCY);
	CHECK(memcmp(&first, &before, sizeof(first)) == 0);

	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &first));
	poisoned = spec;
	memset(&poisoned.goals[0].choices[0].destination.value, 0xa5,
		sizeof(poisoned.goals[0].choices[0].destination.value));
	poisoned.goals[0].choices[0].destination.kind = SG_DESTINATION_WAYPOINT;
	poisoned.goals[0].choices[0].destination.value.point.point_id = 100U;
	CHECK(Compile(&poisoned, &second));
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 13U;
	spec.goal_count = 2U;
	spec.goals[0] = Goal(1U);
	spec.goals[1] = Goal(1U);
	memset(&first, 0x5a, sizeof(first));
	before = first;
	CHECK(!SG_StrategyPlanCompile(&spec, &first, &error));
	CHECK(error.code == SG_STRATEGY_COMPILE_DUPLICATE_GOAL_ID);
	CHECK(memcmp(&first, &before, sizeof(first)) == 0);

	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[1] = DestinationGoal(2U, 10U, 101U);
	CHECK(!SG_StrategyPlanCompile(&spec, &first, &error));
	CHECK(error.code == SG_STRATEGY_COMPILE_DUPLICATE_TARGET_ID);

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 14U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 1U, 1U);
	spec.goals[0].choices[0].destination.kind = SG_DESTINATION_CARRIER;
	spec.goals[0].choices[0].destination.value.carrier.team = 1U;
	spec.goals[0].choices[0].destination.value.carrier.selector =
		SG_DESTINATION_CARRIER_ANY;
	spec.goals[0].choices[0].destination.value.carrier.client_id = 1U;
	CHECK(!SG_StrategyPlanCompile(&spec, &first, &error));
}

static void TestPolicyAndTieBreak(void)
{
	sg_strategy_policy_t policy = { 0U };
	sg_strategy_state_t state;
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];

	memset(&state, 0xa5, sizeof(state));
	CHECK(!SG_StrategyStateInit(&state, &policy));
	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 15U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].failure.try_alternatives = 1U;
	spec.goals[0].choice_count = 2U;
	spec.goals[0].choices[1].id = 11U;
	spec.goals[0].choices[1].destination = Waypoint(101U);
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(15U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 40U, 1000U);
	observations[1] = Observation(15U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 40U, 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(reduction.instruction.choice_index == 0U);
}

static void TestFixedPoint64AndOwnedPlan(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	uint16_t index;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 20U;
	spec.goal_count = SG_STRATEGY_MAX_GOALS;
	for (index = 0U; index < spec.goal_count; index++)
	{
		spec.goals[index] = Goal((uint32_t)index + 1U);
		if (index != 0U)
		{
			spec.goals[index].dependency_count = 1U;
			spec.goals[index].dependencies[0].goal_id = (uint32_t)index;
			spec.goals[index].dependencies[0].accept =
				SG_STRATEGY_DEPENDENCY_SUCCESS;
		}
	}
	CHECK(Compile(&spec, &plan));
	Begin(&state, &plan, NULL, 0U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_COMPLETED);
	CHECK(reduction.effect_count == SG_STRATEGY_MAX_GOALS + 2U);
	CHECK(reduction.effect_count <= SG_STRATEGY_MAX_EFFECTS);
	memset(&plan, 0x55, sizeof(plan));
	memset(&spec, 0x66, sizeof(spec));
	CHECK(state.plan.plan_id == 20U);
	CHECK(state.goals[SG_STRATEGY_MAX_GOALS - 1U].phase ==
		SG_STRATEGY_GOAL_SUCCEEDED);
}

static void TestCheapestMovingAndDuplicateRejects(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_state_t before;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];
	sg_strategy_frame_t frame;
	uint64_t activation;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 30U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].failure.try_alternatives = 1U;
	spec.goals[0].choice_count = 2U;
	spec.goals[0].choices[1].id = 11U;
	spec.goals[0].choices[1].destination = Waypoint(101U);
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(30U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 50U, 1000U);
	observations[1] = Observation(30U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 40U, 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(reduction.instruction.choice_index == 1U);
	activation = state.activation.activation_id;

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	observations[0] = Observation(30U, 1U, 11U, 2U, 110U,
		SG_STRATEGY_DESTINATION_REACHABLE, 25U, 2001U);
	frame.destinations = observations;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.activation.activation_id == activation);
	CHECK(reduction.instruction.handle.id == 2001U);
	CHECK(reduction.instruction.cost_ms == 25U);

	before = state;
	frame = Frame(2U, 0U, 0U);
	frame.fact_count = UINT16_MAX;
	frame.facts = NULL;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_DUPLICATE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(reduction.instruction.handle.id == 2001U);

	frame = Frame(3U, state.revision - 1U, 120U);
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_STALE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(3U, state.revision, 120U);
	BindTactical(&frame, &state, 2U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.tactical.activation.activation_id++;
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(3U, state.revision, 120U);
	BindTactical(&frame, &state, 2U, 0U, SG_STRATEGY_BLOCK_NONE);
	observations[0] = Observation(30U, 1U, 11U, 3U, 120U,
		SG_STRATEGY_DESTINATION_REACHABLE, 20U, 3001U);
	observations[1] = observations[0];
	observations[1].target_id = 999U;
	frame.destinations = observations;
	frame.destination_count = 2U;
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void TestSuspensionDeathAndRespawn(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;
	uint64_t first_activation;
	uint64_t suspended_at;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 40U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(40U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	first_activation = state.activation.activation_id;

	frame = Frame(2U, state.revision, 120U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	suspended_at = state.suspension.suspended_at_ms;
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);

	frame = Frame(3U, state.revision, 150U);
	BindTactical(&frame, &state, 2U, 1U, SG_STRATEGY_BLOCK_OBSTRUCTION);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.suspension.suspended_at_ms == suspended_at);

	frame = Frame(4U, state.revision, 160U);
	BindTactical(&frame, &state, 3U, 0U, SG_STRATEGY_BLOCK_NONE);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);

	frame = Frame(5U, state.revision, 170U);
	BindTactical(&frame, &state, 4U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.life.present = 1U;
	frame.life.alive = 0U;
	frame.life.observation_revision = 2U;
	frame.life.life_id = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_LIFE);
	CHECK(state.activation.activation_id == 0U);
	CHECK(state.goals[0].attempt_count == 1U);

	frame = Frame(6U, state.revision, 180U);
	frame.life.present = 1U;
	frame.life.alive = 1U;
	frame.life.observation_revision = 3U;
	frame.life.life_id = 2U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.activation.activation_id != first_activation);
	CHECK(state.goals[0].attempt_count == 1U);
}

static void TestSuspensionExpiry(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 41U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(41U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	frame = Frame(3U, state.revision, 210U);
	BindTactical(&frame, &state, 2U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_FAILED);
	CHECK(state.goals[0].last_failure ==
		SG_STRATEGY_FAILURE_TACTICAL_BLOCK_EXPIRED);
}

static void TestUnavailableAlternativeRetry(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 50U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].unavailable = SG_STRATEGY_UNAVAILABLE_APPLY_FAILURE;
	spec.goals[0].failure.try_alternatives = 1U;
	spec.goals[0].failure.max_attempts_per_choice = 2U;
	spec.goals[0].failure.retry_wake.kind = SG_STRATEGY_RETRY_NEXT_FRAME;
	spec.goals[0].choice_count = 2U;
	spec.goals[0].choices[1].id = 11U;
	spec.goals[0].choices[1].destination = Waypoint(101U);
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(50U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_UNREACHABLE, SG_DESTINATION_FIELD_INF, 1U);
	observations[1] = Observation(50U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_DESTINATION_UNREACHABLE, SG_DESTINATION_FIELD_INF, 2U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.goals[0].choices[0].attempts == 1U);
	CHECK(state.goals[0].choices[1].attempts == 1U);

	frame = Frame(2U, state.revision, 110U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_FAILED);
	CHECK(state.goals[0].choices[0].attempts == 2U);
	CHECK(state.goals[0].choices[1].attempts == 2U);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_FAILED);
}

static void TestOutcomeAlternativeExhaustion(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 51U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].failure.try_alternatives = 1U;
	spec.goals[0].choice_count = 2U;
	spec.goals[0].choices[1].id = 11U;
	spec.goals[0].choices[1].destination = Waypoint(101U);
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(51U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 10U, 1000U);
	observations[1] = Observation(51U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 20U, 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.goals[0].selected_choice == 0U);
	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_FAILED;
	frame.goal_outcome.failure = SG_STRATEGY_FAILURE_OBSTRUCTED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].selected_choice == 1U);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	frame = Frame(3U, state.revision, 120U);
	BindTactical(&frame, &state, 2U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_FAILED;
	frame.goal_outcome.failure = SG_STRATEGY_FAILURE_OBSTRUCTED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_FAILED);
}

static void TestAuthorityCancelReleaseAndFactBoundary(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_plan_t corrupt;
	sg_strategy_state_t state;
	sg_strategy_state_t before;
	sg_strategy_reduction_t reduction;
	sg_strategy_frame_t frame;
	sg_strategy_fact_observation_t facts[2];

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 60U;
	spec.goal_count = 1U;
	spec.goals[0] = Goal(1U);
	CHECK(Compile(&spec, &plan));
	Begin(&state, &plan, NULL, 0U, &reduction);
	corrupt = plan;
	corrupt.goals[0].id = 0U;
	frame = Frame(2U, state.revision, 105U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.replacement = &corrupt;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 3U, 2U);
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(2U, state.revision, 110U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.replacement = &plan;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_TEAM,
		SG_STRATEGY_PRINCIPAL_TEAM, 2U, 2U);
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_AUTHORITY);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	memset(facts, 0, sizeof(facts));
	facts[0].key.kind = SG_STRATEGY_FACT_CUSTOM;
	facts[0].key.subject_id = 7U;
	facts[0].value = 1;
	facts[0].observation_revision = 1U;
	facts[0].observed_at_ms = 110U;
	facts[0].valid_until_ms = 200U;
	frame = Frame(2U, state.revision, 110U);
	frame.facts = facts;
	frame.fact_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.plan.plan_id == 60U);
	CHECK(state.authority.rank == SG_STRATEGY_AUTHORITY_HUMAN);

	frame = Frame(3U, state.revision, 120U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_TEAM,
		SG_STRATEGY_PRINCIPAL_HUMAN, 3U, 3U);
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_AUTHORITY);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(3U, state.revision, 120U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 3U, 3U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_CANCELLED);

	frame = Frame(4U, state.revision, 130U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_RELEASE;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 99U, 4U);
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_AUTHORITY);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(4U, state.revision, 130U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_RELEASE;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 3U, 4U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.authority.rank == SG_STRATEGY_AUTHORITY_AUTONOMOUS);
	CHECK(state.authority.principal.kind == SG_STRATEGY_PRINCIPAL_NONE);

	frame = Frame(5U, state.revision, 140U);
	facts[0].observation_revision = 2U;
	facts[0].observed_at_ms = 140U;
	facts[0].valid_until_ms = 200U;
	facts[1] = facts[0];
	facts[1].key.subject_id = 8U;
	facts[1].valid_until_ms = 130U;
	frame.facts = facts;
	frame.fact_count = 2U;
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void TestWeaponArmorFlagChain(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[3];
	sg_strategy_frame_t frame;
	uint16_t index;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 70U;
	spec.goal_count = 3U;
	for (index = 0U; index < 3U; index++)
	{
		spec.goals[index] = DestinationGoal((uint32_t)index + 1U,
			(uint32_t)index + 10U, (uint64_t)index + 100U);
		if (index != 0U)
		{
			spec.goals[index].dependency_count = 1U;
			spec.goals[index].dependencies[0].goal_id = (uint32_t)index;
			spec.goals[index].dependencies[0].accept =
				SG_STRATEGY_DEPENDENCY_SUCCESS;
		}
	}
	spec.goals[0].kind = SG_STRATEGY_GOAL_COLLECT_ITEM;
	spec.goals[1].kind = SG_STRATEGY_GOAL_COLLECT_ITEM;
	spec.goals[2].kind = SG_STRATEGY_GOAL_CAPTURE_FLAG;
	spec.goals[0].choices[0].destination.kind = SG_DESTINATION_WEAPON;
	spec.goals[0].choices[0].destination.value.item.item_id = 100U;
	spec.goals[1].choices[0].destination.kind = SG_DESTINATION_ARMOR;
	spec.goals[1].choices[0].destination.value.item.item_id = 101U;
	memset(&spec.goals[2].choices[0].destination, 0,
		sizeof(spec.goals[2].choices[0].destination));
	spec.goals[2].choices[0].destination.kind = SG_DESTINATION_FLAG;
	spec.goals[2].choices[0].destination.value.flag.team = 2U;
	spec.goals[2].choices[0].destination.value.flag.location =
		SG_DESTINATION_FLAG_CURRENT;
	CHECK(Compile(&spec, &plan));
	for (index = 0U; index < 3U; index++)
	{
		observations[index] = Observation(70U, (uint32_t)index + 1U,
			(uint32_t)index + 10U, 1U, 100U,
			SG_STRATEGY_DESTINATION_REACHABLE, 10U + (uint32_t)index,
			1000U + (uint64_t)index);
		observations[index].handle.kind =
			spec.goals[index].choices[0].destination.kind;
	}
	Begin(&state, &plan, observations, 3U, &reduction);
	CHECK(state.activation.goal_id == 1U);
	for (index = 0U; index < 3U; index++)
	{
		frame = Frame((uint64_t)index + 2U, state.revision,
			110U + (uint64_t)index);
		BindTactical(&frame, &state, (uint64_t)index + 1U, 0U,
			SG_STRATEGY_BLOCK_NONE);
		frame.goal_outcome.present = 1U;
		frame.goal_outcome.activation = state.activation;
		frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
		CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
			SG_STRATEGY_REDUCE_APPLIED);
	}
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_COMPLETED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(state.goals[1].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(state.goals[2].phase == SG_STRATEGY_GOAL_SUCCEEDED);
}

static void TestUnavailableWaitAndTimeWindow(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 80U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].condition_count = 1U;
	spec.goals[0].conditions[0].kind = SG_STRATEGY_CONDITION_TIME_WINDOW;
	spec.goals[0].conditions[0].scope = SG_STRATEGY_CONDITION_START_ONLY;
	spec.goals[0].conditions[0].value.time.not_before_ms = 200U;
	spec.goals[0].conditions[0].value.time.not_after_ms = 300U;
	CHECK(Compile(&spec, &plan));
	observation = Observation(80U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_UNREACHABLE, SG_DESTINATION_FIELD_INF, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_CONDITION);
	CHECK(state.goals[0].attempt_count == 0U);
	frame = Frame(2U, state.revision, 200U);
	observation = Observation(80U, 1U, 10U, 2U, 200U,
		SG_STRATEGY_DESTINATION_UNREACHABLE, SG_DESTINATION_FIELD_INF, 1000U);
	frame.destinations = &observation;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind ==
		SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION);
	CHECK(state.goals[0].attempt_count == 0U);
	frame = Frame(3U, state.revision, 301U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_FAILED);
	CHECK(state.goals[0].last_failure == SG_STRATEGY_FAILURE_CONDITION_LOST);

	spec.plan_id = 81U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].unavailable = SG_STRATEGY_UNAVAILABLE_APPLY_FAILURE;
	spec.goals[0].failure.exhausted = SG_STRATEGY_FAILURE_SKIP_GOAL;
	CHECK(Compile(&spec, &plan));
	observation = Observation(81U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_UNREACHABLE, SG_DESTINATION_FIELD_INF, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_SKIPPED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_COMPLETED);
}

static void TestFactsSurviveReplacement(void)
{
	sg_strategy_plan_spec_t first_spec;
	sg_strategy_plan_spec_t second_spec;
	sg_strategy_plan_t first;
	sg_strategy_plan_t second;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_frame_t frame;
	sg_strategy_fact_observation_t fact;

	memset(&first_spec, 0, sizeof(first_spec));
	first_spec.plan_id = 90U;
	first_spec.goal_count = 1U;
	first_spec.goals[0] = Goal(1U);
	CHECK(Compile(&first_spec, &first));
	Begin(&state, &first, NULL, 0U, &reduction);

	memset(&fact, 0, sizeof(fact));
	fact.key.kind = SG_STRATEGY_FACT_CARRYING_FLAG;
	fact.key.team = 1U;
	fact.value = 1;
	fact.observation_revision = 1U;
	fact.observed_at_ms = 110U;
	fact.valid_until_ms = 500U;
	frame = Frame(2U, state.revision, 110U);
	frame.facts = &fact;
	frame.fact_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);

	memset(&second_spec, 0, sizeof(second_spec));
	second_spec.plan_id = 91U;
	second_spec.goal_count = 1U;
	second_spec.goals[0] = Goal(2U);
	second_spec.goals[0].condition_count = 1U;
	second_spec.goals[0].conditions[0].kind =
		SG_STRATEGY_CONDITION_FACT_EQUALS;
	second_spec.goals[0].conditions[0].scope =
		SG_STRATEGY_CONDITION_START_ONLY;
	second_spec.goals[0].conditions[0].value.fact.key = fact.key;
	second_spec.goals[0].conditions[0].value.fact.expected_value = 1;
	CHECK(Compile(&second_spec, &second));
	frame = Frame(3U, state.revision, 120U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.replacement = &second;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 5U, 2U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_COMPLETED);
	CHECK(state.fact_count == 1U);
}

static void TestSemanticObservationReplayIgnoresReservedBytes(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 92U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(92U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_DESTINATION_REACHABLE, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	observation.handle.reserved[0] = 0xa5U;
	observation.handle.reserved[1] = 0x5aU;
	observation.handle.reserved[2] = 0xffU;
	frame.destinations = &observation;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].choices[0].handle.reserved[0] == 0U);
}

int main(void)
{
	TestForwardDag();
	TestCycleRejected();
	TestCompilerFailuresAndCanonicalBytes();
	TestPolicyAndTieBreak();
	TestFixedPoint64AndOwnedPlan();
	TestCheapestMovingAndDuplicateRejects();
	TestSuspensionDeathAndRespawn();
	TestSuspensionExpiry();
	TestUnavailableAlternativeRetry();
	TestOutcomeAlternativeExhaustion();
	TestAuthorityCancelReleaseAndFactBoundary();
	TestWeaponArmorFlagChain();
	TestUnavailableWaitAndTimeWindow();
	TestFactsSurviveReplacement();
	TestSemanticObservationReplayIgnoresReservedBytes();
	if (failures != 0) {
		fprintf(stderr, "sg_strategy_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_strategy_test: ok");
	return 0;
}
