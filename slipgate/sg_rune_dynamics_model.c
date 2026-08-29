#include "sg_rune_dynamics_model.h"

#include <math.h>

static int StableIdValid(const sg_rune_stable_id_t *id)
{
	return SG_RuneModelStableIdValid(id);
}

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

static int FlowEnclosureValid(const sg_rune_flow_enclosure_t *flow)
{
	return flow && Interval3Valid(&flow->position) &&
		Interval3Valid(&flow->velocity) && IntervalValid(&flow->elapsed_ms) &&
		flow->elapsed_ms.min_value >= 0.0f;
}

static int CostBoundsValid(const sg_rune_cost_bounds_t *cost)
{
	return cost && cost->lower_us <= cost->upper_us &&
		cost->upper_us < SG_RUNE_FIELD_COST_INFINITE;
}

static int SpanWithin(uint32_t first, uint32_t count, size_t capacity)
{
	return (size_t)first <= capacity && (size_t)count <= capacity - first;
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

int SG_RuneStateModeValid(const sg_rune_state_mode_t *mode)
{
	if (!mode || mode->kind < SG_RUNE_STATE_MODE_SUPPORTED ||
	    mode->kind >= SG_RUNE_STATE_MODE_KIND_COUNT)
		return 0;
	switch (mode->kind)
	{
	case SG_RUNE_STATE_MODE_SUPPORTED:
		return StableIdValid(&mode->value.supported.support_surface.value);
	case SG_RUNE_STATE_MODE_WATER:
		return (mode->value.water.medium == SG_RUNE_MEDIUM_WATER ||
			mode->value.water.medium == SG_RUNE_MEDIUM_LAVA ||
			mode->value.water.medium == SG_RUNE_MEDIUM_SLIME) &&
			(mode->value.water.contents & SG_RUNE_CONTENTS_WATER_MASK) != 0U &&
			(mode->value.water.contents &
				~(sg_rune_contents_mask_t)SG_RUNE_CONTENTS_KNOWN) == 0U;
	case SG_RUNE_STATE_MODE_AIRBORNE:
		return mode->value.airborne.void_relation >= SG_RUNE_VOID_CLEAR &&
			mode->value.airborne.void_relation <
				SG_RUNE_VOID_RELATION_COUNT;
	case SG_RUNE_STATE_MODE_HOOK_BOLT:
		return StableIdValid(
			&mode->value.hook_bolt.visibility_relation.value);
	case SG_RUNE_STATE_MODE_HOOK_PULL:
		return StableIdValid(&mode->value.hook_pull.anchor_surface.value);
	case SG_RUNE_STATE_MODE_HOOK_COAST:
		return mode->value.hook_coast.void_relation >= SG_RUNE_VOID_CLEAR &&
			mode->value.hook_coast.void_relation <
				SG_RUNE_VOID_RELATION_COUNT;
	case SG_RUNE_STATE_MODE_MOVER_RELATIVE:
		return StableIdValid(&mode->value.mover_relative.mover.value);
	case SG_RUNE_STATE_MODE_KIND_COUNT:
	default:
		return 0;
	}
}

int SG_RuneStateChartShapeValid(const sg_rune_state_chart_t *chart)
{
	return chart && StableIdValid(&chart->id.value) &&
		StableIdValid(&chart->configuration_cell.value) &&
		SG_RuneStateModeValid(&chart->mode) &&
		Interval3Valid(&chart->embedding.position) &&
		Interval3Valid(&chart->embedding.velocity) &&
		IntervalValid(&chart->embedding.elapsed_ms) &&
		chart->embedding.elapsed_ms.min_value >= 0.0f &&
		chart->embedding.dimension_count == SG_RUNE_STATE_DIMENSION_COUNT &&
		chart->embedding.reserved[0] == 0U &&
		chart->embedding.reserved[1] == 0U &&
		chart->embedding.reserved[2] == 0U &&
		chart->state_vertices.count != 0U && chart->simplices.count != 0U &&
		chart->response_patches.count != 0U &&
		StableIdValid(&chart->coverage_proof.value);
}

int SG_RuneControlFiberShapeValid(const sg_rune_control_fiber_t *fiber)
{
	return fiber && StableIdValid(&fiber->id.value) &&
		StableIdValid(&fiber->source_chart.value) &&
		StableIdValid(&fiber->domain.value) &&
		StableIdValid(&fiber->condition.value) &&
		StableIdValid(&fiber->coverage_proof.value);
}

int SG_RuneResponsePatchShapeValid(const sg_rune_response_patch_t *patch)
{
	return patch && StableIdValid(&patch->id.value) &&
		StableIdValid(&patch->source_chart.value) &&
		StableIdValid(&patch->source_simplex.value) &&
		patch->controls.count != 0U && FlowEnclosureValid(&patch->flow) &&
		CostBoundsValid(&patch->running_cost) &&
		patch->running_cost.lower_us != 0U &&
		patch->destination_domains.count != 0U &&
		StableIdValid(&patch->flow_proof.value);
}

int SG_RuneBoundaryTransferShapeValid(
	const sg_rune_boundary_transfer_t *transfer)
{
	return transfer && StableIdValid(&transfer->id.value) &&
		StableIdValid(&transfer->source_chart.value) &&
		StableIdValid(&transfer->source_domain.value) &&
		StableIdValid(&transfer->condition.value) &&
		StableIdValid(&transfer->destination_chart.value) &&
		StableIdValid(&transfer->destination_domain.value) &&
		FlowEnclosureValid(&transfer->reset_enclosure) &&
		StableIdValid(&transfer->transfer_proof.value);
}

int SG_RuneFieldRegionShapeValid(const sg_rune_field_region_t *region)
{
	return region && StableIdValid(&region->id.value) &&
		StableIdValid(&region->coverage_proof.value) &&
		region->charts.count != 0U && region->state_domains.count != 0U &&
		region->response_patches.count != 0U;
}

int SG_RuneFieldRegionHierarchyValid(
	const sg_rune_field_region_hierarchy_t *hierarchy)
{
	size_t region_index;
	size_t child_index;
	size_t chart_index;
	size_t next_child = 0U;

	if (!hierarchy || !hierarchy->regions || hierarchy->region_count == 0U ||
	    hierarchy->region_count > UINT32_MAX ||
	    hierarchy->child_count > UINT32_MAX ||
	    !hierarchy->chart_leaf_regions || hierarchy->chart_count == 0U ||
	    hierarchy->chart_count > UINT32_MAX ||
	    hierarchy->state_domain_count == 0U ||
	    hierarchy->state_domain_count > UINT32_MAX ||
	    hierarchy->response_patch_count == 0U ||
	    hierarchy->response_patch_count > UINT32_MAX ||
	    (hierarchy->child_count != 0U && !hierarchy->children) ||
	    !StableIdValid(&hierarchy->hierarchy_proof.value))
		return 0;
	for (region_index = 0U; region_index < hierarchy->region_count;
	     region_index++)
	{
		const sg_rune_field_region_t *region =
			&hierarchy->regions[region_index];

		if (!SG_RuneFieldRegionShapeValid(region) ||
		    region->children.first != next_child ||
		    !SpanWithin(region->children.first, region->children.count,
			hierarchy->child_count) ||
		    !SpanWithin(region->charts.first, region->charts.count,
			hierarchy->chart_count) ||
		    !SpanWithin(region->state_domains.first,
			region->state_domains.count, hierarchy->state_domain_count) ||
		    !SpanWithin(region->response_patches.first,
			region->response_patches.count,
			hierarchy->response_patch_count))
			return 0;
		if (region_index == 0U)
		{
			if (region->parent_region != SG_RUNE_FIELD_NO_REGION ||
			    region->level != 0U)
				return 0;
		}
		else if ((size_t)region->parent_region >= region_index ||
			 region->level !=
				hierarchy->regions[region->parent_region].level + 1U)
			return 0;
		next_child += region->children.count;
		for (child_index = 0U; child_index < region->children.count;
		     child_index++)
		{
			uint32_t child = hierarchy->children[
				region->children.first + child_index];

			if ((size_t)child >= hierarchy->region_count ||
			    child <= region_index ||
			    hierarchy->regions[child].parent_region != region_index)
				return 0;
		}
	}
	if (next_child != hierarchy->child_count)
		return 0;
	for (region_index = 1U; region_index < hierarchy->region_count;
	     region_index++)
	{
		size_t occurrences = 0U;

		for (child_index = 0U; child_index < hierarchy->child_count;
		     child_index++)
			if (hierarchy->children[child_index] == region_index)
				occurrences++;
		if (occurrences != 1U)
			return 0;
	}
	for (chart_index = 0U; chart_index < hierarchy->chart_count; chart_index++)
	{
		uint32_t leaf = hierarchy->chart_leaf_regions[chart_index];

		if ((size_t)leaf >= hierarchy->region_count ||
		    hierarchy->regions[leaf].children.count != 0U ||
		    chart_index < hierarchy->regions[leaf].charts.first ||
		    chart_index >= (size_t)hierarchy->regions[leaf].charts.first +
			hierarchy->regions[leaf].charts.count)
			return 0;
	}
	return 1;
}

int SG_RuneFieldErrorContractValid(
	const sg_rune_field_error_contract_t *contract)
{
	return contract && contract->cost_quantum_us != 0U &&
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
		state->sampled_at_ms != 0U && StableIdValid(&state->chart.value) &&
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
		if (!StableIdValid(&descent->control.value) ||
		    descent->minimum_descent_us == 0U ||
		    !CostBoundsValid(&descent->endpoint_cost) ||
		    descent->endpoint_cost.upper_us >=
			guidance->value.descent.arrival_cost.upper_us)
			return 0;
	}
	return 1;
}
