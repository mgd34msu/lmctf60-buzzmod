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

static int StableIdOrder(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
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
	size_t index;

	if (!environment || environment->rune_identity == 0U ||
	    environment->topology_revision == 0U ||
	    environment->environment_revision == 0U ||
	    environment->sampled_at_ms == 0U ||
	    environment->authority_identity == 0U ||
	    (environment->guard_count != 0U && !environment->guards) ||
	    (environment->event_slab_count != 0U && !environment->event_slabs) ||
	    environment->authenticated != 1U || environment->reserved[0] != 0U ||
	    environment->reserved[1] != 0U || environment->reserved[2] != 0U ||
	    environment->reserved[3] != 0U || environment->reserved[4] != 0U ||
	    environment->reserved[5] != 0U || environment->reserved[6] != 0U)
		return 0;
	for (index = 0U; index < environment->guard_count; index++)
	{
		const sg_field_guard_state_t *guard = &environment->guards[index];
		if (!SG_RuneGuardConditionRefValid(&guard->condition) ||
		    guard->truth < SG_FIELD_GUARD_FALSE ||
		    guard->truth >= SG_FIELD_GUARD_TRUTH_COUNT ||
		    guard->reserved != 0U ||
		    (index != 0U && StableIdOrder(
			&environment->guards[index - 1U].condition.value,
			&guard->condition.value) >= 0))
			return 0;
	}
	for (index = 0U; index < environment->event_slab_count; index++)
	{
		const sg_field_event_slab_t *slab = &environment->event_slabs[index];
		size_t guard;
		if (slab->valid_from_ms >= slab->valid_until_ms ||
		    (slab->exogenous_guard_count != 0U && !slab->exogenous_guards) ||
		    !SG_RuneDynamicsProofRefValid(&slab->schedule_proof) ||
		    (index != 0U && environment->event_slabs[index - 1U].valid_until_ms !=
			slab->valid_from_ms))
			return 0;
		for (guard = 0U; guard < slab->exogenous_guard_count; guard++)
			if (!SG_RuneGuardConditionRefValid(
				&slab->exogenous_guards[guard].condition) ||
			    slab->exogenous_guards[guard].truth < SG_FIELD_GUARD_FALSE ||
			    slab->exogenous_guards[guard].truth >= SG_FIELD_GUARD_UNKNOWN ||
			    slab->exogenous_guards[guard].reserved != 0U ||
			    (guard != 0U && StableIdOrder(
				&slab->exogenous_guards[guard - 1U].condition.value,
				&slab->exogenous_guards[guard].condition.value) >= 0))
				return 0;
	}
	return environment->event_slab_count == 0U ||
		(environment->event_slabs[0].valid_from_ms <=
			environment->sampled_at_ms &&
		 environment->sampled_at_ms <
			environment->event_slabs[0].valid_until_ms);
}

int SG_FieldHandleValid(const sg_field_handle_t *handle)
{
	return handle && handle->service_identity != 0U &&
		handle->service_generation != 0U &&
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
	    !ErrorInterval3Valid(&guidance->value.descent.position_error) ||
	    !ErrorInterval3Valid(&guidance->value.descent.velocity_error) ||
	    !ErrorIntervalValid(&guidance->value.descent.time_error) ||
	    !guidance->value.descent.options ||
	    guidance->value.descent.option_count == 0U ||
	    guidance->value.descent.required_option_capacity <
		guidance->value.descent.option_count)
		return 0;
	for (index = 0U; index < guidance->value.descent.option_count; index++)
	{
		const sg_field_option_t *option =
			&guidance->value.descent.options[index];
		const sg_rune_cost_bounds_t *endpoint;
		uint64_t minimum_descent;
		if (option->kind == SG_FIELD_OPTION_CONTROL)
		{
			if (!SG_RuneControlFiberIdValid(&option->value.control.control))
				return 0;
			endpoint = &option->value.control.endpoint_cost;
			minimum_descent = option->value.control.minimum_descent_us;
		}
		else if (option->kind == SG_FIELD_OPTION_TRANSFER)
		{
			if (!SG_RuneBoundaryTransferIdValid(
				&option->value.transfer.transfer))
				return 0;
			endpoint = &option->value.transfer.endpoint_cost;
			minimum_descent = option->value.transfer.minimum_descent_us;
		}
		else
			return 0;
		if (minimum_descent == 0U ||
		    !CostBoundsValid(endpoint) || endpoint->upper_us >
			guidance->value.descent.arrival_cost.lower_us ||
		    minimum_descent >
			guidance->value.descent.arrival_cost.lower_us -
				endpoint->upper_us)
			return 0;
	}
	return 1;
}
