#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_tactic_execution.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_portal_t portals[1];
	sg_rune_q8_vec3_t vertices[3];
} fixture_t;

static void FixtureInit(fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->cells[0].bounds.maxs.value[0] = 80;
	fixture->cells[0].bounds.maxs.value[1] = 80;
	fixture->cells[0].bounds.maxs.value[2] = 80;
	fixture->cells[1].bounds.mins.value[0] = 80;
	fixture->cells[1].bounds.mins.value[1] = 80;
	fixture->cells[1].bounds.mins.value[2] = 80;
	fixture->cells[1].bounds.maxs.value[0] = 160;
	fixture->cells[1].bounds.maxs.value[1] = 160;
	fixture->cells[1].bounds.maxs.value[2] = 160;
	fixture->vertices[1].value[0] = 80;
	fixture->vertices[2].value[1] = 80;
	fixture->facets[0].vertices.count = 3U;
	fixture->portals[0].facet.value = 0U;
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 2U;
	fixture->model.facets = fixture->facets;
	fixture->model.facet_count = 1U;
	fixture->model.vertices = fixture->vertices;
	fixture->model.vertex_count = 3U;
	fixture->model.portals = fixture->portals;
	fixture->model.portal_count = 1U;
	fixture->model.mechanism_authority_count = 1U;
}

static sg_rune_compact_field_result_t BaseResult(void)
{
	sg_rune_compact_field_result_t result;

	memset(&result, 0, sizeof(result));
	result.current_cell.value = 0U;
	return result;
}

static void TestResultKinds(const fixture_t *fixture)
{
	sg_rune_compact_mechanism_index_t mechanism = { 0U };
	sg_rune_compact_field_result_t result;
	sg_tactic_execution_t execution;

	result = BaseResult();
	result.kind = SG_RUNE_COMPACT_FIELD_DISCONNECTED;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_DISCONNECTED);

	result = BaseResult();
	result.kind = SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION;
	result.value.destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
	result.value.destination.value.point.value[0] = 24;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_LOCAL_DESTINATION);
	CHECK(execution.destination.value.point.value[0] == 24);

	result = BaseResult();
	result.kind = SG_RUNE_COMPACT_FIELD_CELL_DESTINATION;
	result.value.destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	result.value.destination.value.cell.value = 0U;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_CELL_DESTINATION);

	result = BaseResult();
	result.kind = SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED;
	result.value.requirements.portal.value = 0U;
	result.value.requirements.mechanisms = &mechanism;
	result.value.requirements.mechanism_count = 1U;
	result.value.requirements.state =
		SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_MECHANISMS_REQUIRED);
	CHECK(execution.mechanism_count == 1U);
	CHECK(execution.mechanism_state ==
		SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN);

	result = BaseResult();
	result.kind = SG_RUNE_COMPACT_FIELD_BLOCKED_NOW;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_BLOCKED_NOW);
}

static void TestStepKinds(const fixture_t *fixture)
{
	sg_rune_compact_field_result_t result = BaseResult();
	sg_tactic_execution_t execution;

	result.kind = SG_RUNE_COMPACT_FIELD_STEP;
	result.value.step.cost_to_go.units = 20U;
	result.value.step.next_cost_to_go.units = 10U;
	result.value.step.target_stance = SG_RUNE_COMPACT_FIELD_STANDING;
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
	result.value.step.value.portal.next_cell.value = 1U;
	result.value.step.value.portal.next_portal.value = 0U;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_PORTAL_STEP);
	CHECK(execution.target_cell.value == 1U);
	CHECK(execution.portal.value == 0U);
	CHECK(fabsf(execution.target_point[0] - (10.0f / 3.0f)) < 0.001f);
	CHECK(fabsf(execution.target_point[1] - (10.0f / 3.0f)) < 0.001f);

	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT;
	result.value.step.value.direct.next_cell.value = 1U;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_DIRECT_STEP);
	CHECK(execution.target_point[0] == 15.0f);
	CHECK(execution.target_point[1] == 15.0f);

	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE;
	result.value.step.target_stance = SG_RUNE_COMPACT_FIELD_CROUCHING;
	CHECK(SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
	CHECK(execution.kind == SG_TACTIC_EXECUTION_STANCE_STEP);
	CHECK(execution.target_cell.value == 0U);
	CHECK(execution.target_stance == SG_RUNE_COMPACT_FIELD_CROUCHING);

	result.value.step.next_cost_to_go.units = 20U;
	CHECK(!SG_TacticExecutionDispatch(&fixture->model, &result, &execution));
}

