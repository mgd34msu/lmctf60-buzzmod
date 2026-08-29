#include "sg_rune_dynamics_model_internal.h"

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
DEFINE_DOMAIN_VALIDATOR(SG_RuneControlDomainIdValid,
	sg_rune_control_domain_id_t, SG_RUNE_ORDER_CONTROL_DOMAIN)
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
DEFINE_DOMAIN_VALIDATOR(SG_FieldChoiceIdValid,
	sg_field_choice_id_t, SG_RUNE_ORDER_FIELD_CHOICE)
DEFINE_DOMAIN_VALIDATOR(SG_FieldOutcomeIdValid,
	sg_field_outcome_id_t, SG_RUNE_ORDER_FIELD_OUTCOME)
DEFINE_DOMAIN_VALIDATOR(SG_FieldReachAtomIdValid,
	sg_field_reach_atom_id_t, SG_RUNE_ORDER_FIELD_REACH_ATOM)
DEFINE_DOMAIN_VALIDATOR(SG_FieldLocalProgressIdValid,
	sg_field_local_progress_id_t, SG_RUNE_ORDER_FIELD_LOCAL_PROGRESS)
DEFINE_DOMAIN_VALIDATOR(SG_FieldRefinementNodeIdValid,
	sg_field_refinement_node_id_t, SG_RUNE_ORDER_FIELD_REFINEMENT_NODE)
DEFINE_DOMAIN_VALIDATOR(SG_FieldRefinementTreeIdValid,
	sg_field_refinement_tree_id_t, SG_RUNE_ORDER_FIELD_REFINEMENT_TREE)

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

static int SpanWithin(uint32_t first, uint32_t count, size_t capacity);

static int AffineOperatorValid(const sg_rune_affine_state_operator_t *operator)
{
	uint32_t row;
	uint32_t column;

	if (!operator)
		return 0;
	for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		for (column = 0U; column <= SG_RUNE_STATE_DIMENSION_COUNT;
		     column++)
			if (!isfinite(operator->coefficient[row][column]))
				return 0;
	return operator->exact_rank <= SG_RUNE_STATE_DIMENSION_COUNT &&
		operator->exact_rank == SG_RuneAffineOperatorRankExact(operator) &&
		operator->reserved[0] == 0U && operator->reserved[1] == 0U &&
		operator->reserved[2] == 0U &&
		SG_RuneDynamicsProofRefValid(&operator->operator_proof) &&
		SG_RuneDynamicsProofRefValid(&operator->image_proof) &&
		SG_RuneDynamicsProofRefValid(&operator->cover_proof);
}

int SG_FieldReachAtomShapeValid(const sg_field_reach_atom_t *atom)
{
	return atom && SG_FieldReachAtomIdValid(&atom->id) &&
		SG_RuneStateDomainIdValid(&atom->domain) &&
		atom->simplices.count != 0U && FlowEnclosureValid(&atom->state_bounds) &&
		SG_RuneDynamicsProofRefValid(&atom->partition_proof);
}

int SG_FieldGuardEffectValid(const sg_field_guard_effect_t *effect)
{
	return effect && SG_RuneGuardConditionRefValid(&effect->condition) &&
		effect->required_before >= SG_FIELD_GUARD_FALSE &&
		effect->required_before < SG_FIELD_GUARD_UNKNOWN &&
		effect->resulting_after >= SG_FIELD_GUARD_FALSE &&
		effect->resulting_after < SG_FIELD_GUARD_UNKNOWN &&
		effect->controllable == 1U && effect->reserved[0] == 0U &&
		effect->reserved[1] == 0U && effect->reserved[2] == 0U;
}

static int GuardRequirementValid(const sg_field_guard_requirement_t *requirement)
{
	return requirement &&
		SG_RuneGuardConditionRefValid(&requirement->condition) &&
		requirement->required >= SG_FIELD_GUARD_FALSE &&
		requirement->required < SG_FIELD_GUARD_UNKNOWN &&
		requirement->reserved == 0U;
}

static int IntervalInside(const sg_rune_interval_t *inner,
	const sg_rune_interval_t *outer)
{
	return inner->min_value >= outer->min_value &&
		inner->max_value <= outer->max_value;
}

