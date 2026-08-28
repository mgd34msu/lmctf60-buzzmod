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
	size_t evaluated_outcomes;
} belief_work_counters_t;

static void BeliefReportCounters(sg_belief_reduction_t *reduction,
	const belief_work_counters_t *counters)
{
	reduction->validated_phase_spans = counters->validated_phase_spans;
	reduction->validated_horizon_entries =
		counters->validated_horizon_entries;
	reduction->evaluated_outcomes = counters->evaluated_outcomes;
}

static int BeliefStateBoundToSnapshot(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state)
{
	size_t index;

	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !SG_BeliefStateValid(state) ||
	    state->rune_identity != snapshot->identity ||
	    state->topology_revision != snapshot->topology_revision)
		return 0;
	for (index = 0U; index < state->particle_count; index++)
		if (!SG_PhaseCoordinateValid(snapshot, &state->particles[index].phase))
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

static sg_belief_reduce_result_t BeliefEvidenceValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, const sg_belief_evidence_t *evidence,
	uint64_t at_ms)
{
	const sg_belief_provenance_t *provenance;
	size_t index;

	if (!evidence)
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
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
	    (evidence->kind == SG_BELIEF_EVIDENCE_POSITIVE &&
	     evidence->source == SG_BELIEF_SOURCE_SIGHT &&
	     evidence->support_count != 1U))
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	if (evidence->valid_until_ms < at_ms)
		return SG_BELIEF_REDUCE_REJECTED_STALE;
	for (index = 0U; index < evidence->support_count; index++)
		if (!BeliefSupportValid(snapshot, &evidence->supports[index]) ||
		    (evidence->kind == SG_BELIEF_EVIDENCE_NEGATIVE &&
		     evidence->supports[index].likelihood > 1.0f))
			return SG_BELIEF_REDUCE_REJECTED_INVALID;
	if (evidence->kind == SG_BELIEF_EVIDENCE_POSITIVE &&
	    (evidence->source == SG_BELIEF_SOURCE_SOUND ||
	     evidence->source == SG_BELIEF_SOURCE_DAMAGE) &&
	    evidence->support_count == 1U &&
	    evidence->supports[0].spread_radius == 0.0f)
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	return SG_BELIEF_REDUCE_APPLIED;
}

static int BeliefHorizonEntryValid(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_entry_t *entry)
{
	return entry && SG_PhaseCoordinateValid(snapshot, &entry->from) &&
		SG_PhaseCoordinateValid(snapshot, &entry->to) &&
		BeliefVectorValid(entry->displacement) &&
		SG_BeliefFloatValid(entry->likelihood) && entry->likelihood > 0.0f &&
		entry->likelihood <= 1.0f;
}

static int BeliefHorizonKernelValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_horizon_kernel_t *kernel,
	belief_work_counters_t *counters)
{
	size_t phase;
	size_t cursor = 0U;

	if (!kernel || kernel->rune_identity != snapshot->identity ||
	    kernel->topology_revision != snapshot->topology_revision ||
	    kernel->from_time_ms == 0U ||
	    kernel->from_time_ms >= kernel->to_time_ms ||
	    kernel->host_complete != 1U || kernel->reserved[0] != 0U ||
	    kernel->reserved[1] != 0U || kernel->reserved[2] != 0U ||
	    kernel->reserved[3] != 0U || kernel->reserved[4] != 0U ||
	    kernel->reserved[5] != 0U || kernel->reserved[6] != 0U ||
	    kernel->origin_span_count != (size_t)snapshot->phase_count ||
	    !kernel->origin_spans || kernel->entry_count == 0U || !kernel->entries)
		return 0;
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
			if (!BeliefHorizonEntryValid(snapshot, entry) ||
			    entry->from.phase_id != phase ||
			    entry->from.cell_id != snapshot->phases[phase].cell_id)
				return 0;
			total += (double)entry->likelihood;
			if (counters->validated_horizon_entries != SIZE_MAX)
				counters->validated_horizon_entries++;
		}
		if (fabs(total - 1.0) > (double)SG_BELIEF_WEIGHT_EPSILON)
			return 0;
		if (!BeliefSizeAdd(cursor, span->entry_count, &cursor))
			return 0;
		if (counters->validated_phase_spans != SIZE_MAX)
			counters->validated_phase_spans++;
		if (cursor > kernel->entry_count)
			return 0;
	}
	return cursor == kernel->entry_count;
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

