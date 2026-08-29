#include "sg_belief_contract.h"

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BELIEF_HORIZON_ISSUER_ID UINT64_C(0x53474c4f43485a31)

typedef struct belief_horizon_provenance_s
{
	uint64_t issuer_identity;
	uint64_t source_identity;
	uint64_t source_generation;
	uint64_t fixed_point_identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t state_generation;
	uint64_t state_revision;
	uint64_t state_time_ms;
	uint64_t state_frame_sequence;
	uint64_t to_time_ms;
	uint8_t audience_team;
	uint8_t target_team;
	uint16_t target_client;
} belief_horizon_provenance_t;

struct sg_belief_horizon_source_s
{
	belief_horizon_provenance_t provenance;
	sg_rune_v2_content_id_t chain_identity;
	size_t kernel_count;
	sg_belief_horizon_kernel_t *kernels;
	struct sg_belief_horizon_source_s *next_issued;
};

struct sg_belief_horizon_authority_s
{
	belief_horizon_provenance_t provenance;
	sg_rune_v2_content_id_t chain_identity;
	size_t kernel_count;
	sg_belief_horizon_kernel_t *kernels;
	struct sg_belief_horizon_authority_s *next_issued;
};

static sg_belief_horizon_source_t *belief_issued_sources;
static sg_belief_horizon_authority_t *belief_issued_authorities;

static int BeliefSizeAdd(size_t left, size_t right, size_t *out)
{
	if (!out || left > SIZE_MAX - right)
		return 0;
	*out = left + right;
	return 1;
}

typedef struct belief_byte_range_s
{
	uintptr_t begin;
	uintptr_t end;
} belief_byte_range_t;

static int BeliefByteRange(const void *pointer, size_t count,
	size_t element_size, belief_byte_range_t *range)
{
	size_t bytes;
	uintptr_t begin;

	if (!pointer || !range || count == 0U || element_size == 0U ||
	    count > SIZE_MAX / element_size)
		return 0;
	bytes = count * element_size;
	begin = (uintptr_t)pointer;
	if (bytes > UINTPTR_MAX - begin)
		return 0;
	range->begin = begin;
	range->end = begin + bytes;
	return 1;
}

static int BeliefRangesOverlap(const belief_byte_range_t *left,
	const belief_byte_range_t *right)
{
	return left->begin < right->end && right->begin < left->end;
}

static int BeliefRangeDisjointFromRune(
	const sg_rune_runtime_snapshot_t *snapshot,
	const belief_byte_range_t *range)
{
	return range && range->end > range->begin &&
		SG_BeliefMutableRangeDisjointFromRune(snapshot,
			(const void *)range->begin, range->end - range->begin);
}

static int BeliefPolicyValid(const sg_belief_policy_t *policy)
{
	return policy && policy->confidence_decay_ms != 0U &&
		SG_BeliefFloatValid(policy->diffusion_fraction) &&
		policy->diffusion_fraction >= 0.0f &&
		policy->diffusion_fraction <= 1.0f &&
		SG_BeliefFloatValid(policy->spread_growth_per_ms) &&
		policy->spread_growth_per_ms >= 0.0f;
}

typedef struct belief_work_counters_s
{
	size_t validated_phase_spans;
	size_t validated_horizon_entries;
	size_t validated_horizon_steps;
	size_t evaluated_outcomes;
	int overflowed;
} belief_work_counters_t;

static int BeliefCounterIncrement(size_t *counter,
	belief_work_counters_t *counters)
{
	if (*counter == SIZE_MAX)
	{
		counters->overflowed = 1;
		return 0;
	}
	(*counter)++;
	return 1;
}

static void BeliefReportCounters(sg_belief_reduction_t *reduction,
	const belief_work_counters_t *counters)
{
	reduction->validated_phase_spans = counters->validated_phase_spans;
	reduction->validated_horizon_entries =
		counters->validated_horizon_entries;
	reduction->validated_horizon_steps = counters->validated_horizon_steps;
	reduction->evaluated_outcomes = counters->evaluated_outcomes;
}

static void BeliefReportPredictionCounters(sg_belief_prediction_t *prediction,
	const belief_work_counters_t *counters)
{
	prediction->validated_phase_spans = counters->validated_phase_spans;
	prediction->validated_horizon_entries =
		counters->validated_horizon_entries;
	prediction->validated_horizon_steps =
		counters->validated_horizon_steps;
	prediction->evaluated_outcomes = counters->evaluated_outcomes;
}

static int BeliefStateBoundToSnapshot(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state)
{
	belief_byte_range_t storage_range;
	size_t index;

	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !SG_BeliefStateValid(state) ||
	    state->rune_identity != snapshot->identity ||
	    state->topology_revision != snapshot->topology_revision ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &storage_range))
		return 0;
	for (index = 0U; index < state->particle_count; index++)
		if (!SG_PhaseCoordinateValid(snapshot, &state->particles[index].phase) ||
		    !SG_BeliefPositionInsidePhaseCell(snapshot,
			&state->particles[index].phase,
			state->particles[index].position) ||
		    !SG_BeliefKinematicsCompatible(snapshot,
			&state->particles[index].phase,
			state->particles[index].movement_state,
			state->particles[index].velocity,
			state->particles[index].acceleration,
			state->particles[index].orientation))
			return 0;
	return 1;
}

static int BeliefVectorValid(const float vector[3])
{
	return vector && SG_BeliefFloatValid(vector[0]) &&
		SG_BeliefFloatValid(vector[1]) && SG_BeliefFloatValid(vector[2]);
}

static int BeliefSupportValid(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_evidence_support_t *support)
{
	return support && SG_PhaseCoordinateValid(snapshot, &support->phase) &&
		SG_BeliefPositionInsidePhaseCell(snapshot, &support->phase,
			support->position) &&
		SG_BeliefKinematicsCompatible(snapshot, &support->phase,
			support->movement_state, support->velocity,
			support->acceleration, support->orientation) &&
		support->movement_state >= SG_BELIEF_MOTION_UNKNOWN &&
		support->movement_state < SG_BELIEF_MOTION_COUNT &&
		support->reserved[0] == 0U && support->reserved[1] == 0U &&
		support->reserved[2] == 0U &&
		BeliefVectorValid(support->position) &&
		BeliefVectorValid(support->velocity) &&
		BeliefVectorValid(support->acceleration) &&
		BeliefVectorValid(support->orientation) &&
		SG_BeliefFloatValid(support->spread_radius) &&
		support->spread_radius >= 0.0f &&
		SG_BeliefFloatValid(support->likelihood) && support->likelihood > 0.0f;
}

static int BeliefSupportLocationsDiffer(
	const sg_belief_evidence_support_t *left,
	const sg_belief_evidence_support_t *right)
{
	uint8_t axis;

	if (left->phase.phase_id != right->phase.phase_id ||
	    left->phase.cell_id != right->phase.cell_id)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		if (left->position[axis] != right->position[axis])
			return 1;
	return 0;
}

static int BeliefEvidenceShapeValid(const sg_belief_evidence_t *evidence)
{
	size_t index;
	int all_diffuse = 1;
	int distinct = 0;

	if (evidence->kind == SG_BELIEF_EVIDENCE_NEGATIVE)
		return evidence->source == SG_BELIEF_SOURCE_SIGHT ||
			evidence->source == SG_BELIEF_SOURCE_TEAMMATE;
	if (evidence->source == SG_BELIEF_SOURCE_SIGHT)
		return evidence->support_count == 1U &&
			evidence->supports[0].spread_radius == 0.0f;
	if (evidence->source != SG_BELIEF_SOURCE_SOUND &&
	    evidence->source != SG_BELIEF_SOURCE_DAMAGE)
		return 1;
	for (index = 0U; index < evidence->support_count; index++)
	{
		if (evidence->supports[index].spread_radius == 0.0f)
			all_diffuse = 0;
		if (index != 0U && BeliefSupportLocationsDiffer(
		    &evidence->supports[0], &evidence->supports[index]))
			distinct = 1;
	}
	return all_diffuse || distinct;
}

static sg_belief_reduce_result_t BeliefEvidenceValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, const sg_belief_evidence_t *evidence,
	uint64_t at_ms)
{
	const sg_belief_provenance_t *provenance;
	belief_byte_range_t support_range;
	size_t index;

	if (!evidence)
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	if (evidence->support_count > SIZE_MAX / sizeof(*evidence->supports))
		return SG_BELIEF_REDUCE_OVERFLOW;
	provenance = &evidence->provenance;
	if (provenance->authenticated != 1U ||
	    provenance->issuer_kind < SG_BELIEF_ISSUER_LOCAL_SENSOR ||
	    provenance->issuer_kind >= SG_BELIEF_ISSUER_KIND_COUNT ||
	    provenance->issuer_team != state->audience_team ||
	    provenance->audience_team != state->audience_team ||
	    !SG_BeliefReservedZero(provenance->reserved,
		sizeof(provenance->reserved)) ||
	    !SG_BeliefLifeIdentityValid(&provenance->issuer_life) ||
	    provenance->evidence_id == 0U ||
	    provenance->evidence_sequence == 0U ||
	    provenance->authenticated_at_ms == 0U ||
	    provenance->rune_identity != snapshot->identity ||
	    provenance->topology_revision != snapshot->topology_revision ||
	    provenance->authenticated_at_ms < evidence->observed_at_ms ||
	    provenance->authenticated_at_ms > at_ms ||
	    (evidence->source == SG_BELIEF_SOURCE_TEAMMATE &&
	     provenance->issuer_kind != SG_BELIEF_ISSUER_TEAMMATE) ||
	    (evidence->source != SG_BELIEF_SOURCE_TEAMMATE &&
	     provenance->issuer_kind != SG_BELIEF_ISSUER_LOCAL_SENSOR))
		return SG_BELIEF_REDUCE_REJECTED_AUTHORITY;
	if (evidence->source < SG_BELIEF_SOURCE_SIGHT ||
	    evidence->source >= SG_BELIEF_SOURCE_COUNT ||
	    evidence->kind < SG_BELIEF_EVIDENCE_POSITIVE ||
	    evidence->kind >= SG_BELIEF_EVIDENCE_KIND_COUNT ||
	    evidence->target_team != state->target_team ||
	    !SG_BeliefReservedZero(evidence->reserved, sizeof(evidence->reserved)) ||
	    !SG_BeliefLifeIdentityValid(&evidence->target_life) ||
	    evidence->observed_at_ms == 0U ||
	    evidence->observed_at_ms > at_ms ||
	    evidence->confidence <= 0.0f ||
	    evidence->confidence > 1.0f ||
	    !SG_BeliefFloatValid(evidence->confidence) ||
	    evidence->support_count == 0U || !evidence->supports ||
	    !BeliefByteRange(evidence->supports, evidence->support_count,
		sizeof(*evidence->supports), &support_range) ||
	    !BeliefEvidenceShapeValid(evidence))
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	if (!SG_BeliefLifeIdentityEqual(&evidence->target_life,
		&state->target_life))
		return SG_BELIEF_REDUCE_REJECTED_AUTHORITY;
	if (evidence->valid_until_ms < at_ms)
		return SG_BELIEF_REDUCE_REJECTED_STALE;
	for (index = 0U; index < evidence->support_count; index++)
		if (!BeliefSupportValid(snapshot, &evidence->supports[index]) ||
		    (evidence->kind == SG_BELIEF_EVIDENCE_NEGATIVE &&
		     evidence->supports[index].likelihood > 1.0f))
			return SG_BELIEF_REDUCE_REJECTED_INVALID;
	return SG_BELIEF_REDUCE_APPLIED;
}

static int BeliefStableIdEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return left->source_set_identity == right->source_set_identity &&
		left->high == right->high && left->low == right->low;
}

typedef struct belief_step_bounds_s
{
	float displacement_min[3];
	float displacement_max[3];
	float duration_min_ms;
	float duration_max_ms;
	float speed_max;
	float acceleration_max;
	float vertical_acceleration_max;
	float gravity;
	sg_rune_capability_family_t capability_family;
	int constrains_kinematics;
} belief_step_bounds_t;

static int BeliefIntervalValid(const sg_rune_interval_t *interval,
	int nonnegative)
{
	return interval && SG_BeliefFloatValid(interval->min_value) &&
		SG_BeliefFloatValid(interval->max_value) &&
		interval->min_value <= interval->max_value &&
		(!nonnegative || interval->min_value >= 0.0f);
}

static int BeliefBoundsSetDisplacement(belief_step_bounds_t *bounds,
	const sg_rune_interval3_t *displacement)
{
	const sg_rune_interval_t *axes[3];
	size_t axis;

	if (!bounds || !displacement)
		return 0;
	axes[0] = &displacement->x;
	axes[1] = &displacement->y;
	axes[2] = &displacement->z;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!BeliefIntervalValid(axes[axis], 0))
			return 0;
		bounds->displacement_min[axis] = axes[axis]->min_value;
		bounds->displacement_max[axis] = axes[axis]->max_value;
	}
	return 1;
}

static int BeliefFloatAdd(float left, float right, float *out)
{
	float sum;

	if (!out || !SG_BeliefFloatValid(left) || !SG_BeliefFloatValid(right))
		return 0;
	sum = left + right;
	if (!SG_BeliefFloatValid(sum))
		return 0;
	*out = sum;
	return 1;
}

