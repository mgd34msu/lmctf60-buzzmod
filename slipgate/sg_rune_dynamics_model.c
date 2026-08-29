#include "sg_rune_dynamics_model.h"

#include <math.h>

static int StableIdDomainValid(const sg_rune_stable_id_t *id, uint32_t domain)
{
	return SG_RuneModelStableIdValid(id) &&
		(uint32_t)(id->high >> 32) == domain;
}

static int StableIdSame(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return SG_RuneModelStableIdEqual(left, right);
}

#define DEFINE_DOMAIN_VALIDATOR(function, type, domain) \
int function(const type *value) \
{ \
	return value && StableIdDomainValid(&value->value, domain); \
}

DEFINE_DOMAIN_VALIDATOR(SG_RuneDynamicsModelIdValid,
	sg_rune_dynamics_model_id_t, SG_RUNE_ORDER_DYNAMICS_MODEL)
DEFINE_DOMAIN_VALIDATOR(SG_RuneStateVertexIdValid,
	sg_rune_state_vertex_id_t, SG_RUNE_ORDER_STATE_VERTEX)
DEFINE_DOMAIN_VALIDATOR(SG_RuneStateChartIdValid,
	sg_rune_state_chart_id_t, SG_RUNE_ORDER_STATE_CHART)
DEFINE_DOMAIN_VALIDATOR(SG_RuneStateSimplexIdValid,
	sg_rune_state_simplex_id_t, SG_RUNE_ORDER_STATE_SIMPLEX)
DEFINE_DOMAIN_VALIDATOR(SG_RuneStateDomainIdValid,
	sg_rune_state_domain_id_t, SG_RUNE_ORDER_STATE_DOMAIN)
DEFINE_DOMAIN_VALIDATOR(SG_RuneControlFiberIdValid,
	sg_rune_control_fiber_id_t, SG_RUNE_ORDER_CONTROL_FIBER)
DEFINE_DOMAIN_VALIDATOR(SG_RuneResponsePatchIdValid,
	sg_rune_response_patch_id_t, SG_RUNE_ORDER_RESPONSE_PATCH)
DEFINE_DOMAIN_VALIDATOR(SG_RuneBoundaryTransferIdValid,
	sg_rune_boundary_transfer_id_t, SG_RUNE_ORDER_BOUNDARY_TRANSFER)
DEFINE_DOMAIN_VALIDATOR(SG_RuneControlDomainRefValid,
	sg_rune_control_domain_ref_t, SG_RUNE_ORDER_CONTROL_DOMAIN)
DEFINE_DOMAIN_VALIDATOR(SG_RuneGuardConditionRefValid,
	sg_rune_guard_condition_ref_t, SG_RUNE_ORDER_GUARD_CONDITION)
DEFINE_DOMAIN_VALIDATOR(SG_RuneDynamicsProofRefValid,
	sg_rune_dynamics_proof_ref_t, SG_RUNE_ORDER_DYNAMICS_PROOF)
DEFINE_DOMAIN_VALIDATOR(SG_RuneFieldRegionIdValid,
	sg_rune_field_region_id_t, SG_RUNE_ORDER_FIELD_REGION)
DEFINE_DOMAIN_VALIDATOR(SG_RuneFieldHierarchyIdValid,
	sg_rune_field_hierarchy_id_t, SG_RUNE_ORDER_FIELD_HIERARCHY)
DEFINE_DOMAIN_VALIDATOR(SG_RuneFieldErrorContractIdValid,
	sg_rune_field_error_contract_id_t,
	SG_RUNE_ORDER_FIELD_ERROR_CONTRACT)

#undef DEFINE_DOMAIN_VALIDATOR

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

