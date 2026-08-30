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
	sg_belief_life_identity_t target_life;
} belief_horizon_provenance_t;

struct sg_belief_horizon_source_s
{
	uint64_t issuance_identity;
	uint8_t active;
	uint8_t reserved[7];
	belief_horizon_provenance_t provenance;
	sg_rune_v2_content_id_t chain_identity;
	size_t kernel_count;
	sg_belief_horizon_kernel_t *kernels;
	struct sg_belief_horizon_source_s *next_issued;
};

struct sg_belief_horizon_authority_s
{
	uint64_t issuance_identity;
	uint8_t active;
	uint8_t reserved[7];
	belief_horizon_provenance_t provenance;
	sg_rune_v2_content_id_t chain_identity;
	size_t kernel_count;
	sg_belief_horizon_kernel_t *kernels;
	struct sg_belief_horizon_authority_s *next_issued;
};

/* Scopes are level-owned private leases.  Unlike public issued handles, a
 * scope's source and authority addresses never escape its owner, so their
 * payloads may be replaced without weakening public stale-handle ABA rules. */
struct sg_belief_horizon_scope_s
{
	sg_belief_horizon_source_t source;
	sg_belief_horizon_authority_t authority;
	/* The authority owns the direct state-to-frame kernel.  Delayed evidence
	 * needs one more exact observed-to-frame kernel; frame_kernels is only a
	 * borrowed, sorted vector over those two owned kernel payloads. */
	sg_belief_horizon_kernel_t *evidence_kernels;
	size_t evidence_kernel_count;
	sg_belief_horizon_kernel_t *frame_kernels;
	size_t frame_kernel_count;
	struct sg_belief_horizon_scope_s *next_scope;
};

static sg_belief_horizon_source_t *belief_issued_sources;
static sg_belief_horizon_authority_t *belief_issued_authorities;
static sg_belief_horizon_scope_t *belief_horizon_scopes;
static uint64_t belief_next_issuance_identity = 1U;

#if defined(SG_BELIEF_TESTING)
static sg_belief_horizon_accept_result_t belief_scope_fail_next;
static size_t belief_scope_allocation_count;
#endif

/* Retired handle records remain as tombstones. Their addresses cannot reenter
 * the allocator, and issuance identities never derive from belief state. */

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
		    (transition->flags & ~(sg_rune_phase_transition_flags_t)
			    SG_RUNE_PHASE_TRANSITION_FLAGS_KNOWN) != 0U ||
		    !BeliefStableIdEqual(&transition->cell.value, from_cell_id) ||
		    !BeliefStableIdEqual(&transition->destination_cell.value, to_cell_id) ||
		    ((transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) != 0U) !=
			    (from->cell_id != to->cell_id) ||
		    (transition->kind == SG_RUNE_PHASE_TRANSITION_PORTAL &&
			    (from->cell_id == to->cell_id ||
				    transition->duration_ms.min_value != 0.0f ||
				    transition->duration_ms.max_value != 0.0f)) ||
		    !BeliefStableIdEqual(&transition->source_phase.value, from_id) ||
		    !BeliefStableIdEqual(&transition->destination_phase.value, to_id) ||
		    !BeliefIntervalValid(&transition->duration_ms, 1) ||
		    (transition->kind != SG_RUNE_PHASE_TRANSITION_PORTAL &&
			    transition->duration_ms.max_value <= 0.0f))
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
		'S', 'G', 'B', 'H', 'Z', '0', '0', '3'
	};
	unsigned char *bytes;
	size_t size = sizeof(tag) + 106U + 8U;
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
	BeliefCanonicalU32(bytes, &cursor, provenance->target_life.client_id);
	BeliefCanonicalU32(bytes, &cursor, provenance->target_life.reserved);
	BeliefCanonicalU64(bytes, &cursor,
		provenance->target_life.spawn_generation);
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

static sg_belief_horizon_source_t *BeliefHorizonSourceRecord(
	const sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *cursor;

	for (cursor = belief_issued_sources; cursor; cursor = cursor->next_issued)
		if (cursor == source)
			return cursor;
	return NULL;
}

static sg_belief_horizon_authority_t *BeliefHorizonAuthorityRecord(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *cursor;

	for (cursor = belief_issued_authorities; cursor;
	     cursor = cursor->next_issued)
		if (cursor == authority)
			return cursor;
	return NULL;
}

static sg_belief_horizon_scope_t *BeliefHorizonScopeRecord(
	const sg_belief_horizon_scope_t *scope)
{
	sg_belief_horizon_scope_t *cursor;

	for (cursor = belief_horizon_scopes; cursor; cursor = cursor->next_scope)
		if (cursor == scope)
			return cursor;
	return NULL;
}

static int BeliefHorizonAuthorityScoped(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_scope_t *scope;

	for (scope = belief_horizon_scopes; scope; scope = scope->next_scope)
		if (&scope->authority == authority)
			return scope->authority.active == 1U &&
				scope->authority.issuance_identity == 0U;
	return 0;
}

static int BeliefHorizonSourceIssued(
	const sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *record = BeliefHorizonSourceRecord(source);

	return record && record->active == 1U && record->issuance_identity != 0U;
}

static int BeliefHorizonAuthorityIssued(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	return record && record->active == 1U && record->issuance_identity != 0U;
}