static int FlowInside(const sg_rune_flow_enclosure_t *inner,
	const sg_rune_flow_enclosure_t *outer)
{
	return IntervalInside(&inner->position.x, &outer->position.x) &&
		IntervalInside(&inner->position.y, &outer->position.y) &&
		IntervalInside(&inner->position.z, &outer->position.z) &&
		IntervalInside(&inner->velocity.x, &outer->velocity.x) &&
		IntervalInside(&inner->velocity.y, &outer->velocity.y) &&
		IntervalInside(&inner->velocity.z, &outer->velocity.z) &&
		IntervalInside(&inner->elapsed_ms, &outer->elapsed_ms);
}

static const sg_rune_interval_t *FlowInterval(
	const sg_rune_flow_enclosure_t *flow, uint32_t dimension)
{
	if (dimension < 3U)
		return dimension == 0U ? &flow->position.x :
			dimension == 1U ? &flow->position.y : &flow->position.z;
	if (dimension < 6U)
	{
		dimension -= 3U;
		return dimension == 0U ? &flow->velocity.x :
			dimension == 1U ? &flow->velocity.y : &flow->velocity.z;
	}
	return &flow->elapsed_ms;
}

static void StoreFlowInterval(sg_rune_flow_enclosure_t *flow,
	uint32_t dimension, float minimum, float maximum)
{
	sg_rune_interval_t *interval;
	if (dimension < 3U)
		interval = dimension == 0U ? &flow->position.x :
			dimension == 1U ? &flow->position.y : &flow->position.z;
	else if (dimension < 6U)
	{
		dimension -= 3U;
		interval = dimension == 0U ? &flow->velocity.x :
			dimension == 1U ? &flow->velocity.y : &flow->velocity.z;
	}
	else
		interval = &flow->elapsed_ms;
	interval->min_value = minimum;
	interval->max_value = maximum;
}

static void OperatorImage(const sg_field_reach_atom_t *source,
	const sg_field_outcome_t *outcome, sg_rune_flow_enclosure_t *image)
{
	uint32_t row;

	for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
	{
		double minimum = outcome->endpoint.coefficient[row]
			[SG_RUNE_STATE_DIMENSION_COUNT];
		double maximum = minimum;
		uint32_t column;
		for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT; column++)
		{
			double coefficient = outcome->endpoint.coefficient[row][column];
			const sg_rune_interval_t *input =
				FlowInterval(&source->state_bounds, column);
			double low;
			double high;
			if (coefficient == 0.0)
				continue;
			low = coefficient >= 0.0 ? input->min_value : input->max_value;
			high = coefficient >= 0.0 ? input->max_value : input->min_value;
			minimum = nextafter(minimum + coefficient * low, -INFINITY);
			maximum = nextafter(maximum + coefficient * high, INFINITY);
		}
		{
			const sg_rune_interval_t *remainder =
				FlowInterval(&outcome->remainder, row);
			double lower = minimum + remainder->min_value;
			double upper = maximum + remainder->max_value;
			float stored_minimum = (float)(remainder->min_value == 0.0f ?
				lower : nextafter(lower, -INFINITY));
			float stored_maximum = (float)(remainder->max_value == 0.0f ?
				upper : nextafter(upper, INFINITY));
			if ((double)stored_minimum > lower)
				stored_minimum = nextafterf(stored_minimum, -INFINITY);
			if ((double)stored_maximum < upper)
				stored_maximum = nextafterf(stored_maximum, INFINITY);
			StoreFlowInterval(image, row, stored_minimum, stored_maximum);
		}
	}
}

static int OutcomeCovered(const sg_rune_dynamics_model_t *model,
	const sg_field_reach_atom_t *source, const sg_field_outcome_t *outcome)
{
	sg_rune_flow_enclosure_t image;
	size_t item;

	OperatorImage(source, outcome, &image);
	for (item = outcome->destination_cover.first;
	     item < (size_t)outcome->destination_cover.first +
		outcome->destination_cover.count; item++)
	{
		size_t atom;
		for (atom = 0U; atom < model->reach_atom_count; atom++)
			if (model->reach_atoms[atom].id.value.source_set_identity ==
				model->outcome_destination_atoms[item].value.source_set_identity &&
			    model->reach_atoms[atom].id.value.high ==
				model->outcome_destination_atoms[item].value.high &&
			    model->reach_atoms[atom].id.value.low ==
				model->outcome_destination_atoms[item].value.low &&
			    FlowInside(&image, &model->reach_atoms[atom].state_bounds))
				return 1;
	}
	return 0;
}

