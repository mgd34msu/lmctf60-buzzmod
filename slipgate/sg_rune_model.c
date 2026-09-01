#include "sg_rune_model.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SG_RUNE_MODEL_KNOWN_FLAGS \
	(SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND | \
	 SG_RUNE_MODEL_NO_RUNTIME_ACTORS)
#define SG_RUNE_CELL_SEMANTICS_KNOWN \
	(SG_RUNE_CELL_SEMANTIC_HAZARD | SG_RUNE_CELL_SEMANTIC_SKY_BOUNDARY | \
	 SG_RUNE_CELL_SEMANTIC_VOID_BOUNDARY | SG_RUNE_CELL_SEMANTIC_MOVER_VOLUME)
#define SG_RUNE_SURFACE_SEMANTICS_KNOWN \
	(SG_RUNE_SURFACE_SEMANTIC_HOOKABLE | SG_RUNE_SURFACE_SEMANTIC_SKY | \
	 SG_RUNE_SURFACE_SEMANTIC_COVER_BOUNDARY | \
	 SG_RUNE_SURFACE_SEMANTIC_EXPOSURE_BOUNDARY | \
	 SG_RUNE_SURFACE_SEMANTIC_BOUNCE)
#define SG_RUNE_PORTAL_FLAGS_KNOWN \
	(SG_RUNE_PORTAL_HULL_VALID | SG_RUNE_PORTAL_CONTENTS_CHANGE | \
	 SG_RUNE_PORTAL_VOID_EDGE | SG_RUNE_PORTAL_MOVER_BOUNDARY)
#define SG_RUNE_KERNEL_FLAGS_KNOWN \
	(SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE | \
	 SG_RUNE_KERNEL_CHANGES_MEDIUM | SG_RUNE_KERNEL_REQUIRES_SUPPORT | \
	 SG_RUNE_KERNEL_PROVEN)

static _Thread_local uint64_t sg_rune_model_lookup_comparisons;

static int FiniteValue(float value)
{
	return isfinite(value) != 0;
}

static int FiniteVector(const sg_rune_vec3_t *vector)
{
	return vector && FiniteValue(vector->value[0]) &&
		FiniteValue(vector->value[1]) && FiniteValue(vector->value[2]);
}

static int IntervalValid(const sg_rune_interval_t *interval,
	int nonnegative)
{
	return interval && FiniteValue(interval->min_value) &&
		FiniteValue(interval->max_value) &&
		interval->min_value <= interval->max_value &&
		(!nonnegative || interval->min_value >= 0.0f);
}

static int Interval3Valid(const sg_rune_interval3_t *interval,
	int nonnegative)
{
	return interval && IntervalValid(&interval->x, nonnegative) &&
		IntervalValid(&interval->y, nonnegative) &&
		IntervalValid(&interval->z, nonnegative);
}

static int IntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return left && right && left->min_value == right->min_value &&
		left->max_value == right->max_value;
}

static int Interval3Equal(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return left && right && IntervalEqual(&left->x, &right->x) &&
		IntervalEqual(&left->y, &right->y) &&
		IntervalEqual(&left->z, &right->z);
}

static int BoundsValid(const sg_rune_bounds_t *bounds)
{
	int axis;

	if (!bounds || !FiniteVector(&bounds->mins) ||
		!FiniteVector(&bounds->maxs))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PointInsideBounds(const sg_rune_vec3_t *point,
	const sg_rune_bounds_t *bounds)
{
	int axis;

	if (!FiniteVector(point) || !BoundsValid(bounds))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	int axis;

	if (!hull || !FiniteVector(&hull->mins) || !FiniteVector(&hull->maxs))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int ContentsValid(sg_rune_contents_mask_t contents)
{
	return (contents & ~(sg_rune_contents_mask_t)SG_RUNE_CONTENTS_KNOWN) == 0;
}

static int MediumUsesWaterPhysics(sg_rune_medium_t medium)
{
	return medium == SG_RUNE_MEDIUM_WATER || medium == SG_RUNE_MEDIUM_LAVA ||
		medium == SG_RUNE_MEDIUM_SLIME;
}

static int GeometryRefValid(const sg_rune_source_geometry_ref_t *reference,
	uint64_t source_set_identity)
{
	return reference && source_set_identity != 0 &&
		reference->source_set_identity == source_set_identity &&
		reference->source_index != UINT32_MAX &&
		reference->source_ordinal != UINT32_MAX;
}

static int EntityRefValid(const sg_rune_entity_ref_t *reference)
{
	int missing_index;
	int missing_ordinal;

	if (!reference)
		return 0;
	missing_index = reference->index == UINT32_MAX;
	missing_ordinal = reference->spawn_ordinal == UINT32_MAX;
	return missing_index == missing_ordinal;
}

static int StableIdNone(const sg_rune_stable_id_t *id)
{
	return id && id->source_set_identity == UINT64_MAX &&
		id->high == UINT64_MAX && id->low == UINT64_MAX;
}

static int StableIdHasDomain(const sg_rune_stable_id_t *id, uint32_t domain)
{
	return SG_RuneModelStableIdValid(id) &&
		(uint32_t)(id->high >> 32) == domain;
}

int SG_RuneModelStableIdValid(const sg_rune_stable_id_t *id)
{
	uint32_t domain;
	uint32_t source_index;
	uint32_t local_ordinal;
	uint32_t variant;

	if (!id || id->source_set_identity == 0 ||
		id->source_set_identity == UINT64_MAX)
		return 0;
	domain = (uint32_t)(id->high >> 32);
	source_index = (uint32_t)id->high;
	local_ordinal = (uint32_t)(id->low >> 32);
	variant = (uint32_t)id->low;
	return domain >= SG_RUNE_ORDER_CELL &&
		domain < SG_RUNE_ORDER_DOMAIN_COUNT && source_index != UINT32_MAX &&
		local_ordinal != UINT32_MAX && variant != UINT32_MAX;
}

int SG_RuneModelStableIdEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return left && right &&
		left->source_set_identity == right->source_set_identity &&
		left->high == right->high && left->low == right->low;
}

int SG_RuneModelOrderKeyValid(const sg_rune_order_key_t *key)
{
	return key && key->source_set_identity != 0 &&
		key->source_set_identity != UINT64_MAX &&
		key->domain >= SG_RUNE_ORDER_CELL &&
		key->domain < SG_RUNE_ORDER_DOMAIN_COUNT &&
		key->source_index != UINT32_MAX &&
		key->local_ordinal != UINT32_MAX && key->variant != UINT32_MAX;
}

int SG_RuneModelOrderKeyCompare(const sg_rune_order_key_t *left,
	const sg_rune_order_key_t *right)
{
	if (!left || !right)
		return left ? 1 : right ? -1 : 0;
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->domain != right->domain)
		return left->domain < right->domain ? -1 : 1;
	if (left->source_index != right->source_index)
		return left->source_index < right->source_index ? -1 : 1;
	if (left->local_ordinal != right->local_ordinal)
		return left->local_ordinal < right->local_ordinal ? -1 : 1;
	if (left->variant != right->variant)
		return left->variant < right->variant ? -1 : 1;
	return 0;
}

