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

static sg_strategy_destination_observation_t Observation(uint64_t plan_id,
	uint32_t goal_id, uint32_t target_id, uint64_t revision, uint64_t at_ms,
	sg_strategy_field_state_t field_state, uint64_t cost_units,
	uint64_t semantic_id)
{
	sg_strategy_destination_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.plan_id = plan_id;
	observation.goal_id = goal_id;
	observation.target_id = target_id;
	observation.observation_revision = revision;
	observation.observed_at_ms = at_ms;
	observation.valid_until_ms = at_ms + 10000U;
	observation.field_state = field_state;
	observation.cost_to_go.units = cost_units;
	observation.target_generation = semantic_id;
	return observation;
}

static int Compile(const sg_strategy_plan_spec_t *spec,
	sg_strategy_plan_t *plan)
{
	sg_strategy_compile_error_t error;
	return SG_StrategyPlanCompile(spec, plan, &error);
}

static int HasEffect(const sg_strategy_reduction_t *reduction,
	sg_strategy_effect_kind_t kind)
{
	uint16_t index;

	for (index = 0U; index < reduction->effect_count; index++)
		if (reduction->effects[index].kind == kind)
			return 1;
	return 0;
}

static void Begin(sg_strategy_state_t *state, const sg_strategy_plan_t *plan,
	const sg_strategy_destination_observation_t *observations,
	uint16_t observation_count, sg_strategy_reduction_t *reduction)
{
	sg_strategy_frame_t frame;

	CHECK(SG_StrategyStateInit(state));
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

	spec.goals[0].failure.max_attempts_per_choice = 2U;
	spec.goals[0].failure.retry_wake.kind = SG_STRATEGY_RETRY_NEXT_FRAME;
	CHECK(Compile(&spec, &first));
	poisoned = spec;
	memset(&poisoned.goals[0].failure.retry_wake.fact, 0xa5,
		sizeof(poisoned.goals[0].failure.retry_wake.fact));
	CHECK(Compile(&poisoned, &second));
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
	CHECK(second.goals[0].failure.retry_wake.fact.kind ==
		SG_STRATEGY_FACT_ALIVE);
	CHECK(second.goals[0].failure.retry_wake.fact.subject_id == 0U);
	CHECK(second.goals[0].failure.retry_wake.fact.team == 0U);

	spec.goals[0].failure.retry_wake.kind =
		SG_STRATEGY_RETRY_FACT_REVISION;
	spec.goals[0].failure.retry_wake.fact.kind = SG_STRATEGY_FACT_CUSTOM;
	spec.goals[0].failure.retry_wake.fact.subject_id = 77U;
	spec.goals[0].failure.retry_wake.fact.team = 2U;
	CHECK(Compile(&spec, &first));
	CHECK(first.goals[0].failure.retry_wake.fact.kind ==
		SG_STRATEGY_FACT_CUSTOM);
	CHECK(first.goals[0].failure.retry_wake.fact.subject_id == 77U);
	CHECK(first.goals[0].failure.retry_wake.fact.team == 2U);
	CHECK(first.goals[0].failure.retry_wake.delay_ms == 0U);

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

static void TestInitPriorityAndTieBreak(void)
{
	sg_strategy_state_t state;
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];

	memset(&state, 0xa5, sizeof(state));
	CHECK(!SG_StrategyStateInit(NULL));
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
		SG_STRATEGY_FIELD_STEP, 40U, 1000U);
	observations[1] = Observation(15U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 40U, 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(reduction.instruction.choice_index == 0U);

	spec.plan_id = 16U;
	spec.goal_count = 2U;
	spec.goals[0] = DestinationGoal(1U, 20U, 200U);
	spec.goals[0].priority = 1;
	spec.goals[1] = DestinationGoal(2U, 21U, 201U);
	spec.goals[1].priority = 20;
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(16U, 1U, 20U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 2000U);
	observations[1] = Observation(16U, 2U, 21U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 20U, 2001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.activation.goal_id == 2U);
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
		SG_STRATEGY_FIELD_STEP,
		(UINT64_C(1) << 40) + UINT64_C(50), 1000U);
	observations[1] = Observation(30U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP,
		(UINT64_C(1) << 40) + UINT64_C(40), 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(reduction.instruction.choice_index == 1U);
	activation = state.activation.activation_id;

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	observations[0] = Observation(30U, 1U, 11U, 2U, 110U,
		SG_STRATEGY_FIELD_STEP,
		(UINT64_C(1) << 40) + UINT64_C(25), 2001U);
	frame.destinations = observations;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.activation.activation_id == activation);
	CHECK(reduction.instruction.target_id == 11U);
	CHECK(reduction.instruction.target_generation == 2001U);
	CHECK(reduction.instruction.cost_to_go.units ==
		(UINT64_C(1) << 40) + UINT64_C(25));

	before = state;
	frame = Frame(2U, 0U, 0U);
	frame.fact_count = UINT16_MAX;
	frame.facts = NULL;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_DUPLICATE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(reduction.instruction.target_generation == 2001U);

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
		SG_STRATEGY_FIELD_STEP, 20U, 3001U);
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

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 40U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(40U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	first_activation = state.activation.activation_id;

	frame = Frame(2U, state.revision, 120U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);
	CHECK(state.suspension.active == 1U);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_TACTICAL_SUSPENDED));

	frame = Frame(3U, state.revision, 150U);
	BindTactical(&frame, &state, 2U, 1U, SG_STRATEGY_BLOCK_OBSTRUCTION);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.suspension.active == 1U);
	CHECK(state.suspension.reason == SG_STRATEGY_BLOCK_OBSTRUCTION);

	frame = Frame(4U, state.revision, 160U);
	BindTactical(&frame, &state, 3U, 1U,
		SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);
	CHECK(state.suspension.reason == SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY);

	frame = Frame(5U, state.revision, 165U);
	BindTactical(&frame, &state, 4U, 0U, SG_STRATEGY_BLOCK_NONE);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_TACTICAL_RESUMED));

	frame = Frame(6U, state.revision, 170U);
	BindTactical(&frame, &state, 5U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.life.present = 1U;
	frame.life.alive = 0U;
	frame.life.observation_revision = 2U;
	frame.life.life_id = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_LIFE);
	CHECK(state.activation.activation_id == 0U);
	CHECK(state.goals[0].attempt_count == 1U);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_LIFE_RETIRED));

	frame = Frame(7U, state.revision, 180U);
	frame.life.present = 1U;
	frame.life.alive = 1U;
	frame.life.observation_revision = 3U;
	frame.life.life_id = 2U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.activation.activation_id != first_activation);
	CHECK(state.goals[0].attempt_count == 1U);
}