static int BeliefRuneRecordMatches(
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_horizon_step_kind_t kind, uint32_t record_index,
	const sg_phase_coordinate_t *from, const sg_phase_coordinate_t *to,
	belief_step_bounds_t *bounds)
{
	const sg_rune_model_t *model = snapshot->model;
	const sg_rune_stable_id_t *from_id =
		&model->phases[from->phase_id].id.value;
	const sg_rune_stable_id_t *to_id = &model->phases[to->phase_id].id.value;
	const sg_rune_stable_id_t *from_cell_id =
		&model->cells[from->cell_id].id.value;
	const sg_rune_stable_id_t *to_cell_id =
		&model->cells[to->cell_id].id.value;
	belief_step_bounds_t matched;

	memset(&matched, 0, sizeof(matched));
	matched.capability_family = SG_RUNE_CAPABILITY_FAMILY_COUNT;

	if (kind == SG_BELIEF_HORIZON_PHASE_TRANSITION)
	{
		const sg_rune_phase_transition_t *transition;
		if (record_index >= model->phase_transition_count ||
		    !model->phase_transitions)
			return 0;
		transition = &model->phase_transitions[record_index];
		if (transition->kind <= SG_RUNE_PHASE_TRANSITION_NONE ||
		    transition->kind >= SG_RUNE_PHASE_TRANSITION_KIND_COUNT ||
		    transition->flags != 0U ||
		    !BeliefStableIdEqual(&transition->cell.value, from_cell_id) ||
		    !BeliefStableIdEqual(&transition->cell.value, to_cell_id) ||
		    !BeliefStableIdEqual(&transition->source_phase.value, from_id) ||
		    !BeliefStableIdEqual(&transition->destination_phase.value, to_id) ||
		    !BeliefIntervalValid(&transition->duration_ms, 1) ||
		    transition->duration_ms.max_value <= 0.0f)
			return 0;
		matched.duration_min_ms = transition->duration_ms.min_value;
		matched.duration_max_ms = transition->duration_ms.max_value;
		if (bounds)
			*bounds = matched;
		return 1;
	}
	if (kind == SG_BELIEF_HORIZON_CAPABILITY_KERNEL)
	{
		const sg_rune_capability_kernel_t *capability;
		if (record_index >= model->kernel_count || !model->kernels)
			return 0;
		capability = &model->kernels[record_index];
		if (capability->family < SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT ||
		    capability->family >= SG_RUNE_CAPABILITY_FAMILY_COUNT ||
		    (capability->flags & ~(sg_rune_kernel_flags_t)
			(SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
			 SG_RUNE_KERNEL_CHANGES_MEDIUM |
			 SG_RUNE_KERNEL_REQUIRES_SUPPORT |
			 SG_RUNE_KERNEL_PROVEN)) != 0U ||
		    (capability->flags & (SG_RUNE_KERNEL_DIRECTIONAL |
			 SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN)) !=
			(SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
			 SG_RUNE_KERNEL_PROVEN) ||
		    !BeliefStableIdEqual(&capability->source_cell.value, from_cell_id) ||
		    !BeliefStableIdEqual(&capability->destination_cell.value, to_cell_id) ||
		    !BeliefStableIdEqual(&capability->source_phase.value, from_id) ||
		    !BeliefStableIdEqual(&capability->destination_phase.value, to_id) ||
		    !BeliefBoundsSetDisplacement(&matched,
			&capability->parameters.displacement) ||
		    !BeliefIntervalValid(&capability->parameters.duration_ms, 1) ||
		    capability->parameters.duration_ms.max_value <= 0.0f ||
		    !BeliefIntervalValid(&capability->parameters.speed, 1) ||
		    !BeliefIntervalValid(&capability->parameters.acceleration, 1) ||
		    !BeliefIntervalValid(
			&capability->parameters.vertical_acceleration, 1) ||
		    !SG_BeliefFloatValid(capability->parameters.gravity) ||
		    capability->parameters.gravity < 0.0f)
			return 0;
		matched.duration_min_ms = capability->parameters.duration_ms.min_value;
		matched.duration_max_ms = capability->parameters.duration_ms.max_value;
		matched.speed_max = capability->parameters.speed.max_value;
		matched.acceleration_max =
			capability->parameters.acceleration.max_value;
		matched.vertical_acceleration_max =
			capability->parameters.vertical_acceleration.max_value;
		matched.gravity = capability->parameters.gravity;
		matched.capability_family = capability->family;
		matched.constrains_kinematics = 1;
		if (bounds)
			*bounds = matched;
		return 1;
	}
	return 0;
}

static int BeliefHorizonEntryValid(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	const sg_belief_horizon_entry_t *entry, belief_work_counters_t *counters)
{
	belief_step_bounds_t aggregate;
	sg_phase_coordinate_t cursor;
	uint64_t elapsed_ms;
	size_t axis;
	size_t index;
	size_t end;

	if (!entry || !SG_PhaseCoordinateValid(snapshot, &entry->from) ||
	    !SG_PhaseCoordinateValid(snapshot, &entry->to) ||
	    !BeliefVectorValid(entry->displacement) ||
	    !SG_BeliefFloatValid(entry->likelihood) || entry->likelihood <= 0.0f ||
	    entry->likelihood > 1.0f ||
	    !BeliefSizeAdd(entry->first_step, entry->step_count, &end) ||
	    end > kernel->step_count)
		return 0;
	if (entry->step_count == 0U)
		return entry->from.phase_id == entry->to.phase_id &&
			entry->from.cell_id == entry->to.cell_id &&
			entry->displacement[0] == 0.0f &&
			entry->displacement[1] == 0.0f &&
			entry->displacement[2] == 0.0f;
	memset(&aggregate, 0, sizeof(aggregate));
	cursor = entry->from;
	for (index = entry->first_step; index < end; index++)
	{
		belief_step_bounds_t step_bounds;
		const sg_belief_horizon_step_t *step = &kernel->steps[index];
		if (step->from.phase_id != cursor.phase_id ||
		    step->from.cell_id != cursor.cell_id ||
		    !SG_PhaseCoordinateValid(snapshot, &step->to) ||
		    !BeliefRuneRecordMatches(snapshot, step->kind,
			step->record_index, &step->from, &step->to, &step_bounds))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			if (!BeliefFloatAdd(aggregate.displacement_min[axis],
				step_bounds.displacement_min[axis],
				&aggregate.displacement_min[axis]) ||
			    !BeliefFloatAdd(aggregate.displacement_max[axis],
				step_bounds.displacement_max[axis],
				&aggregate.displacement_max[axis]))
			{
				counters->overflowed = 1;
				return 0;
			}
		if (!BeliefFloatAdd(aggregate.duration_min_ms,
			step_bounds.duration_min_ms, &aggregate.duration_min_ms) ||
		    !BeliefFloatAdd(aggregate.duration_max_ms,
			step_bounds.duration_max_ms, &aggregate.duration_max_ms))
		{
			counters->overflowed = 1;
			return 0;
		}
		cursor = step->to;
	}
	elapsed_ms = kernel->to_time_ms - kernel->from_time_ms;
	if (cursor.phase_id != entry->to.phase_id ||
	    cursor.cell_id != entry->to.cell_id ||
	    (long double)elapsed_ms < (long double)aggregate.duration_min_ms ||
	    (long double)elapsed_ms > (long double)aggregate.duration_max_ms)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (entry->displacement[axis] < aggregate.displacement_min[axis] ||
		    entry->displacement[axis] > aggregate.displacement_max[axis])
			return 0;
	return 1;
}

static int BeliefHorizonKernelValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	belief_work_counters_t *counters)
{
	belief_byte_range_t span_range;
	belief_byte_range_t entry_range;
	belief_byte_range_t step_range;
	size_t phase;
	size_t cursor = 0U;
	size_t step_cursor = 0U;
	if (kernel &&
	    (kernel->origin_span_count > SIZE_MAX / sizeof(*kernel->origin_spans) ||
	     kernel->entry_count > SIZE_MAX / sizeof(*kernel->entries) ||
	     kernel->step_count > SIZE_MAX / sizeof(*kernel->steps)))
	{
		counters->overflowed = 1;
		return 0;
	}

	if (!kernel || kernel->rune_identity != snapshot->identity ||
	    kernel->topology_revision != snapshot->topology_revision ||
	    kernel->from_time_ms == 0U ||
	    kernel->from_time_ms >= kernel->to_time_ms ||
	    kernel->host_complete != 1U || kernel->reserved[0] != 0U ||
	    kernel->reserved[1] != 0U || kernel->reserved[2] != 0U ||
	    kernel->reserved[3] != 0U || kernel->reserved[4] != 0U ||
	    kernel->reserved[5] != 0U || kernel->reserved[6] != 0U ||
	    kernel->origin_span_count != (size_t)snapshot->phase_count ||
	    !kernel->origin_spans || kernel->entry_count == 0U || !kernel->entries ||
	    (kernel->step_count != 0U && !kernel->steps) ||
	    !BeliefByteRange(kernel->origin_spans, kernel->origin_span_count,
		sizeof(*kernel->origin_spans), &span_range) ||
	    !BeliefByteRange(kernel->entries, kernel->entry_count,
		sizeof(*kernel->entries), &entry_range) ||
	    (kernel->step_count != 0U &&
	     !BeliefByteRange(kernel->steps, kernel->step_count,
		sizeof(*kernel->steps), &step_range)))
		return 0;
	for (phase = 0U; phase < kernel->step_count; phase++)
	{
		const sg_belief_horizon_step_t *step = &kernel->steps[phase];
		if (!SG_PhaseCoordinateValid(snapshot, &step->from) ||
		    !SG_PhaseCoordinateValid(snapshot, &step->to) ||
		    !BeliefRuneRecordMatches(snapshot, step->kind,
			step->record_index, &step->from, &step->to, NULL) ||
		    !BeliefCounterIncrement(&counters->validated_horizon_steps,
			counters))
			return 0;
	}
	for (phase = 0U; phase < snapshot->phase_count; phase++)
	{
		const sg_belief_horizon_span_t *span =
			&kernel->origin_spans[phase];
		double total = 0.0;
		size_t offset;
		if (span->first_entry != cursor || span->entry_count == 0U ||
		    span->entry_count > kernel->entry_count - cursor)
			return 0;
		for (offset = 0U; offset < span->entry_count; offset++)
		{
			const sg_belief_horizon_entry_t *entry =
				&kernel->entries[cursor + offset];
			if (entry->first_step != step_cursor ||
			    !BeliefHorizonEntryValid(snapshot, kernel, entry, counters) ||
			    entry->from.phase_id != phase ||
			    entry->from.cell_id != snapshot->phases[phase].cell_id)
				return 0;
			if (!BeliefSizeAdd(step_cursor, entry->step_count,
			    &step_cursor))
			{
				counters->overflowed = 1;
				return 0;
			}
			total += (double)entry->likelihood;
			if (!BeliefCounterIncrement(
			    &counters->validated_horizon_entries, counters))
				return 0;
		}
		if (fabs(total - 1.0) > (double)SG_BELIEF_WEIGHT_EPSILON)
			return 0;
		if (!BeliefSizeAdd(cursor, span->entry_count, &cursor))
			return 0;
		if (!BeliefCounterIncrement(&counters->validated_phase_spans,
		    counters))
			return 0;
		if (cursor > kernel->entry_count)
			return 0;
	}
	return cursor == kernel->entry_count && step_cursor == kernel->step_count;
}

static int BeliefCanonicalSizeAdd(size_t *size, size_t count, size_t width)
{
	size_t bytes;

	if (!size || (count != 0U && width > SIZE_MAX / count))
		return 0;
	bytes = count * width;
	return BeliefSizeAdd(*size, bytes, size);
}

static void BeliefCanonicalU16(unsigned char *bytes, size_t *cursor,
	uint16_t value)
{
	SG_RuneV2WirePutU16(bytes + *cursor, value);
	*cursor += 2U;
}

static void BeliefCanonicalU32(unsigned char *bytes, size_t *cursor,
	uint32_t value)
{
	SG_RuneV2WirePutU32(bytes + *cursor, value);
	*cursor += 4U;
}

static void BeliefCanonicalU64(unsigned char *bytes, size_t *cursor,
	uint64_t value)
{
	SG_RuneV2WirePutU64(bytes + *cursor, value);
	*cursor += 8U;
}

static void BeliefCanonicalSize(unsigned char *bytes, size_t *cursor,
	size_t value)
{
	BeliefCanonicalU64(bytes, cursor, (uint64_t)value);
}

static void BeliefCanonicalFloat(unsigned char *bytes, size_t *cursor,
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	BeliefCanonicalU32(bytes, cursor, bits);
}

static int BeliefHorizonChainIdentity(
	const belief_horizon_provenance_t *provenance,
	const sg_belief_horizon_kernel_t *kernels, size_t kernel_count,
	sg_rune_v2_content_id_t *identity_out)
{
	static const unsigned char tag[8] = {
		'S', 'G', 'B', 'H', 'Z', '0', '0', '2'
	};
	unsigned char *bytes;
	size_t size = sizeof(tag) + 92U + 8U;
	size_t cursor = 0U;
	size_t kernel_index;

	if (!provenance || !kernels || kernel_count == 0U || !identity_out)
		return 0;
	for (kernel_index = 0U; kernel_index < kernel_count; kernel_index++)
	{
		const sg_belief_horizon_kernel_t *kernel = &kernels[kernel_index];
		if ((uint64_t)kernel->origin_span_count !=
		    kernel->origin_span_count ||
		    (uint64_t)kernel->entry_count != kernel->entry_count ||
		    (uint64_t)kernel->step_count != kernel->step_count ||
		    !BeliefCanonicalSizeAdd(&size, 1U, 64U) ||
		    !BeliefCanonicalSizeAdd(&size, kernel->origin_span_count, 16U) ||
		    !BeliefCanonicalSizeAdd(&size, kernel->entry_count, 48U) ||
		    !BeliefCanonicalSizeAdd(&size, kernel->step_count, 24U))
			return 0;
	}
	bytes = malloc(size);
	if (!bytes)
		return 0;
	memcpy(bytes, tag, sizeof(tag));
	cursor += sizeof(tag);
	BeliefCanonicalU64(bytes, &cursor, provenance->issuer_identity);
	BeliefCanonicalU64(bytes, &cursor, provenance->source_identity);
	BeliefCanonicalU64(bytes, &cursor, provenance->source_generation);
	BeliefCanonicalU64(bytes, &cursor, provenance->fixed_point_identity);
	BeliefCanonicalU64(bytes, &cursor, provenance->rune_identity);
	BeliefCanonicalU64(bytes, &cursor, provenance->topology_revision);
	BeliefCanonicalU64(bytes, &cursor, provenance->state_generation);
	BeliefCanonicalU64(bytes, &cursor, provenance->state_revision);
	BeliefCanonicalU64(bytes, &cursor, provenance->state_time_ms);
	BeliefCanonicalU64(bytes, &cursor, provenance->state_frame_sequence);
	BeliefCanonicalU64(bytes, &cursor, provenance->to_time_ms);
	bytes[cursor++] = provenance->audience_team;
	bytes[cursor++] = provenance->target_team;
	BeliefCanonicalU16(bytes, &cursor, provenance->target_client);
	BeliefCanonicalSize(bytes, &cursor, kernel_count);
	for (kernel_index = 0U; kernel_index < kernel_count; kernel_index++)
	{
		const sg_belief_horizon_kernel_t *kernel = &kernels[kernel_index];
		size_t index;
		BeliefCanonicalU64(bytes, &cursor, kernel->rune_identity);
		BeliefCanonicalU64(bytes, &cursor, kernel->topology_revision);
		BeliefCanonicalU64(bytes, &cursor, kernel->from_time_ms);
		BeliefCanonicalU64(bytes, &cursor, kernel->to_time_ms);
		bytes[cursor++] = kernel->host_complete;
		memcpy(bytes + cursor, kernel->reserved, sizeof(kernel->reserved));
		cursor += sizeof(kernel->reserved);
		BeliefCanonicalSize(bytes, &cursor, kernel->origin_span_count);
		BeliefCanonicalSize(bytes, &cursor, kernel->entry_count);
		BeliefCanonicalSize(bytes, &cursor, kernel->step_count);
		for (index = 0U; index < kernel->origin_span_count; index++)
		{
			BeliefCanonicalSize(bytes, &cursor,
				kernel->origin_spans[index].first_entry);
			BeliefCanonicalSize(bytes, &cursor,
				kernel->origin_spans[index].entry_count);
		}
		for (index = 0U; index < kernel->entry_count; index++)
		{
			const sg_belief_horizon_entry_t *entry = &kernel->entries[index];
			uint32_t axis;
			BeliefCanonicalU32(bytes, &cursor, entry->from.phase_id);
			BeliefCanonicalU32(bytes, &cursor, entry->from.cell_id);
			BeliefCanonicalU32(bytes, &cursor, entry->to.phase_id);
			BeliefCanonicalU32(bytes, &cursor, entry->to.cell_id);
			for (axis = 0U; axis < 3U; axis++)
				BeliefCanonicalFloat(bytes, &cursor,
					entry->displacement[axis]);
			BeliefCanonicalFloat(bytes, &cursor, entry->likelihood);
			BeliefCanonicalSize(bytes, &cursor, entry->first_step);
			BeliefCanonicalSize(bytes, &cursor, entry->step_count);
		}
		for (index = 0U; index < kernel->step_count; index++)
		{
			const sg_belief_horizon_step_t *step = &kernel->steps[index];
			BeliefCanonicalU32(bytes, &cursor, step->from.phase_id);
			BeliefCanonicalU32(bytes, &cursor, step->from.cell_id);
			BeliefCanonicalU32(bytes, &cursor, step->to.phase_id);
			BeliefCanonicalU32(bytes, &cursor, step->to.cell_id);
			BeliefCanonicalU32(bytes, &cursor, (uint32_t)step->kind);
			BeliefCanonicalU32(bytes, &cursor, step->record_index);
		}
	}
	if (cursor != size ||
	    !SG_RuneV2ContentIdentitySHA256(bytes, size, identity_out))
	{
		free(bytes);
		return 0;
	}
	free(bytes);
	return 1;
}

