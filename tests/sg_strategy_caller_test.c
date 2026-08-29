#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_cell_phase_localization.h"
#include "slipgate/sg_strategy_caller.h"
#include "slipgate/sg_strategy_runtime_bridge.h"

static int failures;
static int railgun_field[2] = { 900, 0 };
static int armor_field[2] = { 800, 0 };
static int refreshed_armor_field[2] = { 801, 0 };
static int capture_field[2] = { 700, 0 };
static int fallback_field[2] = { 500, 0 };

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct caller_fixture_s
{
	sg_rune_cell_t cells[1];
	sg_rune_phase_basis_t model_phases[1];
	sg_phase_coordinate_t phases[1];
	sg_rune_model_t model;
	sg_rune_runtime_snapshot_t snapshot;
	sg_field_sample_t samples[1];
	sg_destination_field_t fields[4];
	sg_localized_player_state_t localized;
} caller_fixture_t;

static sg_destination_handle_t Handle(sg_destination_kind_t kind,
	uint64_t id)
{
	return (sg_destination_handle_t){
		.id = id,
		.generation = 1U,
		.kind = kind,
		.motion = SG_DESTINATION_STATIC,
		.valid = 1U,
		.pose = {
			.phase = { 0U, 0U },
			.position = { 1.0f, 2.0f, 3.0f }
		}
	};
}

static void InitFixture(caller_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->phases[0] = (sg_phase_coordinate_t){ 0U, 0U };
	fixture->model = (sg_rune_model_t){
		.version = SG_RUNE_MODEL_VERSION,
		.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG,
		.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
			SG_RUNE_MODEL_NO_RUNTIME_ACTORS,
		.completeness = {
			.state = SG_RUNE_COMPLETENESS_COMPLETE,
			.expected_cells = 1U,
			.covered_cells = 1U
		},
		.cells = fixture->cells,
		.cell_count = 1U,
		.phases = fixture->model_phases,
		.phase_count = 1U
	};
	fixture->snapshot = (sg_rune_runtime_snapshot_t){
		.identity = 99U,
		.topology_revision = 7U,
		.cell_count = 1U,
		.phase_count = 1U,
		.model = &fixture->model,
		.phases = fixture->phases
	};
	fixture->samples[0] = (sg_field_sample_t){
		.phase = { 0U, 0U },
		.next_phase = { 0U, 0U },
		.cost_ms = 0U,
		.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE,
		.finite = 1U
	};
	fixture->localized = (sg_localized_player_state_t){
		.field_pose = {
			.phase = { 0U, 0U },
			.position = { 0.0f, 0.0f, 0.0f }
		},
		.rune_identity = fixture->snapshot.identity,
		.topology_revision = fixture->snapshot.topology_revision,
		.frame_sequence = 1U,
		.localized_at_ms = 100U
	};
	fixture->fields[0] = (sg_destination_field_t){
		.rune_identity = fixture->snapshot.identity,
		.topology_revision = fixture->snapshot.topology_revision,
		.generation = 1U,
		.computed_at_ms = 100U,
		.destination = Handle(SG_DESTINATION_WEAPON, 101U),
		.samples = fixture->samples,
		.sample_count = 1U,
		.complete = 1U
	};
	fixture->fields[1] = fixture->fields[0];
	fixture->fields[1].destination = Handle(SG_DESTINATION_ARMOR, 102U);
	fixture->fields[2] = fixture->fields[0];
	fixture->fields[2].destination = Handle(SG_DESTINATION_FLAG, 201U);
	fixture->fields[3] = fixture->fields[0];
	fixture->fields[3].destination = Handle(SG_DESTINATION_FLAG, 202U);
}

static void InitGoal(sg_strategy_goal_spec_t *goal, sg_strategy_goal_id_t id,
	sg_strategy_goal_kind_t kind, int16_t priority)
{
	memset(goal, 0, sizeof(*goal));
	goal->id = id;
	goal->kind = kind;
	goal->priority = priority;
	goal->unavailable = SG_STRATEGY_UNAVAILABLE_WAIT;
	goal->failure.try_alternatives = 1U;
	goal->failure.max_attempts_per_choice = UINT8_MAX;
	goal->failure.retry_wake.kind = SG_STRATEGY_RETRY_TARGET_REVISION;
	goal->failure.exhausted = SG_STRATEGY_FAILURE_SKIP_GOAL;
}