sg_rune_stable_id_t SG_RuneModelStableIdFromOrderKey(
	const sg_rune_order_key_t *key)
{
	sg_rune_stable_id_t id = { 0, 0, 0 };

	if (!SG_RuneModelOrderKeyValid(key))
		return id;
	id.source_set_identity = key->source_set_identity;
	id.high = ((uint64_t)key->domain << 32) | key->source_index;
	id.low = ((uint64_t)key->local_ordinal << 32) | key->variant;
	return id;
}

int SG_RuneModelStableIdToOrderKey(const sg_rune_stable_id_t *id,
	sg_rune_order_key_t *key_out)
{
	sg_rune_order_key_t key;

	if (!SG_RuneModelStableIdValid(id) || !key_out)
		return 0;
	key.source_set_identity = id->source_set_identity;
	key.domain = (uint32_t)(id->high >> 32);
	key.source_index = (uint32_t)id->high;
	key.local_ordinal = (uint32_t)(id->low >> 32);
	key.variant = (uint32_t)id->low;
	if (!SG_RuneModelOrderKeyValid(&key))
		return 0;
	*key_out = key;
	return 1;
}

sg_rune_order_derivation_status_t SG_RuneModelOrderKeyDerive(
	const sg_rune_canonical_order_input_t *input,
	sg_rune_order_key_t *key_out)
{
	if (!input || !key_out || input->domain < SG_RUNE_ORDER_CELL ||
		input->domain >= SG_RUNE_ORDER_DOMAIN_COUNT ||
		input->source_index == UINT32_MAX ||
		input->canonical_ordinal == UINT32_MAX || input->variant == UINT32_MAX ||
		input->source_set_complete > 1)
		return SG_RUNE_ORDER_DERIVATION_INVALID;
	if (input->source_set_complete == 0 || input->source_set_identity == 0 ||
		input->source_set_identity == UINT64_MAX || input->source_set_count == 0 ||
		input->canonical_ordinal >= input->source_set_count)
		return SG_RUNE_ORDER_DERIVATION_INCOMPLETE;
	key_out->source_set_identity = input->source_set_identity;
	key_out->domain = input->domain;
	key_out->source_index = input->source_index;
	key_out->local_ordinal = input->canonical_ordinal;
	key_out->variant = input->variant;
	return SG_RUNE_ORDER_DERIVATION_OK;
}

int SG_RuneModelCompletenessValid(
	const sg_rune_completeness_t *completeness)
{
	if (!completeness || completeness->state < 0 ||
		completeness->state >= SG_RUNE_COMPLETENESS_STATE_COUNT ||
		completeness->reason < 0 ||
		completeness->reason >= SG_RUNE_FAILURE_REASON_COUNT ||
		completeness->expected_cells > SG_RUNE_MODEL_MAX_CELLS ||
		completeness->expected_portals > SG_RUNE_MODEL_MAX_PORTALS ||
		completeness->covered_cells > completeness->expected_cells ||
		completeness->covered_portals > completeness->expected_portals)
		return 0;
	switch (completeness->state) {
	case SG_RUNE_COMPLETENESS_UNSEALED:
	case SG_RUNE_COMPLETENESS_BUILDING:
		return completeness->reason == SG_RUNE_FAILURE_NONE &&
			completeness->failure_record == UINT32_MAX;
	case SG_RUNE_COMPLETENESS_COMPLETE:
		return completeness->reason == SG_RUNE_FAILURE_NONE &&
			completeness->failure_record == UINT32_MAX &&
			completeness->expected_cells > 0 &&
			completeness->covered_cells == completeness->expected_cells &&
			completeness->covered_portals == completeness->expected_portals;
	case SG_RUNE_COMPLETENESS_FAILED:
		return completeness->reason != SG_RUNE_FAILURE_NONE &&
			completeness->failure_record != UINT32_MAX;
	case SG_RUNE_COMPLETENESS_STATE_COUNT:
		break;
	}
	return 0;
}

int SG_RuneModelPhaseValid(const sg_rune_phase_basis_t *phase)
{
	sg_rune_stable_id_t expected;

	if (!phase)
		return 0;
	expected = SG_RuneModelStableIdFromOrderKey(&phase->order);
	if (!SG_RuneModelStableIdValid(&phase->id.value) ||
		!SG_RuneModelOrderKeyValid(&phase->order) ||
		phase->order.domain != SG_RUNE_ORDER_PHASE ||
		!SG_RuneModelStableIdEqual(&phase->id.value, &expected) ||
		phase->stance < 0 || phase->stance >= SG_RUNE_STANCE_COUNT ||
		phase->motion < 0 || phase->motion >= SG_RUNE_MOTION_COUNT ||
		phase->support < 0 || phase->support >= SG_RUNE_SUPPORT_COUNT ||
		phase->medium < 0 || phase->medium >= SG_RUNE_MEDIUM_COUNT ||
		phase->void_relation < 0 ||
		phase->void_relation >= SG_RUNE_VOID_RELATION_COUNT ||
		phase->reference_frame < 0 || phase->reference_frame >= SG_RUNE_FRAME_COUNT ||
		!Interval3Valid(&phase->velocity, 0) ||
		!IntervalValid(&phase->elapsed_ms, 1) || phase->time_quantum_ms == 0 ||
		phase->time_horizon_ms < phase->time_quantum_ms)
		return 0;
	if (phase->motion == SG_RUNE_MOTION_SUPPORTED &&
		phase->support == SG_RUNE_SUPPORT_NONE)
		return 0;
	if (phase->motion == SG_RUNE_MOTION_AIRBORNE &&
		phase->support != SG_RUNE_SUPPORT_NONE)
		return 0;
	if (phase->motion == SG_RUNE_MOTION_SWIMMING &&
		(!MediumUsesWaterPhysics(phase->medium) ||
		 phase->support != SG_RUNE_SUPPORT_NONE))
		return 0;
	if (phase->support == SG_RUNE_SUPPORT_MOVER &&
		phase->reference_frame != SG_RUNE_FRAME_MOVER_RELATIVE)
		return 0;
	if (phase->reference_frame == SG_RUNE_FRAME_WORLD)
		return StableIdNone(&phase->mover.value);
	return !StableIdNone(&phase->mover.value) &&
		StableIdHasDomain(&phase->mover.value, SG_RUNE_ORDER_MECHANISM) &&
		phase->support == SG_RUNE_SUPPORT_MOVER;
}