static void TestSelectedStep(const fixture_t *fixture)
{
	sg_rune_compact_field_result_t result = BaseResult();
	sg_tactic_result_t selection;
	sg_tactic_execution_t execution;

	result.kind = SG_RUNE_COMPACT_FIELD_STEP;
	result.value.step.cost_to_go.units = 20U;
	result.value.step.next_cost_to_go.units = 10U;
	result.value.step.target_stance = SG_RUNE_COMPACT_FIELD_STANDING;
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT;
	result.value.step.value.direct.next_cell.value = 1U;
	memset(&selection, 0, sizeof(selection));
	selection.status = SG_TACTIC_RESULT_PROGRESS;
	selection.capability = SG_TACTIC_CAPABILITY_WALK;
	selection.successor.cell.value = 1U;
	selection.successor.stance = SG_RUNE_COMPACT_FIELD_STANDING;
	selection.successor.hook_phase = SG_HOST_HOOK_IDLE;
	selection.target_phase = SG_TACTIC_PHASE_GROUND;
	selection.nominal_cost.units = 15U;
	selection.progress = 0.25f;
	CHECK(SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.selection_present == 1U);
	CHECK(execution.selection_status == SG_TACTIC_RESULT_PROGRESS);
	CHECK(execution.capability == SG_TACTIC_CAPABILITY_WALK);

	selection.nominal_cost.units = result.value.step.cost_to_go.units;
	CHECK(!SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.selection_present == 0U);
	selection.nominal_cost.units =
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE - UINT64_C(1);
	CHECK(!SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.selection_present == 0U);
	selection.nominal_cost.units = 15U;
	selection.successor.stance = SG_RUNE_COMPACT_FIELD_CROUCHING;
	CHECK(!SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.selection_present == 0U);
	selection.successor.stance = SG_RUNE_COMPACT_FIELD_STANDING;

	selection.capability = SG_TACTIC_CAPABILITY_HOOK;
	selection.target_phase = SG_TACTIC_PHASE_HOOK;
	selection.successor.hook_phase = SG_HOST_HOOK_IN_FLIGHT;
	CHECK(SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.target_hook_phase == SG_HOST_HOOK_IN_FLIGHT);

	selection.capability = SG_TACTIC_CAPABILITY_WALK;
	selection.successor.cell.value = 0U;
	CHECK(!SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));

	memset(&selection, 0, sizeof(selection));
	selection.status = SG_TACTIC_RESULT_HOLD;
	selection.capability = SG_TACTIC_CAPABILITY_WAIT;
	selection.successor.cell.value = 0U;
	selection.successor.stance = SG_RUNE_COMPACT_FIELD_STANDING;
	selection.successor.hook_phase = SG_HOST_HOOK_IDLE;
	selection.target_phase = SG_TACTIC_PHASE_GROUND;
	selection.nominal_cost.units = 20U;
	CHECK(SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.selection_status == SG_TACTIC_RESULT_HOLD);

	memset(&selection, 0, sizeof(selection));
	selection.status = SG_TACTIC_RESULT_RETRY;
	selection.failure = SG_TACTIC_FAILURE_NO_LEGAL_CAPABILITY;
	selection.capability = SG_TACTIC_CAPABILITY_WALK;
	selection.successor.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	selection.successor.stance = SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	selection.successor.hook_phase =
		(sg_host_hook_phase_t)(SG_HOST_HOOK_COAST + 1);
	selection.target_phase = SG_TACTIC_PHASE_COUNT;
	CHECK(SG_TacticExecutionDispatchSelected(&fixture->model, &result,
		&selection, &execution));
	CHECK(execution.selection_status == SG_TACTIC_RESULT_RETRY);
}

int main(void)
{
	fixture_t fixture;

	FixtureInit(&fixture);
	TestResultKinds(&fixture);
	TestStepKinds(&fixture);
	TestSelectedStep(&fixture);
	if (failures != 0)
		return 1;
	puts("sg_tactic_execution_test: ok");
	return 0;
}