static void BeliefHorizonKernelsDestroy(sg_belief_horizon_kernel_t *kernels,
	size_t kernel_count)
{
	size_t index;

	if (!kernels)
		return;
	for (index = 0U; index < kernel_count; index++)
	{
		free((void *)kernels[index].origin_spans);
		free((void *)kernels[index].entries);
		free((void *)kernels[index].steps);
	}
	free(kernels);
}

static sg_belief_horizon_kernel_t *BeliefHorizonKernelsClone(
	const sg_belief_horizon_kernel_t *source, size_t kernel_count)
{
	sg_belief_horizon_kernel_t *copy;
	size_t index;

	if (!source || kernel_count == 0U ||
	    kernel_count > SIZE_MAX / sizeof(*copy))
		return NULL;
	copy = calloc(kernel_count, sizeof(*copy));
	if (!copy)
		return NULL;
	for (index = 0U; index < kernel_count; index++)
	{
		copy[index] = source[index];
		copy[index].origin_spans = NULL;
		copy[index].entries = NULL;
		copy[index].steps = NULL;
		if (source[index].origin_span_count >
		    SIZE_MAX / sizeof(*source[index].origin_spans) ||
		    source[index].entry_count >
		    SIZE_MAX / sizeof(*source[index].entries) ||
		    source[index].step_count > SIZE_MAX / sizeof(*source[index].steps))
			goto failure;
		copy[index].origin_spans = malloc(source[index].origin_span_count *
			sizeof(*source[index].origin_spans));
		copy[index].entries = malloc(source[index].entry_count *
			sizeof(*source[index].entries));
		if (!copy[index].origin_spans || !copy[index].entries)
			goto failure;
		memcpy((void *)copy[index].origin_spans, source[index].origin_spans,
			source[index].origin_span_count *
			sizeof(*source[index].origin_spans));
		memcpy((void *)copy[index].entries, source[index].entries,
			source[index].entry_count * sizeof(*source[index].entries));
		if (source[index].step_count != 0U)
		{
			copy[index].steps = malloc(source[index].step_count *
				sizeof(*source[index].steps));
			if (!copy[index].steps)
				goto failure;
			memcpy((void *)copy[index].steps, source[index].steps,
				source[index].step_count * sizeof(*source[index].steps));
		}
	}
	return copy;

failure:
	BeliefHorizonKernelsDestroy(copy, kernel_count);
	return NULL;
}

static int BeliefHorizonKernelEqual(const sg_belief_horizon_kernel_t *left,
	const sg_belief_horizon_kernel_t *right)
{
	size_t index;

	if (left->rune_identity != right->rune_identity ||
	    left->topology_revision != right->topology_revision ||
	    left->from_time_ms != right->from_time_ms ||
	    left->to_time_ms != right->to_time_ms ||
	    left->host_complete != right->host_complete ||
	    memcmp(left->reserved, right->reserved, sizeof(left->reserved)) != 0 ||
	    left->origin_span_count != right->origin_span_count ||
	    left->entry_count != right->entry_count ||
	    left->step_count != right->step_count)
		return 0;
	for (index = 0U; index < left->origin_span_count; index++)
		if (left->origin_spans[index].first_entry !=
		    right->origin_spans[index].first_entry ||
		    left->origin_spans[index].entry_count !=
		    right->origin_spans[index].entry_count)
			return 0;
	for (index = 0U; index < left->entry_count; index++)
	{
		const sg_belief_horizon_entry_t *a = &left->entries[index];
		const sg_belief_horizon_entry_t *b = &right->entries[index];
		if (a->from.phase_id != b->from.phase_id ||
		    a->from.cell_id != b->from.cell_id ||
		    a->to.phase_id != b->to.phase_id ||
		    a->to.cell_id != b->to.cell_id ||
		    memcmp(a->displacement, b->displacement,
			sizeof(a->displacement)) != 0 ||
		    memcmp(&a->likelihood, &b->likelihood,
			sizeof(a->likelihood)) != 0 ||
		    a->first_step != b->first_step ||
		    a->step_count != b->step_count)
			return 0;
	}
	for (index = 0U; index < left->step_count; index++)
	{
		const sg_belief_horizon_step_t *a = &left->steps[index];
		const sg_belief_horizon_step_t *b = &right->steps[index];
		if (a->from.phase_id != b->from.phase_id ||
		    a->from.cell_id != b->from.cell_id ||
		    a->to.phase_id != b->to.phase_id ||
		    a->to.cell_id != b->to.cell_id || a->kind != b->kind ||
		    a->record_index != b->record_index)
			return 0;
	}
	return 1;
}

static int BeliefHorizonChainValid(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernels, size_t kernel_count,
	uint64_t from_time_ms, uint64_t to_time_ms,
	belief_work_counters_t *counters)
{
	size_t index;

	if (!kernels || kernel_count == 0U ||
	    kernel_count > SIZE_MAX / sizeof(*kernels))
		return 0;
	for (index = 0U; index < kernel_count; index++)
		if (!BeliefHorizonKernelValid(snapshot, &kernels[index], counters) ||
		    (index == 0U && kernels[index].from_time_ms != from_time_ms) ||
		    (index != 0U && kernels[index].from_time_ms !=
			kernels[index - 1U].to_time_ms))
			return 0;
	return kernels[kernel_count - 1U].to_time_ms == to_time_ms;
}

static int BeliefHorizonSourceIssued(
	const sg_belief_horizon_source_t *source)
{
	const sg_belief_horizon_source_t *cursor;

	for (cursor = belief_issued_sources; cursor; cursor = cursor->next_issued)
		if (cursor == source)
			return 1;
	return 0;
}

static int BeliefHorizonAuthorityIssued(
	const sg_belief_horizon_authority_t *authority)
{
	const sg_belief_horizon_authority_t *cursor;

	for (cursor = belief_issued_authorities; cursor;
	     cursor = cursor->next_issued)
		if (cursor == authority)
			return 1;
	return 0;
}

static uint32_t BeliefHorizonPhaseIndex(const sg_rune_model_t *model,
	const sg_rune_phase_ref_t *reference)
{
	uint32_t index;

	for (index = 0U; index < model->phase_count; index++)
		if (BeliefStableIdEqual(&model->phases[index].id.value,
		    &reference->value))
			return index;
	return UINT32_MAX;
}

static int BeliefHorizonDurationContains(const sg_rune_interval_t *duration,
	uint64_t elapsed_ms)
{
	return (long double)elapsed_ms >= (long double)duration->min_value &&
		(long double)elapsed_ms <= (long double)duration->max_value;
}

static float BeliefHorizonMidpoint(const sg_rune_interval_t *interval)
{
	float value = (float)(((double)interval->min_value +
		(double)interval->max_value) * 0.5);

	return value == 0.0f ? 0.0f : value;
}

typedef struct belief_horizon_walk_frame_s
{
	sg_phase_coordinate_t phase;
	size_t next_record;
	float duration_min_ms;
	float duration_max_ms;
	float displacement_min[3];
	float displacement_max[3];
} belief_horizon_walk_frame_t;

static int BeliefHorizonRecordCount(const sg_rune_model_t *model,
	size_t *count_out)
{
	return count_out && BeliefSizeAdd((size_t)model->phase_transition_count,
		(size_t)model->kernel_count, count_out);
}

static int BeliefHorizonRecord(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *from, size_t record,
	sg_belief_horizon_step_t *step_out, belief_step_bounds_t *bounds_out)
{
	const sg_rune_model_t *model = snapshot->model;
	sg_belief_horizon_step_t step;
	uint32_t destination;

	memset(&step, 0, sizeof(step));
	step.from = *from;
	if (record < (size_t)model->phase_transition_count)
	{
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[record];
		if (!BeliefStableIdEqual(&transition->source_phase.value,
		    &model->phases[from->phase_id].id.value))
			return 0;
		destination = BeliefHorizonPhaseIndex(model,
			&transition->destination_phase);
		step.kind = SG_BELIEF_HORIZON_PHASE_TRANSITION;
		step.record_index = (uint32_t)record;
	}
	else
	{
		size_t kernel_record = record -
			(size_t)model->phase_transition_count;
		const sg_rune_capability_kernel_t *capability;
		if (kernel_record >= (size_t)model->kernel_count)
			return -1;
		capability = &model->kernels[kernel_record];
		if (!BeliefStableIdEqual(&capability->source_phase.value,
		    &model->phases[from->phase_id].id.value))
			return 0;
		destination = BeliefHorizonPhaseIndex(model,
			&capability->destination_phase);
		step.kind = SG_BELIEF_HORIZON_CAPABILITY_KERNEL;
		step.record_index = (uint32_t)kernel_record;
	}
	if (destination >= snapshot->phase_count)
		return -1;
	step.to = snapshot->phases[destination];
	if (!BeliefRuneRecordMatches(snapshot, step.kind, step.record_index,
	    &step.from, &step.to, bounds_out))
		return -1;
	*step_out = step;
	return 1;
}

static int BeliefHorizonDepthBound(const sg_rune_runtime_snapshot_t *snapshot,
	uint64_t elapsed_ms, size_t *depth_out, int *overflowed_out)
{
	const sg_rune_model_t *model = snapshot->model;
	size_t *indegree;
	size_t *longest;
	uint32_t *queue;
	size_t queue_read = 0U;
	size_t queue_write = 0U;
	size_t processed = 0U;
	size_t zero_run = 0U;
	size_t positive_steps = 0U;
	float minimum = FLT_MAX;
	uint32_t index;

	if (!depth_out || !overflowed_out)
		return 0;
	indegree = calloc((size_t)snapshot->phase_count, sizeof(*indegree));
	longest = calloc((size_t)snapshot->phase_count, sizeof(*longest));
	queue = calloc((size_t)snapshot->phase_count, sizeof(*queue));
	if (!indegree || !longest || !queue)
	{
		free(indegree);
		free(longest);
		free(queue);
		return -1;
	}
	for (index = 0U; index < model->phase_transition_count; index++)
	{
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[index];
		float value = transition->duration_ms.min_value;
		if (!SG_BeliefFloatValid(value) || value < 0.0f)
			goto invalid;
		if (value == 0.0f)
		{
			uint32_t destination = BeliefHorizonPhaseIndex(model,
				&transition->destination_phase);
			if (destination >= snapshot->phase_count ||
			    indegree[destination] == SIZE_MAX)
				goto overflow;
			indegree[destination]++;
		}
		else if (value < minimum)
			minimum = value;
	}
	for (index = 0U; index < model->kernel_count; index++)
	{
		const sg_rune_capability_kernel_t *capability = &model->kernels[index];
		float value = capability->parameters.duration_ms.min_value;
		if (!SG_BeliefFloatValid(value) || value < 0.0f)
			goto invalid;
		if (value == 0.0f)
		{
			uint32_t destination = BeliefHorizonPhaseIndex(model,
				&capability->destination_phase);
			if (destination >= snapshot->phase_count ||
			    indegree[destination] == SIZE_MAX)
				goto overflow;
			indegree[destination]++;
		}
		else if (value < minimum)
			minimum = value;
	}
	for (index = 0U; index < snapshot->phase_count; index++)
		if (indegree[index] == 0U)
			queue[queue_write++] = index;
	while (queue_read < queue_write)
	{
		uint32_t source = queue[queue_read++];
		size_t record_count;
		size_t record;
		processed++;
		if (longest[source] > zero_run)
			zero_run = longest[source];
		if (!BeliefHorizonRecordCount(model, &record_count))
			goto overflow;
		for (record = 0U; record < record_count; record++)
		{
			sg_belief_horizon_step_t step;
			belief_step_bounds_t bounds;
			int status = BeliefHorizonRecord(snapshot,
				&snapshot->phases[source], record, &step, &bounds);
			if (status < 0)
				goto invalid;
			if (status == 0 || bounds.duration_min_ms != 0.0f)
				continue;
			if (longest[source] == SIZE_MAX)
				goto overflow;
			if (longest[step.to.phase_id] < longest[source] + 1U)
				longest[step.to.phase_id] = longest[source] + 1U;
			if (indegree[step.to.phase_id] == 0U)
				goto invalid;
			indegree[step.to.phase_id]--;
			if (indegree[step.to.phase_id] == 0U)
				queue[queue_write++] = step.to.phase_id;
		}
	}
	if (processed != (size_t)snapshot->phase_count)
		goto invalid;
	if (minimum != FLT_MAX)
	{
		long double count = floorl((long double)elapsed_ms /
			(long double)minimum);
		if (!isfinite(count) || count > (long double)SIZE_MAX)
			goto overflow;
		positive_steps = (size_t)count;
	}
	if (positive_steps == SIZE_MAX ||
	    (zero_run != 0U && positive_steps + 1U > SIZE_MAX / zero_run) ||
	    (positive_steps + 1U) * zero_run > SIZE_MAX - positive_steps)
		goto overflow;
	*depth_out = (positive_steps + 1U) * zero_run + positive_steps;
	free(indegree);
	free(longest);
	free(queue);
	return 1;

overflow:
	*overflowed_out = 1;
invalid:
	free(indegree);
	free(longest);
	free(queue);
	return 0;
}

