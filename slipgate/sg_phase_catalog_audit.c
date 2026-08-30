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

typedef struct sg_audit_phase_entry_s
{
	sg_rune_phase_basis_t phase;
	uint32_t region;
	size_t sequence;
} sg_audit_phase_entry_t;

typedef struct sg_audit_oracle_s
{
	sg_audit_phase_entry_t *phases;
	uint32_t phase_count;
	sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;
	sg_phase_catalog_transition_pair_t *transitions;
	uint32_t transition_count;
	uint32_t *transition_hash;
	uint32_t transition_hash_capacity;
	sg_phase_catalog_completion_t completion;
	sg_phase_catalog_completion_t transition_completion;
	uint64_t mover_support_verifier_identity;
} sg_audit_oracle_t;

static int StableIdCompare(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right);
static uint32_t OracleStateBit(sg_mechanism_state_t state);

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static void OracleSetError(sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_error_code_t code, uint32_t source_index)
{
	if (error_out && error_out->code == SG_PHASE_CATALOG_ERROR_NONE)
	{
		error_out->code = code;
		error_out->source_index = source_index;
	}
}

static int OracleRegionMedium(const sg_configuration_semantic_region_t *region,
	sg_rune_medium_t *medium_out)
{
	uint32_t flags;

	if (!region || !medium_out)
		return 0;
	flags = region->flags & (SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
		SG_CONFIGURATION_SEMANTIC_REGION_SLIME);
	if ((flags & (flags - 1U)) != 0U)
		return 0;
	if (flags == SG_CONFIGURATION_SEMANTIC_REGION_WATER)
		*medium_out = SG_RUNE_MEDIUM_WATER;
	else if (flags == SG_CONFIGURATION_SEMANTIC_REGION_LAVA)
		*medium_out = SG_RUNE_MEDIUM_LAVA;
	else if (flags == SG_CONFIGURATION_SEMANTIC_REGION_SLIME)
		*medium_out = SG_RUNE_MEDIUM_SLIME;
	else
		*medium_out = SG_RUNE_MEDIUM_DRY;
	return 1;
}

/* This is deliberately an audit-owned derivation.  It repeats the source
 * facts needed to account for a phase, rather than calling the construction
 * builder whose output is being audited. */
static void OracleFillPhase(const sg_phase_catalog_source_t *source,
	const sg_configuration_semantic_region_t *region, int mover,
	const sg_rune_mechanism_ref_t *mechanism, uint32_t variant,
	sg_rune_phase_basis_t *phase_out)
{
	sg_rune_medium_t medium = SG_RUNE_MEDIUM_DRY;
	float speed = source->authority->identity.physics.max_velocity;

	memset(phase_out, 0, sizeof(*phase_out));
	phase_out->order.source_set_identity =
		source->authority->identity.source_set_identity;
	phase_out->order.domain = SG_RUNE_ORDER_PHASE;
	phase_out->order.source_index = region->cell;
	phase_out->order.variant = variant;
	phase_out->stance = source->configuration->cells[region->cell].stance;
	(void)OracleRegionMedium(region, &medium);
	phase_out->motion = mover ? SG_RUNE_MOTION_SUPPORTED :
		(region->water_level >= 2U ? SG_RUNE_MOTION_SWIMMING :
			((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U ?
				SG_RUNE_MOTION_SUPPORTED : SG_RUNE_MOTION_AIRBORNE));
	phase_out->support = mover ? SG_RUNE_SUPPORT_MOVER :
		(phase_out->motion == SG_RUNE_MOTION_SUPPORTED ?
			SG_RUNE_SUPPORT_SUPPORTED : SG_RUNE_SUPPORT_NONE);
	phase_out->medium = medium;
	phase_out->void_relation =
		(region->flags & SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) != 0U ?
			SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR;
	phase_out->reference_frame = mover ? SG_RUNE_FRAME_MOVER_RELATIVE :
		SG_RUNE_FRAME_WORLD;
	phase_out->mover = mover && mechanism ? *mechanism :
		SG_RUNE_MECHANISM_REF_NONE;
	phase_out->velocity.x.min_value = -speed;
	phase_out->velocity.x.max_value = speed;
	phase_out->velocity.y = phase_out->velocity.x;
	phase_out->velocity.z = phase_out->velocity.x;
	phase_out->elapsed_ms.min_value = 0.0f;
	phase_out->elapsed_ms.max_value = (float)
		source->authority->identity.physics.frame_ms;
	phase_out->time_quantum_ms = source->authority->identity.physics.substep_ms;
	phase_out->time_horizon_ms = source->authority->identity.physics.frame_ms;
}

static int OracleIntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return left->min_value == right->min_value &&
		left->max_value == right->max_value;
}

static int OracleInterval3Equal(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return OracleIntervalEqual(&left->x, &right->x) &&
		OracleIntervalEqual(&left->y, &right->y) &&
		OracleIntervalEqual(&left->z, &right->z);
}

static int OracleFloatCompare(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	if (left == right)
		return 0;
	if (!isnan(left) && !isnan(right))
		return left < right ? -1 : 1;
	if (isnan(left) != isnan(right))
		return isnan(left) ? 1 : -1;
	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits ? 0 : (left_bits < right_bits ? -1 : 1);
}

static int OracleIntervalCompare(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	int comparison = OracleFloatCompare(left->min_value, right->min_value);

	return comparison != 0 ? comparison :
		OracleFloatCompare(left->max_value, right->max_value);
}

static int OracleInterval3Compare(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	int comparison = OracleIntervalCompare(&left->x, &right->x);

	if (comparison == 0)
		comparison = OracleIntervalCompare(&left->y, &right->y);
	if (comparison == 0)
		comparison = OracleIntervalCompare(&left->z, &right->z);
	return comparison;
}

static int OraclePhaseEquivalent(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->stance == right->stance &&
		left->motion == right->motion && left->support == right->support &&
		left->medium == right->medium && left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value) &&
		OracleInterval3Equal(&left->velocity, &right->velocity) &&
		OracleIntervalEqual(&left->elapsed_ms, &right->elapsed_ms) &&
		left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int OraclePhaseNeutralEquivalent(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value) &&
		OracleInterval3Equal(&left->velocity, &right->velocity) &&
		OracleIntervalEqual(&left->elapsed_ms, &right->elapsed_ms) &&
		left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int OraclePhaseBasisCompare(const void *left_value,
	const void *right_value)
{
	const sg_audit_phase_entry_t *left = left_value;
	const sg_audit_phase_entry_t *right = right_value;
	int comparison;

	if (left->phase.order.source_index != right->phase.order.source_index)
		return left->phase.order.source_index < right->phase.order.source_index ?
			-1 : 1;
	if (left->phase.stance != right->phase.stance)
		return left->phase.stance < right->phase.stance ? -1 : 1;
	if (left->phase.motion != right->phase.motion)
		return left->phase.motion < right->phase.motion ? -1 : 1;
	if (left->phase.support != right->phase.support)
		return left->phase.support < right->phase.support ? -1 : 1;
	if (left->phase.medium != right->phase.medium)
		return left->phase.medium < right->phase.medium ? -1 : 1;
	if (left->phase.void_relation != right->phase.void_relation)
		return left->phase.void_relation < right->phase.void_relation ? -1 : 1;
	if (left->phase.reference_frame != right->phase.reference_frame)
		return left->phase.reference_frame < right->phase.reference_frame ? -1 : 1;
	if ((comparison = StableIdCompare(&left->phase.mover.value,
		&right->phase.mover.value)) != 0)
		return comparison;
	if ((comparison = OracleInterval3Compare(&left->phase.velocity,
		&right->phase.velocity)) != 0)
		return comparison;
	if ((comparison = OracleIntervalCompare(&left->phase.elapsed_ms,
		&right->phase.elapsed_ms)) != 0)
		return comparison;
	if (left->phase.time_quantum_ms != right->phase.time_quantum_ms)
		return left->phase.time_quantum_ms < right->phase.time_quantum_ms ? -1 : 1;
	if (left->phase.time_horizon_ms != right->phase.time_horizon_ms)
		return left->phase.time_horizon_ms < right->phase.time_horizon_ms ? -1 : 1;
	if (left->region != right->region)
		return left->region < right->region ? -1 : 1;
	return left->sequence == right->sequence ? 0 :
		(left->sequence < right->sequence ? -1 : 1);
}