static int BeliefHorizonIssuanceIdentity(uint64_t *identity_out)
{
	if (!identity_out || belief_next_issuance_identity == 0U)
		return 0;
	*identity_out = belief_next_issuance_identity++;
	return 1;
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
	size_t path_depth;
	float duration_min_ms;
	float duration_max_ms;
	float displacement_min[3];
	float displacement_max[3];
	uint32_t *closure_phases;
	uint32_t *closure_parent_phases;
	uint32_t *closure_parent_edges;
	uint32_t closure_count;
	uint32_t closure_cursor;
	uint32_t active_phase;
	size_t active_path_length;
	size_t next_record;
	uint8_t active;
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

typedef struct belief_portal_edge_s
{
	uint32_t source;
	uint32_t destination;
	uint32_t record;
} belief_portal_edge_t;

/* Exact zero-time portal topology.  Components are contracted for cycle and
 * depth analysis.  A per-frame visited traversal later chooses one canonical
 * path from the entry phase to each reachable phase, so an SCC never creates
 * arbitrary portal repetitions. */
typedef struct belief_portal_graph_s
{
	uint32_t phase_count;
	uint32_t edge_count;
	belief_portal_edge_t *edges;
	uint32_t *out_offsets;
	uint32_t *out_edges;
	uint32_t *reverse_offsets;
	uint32_t *reverse_edges;
	uint32_t component_count;
	uint32_t *component;
	uint32_t *path_scratch;
} belief_portal_graph_t;

static void BeliefPortalGraphDestroy(belief_portal_graph_t *graph)
{
	if (!graph)
		return;
	free(graph->edges);
	free(graph->out_offsets);
	free(graph->out_edges);
	free(graph->reverse_offsets);
	free(graph->reverse_edges);
	free(graph->component);
	free(graph->path_scratch);
	memset(graph, 0, sizeof(*graph));
}

static int BeliefPortalGraphBuild(
	const sg_rune_runtime_snapshot_t *snapshot,
	belief_portal_graph_t *graph, int *overflowed_out, int *invalid_out)
{
	const sg_rune_model_t *model;
	uint32_t *out_counts = NULL;
	uint32_t *reverse_counts = NULL;
	uint32_t *out_cursor = NULL;
	uint32_t *reverse_cursor = NULL;
	uint8_t *visited = NULL;
	uint32_t *finish_order = NULL;
	uint32_t *stack = NULL;
	uint32_t *stack_cursor = NULL;
	uint32_t *component_stack = NULL;
	uint32_t phase;
	uint32_t edge_count = 0U;
	uint32_t finish_count = 0U;
	uint32_t index;

	if (!snapshot || !graph || !overflowed_out || !invalid_out ||
		!snapshot->model)
		return 0;
	model = snapshot->model;
	if (snapshot->phase_count != 0U &&
		(!snapshot->phases ||
		 (model->phase_transition_count != 0U &&
		  !model->phase_transitions)))
	{
		*invalid_out = 1;
		return 0;
	}
	memset(graph, 0, sizeof(*graph));
	*overflowed_out = 0;
	*invalid_out = 0;
	graph->phase_count = snapshot->phase_count;
	if (snapshot->phase_count == 0U)
		return 1;
	for (index = 0U; index < model->phase_transition_count; index++)
	{
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[index];
		uint32_t source;
		uint32_t destination;
		belief_step_bounds_t bounds;

		if (transition->kind != SG_RUNE_PHASE_TRANSITION_PORTAL)
			continue;
		if (transition->duration_ms.min_value != 0.0f ||
			transition->duration_ms.max_value != 0.0f)
		{
			*invalid_out = 1;
			return 0;
		}
		source = BeliefHorizonPhaseIndex(model, &transition->source_phase);
		destination = BeliefHorizonPhaseIndex(model,
			&transition->destination_phase);
		if (source >= snapshot->phase_count ||
			destination >= snapshot->phase_count ||
			!BeliefRuneRecordMatches(snapshot,
				SG_BELIEF_HORIZON_PHASE_TRANSITION, index,
				&snapshot->phases[source], &snapshot->phases[destination],
				&bounds))
		{
			*invalid_out = 1;
			return 0;
		}
		if (edge_count == UINT32_MAX)
		{
			*overflowed_out = 1;
			goto failure;
		}
		edge_count++;
	}
	graph->edge_count = edge_count;
	if (edge_count != 0U)
	{
		graph->edges = calloc((size_t)edge_count, sizeof(*graph->edges));
		if (!graph->edges)
			return 0;
	}
	if ((size_t)snapshot->phase_count + 1U >
		SIZE_MAX / sizeof(*graph->out_offsets))
	{
		*overflowed_out = 1;
		goto failure;
	}
	graph->out_offsets = calloc((size_t)snapshot->phase_count + 1U,
		sizeof(*graph->out_offsets));
	graph->reverse_offsets = calloc((size_t)snapshot->phase_count + 1U,
		sizeof(*graph->reverse_offsets));
	out_counts = calloc((size_t)snapshot->phase_count,
		sizeof(*out_counts));
	reverse_counts = calloc((size_t)snapshot->phase_count,
		sizeof(*reverse_counts));
	if (!graph->out_offsets || !graph->reverse_offsets || !out_counts ||
		!reverse_counts)
		goto failure;
	edge_count = 0U;
	for (index = 0U; index < model->phase_transition_count; index++)
	{
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[index];
		uint32_t source;
		uint32_t destination;

		if (transition->kind != SG_RUNE_PHASE_TRANSITION_PORTAL)
			continue;
		source = BeliefHorizonPhaseIndex(model, &transition->source_phase);
		destination = BeliefHorizonPhaseIndex(model,
			&transition->destination_phase);
		graph->edges[edge_count].source = source;
		graph->edges[edge_count].destination = destination;
		graph->edges[edge_count].record = index;
		if (out_counts[source] == UINT32_MAX ||
			reverse_counts[destination] == UINT32_MAX)
		{
			*overflowed_out = 1;
			goto failure;
		}
		out_counts[source]++;
		reverse_counts[destination]++;
		edge_count++;
	}
	for (phase = 0U; phase < snapshot->phase_count; phase++)
	{
		if (graph->out_offsets[phase] > UINT32_MAX - out_counts[phase] ||
			graph->reverse_offsets[phase] > UINT32_MAX -
				reverse_counts[phase])
		{
			*overflowed_out = 1;
			goto failure;
		}
		graph->out_offsets[phase + 1U] =
			graph->out_offsets[phase] + out_counts[phase];
		graph->reverse_offsets[phase + 1U] =
			graph->reverse_offsets[phase] + reverse_counts[phase];
	}
	if (graph->out_offsets[snapshot->phase_count] != graph->edge_count ||
		graph->reverse_offsets[snapshot->phase_count] != graph->edge_count)
	{
		*invalid_out = 1;
		goto failure;
	}
	if (edge_count != 0U)
	{
		graph->out_edges = calloc((size_t)edge_count,
			sizeof(*graph->out_edges));
		graph->reverse_edges = calloc((size_t)edge_count,
			sizeof(*graph->reverse_edges));
		if (!graph->out_edges || !graph->reverse_edges)
			goto failure;
	}
	out_cursor = calloc((size_t)snapshot->phase_count,
		sizeof(*out_cursor));
	reverse_cursor = calloc((size_t)snapshot->phase_count,
		sizeof(*reverse_cursor));
	if (!out_cursor || !reverse_cursor)
		goto failure;
	for (index = 0U; index < graph->edge_count; index++)
	{
		uint32_t source = graph->edges[index].source;
		uint32_t destination = graph->edges[index].destination;
		graph->out_edges[graph->out_offsets[source] + out_cursor[source]++] = index;
		graph->reverse_edges[graph->reverse_offsets[destination] +
			reverse_cursor[destination]++] = index;
	}
	graph->component = malloc((size_t)snapshot->phase_count *
		sizeof(*graph->component));
	visited = calloc((size_t)snapshot->phase_count, sizeof(*visited));
	finish_order = malloc((size_t)snapshot->phase_count *
		sizeof(*finish_order));
	stack = malloc((size_t)snapshot->phase_count * sizeof(*stack));
	stack_cursor = malloc((size_t)snapshot->phase_count *
		sizeof(*stack_cursor));
	if (!graph->component || !visited || !finish_order || !stack ||
		!stack_cursor)
		goto failure;
	for (phase = 0U; phase < snapshot->phase_count; phase++)
		graph->component[phase] = UINT32_MAX;
	for (phase = 0U; phase < snapshot->phase_count; phase++)
	{
		size_t top;

		if (visited[phase] != 0U)
			continue;
		visited[phase] = 1U;
		top = 0U;
		stack[top] = phase;
		stack_cursor[top] = graph->out_offsets[phase];
		top++;
		while (top != 0U)
		{
			uint32_t source = stack[top - 1U];
			uint32_t cursor = stack_cursor[top - 1U];

			if (cursor < graph->out_offsets[source + 1U])
			{
				uint32_t edge = graph->out_edges[cursor];
				uint32_t destination = graph->edges[edge].destination;

				stack_cursor[top - 1U] = cursor + 1U;
				if (visited[destination] == 0U)
				{
					visited[destination] = 1U;
					stack[top] = destination;
					stack_cursor[top] = graph->out_offsets[destination];
					top++;
				}
			}
			else
			{
				if (finish_count == UINT32_MAX)
				{
					*overflowed_out = 1;
					goto failure;
				}
				finish_order[finish_count++] = source;
				top--;
			}
		}
	}
	component_stack = malloc((size_t)snapshot->phase_count *
		sizeof(*component_stack));
	if (!component_stack)
		goto failure;
	for (index = finish_count; index != 0U; index--)
	{
		uint32_t root = finish_order[index - 1U];
		uint32_t component;
		size_t top;

		if (graph->component[root] != UINT32_MAX)
			continue;
		component = graph->component_count++;
		if (component == UINT32_MAX)
		{
			*overflowed_out = 1;
			goto failure;
		}
		graph->component[root] = component;
		top = 0U;
		component_stack[top++] = root;
		while (top != 0U)
		{
			uint32_t source = component_stack[--top];
			uint32_t cursor;

			for (cursor = graph->reverse_offsets[source];
				cursor < graph->reverse_offsets[source + 1U]; cursor++)
			{
				uint32_t edge = graph->reverse_edges[cursor];
				uint32_t destination = graph->edges[edge].source;

				if (graph->component[destination] == UINT32_MAX)
				{
					graph->component[destination] = component;
					component_stack[top++] = destination;
				}
			}
		}
	}
	graph->path_scratch = malloc((size_t)snapshot->phase_count *
		sizeof(*graph->path_scratch));
	if (!graph->path_scratch)
		goto failure;
	free(out_counts);
	free(reverse_counts);
	free(out_cursor);
	free(reverse_cursor);
	free(visited);
	free(finish_order);
	free(stack);
	free(stack_cursor);
	free(component_stack);
	return 1;

failure:
	free(out_counts);
	free(reverse_counts);
	free(out_cursor);
	free(reverse_cursor);
	free(visited);
	free(finish_order);
	free(stack);
	free(stack_cursor);
	free(component_stack);
	BeliefPortalGraphDestroy(graph);
	return 0;
}

typedef struct belief_component_edge_s
{
	uint32_t source;
	uint32_t destination;
} belief_component_edge_t;

static int BeliefHorizonDepthBound(const sg_rune_runtime_snapshot_t *snapshot,
	uint64_t elapsed_ms, const belief_portal_graph_t *portal_graph,
	size_t *depth_out, int *overflowed_out)
{
	const sg_rune_model_t *model;
	uint32_t *indegree = NULL;
	uint32_t *longest = NULL;
	uint32_t *queue = NULL;
	uint32_t *edge_counts = NULL;
	uint32_t *edge_offsets = NULL;
	uint32_t *edge_cursor = NULL;
	uint32_t *edge_indexes = NULL;
	belief_component_edge_t *edges = NULL;
	size_t record_count;
	uint32_t edge_count = 0U;
	uint32_t queue_read = 0U;
	uint32_t queue_write = 0U;
	uint32_t processed = 0U;
	uint32_t component;
	uint32_t index;
	uint32_t longest_zero = 0U;
	float minimum = FLT_MAX;
	size_t positive_steps = 0U;
	size_t internal_extra;
	size_t zero_run;
	size_t segment_count;

	if (!snapshot || !snapshot->model || !portal_graph || !depth_out ||
		!overflowed_out)
		return 0;
	model = snapshot->model;
	if (!BeliefHorizonRecordCount(model, &record_count) ||
		record_count > UINT32_MAX)
	{
		*overflowed_out = 1;
		return 0;
	}
	if (portal_graph->component_count == 0U)
	{
		*depth_out = 0U;
		return 1;
	}
	indegree = calloc((size_t)portal_graph->component_count,
		sizeof(*indegree));
	longest = calloc((size_t)portal_graph->component_count,
		sizeof(*longest));
	queue = calloc((size_t)portal_graph->component_count, sizeof(*queue));
	edge_counts = calloc((size_t)portal_graph->component_count,
		sizeof(*edge_counts));
	if (!indegree || !longest || !queue || !edge_counts)
		goto allocation_failure;
	if (record_count == SIZE_MAX)
		goto overflow;
	edges = calloc(record_count == 0U ? 1U : record_count,
		sizeof(*edges));
	if (!edges)
		goto allocation_failure;
	for (index = 0U; index < model->phase_transition_count; index++)
	{
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[index];
		float value = transition->duration_ms.min_value;
		uint32_t source;
		uint32_t destination;
		uint32_t source_component;
		uint32_t destination_component;
		belief_step_bounds_t bounds;

		if (!SG_BeliefFloatValid(value) || value < 0.0f)
			goto invalid;
		if (value != 0.0f)
		{
			if (value < minimum)
				minimum = value;
			continue;
		}
		source = BeliefHorizonPhaseIndex(model, &transition->source_phase);
		destination = BeliefHorizonPhaseIndex(model,
			&transition->destination_phase);
		if (source >= snapshot->phase_count ||
			destination >= snapshot->phase_count ||
			!BeliefRuneRecordMatches(snapshot,
				SG_BELIEF_HORIZON_PHASE_TRANSITION, index,
				&snapshot->phases[source], &snapshot->phases[destination],
				&bounds))
			goto invalid;
		source_component = portal_graph->component[source];
		destination_component = portal_graph->component[destination];
		if (transition->kind == SG_RUNE_PHASE_TRANSITION_PORTAL &&
			source_component == destination_component)
			continue;
		if (source_component == destination_component)
			goto invalid;
		if (edge_count == UINT32_MAX ||
			edge_counts[source_component] == UINT32_MAX ||
			indegree[destination_component] == UINT32_MAX)
			goto overflow;
		edges[edge_count++] = (belief_component_edge_t){ source_component,
			destination_component };
		edge_counts[source_component]++;
		indegree[destination_component]++;
	}
	for (index = 0U; index < model->kernel_count; index++)
	{
		const sg_rune_capability_kernel_t *capability = &model->kernels[index];
		float value = capability->parameters.duration_ms.min_value;
		uint32_t source;
		uint32_t destination;
		uint32_t source_component;
		uint32_t destination_component;
		belief_step_bounds_t bounds;

		if (!SG_BeliefFloatValid(value) || value < 0.0f)
			goto invalid;
		if (value != 0.0f)
		{
			if (value < minimum)
				minimum = value;
			continue;
		}
		source = BeliefHorizonPhaseIndex(model, &capability->source_phase);
		destination = BeliefHorizonPhaseIndex(model,
			&capability->destination_phase);
		if (source >= snapshot->phase_count ||
			destination >= snapshot->phase_count ||
			!BeliefRuneRecordMatches(snapshot,
				SG_BELIEF_HORIZON_CAPABILITY_KERNEL, index,
				&snapshot->phases[source], &snapshot->phases[destination],
				&bounds))
			goto invalid;
		source_component = portal_graph->component[source];
		destination_component = portal_graph->component[destination];
		if (source_component == destination_component)
			goto invalid;
		if (edge_count == UINT32_MAX ||
			edge_counts[source_component] == UINT32_MAX ||
			indegree[destination_component] == UINT32_MAX)
			goto overflow;
		edges[edge_count++] = (belief_component_edge_t){ source_component,
			destination_component };
		edge_counts[source_component]++;
		indegree[destination_component]++;
	}
	edge_offsets = calloc((size_t)portal_graph->component_count + 1U,
		sizeof(*edge_offsets));
	edge_cursor = calloc((size_t)portal_graph->component_count,
		sizeof(*edge_cursor));
	if (!edge_offsets || !edge_cursor)
		goto allocation_failure;
	for (component = 0U; component < portal_graph->component_count;
		component++)
	{
		if (edge_offsets[component] > UINT32_MAX -
			edge_counts[component])
			goto overflow;
		edge_offsets[component + 1U] = edge_offsets[component] +
			edge_counts[component];
	}
	if (edge_offsets[portal_graph->component_count] != edge_count)
		goto invalid;
	if (edge_count != 0U)
	{
		edge_indexes = calloc((size_t)edge_count, sizeof(*edge_indexes));
		if (!edge_indexes)
			goto allocation_failure;
	}
	for (index = 0U; index < edge_count; index++)
	{
		uint32_t source = edges[index].source;

		edge_indexes[edge_offsets[source] + edge_cursor[source]++] = index;
	}
	for (component = 0U; component < portal_graph->component_count;
		component++)
		if (indegree[component] == 0U)
			queue[queue_write++] = component;
	while (queue_read < queue_write)
	{
		uint32_t source = queue[queue_read++];
		uint32_t cursor;

		processed++;
		if (longest[source] > longest_zero)
			longest_zero = longest[source];
		for (cursor = edge_offsets[source];
			cursor < edge_offsets[source + 1U]; cursor++)
		{
			belief_component_edge_t edge = edges[edge_indexes[cursor]];

			if (longest[edge.destination] < longest[source] + 1U)
				longest[edge.destination] = longest[source] + 1U;
			if (indegree[edge.destination] == 0U)
				goto invalid;
			indegree[edge.destination]--;
			if (indegree[edge.destination] == 0U)
				queue[queue_write++] = edge.destination;
		}
	}
	if (processed != portal_graph->component_count)
		goto invalid;
	if (minimum != FLT_MAX)
	{
		long double count = floorl((long double)elapsed_ms /
			(long double)minimum);
		if (!isfinite(count) || count > (long double)SIZE_MAX)
			goto overflow;
		positive_steps = (size_t)count;
	}
	/* Each contracted component can contribute at most one canonical path to
	 * its root and one canonical path from its root.  This is a bound derived
	 * from the quotient representation, not a loop budget. */
	if (portal_graph->phase_count < portal_graph->component_count)
		goto invalid;
	internal_extra = (size_t)portal_graph->phase_count -
		(size_t)portal_graph->component_count;
	if (internal_extra > SIZE_MAX / 2U)
		goto overflow;
	internal_extra *= 2U;
	if ((size_t)longest_zero > SIZE_MAX - internal_extra)
		goto overflow;
	zero_run = (size_t)longest_zero + internal_extra;
	if (!BeliefSizeAdd(positive_steps, 1U, &segment_count) ||
		(zero_run != 0U && segment_count > SIZE_MAX / zero_run))
		goto overflow;
	zero_run *= segment_count;
	if (zero_run > SIZE_MAX - positive_steps)
		goto overflow;
	*depth_out = zero_run + positive_steps;
	free(indegree);
	free(longest);
	free(queue);
	free(edge_counts);
	free(edge_offsets);
	free(edge_cursor);
	free(edge_indexes);
	free(edges);
	return 1;

overflow:
	*overflowed_out = 1;
	goto cleanup;
invalid:
	/* The caller supplied a finite but semantically invalid horizon graph. */
	*overflowed_out = 0;
	goto cleanup;
allocation_failure:
cleanup:
	free(indegree);
	free(longest);
	free(queue);
	free(edge_counts);
	free(edge_offsets);
	free(edge_cursor);
	free(edge_indexes);
	free(edges);
	return 0;
}

static void BeliefHorizonFrameDestroy(belief_horizon_walk_frame_t *frame)
{
	if (!frame)
		return;
	free(frame->closure_phases);
	free(frame->closure_parent_phases);
	free(frame->closure_parent_edges);
	memset(frame, 0, sizeof(*frame));
}

static int BeliefHorizonFrameClosureBuild(
	const sg_rune_runtime_snapshot_t *snapshot,
	const belief_portal_graph_t *portal_graph, uint32_t entry_phase,
	belief_horizon_walk_frame_t *frame, int *overflowed_out)
{
	size_t bytes;
	uint32_t cursor;

	if (!snapshot || !portal_graph || !frame || !overflowed_out ||
		entry_phase >= snapshot->phase_count ||
		portal_graph->phase_count != snapshot->phase_count)
		return 0;
	if (snapshot->phase_count != 0U &&
		sizeof(uint32_t) > SIZE_MAX / (size_t)snapshot->phase_count)
	{
		*overflowed_out = 1;
		return 0;
	}
	bytes = (size_t)snapshot->phase_count * sizeof(uint32_t);
	frame->closure_phases = malloc(bytes);
	frame->closure_parent_phases = malloc(bytes);
	frame->closure_parent_edges = malloc(bytes);
	if (!frame->closure_phases || !frame->closure_parent_phases ||
		!frame->closure_parent_edges)
		goto failure;
	for (cursor = 0U; cursor < snapshot->phase_count; cursor++)
	{
		frame->closure_parent_phases[cursor] = UINT32_MAX;
		frame->closure_parent_edges[cursor] = UINT32_MAX;
	}
	frame->closure_count = 1U;
	frame->closure_cursor = 0U;
	frame->closure_phases[0] = entry_phase;
	frame->closure_parent_phases[entry_phase] = entry_phase;
	while (frame->closure_cursor < frame->closure_count)
	{
		uint32_t source =
			frame->closure_phases[frame->closure_cursor++];
		uint32_t edge_cursor;

		if (source >= snapshot->phase_count ||
			!portal_graph->out_offsets ||
			portal_graph->out_offsets[source] >
			portal_graph->out_offsets[source + 1U] ||
			portal_graph->out_offsets[source + 1U] >
			portal_graph->edge_count)
			goto failure;
		for (edge_cursor = portal_graph->out_offsets[source];
			edge_cursor < portal_graph->out_offsets[source + 1U];
			edge_cursor++)
		{
			uint32_t edge_index;
			uint32_t destination;

			if (!portal_graph->out_edges)
				goto failure;
			edge_index = portal_graph->out_edges[edge_cursor];
			if (edge_index >= portal_graph->edge_count)
				goto failure;
			destination = portal_graph->edges[edge_index].destination;
			if (destination >= snapshot->phase_count)
				goto failure;
			if (frame->closure_parent_phases[destination] != UINT32_MAX)
				continue;
			if (frame->closure_count == UINT32_MAX ||
			frame->closure_count == snapshot->phase_count)
			{
				*overflowed_out = 1;
				goto failure;
			}
			frame->closure_parent_phases[destination] = source;
			frame->closure_parent_edges[destination] = edge_index;
			frame->closure_phases[frame->closure_count++] = destination;
		}
	}
	frame->closure_cursor = 0U;
	return 1;

failure:
	BeliefHorizonFrameDestroy(frame);
	return 0;
}

static int BeliefHorizonClosurePath(
	const sg_rune_runtime_snapshot_t *snapshot,
	const belief_portal_graph_t *portal_graph,
	const belief_horizon_walk_frame_t *frame, uint32_t destination,
	size_t max_depth, sg_belief_horizon_step_t *path,
	size_t *step_count_out)
{
	const sg_rune_model_t *model;
	uint32_t cursor;
	size_t reverse_count = 0U;
	size_t index;

	if (!snapshot || !portal_graph || !frame || !step_count_out ||
		destination >= snapshot->phase_count ||
		!frame->closure_parent_phases || !frame->closure_parent_edges ||
		frame->phase.phase_id >= snapshot->phase_count ||
		frame->closure_parent_phases[destination] == UINT32_MAX)
		return 0;
	model = snapshot->model;
	cursor = destination;
	while (cursor != frame->phase.phase_id)
	{
		uint32_t parent;
		uint32_t edge_index;

		if (reverse_count == snapshot->phase_count ||
			!portal_graph->path_scratch)
			return 0;
		parent = frame->closure_parent_phases[cursor];
		edge_index = frame->closure_parent_edges[cursor];
		if (parent >= snapshot->phase_count ||
			edge_index >= portal_graph->edge_count ||
			portal_graph->edges[edge_index].source != parent ||
			portal_graph->edges[edge_index].destination != cursor)
			return 0;
		portal_graph->path_scratch[reverse_count++] = edge_index;
		cursor = parent;
	}
	if (frame->path_depth > max_depth ||
		reverse_count > max_depth - frame->path_depth ||
		(reverse_count != 0U && !path))
		return 0;
	for (index = 0U; index < reverse_count; index++)
	{
		uint32_t edge_index =
			portal_graph->path_scratch[reverse_count - index - 1U];
		const belief_portal_edge_t *edge = &portal_graph->edges[edge_index];
		sg_belief_horizon_step_t step;
		belief_step_bounds_t bounds;

		if (edge->source >= snapshot->phase_count ||
			edge->destination >= snapshot->phase_count ||
			BeliefHorizonRecord(snapshot,
				&snapshot->phases[edge->source], edge->record, &step,
				&bounds) != 1 ||
			step.kind != SG_BELIEF_HORIZON_PHASE_TRANSITION ||
			step.to.phase_id != edge->destination ||
			model->phase_transitions[edge->record].kind !=
				SG_RUNE_PHASE_TRANSITION_PORTAL)
			return 0;
		path[frame->path_depth + index] = step;
	}
	*step_count_out = reverse_count;
	return 1;
}

static int BeliefHorizonEmit(
	const sg_rune_runtime_snapshot_t *snapshot, uint32_t origin_phase,
	const sg_phase_coordinate_t *to, const belief_horizon_walk_frame_t *frame,
	const sg_belief_horizon_step_t *path, size_t path_length,
	float likelihood, sg_belief_horizon_entry_t *entries,
	sg_belief_horizon_step_t *steps, size_t *entry_write,
	size_t *step_write, int *overflowed_out)
{
	size_t first_step;
	size_t axis;

	if (!snapshot || !to || !frame || !entry_write || !step_write ||
		!overflowed_out || origin_phase >= snapshot->phase_count ||
		(path_length != 0U && !path) ||
		(entries && path_length != 0U && !steps))
		return 0;
	first_step = *step_write;
	if (!BeliefSizeAdd(*step_write, path_length, step_write) ||
		!BeliefSizeAdd(*entry_write, 1U, entry_write))
	{
		*overflowed_out = 1;
		return 0;
	}
	if (!entries)
		return 1;
	{
		sg_belief_horizon_entry_t *entry = &entries[*entry_write - 1U];
		entry->from = snapshot->phases[origin_phase];
		entry->to = *to;
		for (axis = 0U; axis < 3U; axis++)
			entry->displacement[axis] = BeliefHorizonMidpoint(
				&(sg_rune_interval_t){
					frame->displacement_min[axis],
					frame->displacement_max[axis] });
		entry->likelihood = likelihood;
		entry->first_step = first_step;
		entry->step_count = path_length;
		if (path_length != 0U)
			memcpy(&steps[first_step], path,
				path_length * sizeof(*path));
	}
	return 1;
}

static int BeliefHorizonWalk(
	const sg_rune_runtime_snapshot_t *snapshot, uint32_t origin_phase,
	uint64_t elapsed_ms, size_t max_depth, size_t record_count,
	const belief_portal_graph_t *portal_graph,
	belief_horizon_walk_frame_t *frames, sg_belief_horizon_step_t *path,
	float likelihood, sg_belief_horizon_entry_t *entries,
	sg_belief_horizon_step_t *steps, size_t *entry_write,
	size_t *step_write, int *overflowed_out)
{
	size_t top = 1U;

	if (!snapshot || !portal_graph || !frames || !entry_write ||
		!step_write || !overflowed_out || origin_phase >= snapshot->phase_count)
		return 0;

	memset(&frames[0], 0, sizeof(frames[0]));
	frames[0].phase = snapshot->phases[origin_phase];
	if (!BeliefHorizonFrameClosureBuild(snapshot, portal_graph, origin_phase,
		&frames[0], overflowed_out))
		goto failure;
	if (entries)
	{
		entries[*entry_write].from = frames[0].phase;
		entries[*entry_write].to = frames[0].phase;
		entries[*entry_write].likelihood = likelihood;
		entries[*entry_write].first_step = *step_write;
		entries[*entry_write].step_count = 0U;
		entries[*entry_write].displacement[0] = 0.0f;
		entries[*entry_write].displacement[1] = 0.0f;
		entries[*entry_write].displacement[2] = 0.0f;
	}
	if (!BeliefSizeAdd(*entry_write, 1U, entry_write))
	{
		*overflowed_out = 1;
		goto failure;
	}
	while (1)
	{
		belief_horizon_walk_frame_t *frame;
		belief_horizon_walk_frame_t next;
		belief_step_bounds_t bounds;
		sg_belief_horizon_step_t step;
		int record_status;
		size_t axis;
		size_t closure_steps;
		size_t path_length;

		frame = &frames[top - 1U];
		if (!frame->active)
		{
			if (frame->closure_cursor == frame->closure_count)
			{
				BeliefHorizonFrameDestroy(frame);
				top--;
				if (top == 0U)
					return 1;
				continue;
			}
			frame->active_phase =
				frame->closure_phases[frame->closure_cursor++];
			if (!BeliefHorizonClosurePath(snapshot, portal_graph, frame,
				frame->active_phase, max_depth, path, &closure_steps) ||
				!BeliefSizeAdd(frame->path_depth, closure_steps,
					&frame->active_path_length))
			{
				*overflowed_out = 1;
				goto failure;
			}
			/* The zero-time closure supplies one canonical path to each source
			 * phase, but it does not create another probabilistic outcome.  Only
			 * a positive-time record emits an entry.  This contracts portal SCCs
			 * for probability accounting while retaining their portal witnesses
			 * on paths that leave the contracted component. */
			frame->next_record = 0U;
			frame->active = 1U;
		}
		if (frame->next_record == record_count)
		{
			frame->active = 0U;
			continue;
		}
		record_status = BeliefHorizonRecord(snapshot,
			&snapshot->phases[frame->active_phase],
			frame->next_record++, &step, &bounds);
		if (record_status < 0)
			goto failure;
		if (record_status == 0)
			continue;
		if (step.kind == SG_BELIEF_HORIZON_PHASE_TRANSITION &&
			step.record_index < snapshot->model->phase_transition_count &&
			snapshot->model->phase_transitions[step.record_index].kind ==
			SG_RUNE_PHASE_TRANSITION_PORTAL)
			continue;
		memset(&next, 0, sizeof(next));
		next.phase = step.to;
		if (!BeliefFloatAdd(frame->duration_min_ms,
		    bounds.duration_min_ms, &next.duration_min_ms) ||
		    !BeliefFloatAdd(frame->duration_max_ms,
			bounds.duration_max_ms, &next.duration_max_ms))
		{
			*overflowed_out = 1;
			goto failure;
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
				goto failure;
			}
		if ((long double)next.duration_min_ms > (long double)elapsed_ms)
			continue;
		if (frame->active_path_length == max_depth || !path)
		{
			*overflowed_out = 1;
			goto failure;
		}
		path[frame->active_path_length] = step;
		path_length = frame->active_path_length + 1U;
		if (BeliefHorizonDurationContains(&(sg_rune_interval_t){
		    next.duration_min_ms, next.duration_max_ms }, elapsed_ms))
		{
			if (!BeliefHorizonEmit(snapshot, origin_phase, &next.phase,
				&next, path, path_length, likelihood, entries, steps,
				entry_write, step_write, overflowed_out))
				goto failure;
		}
		if (path_length < max_depth &&
		    (long double)next.duration_min_ms <= (long double)elapsed_ms)
		{
			if (top == max_depth + 1U)
			{
				*overflowed_out = 1;
				goto failure;
			}
			memset(&frames[top], 0, sizeof(frames[top]));
			frames[top] = next;
			frames[top].path_depth = path_length;
			if (!BeliefHorizonFrameClosureBuild(snapshot, portal_graph,
				frames[top].phase.phase_id, &frames[top],
				overflowed_out))
				goto failure;
			top++;
		}
	}

failure:
	while (top != 0U)
	{
		top--;
		BeliefHorizonFrameDestroy(&frames[top]);
	}
	return 0;
}

static sg_belief_horizon_kernel_t *BeliefHorizonFixedPointCreate(
	const sg_rune_runtime_snapshot_t *snapshot, uint64_t from_time_ms,
	uint64_t to_time_ms, int *overflowed_out, int *invalid_out)
{
	const sg_rune_model_t *model;
	belief_portal_graph_t portal_graph;
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
	int graph_invalid = 0;
	uint32_t phase;

	if (!overflowed_out || !invalid_out)
		return NULL;
	*overflowed_out = 0;
	*invalid_out = 0;
	if (!snapshot || !snapshot->model ||
		(to_time_ms < from_time_ms))
	{
		*invalid_out = 1;
		return NULL;
	}
	model = snapshot->model;
	memset(&portal_graph, 0, sizeof(portal_graph));
	if (!BeliefHorizonRecordCount(model, &record_count))
	{
		*overflowed_out = 1;
		return NULL;
	}
	if (!BeliefPortalGraphBuild(snapshot, &portal_graph, overflowed_out,
		&graph_invalid))
	{
		*invalid_out = graph_invalid && !*overflowed_out;
		BeliefPortalGraphDestroy(&portal_graph);
		return NULL;
	}
	depth_status = BeliefHorizonDepthBound(snapshot, elapsed_ms,
		&portal_graph, &max_depth, overflowed_out);
	if (depth_status <= 0)
	{
		*invalid_out = depth_status == 0 && !*overflowed_out;
		BeliefPortalGraphDestroy(&portal_graph);
		return NULL;
	}
	if (max_depth > SIZE_MAX / sizeof(*path) ||
	    max_depth == SIZE_MAX || max_depth + 1U > SIZE_MAX / sizeof(*frames))
	{
		*overflowed_out = 1;
		BeliefPortalGraphDestroy(&portal_graph);
		return NULL;
	}
	frames = calloc(max_depth + 1U, sizeof(*frames));
	path = max_depth == 0U ? NULL : calloc(max_depth, sizeof(*path));
	if (!frames || (max_depth != 0U && !path))
	{
		*invalid_out = !*overflowed_out;
		goto failure;
	}
	spans = calloc((size_t)snapshot->phase_count, sizeof(*spans));
	if (!spans)
	{
		goto failure;
	}
	for (phase = 0U; phase < snapshot->phase_count; phase++)
	{
		size_t phase_first = entry_count;
		if (!BeliefHorizonWalk(snapshot, phase, elapsed_ms, max_depth,
		    record_count, &portal_graph, frames, path, 0.0f, NULL, NULL,
		    &entry_count,
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
		    record_count, &portal_graph, frames, path, likelihood, entries,
		    steps, &write,
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
	BeliefPortalGraphDestroy(&portal_graph);
	return kernel;

failure:
	free(kernel);
	free(spans);
	free(entries);
	free(steps);
	free(frames);
	free(path);
	BeliefPortalGraphDestroy(&portal_graph);
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

static int BeliefHorizonPayloadDisjointFromAll(
	const sg_belief_horizon_kernel_t *kernels, size_t kernel_count,
	const belief_byte_range_t *writable, size_t writable_count)
{
	belief_byte_range_t read;
	size_t index;

	if (kernel_count == 0U)
		return kernels == NULL;
	if (!BeliefByteRange(kernels, kernel_count, sizeof(*kernels), &read) ||
	    !BeliefRangeDisjointFromAll(&read, writable, writable_count))
		return 0;
	for (index = 0U; index < kernel_count; index++)
	{
		const sg_belief_horizon_kernel_t *kernel = &kernels[index];

		if (!BeliefByteRange(kernel->origin_spans,
		    kernel->origin_span_count, sizeof(*kernel->origin_spans), &read) ||
		    !BeliefRangeDisjointFromAll(&read, writable, writable_count) ||
		    !BeliefByteRange(kernel->entries, kernel->entry_count,
			sizeof(*kernel->entries), &read) ||
		    !BeliefRangeDisjointFromAll(&read, writable, writable_count) ||
		    (kernel->step_count != 0U &&
		     (!BeliefByteRange(kernel->steps, kernel->step_count,
			sizeof(*kernel->steps), &read) ||
		      !BeliefRangeDisjointFromAll(&read, writable,
			writable_count))))
			return 0;
	}
	return 1;
}

static int BeliefHorizonRegistriesDisjointFromAll(
	const belief_byte_range_t *writable, size_t writable_count)
{
	const sg_belief_horizon_source_t *source;
	const sg_belief_horizon_authority_t *authority;
	const sg_belief_horizon_scope_t *scope;
	belief_byte_range_t read;

	for (source = belief_issued_sources; source;
	     source = source->next_issued)
		if (!BeliefByteRange(source, 1U, sizeof(*source), &read) ||
		    !BeliefRangeDisjointFromAll(&read, writable, writable_count) ||
		    !BeliefHorizonPayloadDisjointFromAll(source->kernels,
			source->kernel_count, writable, writable_count))
			return 0;
	for (authority = belief_issued_authorities; authority;
	     authority = authority->next_issued)
		if (!BeliefByteRange(authority, 1U, sizeof(*authority), &read) ||
		    !BeliefRangeDisjointFromAll(&read, writable, writable_count) ||
			!BeliefHorizonPayloadDisjointFromAll(authority->kernels,
			authority->kernel_count, writable, writable_count))
			return 0;
	for (scope = belief_horizon_scopes; scope; scope = scope->next_scope)
	{
		source = &scope->source;
		authority = &scope->authority;
		if (!BeliefByteRange(source, 1U, sizeof(*source), &read) ||
			!BeliefRangeDisjointFromAll(&read, writable, writable_count) ||
			!BeliefHorizonPayloadDisjointFromAll(source->kernels,
				source->kernel_count, writable, writable_count) ||
			!BeliefByteRange(authority, 1U, sizeof(*authority), &read) ||
			!BeliefRangeDisjointFromAll(&read, writable, writable_count) ||
			!BeliefHorizonPayloadDisjointFromAll(authority->kernels,
				authority->kernel_count, writable, writable_count) ||
			!BeliefHorizonPayloadDisjointFromAll(scope->evidence_kernels,
				scope->evidence_kernel_count, writable, writable_count) ||
			!BeliefHorizonPayloadDisjointFromAll(scope->frame_kernels,
				scope->frame_kernel_count, writable, writable_count))
			return 0;
	}
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
	if (!BeliefHorizonRegistriesDisjointFromAll(writable, writable_count))
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
	belief_byte_range_t writable[2];

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
	writable[0] = state_range;
	writable[1] = storage_range;
	if (!BeliefHorizonRegistriesDisjointFromAll(writable, 2U))
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
		SG_BeliefLifeIdentityEqual(&provenance->target_life,
			&state->target_life);
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

	if ((!BeliefHorizonAuthorityIssued(authority) &&
	     !BeliefHorizonAuthorityScoped(authority)) ||
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

static int BeliefHorizonIssueOutputValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	sg_belief_horizon_source_t **source_out)
{
	belief_byte_range_t writable;
	belief_byte_range_t read;

	return BeliefByteRange(source_out, 1U, sizeof(*source_out), &writable) &&
		BeliefRangeDisjointFromRune(snapshot, &writable) &&
		BeliefHorizonRegistriesDisjointFromAll(&writable, 1U) &&
		BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &read) &&
		!BeliefRangesOverlap(&writable, &read) &&
		BeliefByteRange(state, 1U, sizeof(*state), &read) &&
		!BeliefRangesOverlap(&writable, &read) &&
		BeliefByteRange(state->particles, state->particle_capacity,
			sizeof(*state->particles), &read) &&
		!BeliefRangesOverlap(&writable, &read);
}

static int BeliefHorizonViewOutputsValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_horizon_source_t *source,
	const sg_belief_horizon_kernel_t **kernels_out,
	size_t *kernel_count_out,
	sg_rune_v2_content_id_t *content_identity_out)
{
	belief_byte_range_t writable[3];
	belief_byte_range_t read;
	size_t left;
	size_t right;
	size_t index;

	if (!BeliefByteRange(kernels_out, 1U, sizeof(*kernels_out),
	    &writable[0]) ||
	    !BeliefByteRange(kernel_count_out, 1U, sizeof(*kernel_count_out),
		&writable[1]) ||
	    !BeliefByteRange(content_identity_out, 1U,
		sizeof(*content_identity_out), &writable[2]))
		return 0;
	if (!BeliefHorizonRegistriesDisjointFromAll(writable, 3U))
		return 0;
	for (left = 0U; left < 3U; left++)
	{
		if (!BeliefRangeDisjointFromRune(snapshot, &writable[left]))
			return 0;
		for (right = left + 1U; right < 3U; right++)
			if (BeliefRangesOverlap(&writable[left], &writable[right]))
				return 0;
	}
	if (!BeliefByteRange(snapshot, 1U, sizeof(*snapshot), &read) ||
	    !BeliefRangeDisjointFromAll(&read, writable, 3U) ||
	    !BeliefByteRange(state, 1U, sizeof(*state), &read) ||
	    !BeliefRangeDisjointFromAll(&read, writable, 3U) ||
	    !BeliefByteRange(state->particles, state->particle_capacity,
		sizeof(*state->particles), &read) ||
	    !BeliefRangeDisjointFromAll(&read, writable, 3U) ||
	    !BeliefByteRange(source, 1U, sizeof(*source), &read) ||
	    !BeliefRangeDisjointFromAll(&read, writable, 3U) ||
	    !BeliefByteRange(source->kernels, source->kernel_count,
		sizeof(*source->kernels), &read) ||
	    !BeliefRangeDisjointFromAll(&read, writable, 3U))
		return 0;
	for (index = 0U; index < source->kernel_count; index++)
	{
		const sg_belief_horizon_kernel_t *kernel = &source->kernels[index];
		if (!BeliefByteRange(kernel->origin_spans, kernel->origin_span_count,
		    sizeof(*kernel->origin_spans), &read) ||
		    !BeliefRangeDisjointFromAll(&read, writable, 3U) ||
		    !BeliefByteRange(kernel->entries, kernel->entry_count,
			sizeof(*kernel->entries), &read) ||
		    !BeliefRangeDisjointFromAll(&read, writable, 3U) ||
		    (kernel->step_count != 0U &&
		     (!BeliefByteRange(kernel->steps, kernel->step_count,
			sizeof(*kernel->steps), &read) ||
		      !BeliefRangeDisjointFromAll(&read, writable, 3U))))
			return 0;
	}
	return 1;
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

	if (!snapshot || !state || !source_out ||
	    !BeliefHorizonIssueOutputValid(snapshot, state, source_out) ||
	    *source_out ||
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
	source->provenance.target_life = state->target_life;
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
	if (!BeliefHorizonIssuanceIdentity(&source->issuance_identity))
	{
		BeliefHorizonKernelsDestroy(source->kernels, source->kernel_count);
		free(source);
		return SG_BELIEF_HORIZON_OVERFLOW;
	}
	source->active = 1U;
	source->next_issued = belief_issued_sources;
	belief_issued_sources = source;
	*source_out = source;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

int SG_BeliefHorizonSourceView(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_horizon_source_t *source,
	const sg_belief_horizon_kernel_t **kernels_out, size_t *kernel_count_out,
	sg_rune_v2_content_id_t *content_identity_out)
{
	belief_work_counters_t counters;

	if (!snapshot || !state || !kernels_out || !kernel_count_out ||
	    !content_identity_out ||
	    !BeliefHorizonSourceIssued(source) ||
	    !BeliefHorizonViewOutputsValid(snapshot, state, source, kernels_out,
		kernel_count_out, content_identity_out))
		return 0;
	memset(&counters, 0, sizeof(counters));
	if (!BeliefStateBoundToSnapshot(snapshot, state) ||
	    !BeliefHorizonSourceValid(source, snapshot, state, &counters))
		return 0;
	*kernels_out = source->kernels;
	*kernel_count_out = source->kernel_count;
	*content_identity_out = source->chain_identity;
	return 1;
}

void SG_BeliefHorizonSourceDestroy(sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *record = BeliefHorizonSourceRecord(source);

	if (!record || record->active != 1U)
		return;
	record->active = 0U;
	BeliefHorizonKernelsDestroy(record->kernels, record->kernel_count);
	record->kernels = NULL;
	record->kernel_count = 0U;
	memset(&record->chain_identity, 0, sizeof(record->chain_identity));
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
	    !BeliefHorizonRegistriesDisjointFromAll(&writable, 1U) ||
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
	if (!BeliefHorizonIssuanceIdentity(&authority->issuance_identity))
	{
		BeliefHorizonKernelsDestroy(authority->kernels,
			authority->kernel_count);
		free(authority);
		return SG_BELIEF_HORIZON_OVERFLOW;
	}
	authority->active = 1U;
	authority->next_issued = belief_issued_authorities;
	belief_issued_authorities = authority;
	*authority_out = authority;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

void SG_BeliefHorizonAuthorityDestroy(
	sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	if (!record || record->active != 1U)
		return;
	record->active = 0U;
	BeliefHorizonKernelsDestroy(record->kernels, record->kernel_count);
	record->kernels = NULL;
	record->kernel_count = 0U;
	memset(&record->chain_identity, 0, sizeof(record->chain_identity));
}

static void BeliefHorizonScopePayloadClear(
	sg_belief_horizon_scope_t *scope)
{
	if (!scope)
		return;
	BeliefHorizonKernelsDestroy(scope->source.kernels,
		scope->source.kernel_count);
	BeliefHorizonKernelsDestroy(scope->authority.kernels,
		scope->authority.kernel_count);
	BeliefHorizonKernelsDestroy(scope->evidence_kernels,
		scope->evidence_kernel_count);
	free(scope->frame_kernels);
	memset(&scope->source, 0, sizeof(scope->source));
	memset(&scope->authority, 0, sizeof(scope->authority));
	scope->evidence_kernels = NULL;
	scope->evidence_kernel_count = 0U;
	scope->frame_kernels = NULL;
	scope->frame_kernel_count = 0U;
}

static sg_belief_horizon_accept_result_t BeliefHorizonScopeSourceBuild(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, uint64_t to_time_ms,
	sg_belief_horizon_source_t *source)
{
	belief_work_counters_t counters;
	int overflowed = 0;
	int invalid = 0;

	if (!snapshot || !state || !source ||
		!BeliefStateBoundToSnapshot(snapshot, state) ||
		to_time_ms <= state->updated_at_ms)
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
	memset(source, 0, sizeof(*source));
	source->kernels = BeliefHorizonFixedPointCreate(snapshot,
		state->updated_at_ms, to_time_ms, &overflowed, &invalid);
	if (!source->kernels)
		return overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			invalid ? SG_BELIEF_HORIZON_REJECTED_INVALID :
			SG_BELIEF_HORIZON_ALLOCATION_FAILED;
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
	source->provenance.target_life = state->target_life;
	source->kernel_count = 1U;
	memset(&counters, 0, sizeof(counters));
	if (!BeliefHorizonChainValid(snapshot, source->kernels,
		source->kernel_count, state->updated_at_ms, to_time_ms, &counters) ||
		!BeliefHorizonChainIdentity(&source->provenance, source->kernels,
			source->kernel_count, &source->chain_identity))
	{
		BeliefHorizonKernelsDestroy(source->kernels, source->kernel_count);
		memset(source, 0, sizeof(*source));
		return counters.overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			SG_BELIEF_HORIZON_REJECTED_INVALID;
	}
	source->active = 1U;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

static sg_belief_horizon_accept_result_t BeliefHorizonScopeAuthorityBuild(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_horizon_source_t *source,
	sg_belief_horizon_authority_t *authority)
{
	belief_work_counters_t counters;

	if (!snapshot || !state || !source || !authority ||
		source->active != 1U || source->issuance_identity != 0U ||
		!BeliefHorizonProvenanceMatches(&source->provenance, snapshot, state,
			source->provenance.to_time_ms))
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
	memset(&counters, 0, sizeof(counters));
	if (!BeliefHorizonChainValid(snapshot, source->kernels,
		source->kernel_count, state->updated_at_ms,
		source->provenance.to_time_ms, &counters))
		return counters.overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			SG_BELIEF_HORIZON_REJECTED_INVALID;
	memset(authority, 0, sizeof(*authority));
	authority->kernels = BeliefHorizonKernelsClone(source->kernels,
		source->kernel_count);
	if (!authority->kernels)
		return SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	authority->provenance = source->provenance;
	authority->kernel_count = source->kernel_count;
	if (!BeliefHorizonChainIdentity(&authority->provenance,
		authority->kernels, authority->kernel_count,
		&authority->chain_identity))
	{
		BeliefHorizonKernelsDestroy(authority->kernels,
			authority->kernel_count);
		memset(authority, 0, sizeof(*authority));
		return SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	}
	authority->active = 1U;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

static sg_belief_horizon_accept_result_t BeliefHorizonScopeEvidenceBuild(
	const sg_rune_runtime_snapshot_t *snapshot, uint64_t observed_at_ms,
	uint64_t to_time_ms, sg_belief_horizon_kernel_t **kernels_out,
	size_t *kernel_count_out)
{
	belief_work_counters_t counters;
	sg_belief_horizon_kernel_t *kernels;
	int overflowed = 0;
	int invalid = 0;

	if (!snapshot || !kernels_out || !kernel_count_out ||
		observed_at_ms >= to_time_ms)
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
	*kernels_out = NULL;
	*kernel_count_out = 0U;
	kernels = BeliefHorizonFixedPointCreate(snapshot, observed_at_ms,
		to_time_ms, &overflowed, &invalid);
	if (!kernels)
		return overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			invalid ? SG_BELIEF_HORIZON_REJECTED_INVALID :
			SG_BELIEF_HORIZON_ALLOCATION_FAILED;
	memset(&counters, 0, sizeof(counters));
	if (!BeliefHorizonChainValid(snapshot, kernels, 1U, observed_at_ms,
		to_time_ms, &counters))
	{
		BeliefHorizonKernelsDestroy(kernels, 1U);
		return counters.overflowed ? SG_BELIEF_HORIZON_OVERFLOW :
			SG_BELIEF_HORIZON_REJECTED_INVALID;
	}
	*kernels_out = kernels;
	*kernel_count_out = 1U;
	return SG_BELIEF_HORIZON_ACCEPTED;
}

sg_belief_horizon_scope_t *SG_BeliefHorizonScopeCreate(void)
{
	sg_belief_horizon_scope_t *scope = calloc(1U, sizeof(*scope));

	if (!scope)
		return NULL;
	scope->next_scope = belief_horizon_scopes;
	belief_horizon_scopes = scope;
#if defined(SG_BELIEF_TESTING)
	belief_scope_allocation_count++;
#endif
	return scope;
}

void SG_BeliefHorizonScopeDestroy(sg_belief_horizon_scope_t *scope)
{
	sg_belief_horizon_scope_t **cursor;

	if (!scope)
		return;
	for (cursor = &belief_horizon_scopes; *cursor;
		cursor = &(*cursor)->next_scope)
		if (*cursor == scope)
		{
			*cursor = scope->next_scope;
			BeliefHorizonScopePayloadClear(scope);
			free(scope);
			return;
		}
}

sg_belief_horizon_accept_result_t SG_BeliefHorizonScopePrepare(
	sg_belief_horizon_scope_t *scope,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, uint64_t to_time_ms,
	uint64_t evidence_observed_at_ms)
{
	sg_belief_horizon_source_t source;
	sg_belief_horizon_authority_t authority;
	sg_belief_horizon_kernel_t *evidence_kernels = NULL;
	sg_belief_horizon_kernel_t *frame_kernels = NULL;
	size_t evidence_kernel_count = 0U;
	size_t frame_kernel_count = 0U;
	sg_belief_horizon_accept_result_t result;

	if (!BeliefHorizonScopeRecord(scope))
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
	if (!state || (evidence_observed_at_ms != 0U &&
		(evidence_observed_at_ms < state->updated_at_ms ||
		 evidence_observed_at_ms > to_time_ms)))
		return SG_BELIEF_HORIZON_REJECTED_INVALID;
#if defined(SG_BELIEF_TESTING)
	if (belief_scope_fail_next != SG_BELIEF_HORIZON_ACCEPTED)
	{
		result = belief_scope_fail_next;
		belief_scope_fail_next = SG_BELIEF_HORIZON_ACCEPTED;
		return result;
	}
#endif
	memset(&source, 0, sizeof(source));
	memset(&authority, 0, sizeof(authority));
	result = BeliefHorizonScopeSourceBuild(snapshot, state, to_time_ms,
		&source);
	if (result != SG_BELIEF_HORIZON_ACCEPTED)
		return result;
	result = BeliefHorizonScopeAuthorityBuild(snapshot, state, &source,
		&authority);
	if (result != SG_BELIEF_HORIZON_ACCEPTED)
		goto failure;
	/* The reducer needs both exact intervals: the direct projection advances
	 * the accepted track, while the supplemental kernel advances evidence from
	 * its authenticated observation time.  Both come from the same immutable
	 * snapshot and are checked before replacing the current scope. */
	if (evidence_observed_at_ms > state->updated_at_ms &&
		evidence_observed_at_ms < to_time_ms)
	{
		result = BeliefHorizonScopeEvidenceBuild(snapshot,
			evidence_observed_at_ms, to_time_ms, &evidence_kernels,
			&evidence_kernel_count);
		if (result != SG_BELIEF_HORIZON_ACCEPTED)
			goto failure;
		frame_kernel_count = 2U;
		frame_kernels = calloc(frame_kernel_count, sizeof(*frame_kernels));
		if (!frame_kernels)
		{
			result = SG_BELIEF_HORIZON_ALLOCATION_FAILED;
			goto failure;
		}
		frame_kernels[0] = authority.kernels[0];
		frame_kernels[1] = evidence_kernels[0];
	}
	BeliefHorizonScopePayloadClear(scope);
	scope->source = source;
	scope->authority = authority;
	scope->evidence_kernels = evidence_kernels;
	scope->evidence_kernel_count = evidence_kernel_count;
	scope->frame_kernels = frame_kernels;
	scope->frame_kernel_count = frame_kernel_count;
	return SG_BELIEF_HORIZON_ACCEPTED;

failure:
	free(frame_kernels);
	BeliefHorizonKernelsDestroy(evidence_kernels, evidence_kernel_count);
	BeliefHorizonKernelsDestroy(authority.kernels, authority.kernel_count);
	BeliefHorizonKernelsDestroy(source.kernels, source.kernel_count);
	return result;
}

const sg_belief_horizon_authority_t *SG_BeliefHorizonScopeAuthority(
	const sg_belief_horizon_scope_t *scope)
{
	sg_belief_horizon_scope_t *record = BeliefHorizonScopeRecord(scope);

	return record && record->authority.active == 1U ?
		&record->authority : NULL;
}

const sg_belief_horizon_kernel_t *SG_BeliefHorizonScopeKernels(
	const sg_belief_horizon_scope_t *scope, size_t *kernel_count_out)
{
	sg_belief_horizon_scope_t *record = BeliefHorizonScopeRecord(scope);

	if (!record || record->authority.active != 1U || !kernel_count_out)
		return NULL;
	if (record->frame_kernel_count != 0U)
	{
		*kernel_count_out = record->frame_kernel_count;
		return record->frame_kernels;
	}
	*kernel_count_out = record->authority.kernel_count;
	return record->authority.kernels;
}

#if defined(SG_BELIEF_TESTING)
void SG_BeliefTestHorizonAuthorityCorrupt(
	sg_belief_horizon_authority_t *authority)
{
	if (BeliefHorizonAuthorityIssued(authority))
		authority->chain_identity.bytes[0] ^= UINT8_C(1);
}

uint64_t SG_BeliefTestHorizonSourceIssuanceIdentity(
	const sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *record = BeliefHorizonSourceRecord(source);

	return record ? record->issuance_identity : 0U;
}

uint64_t SG_BeliefTestHorizonAuthorityIssuanceIdentity(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	return record ? record->issuance_identity : 0U;
}

void *SG_BeliefTestHorizonSourcePayloadPointerSlot(
	const sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *record = BeliefHorizonSourceRecord(source);

	return record ? (void *)&record->kernels : NULL;
}

void *SG_BeliefTestHorizonAuthorityPayloadPointerSlot(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	return record ? (void *)&record->kernels : NULL;
}

void *SG_BeliefTestHorizonSourceNextPointerSlot(
	const sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *record = BeliefHorizonSourceRecord(source);

	return record ? (void *)&record->next_issued : NULL;
}

void *SG_BeliefTestHorizonAuthorityNextPointerSlot(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	return record ? (void *)&record->next_issued : NULL;
}

void *SG_BeliefTestHorizonAuthorityFirstEntryStepSlot(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	if (!record || record->kernel_count == 0U || !record->kernels ||
	    record->kernels[0].entry_count == 0U ||
	    !record->kernels[0].entries)
		return NULL;
	return (void *)&record->kernels[0].entries[0].first_step;
}

int SG_BeliefTestHorizonSourceRetired(
	const sg_belief_horizon_source_t *source)
{
	sg_belief_horizon_source_t *record = BeliefHorizonSourceRecord(source);

	return record && record->active == 0U && record->kernels == NULL &&
		record->kernel_count == 0U;
}

int SG_BeliefTestHorizonAuthorityRetired(
	const sg_belief_horizon_authority_t *authority)
{
	sg_belief_horizon_authority_t *record =
		BeliefHorizonAuthorityRecord(authority);

	return record && record->active == 0U && record->kernels == NULL &&
		record->kernel_count == 0U;
}

void SG_BeliefTestHorizonScopeFailNext(
	sg_belief_horizon_accept_result_t result)
{
	if (result == SG_BELIEF_HORIZON_ALLOCATION_FAILED ||
		result == SG_BELIEF_HORIZON_OVERFLOW)
		belief_scope_fail_next = result;
}

size_t SG_BeliefTestHorizonScopeLiveCount(void)
{
	const sg_belief_horizon_scope_t *scope;
	size_t count = 0U;

	for (scope = belief_horizon_scopes; scope; scope = scope->next_scope)
		count++;
	return count;
}

size_t SG_BeliefTestHorizonScopeAllocationCount(void)
{
	return belief_scope_allocation_count;
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
	if (!BeliefHorizonRegistriesDisjointFromAll(writable, writable_count))
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