int SG_FieldOutcomeShapeValid(const sg_field_outcome_t *outcome)
{
	return outcome && SG_FieldOutcomeIdValid(&outcome->id) &&
		AffineOperatorValid(&outcome->endpoint) &&
		FlowEnclosureValid(&outcome->remainder) &&
		outcome->destination_cover.count != 0U &&
		outcome->absolute_time_advance.minimum_ms <=
			outcome->absolute_time_advance.maximum_ms &&
		SG_RuneDynamicsProofRefValid(&outcome->proof);
}

int SG_FieldChoiceShapeValid(const sg_field_choice_t *choice)
{
	if (!choice || !SG_FieldChoiceIdValid(&choice->id) ||
	    choice->kind < SG_FIELD_CHOICE_CONTROL ||
	    choice->kind >= SG_FIELD_CHOICE_KIND_COUNT ||
	    !SG_FieldReachAtomIdValid(&choice->source_atom) ||
	    choice->outcomes.count == 0U || !CostBoundsValid(&choice->cost) ||
	    !SG_RuneDynamicsProofRefValid(&choice->proof))
		return 0;
	return choice->kind == SG_FIELD_CHOICE_CONTROL ?
		SG_RuneControlFiberIdValid(&choice->authority.control) :
		SG_RuneBoundaryTransferIdValid(&choice->authority.transfer);
}

int SG_FieldLocalProgressKernelShapeValid(
	const sg_field_local_progress_kernel_t *progress)
{
	uint32_t coefficient;
	if (!progress || !SG_FieldLocalProgressIdValid(&progress->id) ||
		!SG_FieldReachAtomIdValid(&progress->source_atom) ||
		progress->covered_sources.count == 0U ||
		!SG_FieldReachAtomIdValid(&progress->target_atom) ||
		!FlowEnclosureValid(&progress->terminal_parameters.anchor_bounds) ||
		!Interval3Valid(&progress->terminal_parameters.position_offset_bounds) ||
		!Interval3Valid(&progress->terminal_parameters.velocity_bounds) ||
		!IntervalValid(&progress->terminal_parameters.local_elapsed_bounds) ||
		progress->terminal_parameters.local_elapsed_bounds.min_value < 0.0f ||
		!SG_RuneDynamicsProofRefValid(&progress->terminal_parameters.proof) ||
		progress->admissible_choices.count == 0U ||
		progress->whole_outcome_targets.count == 0U ||
		progress->finite_rank == 0U || progress->reserved != 0U ||
		!isfinite(progress->minimum_lyapunov_decrease) ||
		progress->minimum_lyapunov_decrease <= 0.0f ||
		!SG_RuneDynamicsProofRefValid(&progress->proof))
		return 0;
	for (coefficient = 0U; coefficient < SG_RUNE_STATE_DIMENSION_COUNT;
	     coefficient++)
		if (!isfinite(progress->state_lyapunov[coefficient]) ||
		    !isfinite(progress->anchor_lyapunov[coefficient]))
			return 0;
	return isfinite(progress->lyapunov_constant);
}

int SG_FieldLocalProgressKernelAcceptsCapture(
	const sg_field_local_progress_kernel_t *kernel,
	const sg_destination_terminal_capture_t *capture)
{
	uint32_t dimension;

	if (!SG_FieldLocalProgressKernelShapeValid(kernel) || !capture ||
	    !SG_DestinationTerminalCaptureValidFor(capture,
		&capture->anchor.destination,
		capture->anchor.destination_generation))
		return 0;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		const sg_rune_interval_t *allowed =
			FlowInterval(&kernel->terminal_parameters.anchor_bounds, dimension);
		float coordinate = dimension < 3U ?
			capture->anchor.position[dimension] : dimension < 6U ?
			capture->anchor.velocity[dimension - 3U] :
			capture->anchor.local_elapsed_ms;
		if (!isfinite(coordinate) || coordinate < allowed->min_value ||
		    coordinate > allowed->max_value)
			return 0;
	}
	return capture->position_offset.x.min_value >=
			kernel->terminal_parameters.position_offset_bounds.x.min_value &&
		capture->position_offset.x.max_value <=
			kernel->terminal_parameters.position_offset_bounds.x.max_value &&
		capture->position_offset.y.min_value >=
			kernel->terminal_parameters.position_offset_bounds.y.min_value &&
		capture->position_offset.y.max_value <=
			kernel->terminal_parameters.position_offset_bounds.y.max_value &&
		capture->position_offset.z.min_value >=
			kernel->terminal_parameters.position_offset_bounds.z.min_value &&
		capture->position_offset.z.max_value <=
			kernel->terminal_parameters.position_offset_bounds.z.max_value &&
		capture->velocity.x.min_value >=
			kernel->terminal_parameters.velocity_bounds.x.min_value &&
		capture->velocity.x.max_value <=
			kernel->terminal_parameters.velocity_bounds.x.max_value &&
		capture->velocity.y.min_value >=
			kernel->terminal_parameters.velocity_bounds.y.min_value &&
		capture->velocity.y.max_value <=
			kernel->terminal_parameters.velocity_bounds.y.max_value &&
		capture->velocity.z.min_value >=
			kernel->terminal_parameters.velocity_bounds.z.min_value &&
		capture->velocity.z.max_value <=
			kernel->terminal_parameters.velocity_bounds.z.max_value &&
		capture->local_elapsed_ms.min_value >=
			kernel->terminal_parameters.local_elapsed_bounds.min_value &&
		capture->local_elapsed_ms.max_value <=
			kernel->terminal_parameters.local_elapsed_bounds.max_value;
}