static int WaterModeValid(const sg_rune_water_mode_t *water)
{
	sg_rune_contents_mask_t required;
	sg_rune_contents_mask_t allowed;

	if (!water)
		return 0;
	switch (water->medium)
	{
	case SG_RUNE_MEDIUM_WATER:
		required = SG_RUNE_CONTENTS_WATER;
		break;
	case SG_RUNE_MEDIUM_LAVA:
		required = SG_RUNE_CONTENTS_LAVA;
		break;
	case SG_RUNE_MEDIUM_SLIME:
		required = SG_RUNE_CONTENTS_SLIME;
		break;
	case SG_RUNE_MEDIUM_DRY:
	case SG_RUNE_MEDIUM_COUNT:
	default:
		return 0;
	}
	allowed = required | SG_RUNE_CONTENTS_CURRENT_MASK;
	return (water->contents & SG_RUNE_CONTENTS_WATER_MASK) == required &&
		(water->contents & ~allowed) == 0U;
}

int SG_RuneStateModeValid(const sg_rune_state_mode_t *mode)
{
	if (!mode || mode->kind < SG_RUNE_STATE_MODE_SUPPORTED ||
	    mode->kind >= SG_RUNE_STATE_MODE_KIND_COUNT)
		return 0;
	switch (mode->kind)
	{
	case SG_RUNE_STATE_MODE_SUPPORTED:
		return StableIdDomainValid(
			&mode->value.supported.support_surface.value,
			SG_RUNE_ORDER_SURFACE);
	case SG_RUNE_STATE_MODE_WATER:
		return WaterModeValid(&mode->value.water);
	case SG_RUNE_STATE_MODE_AIRBORNE:
		return mode->value.airborne.void_relation >= SG_RUNE_VOID_CLEAR &&
			mode->value.airborne.void_relation <
				SG_RUNE_VOID_RELATION_COUNT;
	case SG_RUNE_STATE_MODE_HOOK_BOLT:
		return StableIdDomainValid(
			&mode->value.hook_bolt.visibility_relation.value,
			SG_RUNE_ORDER_AFFORDANCE);
	case SG_RUNE_STATE_MODE_HOOK_PULL:
		return StableIdDomainValid(
			&mode->value.hook_pull.anchor_surface.value,
			SG_RUNE_ORDER_SURFACE);
	case SG_RUNE_STATE_MODE_HOOK_COAST:
		return mode->value.hook_coast.void_relation >= SG_RUNE_VOID_CLEAR &&
			mode->value.hook_coast.void_relation <
				SG_RUNE_VOID_RELATION_COUNT;
	case SG_RUNE_STATE_MODE_MOVER_RELATIVE:
		return StableIdDomainValid(
			&mode->value.mover_relative.mover.value,
			SG_RUNE_ORDER_MECHANISM);
	case SG_RUNE_STATE_MODE_KIND_COUNT:
	default:
		return 0;
	}
}

int SG_RuneStateVertexShapeValid(const sg_rune_state_vertex_t *vertex)
{
	return vertex && SG_RuneStateVertexIdValid(&vertex->id) &&
		SG_RuneStateChartIdValid(&vertex->chart) &&
		VectorValid(&vertex->position) && VectorValid(&vertex->velocity) &&
		isfinite(vertex->elapsed_ms) && vertex->elapsed_ms >= 0.0f;
}

int SG_RuneStateSimplexShapeValid(const sg_rune_state_simplex_t *simplex)
{
	return simplex && SG_RuneStateSimplexIdValid(&simplex->id) &&
		SG_RuneStateChartIdValid(&simplex->chart) &&
		simplex->vertices.count ==
			(uint32_t)SG_RUNE_STATE_DIMENSION_COUNT + 1U;
}

int SG_RuneStateDomainShapeValid(const sg_rune_state_domain_t *domain)
{
	return domain && SG_RuneStateDomainIdValid(&domain->id) &&
		SG_RuneStateChartIdValid(&domain->chart) &&
		domain->simplices.count != 0U;
}