static void AddBinding(sg_strategy_caller_plan_t *plan, uint16_t index,
	sg_strategy_goal_id_t goal_id, sg_strategy_target_id_t target_id,
	const sg_destination_ref_t *destination, int role,
	const int *execution_field, const caller_fixture_t *fixture,
	const sg_destination_field_t *field)
{
	plan->bindings[index] = (sg_strategy_caller_target_binding_t){
		.commitment_id = plan->commitment_id,
		.authority = plan->authority,
		.goal_id = goal_id,
		.target_id = target_id,
		.destination = *destination,
		.role = role,
		.execution_field = execution_field,
		.snapshot = &fixture->snapshot,
		.field = field,
		.localized = &fixture->localized,
		.observation_revision = 1U,
		.pose_revision = 1U,
		.valid_until_ms = UINT64_MAX
	};
}

static sg_strategy_caller_plan_t Plan(const caller_fixture_t *fixture,
	uint64_t commitment, sg_strategy_authority_rank_t rank,
	sg_strategy_principal_kind_t principal, uint32_t principal_id)
{
	sg_strategy_caller_plan_t plan;
	sg_strategy_goal_spec_t *railgun;
	sg_strategy_goal_spec_t *armor;
	sg_strategy_goal_spec_t *primary;

	memset(&plan, 0, sizeof(plan));
	plan.commitment_id = commitment;
	plan.authority = (sg_strategy_caller_authority_t){
		.rank = rank,
		.principal_kind = principal,
		.principal_id = principal_id
	};
	railgun = &plan.spec.goals[0];
	InitGoal(railgun, 1U, SG_STRATEGY_GOAL_COLLECT_ITEM, INT16_C(30));
	railgun->failure.try_alternatives = 0U;
	railgun->choice_count = 1U;
	railgun->choices[0] = (sg_strategy_target_choice_t){
		.id = 1U,
		.destination = {
			.kind = SG_DESTINATION_WEAPON,
			.value.item = { 101U }
		}
	};
	armor = &plan.spec.goals[1];
	InitGoal(armor, 2U, SG_STRATEGY_GOAL_COLLECT_ITEM, INT16_C(20));
	armor->failure.try_alternatives = 0U;
	armor->dependency_count = 1U;
	armor->dependencies[0] = (sg_strategy_dependency_spec_t){
		.goal_id = 1U,
		.accept = SG_STRATEGY_DEPENDENCY_SETTLED
	};
	armor->choice_count = 1U;
	armor->choices[0] = (sg_strategy_target_choice_t){
		.id = 2U,
		.destination = {
			.kind = SG_DESTINATION_ARMOR,
			.value.item = { 102U }
		}
	};
	primary = &plan.spec.goals[2];
	InitGoal(primary, 3U, SG_STRATEGY_GOAL_CAPTURE_FLAG, INT16_C(10));
	primary->dependency_count = 1U;
	primary->dependencies[0] = (sg_strategy_dependency_spec_t){
		.goal_id = 2U,
		.accept = SG_STRATEGY_DEPENDENCY_SETTLED
	};
	primary->choice_count = 2U;
	primary->choices[0] = (sg_strategy_target_choice_t){
		.id = 3U,
		.destination = {
			.kind = SG_DESTINATION_FLAG,
			.value.flag = { 2U, SG_DESTINATION_FLAG_CURRENT, 0U }
		}
	};
	primary->choices[1] = (sg_strategy_target_choice_t){
		.id = 4U,
		.destination = {
			.kind = SG_DESTINATION_FLAG,
			.value.flag = { 2U, SG_DESTINATION_FLAG_HOME, 0U }
		}
	};
	plan.spec.goal_count = 3U;
	plan.binding_count = 4U;
	AddBinding(&plan, 0U, 1U, 1U, &railgun->choices[0].destination, 4,
		railgun_field, fixture,
		&fixture->fields[0]);
	AddBinding(&plan, 1U, 2U, 2U, &armor->choices[0].destination, 5,
		armor_field, fixture,
		&fixture->fields[1]);
	AddBinding(&plan, 2U, 3U, 3U, &primary->choices[0].destination, 6,
		capture_field, fixture,
		&fixture->fields[2]);
	AddBinding(&plan, 3U, 3U, 4U, &primary->choices[1].destination, 7,
		fallback_field, fixture, &fixture->fields[3]);
	return plan;
}

