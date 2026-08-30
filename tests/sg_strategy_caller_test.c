#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
	sg_destination_terminal_t terminals[4];
	sg_field_handle_t field_handles[4];
	sg_field_guidance_t guidances[4];
	sg_localized_field_state_t localized;
	int accepted_views[4];
} caller_fixture_t;

static sg_rune_stable_id_t Stable(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_stable_id_t){
		.source_set_identity = 1U,
		.high = (uint64_t)domain << 32,
		.low = (uint64_t)ordinal << 32
	};
}

static sg_rune_state_mode_t SupportedMode(void)
{
	sg_rune_state_mode_t mode = { 0 };

	mode.kind = SG_RUNE_STATE_MODE_SUPPORTED;
	mode.value.supported.support_surface.value =
		Stable(SG_RUNE_ORDER_SURFACE, 1U);
	return mode;
}

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
		.region_count = 1U,
		.model = &fixture->model,
		.phases = fixture->phases
	};
	fixture->localized = (sg_localized_field_state_t){
		.rune_identity = fixture->snapshot.identity,
		.topology_revision = fixture->snapshot.topology_revision,
		.pose_revision = 1U,
		.sampled_at_ms = 100U,
		.chart = { Stable(SG_RUNE_ORDER_STATE_CHART, 1U) },
		.mode = SupportedMode(),
		.position = { { 0.0f, 0.0f, 0.0f } },
		.velocity = { { 0.0f, 0.0f, 0.0f } },
		.elapsed_ms = 0.0f
	};
}

static void InitBindingRuntime(caller_fixture_t *fixture, uint16_t index,
	const sg_destination_ref_t *destination)
{
	sg_destination_terminal_t *terminal;
	sg_field_handle_t *field;
	sg_field_guidance_t *guidance;

	if (!fixture || !destination || index >= 4U)
		return;
	terminal = &fixture->terminals[index];
	field = &fixture->field_handles[index];
	guidance = &fixture->guidances[index];
	*terminal = (sg_destination_terminal_t){
		.owner_identity = 1U,
		.destination = *destination,
		.generation = (uint64_t)index + 1U,
		.kind = SG_DESTINATION_TERMINAL_STATIC_PATCH,
		.value.static_patch = {
			.domain = {
				.chart = { Stable(SG_RUNE_ORDER_STATE_CHART, 1U) },
				.domain = { Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U) }
			}
		}
	};
	terminal->value.static_patch.capture.anchor.owner_identity = 1U;
	terminal->value.static_patch.capture.anchor.destination = *destination;
	terminal->value.static_patch.capture.anchor.destination_generation =
		terminal->generation;
	*field = (sg_field_handle_t){
		.service_identity = 1U,
		.service_generation = 1U,
		.rune_identity = fixture->snapshot.identity,
		.topology_revision = fixture->snapshot.topology_revision,
		.terminal_generation = terminal->generation,
		.field_generation = (uint64_t)index + 1U
	};
	*guidance = (sg_field_guidance_t){
		.field = *field,
		.pose_revision = fixture->localized.pose_revision,
		.sampled_at_ms = fixture->localized.sampled_at_ms,
		.kind = SG_FIELD_GUIDANCE_TERMINAL,
		.value.terminal = {
			.arrival_cost = { 0U, 0U },
			.residual_bound_us = 0U
		}
	};
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
	const int *execution_field, caller_fixture_t *fixture)
{
	InitBindingRuntime(fixture, index, destination);
	plan->bindings[index] = (sg_strategy_caller_target_binding_t){
		.commitment_id = plan->commitment_id,
		.authority = plan->authority,
		.goal_id = goal_id,
		.target_id = target_id,
		.destination = *destination,
		.role = role,
		.execution_field = execution_field,
		.accepted_view = &fixture->accepted_views[index],
		.snapshot = &fixture->snapshot,
		.terminal = &fixture->terminals[index],
		.field_handle = &fixture->field_handles[index],
		.guidance = &fixture->guidances[index],
		.localized = &fixture->localized,
		.resolved_destination = Handle(destination->kind,
			(uint64_t)index + 1U),
		.observation_revision = 1U,
		.pose_revision = 1U,
		.valid_until_ms = UINT64_MAX
	};
}

static sg_strategy_caller_plan_t Plan(caller_fixture_t *fixture,
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
		railgun_field, fixture);
	AddBinding(&plan, 1U, 2U, 2U, &armor->choices[0].destination, 5,
		armor_field, fixture);
	AddBinding(&plan, 2U, 3U, 3U, &primary->choices[0].destination, 6,
		capture_field, fixture);
	AddBinding(&plan, 3U, 3U, 4U, &primary->choices[1].destination, 7,
		fallback_field, fixture);
	return plan;
}