static int OraclePhaseSequenceCompare(const void *left_value,
	const void *right_value)
{
	const sg_audit_phase_entry_t *left = left_value;
	const sg_audit_phase_entry_t *right = right_value;

	if (left->sequence != right->sequence)
		return left->sequence < right->sequence ? -1 : 1;
	return OraclePhaseBasisCompare(left_value, right_value);
}

static int OraclePhaseOrderCompare(const void *left_value,
	const void *right_value)
{
	const sg_audit_phase_entry_t *left = left_value;
	const sg_audit_phase_entry_t *right = right_value;

	return SG_RuneModelOrderKeyCompare(&left->phase.order,
		&right->phase.order);
}

static int OracleAppendRawPhase(sg_audit_phase_entry_t **entries_out,
	size_t *count_out, size_t *capacity_out,
	const sg_rune_phase_basis_t *phase, uint32_t region,
	sg_phase_catalog_error_t *error_out)
{
	sg_audit_phase_entry_t *grown;
	size_t capacity;

	if (!entries_out || !count_out || !capacity_out || !phase ||
		*count_out == SIZE_MAX)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
		return 0;
	}
	if (*count_out == *capacity_out)
	{
		capacity = *capacity_out == 0U ? 16U : *capacity_out;
		while (capacity <= *count_out)
		{
			if (capacity > SIZE_MAX / 2U)
			{
				capacity = SIZE_MAX;
				break;
			}
			capacity *= 2U;
		}
		if (capacity == SIZE_MAX && *count_out == SIZE_MAX - 1U)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
			return 0;
		}
		if (!AllocationFits(capacity, sizeof(*grown)))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
			return 0;
		}
		grown = realloc(*entries_out, capacity * sizeof(*grown));
		if (!grown)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		*entries_out = grown;
		*capacity_out = capacity;
	}
	memset(&(*entries_out)[*count_out], 0,
		sizeof((*entries_out)[*count_out]));
	(*entries_out)[*count_out].phase = *phase;
	(*entries_out)[*count_out].region = region;
	(*entries_out)[*count_out].sequence = *count_out;
	(*count_out)++;
	return 1;
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

static int OracleCellRange(const sg_audit_oracle_t *oracle, uint32_t cell,
	uint32_t *first_out, uint32_t *last_out)
{
	uint32_t first = 0U;
	uint32_t last = oracle->phase_count;

	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;

		if (oracle->phases[middle].phase.order.source_index < cell)
			first = middle + 1U;
		else
			last = middle;
	}
	*first_out = first;
	last = oracle->phase_count;
	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;

		if (oracle->phases[middle].phase.order.source_index <= cell)
			first = middle + 1U;
		else
			last = middle;
	}
	*last_out = first;
	return *first_out != *last_out;
}

static int OracleFindPhaseForRegion(const sg_audit_oracle_t *oracle,
	uint32_t cell, uint32_t region, const sg_rune_phase_basis_t *candidate,
	uint32_t *phase_out)
{
	uint32_t first;
	uint32_t last;
	uint32_t index;

	if (!oracle || !oracle->phases || !candidate ||
		!OracleCellRange(oracle, cell, &first, &last))
		return 0;
	for (index = first; index < last; index++)
		if (oracle->phases[index].region == region &&
			OraclePhaseEquivalent(&oracle->phases[index].phase, candidate))
		{
			if (phase_out)
				*phase_out = index;
			return 1;
		}
	return 0;
}

static int OracleFindNeutralPhase(const sg_audit_oracle_t *oracle,
	uint32_t cell, const sg_rune_phase_basis_t *candidate,
	sg_rune_stance_t stance, uint32_t *phase_out)
{
	uint32_t first;
	uint32_t last;
	uint32_t index;

	if (!OracleCellRange(oracle, cell, &first, &last))
		return 0;
	for (index = first; index < last; index++)
		if (oracle->phases[index].phase.stance == stance &&
			OraclePhaseNeutralEquivalent(&oracle->phases[index].phase, candidate))
		{
			if (phase_out)
				*phase_out = index;
			return 1;
		}
	return 0;
}

static uint32_t OracleTimingSpan(const sg_mechanism_capability_fact_t *fact)
{
	uint64_t total = (uint64_t)fact->delay_ms + fact->dwell_ms +
		fact->travel_ms + fact->wait_ms + fact->reset_ms;

	if (total == 0U)
		return 1U;
	return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static int OracleBuildPhases(const sg_phase_catalog_source_t *source,
	sg_audit_oracle_t *oracle, sg_phase_catalog_error_t *error_out)
{
	sg_audit_phase_entry_t *raw = NULL;
	size_t raw_count = 0U;
	size_t raw_capacity = 0U;
	uint32_t *cell_ordinals = NULL;
	uint32_t region;
	uint32_t support_cursor = 0U;
	uint32_t fact;

	for (region = 0U; region < source->semantics->region_count; region++)
	{
		sg_rune_phase_basis_t phase;

		OracleFillPhase(source, &source->semantics->regions[region], 0, NULL,
			0U, &phase);
		if (!OracleAppendRawPhase(&raw, &raw_count, &raw_capacity, &phase,
			region, error_out))
			goto failure;
		while (support_cursor < SG_PHASE_SOURCE_PROVIDER(source)->support_count &&
			SG_PHASE_SOURCE_PROVIDER(source)->supports[support_cursor].
				semantic_region_id < source->semantics->regions[region].id)
			support_cursor++;
		while (support_cursor < SG_PHASE_SOURCE_PROVIDER(source)->support_count &&
			SG_PHASE_SOURCE_PROVIDER(source)->supports[support_cursor].
				semantic_region_id == source->semantics->regions[region].id)
		{
			const sg_phase_mover_support_t *record =
				&SG_PHASE_SOURCE_PROVIDER(source)->supports[support_cursor];

			OracleFillPhase(source, &source->semantics->regions[region], 1,
				&record->mechanism, 1U, &phase);
			if (!OracleAppendRawPhase(&raw, &raw_count, &raw_capacity, &phase,
				region, error_out))
				goto failure;
			support_cursor++;
		}
	}
	if (support_cursor != SG_PHASE_SOURCE_PROVIDER(source)->support_count)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
			support_cursor);
		goto failure;
	}
	for (fact = 0U; fact < SG_PHASE_SOURCE_PROVIDER(source)->fact_count; fact++)
	{
		const sg_mechanism_capability_fact_t *record =
			&SG_PHASE_SOURCE_PROVIDER(source)->facts[fact];
		sg_rune_phase_basis_t phase;
		uint32_t elapsed;
		uint32_t frame = source->authority->identity.physics.frame_ms;

		if (record->source_region >= source->semantics->region_count ||
			record->destination_region >= source->semantics->region_count)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, fact);
			goto failure;
		}
		OracleFillPhase(source, &source->semantics->regions[
			record->destination_region], 1, &record->mechanism_id, 1U, &phase);
		elapsed = OracleTimingSpan(record);
		if (elapsed > frame)
			elapsed = frame;
		if (elapsed == 0U)
			elapsed = source->authority->identity.physics.substep_ms;
		phase.order.variant = 3U;
		phase.elapsed_ms.min_value = (float)elapsed;
		phase.elapsed_ms.max_value = (float)frame;
		if (!OracleAppendRawPhase(&raw, &raw_count, &raw_capacity, &phase,
			record->destination_region, error_out))
			goto failure;
	}
	if (raw_count > UINT32_MAX)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, UINT32_MAX);
		goto failure;
	}
	if (raw_count != 0U)
	{
		size_t unique = 0U;
		size_t index;

		qsort(raw, raw_count, sizeof(*raw), OraclePhaseBasisCompare);
		for (index = 0U; index < raw_count; index++)
		{
			if (unique == 0U || raw[index].region != raw[unique - 1U].region ||
				raw[index].phase.order.source_index !=
					raw[unique - 1U].phase.order.source_index ||
				!OraclePhaseEquivalent(&raw[index].phase,
					&raw[unique - 1U].phase))
				raw[unique++] = raw[index];
		}
		raw_count = unique;
		qsort(raw, raw_count, sizeof(*raw), OraclePhaseSequenceCompare);
	}
	if (raw_count > SG_RUNE_MODEL_MAX_PHASES)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			SG_RUNE_MODEL_MAX_PHASES);
		goto failure;
	}
	if (source->configuration->cell_count != 0U)
	{
		if (!AllocationFits((size_t)source->configuration->cell_count,
			sizeof(*cell_ordinals)))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
				source->configuration->cell_count);
			goto failure;
		}
		cell_ordinals = calloc((size_t)source->configuration->cell_count,
			sizeof(*cell_ordinals));
		if (!cell_ordinals)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			goto failure;
		}
	}
	for (region = 0U; region < (uint32_t)raw_count; region++)
	{
		uint32_t cell = raw[region].phase.order.source_index;
		uint32_t ordinal;

		if (cell >= source->configuration->cell_count ||
			cell_ordinals[cell] >= SG_RUNE_MODEL_MAX_CELL_PHASES)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, cell);
			goto failure;
		}
		ordinal = cell_ordinals[cell]++;
		raw[region].phase.order.local_ordinal = ordinal;
		raw[region].phase.id.value = SG_RuneModelStableIdFromOrderKey(
			&raw[region].phase.order);
		if (!SG_RuneModelPhaseValid(&raw[region].phase))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_PHASE,
				region);
			goto failure;
		}
	}
	if (raw_count != 0U)
		qsort(raw, raw_count, sizeof(*raw), OraclePhaseOrderCompare);
	oracle->phases = raw;
	oracle->phase_count = (uint32_t)raw_count;
	free(cell_ordinals);
	return 1;