static sg_rune_failure_reason_t RecordIdentity(
	const sg_rune_stable_id_t *id, const sg_rune_order_key_t *order,
	uint32_t domain, uint64_t source_set_identity,
	const sg_rune_order_key_t *previous)
{
	sg_rune_stable_id_t expected;
	int comparison;

	if (!id || !SG_RuneModelStableIdValid(id) ||
		!SG_RuneModelOrderKeyValid(order) || order->domain != domain ||
		order->source_set_identity != source_set_identity)
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	expected = SG_RuneModelStableIdFromOrderKey(order);
	if (!SG_RuneModelStableIdEqual(id, &expected))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	if (!previous)
		return SG_RUNE_FAILURE_NONE;
	comparison = SG_RuneModelOrderKeyCompare(previous, order);
	if (comparison == 0)
		return SG_RUNE_FAILURE_DUPLICATE_ID;
	if (comparison > 0)
		return SG_RUNE_FAILURE_NONDETERMINISTIC_ORDER;
	return SG_RUNE_FAILURE_NONE;
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

static int BinaryFindId(const void *records, size_t record_size,
	size_t id_offset, uint32_t count, const sg_rune_stable_id_t *target,
	uint32_t *index_out)
{
	uint32_t first = 0;
	uint32_t last = count;

	if (!records || !target || !SG_RuneModelStableIdValid(target))
		return 0;
	while (first < last) {
		uint32_t middle = first + (last - first) / 2;
		const unsigned char *record = (const unsigned char *)records +
			(size_t)middle * record_size;
		sg_rune_stable_id_t id;
		int comparison;

		memcpy(&id, record + id_offset, sizeof(id));
		sg_rune_model_lookup_comparisons++;
		comparison = StableIdCompare(&id, target);
		if (comparison == 0) {
			if (index_out)
				*index_out = middle;
			return 1;
		}
		if (comparison < 0)
			first = middle + 1;
		else
			last = middle;
	}
	return 0;
}

static int FindCell(const sg_rune_model_t *model, sg_rune_cell_ref_t reference,
	uint32_t *index_out)
{
	return model && BinaryFindId(model->cells, sizeof(*model->cells),
		offsetof(sg_rune_cell_t, id.value), model->cell_count,
		&reference.value, index_out);
}

static int FindPlane(const sg_rune_model_t *model, sg_rune_plane_ref_t reference,
	uint32_t *index_out)
{
	return model && BinaryFindId(model->planes, sizeof(*model->planes),
		offsetof(sg_rune_plane_t, id.value), model->plane_count,
		&reference.value, index_out);
}

static int FindPhase(const sg_rune_model_t *model, sg_rune_phase_ref_t reference,
	uint32_t *index_out)
{
	return model && BinaryFindId(model->phases, sizeof(*model->phases),
		offsetof(sg_rune_phase_basis_t, id.value), model->phase_count,
		&reference.value, index_out);
}

static int FindTransition(const sg_rune_model_t *model,
	sg_rune_phase_transition_ref_t reference, uint32_t *index_out)
{
	return model && BinaryFindId(model->phase_transitions,
		sizeof(*model->phase_transitions),
		offsetof(sg_rune_phase_transition_t, id.value),
		model->phase_transition_count, &reference.value, index_out);
}

static int FindPortal(const sg_rune_model_t *model,
	sg_rune_portal_ref_t reference, uint32_t *index_out)
{
	return model && BinaryFindId(model->portals, sizeof(*model->portals),
		offsetof(sg_rune_portal_t, id.value), model->portal_count,
		&reference.value, index_out);
}

static int FindSurface(const sg_rune_model_t *model,
	sg_rune_surface_ref_t reference, uint32_t *index_out)
{
	return model && BinaryFindId(model->surfaces, sizeof(*model->surfaces),
		offsetof(sg_rune_surface_t, id.value), model->surface_count,
		&reference.value, index_out);
}

static int FindAffordance(const sg_rune_model_t *model,
	sg_rune_affordance_ref_t reference, uint32_t *index_out)
{
	return model && BinaryFindId(model->affordances,
		sizeof(*model->affordances),
		offsetof(sg_rune_affordance_t, id.value), model->affordance_count,
		&reference.value, index_out);
}

static int FindLandmark(const sg_rune_model_t *model,
	sg_rune_landmark_ref_t reference, uint32_t *index_out)
{
	return model && BinaryFindId(model->landmarks, sizeof(*model->landmarks),
		offsetof(sg_rune_landmark_t, id.value), model->landmark_count,
		&reference.value, index_out);
}

static int FindMechanism(const sg_rune_model_t *model,
	sg_rune_mechanism_ref_t reference, uint32_t *index_out)
{
	return model && BinaryFindId(model->mechanisms,
		sizeof(*model->mechanisms),
		offsetof(sg_rune_mechanism_t, id.value), model->mechanism_count,
		&reference.value, index_out);
}

static int PhaseInCell(const sg_rune_model_t *model,
	const sg_rune_cell_t *cell, const sg_rune_phase_basis_t *phase)
{
	uint32_t index;

	for (index = 0; model && cell && phase && index < cell->phases.count;
		index++)
		if (SG_RuneModelStableIdEqual(
			&model->phases[cell->phases.first + index].id.value,
			&phase->id.value))
			return 1;
	return 0;
}

static int PhaseIndexInCell(const sg_rune_cell_t *cell,
	uint32_t phase_index)
{
	return cell && phase_index >= cell->phases.first &&
		phase_index - cell->phases.first < cell->phases.count;
}

static int PhaseDiscreteEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->stance == right->stance &&
		left->motion == right->motion && left->support == right->support &&
		left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value);
}

static int PhaseClockEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int PhaseEqualExceptStance(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value) &&
		Interval3Equal(&left->velocity, &right->velocity) &&
		IntervalEqual(&left->elapsed_ms, &right->elapsed_ms) &&
		PhaseClockEqual(left, right);
}