static void PlanPrimaryRecover(sg_strategy_caller_plan_t *plan)
{
	sg_strategy_goal_spec_t *primary;
	sg_destination_ref_t current;
	sg_destination_ref_t home;

	if (!plan)
		return;
	primary = &plan->spec.goals[2];
	memset(&current, 0, sizeof(current));
	memset(&home, 0, sizeof(home));
	current.kind = SG_DESTINATION_FLAG;
	current.value.flag.team = 1U;
	current.value.flag.location = SG_DESTINATION_FLAG_CURRENT;
	home.kind = SG_DESTINATION_FLAG;
	home.value.flag.team = 1U;
	home.value.flag.location = SG_DESTINATION_FLAG_HOME;
	primary->kind = SG_STRATEGY_GOAL_RECOVER_FLAG;
	primary->choices[0].destination = current;
	primary->choices[1].destination = home;
	plan->bindings[2].destination = current;
	plan->bindings[3].destination = home;
	((sg_destination_terminal_t *)plan->bindings[2].terminal)->destination =
		current;
	((sg_destination_terminal_t *)plan->bindings[3].terminal)->destination =
		home;
	((sg_destination_terminal_t *)plan->bindings[2].terminal)->value.static_patch
		.capture.anchor.destination = current;
	((sg_destination_terminal_t *)plan->bindings[3].terminal)->value.static_patch
		.capture.anchor.destination = home;
}

static sg_strategy_runtime_plan_request_t RuntimeRequest(
	const sg_strategy_caller_plan_t *plan)
{
	static sg_localized_player_state_t localized_player;
	sg_strategy_runtime_plan_request_t request;
	uint16_t index;

	memset(&request, 0, sizeof(request));
	request.commitment_id = plan->commitment_id;
	memset(&localized_player, 0, sizeof(localized_player));
	localized_player.subject.client_id = 1U;
	localized_player.subject.spawn_generation = 1U;
	localized_player.rune_identity = plan->bindings[0].snapshot->identity;
	localized_player.topology_revision =
		plan->bindings[0].snapshot->topology_revision;
	localized_player.frame_sequence = 1U;
	localized_player.field_pose.phase =
		plan->bindings[0].snapshot->phases[0];
	localized_player.field_pose.region_id = 0U;
	localized_player.field_pose.sample_time_ms =
		plan->bindings[0].localized->sampled_at_ms;
	request.localized_player = &localized_player;
	request.authority = plan->authority;
	request.spec = plan->spec;
	request.execution_count = plan->binding_count;
	for (index = 0U; index < plan->binding_count; index++)
		request.executions[index] = (sg_strategy_runtime_execution_t){
			.goal_id = plan->bindings[index].goal_id,
			.target_id = plan->bindings[index].target_id,
			.role = plan->bindings[index].role
		};
	return request;
}

typedef struct runtime_target_record_s
{
	/* These are the field-service owner's immutable view facts.  The
	 * locator below cannot change them by echoing a different binding. */
	sg_destination_ref_t semantic_destination;
	const int *semantic_execution_field;
	sg_strategy_caller_target_binding_t binding;
} runtime_target_record_t;

typedef struct runtime_fixture_s
{
	runtime_target_record_t records[SG_STRATEGY_CALLER_MAX_BINDINGS];
	struct runtime_fixture_s *replacement;
	uint16_t count;
	uint16_t change_locator_call;
	uint16_t change_authority_call;
	int forced_view_index;
	uint32_t locator_calls;
	uint32_t authority_calls;
	uint32_t acquired;
	uint32_t released;
	uint32_t releases[SG_STRATEGY_CALLER_MAX_BINDINGS];
	uint8_t clear_provider_on_release;
} runtime_fixture_t;

static int RuntimeProvider(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_runtime_target_view_t *view_out);
static int RuntimeAuthority(void *context,
	const sg_strategy_runtime_target_request_t *request,
	const sg_strategy_runtime_target_view_t *view,
	sg_strategy_caller_target_binding_t *binding_out);
static void RuntimeRelease(void *context, const void *accepted_view);