failure:
	free(cell_ordinals);
	free(raw);
	return 0;
}

static int OracleAppendBinding(sg_audit_oracle_t *oracle, size_t *capacity_out,
	uint64_t region_id, uint32_t cell, uint32_t phase,
	sg_phase_mechanism_state_mask_t state_mask,
	sg_phase_catalog_error_t *error_out)
{
	sg_phase_catalog_binding_t *grown;
	sg_phase_catalog_binding_t *binding;
	size_t capacity;
	uint32_t index;
	uint32_t insertion;

	if (!oracle || phase >= oracle->phase_count)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, phase);
		return 0;
	}
	insertion = oracle->binding_count;
	for (index = 0U; index < oracle->binding_count; index++)
	{
		binding = &oracle->bindings[index];
		if (binding->semantic_region_id == region_id &&
			binding->configuration_cell == cell &&
			SG_RuneModelStableIdEqual(&binding->phase.value,
				&oracle->phases[phase].phase.id.value))
		{
			binding->mechanism_state_mask |= state_mask;
			return 1;
		}
		if (insertion == oracle->binding_count &&
			binding->semantic_region_id > region_id)
			insertion = index;
	}
	if (oracle->binding_count >= SG_PHASE_CATALOG_MAX_BINDINGS)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			oracle->binding_count);
		return 0;
	}
	if ((size_t)oracle->binding_count == *capacity_out)
	{
		capacity = *capacity_out == 0U ? 16U : *capacity_out;
		while (capacity <= (size_t)oracle->binding_count)
		{
			if (capacity > SIZE_MAX / 2U)
			{
				capacity = SIZE_MAX;
				break;
			}
			capacity *= 2U;
		}
		if (!AllocationFits(capacity, sizeof(*grown)))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
				oracle->binding_count);
			return 0;
		}
		grown = realloc(oracle->bindings, capacity * sizeof(*grown));
		if (!grown)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		oracle->bindings = grown;
		*capacity_out = capacity;
	}
	if (insertion < oracle->binding_count)
		memmove(&oracle->bindings[insertion + 1U],
			&oracle->bindings[insertion],
			(size_t)(oracle->binding_count - insertion) *
				sizeof(*oracle->bindings));
	binding = &oracle->bindings[insertion];
	memset(binding, 0, sizeof(*binding));
	binding->semantic_region_id = region_id;
	binding->configuration_cell = cell;
	binding->phase = oracle->phases[phase].phase.id;
	binding->mechanism_state_mask = state_mask;
	oracle->binding_count++;
	return 1;
}

static int OracleBuildBindings(const sg_phase_catalog_source_t *source,
	sg_audit_oracle_t *oracle, sg_phase_catalog_error_t *error_out)
{
	size_t capacity = 0U;
	uint32_t region;
	uint32_t support_cursor = 0U;
	uint32_t fact;

	for (region = 0U; region < source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&source->semantics->regions[region];
		sg_rune_phase_basis_t phase;
		uint32_t phase_index;

		OracleFillPhase(source, record, 0, NULL, 0U, &phase);
		if (!OracleFindPhaseForRegion(oracle, record->cell, region, &phase,
			&phase_index) ||
			!OracleAppendBinding(oracle, &capacity, record->id, record->cell,
				phase_index, 0U, error_out))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				region);
			return 0;
		}
		while (support_cursor < SG_PHASE_SOURCE_PROVIDER(source)->support_count &&
			SG_PHASE_SOURCE_PROVIDER(source)->supports[support_cursor].
				semantic_region_id < record->id)
			support_cursor++;
		while (support_cursor < SG_PHASE_SOURCE_PROVIDER(source)->support_count &&
			SG_PHASE_SOURCE_PROVIDER(source)->supports[support_cursor].
			semantic_region_id == record->id)
		{
			const sg_phase_mover_support_t *support =
				&SG_PHASE_SOURCE_PROVIDER(source)->supports[support_cursor];

			OracleFillPhase(source, record, 1, &support->mechanism, 1U, &phase);
			if (!OracleFindPhaseForRegion(oracle, record->cell, region, &phase,
				&phase_index) ||
				!OracleAppendBinding(oracle, &capacity, record->id, record->cell,
					phase_index, support->mechanism_state_mask, error_out))
			{
				OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
					support_cursor);
				return 0;
			}
			support_cursor++;
		}
	}
	if (support_cursor != SG_PHASE_SOURCE_PROVIDER(source)->support_count)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
			support_cursor);
		return 0;
	}
	for (fact = 0U; fact < SG_PHASE_SOURCE_PROVIDER(source)->fact_count; fact++)
	{
		const sg_mechanism_capability_fact_t *record =
			&SG_PHASE_SOURCE_PROVIDER(source)->facts[fact];
		const sg_configuration_semantic_region_t *destination =
			&source->semantics->regions[record->destination_region];
		sg_rune_phase_basis_t phase;
		uint32_t phase_index;
		uint32_t elapsed = OracleTimingSpan(record);
		uint32_t frame = source->authority->identity.physics.frame_ms;

		OracleFillPhase(source, destination, 1, &record->mechanism_id, 1U,
			&phase);
		if (elapsed > frame)
			elapsed = frame;
		if (elapsed == 0U)
			elapsed = source->authority->identity.physics.substep_ms;
		phase.order.variant = 3U;
		phase.elapsed_ms.min_value = (float)elapsed;
		phase.elapsed_ms.max_value = (float)frame;
		if (!OracleFindPhaseForRegion(oracle, destination->cell,
			record->destination_region, &phase, &phase_index) ||
			!OracleAppendBinding(oracle, &capacity, destination->id,
				destination->cell, phase_index,
				(sg_phase_mechanism_state_mask_t)
					OracleStateBit(record->destination_state), error_out))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, fact);
			return 0;
		}
	}
	return 1;
}

