#include "../slipgate/sg_rune_compact_field_service.h"
#include "../slipgate/sg_strategy_caller.h"
#include "../slipgate/sg_strategy_caller_private.h"
#include "../slipgate/sg_strategy_runtime_bridge.h"
#include "../slipgate/sg_strategy_runtime_bridge_private.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
typedef struct strategy_fixture_s strategy_fixture_t;

struct sg_strategy_runtime_bot_observation_s
{
	uint32_t guard;
};

typedef struct test_bot_observation_s
{
	struct sg_strategy_runtime_bot_observation_s capability;
	sg_strategy_runtime_bot_observation_view_t view;
} test_bot_observation_t;

static test_bot_observation_t test_bot_observation;

static int ValidateBotObservation(void *context,
	const sg_strategy_runtime_bot_observation_t *observation,
	sg_strategy_runtime_bot_observation_view_t *view_out)
{
	test_bot_observation_t *issued = context;

	if (issued == NULL || observation != &issued->capability ||
		view_out == NULL || issued->capability.guard != UINT32_C(0xb07b07a1))
		return 0;
	*view_out = issued->view;
	return 1;
}

static int BotObservationCurrent(void *context,
	const sg_strategy_runtime_bot_observation_view_t *view)
{
	const test_bot_observation_t *issued = context;

	return issued != NULL && view != NULL &&
		memcmp(view, &issued->view, sizeof(*view)) == 0;
}

static void IssueBotObservation(const strategy_fixture_t *fixture);

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

int SG_RuneCompactModelValidateBound(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out)
{
	if (error_out != NULL)
	{
		error_out->code = SG_RUNE_COMPACT_ERROR_NONE;
		error_out->domain = SG_RUNE_COMPACT_RECORD_MODEL;
		error_out->record = 0U;
	}
	return model != NULL && expected_identity != NULL &&
		expected_identity == &model->identity;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL && actual == expected;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model,
	const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	(void)point;
	if (model == NULL || location_out == NULL || model->cell_count == 0U)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = point->value[0] >= 400 &&
		model->cell_count > 1U ? 1U : 0U;
	location_out->valid_stances = SG_RUNE_STANCE_VALID_ALL;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

struct strategy_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_static_t static_data;
	sg_rune_compact_landmark_t landmarks[3];
	sg_rune_compact_cell_index_t landmark_cells[3];
	sg_rune_compact_model_t model;
	sg_compact_localized_state_t localized;
	sg_rune_compact_field_service_t *service;
};

static sg_destination_ref_t Flag(sg_destination_flag_location_t location)
{
	sg_destination_ref_t destination;

	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_DESTINATION_FLAG;
	destination.value.flag.team = 1U;
	destination.value.flag.location = (uint8_t)location;
	return destination;
}

static sg_destination_ref_t TeamFlag(uint8_t team,
	sg_destination_flag_location_t location)
{
	sg_destination_ref_t destination = Flag(location);

	destination.value.flag.team = team;
	return destination;
}

static sg_destination_ref_t Weapon(uint64_t item_id)
{
	sg_destination_ref_t destination;

	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_DESTINATION_WEAPON;
	destination.value.item.item_id = item_id;
	return destination;
}

static sg_rune_compact_field_result_t FieldResult(
	sg_rune_compact_field_result_kind_t kind)
{
	sg_rune_compact_field_result_t result;

	memset(&result, 0, sizeof(result));
	result.kind = kind;
	result.current_cell.value = 0U;
	return result;
}