static int RuntimeDestinationEqual(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (!left || !right || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_DESTINATION_FLAG:
		return left->value.flag.team == right->value.flag.team &&
			left->value.flag.location == right->value.flag.location;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return left->value.item.item_id == right->value.item.item_id;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return left->value.carrier.client_id ==
				right->value.carrier.client_id &&
			left->value.carrier.team == right->value.carrier.team &&
			left->value.carrier.selector == right->value.carrier.selector;
	case SG_DESTINATION_DEFENSIVE_POST:
		return left->value.post.region_id == right->value.post.region_id;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return left->value.point.point_id == right->value.point.point_id;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int RuntimeAuthorityEqual(const sg_strategy_caller_authority_t *left,
	const sg_strategy_caller_authority_t *right)
{
	return left && right && left->rank == right->rank &&
		left->principal_kind == right->principal_kind &&
		left->principal_id == right->principal_id;
}

static void InitRuntimeFixture(runtime_fixture_t *runtime,
	const sg_strategy_caller_plan_t *plan)
{
	uint16_t index;

	memset(runtime, 0, sizeof(*runtime));
	runtime->forced_view_index = -1;
	runtime->count = plan->binding_count;
	for (index = 0U; index < plan->binding_count; index++)
	{
		runtime->records[index].semantic_destination =
			plan->bindings[index].destination;
		runtime->records[index].semantic_execution_field =
			plan->bindings[index].execution_field;
		runtime->records[index].binding = plan->bindings[index];
		runtime->records[index].binding.accepted_view =
			&runtime->records[index];
	}
}

static void RuntimeProviderChange(runtime_fixture_t *runtime)
{
	if (!runtime)
		return;
	if (!runtime->replacement)
	{
		SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
		return;
	}
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, runtime->replacement,
		RuntimeAuthority, runtime->replacement, RuntimeRelease,
		runtime->replacement);
}

static int RuntimeProvider(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_runtime_target_view_t *view_out)
{
	runtime_fixture_t *runtime = context;
	uint16_t index;
	int found = 0;

	if (!runtime || !request || !view_out)
		return 0;
	runtime->locator_calls++;
	if (runtime->forced_view_index >= 0 &&
	    runtime->forced_view_index < runtime->count)
	{
		view_out->opaque = &runtime->records[runtime->forced_view_index];
		found = 1;
	}
	for (index = 0U; !found && index < runtime->count; index++)
		if (runtime->records[index].binding.goal_id == request->goal_id &&
		    runtime->records[index].binding.target_id == request->target_id)
		{
			view_out->opaque = &runtime->records[index];
			found = 1;
		}
	if (found && runtime->change_locator_call == runtime->locator_calls)
		RuntimeProviderChange(runtime);
	return found;
}

static int RuntimeAuthority(void *context,
	const sg_strategy_runtime_target_request_t *request,
	const sg_strategy_runtime_target_view_t *view,
	sg_strategy_caller_target_binding_t *binding_out)
{
	runtime_fixture_t *runtime = context;
	uint16_t index;

	if (!runtime || !request || !view || !view->opaque || !binding_out)
		return 0;
	runtime->authority_calls++;
	for (index = 0U; index < runtime->count; index++)
	{
		const runtime_target_record_t *record = &runtime->records[index];

		if (view->opaque != record)
			continue;
		/* The opaque owner record, not locator-controlled echo data, proves
		 * that the actual execution field is tied to this full semantic target. */
		if (record->binding.commitment_id != request->commitment_id ||
		    !RuntimeAuthorityEqual(&record->binding.authority,
				&request->authority) ||
		    record->binding.goal_id != request->goal_id ||
		    record->binding.target_id != request->target_id ||
		    !RuntimeDestinationEqual(&record->semantic_destination,
				&request->destination) ||
		    !RuntimeDestinationEqual(&record->binding.destination,
				&record->semantic_destination) ||
		    record->binding.role != request->role ||
		    record->binding.accepted_view != record ||
		    !record->binding.terminal ||
		    !RuntimeDestinationEqual(&record->binding.terminal->destination,
				&record->semantic_destination) ||
		    record->binding.execution_field !=
				record->semantic_execution_field)
			return 0;
		runtime->acquired++;
		*binding_out = record->binding;
		if (runtime->change_authority_call == runtime->authority_calls)
			RuntimeProviderChange(runtime);
		return 1;
	}
	return 0;
}

static void RuntimeRelease(void *context, const void *accepted_view)
{
	runtime_fixture_t *runtime = context;
	uint16_t index;

	if (!runtime || !accepted_view)
		return;
	for (index = 0U; index < runtime->count; index++)
		if (accepted_view == &runtime->records[index])
		{
			runtime->released++;
			runtime->releases[index]++;
			if (runtime->clear_provider_on_release)
			{
				runtime->clear_provider_on_release = 0U;
				SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL,
					NULL, NULL);
			}
			return;
		}
}

static void CountRelease(void *context, const void *accepted_view)
{
	uint32_t *count = context;

	if (count && accepted_view)
		(*count)++;
}