static int OracleTransitionPairCompare(const void *left_value,
	const void *right_value)
{
	const sg_phase_catalog_transition_pair_t *left = left_value;
	const sg_phase_catalog_transition_pair_t *right = right_value;
	const sg_rune_phase_transition_t *lt = &left->transition;
	const sg_rune_phase_transition_t *rt = &right->transition;
	int comparison;

	comparison = StableIdCompare(&lt->cell.value, &rt->cell.value);
	if (comparison != 0)
		return comparison;
	comparison = StableIdCompare(&lt->source_phase.value,
		&rt->source_phase.value);
	if (comparison != 0)
		return comparison;
	comparison = StableIdCompare(&lt->destination_phase.value,
		&rt->destination_phase.value);
	if (comparison != 0)
		return comparison;
	if (lt->kind != rt->kind)
		return lt->kind < rt->kind ? -1 : 1;
	comparison = OracleFloatCompare(lt->duration_ms.min_value,
		rt->duration_ms.min_value);
	if (comparison != 0)
		return comparison;
	comparison = OracleFloatCompare(lt->duration_ms.max_value,
		rt->duration_ms.max_value);
	if (comparison != 0)
		return comparison;
	if (lt->flags != rt->flags)
		return lt->flags < rt->flags ? -1 : 1;
	return StableIdCompare(&lt->destination_cell.value,
		&rt->destination_cell.value);
}

static uint64_t OracleHashU32(uint64_t hash, uint32_t value)
{
	uint32_t shift;
	for (shift = 0U; shift != 32U; shift += 8U)
		hash = (hash ^ (uint8_t)(value >> shift)) * UINT64_C(1099511628211);
	return hash;
}

static uint64_t OracleHashU64(uint64_t hash, uint64_t value)
{
	uint32_t shift;
	for (shift = 0U; shift != 64U; shift += 8U)
		hash = (hash ^ (uint8_t)(value >> shift)) * UINT64_C(1099511628211);
	return hash;
}

static uint64_t OracleHashStable(uint64_t hash,
	const sg_rune_stable_id_t *value)
{
	hash = OracleHashU64(hash, value->source_set_identity);
	hash = OracleHashU64(hash, value->high);
	return OracleHashU64(hash, value->low);
}

static uint64_t OracleTransitionHash(const sg_rune_phase_transition_t *value)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	uint32_t bits;
	float duration;

	hash = OracleHashStable(hash, &value->cell.value);
	hash = OracleHashStable(hash, &value->source_phase.value);
	hash = OracleHashStable(hash, &value->destination_phase.value);
	hash = OracleHashU32(hash, (uint32_t)value->kind);
	duration = value->duration_ms.min_value;
	if (duration == 0.0f) duration = 0.0f;
	memcpy(&bits, &duration, sizeof(bits)); hash = OracleHashU32(hash, bits);
	duration = value->duration_ms.max_value;
	if (duration == 0.0f) duration = 0.0f;
	memcpy(&bits, &duration, sizeof(bits)); hash = OracleHashU32(hash, bits);
	hash = OracleHashU32(hash, value->flags);
	return OracleHashStable(hash, &value->destination_cell.value);
}

static int OracleTransitionEqual(const sg_rune_phase_transition_t *left,
	const sg_rune_phase_transition_t *right)
{
	return SG_RuneModelStableIdEqual(&left->cell.value, &right->cell.value) &&
		SG_RuneModelStableIdEqual(&left->source_phase.value,
			&right->source_phase.value) &&
		SG_RuneModelStableIdEqual(&left->destination_phase.value,
			&right->destination_phase.value) && left->kind == right->kind &&
		left->duration_ms.min_value == right->duration_ms.min_value &&
		left->duration_ms.max_value == right->duration_ms.max_value &&
		left->flags == right->flags && SG_RuneModelStableIdEqual(
			&left->destination_cell.value, &right->destination_cell.value);
}

static int OracleAppendTransition(sg_audit_oracle_t *oracle,
	size_t *capacity_out, const sg_rune_phase_transition_t *transition,
	const sg_phase_catalog_transition_evidence_t *evidence,
	sg_phase_catalog_error_t *error_out)
{
	sg_phase_catalog_transition_pair_t *grown;
	uint32_t *grown_hash;
	size_t capacity;
	uint32_t slot;
	uint32_t index;
	uint32_t hash_capacity;
	uint64_t hash = OracleTransitionHash(transition);

	if (oracle->transition_hash_capacity == 0U)
	{
		oracle->transition_hash_capacity = 32U;
		oracle->transition_hash = calloc(oracle->transition_hash_capacity,
			sizeof(*oracle->transition_hash));
		if (!oracle->transition_hash)
		{
			oracle->transition_hash_capacity = 0U;
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
	}
	slot = (uint32_t)hash & (oracle->transition_hash_capacity - 1U);
	while (oracle->transition_hash[slot] != 0U)
	{
		index = oracle->transition_hash[slot] - 1U;
		if (OracleTransitionEqual(&oracle->transitions[index].transition,
				transition))
			return 1;
		slot = (slot + 1U) & (oracle->transition_hash_capacity - 1U);
	}
	if (oracle->transition_count >= SG_PHASE_CATALOG_TRANSITION_APPEND_LIMIT)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			oracle->transition_count);
		return 0;
	}
	if (oracle->transition_hash_capacity / 2U <= oracle->transition_count)
	{
		if (oracle->transition_hash_capacity > UINT32_MAX / 2U)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
				oracle->transition_count);
			return 0;
		}
		hash_capacity = oracle->transition_hash_capacity * 2U;
		grown_hash = calloc(hash_capacity, sizeof(*grown_hash));
		if (!grown_hash)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		for (index = 0U; index < oracle->transition_count; index++)
		{
			uint64_t existing = OracleTransitionHash(
				&oracle->transitions[index].transition);
			uint32_t existing_slot = (uint32_t)existing & (hash_capacity - 1U);
			while (grown_hash[existing_slot] != 0U)
				existing_slot = (existing_slot + 1U) & (hash_capacity - 1U);
			grown_hash[existing_slot] = index + 1U;
		}
		free(oracle->transition_hash);
		oracle->transition_hash = grown_hash;
		oracle->transition_hash_capacity = hash_capacity;
		slot = (uint32_t)hash & (hash_capacity - 1U);
		while (oracle->transition_hash[slot] != 0U)
			slot = (slot + 1U) & (hash_capacity - 1U);
	}
	if ((size_t)oracle->transition_count == *capacity_out)
	{
		capacity = *capacity_out == 0U ? 16U : *capacity_out;
		while (capacity <= (size_t)oracle->transition_count)
		{
			if (capacity > SIZE_MAX / 2U)
			{
				capacity = SIZE_MAX;
				break;
			}
			capacity *= 2U;
		}
		if (!AllocationFits(capacity, sizeof(*grown)))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
				oracle->transition_count);
			return 0;
		}
		grown = realloc(oracle->transitions, capacity * sizeof(*grown));
		if (!grown)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		oracle->transitions = grown;
		*capacity_out = capacity;
	}
	memset(&oracle->transitions[oracle->transition_count], 0,
		sizeof(oracle->transitions[oracle->transition_count]));
	oracle->transitions[oracle->transition_count].transition = *transition;
	oracle->transitions[oracle->transition_count].evidence = *evidence;
	oracle->transition_hash[slot] = oracle->transition_count + 1U;
	oracle->transition_count++;
	return 1;
}

static int OracleAirbornePhase(const sg_rune_phase_basis_t *phase)
{
	return phase && phase->motion == SG_RUNE_MOTION_AIRBORNE &&
		phase->support == SG_RUNE_SUPPORT_NONE &&
		phase->reference_frame == SG_RUNE_FRAME_WORLD &&
		!SG_RuneModelStableIdValid(&phase->mover.value);
}