static sg_rune_failure_reason_t ValidateTransitionSemantics(
	const sg_rune_phase_transition_t *transition,
	const sg_rune_phase_basis_t *source,
	const sg_rune_phase_basis_t *destination)
{
	if (source->medium != destination->medium)
		return SG_RUNE_FAILURE_INVALID_PHASE;
	switch (transition->kind) {
	case SG_RUNE_PHASE_TRANSITION_STANCE:
		if (!PhaseEqualExceptStance(source, destination) ||
			source->stance == destination->stance)
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_PORTAL:
		/* Portal motion is represented separately from TIME.  Its endpoint
		 * phases must have the same elapsed basis, even though their cell
		 * ownership differs. */
		if (!PhaseDiscreteEqual(source, destination) ||
			!PhaseClockEqual(source, destination) ||
			!Interval3Equal(&source->velocity, &destination->velocity) ||
			!IntervalEqual(&source->elapsed_ms, &destination->elapsed_ms))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_ACCELERATION:
		if (!PhaseDiscreteEqual(source, destination) ||
			!PhaseClockEqual(source, destination) ||
			Interval3Equal(&source->velocity, &destination->velocity) ||
			!IntervalEqual(&source->elapsed_ms, &destination->elapsed_ms))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_TIME:
		if (!PhaseDiscreteEqual(source, destination) ||
			!PhaseClockEqual(source, destination) ||
			!Interval3Equal(&source->velocity, &destination->velocity) ||
			IntervalEqual(&source->elapsed_ms, &destination->elapsed_ms))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_MOVER_DWELL:
		if (!PhaseDiscreteEqual(source, destination) ||
			!PhaseClockEqual(source, destination) ||
			source->support != SG_RUNE_SUPPORT_MOVER ||
			!Interval3Equal(&source->velocity, &destination->velocity) ||
			IntervalEqual(&source->elapsed_ms, &destination->elapsed_ms))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_TAKEOFF:
		if (source->motion != SG_RUNE_MOTION_SUPPORTED ||
			source->support == SG_RUNE_SUPPORT_NONE ||
			destination->motion != SG_RUNE_MOTION_AIRBORNE ||
			destination->support != SG_RUNE_SUPPORT_NONE ||
			source->stance != destination->stance ||
			source->void_relation != destination->void_relation ||
			!PhaseClockEqual(source, destination) ||
			destination->reference_frame != SG_RUNE_FRAME_WORLD ||
			!StableIdNone(&destination->mover.value))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_RELAUNCH:
		if (source->motion != SG_RUNE_MOTION_AIRBORNE ||
			destination->motion != SG_RUNE_MOTION_AIRBORNE ||
			!PhaseDiscreteEqual(source, destination) ||
			!PhaseClockEqual(source, destination) ||
			(Interval3Equal(&source->velocity, &destination->velocity) &&
			 IntervalEqual(&source->elapsed_ms, &destination->elapsed_ms)))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_SUPPORT:
		if (source->motion != SG_RUNE_MOTION_AIRBORNE ||
			source->support != SG_RUNE_SUPPORT_NONE ||
			destination->motion != SG_RUNE_MOTION_SUPPORTED ||
			destination->support == SG_RUNE_SUPPORT_NONE ||
			source->stance != destination->stance ||
			source->void_relation != destination->void_relation ||
			!PhaseClockEqual(source, destination))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		break;
	case SG_RUNE_PHASE_TRANSITION_NONE:
	case SG_RUNE_PHASE_TRANSITION_KIND_COUNT:
		return SG_RUNE_FAILURE_INVALID_PHASE;
	}
	return SG_RUNE_FAILURE_NONE;
}

static sg_rune_failure_reason_t ValidateTransition(
	const sg_rune_model_t *model, const sg_rune_phase_transition_t *transition,
	const sg_rune_order_key_t *previous)
{
	uint32_t cell_index;
	uint32_t destination_cell_index;
	uint32_t source_index;
	uint32_t destination_index;
	sg_rune_failure_reason_t reason;
	int cross_cell;

	reason = RecordIdentity(&transition->id.value, &transition->order,
		SG_RUNE_ORDER_PHASE_TRANSITION, model->identity.source_set_identity,
		previous);
	if (reason != SG_RUNE_FAILURE_NONE)
		return reason;
	if (!FindCell(model, transition->cell, &cell_index) ||
		!FindPhase(model, transition->source_phase, &source_index) ||
		!FindPhase(model, transition->destination_phase, &destination_index) ||
		SG_RuneModelStableIdEqual(&transition->source_phase.value,
			&transition->destination_phase.value) ||
		transition->kind <= SG_RUNE_PHASE_TRANSITION_NONE ||
		transition->kind >= SG_RUNE_PHASE_TRANSITION_KIND_COUNT ||
		!IntervalValid(&transition->duration_ms, 1) ||
		(transition->kind == SG_RUNE_PHASE_TRANSITION_PORTAL ?
			(transition->duration_ms.min_value != 0.0f ||
			 transition->duration_ms.max_value != 0.0f) :
			transition->duration_ms.max_value <= 0.0f) ||
		(transition->flags & ~(sg_rune_phase_transition_flags_t)
			SG_RUNE_PHASE_TRANSITION_FLAGS_KNOWN) != 0)
		return SG_RUNE_FAILURE_INVALID_PHASE;
	cross_cell = (transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) != 0;
	if (transition->kind == SG_RUNE_PHASE_TRANSITION_PORTAL && !cross_cell)
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	if (!FindCell(model, transition->destination_cell,
		&destination_cell_index) ||
		cross_cell != (cell_index != destination_cell_index))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	if (!PhaseIndexInCell(&model->cells[cell_index], source_index))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	if (!PhaseIndexInCell(&model->cells[destination_cell_index],
		destination_index))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	return ValidateTransitionSemantics(transition, &model->phases[source_index],
		&model->phases[destination_index]);
}

static float KernelAccelerationLimit(const sg_rune_model_t *model,
	const sg_rune_capability_kernel_t *kernel)
{
	switch (kernel->family) {
	case SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT:
		return model->identity.physics.ground_acceleration;
	case SG_RUNE_CAPABILITY_AIRBORNE_CONTROL:
		return model->identity.physics.air_acceleration;
	case SG_RUNE_CAPABILITY_WATER_VOLUME:
		return model->identity.physics.water_acceleration;
	case SG_RUNE_CAPABILITY_HOOK_TRAJECTORY:
		return model->identity.physics.hook_acceleration;
	case SG_RUNE_CAPABILITY_MECHANISM_CROSSING:
	case SG_RUNE_CAPABILITY_EXTERNAL_FORCE:
		return model->identity.physics.external_acceleration;
	case SG_RUNE_CAPABILITY_FAMILY_COUNT:
		break;
	}
	return -1.0f;
}