static int BeliefHorizonWalk(
	const sg_rune_runtime_snapshot_t *snapshot, uint32_t origin_phase,
	uint64_t elapsed_ms, size_t max_depth, size_t record_count,
	belief_horizon_walk_frame_t *frames, sg_belief_horizon_step_t *path,
	float likelihood, sg_belief_horizon_entry_t *entries,
	sg_belief_horizon_step_t *steps, size_t *entry_write,
	size_t *step_write, int *overflowed_out)
{
	size_t depth = 0U;

	memset(&frames[0], 0, sizeof(frames[0]));
	frames[0].phase = snapshot->phases[origin_phase];
	if (entries)
	{
		entries[*entry_write].from = frames[0].phase;
		entries[*entry_write].to = frames[0].phase;
		entries[*entry_write].likelihood = likelihood;
		entries[*entry_write].first_step = *step_write;
	}
	if (!BeliefSizeAdd(*entry_write, 1U, entry_write))
	{
		*overflowed_out = 1;
		return 0;
	}
	if (max_depth == 0U)
		return 1;
	while (1)
	{
		belief_horizon_walk_frame_t *frame = &frames[depth];
		belief_horizon_walk_frame_t next;
		belief_step_bounds_t bounds;
		sg_belief_horizon_step_t step;
		int record_status;
		size_t axis;
		size_t path_length;

		if (frame->next_record == record_count)
		{
			if (depth == 0U)
				return 1;
			depth--;
			continue;
		}
		record_status = BeliefHorizonRecord(snapshot, &frame->phase,
			frame->next_record++, &step, &bounds);
		if (record_status < 0)
			return 0;
		if (record_status == 0)
			continue;
		memset(&next, 0, sizeof(next));
		next.phase = step.to;
		if (!BeliefFloatAdd(frame->duration_min_ms,
		    bounds.duration_min_ms, &next.duration_min_ms) ||
		    !BeliefFloatAdd(frame->duration_max_ms,
			bounds.duration_max_ms, &next.duration_max_ms))
		{
			*overflowed_out = 1;
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
			if (!BeliefFloatAdd(frame->displacement_min[axis],
			    bounds.displacement_min[axis],
			    &next.displacement_min[axis]) ||
			    !BeliefFloatAdd(frame->displacement_max[axis],
				bounds.displacement_max[axis],
				&next.displacement_max[axis]))
			{
				*overflowed_out = 1;
				return 0;
			}
		if ((long double)next.duration_min_ms > (long double)elapsed_ms)
			continue;
		path[depth] = step;
		path_length = depth + 1U;
		if (BeliefHorizonDurationContains(&(sg_rune_interval_t){
		    next.duration_min_ms, next.duration_max_ms }, elapsed_ms))
		{
			size_t first_step = *step_write;
			if (!BeliefSizeAdd(*step_write, path_length, step_write) ||
			    !BeliefSizeAdd(*entry_write, 1U, entry_write))
			{
				*overflowed_out = 1;
				return 0;
			}
			if (entries)
			{
				sg_belief_horizon_entry_t *entry =
					&entries[*entry_write - 1U];
				entry->from = snapshot->phases[origin_phase];
				entry->to = next.phase;
				for (axis = 0U; axis < 3U; axis++)
					entry->displacement[axis] =
						BeliefHorizonMidpoint(
							&(sg_rune_interval_t){
							 next.displacement_min[axis],
							 next.displacement_max[axis] });
				entry->likelihood = likelihood;
				entry->first_step = first_step;
				entry->step_count = path_length;
				memcpy(&steps[first_step], path,
					path_length * sizeof(*path));
			}
		}
		if (path_length < max_depth &&
		    (long double)next.duration_min_ms < (long double)elapsed_ms)
		{
			frames[path_length] = next;
			depth = path_length;
		}
	}
}

static sg_belief_horizon_kernel_t *BeliefHorizonFixedPointCreate(
	const sg_rune_runtime_snapshot_t *snapshot, uint64_t from_time_ms,
	uint64_t to_time_ms, int *overflowed_out, int *invalid_out)
{
	const sg_rune_model_t *model = snapshot->model;
	sg_belief_horizon_kernel_t *kernel = NULL;
	sg_belief_horizon_span_t *spans = NULL;
	sg_belief_horizon_entry_t *entries = NULL;
	sg_belief_horizon_step_t *steps = NULL;
	belief_horizon_walk_frame_t *frames = NULL;
	sg_belief_horizon_step_t *path = NULL;
	uint64_t elapsed_ms = to_time_ms - from_time_ms;
	size_t entry_count = 0U;
	size_t step_count = 0U;
	size_t record_count;
	size_t max_depth = 0U;
	size_t write = 0U;
	size_t step_write = 0U;
	int depth_status;
	uint32_t phase;

	if (!overflowed_out || !invalid_out)
		return NULL;
	*overflowed_out = 0;
	*invalid_out = 0;
	if (!BeliefHorizonRecordCount(model, &record_count))
	{
		*overflowed_out = 1;
		return NULL;
	}
	depth_status = BeliefHorizonDepthBound(snapshot, elapsed_ms, &max_depth,
		overflowed_out);
	if (depth_status <= 0)
	{
		*invalid_out = depth_status == 0 && !*overflowed_out;
		return NULL;
	}
	if (max_depth > SIZE_MAX / sizeof(*path) ||
	    max_depth == SIZE_MAX || max_depth + 1U > SIZE_MAX / sizeof(*frames))
	{
		*overflowed_out = 1;
		return NULL;
	}
	frames = calloc(max_depth + 1U, sizeof(*frames));
	path = max_depth == 0U ? NULL : calloc(max_depth, sizeof(*path));
	if (!frames || (max_depth != 0U && !path))
	{
		free(frames);
		free(path);
		return NULL;
	}
	spans = calloc((size_t)snapshot->phase_count, sizeof(*spans));
	if (!spans)
	{
		free(frames);
		free(path);
		return NULL;
	}
	for (phase = 0U; phase < snapshot->phase_count; phase++)
	{
		size_t phase_first = entry_count;
		if (!BeliefHorizonWalk(snapshot, phase, elapsed_ms, max_depth,
		    record_count, frames, path, 0.0f, NULL, NULL, &entry_count,
		    &step_count, overflowed_out))
		{
			*invalid_out = !*overflowed_out;
			goto failure;
		}
		spans[phase].first_entry = phase_first;
		spans[phase].entry_count = entry_count - phase_first;
	}
	if (entry_count > SIZE_MAX / sizeof(*entries) ||
	    (step_count != 0U && step_count > SIZE_MAX / sizeof(*steps)))
	{
		*overflowed_out = 1;
		goto failure;
	}
	kernel = calloc(1U, sizeof(*kernel));
	entries = calloc(entry_count, sizeof(*entries));
	steps = step_count == 0U ? NULL : calloc(step_count, sizeof(*steps));
	if (!kernel || !entries || (step_count != 0U && !steps))
	{
		goto failure;
	}
	for (phase = 0U; phase < snapshot->phase_count; phase++)
	{
		float likelihood = 1.0f / (float)spans[phase].entry_count;
		if (!BeliefHorizonWalk(snapshot, phase, elapsed_ms, max_depth,
		    record_count, frames, path, likelihood, entries, steps, &write,
		    &step_write, overflowed_out))
		{
			*invalid_out = !*overflowed_out;
			goto failure;
		}
	}
	if (write != entry_count || step_write != step_count)
		goto failure;
	kernel->rune_identity = snapshot->identity;
	kernel->topology_revision = snapshot->topology_revision;
	kernel->from_time_ms = from_time_ms;
	kernel->to_time_ms = to_time_ms;
	kernel->host_complete = 1U;
	kernel->origin_spans = spans;
	kernel->origin_span_count = snapshot->phase_count;
	kernel->entries = entries;
	kernel->entry_count = entry_count;
	kernel->steps = steps;
	kernel->step_count = step_count;
	free(frames);
	free(path);
	return kernel;

failure:
	free(kernel);
	free(spans);
	free(entries);
	free(steps);
	free(frames);
	free(path);
	return NULL;
}

static const sg_belief_horizon_kernel_t *BeliefFindHorizonKernel(
	const sg_belief_frame_t *frame, uint64_t from_time_ms,
	uint64_t to_time_ms)
{
	size_t low = 0U;
	size_t high = frame->kernel_count;

	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		const sg_belief_horizon_kernel_t *kernel = &frame->kernels[middle];
		if (kernel->from_time_ms < from_time_ms ||
		    (kernel->from_time_ms == from_time_ms &&
		     kernel->to_time_ms < to_time_ms))
			low = middle + 1U;
		else
			high = middle;
	}
	if (low < frame->kernel_count &&
	    frame->kernels[low].from_time_ms == from_time_ms &&
	    frame->kernels[low].to_time_ms == to_time_ms)
		return &frame->kernels[low];
	return NULL;
}

static const sg_belief_horizon_span_t *BeliefHorizonSpan(
	const sg_belief_horizon_kernel_t *kernel,
	const sg_phase_coordinate_t *phase)
{
	if (!kernel)
		return NULL;
	return &kernel->origin_spans[phase->phase_id];
}

static int BeliefRangeDisjointFromAll(const belief_byte_range_t *range,
	const belief_byte_range_t *others, size_t other_count)
{
	size_t index;

	for (index = 0U; index < other_count; index++)
		if (BeliefRangesOverlap(range, &others[index]))
			return 0;
	return 1;
}