static void TestMissedDeathGenerationChange(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;
	uint64_t first_activation;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 42U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(42U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	first_activation = state.activation.activation_id;

	frame = Frame(2U, state.revision, 110U);
	frame.life.present = 1U;
	frame.life.alive = 1U;
	frame.life.observation_revision = 2U;
	frame.life.life_id = 2U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_LIFE_RETIRED));
	CHECK(state.life_id == 2U);
	CHECK(state.activation.activation_id != 0U);
	CHECK(state.activation.activation_id != first_activation);
	CHECK(state.goals[0].attempt_count == 1U);
	CHECK(state.goals[0].choices[0].attempts == 1U);
}

static void TestSuspensionPersistsUntilTacticsResume(void)
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
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	observation.valid_until_ms = 1000001U;
	Begin(&state, &plan, &observation, 1U, &reduction);
	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	frame = Frame(3U, state.revision, 210U);
	BindTactical(&frame, &state, 2U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);
	CHECK(state.activation.goal_id == 1U);

	frame = Frame(4U, state.revision, 1000000U);
	BindTactical(&frame, &state, 3U, 0U, SG_STRATEGY_BLOCK_NONE);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(state.activation.goal_id == 1U);
}

static void TestTerminalOutcomeSettlesBlockedActivation(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 411U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].kind = SG_STRATEGY_GOAL_COLLECT_ITEM;
	spec.goals[0].choices[0].destination.kind = SG_DESTINATION_WEAPON;
	spec.goals[0].choices[0].destination.value.item.item_id = 100U;
	CHECK(Compile(&spec, &plan));
	observation = Observation(411U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.suspension.active == 1U);

	frame = Frame(3U, state.revision, 120U);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(state.activation.activation_id == 0U);
	CHECK(state.suspension.active == 0U);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_GOAL_COMPLETED));

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 412U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(2U, 20U, 200U);
	spec.goals[0].kind = SG_STRATEGY_GOAL_CAPTURE_FLAG;
	spec.goals[0].choices[0].destination.kind = SG_DESTINATION_FLAG;
	spec.goals[0].choices[0].destination.value.flag.team = 2U;
	spec.goals[0].choices[0].destination.value.flag.location =
		SG_DESTINATION_FLAG_CURRENT;
	CHECK(Compile(&spec, &plan));
	observation = Observation(412U, 2U, 20U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 2000U);
	Begin(&state, &plan, &observation, 1U, &reduction);

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_OBSTRUCTION);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(state.activation.activation_id == 0U);
	CHECK(state.suspension.active == 0U);
	CHECK(!HasEffect(&reduction, SG_STRATEGY_EFFECT_TACTICAL_SUSPENDED));
}