int SG_RuneStateChartShapeValid(const sg_rune_state_chart_t *chart)
{
	return chart && SG_RuneStateChartIdValid(&chart->id) &&
		StableIdDomainValid(&chart->configuration_cell.value,
			SG_RUNE_ORDER_CELL) &&
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
		chart->state_domains.count != 0U &&
		chart->control_fibers.count != 0U &&
		chart->response_patches.count != 0U &&
		SG_RuneDynamicsProofRefValid(&chart->coverage_proof);
}

int SG_RuneControlFiberShapeValid(const sg_rune_control_fiber_t *fiber)
{
	return fiber && SG_RuneControlFiberIdValid(&fiber->id) &&
		SG_RuneStateChartIdValid(&fiber->source_chart) &&
		SG_RuneControlDomainRefValid(&fiber->domain) &&
		SG_RuneGuardConditionRefValid(&fiber->condition) &&
		SG_RuneDynamicsProofRefValid(&fiber->coverage_proof);
}

int SG_RuneResponsePatchShapeValid(const sg_rune_response_patch_t *patch)
{
	return patch && SG_RuneResponsePatchIdValid(&patch->id) &&
		SG_RuneStateChartIdValid(&patch->source_chart) &&
		SG_RuneStateSimplexIdValid(&patch->source_simplex) &&
		patch->controls.count != 0U && FlowEnclosureValid(&patch->flow) &&
		CostBoundsValid(&patch->running_cost) &&
		patch->running_cost.lower_us != 0U &&
		patch->destination_domains.count != 0U &&
		SG_RuneDynamicsProofRefValid(&patch->flow_proof);
}

int SG_RuneBoundaryTransferShapeValid(
	const sg_rune_boundary_transfer_t *transfer)
{
	return transfer && SG_RuneBoundaryTransferIdValid(&transfer->id) &&
		SG_RuneStateChartIdValid(&transfer->source_chart) &&
		SG_RuneStateDomainIdValid(&transfer->source_domain) &&
		SG_RuneGuardConditionRefValid(&transfer->condition) &&
		SG_RuneStateChartIdValid(&transfer->destination_chart) &&
		SG_RuneStateDomainIdValid(&transfer->destination_domain) &&
		FlowEnclosureValid(&transfer->reset_enclosure) &&
		SG_RuneDynamicsProofRefValid(&transfer->transfer_proof);
}

int SG_RuneFieldRegionShapeValid(const sg_rune_field_region_t *region)
{
	return region && SG_RuneFieldRegionIdValid(&region->id) &&
		SG_RuneDynamicsProofRefValid(&region->coverage_proof) &&
		region->charts.count != 0U && region->state_domains.count != 0U &&
		region->response_patches.count != 0U;
}

typedef enum field_region_owned_kind_e
{
	FIELD_REGION_OWNS_CHART = 0,
	FIELD_REGION_OWNS_STATE_DOMAIN,
	FIELD_REGION_OWNS_RESPONSE_PATCH
} field_region_owned_kind_t;

static sg_rune_field_region_span_t RegionOwnedSpan(
	const sg_rune_field_region_t *region, field_region_owned_kind_t kind)
{
	sg_rune_field_region_span_t span;

	if (kind == FIELD_REGION_OWNS_CHART)
	{
		span.first = region->charts.first;
		span.count = region->charts.count;
	}
	else if (kind == FIELD_REGION_OWNS_STATE_DOMAIN)
	{
		span.first = region->state_domains.first;
		span.count = region->state_domains.count;
	}
	else
	{
		span.first = region->response_patches.first;
		span.count = region->response_patches.count;
	}
	return span;
}