typedef struct retirement_fixture_s
{
	sg_strategy_caller_t *caller;
	sg_strategy_caller_plan_t *plan_to_mutate;
	sg_strategy_caller_authority_t authority;
	const void *views[4];
	uint32_t released;
	uint32_t releases[4];
	uint8_t callback_entered;
	uint8_t destroy_on_first;
	uint8_t release_on_first;
	uint8_t mutate_plan_on_first;
	int reentrant_release_result;
} retirement_fixture_t;

static void InitRetirementFixture(retirement_fixture_t *retirement,
	caller_fixture_t *fixture)
{
	uint16_t index;

	memset(retirement, 0, sizeof(*retirement));
	for (index = 0U; index < 4U; index++)
		retirement->views[index] = &fixture->accepted_views[index];
}

static void RetirementRelease(void *context, const void *accepted_view)
{
	retirement_fixture_t *retirement = context;
	uint16_t index;

	if (!retirement || !accepted_view)
		return;
	retirement->released++;
	for (index = 0U; index < 4U; index++)
		if (retirement->views[index] == accepted_view)
		{
			retirement->releases[index]++;
			break;
		}
	if (retirement->callback_entered)
		return;
	retirement->callback_entered = 1U;
	if (retirement->mutate_plan_on_first && retirement->plan_to_mutate)
		memset(retirement->plan_to_mutate, 0,
			sizeof(*retirement->plan_to_mutate));
	if (retirement->release_on_first && retirement->caller)
	{
		sg_strategy_caller_output_t output;

		retirement->reentrant_release_result = SG_StrategyCallerRelease(
			retirement->caller, &retirement->authority, 1U, 101U,
			&output);
	}
	if (retirement->destroy_on_first && retirement->caller)
		SG_StrategyCallerDestroy(retirement->caller);
}