static void TestTerminalOutcomeBindingAndDirectiveIsolation(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_state_t before;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 413U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(413U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);

	frame = Frame(2U, state.revision, 110U);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.activation.activation_id++;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
	before = state;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 1U, SG_STRATEGY_BLOCK_COMBAT);
	frame.tactical.activation.activation_id++;
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(2U, state.revision - 1U, 110U);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_STALE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	frame = Frame(2U, state.revision, 110U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_RELEASE;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_HUMAN,
		SG_STRATEGY_PRINCIPAL_HUMAN, 9U, 2U);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void TestCancelSettlesEveryOpenGoal(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 42U;
	spec.goal_count = 2U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[1] = DestinationGoal(2U, 11U, 101U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(42U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_ACTIVE);
	CHECK(state.goals[1].phase == SG_STRATEGY_GOAL_PENDING);

	frame = Frame(2U, state.revision, 110U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 3U, 2U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_CANCELLED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_CANCELLED);
	CHECK(state.goals[1].phase == SG_STRATEGY_GOAL_CANCELLED);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_PLAN_CANCELLED));
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_GOAL_CANCELLED));
}

static void TestCancelAtGoalCapacity(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;
	uint16_t index;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 43U;
	spec.goal_count = SG_STRATEGY_MAX_GOALS;
	for (index = 0U; index < spec.goal_count; index++)
		spec.goals[index] = DestinationGoal((uint32_t)index + 1U,
			(uint32_t)index + 100U, (uint64_t)index + 1000U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(43U, 1U, 100U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);

	frame = Frame(2U, state.revision, 110U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_EMERGENCY,
		SG_STRATEGY_PRINCIPAL_EMERGENCY, 3U, 2U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.effect_count == SG_STRATEGY_MAX_GOALS + 1U);
	CHECK(reduction.effect_count <= SG_STRATEGY_MAX_EFFECTS);
	for (index = 0U; index < spec.goal_count; index++)
		CHECK(state.goals[index].phase == SG_STRATEGY_GOAL_CANCELLED);
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
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 1U);
	observations[1] = Observation(50U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 2U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.goals[0].choices[0].attempts == 1U);
	CHECK(state.goals[0].choices[1].attempts == 0U);

	frame = Frame(2U, state.revision, 110U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.goals[0].choices[0].attempts == 1U);
	CHECK(state.goals[0].choices[1].attempts == 1U);

	frame = Frame(3U, state.revision, 120U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.goals[0].choices[0].attempts == 2U);
	CHECK(state.goals[0].choices[1].attempts == 1U);

	frame = Frame(4U, state.revision, 130U);
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
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	observations[1] = Observation(51U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 20U, 1001U);
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

static void TestRetryWakeGatesRepeatedChoice(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 52U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].failure.try_alternatives = 1U;
	spec.goals[0].failure.max_attempts_per_choice = 2U;
	spec.goals[0].failure.retry_wake.kind = SG_STRATEGY_RETRY_NEXT_FRAME;
	spec.goals[0].choice_count = 2U;
	spec.goals[0].choices[1].id = 11U;
	spec.goals[0].choices[1].destination = Waypoint(101U);
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(52U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	observations[1] = Observation(52U, 1U, 11U, 1U, 100U,
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.goals[0].choices[0].attempts == 1U);

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_FAILED;
	frame.goal_outcome.failure = SG_STRATEGY_FAILURE_OBSTRUCTED;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.goals[0].choices[0].attempts == 1U);
	CHECK(state.activation.activation_id == 0U);
}