static int LeafOwnershipValid(const sg_rune_field_region_hierarchy_t *hierarchy,
	const uint32_t *owners, size_t item_count,
	field_region_owned_kind_t kind)
{
	size_t region_index;
	size_t item_index;
	size_t next_item = 0U;

	for (region_index = 0U; region_index < hierarchy->region_count;
	     region_index++)
	{
		const sg_rune_field_region_t *region =
			&hierarchy->regions[region_index];
		sg_rune_field_region_span_t span;

		if (region->children.count != 0U)
			continue;
		span = RegionOwnedSpan(region, kind);
		if (span.first != next_item ||
		    !SpanWithin(span.first, span.count, item_count))
			return 0;
		for (item_index = span.first;
		     item_index < (size_t)span.first + span.count; item_index++)
			if (owners[item_index] != region_index)
				return 0;
		next_item += span.count;
	}
	return next_item == item_count;
}

int SG_RuneFieldRegionHierarchyValid(
	const sg_rune_field_region_hierarchy_t *hierarchy)
{
	size_t region_index;
	size_t child_index;
	size_t next_child = 0U;
	uint64_t source_set_identity;

	if (!hierarchy || !SG_RuneFieldHierarchyIdValid(&hierarchy->id) ||
	    !hierarchy->regions || hierarchy->region_count == 0U ||
	    hierarchy->region_count > UINT32_MAX ||
	    hierarchy->child_count != hierarchy->region_count - 1U ||
	    !hierarchy->chart_leaf_regions || hierarchy->chart_count == 0U ||
	    hierarchy->chart_count > UINT32_MAX ||
	    !hierarchy->state_domain_leaf_regions ||
	    hierarchy->state_domain_count == 0U ||
	    hierarchy->state_domain_count > UINT32_MAX ||
	    !hierarchy->response_patch_leaf_regions ||
	    hierarchy->response_patch_count == 0U ||
	    hierarchy->response_patch_count > UINT32_MAX ||
	    (hierarchy->child_count != 0U && !hierarchy->children) ||
	    !SG_RuneDynamicsProofRefValid(&hierarchy->hierarchy_proof))
		return 0;
	source_set_identity = hierarchy->id.value.source_set_identity;
	if (hierarchy->hierarchy_proof.value.source_set_identity !=
	    source_set_identity)
		return 0;
	for (region_index = 0U; region_index < hierarchy->region_count;
	     region_index++)
	{
		const sg_rune_field_region_t *region =
			&hierarchy->regions[region_index];

		if (!SG_RuneFieldRegionShapeValid(region) ||
		    region->id.value.source_set_identity != source_set_identity ||
		    region->coverage_proof.value.source_set_identity !=
			source_set_identity ||
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
			size_t packed_index =
				(size_t)region->children.first + child_index;
			uint32_t child = hierarchy->children[packed_index];

			if (child != packed_index + 1U ||
			    (size_t)child >= hierarchy->region_count ||
			    hierarchy->regions[child].parent_region != region_index)
				return 0;
		}
	}
	if (next_child != hierarchy->child_count)
		return 0;
	return LeafOwnershipValid(hierarchy, hierarchy->chart_leaf_regions,
			hierarchy->chart_count, FIELD_REGION_OWNS_CHART) &&
		LeafOwnershipValid(hierarchy,
			hierarchy->state_domain_leaf_regions,
			hierarchy->state_domain_count,
			FIELD_REGION_OWNS_STATE_DOMAIN) &&
		LeafOwnershipValid(hierarchy,
			hierarchy->response_patch_leaf_regions,
			hierarchy->response_patch_count,
			FIELD_REGION_OWNS_RESPONSE_PATCH);
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

static int StableIdCompareValue(const sg_rune_stable_id_t *left,
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

static int SpanInside(uint32_t first, uint32_t count, uint32_t outer_first,
	uint32_t outer_count)
{
	return first >= outer_first &&
		(size_t)first + count <= (size_t)outer_first + outer_count;
}

static int ModeSourceMatches(const sg_rune_state_mode_t *mode,
	uint64_t source_set_identity)
{
	if (mode->kind == SG_RUNE_STATE_MODE_SUPPORTED)
		return mode->value.supported.support_surface.value.source_set_identity ==
			source_set_identity;
	if (mode->kind == SG_RUNE_STATE_MODE_HOOK_BOLT)
		return mode->value.hook_bolt.visibility_relation.value
			.source_set_identity == source_set_identity;
	if (mode->kind == SG_RUNE_STATE_MODE_HOOK_PULL)
		return mode->value.hook_pull.anchor_surface.value.source_set_identity ==
			source_set_identity;
	if (mode->kind == SG_RUNE_STATE_MODE_MOVER_RELATIVE)
		return mode->value.mover_relative.mover.value.source_set_identity ==
			source_set_identity;
	return 1;
}

#define DEFINE_RECORD_FIND(function, type, reference_type, records, count) \
static const type *function(const sg_rune_dynamics_model_t *model, \
	const reference_type *reference) \
{ \
	size_t low = 0U; \
	size_t high = model->count; \
	while (low < high) \
	{ \
		size_t middle = low + (high - low) / 2U; \
		int order = StableIdCompareValue( \
			&model->records[middle].id.value, &reference->value); \
		if (order < 0) \
			low = middle + 1U; \
		else if (order > 0) \
			high = middle; \
		else \
			return &model->records[middle]; \
	} \
	return NULL; \
}

DEFINE_RECORD_FIND(FindStateSimplex, sg_rune_state_simplex_t,
	sg_rune_state_simplex_ref_t, state_simplices, state_simplex_count)
DEFINE_RECORD_FIND(FindStateChart, sg_rune_state_chart_t,
	sg_rune_state_chart_ref_t, state_charts, state_chart_count)
DEFINE_RECORD_FIND(FindStateDomain, sg_rune_state_domain_t,
	sg_rune_state_domain_ref_t, state_domains, state_domain_count)

#undef DEFINE_RECORD_FIND

static int DynamicsModelArraysValid(const sg_rune_dynamics_model_t *model,
	uint64_t source_set_identity)
{
	size_t index;

#define VALIDATE_RECORD_SEQUENCE(records, count, validator) do { \
	for (index = 0U; index < (count); index++) \
		if (!(validator)(&(records)[index]) || \
		    (records)[index].id.value.source_set_identity != \
			source_set_identity || \
		    (index != 0U && StableIdCompareValue( \
			&(records)[index - 1U].id.value, \
			&(records)[index].id.value) >= 0)) \
			return 0; \
} while (0)
	VALIDATE_RECORD_SEQUENCE(model->state_vertices,
		model->state_vertex_count, SG_RuneStateVertexShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->state_charts,
		model->state_chart_count, SG_RuneStateChartShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->state_simplices,
		model->state_simplex_count, SG_RuneStateSimplexShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->state_domains,
		model->state_domain_count, SG_RuneStateDomainShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->control_fibers,
		model->control_fiber_count, SG_RuneControlFiberShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->response_patches,
		model->response_patch_count, SG_RuneResponsePatchShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->boundary_transfers,
		model->boundary_transfer_count, SG_RuneBoundaryTransferShapeValid);
#undef VALIDATE_RECORD_SEQUENCE
	for (index = 0U; index < model->state_chart_count; index++)
		if (model->state_charts[index].configuration_cell.value
			.source_set_identity != source_set_identity ||
		    model->state_charts[index].coverage_proof.value
			.source_set_identity != source_set_identity ||
		    !ModeSourceMatches(&model->state_charts[index].mode,
			source_set_identity))
			return 0;
	for (index = 0U; index < model->control_fiber_count; index++)
		if (model->control_fibers[index].domain.value.source_set_identity !=
			source_set_identity ||
		    model->control_fibers[index].condition.value.source_set_identity !=
			source_set_identity ||
		    model->control_fibers[index].coverage_proof.value
			.source_set_identity != source_set_identity)
			return 0;
	for (index = 0U; index < model->response_patch_count; index++)
		if (model->response_patches[index].flow_proof.value
			.source_set_identity != source_set_identity)
			return 0;
	for (index = 0U; index < model->boundary_transfer_count; index++)
		if (model->boundary_transfers[index].condition.value
			.source_set_identity != source_set_identity ||
		    model->boundary_transfers[index].transfer_proof.value
			.source_set_identity != source_set_identity)
			return 0;
	return 1;
}

static int DynamicsModelOwnershipValid(const sg_rune_dynamics_model_t *model)
{
	size_t chart_index;
	size_t record_index;
	size_t next_vertex = 0U;
	size_t next_simplex = 0U;
	size_t next_domain = 0U;
	size_t next_fiber = 0U;
	size_t next_patch = 0U;
	size_t next_transfer = 0U;

	for (chart_index = 0U; chart_index < model->state_chart_count;
	     chart_index++)
	{
		const sg_rune_state_chart_t *chart =
			&model->state_charts[chart_index];

		if (chart->state_vertices.first != next_vertex ||
		    chart->simplices.first != next_simplex ||
		    chart->state_domains.first != next_domain ||
		    chart->control_fibers.first != next_fiber ||
		    chart->response_patches.first != next_patch ||
		    chart->boundary_transfers.first != next_transfer ||
		    !SpanWithin(chart->state_vertices.first,
			chart->state_vertices.count, model->state_vertex_count) ||
		    !SpanWithin(chart->simplices.first, chart->simplices.count,
			model->state_simplex_count) ||
		    !SpanWithin(chart->state_domains.first, chart->state_domains.count,
			model->state_domain_count) ||
		    !SpanWithin(chart->control_fibers.first,
			chart->control_fibers.count, model->control_fiber_count) ||
		    !SpanWithin(chart->response_patches.first,
			chart->response_patches.count, model->response_patch_count) ||
		    !SpanWithin(chart->boundary_transfers.first,
			chart->boundary_transfers.count,
			model->boundary_transfer_count))
			return 0;
		for (record_index = chart->state_vertices.first;
		     record_index < (size_t)chart->state_vertices.first +
			chart->state_vertices.count; record_index++)
			if (!StableIdSame(&model->state_vertices[record_index].chart.value,
				&chart->id.value))
				return 0;
		for (record_index = chart->simplices.first;
		     record_index < (size_t)chart->simplices.first +
			chart->simplices.count; record_index++)
		{
			const sg_rune_state_simplex_t *simplex =
				&model->state_simplices[record_index];
			if (!StableIdSame(&simplex->chart.value, &chart->id.value) ||
			    !SpanInside(simplex->vertices.first,
				simplex->vertices.count, chart->state_vertices.first,
				chart->state_vertices.count))
				return 0;
		}
		for (record_index = chart->state_domains.first;
		     record_index < (size_t)chart->state_domains.first +
			chart->state_domains.count; record_index++)
		{
			const sg_rune_state_domain_t *domain =
				&model->state_domains[record_index];
			if (!StableIdSame(&domain->chart.value, &chart->id.value) ||
			    !SpanInside(domain->simplices.first, domain->simplices.count,
				chart->simplices.first, chart->simplices.count))
				return 0;
		}
		for (record_index = chart->control_fibers.first;
		     record_index < (size_t)chart->control_fibers.first +
			chart->control_fibers.count; record_index++)
			if (!StableIdSame(
				&model->control_fibers[record_index].source_chart.value,
				&chart->id.value))
				return 0;
		for (record_index = chart->response_patches.first;
		     record_index < (size_t)chart->response_patches.first +
			chart->response_patches.count; record_index++)
		{
			const sg_rune_response_patch_t *patch =
				&model->response_patches[record_index];
			const sg_rune_state_simplex_t *source =
				FindStateSimplex(model, &patch->source_simplex);
			if (!StableIdSame(&patch->source_chart.value, &chart->id.value) ||
			    !source || !StableIdSame(&source->chart.value,
				&chart->id.value) ||
			    !SpanInside(patch->controls.first, patch->controls.count,
				chart->control_fibers.first,
				chart->control_fibers.count) ||
			    !SpanWithin(patch->destination_domains.first,
				patch->destination_domains.count,
				model->state_domain_count))
				return 0;
		}
		for (record_index = chart->boundary_transfers.first;
		     record_index < (size_t)chart->boundary_transfers.first +
			chart->boundary_transfers.count; record_index++)
		{
			const sg_rune_boundary_transfer_t *transfer =
				&model->boundary_transfers[record_index];
			const sg_rune_state_domain_t *source =
				FindStateDomain(model, &transfer->source_domain);
			const sg_rune_state_chart_t *destination_chart =
				FindStateChart(model, &transfer->destination_chart);
			const sg_rune_state_domain_t *destination_domain =
				FindStateDomain(model, &transfer->destination_domain);
			if (!StableIdSame(&transfer->source_chart.value,
				&chart->id.value) || !source ||
			    !StableIdSame(&source->chart.value, &chart->id.value) ||
			    !destination_chart || !destination_domain ||
			    !StableIdSame(&destination_domain->chart.value,
				&destination_chart->id.value))
				return 0;
		}
		next_vertex += chart->state_vertices.count;
		next_simplex += chart->simplices.count;
		next_domain += chart->state_domains.count;
		next_fiber += chart->control_fibers.count;
		next_patch += chart->response_patches.count;
		next_transfer += chart->boundary_transfers.count;
	}
	return next_vertex == model->state_vertex_count &&
		next_simplex == model->state_simplex_count &&
		next_domain == model->state_domain_count &&
		next_fiber == model->control_fiber_count &&
		next_patch == model->response_patch_count &&
		next_transfer == model->boundary_transfer_count;
}

int SG_RuneDynamicsModelValid(const sg_rune_dynamics_model_t *model,
	const sg_rune_runtime_snapshot_t *snapshot)
{
	uint64_t source_set_identity;

	if (!model || !SG_RuneRuntimeSnapshotValid(snapshot) ||
	    model->version != SG_RUNE_DYNAMICS_MODEL_VERSION ||
	    model->reserved != 0U || !SG_RuneDynamicsModelIdValid(&model->id) ||
	    model->rune_identity != snapshot->identity ||
	    model->topology_revision != snapshot->topology_revision ||
	    !model->state_vertices || model->state_vertex_count == 0U ||
	    model->state_vertex_count > UINT32_MAX || !model->state_charts ||
	    model->state_chart_count == 0U ||
	    model->state_chart_count > UINT32_MAX || !model->state_simplices ||
	    model->state_simplex_count == 0U ||
	    model->state_simplex_count > UINT32_MAX || !model->state_domains ||
	    model->state_domain_count == 0U ||
	    model->state_domain_count > UINT32_MAX || !model->control_fibers ||
	    model->control_fiber_count == 0U ||
	    model->control_fiber_count > UINT32_MAX || !model->response_patches ||
	    model->response_patch_count == 0U ||
	    model->response_patch_count > UINT32_MAX ||
	    !model->boundary_transfers || model->boundary_transfer_count == 0U ||
	    model->boundary_transfer_count > UINT32_MAX ||
	    !SG_RuneFieldRegionHierarchyValid(&model->hierarchy) ||
	    !SG_RuneFieldErrorContractValid(&model->error_contract) ||
	    model->hierarchy.chart_count != model->state_chart_count ||
	    model->hierarchy.state_domain_count != model->state_domain_count ||
	    model->hierarchy.response_patch_count != model->response_patch_count)
		return 0;
	source_set_identity = model->id.value.source_set_identity;
	if (source_set_identity != snapshot->model->identity.source_set_identity ||
	    model->hierarchy.id.value.source_set_identity != source_set_identity ||
	    model->error_contract.id.value.source_set_identity !=
		source_set_identity)
		return 0;
	return DynamicsModelArraysValid(model, source_set_identity) &&
		DynamicsModelOwnershipValid(model);
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