static int SameStableId(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return left->source_set_identity == right->source_set_identity &&
		left->high == right->high && left->low == right->low;
}

int SG_FieldRefinementTreeValid(const sg_field_refinement_tree_t *tree,
	const sg_field_reach_atom_t *atoms, size_t atom_count)
{
	size_t node;
	size_t packed = 0U;
	size_t atom;
	size_t root_cursor = 0U;
	uint64_t source_set_identity;

	if (!tree || !SG_FieldRefinementTreeIdValid(&tree->id) || !tree->nodes ||
	    tree->node_count == 0U || tree->node_count > UINT32_MAX ||
	    tree->child_count > UINT32_MAX ||
	    (tree->child_count != 0U && !tree->children) || !tree->atom_roots ||
	    !atoms || atom_count == 0U || tree->atom_count != atom_count ||
	    !SG_RuneDynamicsProofRefValid(&tree->proof))
		return 0;
	source_set_identity = tree->id.value.source_set_identity;
	if (tree->proof.value.source_set_identity != source_set_identity ||
	    tree->child_count != tree->node_count - atom_count)
		return 0;
	for (node = 0U; node < tree->node_count; node++)
	{
		const sg_field_refinement_node_t *record = &tree->nodes[node];
		size_t child;
		if (!SG_FieldRefinementNodeIdValid(&record->id) ||
		    !SG_FieldReachAtomIdValid(&record->atom) ||
		    !FlowEnclosureValid(&record->state_bounds) ||
		    !FlowEnclosureValid(&record->interpolation_error) ||
		    !SG_RuneDynamicsProofRefValid(&record->proof) ||
		    record->id.value.source_set_identity != source_set_identity ||
		    record->atom.value.source_set_identity != source_set_identity ||
		    record->proof.value.source_set_identity != source_set_identity ||
		    record->children.first != packed ||
		    !SpanWithin(record->children.first, record->children.count,
			tree->child_count) ||
		    (node == 0U ? record->parent != UINT32_MAX :
			(record->parent != UINT32_MAX && record->parent >= node)))
			return 0;
		if (record->parent == UINT32_MAX)
		{
			if (root_cursor >= atom_count || tree->atom_roots[root_cursor] != node)
				return 0;
			root_cursor++;
		}
		else if (!FlowInside(&record->state_bounds,
			&tree->nodes[record->parent].state_bounds) ||
			 !FlowInside(&record->interpolation_error,
				&tree->nodes[record->parent].interpolation_error))
			return 0;
		for (child = 0U; child < record->children.count; child++)
		{
			uint32_t child_node = tree->children[packed + child];
			if (child_node <= node || child_node >= tree->node_count ||
			    (packed + child != 0U &&
			     tree->children[packed + child - 1U] >= child_node) ||
			    tree->nodes[child_node].parent != node ||
			    !SameStableId(&tree->nodes[child_node].atom.value,
				&record->atom.value))
				return 0;
		}
		packed += record->children.count;
	}
	if (packed != tree->child_count || root_cursor != atom_count)
		return 0;
	for (atom = 0U; atom < atom_count; atom++)
	{
		uint32_t root = tree->atom_roots[atom];
		if (root >= tree->node_count || tree->nodes[root].parent != UINT32_MAX ||
		    !SameStableId(&tree->nodes[root].atom.value, &atoms[atom].id.value))
			return 0;
		if (!FlowInside(&tree->nodes[root].state_bounds,
			&atoms[atom].state_bounds) ||
		    !FlowInside(&atoms[atom].state_bounds,
			&tree->nodes[root].state_bounds))
			return 0;
	}
	return 1;
}