static void TestZeroDelayNotBeforeRejected(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_plan_t before;
	sg_strategy_compile_error_t error;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 54U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].failure.max_attempts_per_choice = 2U;
	spec.goals[0].failure.retry_wake.kind =
		SG_STRATEGY_RETRY_NOT_BEFORE;
	memset(&plan, 0xa5, sizeof(plan));
	before = plan;
	CHECK(!SG_StrategyPlanCompile(&spec, &plan, &error));
	CHECK(error.code == SG_STRATEGY_COMPILE_INVALID_GOAL);
	CHECK(memcmp(&plan, &before, sizeof(plan)) == 0);
}

static void TestTargetRevisionWakeIsPerChoice(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 55U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].unavailable = SG_STRATEGY_UNAVAILABLE_APPLY_FAILURE;
	spec.goals[0].failure.try_alternatives = 1U;
	spec.goals[0].failure.max_attempts_per_choice = 2U;
	spec.goals[0].failure.retry_wake.kind =
		SG_STRATEGY_RETRY_TARGET_REVISION;
	spec.goals[0].choice_count = 2U;
	spec.goals[0].choices[1].id = 11U;
	spec.goals[0].choices[1].destination = Waypoint(101U);
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(55U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE,
		1000U);
	observations[1] = Observation(55U, 1U, 11U, 100U, 100U,
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE,
		1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.goals[0].retry.target_baseline_revisions[0] == 1U);
	CHECK(state.goals[0].retry.target_baseline_revisions[1] == 100U);

	observations[0] = Observation(55U, 1U, 10U, 2U, 110U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	frame = Frame(2U, state.revision, 110U);
	frame.destinations = observations;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_ACTIVE);
	CHECK(state.goals[0].selected_choice == 0U);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
}

static void TestUnobservedAlternativeCannotBypassRetry(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 56U;
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
	observation = Observation(56U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	CHECK(state.goals[0].selected_choice == 0U);
	CHECK(state.goals[0].choices[0].attempts == 1U);
	CHECK(state.goals[0].choices[1].attempts == 0U);

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = state.activation;
	frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_FAILED;
	frame.goal_outcome.failure = SG_STRATEGY_FAILURE_UNAVAILABLE;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_RETRY_WAIT);
	CHECK(state.activation.activation_id == 0U);
	CHECK(state.goals[0].choices[0].attempts == 1U);
	CHECK(state.goals[0].choices[1].attempts == 0U);

	frame = Frame(3U, state.revision, 120U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_ACTIVE);
	CHECK(state.goals[0].selected_choice == 0U);
	CHECK(state.goals[0].choices[0].attempts == 2U);
}

