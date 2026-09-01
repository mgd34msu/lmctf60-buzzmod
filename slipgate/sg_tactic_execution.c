#include "sg_tactic_execution.h"

#include <limits.h>
#include <string.h>

static int TacticCellCenter(const sg_rune_compact_model_t *model,
	sg_rune_compact_cell_index_t cell, float point_out[3])
{
	uint32_t axis;

	if (model == NULL || point_out == NULL || model->cells == NULL ||
		cell.value >= model->cell_count)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const int64_t total =
			(int64_t)model->cells[cell.value].bounds.mins.value[axis] +
			(int64_t)model->cells[cell.value].bounds.maxs.value[axis];

		point_out[axis] = (float)((double)total / 16.0);
	}
	return 1;
}

static int TacticPortalCenter(const sg_rune_compact_model_t *model,
	sg_rune_compact_portal_index_t portal, float point_out[3])
{
	const sg_rune_compact_facet_t *facet;
	uint32_t axis;
	uint32_t offset;

	if (model == NULL || point_out == NULL || model->portals == NULL ||
		model->facets == NULL || model->vertices == NULL ||
		portal.value >= model->portal_count ||
		model->portals[portal.value].facet.value >= model->facet_count)
		return 0;
	facet = &model->facets[model->portals[portal.value].facet.value];
	if (facet->vertices.count == 0U ||
		facet->vertices.first > model->vertex_count ||
		facet->vertices.count > model->vertex_count - facet->vertices.first)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		int64_t total = 0;

		for (offset = 0U; offset < facet->vertices.count; offset++)
		{
			const int32_t coordinate = model->vertices[
				facet->vertices.first + offset].value[axis];

			if ((coordinate > 0 && total > INT64_MAX - coordinate) ||
				(coordinate < 0 && total < INT64_MIN - coordinate))
				return 0;
			total += coordinate;
		}
		point_out[axis] = (float)((double)total /
			((double)facet->vertices.count * 8.0));
	}
	return 1;
}

static int TacticStepCostsValid(
	const sg_rune_compact_field_step_t *step)
{
	return step != NULL &&
		step->cost_to_go.units != SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE &&
		step->next_cost_to_go.units !=
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE &&
		step->next_cost_to_go.units < step->cost_to_go.units &&
		step->target_stance >= SG_RUNE_COMPACT_FIELD_STANDING &&
		step->target_stance < SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
}

static int TacticMechanismsValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_mechanism_requirements_t *requirements)
{
	uint32_t index;

	if (model == NULL || requirements == NULL || model->portals == NULL ||
		requirements->portal.value >= model->portal_count ||
		requirements->mechanism_count == 0U ||
		requirements->mechanisms == NULL ||
		requirements->state >=
			SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT)
		return 0;
	for (index = 0U; index < requirements->mechanism_count; index++)
		if (requirements->mechanisms[index].value >=
			model->mechanism_authority_count)
			return 0;
	return 1;
}