static int BeliefFrameMemoryValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, const sg_belief_frame_t *frame,
	const sg_belief_reduction_t *out)
{
	belief_byte_range_t writable[6];
	belief_byte_range_t read_range;
	size_t writable_count = 0U;
	size_t left;
	size_t right;

	if (!BeliefByteRange(state, 1U, sizeof(*state),
	    &writable[writable_count++]) ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &writable[writable_count++]))
		return 0;
	if (frame->scratch_capacity == 0U)
	{
		if (frame->scratch_first || frame->scratch_second)
			return 0;
	}
	else if (!BeliefByteRange(frame->scratch_first, frame->scratch_capacity,
	    sizeof(*frame->scratch_first), &writable[writable_count++]) ||
	    !BeliefByteRange(frame->scratch_second, frame->scratch_capacity,
		sizeof(*frame->scratch_second), &writable[writable_count++]))
		return 0;
	if (frame->commit_storage &&
	    !BeliefByteRange(frame->commit_storage, frame->commit_capacity,
		sizeof(*frame->commit_storage), &writable[writable_count++]))
		return 0;
	if (!BeliefByteRange(out, 1U, sizeof(*out),
	    &writable[writable_count++]))
		return 0;
	for (left = 0U; left < writable_count; left++)
		if (!BeliefRangeDisjointFromRune(snapshot, &writable[left]))
			return 0;
	for (left = 0U; left < writable_count; left++)
		for (right = left + 1U; right < writable_count; right++)
			if (BeliefRangesOverlap(&writable[left], &writable[right]))
				return 0;
	if (!BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count) ||
	    !BeliefByteRange(frame, 1U, sizeof(*frame), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
		return 0;
	if (frame->evidence_count != 0U)
	{
		if (!BeliefByteRange(frame->evidence, frame->evidence_count,
		    sizeof(*frame->evidence), &read_range) ||
		    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
			return 0;
		for (left = 0U; left < frame->evidence_count; left++)
			if (!BeliefByteRange(frame->evidence[left].supports,
			    frame->evidence[left].support_count,
			    sizeof(*frame->evidence[left].supports), &read_range) ||
			    !BeliefRangeDisjointFromAll(&read_range, writable,
				writable_count))
				return 0;
	}
	if (frame->kernel_count != 0U)
	{
		if (!BeliefByteRange(frame->kernels, frame->kernel_count,
		    sizeof(*frame->kernels), &read_range) ||
		    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
			return 0;
		for (left = 0U; left < frame->kernel_count; left++)
		{
			const sg_belief_horizon_kernel_t *kernel = &frame->kernels[left];
			if (!BeliefByteRange(kernel->origin_spans,
			    kernel->origin_span_count, sizeof(*kernel->origin_spans),
			    &read_range) ||
			    !BeliefRangeDisjointFromAll(&read_range, writable,
				writable_count) ||
			    !BeliefByteRange(kernel->entries, kernel->entry_count,
				sizeof(*kernel->entries), &read_range) ||
			    !BeliefRangeDisjointFromAll(&read_range, writable,
				writable_count))
				return 0;
			if (kernel->step_count != 0U &&
			    (!BeliefByteRange(kernel->steps, kernel->step_count,
				sizeof(*kernel->steps), &read_range) ||
			     !BeliefRangeDisjointFromAll(&read_range, writable,
				writable_count)))
				return 0;
		}
	}
	return 1;
}

static sg_belief_reduce_result_t BeliefPreflight(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, const sg_belief_frame_t *frame,
	size_t *required_scratch, belief_work_counters_t *counters)
{
	belief_byte_range_t kernel_range;
	belief_byte_range_t evidence_range;
	size_t index;
	size_t required;
	size_t advance_extra = 0U;
	uint64_t prior_evidence_sequence;
	if (frame &&
	    (frame->kernel_count > SIZE_MAX / sizeof(*frame->kernels) ||
	     frame->evidence_count > SIZE_MAX / sizeof(*frame->evidence)))
		return SG_BELIEF_REDUCE_OVERFLOW;

	if (!frame || !required_scratch || !counters || frame->sequence == 0U ||
	    frame->expected_revision == 0U || frame->expected_generation == 0U ||
	    frame->at_ms == 0U ||
	    frame->expected_revision != state->revision ||
	    frame->expected_generation != state->generation ||
	    frame->at_ms < state->updated_at_ms ||
	    (frame->kernel_count != 0U && !frame->kernels) ||
	    (frame->evidence_count != 0U && !frame->evidence) ||
	    (frame->kernel_count != 0U &&
	     !BeliefByteRange(frame->kernels, frame->kernel_count,
		sizeof(*frame->kernels), &kernel_range)) ||
	    (frame->evidence_count != 0U &&
	     !BeliefByteRange(frame->evidence, frame->evidence_count,
		sizeof(*frame->evidence), &evidence_range)) ||
	    (!frame->commit_storage && frame->commit_capacity != 0U) ||
	    (frame->commit_storage && frame->commit_capacity == 0U) ||
	    frame->commit_storage == state->particles ||
	    frame->commit_storage == frame->scratch_first ||
	    frame->commit_storage == frame->scratch_second)
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	if (frame->sequence < state->last_frame_sequence)
		return SG_BELIEF_REDUCE_REJECTED_STALE;
	for (index = 0U; index < frame->kernel_count; index++)
	{
		if (!BeliefHorizonKernelValid(snapshot, &frame->kernels[index],
		    counters))
		{
			if (counters->overflowed)
			{
				*required_scratch = SIZE_MAX;
				return SG_BELIEF_REDUCE_OVERFLOW;
			}
			return SG_BELIEF_REDUCE_REJECTED_INVALID;
		}
		if (index != 0U &&
		    (frame->kernels[index - 1U].from_time_ms >
		     frame->kernels[index].from_time_ms ||
		     (frame->kernels[index - 1U].from_time_ms ==
		      frame->kernels[index].from_time_ms &&
		      frame->kernels[index - 1U].to_time_ms >=
		      frame->kernels[index].to_time_ms)))
			return SG_BELIEF_REDUCE_REJECTED_INVALID;
	}
	prior_evidence_sequence = state->last_evidence_sequence;
	for (index = 0U; index < frame->evidence_count; index++)
	{
		sg_belief_reduce_result_t valid = BeliefEvidenceValid(snapshot, state,
			&frame->evidence[index], frame->at_ms);
		if (valid != SG_BELIEF_REDUCE_APPLIED)
			return valid;
		if (frame->evidence[index].provenance.evidence_sequence <=
		    prior_evidence_sequence)
			return SG_BELIEF_REDUCE_REJECTED_STALE;
		prior_evidence_sequence =
			frame->evidence[index].provenance.evidence_sequence;
	}
	required = state->particle_count;
	if (state->particle_count != 0U && frame->at_ms > state->updated_at_ms &&
	    state->policy.diffusion_fraction > 0.0f)
	{
		const sg_belief_horizon_kernel_t *kernel = BeliefFindHorizonKernel(
			frame, state->updated_at_ms, frame->at_ms);
		advance_extra = 0U;
		if (kernel)
			for (index = 0U; index < state->particle_count; index++)
				if (!BeliefSizeAdd(advance_extra,
				    BeliefHorizonSpan(kernel,
					&state->particles[index].phase)->entry_count,
				    &advance_extra))
				{
					*required_scratch = SIZE_MAX;
					return SG_BELIEF_REDUCE_OVERFLOW;
				}
		if (!BeliefSizeAdd(required, advance_extra, &required))
		{
			*required_scratch = SIZE_MAX;
			return SG_BELIEF_REDUCE_OVERFLOW;
		}
	}
	for (index = 0U; index < frame->evidence_count; index++)
		if (frame->evidence[index].kind == SG_BELIEF_EVIDENCE_POSITIVE)
		{
			const sg_belief_evidence_t *evidence = &frame->evidence[index];
			const sg_belief_horizon_kernel_t *kernel = NULL;
			size_t support;
			advance_extra = evidence->support_count;
			if (evidence->observed_at_ms < frame->at_ms &&
			    state->policy.diffusion_fraction > 0.0f)
			{
				kernel = BeliefFindHorizonKernel(frame,
					evidence->observed_at_ms, frame->at_ms);
				if (kernel)
					for (support = 0U;
					     support < evidence->support_count; support++)
						if (!BeliefSizeAdd(advance_extra,
						    BeliefHorizonSpan(kernel,
							&evidence->supports[support].phase)->
							entry_count,
						    &advance_extra))
						{
							*required_scratch = SIZE_MAX;
							return SG_BELIEF_REDUCE_OVERFLOW;
						}
			}
			if (!BeliefSizeAdd(required, advance_extra, &required))
			{
				*required_scratch = SIZE_MAX;
				return SG_BELIEF_REDUCE_OVERFLOW;
			}
		}
	*required_scratch = required;
	if (required != 0U && (!frame->scratch_first || !frame->scratch_second ||
	    frame->scratch_first == frame->scratch_second ||
	    frame->scratch_first == state->particles ||
	    frame->scratch_second == state->particles ||
	    frame->scratch_capacity < required))
		return SG_BELIEF_REDUCE_CAPACITY;
	return SG_BELIEF_REDUCE_APPLIED;
}

static void BeliefCopyParticle(sg_belief_particle_t *out,
	const sg_belief_particle_t *source)
{
	uint8_t axis;

	memset(out, 0, sizeof(*out));
	out->phase = source->phase;
	out->movement_state = source->movement_state;
	out->weapon_state = source->weapon_state;
	out->source_mask = source->source_mask;
	out->future_time_ms = source->future_time_ms;
	out->latest_evidence_id = source->latest_evidence_id;
	out->latest_evidence_at_ms = source->latest_evidence_at_ms;
	for (axis = 0U; axis < 3U; axis++)
	{
		out->position[axis] = source->position[axis];
		out->velocity[axis] = source->velocity[axis];
		out->acceleration[axis] = source->acceleration[axis];
		out->orientation[axis] = source->orientation[axis];
	}
	out->spread_radius = source->spread_radius;
	out->weight = source->weight;
}

static int BeliefParticleSameMode(const sg_belief_particle_t *left,
	const sg_belief_particle_t *right)
{
	uint8_t axis;

	if (left->phase.phase_id != right->phase.phase_id ||
	    left->phase.cell_id != right->phase.cell_id ||
	    left->movement_state != right->movement_state ||
	    left->weapon_state != right->weapon_state ||
	    left->future_time_ms != right->future_time_ms ||
	    left->spread_radius != right->spread_radius)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->position[axis] != right->position[axis] ||
		    left->velocity[axis] != right->velocity[axis] ||
		    left->acceleration[axis] != right->acceleration[axis] ||
		    left->orientation[axis] != right->orientation[axis])
			return 0;
	return 1;
}

static int BeliefAppendParticle(sg_belief_particle_t *particles,
	size_t *count, const sg_belief_particle_t *particle)
{
	size_t index;

	if (!SG_BeliefFloatValid(particle->weight) || particle->weight < 0.0f)
		return 0;
	if (particle->weight == 0.0f)
		return 1;
	for (index = 0U; index < *count; index++)
		if (BeliefParticleSameMode(&particles[index], particle))
		{
			particles[index].weight += particle->weight;
			if (!SG_BeliefFloatValid(particles[index].weight))
				return 0;
			particles[index].source_mask |= particle->source_mask;
			if (particle->latest_evidence_at_ms >=
			    particles[index].latest_evidence_at_ms)
			{
				particles[index].latest_evidence_id =
					particle->latest_evidence_id;
				particles[index].latest_evidence_at_ms =
					particle->latest_evidence_at_ms;
			}
			return 1;
		}
	BeliefCopyParticle(&particles[*count], particle);
	(*count)++;
	return 1;
}

static int BeliefWeightProduct(float weight, float first_factor,
	float second_factor, float *out)
{
	double product;
	float represented;

	if (!out || !SG_BeliefFloatValid(weight) || weight <= 0.0f ||
	    !SG_BeliefFloatValid(first_factor) || first_factor <= 0.0f ||
	    !SG_BeliefFloatValid(second_factor) || second_factor <= 0.0f)
		return 0;
	product = (double)weight * (double)first_factor *
		(double)second_factor;
	if (!isfinite(product) || product <= 0.0 || product > (double)FLT_MAX)
		return 0;
	represented = (float)product;
	if (represented == 0.0f)
		represented = FLT_TRUE_MIN;
	if (!SG_BeliefFloatValid(represented) || represented <= 0.0f)
		return 0;
	*out = represented;
	return 1;
}

static int BeliefIntegrateParticle(
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_particle_t *particle, uint64_t elapsed_ms, uint64_t at_ms,
	float spread_growth)
{
	uint8_t axis;
	float seconds = (float)elapsed_ms / 1000.0f;
	float half_seconds_squared = 0.5f * seconds * seconds;

	for (axis = 0U; axis < 3U; axis++)
	{
		particle->position[axis] += particle->velocity[axis] * seconds +
			particle->acceleration[axis] * half_seconds_squared;
		particle->velocity[axis] += particle->acceleration[axis] * seconds;
		if (!SG_BeliefFloatValid(particle->position[axis]) ||
		    !SG_BeliefFloatValid(particle->velocity[axis]))
			return 0;
	}
	particle->spread_radius += spread_growth * (float)elapsed_ms;
	if (!SG_BeliefFloatValid(particle->spread_radius))
		return 0;
	particle->future_time_ms = at_ms;
	return SG_BeliefPositionInsidePhaseCell(snapshot, &particle->phase,
		particle->position) &&
		SG_BeliefKinematicsCompatible(snapshot, &particle->phase,
			particle->movement_state, particle->velocity,
			particle->acceleration, particle->orientation);
}

static sg_belief_motion_state_t BeliefMovementAtPhase(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase,
	sg_belief_motion_state_t prior)
{
	const sg_rune_phase_basis_t *basis = &snapshot->model->phases[phase->phase_id];

	if (basis->motion == SG_RUNE_MOTION_SWIMMING)
		return SG_BELIEF_MOTION_WATER;
	if (basis->motion == SG_RUNE_MOTION_AIRBORNE)
		return prior == SG_BELIEF_MOTION_HOOK ?
			SG_BELIEF_MOTION_HOOK : SG_BELIEF_MOTION_AIR;
	if (basis->support == SG_RUNE_SUPPORT_MOVER ||
	    basis->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE)
		return SG_BELIEF_MOTION_MOVER;
	return SG_BELIEF_MOTION_GROUND;
}

static sg_belief_motion_state_t BeliefMovementAfterStep(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase,
	const belief_step_bounds_t *bounds, sg_belief_motion_state_t prior)
{
	sg_belief_motion_state_t movement_state = SG_BELIEF_MOTION_COUNT;

	switch (bounds->capability_family)
	{
	case SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT:
		movement_state = SG_BELIEF_MOTION_GROUND;
		break;
	case SG_RUNE_CAPABILITY_AIRBORNE_CONTROL:
		movement_state = SG_BELIEF_MOTION_AIR;
		break;
	case SG_RUNE_CAPABILITY_WATER_VOLUME:
		movement_state = SG_BELIEF_MOTION_WATER;
		break;
	case SG_RUNE_CAPABILITY_HOOK_TRAJECTORY:
		movement_state = SG_BELIEF_MOTION_HOOK;
		break;
	case SG_RUNE_CAPABILITY_MECHANISM_CROSSING:
	case SG_RUNE_CAPABILITY_EXTERNAL_FORCE:
	case SG_RUNE_CAPABILITY_FAMILY_COUNT:
		movement_state = BeliefMovementAtPhase(snapshot, phase, prior);
		break;
	}
	return SG_BeliefMotionStateCompatible(snapshot, phase, movement_state) ?
		movement_state : SG_BELIEF_MOTION_COUNT;
}

static int BeliefKinematicsWithinStep(
	const belief_step_bounds_t *bounds,
	const sg_belief_particle_t *particle)
{
	double speed_squared = 0.0;
	size_t axis;

	if (!bounds->constrains_kinematics)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		speed_squared += (double)particle->velocity[axis] *
			(double)particle->velocity[axis];
	return isfinite(speed_squared) &&
		speed_squared <= (double)bounds->speed_max *
			(double)bounds->speed_max &&
		SG_BeliefAccelerationWithinLimits(particle->acceleration,
			bounds->acceleration_max, bounds->vertical_acceleration_max,
			bounds->gravity);
}

static int BeliefEntryKinematicsValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	const sg_belief_horizon_entry_t *entry,
	const sg_belief_particle_t *particle,
	sg_belief_motion_state_t *result_movement_state)
{
	sg_belief_particle_t cursor;
	size_t end;
	size_t index;

	if (!result_movement_state ||
	    !BeliefSizeAdd(entry->first_step, entry->step_count, &end))
		return 0;
	BeliefCopyParticle(&cursor, particle);
	for (index = entry->first_step; index < end; index++)
	{
		belief_step_bounds_t bounds;
		const sg_belief_horizon_step_t *step = &kernel->steps[index];

		if (cursor.phase.phase_id != step->from.phase_id ||
		    cursor.phase.cell_id != step->from.cell_id)
			return 0;
		if (!SG_BeliefKinematicsCompatible(snapshot, &step->from,
		    cursor.movement_state, cursor.velocity, cursor.acceleration,
		    cursor.orientation) ||
		    !BeliefRuneRecordMatches(snapshot, step->kind,
		    step->record_index, &step->from, &step->to, &bounds) ||
		    !BeliefKinematicsWithinStep(&bounds, &cursor))
			return 0;
		cursor.phase = step->to;
		cursor.movement_state = BeliefMovementAfterStep(snapshot,
			&cursor.phase, &bounds, cursor.movement_state);
		if (cursor.movement_state == SG_BELIEF_MOTION_COUNT)
			return 0;
	}
	*result_movement_state = cursor.movement_state;
	return 1;
}

static int BeliefEntryDisplacementUncertainty(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	const sg_belief_horizon_entry_t *entry, float *uncertainty_out)
{
	double total = 0.0;
	size_t end;
	size_t index;

	if (!uncertainty_out ||
	    !BeliefSizeAdd(entry->first_step, entry->step_count, &end))
		return 0;
	for (index = entry->first_step; index < end; index++)
	{
		belief_step_bounds_t bounds;
		const sg_belief_horizon_step_t *step = &kernel->steps[index];
		double squared = 0.0;
		size_t axis;

		if (!BeliefRuneRecordMatches(snapshot, step->kind,
		    step->record_index, &step->from, &step->to, &bounds))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
		{
			double half_width = ((double)bounds.displacement_max[axis] -
				(double)bounds.displacement_min[axis]) * 0.5;
			squared += half_width * half_width;
		}
		total += sqrt(squared);
		if (!isfinite(total) || total > (double)FLT_MAX)
			return 0;
	}
	*uncertainty_out = (float)total;
	return SG_BeliefFloatValid(*uncertainty_out);
}

static int BeliefMoveByHorizonEntry(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	const sg_belief_horizon_entry_t *entry,
	const sg_belief_particle_t *source, uint64_t elapsed_ms, uint64_t at_ms,
	float spread_growth, sg_belief_particle_t *moved)
{
	sg_belief_motion_state_t result_movement_state;
	float displacement_uncertainty;
	size_t axis;

	if (!BeliefEntryKinematicsValid(snapshot, kernel, entry, source,
	    &result_movement_state) ||
	    !BeliefEntryDisplacementUncertainty(snapshot, kernel, entry,
		&displacement_uncertainty))
		return 0;
	BeliefCopyParticle(moved, source);
	moved->phase = entry->to;
	moved->movement_state = result_movement_state;
	for (axis = 0U; axis < 3U; axis++)
	{
		moved->position[axis] = source->position[axis] +
			entry->displacement[axis];
		if (!SG_BeliefFloatValid(moved->position[axis]))
			return 0;
	}
	moved->spread_radius += spread_growth * (float)elapsed_ms +
		displacement_uncertainty;
	if (!SG_BeliefFloatValid(moved->spread_radius))
		return 0;
	moved->future_time_ms = at_ms;
	return SG_BeliefPositionInsidePhaseCell(snapshot, &moved->phase,
		moved->position) &&
		SG_BeliefKinematicsCompatible(snapshot, &moved->phase,
		moved->movement_state, moved->velocity, moved->acceleration,
		moved->orientation);
}

static int BeliefNormalize(sg_belief_particle_t *particles, size_t *count)
{
	size_t index;
	size_t write = 0U;
	size_t largest = 0U;
	double scaled_total = 0.0;
	double other_total = 0.0;
	float maximum = 0.0f;

	for (index = 0U; index < *count; index++)
	{
		if (!SG_BeliefFloatValid(particles[index].weight) ||
		    particles[index].weight < 0.0f)
			return 0;
		if (particles[index].weight > 0.0f)
		{
			if (write != index)
				particles[write] = particles[index];
			if (particles[write].weight > maximum)
			{
				maximum = particles[write].weight;
				largest = write;
			}
			write++;
		}
	}
	*count = write;
	if (write == 0U)
		return 1;
	if (maximum <= 0.0f)
		return 0;
	for (index = 0U; index < write; index++)
	{
		double scaled = (double)particles[index].weight / (double)maximum;
		float represented = (float)scaled;
		if (!isfinite(scaled) || scaled <= 0.0)
			return 0;
		if (represented == 0.0f)
			represented = FLT_TRUE_MIN;
		particles[index].weight = represented;
		scaled_total += (double)represented;
	}
	if (!isfinite(scaled_total) || scaled_total <= 0.0)
		return 0;
	for (index = 0U; index < write; index++)
	{
		float normalized;
		if (index == largest)
			continue;
		normalized = (float)((double)particles[index].weight / scaled_total);
		if (normalized == 0.0f)
			normalized = FLT_TRUE_MIN;
		if (!SG_BeliefFloatValid(normalized) || normalized <= 0.0f)
			return 0;
		particles[index].weight = normalized;
		other_total += (double)normalized;
	}
	particles[largest].weight = (float)(1.0 - other_total);
	if (!SG_BeliefFloatValid(particles[largest].weight) ||
	    particles[largest].weight <= 0.0f || particles[largest].weight > 1.0f)
		return 0;
	return 1;
}

