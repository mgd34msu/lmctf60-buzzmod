#include "sg_phase_catalog_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct sg_phase_id_ref_s
{
	sg_rune_stable_id_t id;
	uint32_t index;
} sg_phase_id_ref_t;

static int PhaseIdCompare(const void *left_value, const void *right_value)
{
	const sg_phase_id_ref_t *left = left_value;
	const sg_phase_id_ref_t *right = right_value;
	int comparison = SG_RuneModelOrderKeyCompare(
		&(sg_rune_order_key_t){
			left->id.source_set_identity,
			(uint32_t)(left->id.high >> 32),
			(uint32_t)left->id.high,
			(uint32_t)(left->id.low >> 32),
			(uint32_t)left->id.low
		},
		&(sg_rune_order_key_t){
			right->id.source_set_identity,
			(uint32_t)(right->id.high >> 32),
			(uint32_t)right->id.high,
			(uint32_t)(right->id.low >> 32),
			(uint32_t)right->id.low
		});

	if (comparison != 0)
		return comparison;
	if (left->index != right->index)
		return left->index < right->index ? -1 : 1;
	return 0;
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
		catalog->phase_count > catalog->phase_capacity ||
		catalog->binding_count > catalog->binding_capacity ||
		(catalog->phase_count != 0U && !catalog->phases) ||
		(catalog->binding_count != 0U && !catalog->bindings))
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
		if (SG_RuneModelStableIdEqual(&ids[index - 1U].id, &ids[index].id))
		{
			SetReport(report_out, SG_PHASE_CATALOG_AUDIT_DUPLICATE_PHASE,
				ids[index].index);
			free(ids);
			return 1;
		}
	free(ids);
	return 0;
}

static int PhaseIndexById(const sg_phase_catalog_expected_t *expected,
	const sg_rune_phase_ref_t *reference)
{
	uint32_t index;

	if (!reference || !SG_RuneModelStableIdValid(&reference->value))
		return -1;
	for (index = 0U; index < expected->phase_count; index++)
		if (SG_RuneModelStableIdEqual(&expected->phases[index].id.value,
			&reference->value))
			return (int)index;
	return -1;
}