static void TestStaleTargetIsNotReissued(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 53U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&spec, &plan));
	observation = Observation(53U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	observation.valid_until_ms = 105U;
	Begin(&state, &plan, &observation, 1U, &reduction);

	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind ==
		SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION);
	CHECK(reduction.instruction.target_id == 10U);
	CHECK(reduction.instruction.cost_to_go.units ==
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE);
	CHECK(reduction.instruction.destination_wait_reason ==
		SG_STRATEGY_DESTINATION_WAIT_STALE);
	CHECK(state.activation.goal_id == 1U);
}

static void TestGoalKindRejectsWrongDestinationType(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 54U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].kind = SG_STRATEGY_GOAL_ESCORT_CARRIER;
	CHECK(!Compile(&spec, &plan));

	spec.goals[0].kind = SG_STRATEGY_GOAL_COLLECT_ITEM;
	CHECK(!Compile(&spec, &plan));
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
			SG_STRATEGY_FIELD_STEP, 10U + (uint32_t)index,
			1000U + (uint64_t)index);
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
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_CONDITION);
	CHECK(state.goals[0].attempt_count == 0U);
	frame = Frame(2U, state.revision, 200U);
	observation = Observation(80U, 1U, 10U, 2U, 200U,
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 1000U);
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
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 1000U);
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

static void TestSemanticObservationRejectsInvalidFieldState(void)
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
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	observation.field_state = SG_STRATEGY_FIELD_STATE_COUNT;
	frame.destinations = &observation;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_REJECTED_INVALID);
	CHECK(state.goals[0].choices[0].field_state == SG_STRATEGY_FIELD_STEP);
}

static void TestEscortRecoveryAndTimedPowerupPlan(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[3];
	sg_strategy_frame_t frame;
	uint16_t index;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 93U;
	spec.goal_count = 3U;
	for (index = 0U; index < spec.goal_count; index++)
	{
		spec.goals[index] = DestinationGoal((uint32_t)index + 1U,
			(uint32_t)index + 10U, (uint64_t)index + 100U);
		spec.goals[index].priority = (int16_t)(30 - (int16_t)index * 10);
		if (index != 0U)
		{
			spec.goals[index].dependency_count = 1U;
			spec.goals[index].dependencies[0].goal_id = (uint32_t)index;
			spec.goals[index].dependencies[0].accept =
				SG_STRATEGY_DEPENDENCY_SUCCESS;
		}
	}
	spec.goals[0].kind = SG_STRATEGY_GOAL_COLLECT_ITEM;
	spec.goals[0].choices[0].destination.kind = SG_DESTINATION_POWERUP;
	spec.goals[0].choices[0].destination.value.item.item_id = 100U;
	spec.goals[0].condition_count = 1U;
	spec.goals[0].conditions[0].kind = SG_STRATEGY_CONDITION_TIME_WINDOW;
	spec.goals[0].conditions[0].scope = SG_STRATEGY_CONDITION_START_ONLY;
	spec.goals[0].conditions[0].value.time.not_before_ms = 200U;
	spec.goals[0].conditions[0].value.time.not_after_ms = 250U;
	spec.goals[1].kind = SG_STRATEGY_GOAL_RECOVER_FLAG;
	memset(&spec.goals[1].choices[0].destination, 0,
		sizeof(spec.goals[1].choices[0].destination));
	spec.goals[1].choices[0].destination.kind = SG_DESTINATION_FLAG;
	spec.goals[1].choices[0].destination.value.flag.team = 1U;
	spec.goals[1].choices[0].destination.value.flag.location =
		SG_DESTINATION_FLAG_CURRENT;
	spec.goals[2].kind = SG_STRATEGY_GOAL_ESCORT_CARRIER;
	memset(&spec.goals[2].choices[0].destination, 0,
		sizeof(spec.goals[2].choices[0].destination));
	spec.goals[2].choices[0].destination.kind = SG_DESTINATION_ESCORT;
	spec.goals[2].choices[0].destination.value.carrier.client_id = 7U;
	spec.goals[2].choices[0].destination.value.carrier.team = 1U;
	spec.goals[2].choices[0].destination.value.carrier.selector =
		SG_DESTINATION_CARRIER_EXACT;
	CHECK(Compile(&spec, &plan));
	for (index = 0U; index < spec.goal_count; index++)
	{
		observations[index] = Observation(93U, (uint32_t)index + 1U,
			(uint32_t)index + 10U, 1U, 100U,
			SG_STRATEGY_FIELD_STEP,
			10U + (uint32_t)index, 1000U + (uint64_t)index);
	}
	Begin(&state, &plan, observations, 3U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_CONDITION);

	frame = Frame(2U, state.revision, 200U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.activation.goal_id == 1U);
	for (index = 0U; index < spec.goal_count; index++)
	{
		frame = Frame((uint64_t)index + 3U, state.revision,
			210U + (uint64_t)index);
		BindTactical(&frame, &state, (uint64_t)index + 1U, 0U,
			SG_STRATEGY_BLOCK_NONE);
		frame.goal_outcome.present = 1U;
		frame.goal_outcome.activation = state.activation;
		frame.goal_outcome.kind = SG_STRATEGY_OUTCOME_COMPLETED;
		CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
			SG_STRATEGY_REDUCE_APPLIED);
	}
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_COMPLETED);
}