static int OracleBuildStanceTransitions(const sg_phase_catalog_source_t *source,
	sg_audit_oracle_t *oracle, size_t *capacity_out,
	sg_phase_catalog_error_t *error_out)
{
	uint32_t overlap_index;

	for (overlap_index = 0U;
		overlap_index < source->configuration->stance_overlap_count;
		overlap_index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&source->configuration->stance_overlaps[overlap_index];
		uint32_t direction;

		for (direction = 0U; direction < 2U; direction++)
		{
			uint32_t source_cell = direction == 0U ? overlap->standing_cell :
				overlap->crouching_cell;
			uint32_t destination_cell = direction == 0U ? overlap->crouching_cell :
				overlap->standing_cell;
			sg_rune_stance_t source_stance = direction == 0U ?
				SG_RUNE_STANCE_STANDING : SG_RUNE_STANCE_CROUCHING;
			sg_rune_stance_t destination_stance = direction == 0U ?
				SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
			uint32_t source_first;
			uint32_t source_last;
			uint32_t phase;

			if (!OracleCellRange(oracle, source_cell, &source_first, &source_last))
				continue;
			for (phase = source_first; phase < source_last; phase++)
			{
				uint32_t destination_phase;
				sg_rune_phase_transition_t transition;
				sg_phase_catalog_transition_evidence_t evidence;
				uint32_t quantum = source->authority->identity.physics.substep_ms;
				uint32_t frame = source->authority->identity.physics.frame_ms;
				uint32_t source_region;
				uint32_t destination_region;

				if (oracle->phases[phase].phase.stance != source_stance ||
					oracle->phases[phase].phase.reference_frame != SG_RUNE_FRAME_WORLD ||
					!OracleFindNeutralPhase(oracle, destination_cell,
						&oracle->phases[phase].phase, destination_stance,
						&destination_phase))
					continue;
				source_region = oracle->phases[phase].region;
				destination_region = oracle->phases[destination_phase].region;
				if (source_region >= source->semantics->region_count ||
					destination_region >= source->semantics->region_count)
				{
					OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
						phase);
					return 0;
				}
				memset(&transition, 0, sizeof(transition));
				memset(&evidence, 0, sizeof(evidence));
				transition.cell = source->configuration->cells[source_cell].id;
				transition.destination_cell =
					source->configuration->cells[destination_cell].id;
				transition.source_phase = oracle->phases[phase].phase.id;
				transition.destination_phase =
					oracle->phases[destination_phase].phase.id;
				transition.kind = SG_RUNE_PHASE_TRANSITION_STANCE;
				transition.flags = SG_RUNE_PHASE_TRANSITION_CROSS_CELL;
				transition.duration_ms = (sg_rune_interval_t){ (float)quantum,
					(float)frame };
				evidence.origin = SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP;
				evidence.source_record = overlap_index;
				evidence.destination_record = SG_PHASE_CATALOG_INDEX_NONE;
				evidence.source_cell = source_cell;
				evidence.destination_cell = destination_cell;
				evidence.source_region_id = source->semantics->regions[
					source_region].id;
				evidence.destination_region_id = source->semantics->regions[
					destination_region].id;
				if (!OracleAppendTransition(oracle, capacity_out, &transition,
					&evidence, error_out))
					return 0;
			}
		}
	}
	return 1;
}

static int OracleBuildPortalTransitions(const sg_phase_catalog_source_t *source,
	sg_audit_oracle_t *oracle, size_t *capacity_out,
	sg_phase_catalog_error_t *error_out)
{
	uint32_t portal_index;

	if (!oracle->phases)
	{
		if (oracle->phase_count != 0U)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		return 1;
	}

	for (portal_index = 0U;
		portal_index < source->configuration->portal_count; portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&source->configuration->portals[portal_index];
		uint32_t direction;

		for (direction = 0U; direction < 2U; direction++)
		{
			uint32_t source_cell = direction == 0U ? portal->from_cell :
				portal->to_cell;
			uint32_t destination_cell = direction == 0U ? portal->to_cell :
				portal->from_cell;
			uint32_t source_first;
			uint32_t source_last;
			uint32_t destination_first;
			uint32_t destination_last;
			uint32_t phase;

			if (!OracleCellRange(oracle, source_cell, &source_first, &source_last) ||
				!OracleCellRange(oracle, destination_cell, &destination_first,
					&destination_last))
				continue;
			for (phase = source_first; phase < source_last; phase++)
			{
				uint32_t destination_phase;

				for (destination_phase = destination_first;
					destination_phase < destination_last; destination_phase++)
				{
					const sg_rune_phase_basis_t *source_basis =
						&oracle->phases[phase].phase;
					const sg_rune_phase_basis_t *destination_basis =
						&oracle->phases[destination_phase].phase;
					sg_phase_mechanism_state_mask_t source_states = 0U;
					sg_phase_mechanism_state_mask_t destination_states = 0U;
					sg_rune_phase_transition_t transition;
					sg_phase_catalog_transition_evidence_t evidence;
					uint32_t source_region = oracle->phases[phase].region;
					uint32_t destination_region =
						oracle->phases[destination_phase].region;
					uint32_t binding;
					int legal;

					if (source_basis->stance != portal->stance ||
						destination_basis->stance != portal->stance ||
						source_region >= source->semantics->region_count ||
						destination_region >= source->semantics->region_count)
						continue;
					legal = source_basis->reference_frame == SG_RUNE_FRAME_WORLD &&
						destination_basis->reference_frame == SG_RUNE_FRAME_WORLD;
					if (!legal && source_basis->reference_frame ==
							SG_RUNE_FRAME_MOVER_RELATIVE &&
						destination_basis->reference_frame ==
							SG_RUNE_FRAME_MOVER_RELATIVE &&
						SG_RuneModelStableIdEqual(&source_basis->mover.value,
							&destination_basis->mover.value))
					{
						for (binding = 0U; binding < oracle->binding_count;
							binding++)
						{
							const sg_phase_catalog_binding_t *record =
								&oracle->bindings[binding];

							if (SG_RuneModelStableIdEqual(&record->phase.value,
								&source_basis->id.value))
								source_states |= record->mechanism_state_mask;
							if (SG_RuneModelStableIdEqual(&record->phase.value,
								&destination_basis->id.value))
								destination_states |= record->mechanism_state_mask;
						}
						legal = (source_states & destination_states) != 0U;
					}
					if (!legal)
						continue;
					memset(&transition, 0, sizeof(transition));
					memset(&evidence, 0, sizeof(evidence));
					transition.cell = source->configuration->cells[source_cell].id;
					transition.destination_cell =
						source->configuration->cells[destination_cell].id;
					transition.source_phase = source_basis->id;
					transition.destination_phase = destination_basis->id;
					transition.kind = SG_RUNE_PHASE_TRANSITION_PORTAL;
					transition.flags = SG_RUNE_PHASE_TRANSITION_CROSS_CELL;
					transition.duration_ms =
						(sg_rune_interval_t){ 0.0f, 0.0f };
					evidence.origin = SG_PHASE_CATALOG_TRANSITION_PORTAL;
					evidence.source_record = portal_index;
					evidence.destination_record = destination_cell;
					evidence.source_cell = source_cell;
					evidence.destination_cell = destination_cell;
					evidence.source_region_id = source->semantics->regions[
						source_region].id;
					evidence.destination_region_id = source->semantics->regions[
						destination_region].id;
					evidence.portal = portal->id;
					evidence.source_state_mask = source_states;
					evidence.destination_state_mask = destination_states;
					evidence.portal_duration_ms = 0U;
					if (!OracleAppendTransition(oracle, capacity_out, &transition,
						&evidence, error_out))
						return 0;
				}
			}
		}
	}
	return 1;
}