static int SpanWithin(uint32_t first, uint32_t count, size_t capacity)
{
	return (size_t)first <= capacity && (size_t)count <= capacity - first;
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

int SG_RuneControlDomainShapeValid(const sg_rune_control_domain_t *domain)
{
	return domain && SG_RuneControlDomainIdValid(&domain->id) &&
		SG_RuneStateChartIdValid(&domain->source_chart) &&
		IntervalValid(&domain->forward_move) &&
		IntervalValid(&domain->side_move) &&
		IntervalValid(&domain->up_move) &&
		(domain->required_buttons & ~domain->allowed_buttons) == 0U &&
		SG_RuneDynamicsProofRefValid(&domain->admissibility_proof);
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

static int InternalSummaryValid(
	const sg_rune_field_region_hierarchy_t *hierarchy,
	const sg_rune_field_region_t *region, field_region_owned_kind_t kind)
{
	sg_rune_field_region_span_t summary = RegionOwnedSpan(region, kind);
	size_t child_index;
	size_t next_item = summary.first;

	if (region->children.count == 0U)
		return 1;
	for (child_index = 0U; child_index < region->children.count;
	     child_index++)
	{
		uint32_t child = hierarchy->children[
			(size_t)region->children.first + child_index];
		sg_rune_field_region_span_t child_summary =
			RegionOwnedSpan(&hierarchy->regions[child], kind);

		if (child_summary.first != next_item)
			return 0;
		next_item += child_summary.count;
	}
	return next_item == (size_t)summary.first + summary.count;
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

			if ((size_t)child >= hierarchy->region_count ||
			    child <= region_index ||
			    (child_index != 0U && child <=
				hierarchy->children[packed_index - 1U]) ||
			    hierarchy->regions[child].parent_region != region_index)
				return 0;
		}
		if (!InternalSummaryValid(hierarchy, region,
			FIELD_REGION_OWNS_CHART) ||
		    !InternalSummaryValid(hierarchy, region,
			FIELD_REGION_OWNS_STATE_DOMAIN) ||
		    !InternalSummaryValid(hierarchy, region,
			FIELD_REGION_OWNS_RESPONSE_PATCH))
			return 0;
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

#define DEFINE_BASE_RECORD_CONTAINS(function, type, records, count, ref_type) \
static int function(const sg_rune_model_t *model, const ref_type *reference) \
{ \
	size_t low = 0U; \
	size_t high = model->count; \
	while (low < high) \
	{ \
		size_t middle = low + (high - low) / 2U; \
		const type *record = &model->records[middle]; \
		int order = StableIdCompareValue(&record->id.value, \
			&reference->value); \
		if (order < 0) \
			low = middle + 1U; \
		else if (order > 0) \
			high = middle; \
		else \
			return 1; \
	} \
	return 0; \
}

DEFINE_BASE_RECORD_CONTAINS(CellAccepted, sg_rune_cell_t, cells, cell_count,
	sg_rune_cell_ref_t)
DEFINE_BASE_RECORD_CONTAINS(SurfaceAccepted, sg_rune_surface_t, surfaces,
	surface_count, sg_rune_surface_ref_t)
DEFINE_BASE_RECORD_CONTAINS(AffordanceAccepted, sg_rune_affordance_t,
	affordances, affordance_count, sg_rune_affordance_ref_t)
DEFINE_BASE_RECORD_CONTAINS(MechanismAccepted, sg_rune_mechanism_t,
	mechanisms, mechanism_count, sg_rune_mechanism_ref_t)

#undef DEFINE_BASE_RECORD_CONTAINS

static int ModeReferencesAccepted(const sg_rune_state_mode_t *mode,
	const sg_rune_model_t *model)
{
	if (mode->kind == SG_RUNE_STATE_MODE_SUPPORTED)
		return SurfaceAccepted(model,
			&mode->value.supported.support_surface);
	if (mode->kind == SG_RUNE_STATE_MODE_HOOK_BOLT)
		return AffordanceAccepted(model,
			&mode->value.hook_bolt.visibility_relation);
	if (mode->kind == SG_RUNE_STATE_MODE_HOOK_PULL)
		return SurfaceAccepted(model, &mode->value.hook_pull.anchor_surface);
	if (mode->kind == SG_RUNE_STATE_MODE_MOVER_RELATIVE)
		return MechanismAccepted(model, &mode->value.mover_relative.mover);
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
DEFINE_RECORD_FIND(FindControlDomain, sg_rune_control_domain_t,
	sg_rune_control_domain_ref_t, control_domains, control_domain_count)
DEFINE_RECORD_FIND(FindControlFiber, sg_rune_control_fiber_t,
	sg_rune_control_fiber_ref_t, control_fibers, control_fiber_count)
DEFINE_RECORD_FIND(FindBoundaryTransfer, sg_rune_boundary_transfer_t,
	sg_rune_boundary_transfer_ref_t, boundary_transfers,
	boundary_transfer_count)
DEFINE_RECORD_FIND(FindReachAtom, sg_field_reach_atom_t,
	sg_field_reach_atom_ref_t, reach_atoms, reach_atom_count)
DEFINE_RECORD_FIND(FindChoice, sg_field_choice_t,
	sg_field_choice_ref_t, choices, choice_count)
DEFINE_RECORD_FIND(FindOutcome, sg_field_outcome_t,
	sg_field_outcome_ref_t, outcomes, outcome_count)

#undef DEFINE_RECORD_FIND

static int DynamicsModelArraysValid(const sg_rune_dynamics_model_t *model,
	const sg_rune_model_t *base_model, uint64_t source_set_identity)
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
	VALIDATE_RECORD_SEQUENCE(model->control_domains,
		model->control_domain_count, SG_RuneControlDomainShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->response_patches,
		model->response_patch_count, SG_RuneResponsePatchShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->boundary_transfers,
		model->boundary_transfer_count, SG_RuneBoundaryTransferShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->reach_atoms,
		model->reach_atom_count, SG_FieldReachAtomShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->outcomes,
		model->outcome_count, SG_FieldOutcomeShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->choices,
		model->choice_count, SG_FieldChoiceShapeValid);
	VALIDATE_RECORD_SEQUENCE(model->local_progress_kernels,
		model->local_progress_kernel_count,
		SG_FieldLocalProgressKernelShapeValid);
#undef VALIDATE_RECORD_SEQUENCE
	for (index = 0U; index < model->guard_effect_count; index++)
		if (!SG_FieldGuardEffectValid(&model->guard_effects[index]) ||
		    model->guard_effects[index].condition.value.source_set_identity !=
			source_set_identity)
			return 0;
	for (index = 0U; index < model->guard_requirement_count; index++)
		if (!GuardRequirementValid(&model->guard_requirements[index]) ||
		    model->guard_requirements[index].condition.value.source_set_identity !=
			source_set_identity)
			return 0;
	for (index = 0U; index < model->state_chart_count; index++)
		if (!CellAccepted(base_model,
			&model->state_charts[index].configuration_cell) ||
		    model->state_charts[index].coverage_proof.value
			.source_set_identity != source_set_identity ||
		    !ModeReferencesAccepted(&model->state_charts[index].mode,
			base_model))
			return 0;
	for (index = 0U; index < model->control_domain_count; index++)
		if (model->control_domains[index].source_chart.value
			.source_set_identity != source_set_identity ||
		    model->control_domains[index].admissibility_proof.value
			.source_set_identity != source_set_identity)
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
	for (index = 0U; index < model->reach_atom_count; index++)
		if (model->reach_atoms[index].partition_proof.value.source_set_identity !=
			source_set_identity)
			return 0;
	for (index = 0U; index < model->outcome_count; index++)
		if (model->outcomes[index].proof.value.source_set_identity !=
			source_set_identity ||
		    model->outcomes[index].endpoint.operator_proof.value
			.source_set_identity != source_set_identity ||
		    model->outcomes[index].endpoint.image_proof.value
			.source_set_identity != source_set_identity ||
		    model->outcomes[index].endpoint.cover_proof.value
			.source_set_identity != source_set_identity)
			return 0;
	for (index = 0U; index < model->choice_count; index++)
		if (model->choices[index].proof.value.source_set_identity !=
			source_set_identity)
			return 0;
	for (index = 0U; index < model->local_progress_kernel_count; index++)
		if (model->local_progress_kernels[index].proof.value
			.source_set_identity != source_set_identity ||
		    model->local_progress_kernels[index].terminal_parameters.proof.value
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

	for (record_index = 0U; record_index < model->control_domain_count;
	     record_index++)
		if (!FindStateChart(model,
			&model->control_domains[record_index].source_chart))
			return 0;

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
		{
			const sg_rune_control_fiber_t *fiber =
				&model->control_fibers[record_index];
			const sg_rune_control_domain_t *domain =
				FindControlDomain(model, &fiber->domain);

			if (!StableIdSame(&fiber->source_chart.value,
				&chart->id.value) || !domain ||
			    !StableIdSame(&domain->source_chart.value,
				&chart->id.value))
				return 0;
		}
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

static int FieldModelOwnershipValid(const sg_rune_dynamics_model_t *model)
{
	size_t index;

	for (index = 0U; index < model->reach_atom_count; index++)
	{
		const sg_field_reach_atom_t *atom = &model->reach_atoms[index];
		const sg_rune_state_domain_t *domain =
			FindStateDomain(model, &atom->domain);
		if (!domain || !SpanInside(atom->simplices.first,
			atom->simplices.count, domain->simplices.first,
			domain->simplices.count))
			return 0;
	}
	for (index = 0U; index < model->outcome_count; index++)
	{
		const sg_field_outcome_t *outcome = &model->outcomes[index];
		size_t destination;
		size_t effect;
		if (!SpanWithin(outcome->destination_cover.first,
			outcome->destination_cover.count,
			model->outcome_destination_atom_count) ||
		    !SpanWithin(outcome->guard_effects.first,
			outcome->guard_effects.count, model->guard_effect_count))
			return 0;
		for (destination = outcome->destination_cover.first;
		     destination < (size_t)outcome->destination_cover.first +
			outcome->destination_cover.count; destination++)
			if (!FindReachAtom(model,
				&model->outcome_destination_atoms[destination]) ||
			    (destination != outcome->destination_cover.first &&
			     StableIdCompareValue(
				&model->outcome_destination_atoms[destination - 1U].value,
				&model->outcome_destination_atoms[destination].value) >= 0))
				return 0;
		for (effect = outcome->guard_effects.first;
		     effect < (size_t)outcome->guard_effects.first +
			outcome->guard_effects.count; effect++)
			if (effect != outcome->guard_effects.first &&
			    StableIdCompareValue(
				&model->guard_effects[effect - 1U].condition.value,
				&model->guard_effects[effect].condition.value) >= 0)
				return 0;
	}
	for (index = 0U; index < model->choice_count; index++)
	{
		const sg_field_choice_t *choice = &model->choices[index];
		const sg_field_reach_atom_t *source_atom =
			FindReachAtom(model, &choice->source_atom);
		size_t requirement;
		size_t outcome;
		if (!source_atom ||
		    !SpanWithin(choice->outcomes.first, choice->outcomes.count,
			model->outcome_count) ||
		    !SpanWithin(choice->guard_requirements.first,
			choice->guard_requirements.count,
			model->guard_requirement_count) ||
		    (choice->kind == SG_FIELD_CHOICE_CONTROL &&
		     !FindControlFiber(model, &choice->authority.control)) ||
		    (choice->kind == SG_FIELD_CHOICE_TRANSFER &&
		     !FindBoundaryTransfer(model, &choice->authority.transfer)))
			return 0;
		for (requirement = choice->guard_requirements.first;
		     requirement < (size_t)choice->guard_requirements.first +
			choice->guard_requirements.count; requirement++)
			if (requirement != choice->guard_requirements.first &&
			    StableIdCompareValue(
				&model->guard_requirements[requirement - 1U].condition.value,
				&model->guard_requirements[requirement].condition.value) >= 0)
				return 0;
		for (outcome = choice->outcomes.first;
		     outcome < (size_t)choice->outcomes.first +
			choice->outcomes.count; outcome++)
			if (!OutcomeCovered(model, source_atom, &model->outcomes[outcome]))
				return 0;
	}
	for (index = 0U; index < model->local_progress_kernel_count; index++)
	{
		const sg_field_local_progress_kernel_t *progress =
			&model->local_progress_kernels[index];
		size_t item;
		size_t required_targets = 0U;
		if (!FindReachAtom(model, &progress->source_atom) ||
		    !FindReachAtom(model, &progress->target_atom) ||
		    !SpanWithin(progress->covered_sources.first,
			progress->covered_sources.count,
			model->local_progress_source_count) ||
		    !SpanWithin(progress->admissible_choices.first,
			progress->admissible_choices.count,
			model->local_progress_choice_count) ||
		    !SpanWithin(progress->whole_outcome_targets.first,
			progress->whole_outcome_targets.count,
			model->local_progress_target_count))
			return 0;
		for (item = progress->covered_sources.first;
		     item < (size_t)progress->covered_sources.first +
			progress->covered_sources.count; item++)
		{
			const sg_field_refinement_node_ref_t *reference =
				&model->local_progress_sources[item];
			size_t node;
			for (node = 0U; node < model->refinement_tree.node_count; node++)
				if (SameStableId(&model->refinement_tree.nodes[node].id.value,
					&reference->value))
					break;
			if (node == model->refinement_tree.node_count ||
			    !SameStableId(&model->refinement_tree.nodes[node].atom.value,
				&progress->source_atom.value) ||
			    (item != progress->covered_sources.first &&
			     StableIdCompareValue(
				&model->local_progress_sources[item - 1U].value,
				&reference->value) >= 0))
				return 0;
		}
		for (item = progress->admissible_choices.first;
		     item < (size_t)progress->admissible_choices.first +
			progress->admissible_choices.count; item++)
		{
			const sg_field_choice_t *choice =
				FindChoice(model, &model->local_progress_choices[item]);
			if (!choice || !SameStableId(&choice->source_atom.value,
				&progress->source_atom.value) ||
			    (item != progress->admissible_choices.first &&
			     StableIdCompareValue(
				&model->local_progress_choices[item - 1U].value,
				&model->local_progress_choices[item].value) >= 0) ||
			    required_targets > SIZE_MAX - choice->outcomes.count)
				return 0;
			required_targets += choice->outcomes.count;
		}
		if (required_targets != progress->whole_outcome_targets.count)
			return 0;
		for (item = 0U; item < progress->whole_outcome_targets.count; item++)
		{
			const sg_field_progress_target_t *target =
				&model->local_progress_targets[
					(size_t)progress->whole_outcome_targets.first + item];
			size_t remaining = item;
			size_t choice_index;
			const sg_field_outcome_t *expected = NULL;
			for (choice_index = progress->admissible_choices.first;
			     choice_index < (size_t)progress->admissible_choices.first +
				progress->admissible_choices.count; choice_index++)
			{
				const sg_field_choice_t *choice = FindChoice(model,
					&model->local_progress_choices[choice_index]);
				if (remaining < choice->outcomes.count)
				{
					expected = &model->outcomes[
						(size_t)choice->outcomes.first + remaining];
					break;
				}
				remaining -= choice->outcomes.count;
			}
			if (!FindOutcome(model, &target->outcome) ||
			    !expected || !SameStableId(&target->outcome.value,
				&expected->id.value) ||
			    !FindReachAtom(model, &target->atom) ||
			    target->reserved != 0U ||
			    target->maximum_target_rank >= progress->finite_rank)
				return 0;
		}
	}
	return SG_FieldRefinementTreeValid(&model->refinement_tree,
		model->reach_atoms, model->reach_atom_count);
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
	    !model->control_domains || model->control_domain_count == 0U ||
	    model->control_domain_count > UINT32_MAX ||
	    model->response_patch_count == 0U ||
	    model->response_patch_count > UINT32_MAX ||
	    !model->boundary_transfers || model->boundary_transfer_count == 0U ||
	    model->boundary_transfer_count > UINT32_MAX ||
	    !model->reach_atoms || model->reach_atom_count == 0U ||
	    model->reach_atom_count > UINT32_MAX ||
	    !model->outcome_destination_atoms ||
	    model->outcome_destination_atom_count == 0U ||
	    model->outcome_destination_atom_count > UINT32_MAX ||
	    (!model->guard_requirements && model->guard_requirement_count != 0U) ||
	    model->guard_requirement_count > UINT32_MAX ||
	    (!model->guard_effects && model->guard_effect_count != 0U) ||
	    model->guard_effect_count > UINT32_MAX ||
	    !model->outcomes || model->outcome_count == 0U ||
	    model->outcome_count > UINT32_MAX ||
	    !model->choices || model->choice_count == 0U ||
	    model->choice_count > UINT32_MAX ||
	    !model->local_progress_kernels ||
	    model->local_progress_kernel_count == 0U ||
	    model->local_progress_kernel_count > UINT32_MAX ||
	    !model->local_progress_sources || model->local_progress_source_count == 0U ||
	    model->local_progress_source_count > UINT32_MAX ||
	    !model->local_progress_choices || model->local_progress_choice_count == 0U ||
	    model->local_progress_choice_count > UINT32_MAX ||
	    !model->local_progress_targets || model->local_progress_target_count == 0U ||
	    model->local_progress_target_count > UINT32_MAX ||
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
	return DynamicsModelArraysValid(model, snapshot->model,
			source_set_identity) && DynamicsModelOwnershipValid(model) &&
		FieldModelOwnershipValid(model) &&
		SG_RuneDynamicsGeometryValid(model);
}