static sg_belief_reduce_result_t BeliefPreflight(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, const sg_belief_frame_t *frame,
	size_t *required_scratch, belief_work_counters_t *counters)
{
	size_t index;
	size_t required;
	size_t advance_extra = 0U;
	uint64_t prior_evidence_sequence;

	if (!frame || !required_scratch || !counters || frame->sequence == 0U ||
	    frame->expected_revision == 0U || frame->at_ms == 0U ||
	    frame->expected_revision != state->revision ||
	    frame->at_ms < state->updated_at_ms ||
	    (frame->kernel_count != 0U && !frame->kernels) ||
	    (frame->evidence_count != 0U && !frame->evidence) ||
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
			return SG_BELIEF_REDUCE_REJECTED_INVALID;
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
					return SG_BELIEF_REDUCE_CAPACITY;
				}
		if (!BeliefSizeAdd(required, advance_extra, &required))
		{
			*required_scratch = SIZE_MAX;
			return SG_BELIEF_REDUCE_CAPACITY;
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
							return SG_BELIEF_REDUCE_CAPACITY;
						}
			}
			if (!BeliefSizeAdd(required, advance_extra, &required))
			{
				*required_scratch = SIZE_MAX;
				return SG_BELIEF_REDUCE_CAPACITY;
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

	if (particle->weight <= 0.0f)
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

static int BeliefIntegrateParticle(sg_belief_particle_t *particle,
	uint64_t elapsed_ms, uint64_t at_ms, float spread_growth)
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
	return 1;
}

static int BeliefNormalize(sg_belief_particle_t *particles, size_t *count)
{
	size_t index;
	size_t write = 0U;
	double total = 0.0;
	float divisor;

	for (index = 0U; index < *count; index++)
		if (particles[index].weight > 0.0f)
		{
			if (!SG_BeliefFloatValid(particles[index].weight))
				return 0;
			total += (double)particles[index].weight;
			if (!isfinite(total))
				return 0;
			if (write != index)
				particles[write] = particles[index];
			write++;
		}
	*count = write;
	if (write == 0U)
		return 1;
	if (total <= 0.0 || total > (double)FLT_MAX)
		return 0;
	divisor = (float)total;
	for (index = 0U; index < write; index++)
		particles[index].weight /= divisor;
	return 1;
}