static sg_strategy_runtime_plan_request_t RuntimeRequest(
	const sg_strategy_caller_plan_t *plan)
{
	sg_strategy_runtime_plan_request_t request;
	uint16_t index;

	memset(&request, 0, sizeof(request));
	request.commitment_id = plan->commitment_id;
	request.authority = plan->authority;
	request.spec = plan->spec;
	request.execution_count = plan->binding_count;
	for (index = 0U; index < plan->binding_count; index++)
		request.executions[index] = (sg_strategy_runtime_execution_t){
			.goal_id = plan->bindings[index].goal_id,
			.target_id = plan->bindings[index].target_id,
			.role = plan->bindings[index].role,
			.execution_field = plan->bindings[index].execution_field
		};
	return request;
}

static int RuntimeProvider(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_caller_target_binding_t *binding_out)
{
	const sg_strategy_caller_plan_t *plan = context;
	uint16_t index;

	for (index = 0U; index < plan->binding_count; index++)
		if (plan->bindings[index].goal_id == request->goal_id &&
		    plan->bindings[index].target_id == request->target_id)
		{
			*binding_out = plan->bindings[index];
			return 1;
		}
	return 0;
}

static void TestQueuedPlanAdvancesAcrossGoals(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t plan;
	uint64_t plan_id;
	uint64_t history;

	InitFixture(&fixture);
	plan = Plan(&fixture, 10U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.instruction.goal_id == 1U);
	CHECK(output.execution_field == railgun_field);
	CHECK(caller.reducer.plan.goal_count == 3U);
	CHECK(caller.reducer.plan.goals[2].choice_count == 2U);
	CHECK(SG_StrategyCallerAdvance(&caller, 1U,
		SG_STRATEGY_OUTCOME_COMPLETED, SG_STRATEGY_FAILURE_NONE, 110U,
		&output));
	CHECK(caller.reducer.goals[0].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.instruction.goal_id == 2U);
	CHECK(output.execution_field == armor_field);
	plan_id = output.plan_id;
	history = caller.reducer.history_sequence;
	plan.bindings[1].execution_field = refreshed_armor_field;
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 115U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.plan_id == plan_id);
	CHECK(caller.reducer.goals[0].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(caller.reducer.history_sequence == history);
	CHECK(output.execution_field == refreshed_armor_field);
	CHECK(SG_StrategyCallerAdvance(&caller, 1U,
		SG_STRATEGY_OUTCOME_COMPLETED, SG_STRATEGY_FAILURE_NONE, 120U,
		&output));
	CHECK(caller.reducer.goals[1].phase == SG_STRATEGY_GOAL_SUCCEEDED);
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.instruction.goal_id == 3U);
	CHECK(output.execution_field == capture_field);
}

static void TestQueuedPlanPersistsFailureAdvance(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t plan;
	uint64_t plan_id;

	InitFixture(&fixture);
	plan = Plan(&fixture, 11U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	/* An exhausted intermediate prerequisite is settled as skipped, so the
	 * primary remains queued rather than being reconstructed from role policy. */
	plan.spec.goals[1].failure.max_attempts_per_choice = 1U;
	plan.spec.goals[1].failure.retry_wake.kind = SG_STRATEGY_RETRY_NONE;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	plan_id = output.plan_id;
	CHECK(SG_StrategyCallerSettle(&caller, 1U,
		SG_STRATEGY_OUTCOME_COMPLETED, SG_STRATEGY_FAILURE_NONE, 110U,
		&output));
	CHECK(output.instruction.goal_id == 2U);
	CHECK(SG_StrategyCallerAdvance(&caller, 1U,
		SG_STRATEGY_OUTCOME_FAILED, SG_STRATEGY_FAILURE_UNAVAILABLE, 120U,
		&output));
	CHECK(caller.reducer.goals[1].phase == SG_STRATEGY_GOAL_SKIPPED);
	CHECK(output.plan_id == plan_id);
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.instruction.goal_id == 3U);
	CHECK(output.execution_field == capture_field);
	CHECK(SG_StrategyCallerAdvance(&caller, 1U,
		SG_STRATEGY_OUTCOME_FAILED, SG_STRATEGY_FAILURE_UNAVAILABLE, 130U,
		&output));
	CHECK(output.plan_id == plan_id);
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.instruction.goal_id == 3U);
	CHECK(output.instruction.choice_index == 1U);
	CHECK(output.execution_field == fallback_field);
}