static void TestRoleChangeAndHumanOrderAuthority(void)
{
	sg_strategy_plan_spec_t first_spec;
	sg_strategy_plan_spec_t second_spec;
	sg_strategy_plan_t first;
	sg_strategy_plan_t second;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	memset(&first_spec, 0, sizeof(first_spec));
	first_spec.plan_id = 94U;
	first_spec.goal_count = 1U;
	first_spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	CHECK(Compile(&first_spec, &first));
	CHECK(SG_StrategyStateInit(&state));
	observation = Observation(94U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	frame = Frame(1U, state.revision, 100U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U, 1U);
	frame.directive.replacement = &first;
	frame.life.present = 1U;
	frame.life.alive = 1U;
	frame.life.observation_revision = 1U;
	frame.life.life_id = 1U;
	frame.destinations = &observation;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);

	memset(&second_spec, 0, sizeof(second_spec));
	second_spec.plan_id = 95U;
	second_spec.goal_count = 1U;
	second_spec.goals[0] = DestinationGoal(2U, 20U, 200U);
	CHECK(Compile(&second_spec, &second));
	observation = Observation(95U, 2U, 20U, 1U, 110U,
		SG_STRATEGY_FIELD_STEP, 20U, 2000U);
	frame = Frame(2U, state.revision, 110U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_TEAM,
		SG_STRATEGY_PRINCIPAL_TEAM, 2U, 2U);
	frame.directive.replacement = &second;
	frame.destinations = &observation;
	frame.destination_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.plan.plan_id == 95U);
	CHECK(state.authority.principal.kind == SG_STRATEGY_PRINCIPAL_TEAM);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_PLAN_REPLACED));

	frame = Frame(3U, state.revision, 120U);
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp = Authority(SG_STRATEGY_AUTHORITY_HUMAN,
		SG_STRATEGY_PRINCIPAL_HUMAN, 9U, 3U);
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_CANCELLED);
	CHECK(state.authority.principal.kind == SG_STRATEGY_PRINCIPAL_HUMAN);
	CHECK(HasEffect(&reduction, SG_STRATEGY_EFFECT_PLAN_CANCELLED));
}