static int BeliefAdvance(sg_belief_state_t *candidate,
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
			if (!BeliefIntegrateParticle(&retained, elapsed_ms,
			    frame->at_ms, candidate->policy.spread_growth_per_ms))
				return 0;
			if (kernel)
				retained.weight *=
					1.0f - candidate->policy.diffusion_fraction;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
		}
		if (kernel)
			for (offset = 0U; offset < span->entry_count; offset++)
				{
					uint8_t axis;
					sg_belief_particle_t moved;
					const sg_belief_horizon_entry_t *entry =
						&kernel->entries[span->first_entry + offset];
					BeliefCopyParticle(&moved, source);
					if (!BeliefIntegrateParticle(&moved, elapsed_ms,
					    frame->at_ms,
					    candidate->policy.spread_growth_per_ms))
						return 0;
					moved.phase = entry->to;
					for (axis = 0U; axis < 3U; axis++)
						moved.position[axis] +=
							entry->displacement[axis];
					moved.weight *=
						candidate->policy.diffusion_fraction *
						entry->likelihood;
					if (counters->evaluated_outcomes != SIZE_MAX)
						counters->evaluated_outcomes++;
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

static float BeliefNegativeOverlap(const sg_belief_state_t *candidate,
	const sg_belief_evidence_t *evidence,
	const sg_belief_evidence_support_t *support,
	const sg_belief_frame_t *frame, const sg_belief_particle_t *particle,
	belief_work_counters_t *counters)
{
	uint64_t delay_ms = frame->at_ms - evidence->observed_at_ms;
	const sg_belief_horizon_kernel_t *kernel = NULL;
	sg_belief_particle_t region;
	float overlap;
	float total = 0.0f;
	const sg_belief_horizon_span_t *span;
	size_t offset;

	BeliefParticleFromSupport(&region, evidence, support, 1.0f);
	if (!BeliefIntegrateParticle(&region, delay_ms, frame->at_ms,
	    candidate->policy.spread_growth_per_ms))
		return -1.0f;
	if (delay_ms > 0U && candidate->policy.diffusion_fraction > 0.0f)
		kernel = BeliefFindHorizonKernel(frame, evidence->observed_at_ms,
			frame->at_ms);
	overlap = BeliefSpatialOverlap(&region, particle);
	if (overlap < 0.0f)
		return -1.0f;
	total = overlap * (kernel ?
		1.0f - candidate->policy.diffusion_fraction : 1.0f);
	if (kernel)
	{
		span = BeliefHorizonSpan(kernel, &support->phase);
		for (offset = 0U; offset < span->entry_count; offset++)
			{
				uint8_t axis;
				sg_belief_particle_t moved;
				const sg_belief_horizon_entry_t *entry =
					&kernel->entries[span->first_entry + offset];
				float moved_overlap;
				BeliefCopyParticle(&moved, &region);
				moved.phase = entry->to;
				for (axis = 0U; axis < 3U; axis++)
					moved.position[axis] +=
						entry->displacement[axis];
				moved_overlap = BeliefSpatialOverlap(&moved, particle);
				if (moved_overlap < 0.0f)
					return -1.0f;
				total += candidate->policy.diffusion_fraction *
					entry->likelihood * moved_overlap;
				if (counters->evaluated_outcomes != SIZE_MAX)
					counters->evaluated_outcomes++;
			}
	}
	if (!SG_BeliefFloatValid(total))
		return -1.0f;
	return total > 1.0f ? 1.0f : total;
}

static int BeliefApplyPositive(sg_belief_state_t *candidate,
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
			retained.weight *= old_scale;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
		}
	for (index = 0U; index < evidence->support_count; index++)
	{
		const sg_belief_horizon_span_t *span =
			BeliefHorizonSpan(kernel, &evidence->supports[index].phase);
		size_t offset;
		float weight = confidence * (float)
			((double)evidence->supports[index].likelihood / support_total);
		if (candidate->policy.diffusion_fraction < 1.0f || !kernel)
		{
			sg_belief_particle_t retained;
			BeliefParticleFromSupport(&retained, evidence,
				&evidence->supports[index], weight);
			if (!BeliefIntegrateParticle(&retained, delay_ms, frame->at_ms,
			    candidate->policy.spread_growth_per_ms))
				return 0;
			if (kernel)
				retained.weight *=
					1.0f - candidate->policy.diffusion_fraction;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
		}
		if (kernel)
			for (offset = 0U; offset < span->entry_count; offset++)
				{
					sg_belief_particle_t moved;
					uint8_t axis;
					const sg_belief_horizon_entry_t *entry =
						&kernel->entries[span->first_entry + offset];
					BeliefParticleFromSupport(&moved, evidence,
						&evidence->supports[index], weight);
					if (!BeliefIntegrateParticle(&moved, delay_ms,
					    frame->at_ms,
					    candidate->policy.spread_growth_per_ms))
						return 0;
					moved.phase = entry->to;
					for (axis = 0U; axis < 3U; axis++)
						moved.position[axis] +=
							entry->displacement[axis];
					moved.weight *= candidate->policy.diffusion_fraction *
						entry->likelihood;
					if (counters->evaluated_outcomes != SIZE_MAX)
						counters->evaluated_outcomes++;
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

static int BeliefApplyNegative(sg_belief_state_t *candidate,
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
		float factor = 1.0f;
		for (support = 0U; support < evidence->support_count; support++)
		{
			float overlap = BeliefNegativeOverlap(candidate, evidence,
				&evidence->supports[support], frame,
				&(*current)[index], counters);
			if (overlap < 0.0f)
				return 0;
			factor *= 1.0f - confidence *
				evidence->supports[support].likelihood * overlap;
		}
		if ((*current)[index].weight * factor > 0.0f)
		{
			sg_belief_particle_t retained;
			BeliefCopyParticle(&retained, &(*current)[index]);
			retained.weight *= factor;
			retained.source_mask |=
				(uint16_t)(UINT16_C(1) << evidence->source);
			retained.latest_evidence_id =
				evidence->provenance.evidence_id;
			retained.latest_evidence_at_ms = evidence->observed_at_ms;
			if (!BeliefAppendParticle(*next, &write, &retained))
				return 0;
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

	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !state || !config || !storage ||
	    capacity == 0U || !SG_BeliefTeamValid(config->audience_team) ||
	    !SG_BeliefTeamValid(config->target_team) ||
	    config->target_client >= SG_BELIEF_MAX_CLIENTS ||
	    config->initialized_at_ms == 0U || !BeliefPolicyValid(&config->policy))
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

	if (!snapshot || !state || !frame || !out ||
	    !BeliefStateBoundToSnapshot(snapshot, state))
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	memset(&reduction, 0, sizeof(reduction));
	memset(&counters, 0, sizeof(counters));
	reduction.committed_revision = state->revision;
	reduction.particle_count = state->particle_count;
	reduction.confidence = state->confidence;
	if (state->last_frame_sequence != 0U &&
	    frame->sequence == state->last_frame_sequence)
	{
		reduction.result = SG_BELIEF_REDUCE_DUPLICATE;
		*out = reduction;
		return reduction.result;
	}
	preflight = BeliefPreflight(snapshot, state, frame, &required_scratch,
		&counters);
	if (preflight != SG_BELIEF_REDUCE_APPLIED)
	{
		reduction.result = preflight;
		if (preflight == SG_BELIEF_REDUCE_CAPACITY)
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
	if (!BeliefAdvance(&candidate, frame, &current, &next, &count,
	    &counters))
	{
		reduction.result = SG_BELIEF_REDUCE_REJECTED_INVALID;
		BeliefReportCounters(&reduction, &counters);
		*out = reduction;
		return reduction.result;
	}
	for (index = 0U; index < frame->evidence_count; index++)
	{
		const sg_belief_evidence_t *evidence = &frame->evidence[index];
		int applied;
		if (evidence->kind == SG_BELIEF_EVIDENCE_POSITIVE)
			applied = BeliefApplyPositive(&candidate, evidence, frame, &current,
				&next, &count, &counters);
		else
			applied = BeliefApplyNegative(&candidate, evidence, frame, &current,
				&next, &count, &counters);
		if (!applied)
		{
			reduction.result = SG_BELIEF_REDUCE_REJECTED_INVALID;
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
			reduction.result = SG_BELIEF_REDUCE_CAPACITY;
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
	uint64_t elapsed_ms;
	float confidence;
	size_t required;
	size_t index;

	if (!out || !storage || !BeliefStateBoundToSnapshot(snapshot, state) ||
	    at_time_ms < state->updated_at_ms || storage == state->particles)
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
		if (!BeliefIntegrateParticle(&checked, elapsed_ms, at_time_ms,
		    state->policy.spread_growth_per_ms))
			return 0;
	}
	candidate.particle_count = required;
	for (index = 0U; index < required; index++)
	{
		BeliefCopyParticle(&storage[index], &state->particles[index]);
		if (!BeliefIntegrateParticle(&storage[index], elapsed_ms, at_time_ms,
		    state->policy.spread_growth_per_ms))
			return 0;
	}
	*out = candidate;
	return 1;
}