static int OracleBuildSupportTransitions(const sg_phase_catalog_source_t *source,
	sg_audit_oracle_t *oracle, size_t *capacity_out,
	sg_phase_catalog_error_t *error_out)
{
	uint32_t phase;

	for (phase = 0U; phase < oracle->phase_count; phase++)
	{
		sg_rune_phase_basis_t candidate;
		uint32_t supported_phase;
		uint32_t cell = oracle->phases[phase].phase.order.source_index;
		uint32_t source_region = oracle->phases[phase].region;
		uint32_t destination_region;
		sg_rune_phase_transition_t transition;
		sg_phase_catalog_transition_evidence_t evidence;
		uint32_t frame = source->authority->identity.physics.frame_ms;
		uint32_t quantum = source->authority->identity.physics.substep_ms;

		if (!OracleAirbornePhase(&oracle->phases[phase].phase))
			continue;
		candidate = oracle->phases[phase].phase;
		candidate.motion = SG_RUNE_MOTION_SUPPORTED;
		candidate.support = SG_RUNE_SUPPORT_SUPPORTED;
		{
			uint32_t first;
			uint32_t last;

			if (!OracleCellRange(oracle, cell, &first, &last))
				continue;
			for (supported_phase = first; supported_phase < last;
				supported_phase++)
			{
				if (!OraclePhaseEquivalent(
					&oracle->phases[supported_phase].phase, &candidate))
					continue;
				destination_region = oracle->phases[supported_phase].region;
				if (phase == supported_phase ||
					source_region >= source->semantics->region_count ||
					destination_region >= source->semantics->region_count)
					continue;
				memset(&transition, 0, sizeof(transition));
				memset(&evidence, 0, sizeof(evidence));
				transition.cell = source->configuration->cells[cell].id;
				transition.destination_cell = source->configuration->cells[cell].id;
				transition.source_phase = oracle->phases[phase].phase.id;
				transition.destination_phase =
					oracle->phases[supported_phase].phase.id;
				transition.kind = SG_RUNE_PHASE_TRANSITION_SUPPORT;
				transition.duration_ms = (sg_rune_interval_t){ (float)quantum,
					(float)frame };
				evidence.origin = SG_PHASE_CATALOG_TRANSITION_SUPPORT_CHANGE;
				evidence.source_record = source_region;
				evidence.destination_record = destination_region;
				evidence.source_cell = cell;
				evidence.destination_cell = cell;
				evidence.source_region_id =
					source->semantics->regions[source_region].id;
				evidence.destination_region_id =
					source->semantics->regions[destination_region].id;
				if (!OracleAppendTransition(oracle, capacity_out, &transition,
					&evidence, error_out))
					return 0;
			}
		}
	}
	return 1;
}

static uint32_t OracleStateBit(sg_mechanism_state_t state)
{
	if (state < SG_MECHANISM_STATE_INACTIVE ||
		state >= SG_MECHANISM_STATE_COUNT)
		return 0U;
	return UINT32_C(1) << (uint32_t)state;
}

static int OracleBuildMechanismTransitions(
	const sg_phase_catalog_source_t *source, sg_audit_oracle_t *oracle,
	size_t *capacity_out, sg_phase_catalog_error_t *error_out)
{
	uint32_t fact_index;

	for (fact_index = 0U;
		fact_index < SG_PHASE_SOURCE_PROVIDER(source)->fact_count; fact_index++)
	{
		const sg_mechanism_capability_fact_t *fact =
			&SG_PHASE_SOURCE_PROVIDER(source)->facts[fact_index];
		const sg_configuration_semantic_region_t *source_region;
		const sg_configuration_semantic_region_t *destination_region;
		sg_rune_phase_basis_t source_candidate;
		sg_rune_phase_basis_t destination_candidate;
		uint32_t source_phase;
		uint32_t destination_phase;
		uint32_t elapsed;
		uint32_t frame = source->authority->identity.physics.frame_ms;
		uint32_t source_cell;
		uint32_t destination_cell;
		sg_rune_phase_transition_t transition;
		sg_phase_catalog_transition_evidence_t evidence;

		if (fact->source_region >= source->semantics->region_count ||
			fact->destination_region >= source->semantics->region_count)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				fact_index);
			return 0;
		}
		source_region = &source->semantics->regions[fact->source_region];
		destination_region = &source->semantics->regions[fact->destination_region];
		source_cell = source_region->cell;
		destination_cell = destination_region->cell;
		OracleFillPhase(source, source_region, 1, &fact->mechanism_id, 1U,
			&source_candidate);
		OracleFillPhase(source, destination_region, 1, &fact->mechanism_id, 1U,
			&destination_candidate);
		if (!OracleFindPhaseForRegion(oracle, source_cell, fact->source_region,
			&source_candidate, &source_phase) ||
			!OracleFindPhaseForRegion(oracle, destination_cell,
				fact->destination_region, &destination_candidate,
				&destination_phase))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				fact_index);
			return 0;
		}
		elapsed = OracleTimingSpan(fact);
		if (elapsed > frame)
			elapsed = frame;
		if (elapsed == 0U)
			elapsed = source->authority->identity.physics.substep_ms;
		destination_candidate.order.variant = 3U;
		destination_candidate.elapsed_ms.min_value = (float)elapsed;
		destination_candidate.elapsed_ms.max_value = (float)frame;
		if (!OracleFindPhaseForRegion(oracle, destination_cell,
			fact->destination_region, &destination_candidate,
			&destination_phase))
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				fact_index);
			return 0;
		}
		if (source_phase == destination_phase)
		{
			OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				fact_index);
			return 0;
		}
		memset(&transition, 0, sizeof(transition));
		memset(&evidence, 0, sizeof(evidence));
		transition.cell = source->configuration->cells[source_cell].id;
		transition.destination_cell =
			source->configuration->cells[destination_cell].id;
		transition.source_phase = oracle->phases[source_phase].phase.id;
		transition.destination_phase = oracle->phases[destination_phase].phase.id;
		transition.flags = source_cell == destination_cell ? 0U :
			SG_RUNE_PHASE_TRANSITION_CROSS_CELL;
		transition.kind = fact->kind == SG_MECHANISM_CAPABILITY_DWELL ?
			SG_RUNE_PHASE_TRANSITION_MOVER_DWELL : SG_RUNE_PHASE_TRANSITION_TIME;
		transition.duration_ms = (sg_rune_interval_t){
			(float)OracleTimingSpan(fact), (float)OracleTimingSpan(fact) };
		evidence.origin = SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING;
		evidence.source_record = fact_index;
		evidence.destination_record = SG_PHASE_CATALOG_INDEX_NONE;
		evidence.source_cell = source_cell;
		evidence.destination_cell = destination_cell;
		evidence.source_region_id = source_region->id;
		evidence.destination_region_id = destination_region->id;
		evidence.mechanism = fact->mechanism_id;
		evidence.source_state_mask = (sg_phase_mechanism_state_mask_t)
			OracleStateBit(fact->source_state);
		evidence.destination_state_mask = (sg_phase_mechanism_state_mask_t)
			OracleStateBit(fact->destination_state);
		evidence.provider_verifier_identity =
			SG_PHASE_SOURCE_PROVIDER(source)->verifier_identity;
		evidence.delay_ms = fact->delay_ms;
		evidence.dwell_ms = fact->dwell_ms;
		evidence.travel_ms = fact->travel_ms;
		evidence.wait_ms = fact->wait_ms;
		evidence.reset_ms = fact->reset_ms;
		evidence.activation_time_ms = fact->activation_time_ms;
		evidence.active_time_ms = fact->active_time_ms;
		evidence.exit_time_ms = fact->exit_time_ms;
		evidence.reset_time_ms = fact->reset_time_ms;
		if (!OracleAppendTransition(oracle, capacity_out, &transition, &evidence,
			error_out))
			return 0;
	}
	return 1;
}

static void OracleDestroy(sg_audit_oracle_t *oracle)
{
	if (!oracle)
		return;
	free(oracle->phases);
	free(oracle->bindings);
	free(oracle->transitions);
	free(oracle->transition_hash);
	memset(oracle, 0, sizeof(*oracle));
}