static void CheckReleasedExactlyOnce(const retirement_fixture_t *retirement)
{
	uint16_t index;

	CHECK(retirement->released == 4U);
	for (index = 0U; index < 4U; index++)
		CHECK(retirement->releases[index] == 1U);
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
	plan = Plan(&fixture, 10U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
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
	CHECK(output.terminal == &fixture.terminals[1]);
	CHECK(output.field_handle == &fixture.field_handles[1]);
	CHECK(output.guidance == &fixture.guidances[1]);
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

static void TestSemanticGoalChangeReplacesAutonomousPlan(void)
{
	caller_fixture_t capture_fixture;
	caller_fixture_t recover_fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t capture;
	sg_strategy_caller_plan_t recover;
	uint64_t capture_plan_id;

	InitFixture(&capture_fixture);
	InitFixture(&recover_fixture);
	capture = Plan(&capture_fixture, 23U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	/* Keep the commitment equal on purpose: a CAPTURE->RECOVER semantic
	 * transition must not be hidden behind only a new hash value. */
	recover = Plan(&recover_fixture, 23U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	PlanPrimaryRecover(&recover);
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &capture, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	capture_plan_id = output.plan_id;
	CHECK(SG_StrategyCallerSubmit(&caller, &recover, 1U, 110U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.plan_id != capture_plan_id);
	CHECK(caller.reducer.plan.goals[2].kind == SG_STRATEGY_GOAL_RECOVER_FLAG);
	CHECK(caller.plan.bindings[2].destination.value.flag.team == 1U);
}

static void TestLowerAuthorityCannotReleaseHumanPlan(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t autonomous;
	sg_strategy_caller_plan_t human;
	sg_strategy_caller_authority_t autonomous_authority;
	sg_strategy_caller_authority_t human_authority;
	uint64_t human_plan_id;
	uint64_t history;
	uint32_t ignored_releases = 0U;
	uint16_t ignored_binding_count;

	InitFixture(&fixture);
	autonomous = Plan(&fixture, 30U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	autonomous_authority = autonomous.authority;
	human = Plan(&fixture, 31U, SG_STRATEGY_AUTHORITY_HUMAN,
		SG_STRATEGY_PRINCIPAL_HUMAN, 7U);
	human_authority = human.authority;
	autonomous.release_view = CountRelease;
	autonomous.release_context = &ignored_releases;
	ignored_binding_count = autonomous.binding_count;
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
	CHECK(ignored_releases == ignored_binding_count);
	CHECK(!SG_StrategyCallerCancel(&caller, &autonomous_authority, 1U,
		120U, &output));
	CHECK(!SG_StrategyCallerRelease(&caller, &autonomous_authority, 1U,
		120U, &output));
	CHECK(SG_StrategyCallerCancel(&caller, &human_authority, 1U, 120U,
		&output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_CANCELLED);
	CHECK(caller.reducer.cancelled == 1U);
	CHECK(SG_StrategyCallerRelease(&caller, &human_authority, 1U, 120U,
		&output));
	CHECK(caller.reducer.authority.principal.kind == SG_STRATEGY_PRINCIPAL_NONE);
	autonomous = Plan(&fixture, 30U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
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
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	plan = Plan(&fixture, 40U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&plan);
	InitRuntimeFixture(&runtime, &plan);
	memset(&resolved, 0, sizeof(resolved));
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, RuntimeRelease, &runtime);
	CHECK(SG_StrategyRuntimeTargetProviderAvailable());
	CHECK(SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(resolved.bindings[0].snapshot == &fixture.snapshot);
	SG_StrategyCallerPlanDiscard(&resolved);
	CHECK(runtime.acquired == 4U);
	CHECK(runtime.released == 4U);
	runtime.records[2].binding.destination.value.flag.location =
		SG_DESTINATION_FLAG_HOME;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(resolved.commitment_id == 0U);
	runtime.records[2].binding.destination.value.flag.location =
		SG_DESTINATION_FLAG_CURRENT;
	runtime.records[0].binding.commitment_id++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	runtime.records[0].binding.commitment_id--;
	runtime.records[0].binding.authority.principal_id++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	runtime.records[0].binding.authority.principal_id--;
	runtime.records[0].binding.role++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	runtime.records[0].binding.role--;
	runtime.records[0].binding.execution_field = fallback_field;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

static void TestRuntimeAuthorityRejectsSameKindFieldViewSwap(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_runtime_plan_request_t request;
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	plan = Plan(&fixture, 41U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&plan);
	InitRuntimeFixture(&runtime, &plan);
	memset(&resolved, 0, sizeof(resolved));
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, RuntimeRelease, &runtime);
	/* A malicious locator can nominate HOME's opaque field view and make every
	 * returned target/terminal/field/guidance echo look like CURRENT.  The
	 * destination authority still rejects it because the accepted view owns
	 * HOME and its execution field, not CURRENT. */
	runtime.records[3].binding.commitment_id = request.commitment_id;
	runtime.records[3].binding.authority = request.authority;
	runtime.records[3].binding.goal_id = request.spec.goals[2].id;
	runtime.records[3].binding.target_id = request.spec.goals[2].choices[0].id;
	runtime.records[3].binding.destination = request.spec.goals[2].choices[0].destination;
	runtime.records[3].binding.role = request.executions[2].role;
	runtime.records[3].binding.execution_field = capture_field;
	runtime.records[3].binding.terminal = plan.bindings[2].terminal;
	runtime.records[3].binding.field_handle = plan.bindings[2].field_handle;
	runtime.records[3].binding.guidance = plan.bindings[2].guidance;
	runtime.records[3].binding.resolved_destination =
		plan.bindings[2].resolved_destination;
	runtime.forced_view_index = 3;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	runtime.forced_view_index = -1;
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

static void TestRuntimeResolutionRollsBackAndCallerReleasesViews(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_runtime_plan_request_t request;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	source = Plan(&fixture, 42U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	InitRuntimeFixture(&runtime, &source);
	memset(&resolved, 0, sizeof(resolved));
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, RuntimeRelease, &runtime);
	runtime.records[2].binding.role++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(runtime.acquired == 2U);
	CHECK(runtime.released == 2U);
	runtime.records[2].binding.role--;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(SG_StrategyCallerSubmit(&caller, &resolved, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(runtime.acquired == 6U);
	CHECK(runtime.released == 2U);
	CHECK(SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(SG_StrategyCallerSubmit(&caller, &resolved, 1U, 110U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(runtime.acquired == 10U);
	CHECK(runtime.released == 6U);
	CHECK(SG_StrategyCallerRelease(&caller, &source.authority, 1U, 120U,
		&output));
	CHECK(runtime.released == 10U);
	SG_StrategyCallerDestroy(&caller);
	CHECK(runtime.released == 10U);
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
}

static void TestPlanDiscardDetachesBeforeCallbackMutation(void)
{
	caller_fixture_t fixture;
	retirement_fixture_t retirement;
	sg_strategy_caller_plan_t plan;

	InitFixture(&fixture);
	plan = Plan(&fixture, 44U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	InitRetirementFixture(&retirement, &fixture);
	retirement.plan_to_mutate = &plan;
	retirement.mutate_plan_on_first = 1U;
	plan.release_view = RetirementRelease;
	plan.release_context = &retirement;
	SG_StrategyCallerPlanDiscard(&plan);
	CheckReleasedExactlyOnce(&retirement);
	CHECK(plan.binding_count == 0U);
	CHECK(plan.release_view == NULL);
}

static void TestDestroyDetachesBeforeReentrantDestroyAndRelease(void)
{
	caller_fixture_t fixture;
	retirement_fixture_t retirement;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t plan;

	InitFixture(&fixture);
	plan = Plan(&fixture, 45U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	InitRetirementFixture(&retirement, &fixture);
	retirement.caller = &caller;
	retirement.authority = plan.authority;
	retirement.destroy_on_first = 1U;
	retirement.release_on_first = 1U;
	plan.release_view = RetirementRelease;
	plan.release_context = &retirement;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	SG_StrategyCallerDestroy(&caller);
	CheckReleasedExactlyOnce(&retirement);
	CHECK(retirement.reentrant_release_result == 0);
	CHECK(caller.initialized == 0U);
	CHECK(caller.has_plan == 0U);
}

static void TestExplicitReleaseDetachesBeforeReentrantDestroy(void)
{
	caller_fixture_t fixture;
	retirement_fixture_t retirement;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_authority_t authority;

	InitFixture(&fixture);
	plan = Plan(&fixture, 46U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	authority = plan.authority;
	InitRetirementFixture(&retirement, &fixture);
	retirement.caller = &caller;
	retirement.destroy_on_first = 1U;
	plan.release_view = RetirementRelease;
	plan.release_context = &retirement;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(SG_StrategyCallerRelease(&caller, &authority, 1U, 110U,
		&output));
	CheckReleasedExactlyOnce(&retirement);
	CHECK(caller.initialized == 0U);
	CHECK(caller.has_plan == 0U);
}

static void TestReplacementRetirementSurvivesReentrantDestroy(void)
{
	caller_fixture_t old_fixture;
	caller_fixture_t new_fixture;
	retirement_fixture_t retirement;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t old_plan;
	sg_strategy_caller_plan_t new_plan;
	uint32_t new_releases = 0U;

	InitFixture(&old_fixture);
	InitFixture(&new_fixture);
	old_plan = Plan(&old_fixture, 47U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	new_plan = Plan(&new_fixture, 48U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	PlanPrimaryRecover(&new_plan);
	InitRetirementFixture(&retirement, &old_fixture);
	retirement.caller = &caller;
	retirement.destroy_on_first = 1U;
	old_plan.release_view = RetirementRelease;
	old_plan.release_context = &retirement;
	new_plan.release_view = CountRelease;
	new_plan.release_context = &new_releases;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &old_plan, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(SG_StrategyCallerSubmit(&caller, &new_plan, 1U, 110U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CheckReleasedExactlyOnce(&retirement);
	CHECK(new_releases == 4U);
	CHECK(caller.initialized == 0U);
	CHECK(caller.has_plan == 0U);
}

static void TestLowerAuthorityRetirementSurvivesMutationAndDestroy(void)
{
	caller_fixture_t human_fixture;
	caller_fixture_t autonomous_fixture;
	retirement_fixture_t retirement;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_plan_t human;
	sg_strategy_caller_plan_t autonomous;
	uint32_t human_releases = 0U;

	InitFixture(&human_fixture);
	InitFixture(&autonomous_fixture);
	human = Plan(&human_fixture, 49U, SG_STRATEGY_AUTHORITY_HUMAN,
		SG_STRATEGY_PRINCIPAL_HUMAN, 7U);
	autonomous = Plan(&autonomous_fixture, 50U,
		SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	InitRetirementFixture(&retirement, &autonomous_fixture);
	retirement.caller = &caller;
	retirement.plan_to_mutate = &autonomous;
	retirement.destroy_on_first = 1U;
	retirement.mutate_plan_on_first = 1U;
	human.release_view = CountRelease;
	human.release_context = &human_releases;
	autonomous.release_view = RetirementRelease;
	autonomous.release_context = &retirement;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &human, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(SG_StrategyCallerSubmit(&caller, &autonomous, 1U, 110U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CheckReleasedExactlyOnce(&retirement);
	CHECK(human_releases == 4U);
	CHECK(caller.initialized == 0U);
	CHECK(caller.has_plan == 0U);
}

static void TestPartialRollbackUsesDetachedReleaseOwner(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_runtime_plan_request_t request;
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	source = Plan(&fixture, 51U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	InitRuntimeFixture(&runtime, &source);
	memset(&resolved, 0, sizeof(resolved));
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, RuntimeRelease, &runtime);
	runtime.clear_provider_on_release = 1U;
	((sg_field_guidance_t *)runtime.records[2].binding.guidance)->pose_revision++;
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(runtime.acquired == 3U);
	CHECK(runtime.released == 3U);
	CHECK(runtime.releases[0] == 1U);
	CHECK(runtime.releases[1] == 1U);
	CHECK(runtime.releases[2] == 1U);
	CHECK(resolved.binding_count == 0U);
	CHECK(resolved.release_view == NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

static void TestProviderUnregisterInLocatorLeavesBorrowedViewUnreleased(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_caller_plan_t unchanged;
	sg_strategy_runtime_plan_request_t request;
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	source = Plan(&fixture, 52U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	InitRuntimeFixture(&runtime, &source);
	memset(&resolved, 0, sizeof(resolved));
	unchanged = resolved;
	runtime.change_locator_call = 1U;
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, RuntimeRelease, &runtime);
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(memcmp(&resolved, &unchanged, sizeof(resolved)) == 0);
	CHECK(runtime.locator_calls == 1U);
	CHECK(runtime.authority_calls == 0U);
	CHECK(runtime.acquired == 0U);
	CHECK(runtime.released == 0U);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

static void TestProviderReplacementInSecondLocatorRollsBackOldLease(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_caller_plan_t unchanged;
	sg_strategy_runtime_plan_request_t request;
	runtime_fixture_t old_runtime;
	runtime_fixture_t new_runtime;

	InitFixture(&fixture);
	source = Plan(&fixture, 53U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	InitRuntimeFixture(&old_runtime, &source);
	InitRuntimeFixture(&new_runtime, &source);
	memset(&resolved, 0, sizeof(resolved));
	unchanged = resolved;
	old_runtime.replacement = &new_runtime;
	old_runtime.change_locator_call = 2U;
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &old_runtime,
		RuntimeAuthority, &old_runtime, RuntimeRelease, &old_runtime);
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(memcmp(&resolved, &unchanged, sizeof(resolved)) == 0);
	CHECK(old_runtime.locator_calls == 2U);
	CHECK(old_runtime.authority_calls == 1U);
	CHECK(old_runtime.acquired == 1U);
	CHECK(old_runtime.released == 1U);
	CHECK(old_runtime.releases[0] == 1U);
	CHECK(new_runtime.locator_calls == 0U);
	CHECK(new_runtime.authority_calls == 0U);
	CHECK(new_runtime.released == 0U);
	CHECK(SG_StrategyRuntimeTargetProviderAvailable());
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
}

static void TestProviderUnregisterInAuthorityReleasesAcceptedView(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_caller_plan_t unchanged;
	sg_strategy_runtime_plan_request_t request;
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	source = Plan(&fixture, 54U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	InitRuntimeFixture(&runtime, &source);
	memset(&resolved, 0, sizeof(resolved));
	unchanged = resolved;
	runtime.change_authority_call = 1U;
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, RuntimeRelease, &runtime);
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &resolved));
	CHECK(memcmp(&resolved, &unchanged, sizeof(resolved)) == 0);
	CHECK(runtime.locator_calls == 1U);
	CHECK(runtime.authority_calls == 1U);
	CHECK(runtime.acquired == 1U);
	CHECK(runtime.released == 1U);
	CHECK(runtime.releases[0] == 1U);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

static void TestProviderReplacementInSecondAuthorityUsesOldReleaseOwner(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t resolved;
	sg_strategy_caller_plan_t unchanged;
	sg_strategy_runtime_plan_request_t request;
	runtime_fixture_t old_runtime;
	runtime_fixture_t new_runtime;
	int resolved_ok;

	InitFixture(&fixture);
	source = Plan(&fixture, 55U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	InitRuntimeFixture(&old_runtime, &source);
	InitRuntimeFixture(&new_runtime, &source);
	memset(&resolved, 0, sizeof(resolved));
	unchanged = resolved;
	old_runtime.replacement = &new_runtime;
	old_runtime.change_authority_call = 2U;
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &old_runtime,
		RuntimeAuthority, &old_runtime, RuntimeRelease, &old_runtime);
	resolved_ok = SG_StrategyRuntimePlanResolve(&request, &resolved);
	CHECK(!resolved_ok);
	if (!resolved_ok)
		CHECK(memcmp(&resolved, &unchanged, sizeof(resolved)) == 0);
	else
		SG_StrategyCallerPlanDiscard(&resolved);
	CHECK(old_runtime.locator_calls == 2U);
	CHECK(old_runtime.authority_calls == 2U);
	CHECK(old_runtime.acquired == 2U);
	CHECK(old_runtime.released == 2U);
	CHECK(old_runtime.releases[0] == 1U);
	CHECK(old_runtime.releases[1] == 1U);
	CHECK(new_runtime.locator_calls == 0U);
	CHECK(new_runtime.authority_calls == 0U);
	CHECK(new_runtime.released == 0U);
	CHECK(SG_StrategyRuntimeTargetProviderAvailable());
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
}

static void TestProviderReplacementLeavesCallerOutputAndInputUnchanged(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t source;
	sg_strategy_caller_plan_t submitted;
	sg_strategy_caller_plan_t rejected;
	sg_strategy_caller_plan_t unchanged_rejected;
	sg_strategy_runtime_plan_request_t request;
	sg_strategy_runtime_plan_request_t unchanged_request;
	sg_strategy_caller_t caller;
	sg_strategy_caller_t unchanged_caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_output_t unchanged_output;
	runtime_fixture_t old_runtime;
	runtime_fixture_t new_runtime;

	InitFixture(&fixture);
	source = Plan(&fixture, 56U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	request = RuntimeRequest(&source);
	unchanged_request = request;
	InitRuntimeFixture(&old_runtime, &source);
	InitRuntimeFixture(&new_runtime, &source);
	memset(&submitted, 0, sizeof(submitted));
	memset(&rejected, 0, sizeof(rejected));
	unchanged_rejected = rejected;
	old_runtime.replacement = &new_runtime;
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &old_runtime,
		RuntimeAuthority, &old_runtime, RuntimeRelease, &old_runtime);
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyRuntimePlanResolve(&request, &submitted));
	CHECK(SG_StrategyCallerSubmit(&caller, &submitted, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	unchanged_caller = caller;
	unchanged_output = output;
	old_runtime.change_locator_call = (uint16_t)(old_runtime.locator_calls + 1U);
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &rejected));
	CHECK(memcmp(&rejected, &unchanged_rejected, sizeof(rejected)) == 0);
	CHECK(memcmp(&request, &unchanged_request, sizeof(request)) == 0);
	CHECK(memcmp(&caller, &unchanged_caller, sizeof(caller)) == 0);
	CHECK(memcmp(&output, &unchanged_output, sizeof(output)) == 0);
	CHECK(old_runtime.acquired == 4U);
	CHECK(old_runtime.released == 0U);
	CHECK(new_runtime.locator_calls == 0U);
	CHECK(new_runtime.authority_calls == 0U);
	CHECK(new_runtime.released == 0U);
	SG_StrategyCallerDestroy(&caller);
	CHECK(old_runtime.released == 4U);
	CHECK(old_runtime.releases[0] == 1U);
	CHECK(old_runtime.releases[1] == 1U);
	CHECK(old_runtime.releases[2] == 1U);
	CHECK(old_runtime.releases[3] == 1U);
	CHECK(new_runtime.released == 0U);
	SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
}

static void TestRuntimeProviderRequiresReleaseOwner(void)
{
	caller_fixture_t fixture;
	sg_strategy_caller_plan_t plan;
	runtime_fixture_t runtime;

	InitFixture(&fixture);
	plan = Plan(&fixture, 43U, SG_STRATEGY_AUTHORITY_AUTONOMOUS,
		SG_STRATEGY_PRINCIPAL_AUTONOMOUS, 1U);
	InitRuntimeFixture(&runtime, &plan);
	SG_StrategyRuntimeTargetProviderSet(RuntimeProvider, &runtime,
		RuntimeAuthority, &runtime, NULL, NULL);
	CHECK(!SG_StrategyRuntimeTargetProviderAvailable());
}

int main(void)
{
	TestQueuedPlanAdvancesAcrossGoals();
	TestQueuedPlanPersistsFailureAdvance();
	TestSuspendedTerminalAdvanceAndAuthenticatedSources();
	TestCallerRejectsSameKindDestinationForgery();
	TestSemanticGoalChangeReplacesAutonomousPlan();
	TestLowerAuthorityCannotReleaseHumanPlan();
	TestRuntimeResolverFailsClosedWithoutAuthenticatedProvider();
	TestRuntimeAuthorityRejectsSameKindFieldViewSwap();
	TestRuntimeResolutionRollsBackAndCallerReleasesViews();
	TestPlanDiscardDetachesBeforeCallbackMutation();
	TestDestroyDetachesBeforeReentrantDestroyAndRelease();
	TestExplicitReleaseDetachesBeforeReentrantDestroy();
	TestReplacementRetirementSurvivesReentrantDestroy();
	TestLowerAuthorityRetirementSurvivesMutationAndDestroy();
	TestPartialRollbackUsesDetachedReleaseOwner();
	TestProviderUnregisterInLocatorLeavesBorrowedViewUnreleased();
	TestProviderReplacementInSecondLocatorRollsBackOldLease();
	TestProviderUnregisterInAuthorityReleasesAcceptedView();
	TestProviderReplacementInSecondAuthorityUsesOldReleaseOwner();
	TestProviderReplacementLeavesCallerOutputAndInputUnchanged();
	TestRuntimeProviderRequiresReleaseOwner();
	if (failures)
	{
		fprintf(stderr, "sg_strategy_caller_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_strategy_caller_test: ok");
	return 0;
}
