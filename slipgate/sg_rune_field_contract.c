#include "sg_rune_dynamics_model.h"

#include <math.h>

static int IntervalValid(const sg_rune_interval_t *interval)
{
	return interval && isfinite(interval->min_value) &&
		isfinite(interval->max_value) &&
		interval->min_value <= interval->max_value;
}

static int Interval3Valid(const sg_rune_interval3_t *interval)
{
	return interval && IntervalValid(&interval->x) &&
		IntervalValid(&interval->y) && IntervalValid(&interval->z);
}

static int VectorValid(const sg_rune_vec3_t *vector)
{
	return vector && isfinite(vector->value[0]) &&
		isfinite(vector->value[1]) && isfinite(vector->value[2]);
}

static int ErrorIntervalValid(const sg_rune_interval_t *interval)
{
	return IntervalValid(interval) && interval->min_value <= 0.0f &&
		interval->max_value >= 0.0f;
}

static int ErrorInterval3Valid(const sg_rune_interval3_t *interval)
{
	return interval && ErrorIntervalValid(&interval->x) &&
		ErrorIntervalValid(&interval->y) &&
		ErrorIntervalValid(&interval->z);
}

static int CostBoundsValid(const sg_rune_cost_bounds_t *cost)
{
	return cost && cost->lower_us <= cost->upper_us &&
		cost->upper_us < SG_RUNE_FIELD_COST_INFINITE;
}

int SG_RuneFieldErrorContractValid(
	const sg_rune_field_error_contract_t *contract)
{
	return contract && SG_RuneFieldErrorContractIdValid(&contract->id) &&
		contract->cost_quantum_us != 0U &&
		contract->maximum_value_width_us != 0U &&
		contract->maximum_bellman_residual_us != 0U &&
		ErrorInterval3Valid(&contract->position_error) &&
		ErrorInterval3Valid(&contract->velocity_error) &&
		ErrorIntervalValid(&contract->time_error);
}

int SG_LocalizedFieldStateValid(const sg_localized_field_state_t *state)
{
	return state && state->rune_identity != 0U &&
		state->topology_revision != 0U && state->pose_revision != 0U &&
		state->sampled_at_ms != 0U &&
		SG_RuneStateChartIdValid(&state->chart) &&
		SG_RuneStateModeValid(&state->mode) && VectorValid(&state->position) &&
		VectorValid(&state->velocity) && isfinite(state->elapsed_ms) &&
		state->elapsed_ms >= 0.0f;
}

int SG_FieldEnvironmentValid(const sg_field_environment_t *environment)
{
	return environment && environment->rune_identity != 0U &&
		environment->topology_revision != 0U &&
		environment->environment_revision != 0U &&
		environment->sampled_at_ms != 0U &&
		environment->authenticated == 1U && environment->reserved[0] == 0U &&
		environment->reserved[1] == 0U && environment->reserved[2] == 0U &&
		environment->reserved[3] == 0U && environment->reserved[4] == 0U &&
		environment->reserved[5] == 0U && environment->reserved[6] == 0U;
}

int SG_FieldHandleValid(const sg_field_handle_t *handle)
{
	return handle && handle->service_identity != 0U &&
		handle->rune_identity != 0U && handle->topology_revision != 0U &&
		handle->terminal_generation != 0U && handle->field_generation != 0U;
}

int SG_FieldGuidanceValid(const sg_field_guidance_t *guidance)
{
	size_t index;

	if (!guidance || !SG_FieldHandleValid(&guidance->field) ||
	    guidance->pose_revision == 0U || guidance->sampled_at_ms == 0U ||
	    guidance->kind < SG_FIELD_GUIDANCE_TERMINAL ||
	    guidance->kind >= SG_FIELD_GUIDANCE_KIND_COUNT)
		return 0;
	if (guidance->kind == SG_FIELD_GUIDANCE_TERMINAL)
		return guidance->value.terminal.arrival_cost.lower_us == 0U &&
			guidance->value.terminal.arrival_cost.upper_us == 0U &&
			guidance->value.terminal.residual_bound_us !=
				SG_RUNE_FIELD_COST_INFINITE;
	if (guidance->kind == SG_FIELD_GUIDANCE_UNREACHABLE)
		return guidance->value.unreachable.arrival_cost.lower_us ==
			SG_RUNE_FIELD_COST_INFINITE &&
			guidance->value.unreachable.arrival_cost.upper_us ==
				SG_RUNE_FIELD_COST_INFINITE;
	if (!CostBoundsValid(&guidance->value.descent.arrival_cost) ||
	    guidance->value.descent.residual_bound_us ==
		SG_RUNE_FIELD_COST_INFINITE ||
	    !Interval3Valid(&guidance->value.descent.spatial_subgradient) ||
	    !Interval3Valid(&guidance->value.descent.velocity_subgradient) ||
	    !IntervalValid(&guidance->value.descent.time_subgradient) ||
	    !guidance->value.descent.controls.values ||
	    guidance->value.descent.controls.count == 0U)
		return 0;
	for (index = 0U; index < guidance->value.descent.controls.count; index++)
	{
		const sg_rune_field_descent_t *descent =
			&guidance->value.descent.controls.values[index];
		if (!SG_RuneControlFiberIdValid(&descent->control) ||
		    descent->minimum_descent_us == 0U ||
		    !CostBoundsValid(&descent->endpoint_cost) ||
		    descent->endpoint_cost.upper_us >
			guidance->value.descent.arrival_cost.lower_us ||
		    descent->minimum_descent_us >
			guidance->value.descent.arrival_cost.lower_us -
				descent->endpoint_cost.upper_us)
			return 0;
	}
	return 1;
}