static int OracleBuild(const sg_phase_catalog_source_t *source,
	sg_audit_oracle_t *oracle, sg_phase_catalog_error_t *error_out)
{
	size_t transition_capacity = 0U;
	uint32_t index;

	memset(oracle, 0, sizeof(*oracle));
	oracle->completion = source->configuration->cell_count == 0U ?
		SG_PHASE_CATALOG_PROVEN_EMPTY : SG_PHASE_CATALOG_COMPLETE;
	oracle->mover_support_verifier_identity =
		SG_PHASE_SOURCE_PROVIDER(source)->verifier_identity;
	if (!OracleBuildPhases(source, oracle, error_out) ||
		!OracleBuildBindings(source, oracle, error_out) ||
		!OracleBuildSupportTransitions(source, oracle, &transition_capacity,
			error_out) ||
		!OracleBuildStanceTransitions(source, oracle, &transition_capacity,
			error_out) ||
		!OracleBuildPortalTransitions(source, oracle, &transition_capacity,
			error_out) ||
		!OracleBuildMechanismTransitions(source, oracle, &transition_capacity,
			error_out))
	{
		OracleDestroy(oracle);
		return 0;
	}
	if (oracle->transition_count != 0U)
	{
		uint32_t output = 1U;

		qsort(oracle->transitions, oracle->transition_count,
			sizeof(*oracle->transitions), OracleTransitionPairCompare);
		for (index = 1U; index < oracle->transition_count; index++)
			if (OracleTransitionPairCompare(&oracle->transitions[index - 1U],
				&oracle->transitions[index]) != 0)
				oracle->transitions[output++] = oracle->transitions[index];
		oracle->transition_count = output;
	}
	if (oracle->transition_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS)
	{
		OracleSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			oracle->transition_count);
		OracleDestroy(oracle);
		return 0;
	}
	oracle->transition_completion = oracle->transition_count == 0U ?
		SG_PHASE_CATALOG_PROVEN_EMPTY : SG_PHASE_CATALOG_COMPLETE;
	return 1;
}

static int PhaseExceptStanceEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value) &&
		OracleInterval3Equal(&left->velocity, &right->velocity) &&
		OracleIntervalEqual(&left->elapsed_ms, &right->elapsed_ms) &&
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

static int BindingExists(const sg_phase_catalog_expected_t *expected,
	uint64_t region_id, uint32_t cell, const sg_rune_phase_ref_t *phase,
	sg_phase_mechanism_state_mask_t required_state_mask)
{
	uint32_t index;

	if (!expected || !phase)
		return 0;
	for (index = 0U; index < expected->binding_count; index++)
	{
		const sg_phase_catalog_binding_t *binding = &expected->bindings[index];

		if (binding->semantic_region_id == region_id &&
			binding->configuration_cell == cell &&
			SG_RuneModelStableIdEqual(&binding->phase.value, &phase->value) &&
			(binding->mechanism_state_mask & required_state_mask) ==
				required_state_mask)
			return 1;
	}
	return 0;
}