static int DuplicateBinding(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *report_out)
{
	uint32_t left;

	for (left = 0U; left < catalog->binding_count; left++)
	{
		uint32_t right;

		for (right = left + 1U; right < catalog->binding_count; right++)
			if (catalog->bindings[left].semantic_region_id ==
					catalog->bindings[right].semantic_region_id &&
				catalog->bindings[left].configuration_cell ==
					catalog->bindings[right].configuration_cell &&
				SG_RuneModelStableIdEqual(
					&catalog->bindings[left].phase.value,
					&catalog->bindings[right].phase.value))
			{
				SetReport(report_out,
					SG_PHASE_CATALOG_AUDIT_DUPLICATE_BINDING, right);
				return 1;
			}
	}
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
	if (!SG_PhaseCatalogHeaderValid(catalog))
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
		return 0;
	}
	if (!CatalogStorageShapeValid(catalog, result_out))
		return 0;
	memset(&source_error, 0, sizeof(source_error));
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
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	if (catalog->completion != expected.completion ||
		catalog->transition_completion != expected.completion)
	{
		SetReport(result_out,
			SG_PHASE_CATALOG_AUDIT_COMPLETION_DISAGREEMENT, 0U);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	if (catalog->phase_count < expected.phase_count)
	{
		result_out->omitted_phases = expected.phase_count -
			catalog->phase_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE,
			catalog->phase_count);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	if (catalog->phase_count > expected.phase_count)
	{
		result_out->invented_phases = catalog->phase_count -
			expected.phase_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE,
			expected.phase_count);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	for (index = 0U; index < catalog->phase_count; index++)
		if (!SG_RuneModelPhaseValid(&catalog->phases[index]))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT,
				index);
			SG_PhaseCatalogExpectedDestroy(&expected);
			return 0;
		}
	duplicate = DuplicatePhase(catalog, result_out);
	if (duplicate != 0)
	{
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	for (index = 0U; index < catalog->phase_count; index++)
		if (!SG_PhaseCatalogPhaseEqual(&catalog->phases[index],
			&expected.phases[index]))
		{
			uint32_t expected_index;
			int found = 0;

			for (expected_index = 0U; expected_index < expected.phase_count;
				expected_index++)
				if (SG_RuneModelStableIdEqual(
					&catalog->phases[index].id.value,
					&expected.phases[expected_index].id.value))
				{
					found = 1;
					break;
				}
			SetReport(result_out, found ?
				(expected_index == index ?
					SG_PHASE_CATALOG_AUDIT_PHASE_DISAGREEMENT :
					SG_PHASE_CATALOG_AUDIT_NONDETERMINISTIC_ORDER) :
				SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE, index);
			if (!found)
				result_out->invented_phases = 1U;
			SG_PhaseCatalogExpectedDestroy(&expected);
			return 0;
		}
	if (catalog->binding_count < expected.binding_count)
	{
		result_out->omitted_bindings = expected.binding_count -
			catalog->binding_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_BINDING,
			catalog->binding_count);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	if (catalog->binding_count > expected.binding_count)
	{
		result_out->invented_bindings = catalog->binding_count -
			expected.binding_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_BINDING,
			expected.binding_count);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	if (DuplicateBinding(catalog, result_out))
	{
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	for (index = 0U; index < catalog->binding_count; index++)
	{
		const sg_phase_catalog_binding_t *binding =
			&catalog->bindings[index];

		if (PhaseIndexById(&expected, &binding->phase) < 0)
		{
			SetReport(result_out,
				SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING, index);
			SG_PhaseCatalogExpectedDestroy(&expected);
			return 0;
		}
		if (!SG_PhaseCatalogBindingEqual(binding,
			&expected.bindings[index]))
		{
			SetReport(result_out,
				SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT, index);
			SG_PhaseCatalogExpectedDestroy(&expected);
			return 0;
		}
	}
	result_out->code = expected.completion == SG_PHASE_CATALOG_PROVEN_EMPTY ?
		SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY :
		SG_PHASE_CATALOG_AUDIT_OK_COMPLETE;
	result_out->proved_phases = expected.phase_count;
	result_out->proved_bindings = expected.binding_count;
	SG_PhaseCatalogExpectedDestroy(&expected);
	return 1;
}

const char *SG_PhaseCatalogAuditCodeString(sg_phase_catalog_audit_code_t code)
{
	switch (code)
	{
	case SG_PHASE_CATALOG_AUDIT_OK_COMPLETE: return "ok complete";
	case SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY: return "ok proven empty";
	case SG_PHASE_CATALOG_AUDIT_INVALID_ARGUMENT: return "invalid argument";
	case SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT:
		return "storage disagreement";
	case SG_PHASE_CATALOG_AUDIT_SOURCE_MISMATCH: return "source mismatch";
	case SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE: return "omitted phase";
	case SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE: return "invented phase";
	case SG_PHASE_CATALOG_AUDIT_DUPLICATE_PHASE: return "duplicate phase";
	case SG_PHASE_CATALOG_AUDIT_DUPLICATE_BINDING:
		return "duplicate binding";
	case SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING:
		return "unresolved binding";
	case SG_PHASE_CATALOG_AUDIT_OMITTED_BINDING: return "omitted binding";
	case SG_PHASE_CATALOG_AUDIT_INVENTED_BINDING: return "invented binding";
	case SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT:
		return "binding disagreement";
	case SG_PHASE_CATALOG_AUDIT_PHASE_DISAGREEMENT:
		return "phase disagreement";
	case SG_PHASE_CATALOG_AUDIT_INVALID_SOURCE: return "invalid source";
	case SG_PHASE_CATALOG_AUDIT_COMPLETION_DISAGREEMENT:
		return "completion disagreement";
	case SG_PHASE_CATALOG_AUDIT_NONDETERMINISTIC_ORDER:
		return "nondeterministic order";
	case SG_PHASE_CATALOG_AUDIT_CODE_COUNT: break;
	}
	return "unknown phase catalog audit code";
}