static int BeliefAdvance(const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_state_t *candidate,
	const sg_belief_frame_t *frame, sg_belief_particle_t **current,
	sg_belief_particle_t **next, size_t *count,
	belief_work_counters_t *counters)
{
	uint64_t elapsed_ms = frame->at_ms - candidate->updated_at_ms;
	const sg_belief_horizon_kernel_t *kernel = NULL;
	size_t index;
	size_t write = 0U;

	if (elapsed_ms == 0U)
		return 1;
	candidate->confidence *= expf(-(float)elapsed_ms /
		(float)candidate->policy.confidence_decay_ms);
	if (candidate->confidence <= 0.0f || *count == 0U)
	{
		*count = 0U;
		candidate->confidence = 0.0f;
		candidate->updated_at_ms = frame->at_ms;
		return 1;
	}
	if (candidate->policy.diffusion_fraction > 0.0f)
		kernel = BeliefFindHorizonKernel(frame, candidate->updated_at_ms,
			frame->at_ms);
	for (index = 0U; index < *count; index++)
	{
		const sg_belief_particle_t *source = &(*current)[index];
		const sg_belief_horizon_span_t *span =
			BeliefHorizonSpan(kernel, &source->phase);
		size_t offset;
		if (!kernel || candidate->policy.diffusion_fraction < 1.0f)
		{
			sg_belief_particle_t retained;
			BeliefCopyParticle(&retained, source);
			if (!BeliefIntegrateParticle(snapshot, &retained, elapsed_ms,
			    frame->at_ms, candidate->policy.spread_growth_per_ms))
				return 0;
			if (kernel && !BeliefWeightProduct(retained.weight,
			    1.0f - candidate->policy.diffusion_fraction, 1.0f,
			    &retained.weight))
				return 0;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
		}
		if (kernel)
			for (offset = 0U; offset < span->entry_count; offset++)
				{
					sg_belief_particle_t moved;
					const sg_belief_horizon_entry_t *entry =
						&kernel->entries[span->first_entry + offset];
					if (!BeliefMoveByHorizonEntry(snapshot, kernel, entry,
					    source, elapsed_ms, frame->at_ms,
					    candidate->policy.spread_growth_per_ms, &moved))
						return 0;
					if (!BeliefWeightProduct(moved.weight,
					    candidate->policy.diffusion_fraction,
					    entry->likelihood, &moved.weight))
						return 0;
					if (!BeliefCounterIncrement(&counters->evaluated_outcomes,
					    counters))
						return 0;
					if (!BeliefAppendParticle(*next, &write, &moved))
						return 0;
				}
	}
	if (!BeliefNormalize(*next, &write))
		return 0;
	{
		sg_belief_particle_t *swap = *current;
		*current = *next;
		*next = swap;
	}
	*count = write;
	candidate->updated_at_ms = frame->at_ms;
	return 1;
}

static float BeliefSpatialOverlap(const sg_belief_particle_t *region,
	const sg_belief_particle_t *particle)
{
	uint8_t axis;
	float distance_squared = 0.0f;
	float radius;

	if (region->phase.phase_id != particle->phase.phase_id ||
	    region->phase.cell_id != particle->phase.cell_id)
		return 0.0f;
	for (axis = 0U; axis < 3U; axis++)
	{
		float difference = region->position[axis] - particle->position[axis];
		distance_squared += difference * difference;
	}
	if (!SG_BeliefFloatValid(distance_squared))
		return -1.0f;
	radius = region->spread_radius + particle->spread_radius;
	if (radius == 0.0f)
		return distance_squared == 0.0f ? 1.0f : 0.0f;
	if (!SG_BeliefFloatValid(radius * radius) ||
	    distance_squared >= radius * radius)
		return 0.0f;
	if (distance_squared <= SG_BELIEF_WEIGHT_EPSILON * radius * radius)
		return 1.0f;
	return 1.0f - sqrtf(distance_squared) / radius;
}

static void BeliefParticleFromSupport(sg_belief_particle_t *particle,
	const sg_belief_evidence_t *evidence,
	const sg_belief_evidence_support_t *support, float weight)
{
	uint8_t axis;

	memset(particle, 0, sizeof(*particle));
	particle->phase = support->phase;
	particle->movement_state = support->movement_state;
	particle->weapon_state = support->weapon_state;
	particle->source_mask = (uint16_t)(UINT16_C(1) << evidence->source);
	particle->future_time_ms = evidence->observed_at_ms;
	particle->latest_evidence_id = evidence->provenance.evidence_id;
	particle->latest_evidence_at_ms = evidence->observed_at_ms;
	for (axis = 0U; axis < 3U; axis++)
	{
		particle->position[axis] = support->position[axis];
		particle->velocity[axis] = support->velocity[axis];
		particle->acceleration[axis] = support->acceleration[axis];
		particle->orientation[axis] = support->orientation[axis];
	}
	particle->spread_radius = support->spread_radius;
	particle->weight = weight;
}

static float BeliefNegativeOverlap(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *candidate,
	const sg_belief_evidence_t *evidence,
	const sg_belief_evidence_support_t *support,
	const sg_belief_frame_t *frame, const sg_belief_particle_t *particle,
	belief_work_counters_t *counters)
{
	uint64_t delay_ms = frame->at_ms - evidence->observed_at_ms;
	const sg_belief_horizon_kernel_t *kernel = NULL;
	sg_belief_particle_t source;
	sg_belief_particle_t region;
	float overlap;
	float total = 0.0f;
	const sg_belief_horizon_span_t *span;
	size_t offset;

	BeliefParticleFromSupport(&source, evidence, support, 1.0f);
	if (delay_ms > 0U && candidate->policy.diffusion_fraction > 0.0f)
		kernel = BeliefFindHorizonKernel(frame, evidence->observed_at_ms,
			frame->at_ms);
	if (!kernel || candidate->policy.diffusion_fraction < 1.0f)
	{
		BeliefCopyParticle(&region, &source);
		if (!BeliefIntegrateParticle(snapshot, &region, delay_ms,
		    frame->at_ms, candidate->policy.spread_growth_per_ms))
			return -1.0f;
		overlap = BeliefSpatialOverlap(&region, particle);
		if (overlap < 0.0f)
			return -1.0f;
		total = overlap * (kernel ?
			1.0f - candidate->policy.diffusion_fraction : 1.0f);
	}
	if (kernel)
	{
		span = BeliefHorizonSpan(kernel, &support->phase);
		for (offset = 0U; offset < span->entry_count; offset++)
			{
				sg_belief_particle_t moved;
				const sg_belief_horizon_entry_t *entry =
					&kernel->entries[span->first_entry + offset];
				float moved_overlap;
				if (!BeliefMoveByHorizonEntry(snapshot, kernel, entry,
				    &source, delay_ms, frame->at_ms,
				    candidate->policy.spread_growth_per_ms, &moved))
					return -1.0f;
				moved_overlap = BeliefSpatialOverlap(&moved, particle);
				if (moved_overlap < 0.0f)
					return -1.0f;
				total += candidate->policy.diffusion_fraction *
					entry->likelihood * moved_overlap;
				if (!BeliefCounterIncrement(&counters->evaluated_outcomes,
				    counters))
					return -1.0f;
			}
	}
	if (!SG_BeliefFloatValid(total))
		return -1.0f;
	return total > 1.0f ? 1.0f : total;
}

static int BeliefPositiveModeWeight(float confidence, float likelihood,
	double support_total, float *weight)
{
	double normalized;
	float represented;

	if (!weight || !SG_BeliefFloatValid(confidence) || confidence <= 0.0f ||
	    !SG_BeliefFloatValid(likelihood) || likelihood <= 0.0f ||
	    !isfinite(support_total) || support_total <= 0.0)
		return 0;
	normalized = (double)confidence * (double)likelihood / support_total;
	if (!isfinite(normalized) || normalized <= 0.0)
		return 0;
	represented = (float)normalized;
	if (represented == 0.0f)
		represented = FLT_TRUE_MIN;
	if (!SG_BeliefFloatValid(represented) || represented <= 0.0f)
		return 0;
	*weight = represented;
	return 1;
}

static int BeliefApplyPositive(const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_state_t *candidate,
	const sg_belief_evidence_t *evidence, const sg_belief_frame_t *frame,
	sg_belief_particle_t **current, sg_belief_particle_t **next, size_t *count,
	belief_work_counters_t *counters)
{
	size_t index;
	size_t write = 0U;
	double support_total = 0.0;
	uint64_t delay_ms = frame->at_ms - evidence->observed_at_ms;
	const sg_belief_horizon_kernel_t *kernel = NULL;
	float confidence = evidence->confidence * expf(-(float)delay_ms /
		(float)candidate->policy.confidence_decay_ms);
	float old_scale = 1.0f - confidence;

	if (!SG_BeliefFloatValid(confidence) || confidence <= 0.0f)
		return 0;
	for (index = 0U; index < evidence->support_count; index++)
		support_total += (double)evidence->supports[index].likelihood;
	if (!isfinite(support_total) || support_total <= 0.0)
		return 0;
	if (delay_ms > 0U && candidate->policy.diffusion_fraction > 0.0f)
		kernel = BeliefFindHorizonKernel(frame, evidence->observed_at_ms,
			frame->at_ms);
	for (index = 0U; index < *count; index++)
		if (old_scale > 0.0f)
		{
			sg_belief_particle_t retained;
			BeliefCopyParticle(&retained, &(*current)[index]);
			if (!BeliefWeightProduct(retained.weight, old_scale, 1.0f,
			    &retained.weight))
				return 0;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
		}
	for (index = 0U; index < evidence->support_count; index++)
	{
		const sg_belief_horizon_span_t *span =
			BeliefHorizonSpan(kernel, &evidence->supports[index].phase);
		size_t offset;
		float weight;
		if (!BeliefPositiveModeWeight(confidence,
		    evidence->supports[index].likelihood, support_total, &weight))
			return 0;
		if (candidate->policy.diffusion_fraction < 1.0f || !kernel)
		{
			sg_belief_particle_t retained;
			BeliefParticleFromSupport(&retained, evidence,
				&evidence->supports[index], weight);
			if (!BeliefIntegrateParticle(snapshot, &retained, delay_ms,
			    frame->at_ms,
			    candidate->policy.spread_growth_per_ms))
				return 0;
			if (kernel && !BeliefWeightProduct(retained.weight,
			    1.0f - candidate->policy.diffusion_fraction, 1.0f,
			    &retained.weight))
				return 0;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
		}
		if (kernel)
			for (offset = 0U; offset < span->entry_count; offset++)
				{
					sg_belief_particle_t observed;
					sg_belief_particle_t moved;
					const sg_belief_horizon_entry_t *entry =
						&kernel->entries[span->first_entry + offset];
					BeliefParticleFromSupport(&observed, evidence,
						&evidence->supports[index], weight);
					if (!BeliefMoveByHorizonEntry(snapshot, kernel, entry,
					    &observed, delay_ms, frame->at_ms,
					    candidate->policy.spread_growth_per_ms, &moved))
						return 0;
					if (!BeliefWeightProduct(moved.weight,
					    candidate->policy.diffusion_fraction,
					    entry->likelihood, &moved.weight))
						return 0;
					if (!BeliefCounterIncrement(&counters->evaluated_outcomes,
					    counters))
						return 0;
					if (!BeliefAppendParticle(*next, &write, &moved))
						return 0;
				}
	}
	if (!BeliefNormalize(*next, &write))
		return 0;
	candidate->confidence = candidate->confidence + confidence *
		(1.0f - candidate->confidence);
	{
		sg_belief_particle_t *swap = *current;
		*current = *next;
		*next = swap;
	}
	*count = write;
	return 1;
}

static int BeliefApplyNegative(
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_state_t *candidate,
	const sg_belief_evidence_t *evidence, const sg_belief_frame_t *frame,
	sg_belief_particle_t **current, sg_belief_particle_t **next, size_t *count,
	belief_work_counters_t *counters)
{
	size_t index;
	size_t write = 0U;
	uint64_t delay_ms = frame->at_ms - evidence->observed_at_ms;
	float confidence = evidence->confidence * expf(-(float)delay_ms /
		(float)candidate->policy.confidence_decay_ms);

	for (index = 0U; index < *count; index++)
	{
		size_t support;
		float excluded = 0.0f;
		for (support = 0U; support < evidence->support_count; support++)
		{
			float overlap = BeliefNegativeOverlap(snapshot, candidate, evidence,
				&evidence->supports[support], frame,
				&(*current)[index], counters);
			if (overlap < 0.0f)
				return 0;
			overlap *= evidence->supports[support].likelihood;
			if (overlap > excluded)
				excluded = overlap;
		}
		{
			float factor = 1.0f - confidence * excluded;
			if (!SG_BeliefFloatValid(factor) || factor < 0.0f)
				return 0;
			if (factor > 0.0f)
			{
				sg_belief_particle_t retained;
				BeliefCopyParticle(&retained, &(*current)[index]);
				if (!BeliefWeightProduct(retained.weight, factor, 1.0f,
				    &retained.weight))
					return 0;
				retained.source_mask |=
					(uint16_t)(UINT16_C(1) << evidence->source);
				retained.latest_evidence_id =
					evidence->provenance.evidence_id;
				retained.latest_evidence_at_ms = evidence->observed_at_ms;
				if (!BeliefAppendParticle(*next, &write, &retained))
					return 0;
			}
		}
	}
	if (!BeliefNormalize(*next, &write))
		return 0;
	if (write == 0U)
		candidate->confidence = 0.0f;
	{
		sg_belief_particle_t *swap = *current;
		*current = *next;
		*next = swap;
	}
	*count = write;
	return 1;
}

int SG_BeliefStateInit(const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_state_t *state, const sg_belief_state_config_t *config,
	sg_belief_particle_t *storage, size_t capacity)
{
	sg_belief_state_t candidate;
	belief_byte_range_t state_range;
	belief_byte_range_t config_range;
	belief_byte_range_t snapshot_range;
	belief_byte_range_t storage_range;

	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !state || !config || !storage ||
	    capacity == 0U || !SG_BeliefTeamValid(config->audience_team) ||
	    !SG_BeliefTeamValid(config->target_team) ||
	    !SG_BeliefReservedZero(config->reserved, sizeof(config->reserved)) ||
	    !SG_BeliefLifeIdentityValid(&config->target_life) ||
	    config->initialized_at_ms == 0U || !BeliefPolicyValid(&config->policy) ||
	    !BeliefByteRange(state, 1U, sizeof(*state), &state_range) ||
	    !BeliefByteRange(config, 1U, sizeof(*config), &config_range) ||
	    !BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &snapshot_range) ||
	    !BeliefByteRange(storage, capacity, sizeof(*storage), &storage_range) ||
	    !BeliefRangeDisjointFromRune(snapshot, &state_range) ||
	    !BeliefRangeDisjointFromRune(snapshot, &storage_range) ||
	    BeliefRangesOverlap(&state_range, &config_range) ||
	    BeliefRangesOverlap(&state_range, &snapshot_range) ||
	    BeliefRangesOverlap(&state_range, &storage_range))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.audience_team = config->audience_team;
	candidate.target_team = config->target_team;
	candidate.target_life = config->target_life;
	candidate.particle_capacity = capacity;
	candidate.generation = 1U;
	candidate.revision = 1U;
	candidate.rune_identity = snapshot->identity;
	candidate.topology_revision = snapshot->topology_revision;
	candidate.updated_at_ms = config->initialized_at_ms;
	candidate.policy = config->policy;
	candidate.particles = storage;
	*state = candidate;
	return 1;
}