static void TestFieldObservations(void)
{
	sg_rune_compact_mechanism_index_t mechanisms[2];
	sg_rune_compact_field_result_t result;
	sg_strategy_caller_field_observation_t observation;

	mechanisms[0].value = 2U;
	mechanisms[1].value = 4U;
	result = FieldResult(SG_RUNE_COMPACT_FIELD_DISCONNECTED);
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_DISCONNECTED);
	CHECK(observation.cost_to_go.units ==
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE);

	result = FieldResult(SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION);
	result.value.destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_LOCAL_DESTINATION);
	CHECK(observation.cost_to_go.units == 0U);

	result = FieldResult(SG_RUNE_COMPACT_FIELD_CELL_DESTINATION);
	result.value.destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	result.value.destination.value.cell.value = 3U;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_CELL_DESTINATION);

	result = FieldResult(SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED);
	result.value.requirements.portal.value = 1U;
	result.value.requirements.mechanisms = mechanisms;
	result.value.requirements.mechanism_count = 2U;
	result.value.requirements.state =
		SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_MECHANISMS_REQUIRED);
	CHECK(observation.cost_to_go.units ==
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE);

	result = FieldResult(SG_RUNE_COMPACT_FIELD_BLOCKED_NOW);
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
	result.value.step.target_stance = SG_RUNE_COMPACT_FIELD_STANDING;
	result.value.step.cost_to_go.units =
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	result.value.step.next_cost_to_go.units =
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	result.value.step.value.portal.local_cost = INFINITY;
	result.value.step.value.portal.next_cell.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	result.value.step.value.portal.next_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_BLOCKED_NOW);

	result = FieldResult(SG_RUNE_COMPACT_FIELD_STEP);
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
	result.value.step.target_stance = SG_RUNE_COMPACT_FIELD_CROUCHING;
	result.value.step.cost_to_go.units =
		(UINT64_C(1) << 40) + UINT64_C(1);
	result.value.step.next_cost_to_go.units = UINT64_C(1) << 40;
	result.value.step.value.portal.local_cost = 0.25f;
	result.value.step.value.portal.next_cell.value = 1U;
	result.value.step.value.portal.next_portal.value = 2U;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_STEP);
	CHECK(observation.cost_to_go.units ==
		(UINT64_C(1) << 40) + UINT64_C(1));
	CHECK(SG_StrategyCallerFieldObservationValid(&observation));
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT;
	result.value.step.value.direct.local_cost = 0.5f;
	result.value.step.value.direct.next_cell.value = 3U;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_STEP);
	result.value.step.value.direct.next_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(!SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE;
	result.value.step.cost_to_go.units = 2U;
	result.value.step.next_cost_to_go.units = 1U;
	CHECK(SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(observation.kind == SG_STRATEGY_CALLER_FIELD_STEP);
	CHECK(observation.cost_to_go.units == 2U);
	result.value.step.next_cost_to_go = result.value.step.cost_to_go;
	CHECK(!SG_StrategyCallerFieldObservationFromResult(&result, &observation));
	CHECK(!SG_StrategyCallerFieldObservationValid(&observation));
}

static void InitFixture(strategy_fixture_t *fixture)
{
	uint32_t axis;

	memset(fixture, 0, sizeof(*fixture));
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cells[1].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->landmark_cells[0].value = 0U;
	fixture->landmark_cells[1].value = 0U;
	fixture->landmark_cells[2].value = 0U;
	fixture->landmarks[0].source.entity_ordinal = 10U;
	fixture->landmarks[0].cells.first = 0U;
	fixture->landmarks[0].cells.count = 1U;
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[0].variant = 0U;
	fixture->landmarks[1].source.entity_ordinal = 1U;
	fixture->landmarks[1].cells.first = 1U;
	fixture->landmarks[1].cells.count = 1U;
	fixture->landmarks[1].kind = SG_RUNE_COMPACT_LANDMARK_WEAPON;
	fixture->landmarks[1].origin.value[0] = 8;
	fixture->landmarks[1].origin.value[1] = 16;
	fixture->landmarks[1].origin.value[2] = 24;
	fixture->landmarks[2].source.entity_ordinal = 11U;
	fixture->landmarks[2].cells.first = 2U;
	fixture->landmarks[2].cells.count = 1U;
	fixture->landmarks[2].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[2].variant = 1U;
	fixture->landmarks[2].origin.value[0] = 800;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 3U;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 3U;
	fixture->model.identity.bsp_bytes = 1U;
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 2U;
	fixture->model.static_data = &fixture->static_data;
	fixture->localized.subject.client_id = 0U;
	fixture->localized.subject.spawn_generation = 1U;
	fixture->localized.rune_identity = 41U;
	fixture->localized.topology_revision = 7U;
	fixture->localized.frame_sequence = 1U;
	fixture->localized.localized_at_ms = 100U;
	fixture->localized.location.cell.value = 0U;
	fixture->localized.location.valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->localized.stance = SG_RUNE_STANCE_STANDING;
	fixture->localized.motion = SG_RUNE_MOTION_SUPPORTED;
	fixture->localized.support = SG_RUNE_SUPPORT_SUPPORTED;
	fixture->localized.medium = SG_RUNE_MEDIUM_DRY;
	fixture->localized.valid = 1U;
	for (axis = 0U; axis < 3U; axis++)
	{
		fixture->localized.position[axis] = 0.0f;
		fixture->localized.velocity[axis] = 0.0f;
	}
	CHECK(SG_RuneCompactFieldServiceCreate(&fixture->model,
		&fixture->model.identity, 41U, 7U, &fixture->service, NULL) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
}

static void IssueBotObservation(const strategy_fixture_t *fixture)
{
	memset(&test_bot_observation, 0, sizeof(test_bot_observation));
	test_bot_observation.capability.guard = UINT32_C(0xb07b07a1);
	test_bot_observation.view.subject = fixture->localized.subject;
	test_bot_observation.view.host_authority_epoch = 1U;
	test_bot_observation.view.frame_sequence =
		fixture->localized.frame_sequence;
	test_bot_observation.view.observed_at_ms =
		fixture->localized.localized_at_ms;
	test_bot_observation.view.hook_phase = SG_HOST_HOOK_IDLE;
}

static void InitGoal(sg_strategy_goal_spec_t *goal,
	sg_strategy_goal_id_t goal_id, sg_strategy_target_id_t target_id,
	const sg_destination_ref_t *destination)
{
	memset(goal, 0, sizeof(*goal));
	goal->id = goal_id;
	goal->kind = SG_STRATEGY_GOAL_DESTINATION;
	goal->priority = 1;
	goal->unavailable = SG_STRATEGY_UNAVAILABLE_WAIT;
	goal->choice_count = 1U;
	goal->choices[0].id = target_id;
	goal->choices[0].destination = *destination;
	goal->failure.try_alternatives = 1U;
	goal->failure.max_attempts_per_choice = 1U;
	goal->failure.retry_wake.kind = SG_STRATEGY_RETRY_NONE;
	goal->failure.exhausted = SG_STRATEGY_FAILURE_SKIP_GOAL;
}

static void PoisonInactivePlanRepresentation(sg_strategy_plan_spec_t *spec)
{
	sg_strategy_goal_spec_t *goal;
	sg_destination_ref_t *destination;
	unsigned char *destination_bytes;
	unsigned char *value_bytes;
	size_t index;

	CHECK(spec != NULL && spec->goal_count == 1U);
	if (spec == NULL || spec->goal_count != 1U)
		return;
	goal = &spec->goals[0];
	destination = &goal->choices[0].destination;
	goal->reserved = UINT8_C(0x5a);
	memset(&goal->dependencies[goal->dependency_count], 0xa5,
		(SG_STRATEGY_MAX_DEPENDENCIES - goal->dependency_count) *
			sizeof(goal->dependencies[0]));
	memset(&goal->conditions[goal->condition_count], 0x5a,
		(SG_STRATEGY_MAX_CONDITIONS - goal->condition_count) *
			sizeof(goal->conditions[0]));
	memset(&goal->choices[goal->choice_count], 0x3c,
		(SG_STRATEGY_MAX_CHOICES - goal->choice_count) *
			sizeof(goal->choices[0]));
	destination_bytes = (unsigned char *)destination;
	for (index = sizeof(destination->kind);
	     index < offsetof(sg_destination_ref_t, value); index++)
		destination_bytes[index] = UINT8_C(0xc3);
	value_bytes = (unsigned char *)&destination->value;
	for (index = sizeof(destination->value.flag);
	     index < sizeof(destination->value); index++)
		value_bytes[index] = UINT8_C(0x96);
}

static sg_strategy_runtime_plan_request_t Request(
	strategy_fixture_t *fixture, const sg_destination_ref_t *destination,
	sg_strategy_target_id_t target_id)
{
	sg_strategy_runtime_plan_request_t request;

	memset(&request, 0, sizeof(request));
	request.commitment_id = 900U;
	request.localized_player = &fixture->localized;
	IssueBotObservation(fixture);
	request.bot_observation = &test_bot_observation.capability;
	request.authority.rank = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
	request.authority.principal_kind = SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	request.authority.principal_id = 1U;
	request.spec.goal_count = 1U;
	InitGoal(&request.spec.goals[0], 1U, target_id, destination);
	request.execution_count = 1U;
	request.executions[0].goal_id = 1U;
	request.executions[0].target_id = target_id;
	request.executions[0].role = 4;
	return request;
}

static void CheckAuthenticatedLifeIdentity(strategy_fixture_t *fixture)
{
	sg_destination_ref_t flag = Flag(SG_DESTINATION_FLAG_HOME);
	sg_strategy_runtime_plan_request_t request;
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_t caller;
	uint64_t activation_id;
	uint64_t plan_id;
	uint16_t attempts;

	fixture->localized.subject.client_id = 0U;
	fixture->localized.subject.spawn_generation = 1U;
	fixture->localized.frame_sequence++;
	fixture->localized.localized_at_ms += 100U;
	request = Request(fixture, &flag, 120U);
	memset(&plan, 0, sizeof(plan));
	CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(plan.life_identity.client_id == 0U);
	CHECK(plan.life_identity.spawn_generation == 1U);
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U,
		fixture->localized.localized_at_ms, SG_STRATEGY_BLOCK_NONE, &output));
	activation_id = output.activation_id;
	plan_id = output.plan_id;
	attempts = caller.reducer.goals[0].attempt_count;

	/* Expire the retained frame capability before reporting death. */
	fixture->localized.frame_sequence++;
	fixture->localized.localized_at_ms += 100U;
	CHECK(SG_StrategyCallerRetireCurrentLife(&caller,
		fixture->localized.localized_at_ms, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_LIFE);
	CHECK(caller.reducer.activation.activation_id == 0U);

	fixture->localized.subject.spawn_generation = 2U;
	request = Request(fixture, &flag, 120U);
	memset(&plan, 0, sizeof(plan));
	CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U,
		fixture->localized.localized_at_ms, SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.commitment_id == request.commitment_id);
	CHECK(output.plan_id == plan_id);
	CHECK(output.activation_id != 0U);
	CHECK(output.activation_id != activation_id);
	CHECK(caller.reducer.life_id == 2U);
	CHECK(caller.reducer.goals[0].attempt_count == attempts);

	fixture->localized.subject.client_id = 1U;
	fixture->localized.frame_sequence++;
	fixture->localized.localized_at_ms += 100U;
	request = Request(fixture, &flag, 120U);
	memset(&plan, 0, sizeof(plan));
	CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U,
		fixture->localized.localized_at_ms, SG_STRATEGY_BLOCK_NONE, &output));
	SG_StrategyCallerPlanDiscard(&plan);
	SG_StrategyCallerDestroy(&caller);
	fixture->localized.subject.client_id = 0U;
}

