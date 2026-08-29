#include "sg_belief_contract.h"

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
	    provenance->issuer_client >= SG_BELIEF_MAX_CLIENTS ||
	    provenance->reserved != 0U || provenance->evidence_id == 0U ||
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
	    evidence->target_client != state->target_client ||
	    evidence->reserved != 0U || evidence->observed_at_ms == 0U ||
	    evidence->observed_at_ms > at_ms ||
	    evidence->confidence <= 0.0f ||
	    evidence->confidence > 1.0f ||
	    !SG_BeliefFloatValid(evidence->confidence) ||
	    evidence->support_count == 0U || !evidence->supports ||
	    !BeliefByteRange(evidence->supports, evidence->support_count,
		sizeof(*evidence->supports), &support_range) ||
	    !BeliefEvidenceShapeValid(evidence))
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
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

static int BeliefKinematicsWithinStep(
	const belief_step_bounds_t *bounds,
	const sg_belief_particle_t *particle)
{
	double speed_squared = 0.0;
	double horizontal_acceleration_squared;
	float vertical_limit;
	size_t axis;

	if (!bounds->constrains_kinematics)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		speed_squared += (double)particle->velocity[axis] *
			(double)particle->velocity[axis];
	horizontal_acceleration_squared =
		(double)particle->acceleration[0] *
			(double)particle->acceleration[0] +
		(double)particle->acceleration[1] *
			(double)particle->acceleration[1];
	vertical_limit = bounds->vertical_acceleration_max + bounds->gravity;
	if (!SG_BeliefFloatValid(vertical_limit))
		return 0;
	return isfinite(speed_squared) &&
		isfinite(horizontal_acceleration_squared) &&
		speed_squared <= (double)bounds->speed_max *
			(double)bounds->speed_max &&
		horizontal_acceleration_squared <=
			(double)bounds->acceleration_max *
			(double)bounds->acceleration_max &&
		fabsf(particle->acceleration[2]) <= vertical_limit;
}

static int BeliefEntryKinematicsValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	const sg_belief_horizon_entry_t *entry,
	const sg_belief_particle_t *particle)
{
	sg_belief_particle_t cursor;
	size_t end;
	size_t index;

	if (!BeliefSizeAdd(entry->first_step, entry->step_count, &end))
		return 0;
	BeliefCopyParticle(&cursor, particle);
	for (index = entry->first_step; index < end; index++)
	{
		belief_step_bounds_t bounds;
		const sg_belief_horizon_step_t *step = &kernel->steps[index];

		if (cursor.phase.phase_id != step->from.phase_id ||
		    cursor.phase.cell_id != step->from.cell_id)
			return 0;
		cursor.movement_state = BeliefMovementAtPhase(snapshot,
			&step->from, cursor.movement_state);
		if (!SG_BeliefKinematicsCompatible(snapshot, &step->from,
		    cursor.movement_state, cursor.velocity, cursor.acceleration,
		    cursor.orientation) ||
		    !BeliefRuneRecordMatches(snapshot, step->kind,
		    step->record_index, &step->from, &step->to, &bounds) ||
		    !BeliefKinematicsWithinStep(&bounds, &cursor))
			return 0;
		cursor.phase = step->to;
	}
	return 1;
}