sg_belief_reduce_result_t SG_BeliefReduce(
	const sg_rune_runtime_snapshot_t *snapshot, sg_belief_state_t *state,
	const sg_belief_frame_t *frame, sg_belief_reduction_t *out)
{
	sg_belief_reduction_t reduction;
	sg_belief_reduce_result_t preflight;
	sg_belief_state_t candidate;
	sg_belief_particle_t *current;
	sg_belief_particle_t *next;
	size_t count;
	size_t required_scratch = 0U;
	size_t index;
	belief_work_counters_t counters;
	belief_byte_range_t out_range;
	belief_byte_range_t state_range;
	belief_byte_range_t state_storage_range;
	belief_byte_range_t frame_range;
	belief_byte_range_t snapshot_range;

	if (!snapshot || !state || !frame || !out ||
	    !BeliefByteRange(out, 1U, sizeof(*out), &out_range) ||
	    !BeliefByteRange(state, 1U, sizeof(*state), &state_range) ||
	    !BeliefByteRange(frame, 1U, sizeof(*frame), &frame_range) ||
	    !BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &snapshot_range) ||
	    BeliefRangesOverlap(&out_range, &state_range) ||
	    BeliefRangesOverlap(&out_range, &frame_range) ||
	    BeliefRangesOverlap(&out_range, &snapshot_range) ||
	    !BeliefStateBoundToSnapshot(snapshot, state) ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &state_storage_range) ||
	    !BeliefRangeDisjointFromRune(snapshot, &out_range) ||
	    !BeliefRangeDisjointFromRune(snapshot, &state_range) ||
	    !BeliefRangeDisjointFromRune(snapshot, &state_storage_range) ||
	    BeliefRangesOverlap(&out_range, &state_storage_range))
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	if (frame->kernel_count > SIZE_MAX / sizeof(*frame->kernels) ||
	    frame->evidence_count > SIZE_MAX / sizeof(*frame->evidence))
		return SG_BELIEF_REDUCE_OVERFLOW;
	if (!BeliefFrameMemoryValid(snapshot, state, frame, out))
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	memset(&reduction, 0, sizeof(reduction));
	memset(&counters, 0, sizeof(counters));
	reduction.committed_revision = state->revision;
	reduction.particle_count = state->particle_count;
	reduction.confidence = state->confidence;
	if (state->last_frame_sequence != 0U &&
	    frame->sequence == state->last_frame_sequence)
	{
		if (frame->expected_revision == UINT64_MAX ||
		    frame->expected_generation == UINT64_MAX ||
		    frame->expected_revision + 1U != state->revision ||
		    frame->expected_generation + 1U != state->generation ||
		    frame->at_ms != state->updated_at_ms)
			return SG_BELIEF_REDUCE_REJECTED_INVALID;
		reduction.result = SG_BELIEF_REDUCE_DUPLICATE;
		*out = reduction;
		return reduction.result;
	}
	preflight = BeliefPreflight(snapshot, state, frame, &required_scratch,
		&counters);
	if (preflight != SG_BELIEF_REDUCE_APPLIED)
	{
		reduction.result = preflight;
		if (preflight == SG_BELIEF_REDUCE_CAPACITY ||
		    preflight == SG_BELIEF_REDUCE_OVERFLOW)
			reduction.required_scratch_capacity = required_scratch;
		BeliefReportCounters(&reduction, &counters);
		*out = reduction;
		return reduction.result;
	}
	current = frame->scratch_first;
	next = frame->scratch_second;
	count = state->particle_count;
	for (index = 0U; index < count; index++)
		BeliefCopyParticle(&current[index], &state->particles[index]);
	candidate = *state;
	candidate.particles = current;
	if (!BeliefAdvance(snapshot, &candidate, frame, &current, &next, &count,
	    &counters))
	{
		reduction.result = counters.overflowed ?
			SG_BELIEF_REDUCE_OVERFLOW : SG_BELIEF_REDUCE_REJECTED_INVALID;
		BeliefReportCounters(&reduction, &counters);
		*out = reduction;
		return reduction.result;
	}
	for (index = 0U; index < frame->evidence_count; index++)
	{
		const sg_belief_evidence_t *evidence = &frame->evidence[index];
		int applied;
		if (evidence->kind == SG_BELIEF_EVIDENCE_POSITIVE)
			applied = BeliefApplyPositive(snapshot, &candidate, evidence, frame,
				&current, &next, &count, &counters);
		else
			applied = BeliefApplyNegative(snapshot, &candidate, evidence, frame,
				&current,
				&next, &count, &counters);
		if (!applied)
		{
			reduction.result = counters.overflowed ?
				SG_BELIEF_REDUCE_OVERFLOW :
				SG_BELIEF_REDUCE_REJECTED_INVALID;
			BeliefReportCounters(&reduction, &counters);
			*out = reduction;
			return reduction.result;
		}
		candidate.last_evidence_sequence =
			evidence->provenance.evidence_sequence;
		candidate.latest_provenance = evidence->provenance;
		candidate.latest_source = evidence->source;
		candidate.latest_observed_at_ms = evidence->observed_at_ms;
		candidate.latest_valid_until_ms = evidence->valid_until_ms;
		candidate.latest_evidence_confidence = evidence->confidence;
	}
	{
		sg_belief_particle_t *commit_storage = frame->commit_storage ?
			frame->commit_storage : state->particles;
		size_t commit_capacity = frame->commit_storage ?
			frame->commit_capacity : state->particle_capacity;
		if (count > commit_capacity)
		{
			reduction.result = SG_BELIEF_REDUCE_CAPACITY;
			reduction.required_particle_capacity = count;
			reduction.required_scratch_capacity = required_scratch;
			BeliefReportCounters(&reduction, &counters);
			*out = reduction;
			return reduction.result;
		}
		if (candidate.revision == UINT64_MAX ||
		    candidate.generation == UINT64_MAX)
		{
			reduction.result = SG_BELIEF_REDUCE_OVERFLOW;
			BeliefReportCounters(&reduction, &counters);
			*out = reduction;
			return reduction.result;
		}
		candidate.particles = current;
		candidate.particle_capacity = commit_capacity;
		candidate.particle_count = count;
		candidate.total_weight = count == 0U ? 0.0f : 1.0f;
		candidate.last_frame_sequence = frame->sequence;
		candidate.updated_at_ms = frame->at_ms;
		if (!BeliefStateBoundToSnapshot(snapshot, &candidate))
		{
			reduction.result = SG_BELIEF_REDUCE_REJECTED_INVALID;
			BeliefReportCounters(&reduction, &counters);
			*out = reduction;
			return reduction.result;
		}
		for (index = 0U; index < count; index++)
			BeliefCopyParticle(&commit_storage[index], &current[index]);
		if (commit_storage == state->particles)
			for (index = count; index < state->particle_count; index++)
				memset(&commit_storage[index], 0,
					sizeof(commit_storage[index]));
		candidate.particles = commit_storage;
		candidate.particle_capacity = commit_capacity;
	}
	candidate.revision++;
	candidate.generation++;
	*state = candidate;
	reduction.result = SG_BELIEF_REDUCE_APPLIED;
	reduction.committed_revision = state->revision;
	reduction.particle_count = state->particle_count;
	reduction.confidence = state->confidence;
	BeliefReportCounters(&reduction, &counters);
	*out = reduction;
	return reduction.result;
}

static int BeliefHorizonProvenanceMatches(
	const belief_horizon_provenance_t *provenance,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, uint64_t to_time_ms)
{
	return provenance->issuer_identity != 0U &&
		provenance->source_identity != 0U &&
		provenance->source_generation != 0U &&
		provenance->fixed_point_identity != 0U &&
		provenance->rune_identity == snapshot->identity &&
		provenance->topology_revision == snapshot->topology_revision &&
		provenance->state_generation == state->generation &&
		provenance->state_revision == state->revision &&
		provenance->state_time_ms == state->updated_at_ms &&
		provenance->state_frame_sequence == state->last_frame_sequence &&
		provenance->to_time_ms == to_time_ms &&
		provenance->audience_team == state->audience_team &&
		provenance->target_team == state->target_team &&
		provenance->target_client == state->target_client;
}

static int BeliefHorizonSourceValid(
	const sg_belief_horizon_source_t *source,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, belief_work_counters_t *counters)
{
	sg_rune_v2_content_id_t identity;

	if (!BeliefHorizonSourceIssued(source) ||
	    !BeliefHorizonProvenanceMatches(&source->provenance, snapshot, state,
		source->provenance.to_time_ms) ||
	    !BeliefHorizonChainValid(snapshot, source->kernels,
		source->kernel_count, source->provenance.state_time_ms,
		source->provenance.to_time_ms, counters) ||
	    !BeliefHorizonChainIdentity(&source->provenance, source->kernels,
		source->kernel_count, &identity))
		return 0;
	return SG_RuneV2ContentIdEqual(&source->chain_identity, &identity);
}

static int BeliefHorizonAuthorityValid(
	const sg_belief_horizon_authority_t *authority,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, uint64_t to_time_ms,
	belief_work_counters_t *counters)
{
	sg_rune_v2_content_id_t identity;

	if (!BeliefHorizonAuthorityIssued(authority) ||
	    !BeliefHorizonProvenanceMatches(&authority->provenance, snapshot,
		state, to_time_ms) ||
	    !BeliefHorizonChainValid(snapshot, authority->kernels,
		authority->kernel_count, authority->provenance.state_time_ms,
		to_time_ms, counters) ||
	    !BeliefHorizonChainIdentity(&authority->provenance,
		authority->kernels, authority->kernel_count, &identity))
		return 0;
	return SG_RuneV2ContentIdEqual(&authority->chain_identity, &identity);
}

sg_belief_horizon_accept_result_t SG_BeliefHorizonSourceIssue(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	uint64_t to_time_ms,
	sg_belief_horizon_source_t **source_out)
{
	belief_work_counters_t counters;
	sg_belief_horizon_source_t *source;
	int overflowed;
	int invalid;

	if (!snapshot || !state || !source_out || *source_out ||
	    !BeliefStateBoundToSnapshot(snapshot, state) ||
	    to_time_ms <= state->updated_at_ms)
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
	source = calloc(1U, sizeof(*source));
	if (!source)
		return SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	source->kernels = BeliefHorizonFixedPointCreate(snapshot,
		state->updated_at_ms, to_time_ms, &overflowed, &invalid);
	if (!source->kernels)
	{
		free(source);
		return overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			invalid ? SG_BELIEF_HORIZON_REJECTED_INVALID :
			SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	}
	source->provenance.issuer_identity = BELIEF_HORIZON_ISSUER_ID;
	source->provenance.source_identity = snapshot->identity;
	source->provenance.source_generation = state->generation;
	source->provenance.fixed_point_identity = snapshot->topology_revision;
	source->provenance.rune_identity = snapshot->identity;
	source->provenance.topology_revision = snapshot->topology_revision;
	source->provenance.state_generation = state->generation;
	source->provenance.state_revision = state->revision;
	source->provenance.state_time_ms = state->updated_at_ms;
	source->provenance.state_frame_sequence = state->last_frame_sequence;
	source->provenance.to_time_ms = to_time_ms;
	source->provenance.audience_team = state->audience_team;
	source->provenance.target_team = state->target_team;
	source->provenance.target_client = state->target_client;
	source->kernel_count = 1U;
	memset(&counters, 0, sizeof(counters));
	if (!BeliefHorizonChainValid(snapshot, source->kernels,
	    source->kernel_count, state->updated_at_ms, to_time_ms, &counters) ||
	    !BeliefHorizonChainIdentity(&source->provenance, source->kernels,
		source->kernel_count, &source->chain_identity))
	{
		BeliefHorizonKernelsDestroy(source->kernels, source->kernel_count);
		free(source);
		return counters.overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			SG_BELIEF_HORIZON_REJECTED_INVALID;
	}
	source->next_issued = belief_issued_sources;
	belief_issued_sources = source;
	*source_out = source;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

int SG_BeliefHorizonSourceView(const sg_belief_horizon_source_t *source,
	const sg_belief_horizon_kernel_t **kernels_out, size_t *kernel_count_out,
	sg_rune_v2_content_id_t *content_identity_out)
{
	sg_rune_v2_content_id_t identity;

	if (!kernels_out || !kernel_count_out || !content_identity_out ||
	    !BeliefHorizonSourceIssued(source) ||
	    !BeliefHorizonChainIdentity(&source->provenance, source->kernels,
		source->kernel_count, &identity) ||
	    !SG_RuneV2ContentIdEqual(&source->chain_identity, &identity))
		return 0;
	*kernels_out = source->kernels;
	*kernel_count_out = source->kernel_count;
	*content_identity_out = source->chain_identity;
	return 1;
}

void SG_BeliefHorizonSourceDestroy(sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t **cursor = &belief_issued_sources;

	while (*cursor && *cursor != source)
		cursor = &(*cursor)->next_issued;
	if (!*cursor)
		return;
	*cursor = source->next_issued;
	BeliefHorizonKernelsDestroy(source->kernels, source->kernel_count);
	memset(source, 0, sizeof(*source));
	free(source);
}

static int BeliefHorizonAcceptOutputValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_horizon_source_t *source,
	const sg_belief_horizon_kernel_t *candidate, size_t candidate_count,
	sg_belief_horizon_authority_t **authority_out)
{
	belief_byte_range_t writable;
	belief_byte_range_t read;
	size_t index;

	if (!BeliefByteRange(authority_out, 1U, sizeof(*authority_out),
	    &writable) || !BeliefRangeDisjointFromRune(snapshot, &writable) ||
	    !BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &read) ||
	    BeliefRangesOverlap(&writable, &read) ||
	    !BeliefByteRange(state, 1U, sizeof(*state), &read) ||
	    BeliefRangesOverlap(&writable, &read) ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &read) ||
	    BeliefRangesOverlap(&writable, &read) ||
	    !BeliefByteRange(source, 1U, sizeof(*source), &read) ||
	    BeliefRangesOverlap(&writable, &read) ||
	    !BeliefByteRange(source->kernels, source->kernel_count,
		sizeof(*source->kernels), &read) ||
	    BeliefRangesOverlap(&writable, &read) ||
	    !BeliefByteRange(candidate, candidate_count, sizeof(*candidate),
		&read) || BeliefRangesOverlap(&writable, &read))
		return 0;
	for (index = 0U; index < source->kernel_count; index++)
	{
		const sg_belief_horizon_kernel_t *kernel = &source->kernels[index];
		if (!BeliefByteRange(kernel->origin_spans, kernel->origin_span_count,
		    sizeof(*kernel->origin_spans), &read) ||
		    BeliefRangesOverlap(&writable, &read) ||
		    !BeliefByteRange(kernel->entries, kernel->entry_count,
			sizeof(*kernel->entries), &read) ||
		    BeliefRangesOverlap(&writable, &read) ||
		    (kernel->step_count != 0U &&
		     (!BeliefByteRange(kernel->steps, kernel->step_count,
			sizeof(*kernel->steps), &read) ||
		      BeliefRangesOverlap(&writable, &read))))
			return 0;
	}
	for (index = 0U; index < candidate_count; index++)
	{
		const sg_belief_horizon_kernel_t *kernel = &candidate[index];
		if (!BeliefByteRange(kernel->origin_spans, kernel->origin_span_count,
		    sizeof(*kernel->origin_spans), &read) ||
		    BeliefRangesOverlap(&writable, &read) ||
		    !BeliefByteRange(kernel->entries, kernel->entry_count,
			sizeof(*kernel->entries), &read) ||
		    BeliefRangesOverlap(&writable, &read) ||
		    (kernel->step_count != 0U &&
		     (!BeliefByteRange(kernel->steps, kernel->step_count,
			sizeof(*kernel->steps), &read) ||
		      BeliefRangesOverlap(&writable, &read))))
			return 0;
	}
	return 1;
}