static sg_rune_failure_reason_t ValidateKernel(
	const sg_rune_model_t *model, const sg_rune_capability_kernel_t *kernel,
	const sg_rune_order_key_t *previous)
{
	uint32_t source_cell_index;
	uint32_t destination_cell_index;
	uint32_t source_phase_index;
	uint32_t destination_phase_index;
	uint32_t record_index;
	const sg_rune_phase_basis_t *source_phase;
	const sg_rune_phase_basis_t *destination_phase;
	sg_rune_failure_reason_t reason;
	float acceleration_limit;
	int same_cell;
	int changes_medium;

	reason = RecordIdentity(&kernel->id.value, &kernel->order,
		SG_RUNE_ORDER_KERNEL, model->identity.source_set_identity, previous);
	if (reason != SG_RUNE_FAILURE_NONE)
		return reason;
	if (!FindCell(model, kernel->source_cell, &source_cell_index) ||
		!FindCell(model, kernel->destination_cell, &destination_cell_index) ||
		!FindPhase(model, kernel->source_phase, &source_phase_index) ||
		!FindPhase(model, kernel->destination_phase, &destination_phase_index))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	source_phase = &model->phases[source_phase_index];
	destination_phase = &model->phases[destination_phase_index];
	if (!PhaseInCell(model, &model->cells[source_cell_index], source_phase) ||
		!PhaseInCell(model, &model->cells[destination_cell_index],
			destination_phase))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	same_cell = SG_RuneModelStableIdEqual(&kernel->source_cell.value,
		&kernel->destination_cell.value);
	if (same_cell) {
		if (!StableIdNone(&kernel->boundary.value) ||
			StableIdNone(&kernel->transition.value) ||
			!FindTransition(model, kernel->transition, &record_index) ||
			!SG_RuneModelStableIdEqual(
				&model->phase_transitions[record_index].cell.value,
				&kernel->source_cell.value) ||
			!SG_RuneModelStableIdEqual(
				&model->phase_transitions[record_index].source_phase.value,
				&kernel->source_phase.value) ||
			!SG_RuneModelStableIdEqual(
				&model->phase_transitions[record_index].destination_phase.value,
				&kernel->destination_phase.value))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
	} else {
		const sg_rune_portal_t *portal;
		int forward;
		int reverse;

		if (!StableIdNone(&kernel->transition.value) ||
			!FindPortal(model, kernel->boundary, &record_index))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		portal = &model->portals[record_index];
		forward = SG_RuneModelStableIdEqual(&portal->from_cell.value,
			&kernel->source_cell.value) &&
			SG_RuneModelStableIdEqual(&portal->to_cell.value,
				&kernel->destination_cell.value);
		reverse = SG_RuneModelStableIdEqual(&portal->to_cell.value,
			&kernel->source_cell.value) &&
			SG_RuneModelStableIdEqual(&portal->from_cell.value,
				&kernel->destination_cell.value);
		if ((!forward && !reverse) ||
			(portal->direction == SG_RUNE_PORTAL_FROM_TO && !forward) ||
			(portal->direction == SG_RUNE_PORTAL_TO_FROM && !reverse))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (source_phase->medium != destination_phase->medium &&
			(portal->flags & SG_RUNE_PORTAL_CONTENTS_CHANGE) == 0)
			return SG_RUNE_FAILURE_INVALID_PHASE;
	}
	if (!StableIdNone(&kernel->affordance.value) &&
		!FindAffordance(model, kernel->affordance, NULL))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	if (!StableIdNone(&kernel->mechanism.value) &&
		!FindMechanism(model, kernel->mechanism, NULL))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	if (kernel->family < 0 || kernel->family >= SG_RUNE_CAPABILITY_FAMILY_COUNT ||
		kernel->cost_law < 0 || kernel->cost_law >= SG_RUNE_COST_LAW_COUNT ||
		(kernel->flags & ~(sg_rune_kernel_flags_t)SG_RUNE_KERNEL_FLAGS_KNOWN) != 0 ||
		(kernel->flags & (SG_RUNE_KERNEL_DIRECTIONAL |
		 SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN)) !=
		(SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
		 SG_RUNE_KERNEL_PROVEN))
		return SG_RUNE_FAILURE_INVALID_SEMANTICS;
	if (!Interval3Valid(&kernel->parameters.displacement, 0) ||
		!IntervalValid(&kernel->parameters.duration_ms, 1) ||
		!IntervalValid(&kernel->parameters.speed, 1) ||
		!IntervalValid(&kernel->parameters.acceleration, 1) ||
		!IntervalValid(&kernel->parameters.vertical_acceleration, 1) ||
		!FiniteValue(kernel->parameters.gravity) ||
		!FiniteValue(kernel->parameters.drag) ||
		kernel->parameters.duration_ms.max_value <= 0.0f)
		return SG_RUNE_FAILURE_INVALID_KERNEL;
	if (kernel->parameters.physics_abi_id != model->identity.physics_abi_id)
		return SG_RUNE_FAILURE_IDENTITY_MISMATCH;
	acceleration_limit = KernelAccelerationLimit(model, kernel);
	if (acceleration_limit < 0.0f ||
		kernel->parameters.gravity != model->identity.physics.gravity ||
		kernel->parameters.speed.max_value > model->identity.physics.max_velocity ||
		kernel->parameters.acceleration.max_value > acceleration_limit ||
		kernel->parameters.vertical_acceleration.max_value > acceleration_limit)
		return SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS;
	if (MediumUsesWaterPhysics(source_phase->medium) ||
		MediumUsesWaterPhysics(destination_phase->medium)) {
		if (kernel->parameters.drag != model->identity.physics.water_drag)
			return SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS;
	} else if (kernel->parameters.drag != 0.0f) {
		return SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS;
	}
	changes_medium = source_phase->medium != destination_phase->medium;
	if (((kernel->flags & SG_RUNE_KERNEL_CHANGES_MEDIUM) != 0) !=
		changes_medium)
		return SG_RUNE_FAILURE_INVALID_PHASE;
	if ((kernel->flags & SG_RUNE_KERNEL_REQUIRES_SUPPORT) != 0 &&
		source_phase->support == SG_RUNE_SUPPORT_NONE)
		return SG_RUNE_FAILURE_INVALID_PHASE;
	if (kernel->family == SG_RUNE_CAPABILITY_WATER_VOLUME &&
		!MediumUsesWaterPhysics(source_phase->medium) &&
		!MediumUsesWaterPhysics(destination_phase->medium))
		return SG_RUNE_FAILURE_INVALID_PHASE;
	if (kernel->family == SG_RUNE_CAPABILITY_MECHANISM_CROSSING &&
		StableIdNone(&kernel->mechanism.value))
		return SG_RUNE_FAILURE_INVALID_REFERENCE;
	return SG_RUNE_FAILURE_NONE;
}