static void TestSuspendedTerminalAdvanceAndAuthenticatedSources(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t plan;

	InitFixture(&fixture);
	plan = Plan(&fixture, 20U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 100U,
		SG_STRATEGY_BLOCK_COMBAT, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);
	CHECK(SG_StrategyCallerSettle(&caller, 1U,
		SG_STRATEGY_OUTCOME_COMPLETED, SG_STRATEGY_FAILURE_NONE, 110U,
		&output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.instruction.goal_id == 2U);
	CHECK(output.snapshot == &fixture.snapshot);
	CHECK(output.field == &fixture.fields[1]);
	CHECK(output.localized == &fixture.localized);

	plan = Plan(&fixture, 21U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	plan.bindings[0].snapshot = NULL;
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U, 120U,
		SG_STRATEGY_BLOCK_NONE, &output));
}

static void TestCallerRejectsSameKindDestinationForgery(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t plan;

	InitFixture(&fixture);
	plan = Plan(&fixture, 22U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	/* FLAG/CURRENT and FLAG/HOME share a kind but not an authority identity. */
	plan.bindings[2].destination.value.flag.location =
		SG_DESTINATION_FLAG_HOME;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
}

static void TestLowerAuthorityCannotReleaseHumanPlan(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t autonomous;
	sg_strategy_caller_plan_t human;
	uint64_t human_plan_id;
	uint64_t history;

	InitFixture(&fixture);
	autonomous = Plan(&fixture, 30U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	human = Plan(&fixture, 31U, SG_STRATEGY_AUTHORITY_HUMAN,
		SG_STRATEGY_PRINCIPAL_HUMAN, 7U);
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &human, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	human_plan_id = output.plan_id;
	history = caller.reducer.history_sequence;
	CHECK(SG_StrategyCallerSubmit(&caller, &autonomous, 1U, 110U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.plan_id == human_plan_id);
	CHECK(caller.reducer.authority.rank == SG_STRATEGY_AUTHORITY_HUMAN);
	CHECK(caller.reducer.authority.principal.kind ==
		SG_STRATEGY_PRINCIPAL_HUMAN);
	CHECK(caller.reducer.authority.principal.id == 7U);
	CHECK(caller.reducer.history_sequence == history);
	CHECK(!SG_StrategyCallerCancel(&caller, &autonomous.authority, 1U,
		120U, &output));
	CHECK(!SG_StrategyCallerRelease(&caller, &autonomous.authority, 1U,
		120U, &output));
	CHECK(SG_StrategyCallerCancel(&caller, &human.authority, 1U, 120U,
		&output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_CANCELLED);
	CHECK(caller.reducer.cancelled == 1U);
	CHECK(SG_StrategyCallerRelease(&caller, &human.authority, 1U, 120U,
		&output));
	CHECK(caller.reducer.authority.principal.kind == SG_STRATEGY_PRINCIPAL_NONE);
	CHECK(SG_StrategyCallerSubmit(&caller, &autonomous, 1U, 130U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(caller.reducer.authority.rank == SG_STRATEGY_AUTHORITY_AUTONOMOUS);
}

static void TestRuntimeResolverFailsClosedWithoutAuthenticatedProvider(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_runtime_plan_request_t request;

	InitFixture(&fixture);
	plan = Plan(&fixture, 40U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&plan);
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &plan);
	CHECK(SG_StrategyRuntimeTargetProviderAvailable());
	CHECK(SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(resolved.bindings[0].snapshot == &fixture.snapshot);
	plan.bindings[2].destination.value.flag.location =
		SG_DESTINATION_FLAG_HOME;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	plan.bindings[2].destination.value.flag.location =
		SG_DESTINATION_FLAG_CURRENT;
	plan.bindings[0].commitment_id++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	plan.bindings[0].commitment_id--;
	plan.bindings[0].authority.principal_id++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	plan.bindings[0].authority.principal_id--;
	plan.bindings[0].role++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	plan.bindings[0].role--;
	request.executions[0].execution_field = fallback_field;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

int main(void)
{
	TestQueuedPlanAdvancesAcrossGoals();
	TestQueuedPlanPersistsFailureAdvance();
	TestSuspendedTerminalAdvanceAndAuthenticatedSources();
	TestCallerRejectsSameKindDestinationForgery();
	TestLowerAuthorityCannotReleaseHumanPlan();
	TestRuntimeResolverFailsClosedWithoutAuthenticatedProvider();
	if (failures)
	{
		fprintf(stderr, "sg_strategy_caller_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_strategy_caller_test: ok");
	return 0;
}