static void CheckRejectedBinding(strategy_fixture_t *fixture,
	const sg_strategy_runtime_plan_request_t *request, uint32_t mutation)
{
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_t caller;

	memset(&plan, 0, sizeof(plan));
	IssueBotObservation(fixture);
	CHECK(SG_StrategyRuntimePlanResolve(request, &plan));
	if (plan.binding_count != 1U)
	{
		SG_StrategyCallerPlanDiscard(&plan);
		return;
	}
	switch (mutation)
	{
	case 0U:
		plan.provider_generation++;
		break;
	case 1U:
		plan.bindings[0].field_handle.rune_identity++;
		break;
	case 2U:
		plan.bindings[0].field_handle.topology_revision++;
		break;
	case 3U:
		plan.frame_sequence = 0U;
		break;
	case 4U:
		plan.bindings[0].compact_target.target_generation++;
		break;
	case 5U:
		plan.observe_target = NULL;
		break;
	case 6U:
		plan.plan_current = NULL;
		break;
	default:
		CHECK(0);
		break;
	}
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U,
		fixture->localized.localized_at_ms, SG_STRATEGY_BLOCK_NONE,
		&output));
	SG_StrategyCallerPlanDiscard(&plan);
	SG_StrategyCallerDestroy(&caller);
}

static void CheckRetiredLeaseRejected(strategy_fixture_t *fixture,
	const sg_strategy_runtime_plan_request_t *request)
{
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_t caller;

	memset(&plan, 0, sizeof(plan));
	IssueBotObservation(fixture);
	CHECK(SG_StrategyRuntimePlanResolve(request, &plan));
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U,
		fixture->localized.localized_at_ms,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(SG_RuneCompactFieldServiceInvalidateTarget(fixture->service,
		request->executions[0].target_id) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(!SG_StrategyCallerPulse(&caller, 1U,
		fixture->localized.localized_at_ms,
		SG_STRATEGY_BLOCK_NONE, &output));
	SG_StrategyCallerDestroy(&caller);
}

static void CheckLocalizedIdentityRejected(strategy_fixture_t *fixture,
	const sg_strategy_runtime_plan_request_t *request, uint8_t topology)
{
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_t caller;

	memset(&plan, 0, sizeof(plan));
	CHECK(SG_StrategyRuntimePlanResolve(request, &plan));
	CHECK(SG_StrategyCallerInit(&caller));
	if (topology != 0U)
		fixture->localized.topology_revision++;
	else
		fixture->localized.rune_identity++;
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U,
		fixture->localized.localized_at_ms, SG_STRATEGY_BLOCK_NONE,
		&output));
	if (topology != 0U)
		fixture->localized.topology_revision--;
	else
		fixture->localized.rune_identity--;
	SG_StrategyCallerPlanDiscard(&plan);
	SG_StrategyCallerDestroy(&caller);
}