static sg_rune_failure_reason_t ValidateRecords(const sg_rune_model_t *model)
{
	const sg_rune_order_key_t *previous;
	sg_rune_failure_reason_t reason;
	uint32_t index;

	previous = NULL;
	for (index = 0; index < model->plane_count; index++) {
		const sg_rune_plane_t *record = &model->planes[index];
		double length_squared;

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_PLANE, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!FiniteVector(&record->normal) || !FiniteValue(record->distance))
			return SG_RUNE_FAILURE_NONFINITE_GEOMETRY;
		length_squared = (double)record->normal.value[0] * record->normal.value[0] +
			(double)record->normal.value[1] * record->normal.value[1] +
			(double)record->normal.value[2] * record->normal.value[2];
		if (length_squared <= 0.0 || !isfinite(length_squared))
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->phase_count; index++) {
		const sg_rune_phase_basis_t *record = &model->phases[index];

		if (!SG_RuneModelPhaseValid(record))
			return SG_RUNE_FAILURE_INVALID_PHASE;
		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_PHASE, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->cell_count; index++) {
		const sg_rune_cell_t *record = &model->cells[index];

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_CELL, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!GeometryRefValid(&record->geometry,
			model->identity.source_set_identity))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (!BoundsValid(&record->bounds))
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		/* A cell needs four planes to bound a volume and one phase to be
		 * occupiable.  Every other per-cell count is bounded by the SpanWithin
		 * checks below, which hold each span inside its global array.  Cell
		 * complexity follows from the BSP, so no fixed per-cell ceiling may
		 * reject geometry the host accepts. */
		if (record->boundary_planes.count < 4 ||
			record->phases.count == 0)
			return SG_RUNE_FAILURE_LIMIT_EXCEEDED;
		if (!SpanWithin(record->boundary_planes.first,
			record->boundary_planes.count, model->plane_count) ||
			!SpanWithin(record->phases.first, record->phases.count,
				model->phase_count) ||
			!SpanWithin(record->surfaces.first, record->surfaces.count,
				model->surface_count) ||
			!SpanWithin(record->affordances.first, record->affordances.count,
				model->affordance_count) ||
			!SpanWithin(record->kernels.first, record->kernels.count,
				model->kernel_count) ||
			!SpanWithin(record->landmarks.first, record->landmarks.count,
				model->landmark_count) ||
			!SpanWithin(record->mechanisms.first, record->mechanisms.count,
				model->mechanism_count))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (record->bsp_leaf.index == UINT32_MAX ||
			record->bsp_area.index == UINT32_MAX ||
			!ContentsValid(record->contents) ||
			(record->semantics &
			 ~(sg_rune_cell_semantics_t)SG_RUNE_CELL_SEMANTICS_KNOWN) != 0)
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->phase_transition_count; index++) {
		reason = ValidateTransition(model, &model->phase_transitions[index],
			previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		previous = &model->phase_transitions[index].order;
	}
	previous = NULL;
	for (index = 0; index < model->portal_count; index++) {
		const sg_rune_portal_t *record = &model->portals[index];
		uint32_t vertex;

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_PORTAL, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!GeometryRefValid(&record->geometry,
			model->identity.source_set_identity) ||
			!FindCell(model, record->from_cell, NULL) ||
			!FindCell(model, record->to_cell, NULL) ||
			SG_RuneModelStableIdEqual(&record->from_cell.value,
				&record->to_cell.value) || !FindPlane(model, record->boundary_plane, NULL))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (record->boundary_vertices.count < 3 ||
			record->phases.count == 0)
			return SG_RUNE_FAILURE_LIMIT_EXCEEDED;
		if (!SpanWithin(record->boundary_vertices.first,
			record->boundary_vertices.count, model->portal_vertex_count) ||
			!SpanWithin(record->phases.first, record->phases.count,
				model->phase_count))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		for (vertex = 0; vertex < record->boundary_vertices.count; vertex++)
			if (!FiniteVector(&model->portal_vertices[
				record->boundary_vertices.first + vertex]))
				return SG_RUNE_FAILURE_NONFINITE_GEOMETRY;
		if (record->direction < 0 ||
			record->direction >= SG_RUNE_PORTAL_DIRECTION_COUNT ||
			!FiniteValue(record->clearance) || record->clearance < 0.0f ||
			(record->flags & SG_RUNE_PORTAL_HULL_VALID) == 0 ||
			(record->flags &
			 ~(sg_rune_portal_flags_t)SG_RUNE_PORTAL_FLAGS_KNOWN) != 0 ||
			!ContentsValid(record->contents_from) ||
			!ContentsValid(record->contents_to) ||
			((record->flags & SG_RUNE_PORTAL_CONTENTS_CHANGE) == 0 &&
			 record->contents_from != record->contents_to))
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->surface_count; index++) {
		const sg_rune_surface_t *record = &model->surfaces[index];

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_SURFACE, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!GeometryRefValid(&record->geometry,
			model->identity.source_set_identity) ||
			!FindCell(model, record->owner_cell, NULL) ||
			!FindPlane(model, record->plane, NULL))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (!FiniteVector(&record->normal))
			return SG_RUNE_FAILURE_NONFINITE_GEOMETRY;
		if (!ContentsValid(record->contents) ||
			(record->semantics &
			 ~(sg_rune_surface_semantics_t)SG_RUNE_SURFACE_SEMANTICS_KNOWN) != 0)
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->affordance_count; index++) {
		const sg_rune_affordance_t *record = &model->affordances[index];

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_AFFORDANCE, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!FindCell(model, record->owner_cell, NULL) ||
			record->surfaces.count == 0 ||
			record->phases.count == 0 ||
			!SpanWithin(record->surfaces.first, record->surfaces.count,
				model->surface_count) ||
			!SpanWithin(record->phases.first, record->phases.count,
				model->phase_count))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (record->kind < 0 || record->kind >= SG_RUNE_AFFORDANCE_KIND_COUNT ||
			!IntervalValid(&record->range, 1))
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->kernel_count; index++) {
		reason = ValidateKernel(model, &model->kernels[index], previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		previous = &model->kernels[index].order;
	}
	previous = NULL;
	for (index = 0; index < model->landmark_count; index++) {
		const sg_rune_landmark_t *record = &model->landmarks[index];
		uint32_t cell_index;
		uint32_t surface_index;

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_LANDMARK, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!GeometryRefValid(&record->geometry,
			model->identity.source_set_identity) ||
			!FindCell(model, record->cell, &cell_index))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (record->kind < 0 || record->kind >= SG_RUNE_LANDMARK_KIND_COUNT ||
			!EntityRefValid(&record->entity) || !BoundsValid(&record->bounds) ||
			!PointInsideBounds(&record->origin, &record->bounds) ||
			!PointInsideBounds(&record->origin, &model->cells[cell_index].bounds))
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		if (!StableIdNone(&record->mechanism.value) &&
			!FindMechanism(model, record->mechanism, NULL))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		if (!StableIdNone(&record->surface.value)) {
			if (!FindSurface(model, record->surface, &surface_index) ||
				!SG_RuneModelStableIdEqual(
					&model->surfaces[surface_index].owner_cell.value,
					&record->cell.value))
				return SG_RUNE_FAILURE_INVALID_REFERENCE;
		}
		previous = &record->order;
	}
	previous = NULL;
	for (index = 0; index < model->mechanism_count; index++) {
		const sg_rune_mechanism_t *record = &model->mechanisms[index];

		reason = RecordIdentity(&record->id.value, &record->order,
			SG_RUNE_ORDER_MECHANISM, model->identity.source_set_identity, previous);
		if (reason != SG_RUNE_FAILURE_NONE)
			return reason;
		if (!FindCell(model, record->entry_cell, NULL) ||
			!FindCell(model, record->exit_cell, NULL) ||
			SG_RuneModelStableIdEqual(&record->entry_cell.value,
				&record->exit_cell.value) ||
			record->kind < 0 || record->kind >= SG_RUNE_MECHANISM_KIND_COUNT ||
			!EntityRefValid(&record->entity) ||
			!IntervalValid(&record->dwell_ms, 1) ||
			!IntervalValid(&record->travel_ms, 1))
			return SG_RUNE_FAILURE_INVALID_SEMANTICS;
		if (!SpanWithin(record->topology.first, record->topology.count,
			model->mechanism_count) ||
			(!StableIdNone(&record->activation_landmark.value) &&
			 !FindLandmark(model, record->activation_landmark, NULL)) ||
			(StableIdNone(&record->activation_landmark.value) &&
			 record->entity.index == UINT32_MAX))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		previous = &record->order;
	}
	for (index = 0; index < model->phase_count; index++) {
		const sg_rune_phase_basis_t *phase = &model->phases[index];
		uint32_t mechanism_index;
		uint32_t entry_index;
		uint32_t exit_index;
		const sg_rune_mechanism_t *mechanism;

		if (phase->reference_frame != SG_RUNE_FRAME_MOVER_RELATIVE)
			continue;
		if (!FindMechanism(model,
			(sg_rune_mechanism_ref_t){ phase->mover.value },
			&mechanism_index))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
		mechanism = &model->mechanisms[mechanism_index];
		if (!FindCell(model, mechanism->entry_cell, &entry_index) ||
			!FindCell(model, mechanism->exit_cell, &exit_index) ||
			(!PhaseInCell(model, &model->cells[entry_index], phase) &&
			 !PhaseInCell(model, &model->cells[exit_index], phase)))
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
	}
	return SG_RUNE_FAILURE_NONE;
}

