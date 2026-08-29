#include "sg_phase_catalog_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_phase_id_ref_s
{
	sg_rune_stable_id_t id;
	uint32_t index;
} sg_phase_id_ref_t;

typedef struct sg_binding_ref_s
{
	uint64_t region;
	uint32_t cell;
	sg_rune_stable_id_t phase;
	uint32_t index;
} sg_binding_ref_t;

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static int StableIdCompare(const sg_rune_stable_id_t *left,
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

static int PhaseIdCompare(const void *left_value, const void *right_value)
{
	const sg_phase_id_ref_t *left = left_value;
	const sg_phase_id_ref_t *right = right_value;
	int comparison = StableIdCompare(&left->id, &right->id);

	if (comparison != 0)
		return comparison;
	return left->index == right->index ? 0 : (left->index < right->index ? -1 : 1);
}

static int BindingRefCompare(const void *left_value, const void *right_value)
{
	const sg_binding_ref_t *left = left_value;
	const sg_binding_ref_t *right = right_value;
	int comparison;

	if (left->region != right->region)
		return left->region < right->region ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	comparison = StableIdCompare(&left->phase, &right->phase);
	if (comparison != 0)
		return comparison;
	return left->index == right->index ? 0 : (left->index < right->index ? -1 : 1);
}

static int TransitionIdCompare(const void *left_value, const void *right_value)
{
	const sg_phase_id_ref_t *left = left_value;
	const sg_phase_id_ref_t *right = right_value;

	return PhaseIdCompare(left, right);
}

static void SetReport(sg_phase_catalog_audit_result_t *report_out,
	sg_phase_catalog_audit_code_t code, uint32_t record)
{
	report_out->code = code;
	report_out->record = record;
}

static int CatalogStorageShapeValid(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *report_out)
{
	if (catalog->phase_capacity > SG_RUNE_MODEL_MAX_PHASES ||
		catalog->binding_capacity > SG_PHASE_CATALOG_MAX_BINDINGS ||
		catalog->transition_capacity > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		!AllocationFits((size_t)catalog->phase_capacity,
			sizeof(*catalog->phases)) ||
		!AllocationFits((size_t)catalog->binding_capacity,
			sizeof(*catalog->bindings)) ||
		!AllocationFits((size_t)catalog->transition_capacity,
			sizeof(*catalog->transitions)) ||
		!AllocationFits((size_t)catalog->transition_capacity,
			sizeof(*catalog->transition_evidence)) ||
		catalog->phase_count > catalog->phase_capacity ||
		catalog->binding_count > catalog->binding_capacity ||
		catalog->transition_count > catalog->transition_capacity ||
		(catalog->phase_count != 0U && !catalog->phases) ||
		(catalog->binding_count != 0U && !catalog->bindings) ||
		(catalog->transition_count != 0U &&
			(!catalog->transitions || !catalog->transition_evidence)))
	{
		SetReport(report_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
		return 0;
	}
	return 1;
}

static int DuplicatePhase(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *report_out)
{
	sg_phase_id_ref_t *ids;
	uint32_t index;

	if (catalog->phase_count < 2U)
		return 0;
	ids = malloc((size_t)catalog->phase_count * sizeof(*ids));
	if (!ids)
	{
		SetReport(report_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
		return -1;
	}
	for (index = 0U; index < catalog->phase_count; index++)
	{
		ids[index].id = catalog->phases[index].id.value;
		ids[index].index = index;
	}
	qsort(ids, catalog->phase_count, sizeof(*ids), PhaseIdCompare);
	for (index = 1U; index < catalog->phase_count; index++)
		if (StableIdCompare(&ids[index - 1U].id, &ids[index].id) == 0)
		{
			SetReport(report_out, SG_PHASE_CATALOG_AUDIT_DUPLICATE_PHASE,
				ids[index].index);
			free(ids);
			return 1;
		}
	free(ids);
	return 0;
}

static int DuplicateBinding(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *report_out)
{
	sg_binding_ref_t *refs;
	uint32_t index;

	if (catalog->binding_count < 2U)
		return 0;
	refs = malloc((size_t)catalog->binding_count * sizeof(*refs));
	if (!refs)
	{
		SetReport(report_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
		return -1;
	}
	for (index = 0U; index < catalog->binding_count; index++)
	{
		refs[index].region = catalog->bindings[index].semantic_region_id;
		refs[index].cell = catalog->bindings[index].configuration_cell;
		refs[index].phase = catalog->bindings[index].phase.value;
		refs[index].index = index;
	}
	qsort(refs, catalog->binding_count, sizeof(*refs), BindingRefCompare);
	for (index = 1U; index < catalog->binding_count; index++)
		if (refs[index - 1U].region == refs[index].region &&
			refs[index - 1U].cell == refs[index].cell &&
			StableIdCompare(&refs[index - 1U].phase, &refs[index].phase) == 0)
		{
			SetReport(report_out, SG_PHASE_CATALOG_AUDIT_DUPLICATE_BINDING,
				refs[index].index);
			free(refs);
			return 1;
		}
	free(refs);
	return 0;
}

static int DuplicateTransition(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *report_out)
{
	sg_phase_id_ref_t *ids;
	uint32_t index;

	if (catalog->transition_count < 2U)
		return 0;
	ids = malloc((size_t)catalog->transition_count * sizeof(*ids));
	if (!ids)
	{
		SetReport(report_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
		return -1;
	}
	for (index = 0U; index < catalog->transition_count; index++)
	{
		ids[index].id = catalog->transitions[index].id.value;
		ids[index].index = index;
	}
	qsort(ids, catalog->transition_count, sizeof(*ids), TransitionIdCompare);
	for (index = 1U; index < catalog->transition_count; index++)
		if (StableIdCompare(&ids[index - 1U].id, &ids[index].id) == 0)
		{
			SetReport(report_out,
				SG_PHASE_CATALOG_AUDIT_DUPLICATE_TRANSITION, ids[index].index);
			free(ids);
			return 1;
		}
	free(ids);
	return 0;
}

static int PhaseIndexById(const sg_phase_catalog_expected_t *expected,
	const sg_rune_phase_ref_t *reference)
{
	uint32_t first = 0U;
	uint32_t last = expected->phase_count;

	if (!reference || !SG_RuneModelStableIdValid(&reference->value))
		return -1;
	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		int comparison = StableIdCompare(
			&expected->phases[middle].id.value, &reference->value);

		if (comparison == 0)
			return (int)middle;
		if (comparison < 0)
			first = middle + 1U;
		else
			last = middle;
	}
	return -1;
}

static int RegionIndexById(const sg_configuration_semantics_t *semantics,
	uint64_t id)
{
	uint32_t first = 0U;
	uint32_t last = semantics->region_count;

	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;

		if (semantics->regions[middle].id == id)
			return (int)middle;
		if (semantics->regions[middle].id < id)
			first = middle + 1U;
		else
			last = middle;
	}
	return -1;
}

static int PhaseExceptStanceEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value) &&
		memcmp(&left->velocity, &right->velocity, sizeof(left->velocity)) == 0 &&
		memcmp(&left->elapsed_ms, &right->elapsed_ms, sizeof(left->elapsed_ms)) == 0 &&
		left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int IntervalValid(const sg_rune_interval_t *interval)
{
	return interval && isfinite(interval->min_value) &&
		isfinite(interval->max_value) && interval->min_value <= interval->max_value;
}

static uint32_t TimingSpan(const sg_mechanism_capability_fact_t *fact)
{
	uint64_t total = (uint64_t)fact->delay_ms + fact->dwell_ms +
		fact->travel_ms + fact->wait_ms + fact->reset_ms;

	if (total == 0U)
		return 1U;
	return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static int TransitionValid(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_expected_t *expected, uint32_t index,
	const sg_rune_phase_transition_t *transition,
	const sg_phase_catalog_transition_evidence_t *evidence)
{
	int source_phase;
	int destination_phase;
	const sg_configuration_space_t *configuration = source->configuration;
	const sg_configuration_semantics_t *semantics = source->semantics;
	sg_rune_stable_id_t expected_id;

	if (!transition || !evidence || !SG_RuneModelStableIdValid(&transition->id.value) ||
		!SG_RuneModelOrderKeyValid(&transition->order) ||
		transition->order.domain != SG_RUNE_ORDER_PHASE_TRANSITION ||
		transition->order.source_set_identity !=
			source->authority->identity.source_set_identity ||
		!SG_RuneModelStableIdValid(&transition->cell.value) ||
		!IntervalValid(&transition->duration_ms) ||
		transition->duration_ms.max_value <= 0.0f || transition->flags != 0U ||
		evidence->origin < SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP ||
		evidence->origin >= SG_PHASE_CATALOG_TRANSITION_ORIGIN_COUNT ||
		evidence->source_cell >= configuration->cell_count ||
		evidence->destination_cell >= configuration->cell_count ||
		!SG_RuneModelStableIdEqual(&transition->cell.value,
			&configuration->cells[evidence->source_cell].id.value))
		return 0;
	expected_id = SG_RuneModelStableIdFromOrderKey(&transition->order);
	if (!SG_RuneModelStableIdEqual(&transition->id.value, &expected_id))
		return 0;
	source_phase = PhaseIndexById(expected, &transition->source_phase);
	destination_phase = PhaseIndexById(expected, &transition->destination_phase);
	if (source_phase < 0 || destination_phase < 0 || source_phase == destination_phase ||
		expected->phases[source_phase].order.source_index != evidence->source_cell ||
		expected->phases[destination_phase].order.source_index !=
			evidence->destination_cell)
		return 0;
	switch (evidence->origin)
	{
	case SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP:
	{
		int source_region = RegionIndexById(semantics,
			evidence->source_region_id);
		int destination_region = RegionIndexById(semantics,
			evidence->destination_region_id);

		return transition->kind == SG_RUNE_PHASE_TRANSITION_STANCE &&
			evidence->source_record < configuration->stance_overlap_count &&
			evidence->destination_record == SG_PHASE_CATALOG_INDEX_NONE &&
			evidence->source_cell == configuration->stance_overlaps[
				evidence->source_record].standing_cell &&
			evidence->destination_cell == configuration->stance_overlaps[
				evidence->source_record].crouching_cell && source_region >= 0 &&
			destination_region >= 0 && semantics->regions[source_region].cell ==
			evidence->source_cell && semantics->regions[destination_region].cell ==
			evidence->destination_cell &&
			expected->phases[source_phase].stance == SG_RUNE_STANCE_STANDING &&
			expected->phases[destination_phase].stance == SG_RUNE_STANCE_CROUCHING &&
			PhaseExceptStanceEqual(&expected->phases[source_phase],
				&expected->phases[destination_phase]);
	}
	case SG_PHASE_CATALOG_TRANSITION_PORTAL:
	{
		const sg_configuration_portal_t *portal;

		if (transition->kind != SG_RUNE_PHASE_TRANSITION_TIME ||
			evidence->source_record >= configuration->portal_count ||
			evidence->destination_record >= configuration->cell_count ||
			evidence->source_state_mask != 0U || evidence->destination_state_mask != 0U ||
			evidence->provider_verifier_identity != 0U)
			return 0;
		portal = &configuration->portals[evidence->source_record];
		return SG_RuneModelStableIdEqual(&evidence->portal.value, &portal->id.value) &&
			((evidence->source_cell == portal->from_cell &&
				evidence->destination_cell == portal->to_cell) ||
			 (evidence->source_cell == portal->to_cell &&
				evidence->destination_cell == portal->from_cell)) &&
			evidence->source_region_id != evidence->destination_region_id &&
			RegionIndexById(semantics, evidence->source_region_id) >= 0 &&
			RegionIndexById(semantics, evidence->destination_region_id) >= 0 &&
			semantics->regions[RegionIndexById(semantics,
				evidence->source_region_id)].cell == evidence->source_cell &&
			semantics->regions[RegionIndexById(semantics,
				evidence->destination_region_id)].cell == evidence->destination_cell;
	}
	case SG_PHASE_CATALOG_TRANSITION_SUPPORT_CHANGE:
		return transition->kind == SG_RUNE_PHASE_TRANSITION_SUPPORT &&
			evidence->source_cell == evidence->destination_cell &&
			evidence->source_record < semantics->region_count &&
			evidence->destination_record < semantics->region_count &&
			semantics->regions[evidence->source_record].cell == evidence->source_cell &&
			semantics->regions[evidence->destination_record].cell ==
				evidence->destination_cell &&
			evidence->source_region_id == semantics->regions[
				evidence->source_record].id &&
			evidence->destination_region_id == semantics->regions[
				evidence->destination_record].id &&
			expected->phases[source_phase].motion == SG_RUNE_MOTION_AIRBORNE &&
			expected->phases[source_phase].support == SG_RUNE_SUPPORT_NONE &&
			expected->phases[destination_phase].motion ==
				SG_RUNE_MOTION_SUPPORTED &&
			expected->phases[destination_phase].support != SG_RUNE_SUPPORT_NONE &&
			expected->phases[source_phase].stance ==
				expected->phases[destination_phase].stance &&
			expected->phases[source_phase].void_relation ==
				expected->phases[destination_phase].void_relation &&
			expected->phases[source_phase].time_quantum_ms ==
				expected->phases[destination_phase].time_quantum_ms &&
			expected->phases[source_phase].time_horizon_ms ==
				expected->phases[destination_phase].time_horizon_ms &&
			evidence->source_state_mask == 0U && evidence->destination_state_mask == 0U &&
			evidence->provider_verifier_identity == 0U;
	case SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING:
	{
		const sg_mechanism_capability_fact_t *fact;
		uint32_t source_mask;
		uint32_t destination_mask;
		int source_region;
		int destination_region;

		if (evidence->source_record >= source->mover_support_provider->fact_count ||
			evidence->destination_record != SG_PHASE_CATALOG_INDEX_NONE ||
			(evidence->source_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			(evidence->destination_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			evidence->provider_verifier_identity !=
				source->mover_support_provider->verifier_identity)
			return 0;
		fact = &source->mover_support_provider->facts[evidence->source_record];
		if (fact->source_state < SG_MECHANISM_STATE_INACTIVE ||
			fact->source_state >= SG_MECHANISM_STATE_COUNT ||
			fact->destination_state < SG_MECHANISM_STATE_INACTIVE ||
			fact->destination_state >= SG_MECHANISM_STATE_COUNT)
			return 0;
		source_mask = UINT32_C(1) << (uint32_t)fact->source_state;
		destination_mask = UINT32_C(1) << (uint32_t)fact->destination_state;
		source_region = RegionIndexById(semantics, evidence->source_region_id);
		destination_region = RegionIndexById(semantics,
			evidence->destination_region_id);
		return transition->kind == (fact->kind == SG_MECHANISM_CAPABILITY_DWELL ?
			SG_RUNE_PHASE_TRANSITION_MOVER_DWELL : SG_RUNE_PHASE_TRANSITION_TIME) &&
			transition->duration_ms.min_value == (float)TimingSpan(fact) &&
			transition->duration_ms.max_value == (float)TimingSpan(fact) &&
			transition->kind != SG_RUNE_PHASE_TRANSITION_NONE &&
			transition->kind != SG_RUNE_PHASE_TRANSITION_KIND_COUNT &&
			SG_RuneModelStableIdEqual(&evidence->mechanism.value,
				&fact->mechanism_id.value) && source_region >= 0 &&
			destination_region >= 0 && fact->source_region == (uint32_t)source_region &&
			fact->destination_region == (uint32_t)destination_region &&
			semantics->regions[source_region].cell == evidence->source_cell &&
			semantics->regions[destination_region].cell == evidence->destination_cell &&
			evidence->source_state_mask == source_mask &&
			evidence->destination_state_mask == destination_mask &&
			evidence->delay_ms == fact->delay_ms && evidence->dwell_ms == fact->dwell_ms &&
			evidence->travel_ms == fact->travel_ms && evidence->wait_ms == fact->wait_ms &&
			evidence->reset_ms == fact->reset_ms &&
			evidence->activation_time_ms == fact->activation_time_ms &&
			evidence->active_time_ms == fact->active_time_ms &&
			evidence->exit_time_ms == fact->exit_time_ms &&
			evidence->reset_time_ms == fact->reset_time_ms &&
			SG_RuneModelStableIdEqual(&expected->phases[source_phase].mover.value,
				&evidence->mechanism.value) &&
			SG_RuneModelStableIdEqual(&expected->phases[destination_phase].mover.value,
				&evidence->mechanism.value);
	}
	case SG_PHASE_CATALOG_TRANSITION_ORIGIN_COUNT:
		break;
	}
	(void)index;
	return 0;
}

int SG_PhaseCatalogAudit(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *result_out)
{
	sg_phase_catalog_expected_t expected;
	sg_phase_catalog_error_t source_error;
	uint32_t index;
	int duplicate;

	if (result_out)
		memset(result_out, 0, sizeof(*result_out));
	if (!result_out || !source || !catalog)
	{
		if (result_out)
			result_out->code = SG_PHASE_CATALOG_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	if (!SG_PhaseCatalogHeaderValid(catalog) ||
		!CatalogStorageShapeValid(catalog, result_out))
	{
		if (!SG_PhaseCatalogHeaderValid(catalog))
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
		return 0;
	}
	memset(&source_error, 0, sizeof(source_error));
	memset(&expected, 0, sizeof(expected));
	if (!SG_PhaseCatalogBuildExpected(source, &expected, &source_error))
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVALID_SOURCE,
			source_error.source_index);
		return 0;
	}
	if (!SG_PhaseCatalogIdentityEqual(&catalog->identity,
		&source->authority->identity) ||
		catalog->mover_support_verifier_identity !=
			expected.mover_support_verifier_identity)
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_SOURCE_MISMATCH, 0U);
		goto failure;
	}
	if (catalog->completion != expected.completion ||
		catalog->transition_completion != expected.transition_completion)
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_COMPLETION_DISAGREEMENT, 0U);
		goto failure;
	}
	if (catalog->phase_count < expected.phase_count)
	{
		result_out->omitted_phases = expected.phase_count - catalog->phase_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE,
			catalog->phase_count);
		goto failure;
	}
	if (catalog->phase_count > expected.phase_count)
	{
		result_out->invented_phases = catalog->phase_count - expected.phase_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE,
			expected.phase_count);
		goto failure;
	}
	for (index = 0U; index < catalog->phase_count; index++)
	{
		if (!SG_RuneModelPhaseValid(&catalog->phases[index]) ||
			(index != 0U && SG_RuneModelOrderKeyCompare(
				&catalog->phases[index - 1U].order,
				&catalog->phases[index].order) >= 0))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT,
				index);
			goto failure;
		}
	}
	duplicate = DuplicatePhase(catalog, result_out);
	if (duplicate != 0)
		goto failure;
	for (index = 0U; index < catalog->phase_count; index++)
		if (!SG_PhaseCatalogPhaseEqual(&catalog->phases[index],
			&expected.phases[index]))
		{
			int expected_index = PhaseIndexById(&expected,
				&catalog->phases[index].id);

			SetReport(result_out, expected_index < 0 ?
				SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE :
				(expected_index == (int)index ?
					SG_PHASE_CATALOG_AUDIT_PHASE_DISAGREEMENT :
					SG_PHASE_CATALOG_AUDIT_NONDETERMINISTIC_ORDER), index);
			if (expected_index < 0)
				result_out->invented_phases = 1U;
			goto failure;
		}
	if (catalog->binding_count < expected.binding_count)
	{
		result_out->omitted_bindings = expected.binding_count -
			catalog->binding_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_BINDING,
			catalog->binding_count);
		goto failure;
	}
	if (catalog->binding_count > expected.binding_count)
	{
		result_out->invented_bindings = catalog->binding_count -
			expected.binding_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_BINDING,
			expected.binding_count);
		goto failure;
	}
	duplicate = DuplicateBinding(catalog, result_out);
	if (duplicate != 0)
		goto failure;
	for (index = 0U; index < catalog->binding_count; index++)
	{
		if (PhaseIndexById(&expected, &catalog->bindings[index].phase) < 0)
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING,
				index);
			goto failure;
		}
		if (!SG_PhaseCatalogBindingEqual(&catalog->bindings[index],
			&expected.bindings[index]))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT,
				index);
			goto failure;
		}
	}
	if (catalog->transition_count < expected.transition_count)
	{
		result_out->omitted_phases = expected.transition_count -
			catalog->transition_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_TRANSITION,
			catalog->transition_count);
		goto failure;
	}
	if (catalog->transition_count > expected.transition_count)
	{
		result_out->invented_phases = catalog->transition_count -
			expected.transition_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_TRANSITION,
			expected.transition_count);
		goto failure;
	}
	duplicate = DuplicateTransition(catalog, result_out);
	if (duplicate != 0)
		goto failure;
	for (index = 0U; index < catalog->transition_count; index++)
	{
		if (!TransitionValid(source, &expected, index,
			&catalog->transitions[index], &catalog->transition_evidence[index]))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_TRANSITION_DISAGREEMENT,
				index);
			goto failure;
		}
		if (memcmp(&catalog->transitions[index], &expected.transitions[index],
				sizeof(*catalog->transitions)) != 0 ||
			memcmp(&catalog->transition_evidence[index],
				&expected.transition_evidence[index],
				sizeof(*catalog->transition_evidence)) != 0)
		{
			SetReport(result_out,
				SG_PHASE_CATALOG_AUDIT_TRANSITION_DISAGREEMENT, index);
			goto failure;
		}
	}
	result_out->code = expected.completion == SG_PHASE_CATALOG_PROVEN_EMPTY ?
		SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY : SG_PHASE_CATALOG_AUDIT_OK_COMPLETE;
	result_out->proved_phases = expected.phase_count;
	result_out->proved_bindings = expected.binding_count;
	SG_PhaseCatalogExpectedDestroy(&expected);
	return 1;