static void TestUnavailablePrerequisiteFailsDependentGoal(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observations[2];

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 96U;
	spec.goal_count = 2U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].unavailable = SG_STRATEGY_UNAVAILABLE_APPLY_FAILURE;
	spec.goals[0].failure.exhausted = SG_STRATEGY_FAILURE_SKIP_GOAL;
	spec.goals[1] = DestinationGoal(2U, 11U, 101U);
	spec.goals[1].dependency_count = 1U;
	spec.goals[1].dependencies[0].goal_id = 1U;
	spec.goals[1].dependencies[0].accept = SG_STRATEGY_DEPENDENCY_SUCCESS;
	CHECK(Compile(&spec, &plan));
	observations[0] = Observation(96U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE, 1000U);
	observations[1] = Observation(96U, 2U, 11U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1001U);
	Begin(&state, &plan, observations, 2U, &reduction);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_SKIPPED);
	CHECK(state.goals[0].last_failure == SG_STRATEGY_FAILURE_UNAVAILABLE);
	CHECK(state.goals[1].phase == SG_STRATEGY_GOAL_FAILED);
	CHECK(state.goals[1].last_failure == SG_STRATEGY_FAILURE_DEPENDENCY);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_FAILED);
}

static void TestItemLossRetiresActiveGoal(void)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_state_t state;
	sg_strategy_reduction_t reduction;
	sg_strategy_destination_observation_t observation;
	sg_strategy_fact_observation_t fact;
	sg_strategy_frame_t frame;

	memset(&spec, 0, sizeof(spec));
	spec.plan_id = 97U;
	spec.goal_count = 1U;
	spec.goals[0] = DestinationGoal(1U, 10U, 100U);
	spec.goals[0].condition_count = 1U;
	spec.goals[0].conditions[0].kind = SG_STRATEGY_CONDITION_FACT_EQUALS;
	spec.goals[0].conditions[0].scope = SG_STRATEGY_CONDITION_WHILE_ACTIVE;
	spec.goals[0].conditions[0].value.fact.key.kind =
		SG_STRATEGY_FACT_ITEM_OWNED;
	spec.goals[0].conditions[0].value.fact.key.subject_id = 100U;
	spec.goals[0].conditions[0].value.fact.expected_value = 1;
	CHECK(Compile(&spec, &plan));
	observation = Observation(97U, 1U, 10U, 1U, 100U,
		SG_STRATEGY_FIELD_STEP, 10U, 1000U);
	Begin(&state, &plan, &observation, 1U, &reduction);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_CONDITION);

	memset(&fact, 0, sizeof(fact));
	fact.key.kind = SG_STRATEGY_FACT_ITEM_OWNED;
	fact.key.subject_id = 100U;
	fact.value = 1;
	fact.observation_revision = 1U;
	fact.observed_at_ms = 110U;
	fact.valid_until_ms = 200U;
	frame = Frame(2U, state.revision, 110U);
	BindTactical(&frame, &state, 1U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.facts = &fact;
	frame.fact_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(reduction.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);

	fact.value = 0;
	fact.observation_revision = 2U;
	fact.observed_at_ms = 120U;
	frame = Frame(3U, state.revision, 120U);
	BindTactical(&frame, &state, 2U, 0U, SG_STRATEGY_BLOCK_NONE);
	frame.facts = &fact;
	frame.fact_count = 1U;
	CHECK(SG_StrategyReduce(&state, &frame, &reduction) ==
		SG_STRATEGY_REDUCE_APPLIED);
	CHECK(state.goals[0].phase == SG_STRATEGY_GOAL_FAILED);
	CHECK(state.goals[0].last_failure ==
		SG_STRATEGY_FAILURE_CONDITION_LOST);
}