static sg_rune_failure_reason_t ValidateCellOwnership(
	const sg_rune_model_t *model)
{
	uint32_t cell_index;
	uint32_t record_index;

	for (cell_index = 0; cell_index < model->cell_count; cell_index++) {
		const sg_rune_cell_t *cell = &model->cells[cell_index];
		uint32_t index;

		for (index = 0; index < cell->surfaces.count; index++)
			if (!SG_RuneModelStableIdEqual(
				&model->surfaces[cell->surfaces.first + index].owner_cell.value,
				&cell->id.value))
				return SG_RUNE_FAILURE_INVALID_REFERENCE;
		for (index = 0; index < cell->affordances.count; index++)
			if (!SG_RuneModelStableIdEqual(
				&model->affordances[cell->affordances.first + index].owner_cell.value,
				&cell->id.value))
				return SG_RUNE_FAILURE_INVALID_REFERENCE;
		for (index = 0; index < cell->kernels.count; index++)
			if (!SG_RuneModelStableIdEqual(
				&model->kernels[cell->kernels.first + index].source_cell.value,
				&cell->id.value))
				return SG_RUNE_FAILURE_INVALID_REFERENCE;
		for (index = 0; index < cell->landmarks.count; index++)
			if (!SG_RuneModelStableIdEqual(
				&model->landmarks[cell->landmarks.first + index].cell.value,
				&cell->id.value))
				return SG_RUNE_FAILURE_INVALID_REFERENCE;
		for (index = 0; index < cell->mechanisms.count; index++) {
			const sg_rune_mechanism_t *mechanism =
				&model->mechanisms[cell->mechanisms.first + index];

			if (!SG_RuneModelStableIdEqual(&mechanism->entry_cell.value,
					&cell->id.value) &&
				!SG_RuneModelStableIdEqual(&mechanism->exit_cell.value,
					&cell->id.value))
				return SG_RUNE_FAILURE_INVALID_REFERENCE;
		}
	}
	for (record_index = 0; record_index < model->surface_count; record_index++) {
		uint32_t owner_index;
		const sg_rune_surface_t *record = &model->surfaces[record_index];

		if (!FindCell(model, record->owner_cell, &owner_index) ||
			record_index < model->cells[owner_index].surfaces.first ||
			record_index >= model->cells[owner_index].surfaces.first +
				model->cells[owner_index].surfaces.count)
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
	}
	for (record_index = 0; record_index < model->affordance_count;
		record_index++) {
		uint32_t owner_index;
		const sg_rune_affordance_t *record = &model->affordances[record_index];

		if (!FindCell(model, record->owner_cell, &owner_index) ||
			record_index < model->cells[owner_index].affordances.first ||
			record_index >= model->cells[owner_index].affordances.first +
				model->cells[owner_index].affordances.count)
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
	}
	for (record_index = 0; record_index < model->kernel_count; record_index++) {
		uint32_t owner_index;
		const sg_rune_capability_kernel_t *record = &model->kernels[record_index];

		if (!FindCell(model, record->source_cell, &owner_index) ||
			record_index < model->cells[owner_index].kernels.first ||
			record_index >= model->cells[owner_index].kernels.first +
				model->cells[owner_index].kernels.count)
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
	}
	for (record_index = 0; record_index < model->landmark_count;
		record_index++) {
		uint32_t owner_index;
		const sg_rune_landmark_t *record = &model->landmarks[record_index];

		if (!FindCell(model, record->cell, &owner_index) ||
			record_index < model->cells[owner_index].landmarks.first ||
			record_index >= model->cells[owner_index].landmarks.first +
				model->cells[owner_index].landmarks.count)
			return SG_RUNE_FAILURE_INVALID_REFERENCE;
	}
	return SG_RUNE_FAILURE_NONE;
}

static int CountPointerValid(const void *pointer, uint32_t count,
	uint32_t limit)
{
	return count <= limit && (count == 0 || pointer != NULL);
}

static int ModelPointersValid(const sg_rune_model_t *model)
{
	return model &&
		CountPointerValid(model->planes, model->plane_count,
			SG_RUNE_MODEL_MAX_PLANES) &&
		CountPointerValid(model->portal_vertices, model->portal_vertex_count,
			SG_RUNE_MODEL_MAX_PORTAL_VERTICES) &&
		CountPointerValid(model->phases, model->phase_count,
			SG_RUNE_MODEL_MAX_PHASES) &&
		CountPointerValid(model->phase_transitions,
			model->phase_transition_count,
			SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS) &&
		CountPointerValid(model->cells, model->cell_count,
			SG_RUNE_MODEL_MAX_CELLS) &&
		CountPointerValid(model->portals, model->portal_count,
			SG_RUNE_MODEL_MAX_PORTALS) &&
		CountPointerValid(model->surfaces, model->surface_count,
			SG_RUNE_MODEL_MAX_SURFACES) &&
		CountPointerValid(model->affordances, model->affordance_count,
			SG_RUNE_MODEL_MAX_AFFORDANCES) &&
		CountPointerValid(model->kernels, model->kernel_count,
			SG_RUNE_MODEL_MAX_KERNELS) &&
		CountPointerValid(model->landmarks, model->landmark_count,
			SG_RUNE_MODEL_MAX_LANDMARKS) &&
		CountPointerValid(model->mechanisms, model->mechanism_count,
			SG_RUNE_MODEL_MAX_MECHANISMS);
}