int SG_TacticExecutionDispatch(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_result_t *result,
	sg_tactic_execution_t *execution_out)
{
	sg_tactic_execution_t execution;

	if (execution_out == NULL)
		return 0;
	memset(execution_out, 0, sizeof(*execution_out));
	if (model == NULL || result == NULL || model->cells == NULL ||
		result->current_cell.value >= model->cell_count)
		return 0;
	memset(&execution, 0, sizeof(execution));
	execution.current_cell = result->current_cell;
	execution.target_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	execution.portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	execution.cost_to_go.units = SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	execution.next_cost_to_go.units = SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	switch (result->kind)
	{
	case SG_RUNE_COMPACT_FIELD_DISCONNECTED:
		execution.kind = SG_TACTIC_EXECUTION_DISCONNECTED;
		break;
	case SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION:
		execution.kind = SG_TACTIC_EXECUTION_LOCAL_DESTINATION;
		execution.destination = result->value.destination;
		break;
	case SG_RUNE_COMPACT_FIELD_CELL_DESTINATION:
		if (result->value.destination.kind !=
			SG_RUNE_COMPACT_DESTINATION_CELL ||
			result->value.destination.value.cell.value !=
				result->current_cell.value)
			return 0;
		execution.kind = SG_TACTIC_EXECUTION_CELL_DESTINATION;
		execution.destination = result->value.destination;
		break;
	case SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED:
		if (!TacticMechanismsValid(model, &result->value.requirements))
			return 0;
		execution.kind = SG_TACTIC_EXECUTION_MECHANISMS_REQUIRED;
		execution.portal = result->value.requirements.portal;
		execution.mechanism_count =
			result->value.requirements.mechanism_count;
		execution.mechanism_state = result->value.requirements.state;
		break;
	case SG_RUNE_COMPACT_FIELD_BLOCKED_NOW:
		execution.kind = SG_TACTIC_EXECUTION_BLOCKED_NOW;
		break;
	case SG_RUNE_COMPACT_FIELD_STEP:
		if (!TacticStepCostsValid(&result->value.step))
			return 0;
		execution.target_stance = result->value.step.target_stance;
		execution.cost_to_go = result->value.step.cost_to_go;
		execution.next_cost_to_go = result->value.step.next_cost_to_go;
		switch (result->value.step.kind)
		{
		case SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL:
			execution.target_cell =
				result->value.step.value.portal.next_cell;
			execution.portal =
				result->value.step.value.portal.next_portal;
			if (execution.target_cell.value >= model->cell_count ||
				!TacticPortalCenter(model, execution.portal,
					execution.target_point))
				return 0;
			execution.kind = SG_TACTIC_EXECUTION_PORTAL_STEP;
			execution.target_point_present = 1U;
			break;
		case SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT:
			execution.target_cell =
				result->value.step.value.direct.next_cell;
			if (!TacticCellCenter(model, execution.target_cell,
				execution.target_point))
				return 0;
			execution.kind = SG_TACTIC_EXECUTION_DIRECT_STEP;
			execution.target_point_present = 1U;
			break;
		case SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE:
			execution.kind = SG_TACTIC_EXECUTION_STANCE_STEP;
			execution.target_cell = result->current_cell;
			break;
		case SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT:
		default:
			return 0;
		}
		break;
	case SG_RUNE_COMPACT_FIELD_RESULT_KIND_COUNT:
	default:
		return 0;
	}
	*execution_out = execution;
	return 1;
}

static int TacticSelectedSuccessorMatchesStep(
	const sg_rune_compact_field_result_t *result,
	const sg_tactic_result_t *selection)
{
	sg_rune_compact_cell_index_t target_cell;

	if (result == NULL || selection == NULL ||
		result->kind != SG_RUNE_COMPACT_FIELD_STEP)
		return 0;
	target_cell = result->current_cell;
	switch (result->value.step.kind)
	{
	case SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL:
		target_cell = result->value.step.value.portal.next_cell;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT:
		target_cell = result->value.step.value.direct.next_cell;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE:
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT:
	default:
		return 0;
	}
	return selection->successor.cell.value == target_cell.value &&
		selection->successor.stance == result->value.step.target_stance;
}

/* The selector has already authenticated the exact live probe and composed
 * its nominal Q52.12 cost.  Dispatch must retain that strict source-state
 * descent, but it must not repeat field evaluation or reconstruct a local
 * residual from field costs. */
static int TacticSelectedProgressDescends(
	const sg_rune_compact_field_result_t *result,
	const sg_tactic_result_t *selection)
{
	return result != NULL && selection != NULL &&
		result->kind == SG_RUNE_COMPACT_FIELD_STEP &&
		TacticStepCostsValid(&result->value.step) &&
		selection->nominal_cost.units !=
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE &&
		selection->nominal_cost.units < result->value.step.cost_to_go.units;
}

int SG_TacticExecutionDispatchSelected(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_result_t *result,
	const sg_tactic_result_t *selection,
	sg_tactic_execution_t *execution_out)
{
	sg_tactic_execution_t execution;

	if (execution_out == NULL)
		return 0;
	memset(execution_out, 0, sizeof(*execution_out));
	if (selection == NULL || !SG_TacticResultValid(selection) ||
		!SG_TacticExecutionDispatch(model, result, &execution) ||
		result->kind != SG_RUNE_COMPACT_FIELD_STEP)
		return 0;
	if (selection->status == SG_TACTIC_RESULT_PROGRESS &&
		(!TacticSelectedSuccessorMatchesStep(result, selection) ||
		 !TacticSelectedProgressDescends(result, selection)))
		return 0;
	execution.selection_present = 1U;
	execution.selection_status = selection->status;
	execution.selection_failure = selection->failure;
	execution.capability = selection->capability;
	execution.target_phase = selection->target_phase;
	if (selection->status == SG_TACTIC_RESULT_PROGRESS ||
		selection->status == SG_TACTIC_RESULT_HOLD)
		execution.target_hook_phase = selection->successor.hook_phase;
	else
		execution.target_hook_phase = SG_HOST_HOOK_IDLE;
	if (selection->mechanism_handoff_valid != 0U)
	{
		execution.mechanism_handoff = selection->mechanism_handoff;
		execution.mechanism_handoff_valid = 1U;
	}
	*execution_out = execution;
	return 1;
}