static void TestCompactProvider(strategy_fixture_t *fixture)
{
	sg_strategy_runtime_plan_request_t request;
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_authority_t authority;
	sg_strategy_caller_t caller;
	sg_strategy_runtime_plan_request_t moving_request;
	sg_strategy_caller_plan_t moving_plan;
	sg_destination_ref_t flag = Flag(SG_DESTINATION_FLAG_HOME);
	sg_destination_ref_t current = Flag(SG_DESTINATION_FLAG_CURRENT);
	sg_destination_ref_t weapon = Weapon(2U);
	sg_rune_compact_field_target_t item_target;
	sg_destination_handle_t item_handle;
	sg_rune_compact_field_service_stats_t stats;
	sg_strategy_caller_output_proof_t output_proof;
	sg_strategy_caller_output_receipt_t output_receipt;
	sg_strategy_runtime_caller_query_proof_t query_proof;
	sg_strategy_runtime_bot_observation_owner_t bot_observation_owner;

	memset(&plan, 0, sizeof(plan));
	memset(&bot_observation_owner, 0, sizeof(bot_observation_owner));
	bot_observation_owner.context = &test_bot_observation;
	bot_observation_owner.validate = ValidateBotObservation;
	bot_observation_owner.current = BotObservationCurrent;
	SG_StrategyRuntimeCompactProviderClear(NULL);
	request = Request(fixture, &flag, 100U);
	CHECK(!SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(!SG_StrategyRuntimeCompactProviderInstall(NULL,
		&bot_observation_owner));
	CHECK(SG_StrategyRuntimeCompactProviderInstall(fixture->service,
		&bot_observation_owner));
	CHECK(SG_StrategyRuntimeCompactProviderInstalledFor(fixture->service));
	CHECK(SG_StrategyRuntimeCompactProviderAvailable());
	CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(plan.binding_count == 1U);
	CHECK(plan.bindings[0].field_service == fixture->service);
	CHECK(plan.bindings[0].compact_target.motion ==
		SG_RUNE_COMPACT_FIELD_TARGET_STATIC);
	CHECK(plan.bindings[0].compact_target.destination.kind ==
		SG_RUNE_COMPACT_DESTINATION_POINT);
	CHECK(plan.provider_generation != 0U);
	CHECK(plan.frame_sequence == 1U);
	CHECK(plan.frame_capability == &fixture->localized);
	SG_RuneCompactFieldServiceStats(fixture->service, &stats);
	CHECK(stats.lease_count == 1U);

	fixture->localized.frame_sequence = 2U;
	fixture->localized.localized_at_ms = 101U;
	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U, 101U,
		SG_STRATEGY_BLOCK_NONE, &output));
	SG_StrategyCallerPlanDiscard(&plan);
	request = Request(fixture, &flag, 100U);
	CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(!SG_StrategyCallerSubmit(&caller, &plan, 1U, 102U,
		SG_STRATEGY_BLOCK_NONE, &output));
	SG_StrategyCallerPlanDiscard(&plan);
	CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
	CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 101U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(caller.has_plan == 1U);
	CHECK(output.instruction.plan_id != 0U);
	CHECK(output.instruction.cost_to_go.units == 0U);
	CHECK(output.instruction.field_state ==
		SG_STRATEGY_FIELD_LOCAL_DESTINATION);
	CHECK(output.instruction.target_id == 100U);
	CHECK(output.instruction.target_generation == 1U);
	CHECK(output.commitment_id == request.commitment_id);
	CHECK(output.frame_sequence == fixture->localized.frame_sequence);
	CHECK(output.observed_at_ms == fixture->localized.localized_at_ms);
	{
		sg_rune_compact_field_mechanism_snapshot_t stale_mechanisms;
		sg_rune_compact_field_local_context_t query_context;
		sg_rune_compact_field_result_t queried;
		sg_strategy_runtime_caller_query_snapshot_t query_snapshot;
		sg_strategy_caller_output_t tampered;

		CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &queried,
			&query_context, &output_proof, &query_proof));
		CHECK(SG_StrategyCallerOutputProofCurrent(&caller, &output,
			&output_proof));
		tampered = output;
		tampered.activation_id++;
		CHECK(!SG_StrategyCallerOutputProofCurrent(&caller, &tampered,
			&output_proof));
		CHECK(SG_StrategyCallerOutputProofConsume(&caller, &output,
			&output_proof, &output_receipt));
		CHECK(SG_StrategyCallerOutputReceiptMatchesProof(&caller, &output,
			&output_proof, &output_receipt));
		CHECK(SG_StrategyCallerOutputReceiptLineageMatches(&output_proof,
			&output_receipt));
		{
			sg_strategy_runtime_caller_query_proof_t hostile_query_proof =
				query_proof;
			sg_strategy_caller_output_receipt_t hostile_receipt =
				output_receipt;

			hostile_query_proof.opaque[0] ^= UINT8_C(1);
			hostile_receipt.opaque[31] ^= UINT8_C(1);
			CHECK(!SG_StrategyCallerOutputReceiptLineageMatches(&output_proof,
				&hostile_receipt));
			CHECK(!SG_StrategyRuntimeCallerQueryReceiptRelease(&caller,
				&output, &output_receipt, &hostile_query_proof));
			CHECK(SG_StrategyRuntimeCallerQueryReceiptRelease(&caller,
				&output, &output_receipt, &query_proof));
			CHECK(!SG_StrategyRuntimeCallerQueryReceiptRelease(&caller,
				&output, &output_receipt, &query_proof));
		}
		CHECK(!SG_StrategyCallerOutputProofCurrent(&caller, &output,
			&output_proof));
		CHECK(SG_StrategyCallerOutputReceiptCurrent(&caller, &output,
			&output_receipt));
		CHECK(!SG_StrategyCallerOutputProofConsume(&caller, &output,
			&output_proof, &output_receipt));
		CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &queried,
			&query_context, &output_proof, &query_proof));
		CHECK(!SG_StrategyCallerOutputReceiptCurrent(&caller, &output,
			&output_receipt));
		{
			sg_strategy_caller_output_receipt_t retired_receipt;
			sg_strategy_caller_output_proof_t replacement_proof;

			CHECK(SG_StrategyCallerOutputProofConsume(&caller, &output,
				&output_proof, &retired_receipt));
			CHECK(SG_StrategyCallerOutputProofIssue(&caller, &output,
				&replacement_proof));
			CHECK(!SG_StrategyCallerOutputReceiptCurrent(&caller, &output,
				&retired_receipt));
			CHECK(SG_StrategyRuntimeCallerQueryReceiptRelease(&caller,
				&output, &retired_receipt, &query_proof));
		}
		{
			sg_strategy_caller_t other_caller;
			sg_strategy_caller_plan_t other_plan;
			sg_strategy_caller_output_t other_output;
			sg_strategy_caller_output_proof_t other_proof;

			memset(&other_plan, 0, sizeof(other_plan));
			CHECK(SG_StrategyRuntimePlanResolve(&request, &other_plan));
			CHECK(SG_StrategyCallerInit(&other_caller));
			CHECK(SG_StrategyCallerSubmit(&other_caller, &other_plan, 1U,
				fixture->localized.localized_at_ms,
				SG_STRATEGY_BLOCK_NONE, &other_output));
			CHECK(!SG_StrategyCallerOutputProofCurrent(&other_caller,
				&other_output, &output_proof));
			CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(
				&other_caller, &other_output, &fixture->localized, NULL,
				NULL, &test_bot_observation.capability, &queried,
				&query_context, &other_proof, &query_proof));
			CHECK(!SG_StrategyCallerOutputProofCurrent(&caller, &output,
				&other_proof));
			SG_StrategyCallerDestroy(&other_caller);
			CHECK(!SG_StrategyCallerOutputProofCurrent(&other_caller,
				&other_output, &other_proof));
			CHECK(SG_StrategyCallerInit(&other_caller));
			CHECK(!SG_StrategyCallerOutputProofCurrent(&other_caller,
				&other_output, &other_proof));
			SG_StrategyCallerDestroy(&other_caller);
		}
		CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &queried, &query_context,
			&output_proof, &query_proof));
		CHECK(queried.kind == SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION);
		CHECK(query_context.frame_sequence ==
			fixture->localized.frame_sequence);
		CHECK(query_context.hook_phase == SG_HOST_HOOK_IDLE);
		CHECK(query_context.mechanisms == NULL);
		CHECK(query_context.portal_roots == NULL);
		CHECK(SG_StrategyCallerOutputProofCurrent(&caller, &output,
			&output_proof));
		CHECK(SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
			&output_proof, &query_context, &queried, &query_proof,
			&query_snapshot));
		{
			sg_rune_compact_field_local_context_t changed_context =
				query_context;
			sg_rune_compact_field_result_t changed_result = queried;
			sg_strategy_runtime_caller_query_proof_t hostile_proof;

			changed_context.hook_length = 1.0f;
			CHECK(!SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
				&output_proof, &changed_context, &queried, &query_proof,
				&query_snapshot));
			changed_context = query_context;
			changed_context.target_radius = 1.0f;
			CHECK(!SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
				&output_proof, &changed_context, &queried, &query_proof,
				&query_snapshot));
			changed_result.current_cell.value++;
			CHECK(!SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
				&output_proof, &query_context, &changed_result, &query_proof,
				&query_snapshot));
			memset(&hostile_proof, 0, sizeof(hostile_proof));
			CHECK(!SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
				&output_proof, &query_context, &queried, &hostile_proof,
				&query_snapshot));
			{
				sg_strategy_caller_output_proof_t reissued_proof;
				uint64_t encoded_owner_id = 0U;

				memcpy(&encoded_owner_id, output_proof.opaque,
					sizeof(encoded_owner_id));
				CHECK(encoded_owner_id == caller.output_authority_owner_id);
				CHECK(SG_StrategyCallerOutputProofIssue(&caller, &output,
					&reissued_proof));
				CHECK(!SG_StrategyRuntimeCallerQueryProofCurrent(&caller,
					&output, &reissued_proof, &query_context, &queried,
					&query_proof, &query_snapshot));
			}
			hostile_proof = query_proof;
			CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
				&output, &fixture->localized, NULL, NULL,
				&test_bot_observation.capability, &queried, &query_context,
				&output_proof, &query_proof));
			CHECK(!SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
				&output_proof, &query_context, &queried, &hostile_proof,
				&query_snapshot));
			CHECK(SG_StrategyRuntimeCallerQueryProofCurrent(&caller, &output,
				&output_proof, &query_context, &queried, &query_proof,
				&query_snapshot));
		}
		memset(&stale_mechanisms, 0, sizeof(stale_mechanisms));
		stale_mechanisms.model_identity = &fixture->model.identity;
		stale_mechanisms.frame_sequence =
			fixture->localized.frame_sequence + 1U;
		CHECK(!SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, &stale_mechanisms, NULL,
			&test_bot_observation.capability, &queried, &query_context,
			&output_proof, &query_proof));
		output.compact_target.target_generation++;
		CHECK(!SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &queried, &query_context,
			&output_proof, &query_proof));
		output.compact_target.target_generation--;
		output.frame_sequence++;
		CHECK(!SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &queried, &query_context,
			&output_proof, &query_proof));
		output.frame_sequence--;
	}
	{
		const uint64_t activation = output.activation_id;
		const uint64_t plan_id = output.plan_id;
		const uint64_t commitment_id = output.commitment_id;

		CHECK(SG_StrategyCallerPulse(&caller, 1U, 101U,
			SG_STRATEGY_BLOCK_NONE, &output));
		CHECK(!SG_StrategyCallerOutputProofCurrent(&caller, &output,
			&output_proof));
		CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
		CHECK(output.activation_id == activation);
		CHECK(output.plan_id == plan_id);
		CHECK(!SG_StrategyCallerPulse(&caller, 1U, 102U,
			SG_STRATEGY_BLOCK_NONE, &output));

		fixture->localized.frame_sequence = 3U;
		fixture->localized.localized_at_ms = 200U;
		CHECK(!SG_StrategyCallerPulse(&caller, 1U, 200U,
			SG_STRATEGY_BLOCK_NONE, &output));
		request = Request(fixture, &flag, 100U);
		PoisonInactivePlanRepresentation(&request.spec);
		CHECK(SG_StrategyRuntimePlanResolve(&request, &plan));
		CHECK(SG_StrategyCallerSubmit(&caller, &plan, 1U, 200U,
			SG_STRATEGY_BLOCK_NONE, &output));
		CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
		CHECK(output.activation_id == activation);
		CHECK(output.plan_id == plan_id);
		CHECK(output.commitment_id == commitment_id);
	}
	if (output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE ||
		output.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED)
	{
		CHECK(output.field_service == fixture->service);
		CHECK(output.field_handle.service_identity != 0U);
	}
	/* Clearing the provider revokes the retained generation before the
	 * service is destroyed. No stale plan operation may expose its handle. */
	authority = caller.plan.authority;
	{
		sg_rune_compact_field_result_t queried;
		sg_rune_compact_field_local_context_t query_context;

		CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
			&output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &queried,
			&query_context, &output_proof, &query_proof));
	}
	SG_StrategyRuntimeCompactProviderClear(fixture->service);
	CHECK(!SG_StrategyRuntimeCompactProviderAvailable());
	CHECK(!SG_StrategyCallerOutputProofCurrent(&caller, &output,
		&output_proof));
	CHECK(!SG_StrategyCallerPulse(&caller, 1U, 200U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(!SG_StrategyCallerCancel(&caller, &authority, 1U, 200U, &output));
	CHECK(SG_StrategyCallerRelease(&caller, &authority, 1U, 200U, &output));
	SG_StrategyCallerDestroy(&caller);
	SG_RuneCompactFieldServiceStats(fixture->service, &stats);
	CHECK(stats.lease_count == 0U);

	CHECK(SG_RuneCompactFieldServiceResolveSemanticTarget(fixture->service,
		103U, &weapon, NULL, &item_target, &item_handle));
	CHECK(item_target.destination.kind == SG_RUNE_COMPACT_DESTINATION_ITEM);
	CHECK(item_target.destination.value.item.value == 1U);
	CHECK(item_handle.kind == SG_DESTINATION_WEAPON);
	CHECK(SG_StrategyRuntimeCompactProviderInstall(fixture->service,
		&bot_observation_owner));
	{
		sg_destination_ref_t remote = TeamFlag(2U,
			SG_DESTINATION_FLAG_HOME);
		sg_strategy_runtime_plan_request_t remote_request =
			Request(fixture, &remote, 104U);
		sg_strategy_caller_plan_t remote_plan;
		sg_strategy_caller_output_t remote_output;
		sg_strategy_caller_t remote_caller;
		sg_rune_compact_field_result_t remote_result;
		sg_rune_compact_field_local_context_t remote_context;

		memset(&remote_plan, 0, sizeof(remote_plan));
		CHECK(SG_StrategyRuntimePlanResolve(&remote_request, &remote_plan));
		CHECK(SG_StrategyCallerInit(&remote_caller));
		CHECK(SG_StrategyCallerSubmit(&remote_caller, &remote_plan, 1U,
			fixture->localized.localized_at_ms, SG_STRATEGY_BLOCK_NONE,
			&remote_output));
		CHECK(remote_output.instruction.kind ==
			SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION);
		CHECK(remote_output.instruction.field_state ==
			SG_STRATEGY_FIELD_DISCONNECTED);
		CHECK(remote_output.field_service == fixture->service);
		CHECK(remote_output.commitment_id == remote_request.commitment_id);
		CHECK(remote_output.frame_sequence ==
			fixture->localized.frame_sequence);
		CHECK(!SG_StrategyRuntimeQueryCallerOutputWithContext(&remote_caller,
			&remote_output, &fixture->localized, NULL, NULL,
			&test_bot_observation.capability, &remote_result, &remote_context,
			&output_proof, &query_proof));
		CHECK(remote_result.kind == SG_RUNE_COMPACT_FIELD_DISCONNECTED);
		SG_StrategyCallerDestroy(&remote_caller);
	}

	CheckRejectedBinding(fixture, &request, 0U);
	CheckRejectedBinding(fixture, &request, 1U);
	CheckRejectedBinding(fixture, &request, 2U);
	CheckRejectedBinding(fixture, &request, 3U);
	CheckRejectedBinding(fixture, &request, 4U);
	CheckRejectedBinding(fixture, &request, 5U);
	CheckRejectedBinding(fixture, &request, 6U);
	CheckLocalizedIdentityRejected(fixture, &request, 0U);
	CheckLocalizedIdentityRejected(fixture, &request, 1U);
	CheckAuthenticatedLifeIdentity(fixture);
	CheckRetiredLeaseRejected(fixture, &request);

	moving_request = Request(fixture, &current, 101U);
	moving_request.executions[0].live_pose.present = 1U;
	moving_request.executions[0].live_pose.generation = 2U;
	moving_request.executions[0].live_pose.position[0] = 1.0f;
	moving_request.executions[0].live_pose.observed_at_ms = 120U;
	memset(&moving_plan, 0, sizeof(moving_plan));
	CHECK(SG_StrategyRuntimePlanResolve(&moving_request, &moving_plan));
	CHECK(moving_plan.bindings[0].compact_target.motion ==
		SG_RUNE_COMPACT_FIELD_TARGET_MOVING);
	CHECK(moving_plan.bindings[0].compact_target.target_generation == 2U);
	CHECK(moving_plan.bindings[0].destination.kind == SG_DESTINATION_FLAG);
	SG_StrategyCallerPlanDiscard(&moving_plan);
	moving_request.executions[0].live_pose.generation = 3U;
	moving_request.executions[0].live_pose.observed_at_ms = 121U;
	CHECK(SG_StrategyRuntimePlanResolve(&moving_request, &moving_plan));
	CHECK(moving_plan.bindings[0].compact_target.target_generation == 3U);
	SG_StrategyCallerPlanDiscard(&moving_plan);
	SG_StrategyRuntimeCompactProviderClear(fixture->service);
	CHECK(!SG_StrategyRuntimeCompactProviderAvailable());
}

int main(void)
{
	strategy_fixture_t fixture;

	TestFieldObservations();
	InitFixture(&fixture);
	if (fixture.service != NULL)
	{
		TestCompactProvider(&fixture);
		SG_RuneCompactFieldServiceDestroy(fixture.service);
	}
	else
		failures++;
	if (failures != 0)
	{
		fprintf(stderr, "%d compact strategy caller tests failed\n", failures);
		return 1;
	}
	puts("sg_strategy_caller_test: PASS");
	return 0;
}