static void TestAllCompactFieldStatesRemainTyped(void)
{
	static const sg_strategy_field_state_t states[] = {
		SG_STRATEGY_FIELD_DISCONNECTED,
		SG_STRATEGY_FIELD_LOCAL_DESTINATION,
		SG_STRATEGY_FIELD_CELL_DESTINATION,
		SG_STRATEGY_FIELD_MECHANISMS_REQUIRED,
		SG_STRATEGY_FIELD_BLOCKED_NOW,
		SG_STRATEGY_FIELD_STEP
	};
	uint32_t index;

	for (index = 0U; index < sizeof(states) / sizeof(states[0]); index++)
	{
		sg_strategy_plan_spec_t spec;
		sg_strategy_plan_t plan;
		sg_strategy_state_t state;
		sg_strategy_reduction_t reduction;
		sg_strategy_destination_observation_t observation;
		uint64_t cost = SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;

		if (states[index] == SG_STRATEGY_FIELD_LOCAL_DESTINATION ||
		    states[index] == SG_STRATEGY_FIELD_CELL_DESTINATION)
			cost = 0U;
		else if (states[index] == SG_STRATEGY_FIELD_STEP)
			cost = UINT64_C(4097);
		memset(&spec, 0, sizeof(spec));
		spec.plan_id = UINT64_C(200) + index;
		spec.goal_count = 1U;
		spec.goals[0] = DestinationGoal(1U, 10U, 100U);
		CHECK(Compile(&spec, &plan));
		observation = Observation(spec.plan_id, 1U, 10U, 1U, 100U,
			states[index], cost, UINT64_C(500) + index);
		Begin(&state, &plan, &observation, 1U, &reduction);
		CHECK(state.goals[0].choices[0].field_state == states[index]);
		CHECK(state.goals[0].choices[0].cost_to_go.units == cost);
		if (states[index] == SG_STRATEGY_FIELD_LOCAL_DESTINATION ||
		    states[index] == SG_STRATEGY_FIELD_CELL_DESTINATION ||
		    states[index] == SG_STRATEGY_FIELD_STEP)
		{
			CHECK(reduction.instruction.kind ==
				SG_STRATEGY_INSTRUCTION_EXECUTE);
			CHECK(reduction.instruction.field_state == states[index]);
			CHECK(reduction.instruction.target_generation ==
				UINT64_C(500) + index);
		}
		else if (states[index] == SG_STRATEGY_FIELD_MECHANISMS_REQUIRED ||
			 states[index] == SG_STRATEGY_FIELD_BLOCKED_NOW)
		{
			CHECK(reduction.instruction.kind ==
				SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION);
			CHECK(reduction.instruction.field_state == states[index]);
		}
	}
}

int main(void)
{
	TestForwardDag();
	TestCycleRejected();
	TestCompilerFailuresAndCanonicalBytes();
	TestInitPriorityAndTieBreak();
	TestFixedPoint64AndOwnedPlan();
	TestCheapestMovingAndDuplicateRejects();
	TestSuspensionDeathAndRespawn();
	TestMissedDeathGenerationChange();
	TestSuspensionPersistsUntilTacticsResume();
	TestTerminalOutcomeSettlesBlockedActivation();
	TestTerminalOutcomeBindingAndDirectiveIsolation();
	TestCancelSettlesEveryOpenGoal();
	TestCancelAtGoalCapacity();
	TestUnavailableAlternativeRetry();
	TestOutcomeAlternativeExhaustion();
	TestRetryWakeGatesRepeatedChoice();
	TestZeroDelayNotBeforeRejected();
	TestTargetRevisionWakeIsPerChoice();
	TestUnobservedAlternativeCannotBypassRetry();
	TestStaleTargetIsNotReissued();
	TestGoalKindRejectsWrongDestinationType();
	TestAuthorityCancelReleaseAndFactBoundary();
	TestWeaponArmorFlagChain();
	TestUnavailableWaitAndTimeWindow();
	TestFactsSurviveReplacement();
	TestSemanticObservationRejectsInvalidFieldState();
	TestEscortRecoveryAndTimedPowerupPlan();
	TestRoleChangeAndHumanOrderAuthority();
	TestUnavailablePrerequisiteFailsDependentGoal();
	TestItemLossRetiresActiveGoal();
	TestAllCompactFieldStatesRemainTyped();
	if (failures != 0) {
		fprintf(stderr, "sg_strategy_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_strategy_test: ok");
	return 0;
}