static int PhysicsValid(const sg_rune_physics_parameters_t *physics)
{
	return physics && FiniteValue(physics->gravity) &&
		FiniteValue(physics->ground_acceleration) &&
		FiniteValue(physics->air_acceleration) &&
		FiniteValue(physics->water_acceleration) &&
		FiniteValue(physics->hook_acceleration) &&
		FiniteValue(physics->external_acceleration) &&
		FiniteValue(physics->water_drag) &&
		FiniteValue(physics->max_velocity) && physics->gravity >= 0.0f &&
		physics->ground_acceleration >= 0.0f &&
		physics->air_acceleration >= 0.0f &&
		physics->water_acceleration >= 0.0f &&
		physics->hook_acceleration >= 0.0f &&
		physics->external_acceleration >= 0.0f &&
		physics->water_drag >= 0.0f && physics->max_velocity > 0.0f &&
		physics->frame_ms > 0 && physics->substep_ms > 0 &&
		physics->substep_ms <= physics->frame_ms;
}

static sg_rune_failure_reason_t ValidateIdentity(
	const sg_rune_model_identity_t *identity)
{
	if (!identity || identity->bsp_content_id == 0 ||
		identity->entity_semantics_id == 0 || identity->physics_abi_id == 0 ||
		identity->source_set_identity == 0 ||
		identity->source_set_identity == UINT64_MAX || identity->schema_id == 0 ||
		identity->producer_identity == 0)
		return SG_RUNE_FAILURE_IDENTITY_MISMATCH;
	if (!PhysicsValid(&identity->physics))
		return SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS;
	if (!HullValid(&identity->standing_hull) ||
		!HullValid(&identity->crouching_hull))
		return SG_RUNE_FAILURE_INVALID_SEMANTICS;
	return SG_RUNE_FAILURE_NONE;
}

static int EvidenceValid(const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence)
{
	return evidence && evidence->version == SG_RUNE_VALIDATION_EVIDENCE_VERSION &&
		evidence->reserved == 0 && evidence->verifier_identity != 0 &&
		evidence->verifier_identity != model->identity.producer_identity &&
		evidence->bsp_content_id == model->identity.bsp_content_id &&
		evidence->source_set_identity == model->identity.source_set_identity &&
		evidence->fixed_point_identity != 0 &&
		evidence->fixed_point_rounds != 0 &&
		evidence->proved_cells == model->cell_count &&
		evidence->proved_portals == model->portal_count &&
		evidence->omitted_cells == 0 && evidence->omitted_portals == 0 &&
		evidence->invented_portals == 0 && evidence->pending_work == 0;
}

uint64_t SG_RuneModelLastLookupComparisons(void)
{
	return sg_rune_model_lookup_comparisons;
}

sg_rune_failure_reason_t SG_RuneModelValidate(const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence)
{
	sg_rune_failure_reason_t reason;

	if (!model)
		return SG_RUNE_FAILURE_INVALID_ARGUMENT;
	if (model->version != SG_RUNE_MODEL_VERSION || model->reserved != 0 ||
		model->schema_tag != SG_RUNE_MODEL_SCHEMA_TAG ||
		(model->flags & ~(sg_rune_model_flags_t)SG_RUNE_MODEL_KNOWN_FLAGS) != 0 ||
		(model->flags & SG_RUNE_MODEL_KNOWN_FLAGS) != SG_RUNE_MODEL_KNOWN_FLAGS)
		return SG_RUNE_FAILURE_INVALID_SEMANTICS;
	reason = ValidateIdentity(&model->identity);
	if (reason != SG_RUNE_FAILURE_NONE)
		return reason;
	if (!SG_RuneModelCompletenessValid(&model->completeness))
		return SG_RUNE_FAILURE_INVALID_SEMANTICS;
	if (model->completeness.state == SG_RUNE_COMPLETENESS_FAILED)
		return model->completeness.reason;
	if (model->completeness.state != SG_RUNE_COMPLETENESS_COMPLETE ||
		model->completeness.covered_cells != model->cell_count ||
		model->completeness.covered_portals != model->portal_count)
		return SG_RUNE_FAILURE_INCOMPLETE;
	if (!ModelPointersValid(model))
		return SG_RUNE_FAILURE_LIMIT_EXCEEDED;
	if (!EvidenceValid(model, evidence))
		return SG_RUNE_FAILURE_INCOMPLETE;
	sg_rune_model_lookup_comparisons = 0;
	reason = ValidateRecords(model);
	if (reason != SG_RUNE_FAILURE_NONE)
		return reason;
	return ValidateCellOwnership(model);
}

const char *SG_RuneModelFailureReasonString(sg_rune_failure_reason_t reason)
{
	switch (reason) {
	case SG_RUNE_FAILURE_NONE: return "none";
	case SG_RUNE_FAILURE_INVALID_ARGUMENT: return "invalid argument";
	case SG_RUNE_FAILURE_LIMIT_EXCEEDED: return "limit exceeded";
	case SG_RUNE_FAILURE_NONFINITE_GEOMETRY: return "nonfinite geometry";
	case SG_RUNE_FAILURE_INVALID_REFERENCE: return "invalid reference";
	case SG_RUNE_FAILURE_DUPLICATE_ID: return "duplicate id";
	case SG_RUNE_FAILURE_NONDETERMINISTIC_ORDER: return "nondeterministic order";
	case SG_RUNE_FAILURE_MISSING_CONFIGURATION: return "missing configuration";
	case SG_RUNE_FAILURE_MISSING_PORTAL: return "missing portal";
	case SG_RUNE_FAILURE_INVALID_PHASE: return "invalid phase";
	case SG_RUNE_FAILURE_INVALID_KERNEL: return "invalid kernel";
	case SG_RUNE_FAILURE_INVALID_SEMANTICS: return "invalid semantics";
	case SG_RUNE_FAILURE_IDENTITY_MISMATCH: return "identity mismatch";
	case SG_RUNE_FAILURE_UNSUPPORTED_BSP: return "unsupported bsp";
	case SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS: return "unsupported physics";
	case SG_RUNE_FAILURE_INCOMPLETE: return "incomplete";
	case SG_RUNE_FAILURE_REASON_COUNT: break;
	}
	return "invalid failure reason";
}