static int TransitionValid(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_expected_t *expected,
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
		!SG_RuneModelStableIdValid(&transition->destination_cell.value) ||
		!IntervalValid(&transition->duration_ms) ||
		(evidence->origin != SG_PHASE_CATALOG_TRANSITION_PORTAL &&
			transition->duration_ms.max_value <= 0.0f) ||
		(transition->flags & ~(uint32_t)SG_RUNE_PHASE_TRANSITION_FLAGS_KNOWN) !=
			0U ||
		evidence->origin < SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP ||
		evidence->origin >= SG_PHASE_CATALOG_TRANSITION_ORIGIN_COUNT ||
		evidence->source_cell >= configuration->cell_count ||
		evidence->destination_cell >= configuration->cell_count ||
		!SG_RuneModelStableIdEqual(&transition->cell.value,
			&configuration->cells[evidence->source_cell].id.value) ||
		!SG_RuneModelStableIdEqual(&transition->destination_cell.value,
			&configuration->cells[evidence->destination_cell].id.value))
		return 0;
	expected_id = SG_RuneModelStableIdFromOrderKey(&transition->order);
	if (!SG_RuneModelStableIdEqual(&transition->id.value, &expected_id))
		return 0;
	source_phase = PhaseIndexById(expected, &transition->source_phase);
	destination_phase = PhaseIndexById(expected, &transition->destination_phase);
	if (source_phase < 0 || destination_phase < 0 || source_phase == destination_phase ||
		expected->phases[source_phase].order.source_index != evidence->source_cell ||
		expected->phases[destination_phase].order.source_index !=
			evidence->destination_cell ||
		((evidence->source_cell != evidence->destination_cell) !=
			((transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) != 0U)) ||
		!BindingExists(expected, evidence->source_region_id,
			evidence->source_cell, &transition->source_phase, 0U) ||
		!BindingExists(expected, evidence->destination_region_id,
			evidence->destination_cell, &transition->destination_phase, 0U))
		return 0;
	switch (evidence->origin)
	{
	case SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP:
	{
		int source_region = RegionIndexById(semantics,
			evidence->source_region_id);
		int destination_region = RegionIndexById(semantics,
			evidence->destination_region_id);

		{
			const sg_configuration_stance_overlap_t *overlap;
			int forward;
			int reverse;

			if (transition->kind != SG_RUNE_PHASE_TRANSITION_STANCE ||
				(transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) == 0U ||
				evidence->source_record >= configuration->stance_overlap_count ||
				evidence->destination_record != SG_PHASE_CATALOG_INDEX_NONE ||
				source_region < 0 || destination_region < 0 ||
				semantics->regions[source_region].cell != evidence->source_cell ||
				semantics->regions[destination_region].cell !=
					evidence->destination_cell)
				return 0;
			overlap = &configuration->stance_overlaps[evidence->source_record];
			forward = evidence->source_cell == overlap->standing_cell &&
				evidence->destination_cell == overlap->crouching_cell;
			reverse = evidence->source_cell == overlap->crouching_cell &&
				evidence->destination_cell == overlap->standing_cell;
			return (forward || reverse) &&
				((forward && expected->phases[source_phase].stance ==
					SG_RUNE_STANCE_STANDING &&
					expected->phases[destination_phase].stance ==
					SG_RUNE_STANCE_CROUCHING) ||
				 (reverse && expected->phases[source_phase].stance ==
					SG_RUNE_STANCE_CROUCHING &&
					expected->phases[destination_phase].stance ==
					SG_RUNE_STANCE_STANDING)) &&
				PhaseExceptStanceEqual(&expected->phases[source_phase],
					&expected->phases[destination_phase]);
		}
	}
	case SG_PHASE_CATALOG_TRANSITION_PORTAL:
	{
		const sg_configuration_portal_t *portal;
		const sg_rune_phase_basis_t *source_basis =
			&expected->phases[source_phase];
		const sg_rune_phase_basis_t *destination_basis =
			&expected->phases[destination_phase];
		int world_pair;
		int mover_pair;

		if (transition->kind != SG_RUNE_PHASE_TRANSITION_PORTAL ||
			(transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) == 0U ||
			evidence->source_record >= configuration->portal_count ||
			evidence->destination_record != evidence->destination_cell ||
			evidence->provider_verifier_identity != 0U ||
			evidence->portal_duration_ms != 0U ||
			transition->duration_ms.min_value != 0.0f ||
			transition->duration_ms.max_value != 0.0f)
			return 0;
		portal = &configuration->portals[evidence->source_record];
		world_pair = source_basis->reference_frame == SG_RUNE_FRAME_WORLD &&
			destination_basis->reference_frame == SG_RUNE_FRAME_WORLD;
		mover_pair = source_basis->reference_frame ==
				SG_RUNE_FRAME_MOVER_RELATIVE &&
			destination_basis->reference_frame ==
				SG_RUNE_FRAME_MOVER_RELATIVE &&
			SG_RuneModelStableIdEqual(&source_basis->mover.value,
				&destination_basis->mover.value);
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
				evidence->destination_region_id)].cell == evidence->destination_cell &&
			source_basis->stance == portal->stance &&
			destination_basis->stance == portal->stance &&
			(world_pair || mover_pair) &&
			(world_pair ? (evidence->source_state_mask == 0U &&
				evidence->destination_state_mask == 0U) :
				(mover_pair && evidence->source_state_mask != 0U &&
				evidence->destination_state_mask != 0U &&
				(evidence->source_state_mask &
					evidence->destination_state_mask) != 0U &&
				BindingExists(expected, evidence->source_region_id,
					evidence->source_cell, &transition->source_phase,
					evidence->source_state_mask) &&
				BindingExists(expected, evidence->destination_region_id,
					evidence->destination_cell, &transition->destination_phase,
					evidence->destination_state_mask)));
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

		if (evidence->source_record >= SG_PHASE_SOURCE_PROVIDER(source)->fact_count ||
			evidence->destination_record != SG_PHASE_CATALOG_INDEX_NONE ||
			(evidence->source_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			(evidence->destination_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			evidence->provider_verifier_identity !=
				SG_PHASE_SOURCE_PROVIDER(source)->verifier_identity)
			return 0;
		fact = &SG_PHASE_SOURCE_PROVIDER(source)->facts[evidence->source_record];
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
			((evidence->source_cell != evidence->destination_cell) ==
			 ((transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) != 0U)) &&
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
			BindingExists(expected, evidence->source_region_id,
				evidence->source_cell, &transition->source_phase,
				(sg_phase_mechanism_state_mask_t)source_mask) &&
			BindingExists(expected, evidence->destination_region_id,
				evidence->destination_cell, &transition->destination_phase,
				(sg_phase_mechanism_state_mask_t)destination_mask) &&
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
	return 0;
}

int SG_PhaseCatalogAudit(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *result_out)
{
	sg_audit_oracle_t oracle;
	sg_phase_catalog_expected_t phase_view;
	sg_phase_catalog_error_t source_error;
	sg_rune_phase_basis_t *oracle_phase_records = NULL;
	sg_phase_catalog_transition_pair_t *catalog_pairs = NULL;
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
	memset(&oracle, 0, sizeof(oracle));
	memset(&phase_view, 0, sizeof(phase_view));
	if (!SG_PhaseCatalogSourceValidate(source, &source_error) ||
		!OracleBuild(source, &oracle, &source_error))
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVALID_SOURCE,
			source_error.source_index);
		return 0;
	}
	if (!SG_PhaseCatalogIdentityEqual(&catalog->identity,
		&source->authority->identity) ||
		catalog->mover_support_verifier_identity !=
			oracle.mover_support_verifier_identity)
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_SOURCE_MISMATCH, 0U);
		goto failure;
	}
	if (catalog->completion != oracle.completion ||
		catalog->transition_completion != oracle.transition_completion)
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_COMPLETION_DISAGREEMENT, 0U);
		goto failure;
	}
	if (catalog->phase_count < oracle.phase_count)
	{
		result_out->omitted_phases = oracle.phase_count - catalog->phase_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE,
			catalog->phase_count);
		goto failure;
	}
	if (catalog->phase_count > oracle.phase_count)
	{
		result_out->invented_phases = catalog->phase_count - oracle.phase_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE,
			oracle.phase_count);
		goto failure;
	}
	if (oracle.phase_count != 0U)
	{
		if (!AllocationFits((size_t)oracle.phase_count,
			sizeof(*oracle_phase_records)))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
			goto failure;
		}
		oracle_phase_records = malloc((size_t)oracle.phase_count *
			sizeof(*oracle_phase_records));
		if (!oracle_phase_records)
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
			goto failure;
		}
		for (index = 0U; index < oracle.phase_count; index++)
			oracle_phase_records[index] = oracle.phases[index].phase;
	}
	phase_view.phases = oracle_phase_records;
	phase_view.phase_count = oracle.phase_count;
	phase_view.bindings = oracle.bindings;
	phase_view.binding_count = oracle.binding_count;
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
			&oracle.phases[index].phase))
		{
			int expected_index = PhaseIndexById(&phase_view,
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
	if (catalog->binding_count < oracle.binding_count)
	{
		result_out->omitted_bindings = oracle.binding_count -
			catalog->binding_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_BINDING,
			catalog->binding_count);
		goto failure;
	}
	if (catalog->binding_count > oracle.binding_count)
	{
		result_out->invented_bindings = catalog->binding_count - oracle.binding_count;
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_BINDING,
			oracle.binding_count);
		goto failure;
	}
	duplicate = DuplicateBinding(catalog, result_out);
	if (duplicate != 0)
		goto failure;
	for (index = 0U; index < catalog->binding_count; index++)
	{
		if (PhaseIndexById(&phase_view, &catalog->bindings[index].phase) < 0)
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING,
				index);
			goto failure;
		}
		if (!SG_PhaseCatalogBindingEqual(&catalog->bindings[index],
			&oracle.bindings[index]))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT,
				index);
			goto failure;
		}
	}
	if (catalog->transition_count < oracle.transition_count)
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_OMITTED_TRANSITION,
			catalog->transition_count);
		goto failure;
	}
	if (catalog->transition_count > oracle.transition_count)
	{
		SetReport(result_out, SG_PHASE_CATALOG_AUDIT_INVENTED_TRANSITION,
			oracle.transition_count);
		goto failure;
	}
	duplicate = DuplicateTransition(catalog, result_out);
	if (duplicate != 0)
		goto failure;
	if (catalog->transition_count != 0U)
	{
		if (!AllocationFits((size_t)catalog->transition_count,
			sizeof(*catalog_pairs)))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
			goto failure;
		}
		catalog_pairs = calloc((size_t)catalog->transition_count,
			sizeof(*catalog_pairs));
		if (!catalog_pairs)
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT, 0U);
			goto failure;
		}
		for (index = 0U; index < catalog->transition_count; index++)
		{
			catalog_pairs[index].transition = catalog->transitions[index];
			catalog_pairs[index].evidence = catalog->transition_evidence[index];
			/* The source-accounting oracle compares the transition meaning;
			 * order/id are checked independently below. */
			memset(&catalog_pairs[index].transition.id, 0,
				sizeof(catalog_pairs[index].transition.id));
			memset(&catalog_pairs[index].transition.order, 0,
				sizeof(catalog_pairs[index].transition.order));
		}
		qsort(catalog_pairs, catalog->transition_count,
			sizeof(*catalog_pairs), OracleTransitionPairCompare);
	}
	for (index = 0U; index < catalog->transition_count; index++)
	{
		if (catalog->transitions[index].order.source_index != index ||
			!TransitionValid(source, &phase_view,
				&catalog->transitions[index], &catalog->transition_evidence[index]))
		{
			SetReport(result_out, SG_PHASE_CATALOG_AUDIT_TRANSITION_DISAGREEMENT,
				index);
			goto failure;
		}
		if (OracleTransitionPairCompare(&catalog_pairs[index],
				&oracle.transitions[index]) != 0)
		{
			SetReport(result_out,
				SG_PHASE_CATALOG_AUDIT_TRANSITION_DISAGREEMENT, index);
			goto failure;
		}
	}
	result_out->code = oracle.completion == SG_PHASE_CATALOG_PROVEN_EMPTY ?
		SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY : SG_PHASE_CATALOG_AUDIT_OK_COMPLETE;
	result_out->proved_phases = oracle.phase_count;
	result_out->proved_bindings = oracle.binding_count;
	free(catalog_pairs);
	free(oracle_phase_records);
	OracleDestroy(&oracle);
	return 1;

failure:
	free(catalog_pairs);
	free(oracle_phase_records);
	OracleDestroy(&oracle);
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