sg_belief_horizon_accept_result_t SG_BeliefHorizonAuthorityAccept(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_horizon_source_t *source,
	const sg_belief_horizon_kernel_t *candidate,
	size_t candidate_count,
	sg_belief_horizon_authority_t **authority_out)
{
	belief_work_counters_t source_counters;
	belief_work_counters_t candidate_counters;
	sg_belief_horizon_authority_t *authority;
	size_t index;

	if (candidate_count > SIZE_MAX / sizeof(*candidate))
		return SG_BELIEF_HORIZON_OVERFLOW;
	if (!snapshot || !state || !source || !candidate ||
	    candidate_count == 0U || !authority_out ||
	    !BeliefHorizonSourceIssued(source) ||
	    !BeliefHorizonAcceptOutputValid(snapshot, state, source, candidate,
		candidate_count, authority_out) || *authority_out)
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
	memset(&source_counters, 0, sizeof(source_counters));
	memset(&candidate_counters, 0, sizeof(candidate_counters));
	if (!BeliefStateBoundToSnapshot(snapshot, state) ||
	    !BeliefHorizonSourceValid(source, snapshot, state, &source_counters) ||
	    candidate_count != source->kernel_count ||
	    !BeliefHorizonChainValid(snapshot, candidate, candidate_count,
		state->updated_at_ms, source->provenance.to_time_ms,
		&candidate_counters))
		return source_counters.overflowed || candidate_counters.overflowed ?
			SG_BELIEF_HORIZON_OVERFLOW :
			SG_BELIEF_HORIZON_REJECTED_INVALID;
	for (index = 0U; index < candidate_count; index++)
		if (!BeliefHorizonKernelEqual(&candidate[index],
		    &source->kernels[index]))
			return SG_BELIEF_HORIZON_REJECTED_INVALID;
	authority = calloc(1U, sizeof(*authority));
	if (!authority)
		return SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	authority->kernels = BeliefHorizonKernelsClone(source->kernels,
		source->kernel_count);
	if (!authority->kernels)
	{
		free(authority);
		return SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	}
	authority->provenance = source->provenance;
	authority->kernel_count = source->kernel_count;
	if (!BeliefHorizonChainIdentity(&authority->provenance,
	    authority->kernels, authority->kernel_count,
	    &authority->chain_identity))
	{
		BeliefHorizonKernelsDestroy(authority->kernels,
			authority->kernel_count);
		free(authority);
		return SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	}
	authority->next_issued = belief_issued_authorities;
	belief_issued_authorities = authority;
	*authority_out = authority;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

void SG_BeliefHorizonAuthorityDestroy(
	sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t **cursor = &belief_issued_authorities;

	while (*cursor && *cursor != authority)
		cursor = &(*cursor)->next_issued;
	if (!*cursor)
		return;
	*cursor = authority->next_issued;
	BeliefHorizonKernelsDestroy(authority->kernels,
		authority->kernel_count);
	memset(authority, 0, sizeof(*authority));
	free(authority);
}

#if defined(SG_BELIEF_TESTING)
void SG_BeliefTestHorizonAuthorityCorrupt(
	sg_belief_horizon_authority_t *authority)
{
	if (BeliefHorizonAuthorityIssued(authority))
		authority->chain_identity.bytes[0] ^= UINT8_C(1);
}
#endif

static int BeliefPredictionMemoryValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_prediction_request_t *request,
	const sg_belief_prediction_t *out)
{
	belief_byte_range_t writable[4];
	belief_byte_range_t read_range;
	size_t writable_count = 0U;
	size_t left;
	size_t right;

	if ((request->scratch_capacity == 0U &&
	     (request->scratch_first || request->scratch_second)) ||
	    (request->scratch_capacity != 0U &&
	     (!request->scratch_first || !request->scratch_second)) ||
	    (request->particle_capacity == 0U && request->particles) ||
	    (request->particle_capacity != 0U && !request->particles))
		return 0;
	if (request->scratch_capacity != 0U)
	{
		if (!BeliefByteRange(request->scratch_first,
		    request->scratch_capacity, sizeof(*request->scratch_first),
		    &writable[writable_count++]) ||
		    !BeliefByteRange(request->scratch_second,
			request->scratch_capacity, sizeof(*request->scratch_second),
			&writable[writable_count++]))
			return 0;
	}
	if (request->particle_capacity != 0U &&
	    !BeliefByteRange(request->particles, request->particle_capacity,
		sizeof(*request->particles), &writable[writable_count++]))
		return 0;
	if (!BeliefByteRange(out, 1U, sizeof(*out),
	    &writable[writable_count++]))
		return 0;
	for (left = 0U; left < writable_count; left++)
	{
		if (!BeliefRangeDisjointFromRune(snapshot, &writable[left]))
			return 0;
		for (right = left + 1U; right < writable_count; right++)
			if (BeliefRangesOverlap(&writable[left], &writable[right]))
				return 0;
	}
	if (!BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count) ||
	    !BeliefByteRange(state, 1U, sizeof(*state), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count) ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count) ||
	    !BeliefByteRange(request, 1U, sizeof(*request), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
		return 0;
	if (!request->horizon)
		return 1;
	if (!BeliefByteRange(request->horizon, 1U,
	    sizeof(*request->horizon), &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
		return 0;
	if (!BeliefByteRange(request->horizon->kernels,
	    request->horizon->kernel_count, sizeof(*request->horizon->kernels),
	    &read_range) ||
	    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
		return 0;
	for (left = 0U; left < request->horizon->kernel_count; left++)
	{
		const sg_belief_horizon_kernel_t *kernel =
			&request->horizon->kernels[left];
		if (!BeliefByteRange(kernel->origin_spans, kernel->origin_span_count,
		    sizeof(*kernel->origin_spans), &read_range) ||
		    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count) ||
		    !BeliefByteRange(kernel->entries, kernel->entry_count,
			sizeof(*kernel->entries), &read_range) ||
		    !BeliefRangeDisjointFromAll(&read_range, writable, writable_count))
			return 0;
		if (kernel->step_count != 0U &&
		    (!BeliefByteRange(kernel->steps, kernel->step_count,
			sizeof(*kernel->steps), &read_range) ||
		     !BeliefRangeDisjointFromAll(&read_range, writable,
			writable_count)))
			return 0;
	}
	return 1;
}

static void BeliefPredictionDescribe(sg_belief_prediction_t *prediction,
	const sg_belief_state_t *state,
	const sg_belief_prediction_request_t *request, float confidence)
{
	memset(prediction, 0, sizeof(*prediction));
	prediction->at_time_ms = request->at_time_ms;
	prediction->target_life = state->target_life;
	prediction->subject.audience_team = state->audience_team;
	prediction->subject.target_team = state->target_team;
	prediction->source.rune_identity = state->rune_identity;
	prediction->source.topology_revision = state->topology_revision;
	prediction->source.state_generation = state->generation;
	prediction->source.state_revision = state->revision;
	prediction->source.state_time_ms = state->updated_at_ms;
	if (request->horizon)
	{
		prediction->source.horizon_issuer_identity =
			request->horizon->provenance.issuer_identity;
		prediction->source.horizon_source_identity =
			request->horizon->provenance.source_identity;
		prediction->source.horizon_source_generation =
			request->horizon->provenance.source_generation;
		prediction->source.horizon_fixed_point_identity =
			request->horizon->provenance.fixed_point_identity;
		prediction->source.horizon_chain_identity =
			request->horizon->chain_identity;
	}
	prediction->particle_capacity = request->particle_capacity;
	prediction->confidence = confidence;
	prediction->particles = request->particles;
}

sg_belief_predict_result_t SG_BeliefPredict(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_prediction_request_t *request,
	sg_belief_prediction_t *out)
{
	sg_belief_prediction_t prediction;
	belief_work_counters_t counters;
	sg_belief_particle_t *current;
	sg_belief_particle_t *next;
	uint64_t elapsed_ms;
	float confidence;
	size_t count;
	size_t required_scratch;
	size_t index;
	const sg_belief_horizon_kernel_t *kernels = NULL;
	size_t kernel_count = 0U;

	if (!snapshot || !state || !request || !out)
		return SG_BELIEF_PREDICT_REJECTED_INVALID;
	if (request->scratch_capacity > SIZE_MAX / sizeof(*request->scratch_first) ||
	    request->particle_capacity > SIZE_MAX / sizeof(*request->particles))
		return SG_BELIEF_PREDICT_OVERFLOW;
	if (!BeliefStateBoundToSnapshot(snapshot, state) ||
	    request->at_time_ms < state->updated_at_ms)
		return SG_BELIEF_PREDICT_REJECTED_INVALID;
	memset(&counters, 0, sizeof(counters));
	if (request->horizon)
	{
		if (!BeliefHorizonAuthorityValid(request->horizon, snapshot, state,
		    request->at_time_ms, &counters))
			return counters.overflowed ? SG_BELIEF_PREDICT_OVERFLOW :
				SG_BELIEF_PREDICT_REJECTED_INVALID;
		kernels = request->horizon->kernels;
		kernel_count = request->horizon->kernel_count;
	}
	if (!BeliefPredictionMemoryValid(snapshot, state, request, out))
		return SG_BELIEF_PREDICT_REJECTED_INVALID;
	elapsed_ms = request->at_time_ms - state->updated_at_ms;
	confidence = state->confidence * expf(-(float)elapsed_ms /
		(float)state->policy.confidence_decay_ms);
	BeliefPredictionDescribe(&prediction, state, request, confidence);
	BeliefReportPredictionCounters(&prediction, &counters);
	count = confidence > 0.0f ? state->particle_count : 0U;
	required_scratch = count;
	prediction.required_scratch_capacity = required_scratch;
	if (request->scratch_capacity < required_scratch)
	{
		*out = prediction;
		return SG_BELIEF_PREDICT_CAPACITY;
	}
	if (count == 0U)
	{
		prediction.total_weight = 0.0f;
		*out = prediction;
		return SG_BELIEF_PREDICT_APPLIED;
	}
	current = request->scratch_first;
	next = request->scratch_second;
	for (index = 0U; index < count; index++)
		BeliefCopyParticle(&current[index], &state->particles[index]);
	if (kernel_count == 0U)
	{
		for (index = 0U; index < count; index++)
			if (!BeliefIntegrateParticle(snapshot, &current[index], elapsed_ms,
			    request->at_time_ms,
			    state->policy.spread_growth_per_ms))
				return SG_BELIEF_PREDICT_REJECTED_INVALID;
	}
	else
	{
		for (index = 0U; index < kernel_count; index++)
		{
			const sg_belief_horizon_kernel_t *kernel = &kernels[index];
			uint64_t stage_elapsed = kernel->to_time_ms -
				kernel->from_time_ms;
			size_t source_index;
			size_t stage_required = 0U;
			size_t write = 0U;

			for (source_index = 0U; source_index < count; source_index++)
			{
				const sg_belief_horizon_span_t *span =
					BeliefHorizonSpan(kernel,
						&current[source_index].phase);
				if (state->policy.diffusion_fraction < 1.0f &&
				    !BeliefSizeAdd(stage_required, 1U, &stage_required))
					goto prediction_overflow;
				if (state->policy.diffusion_fraction > 0.0f &&
				    !BeliefSizeAdd(stage_required, span->entry_count,
					&stage_required))
					goto prediction_overflow;
			}
			if (stage_required > required_scratch)
				required_scratch = stage_required;
			prediction.required_scratch_capacity = required_scratch;
			if (request->scratch_capacity < stage_required)
			{
				BeliefReportPredictionCounters(&prediction, &counters);
				*out = prediction;
				return SG_BELIEF_PREDICT_CAPACITY;
			}
			for (source_index = 0U; source_index < count; source_index++)
			{
				const sg_belief_particle_t *source = &current[source_index];
				const sg_belief_horizon_span_t *span =
					BeliefHorizonSpan(kernel, &source->phase);
				size_t offset;
				if (state->policy.diffusion_fraction < 1.0f)
				{
					sg_belief_particle_t retained;
					BeliefCopyParticle(&retained, source);
					if (!BeliefIntegrateParticle(snapshot, &retained,
					    stage_elapsed, kernel->to_time_ms,
					    state->policy.spread_growth_per_ms) ||
					    !BeliefWeightProduct(retained.weight,
						1.0f - state->policy.diffusion_fraction,
						1.0f, &retained.weight) ||
					    !BeliefAppendParticle(next, &write, &retained))
						return SG_BELIEF_PREDICT_REJECTED_INVALID;
				}
				if (state->policy.diffusion_fraction > 0.0f)
					for (offset = 0U; offset < span->entry_count; offset++)
					{
						sg_belief_particle_t moved;
						const sg_belief_horizon_entry_t *entry =
							&kernel->entries[
								span->first_entry + offset];
						if (!BeliefMoveByHorizonEntry(snapshot, kernel,
						    entry, source, stage_elapsed,
						    kernel->to_time_ms,
						    state->policy.spread_growth_per_ms, &moved) ||
						    !BeliefWeightProduct(moved.weight,
							state->policy.diffusion_fraction,
							entry->likelihood, &moved.weight) ||
						    !BeliefCounterIncrement(
							&counters.evaluated_outcomes, &counters) ||
						    !BeliefAppendParticle(next, &write, &moved))
							return counters.overflowed ?
								SG_BELIEF_PREDICT_OVERFLOW :
								SG_BELIEF_PREDICT_REJECTED_INVALID;
					}
			}
			if (!BeliefNormalize(next, &write))
				return SG_BELIEF_PREDICT_REJECTED_INVALID;
			{
				sg_belief_particle_t *swap = current;
				current = next;
				next = swap;
			}
			count = write;
		}
	}
	prediction.required_particle_capacity = count;
	prediction.required_scratch_capacity = required_scratch;
	BeliefReportPredictionCounters(&prediction, &counters);
	if (request->particle_capacity < count)
	{
		*out = prediction;
		return SG_BELIEF_PREDICT_CAPACITY;
	}
	for (index = 0U; index < count; index++)
		BeliefCopyParticle(&request->particles[index], &current[index]);
	prediction.particle_count = count;
	prediction.total_weight = 1.0f;
	*out = prediction;
	return SG_BELIEF_PREDICT_APPLIED;

prediction_overflow:
	prediction.required_scratch_capacity = SIZE_MAX;
	BeliefReportPredictionCounters(&prediction, &counters);
	*out = prediction;
	return SG_BELIEF_PREDICT_OVERFLOW;
}