static int BeliefMoveByHorizonEntry(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	const sg_belief_horizon_entry_t *entry,
	const sg_belief_particle_t *source, uint64_t elapsed_ms, uint64_t at_ms,
	float spread_growth, sg_belief_particle_t *moved)
{
	size_t axis;

	if (!BeliefEntryKinematicsValid(snapshot, kernel, entry, source))
		return 0;
	BeliefCopyParticle(moved, source);
	moved->phase = entry->to;
	moved->movement_state = BeliefMovementAtPhase(snapshot, &moved->phase,
		moved->movement_state);
	for (axis = 0U; axis < 3U; axis++)
	{
		moved->position[axis] = source->position[axis] +
			entry->displacement[axis];
		if (!SG_BeliefFloatValid(moved->position[axis]))
			return 0;
	}
	moved->spread_radius += spread_growth * (float)elapsed_ms;
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
	double total = 0.0;
	double normalized_total = 0.0;
	float divisor;

	for (index = 0U; index < *count; index++)
	{
		if (!SG_BeliefFloatValid(particles[index].weight) ||
		    particles[index].weight < 0.0f)
			return 0;
		if (particles[index].weight > 0.0f)
		{
			total += (double)particles[index].weight;
			if (!isfinite(total))
				return 0;
			if (write != index)
				particles[write] = particles[index];
			write++;
		}
	}
	*count = write;
	if (write == 0U)
		return 1;
	if (total <= 0.0 || total > (double)FLT_MAX)
		return 0;
	divisor = (float)total;
	for (index = 0U; index < write; index++)
	{
		particles[index].weight /= divisor;
		if (!SG_BeliefFloatValid(particles[index].weight) ||
		    particles[index].weight <= 0.0f)
			return 0;
		if (particles[index].weight > particles[largest].weight)
			largest = index;
		normalized_total += (double)particles[index].weight;
	}
	particles[largest].weight += (float)(1.0 - normalized_total);
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
	    config->target_client >= SG_BELIEF_MAX_CLIENTS ||
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
	candidate.target_client = config->target_client;
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
		memset(&candidate.latest_provenance, 0,
			sizeof(candidate.latest_provenance));
		candidate.latest_provenance.authenticated =
			evidence->provenance.authenticated;
		candidate.latest_provenance.issuer_kind =
			evidence->provenance.issuer_kind;
		candidate.latest_provenance.issuer_team =
			evidence->provenance.issuer_team;
		candidate.latest_provenance.audience_team =
			evidence->provenance.audience_team;
		candidate.latest_provenance.issuer_client =
			evidence->provenance.issuer_client;
		candidate.latest_provenance.evidence_id =
			evidence->provenance.evidence_id;
		candidate.latest_provenance.evidence_sequence =
			evidence->provenance.evidence_sequence;
		candidate.latest_provenance.authenticated_at_ms =
			evidence->provenance.authenticated_at_ms;
		candidate.latest_provenance.rune_identity =
			evidence->provenance.rune_identity;
		candidate.latest_provenance.topology_revision =
			evidence->provenance.topology_revision;
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

int SG_BeliefPredict(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, uint64_t at_time_ms,
	sg_belief_particle_t *storage, size_t capacity,
	sg_belief_prediction_t *out)
{
	sg_belief_prediction_t candidate;
	belief_byte_range_t out_range;
	belief_byte_range_t state_range;
	belief_byte_range_t state_storage_range;
	belief_byte_range_t output_storage_range;
	uint64_t elapsed_ms;
	float confidence;
	size_t required;
	size_t index;

	if (!out || !storage || !BeliefStateBoundToSnapshot(snapshot, state) ||
	    at_time_ms < state->updated_at_ms ||
	    !BeliefByteRange(out, 1U, sizeof(*out), &out_range) ||
	    !BeliefByteRange(state, 1U, sizeof(*state), &state_range) ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &state_storage_range) ||
	    !BeliefRangeDisjointFromRune(snapshot, &out_range) ||
	    BeliefRangesOverlap(&out_range, &state_range) ||
	    BeliefRangesOverlap(&out_range, &state_storage_range) ||
	    (capacity != 0U &&
	     (!BeliefByteRange(storage, capacity, sizeof(*storage),
		&output_storage_range) ||
	      !BeliefRangeDisjointFromRune(snapshot, &output_storage_range) ||
	      BeliefRangesOverlap(&output_storage_range, &state_range) ||
	      BeliefRangesOverlap(&output_storage_range, &state_storage_range) ||
	      BeliefRangesOverlap(&output_storage_range, &out_range))))
		return 0;
	elapsed_ms = at_time_ms - state->updated_at_ms;
	confidence = state->confidence * expf(-(float)elapsed_ms /
		(float)state->policy.confidence_decay_ms);
	required = confidence > 0.0f ? state->particle_count : 0U;
	memset(&candidate, 0, sizeof(candidate));
	candidate.at_time_ms = at_time_ms;
	candidate.particle_capacity = capacity;
	candidate.required_particle_capacity = required;
	candidate.confidence = confidence;
	candidate.total_weight = required == 0U ? 0.0f : 1.0f;
	candidate.particles = storage;
	if (capacity < required)
	{
		*out = candidate;
		return 0;
	}
	for (index = 0U; index < required; index++)
	{
		sg_belief_particle_t checked;
		BeliefCopyParticle(&checked, &state->particles[index]);
		if (!BeliefIntegrateParticle(snapshot, &checked, elapsed_ms, at_time_ms,
		    state->policy.spread_growth_per_ms))
			return 0;
	}
	candidate.particle_count = required;
	for (index = 0U; index < required; index++)
	{
		BeliefCopyParticle(&storage[index], &state->particles[index]);
		if (!BeliefIntegrateParticle(snapshot, &storage[index], elapsed_ms,
		    at_time_ms,
		    state->policy.spread_growth_per_ms))
			return 0;
	}
	*out = candidate;
	return 1;
}