failure:
	SG_PhaseCatalogExpectedDestroy(&expected);
	return 0;
}

const char *SG_PhaseCatalogAuditCodeString(sg_phase_catalog_audit_code_t code)
{
	switch (code)
	{
	case SG_PHASE_CATALOG_AUDIT_OK_COMPLETE: return "ok complete";
	case SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY: return "ok proven empty";
	case SG_PHASE_CATALOG_AUDIT_INVALID_ARGUMENT: return "invalid argument";
	case SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT: return "storage disagreement";
	case SG_PHASE_CATALOG_AUDIT_SOURCE_MISMATCH: return "source mismatch";
	case SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE: return "omitted phase";
	case SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE: return "invented phase";
	case SG_PHASE_CATALOG_AUDIT_DUPLICATE_PHASE: return "duplicate phase";
	case SG_PHASE_CATALOG_AUDIT_DUPLICATE_BINDING: return "duplicate binding";
	case SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING: return "unresolved binding";
	case SG_PHASE_CATALOG_AUDIT_OMITTED_BINDING: return "omitted binding";
	case SG_PHASE_CATALOG_AUDIT_INVENTED_BINDING: return "invented binding";
	case SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT: return "binding disagreement";
	case SG_PHASE_CATALOG_AUDIT_PHASE_DISAGREEMENT: return "phase disagreement";
	case SG_PHASE_CATALOG_AUDIT_OMITTED_TRANSITION: return "omitted transition";
	case SG_PHASE_CATALOG_AUDIT_INVENTED_TRANSITION: return "invented transition";
	case SG_PHASE_CATALOG_AUDIT_DUPLICATE_TRANSITION: return "duplicate transition";
	case SG_PHASE_CATALOG_AUDIT_TRANSITION_DISAGREEMENT:
		return "transition disagreement";
	case SG_PHASE_CATALOG_AUDIT_INVALID_SOURCE: return "invalid source";
	case SG_PHASE_CATALOG_AUDIT_COMPLETION_DISAGREEMENT:
		return "completion disagreement";
	case SG_PHASE_CATALOG_AUDIT_NONDETERMINISTIC_ORDER:
		return "nondeterministic order";
	case SG_PHASE_CATALOG_AUDIT_CODE_COUNT: break;
	}
	return "unknown phase catalog audit code";
}
