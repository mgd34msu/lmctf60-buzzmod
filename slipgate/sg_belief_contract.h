/* Sparse earned-information beliefs over immutable RUNE runtime phases. */
#ifndef SG_BELIEF_CONTRACT_H
#define SG_BELIEF_CONTRACT_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_destination_field.h"

#define SG_BELIEF_MAX_CLIENTS 256U
#define SG_BELIEF_WEIGHT_EPSILON 0.000001f

typedef enum sg_belief_evidence_source_e
{
	SG_BELIEF_SOURCE_SIGHT = 0,
	SG_BELIEF_SOURCE_SOUND,
	SG_BELIEF_SOURCE_DAMAGE,
	SG_BELIEF_SOURCE_ITEM,
	SG_BELIEF_SOURCE_FLAG,
	SG_BELIEF_SOURCE_TEAMMATE,
	SG_BELIEF_SOURCE_COUNT
} sg_belief_evidence_source_t;

typedef enum sg_belief_issuer_kind_e
{
	SG_BELIEF_ISSUER_LOCAL_SENSOR = 0,
	SG_BELIEF_ISSUER_TEAMMATE,
	SG_BELIEF_ISSUER_KIND_COUNT
} sg_belief_issuer_kind_t;

typedef enum sg_belief_evidence_kind_e
{
	SG_BELIEF_EVIDENCE_POSITIVE = 0,
	SG_BELIEF_EVIDENCE_NEGATIVE,
	SG_BELIEF_EVIDENCE_KIND_COUNT
} sg_belief_evidence_kind_t;

typedef enum sg_belief_motion_state_e
{
	SG_BELIEF_MOTION_UNKNOWN = 0,
	SG_BELIEF_MOTION_GROUND,
	SG_BELIEF_MOTION_AIR,
	SG_BELIEF_MOTION_WATER,
	SG_BELIEF_MOTION_HOOK,
	SG_BELIEF_MOTION_MOVER,
	SG_BELIEF_MOTION_COUNT
} sg_belief_motion_state_t;

typedef struct sg_belief_provenance_s
{
	uint8_t authenticated;
	sg_belief_issuer_kind_t issuer_kind;
	uint8_t issuer_team;
	uint8_t audience_team;
	uint16_t issuer_client;
	uint16_t reserved;
	uint64_t evidence_id;
	uint64_t evidence_sequence;
	uint64_t authenticated_at_ms;
} sg_belief_provenance_t;

typedef struct sg_belief_evidence_support_s
{
	sg_phase_coordinate_t phase;
	sg_belief_motion_state_t movement_state;
	uint8_t weapon_state;
	uint8_t reserved[3];
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float likelihood;
} sg_belief_evidence_support_t;

typedef struct sg_belief_evidence_s
{
	sg_belief_provenance_t provenance;
	sg_belief_evidence_source_t source;
	sg_belief_evidence_kind_t kind;
	uint8_t target_team;
	uint16_t target_client;
	uint8_t reserved;
	uint64_t observed_at_ms;
	uint64_t valid_until_ms;
	float confidence;
	const sg_belief_evidence_support_t *supports;
	size_t support_count;
} sg_belief_evidence_t;

typedef struct sg_belief_particle_s
{
	sg_phase_coordinate_t phase;
	sg_belief_motion_state_t movement_state;
	uint8_t weapon_state;
	uint8_t reserved;
	uint16_t source_mask;
	uint16_t reserved2;
	uint64_t future_time_ms;
	uint64_t latest_evidence_id;
	uint64_t latest_evidence_at_ms;
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float weight;
} sg_belief_particle_t;

typedef struct sg_belief_policy_s
{
	uint64_t confidence_decay_ms;
	float diffusion_fraction;
	float spread_growth_per_ms;
} sg_belief_policy_t;

typedef struct sg_belief_state_config_s
{
	uint8_t audience_team;
	uint8_t target_team;
	uint16_t target_client;
	uint64_t initialized_at_ms;
	sg_belief_policy_t policy;
} sg_belief_state_config_t;

/* particles is caller-owned mutable storage. It must remain alive and exclusive
 * to this track until the track is retired. Frame inputs never escape a call. */
typedef struct sg_belief_state_s
{
	uint8_t audience_team;
	uint8_t target_team;
	uint16_t target_client;
	size_t particle_count;
	size_t particle_capacity;
	uint64_t generation;
	uint64_t revision;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t updated_at_ms;
	uint64_t last_frame_sequence;
	uint64_t last_evidence_sequence;
	sg_belief_provenance_t latest_provenance;
	sg_belief_evidence_source_t latest_source;
	uint64_t latest_observed_at_ms;
	uint64_t latest_valid_until_ms;
	float latest_evidence_confidence;
	float confidence;
	float total_weight;
	sg_belief_policy_t policy;
	sg_belief_particle_t *particles;
} sg_belief_state_t;

/* One weighted outcome in a host-certified complete horizon kernel. An entry
 * may summarize any number of valid movements during the kernel interval. */
typedef struct sg_belief_horizon_entry_s
{
	sg_phase_coordinate_t from;
	sg_phase_coordinate_t to;
	float displacement[3];
	float likelihood;
} sg_belief_horizon_entry_t;

typedef struct sg_belief_horizon_span_s
{
	size_t first_entry;
	size_t entry_count;
} sg_belief_horizon_span_t;

/* Runtime-only movement evidence. For every phase in the bound snapshot,
 * entries contain at least one outcome and its outgoing likelihoods sum to
 * one. host_complete certifies that the host included every phase reachable
 * through valid movement during [from_time_ms, to_time_ms]. */
typedef struct sg_belief_horizon_kernel_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t from_time_ms;
	uint64_t to_time_ms;
	uint8_t host_complete;
	uint8_t reserved[7];
	/* One span per dense snapshot phase_id. Entries are grouped by origin and
	 * each entry's full phase coordinate must match that origin. */
	const sg_belief_horizon_span_t *origin_spans;
	size_t origin_span_count;
	const sg_belief_horizon_entry_t *entries;
	size_t entry_count;
} sg_belief_horizon_kernel_t;

typedef struct sg_belief_frame_s
{
	uint64_t sequence;
	uint64_t expected_revision;
	uint64_t at_ms;
	/* Kernels are borrowed and sorted by (from_time_ms, to_time_ms). If no exact
	 * interval kernel is present, the reducer performs only same-phase
	 * kinematic aging and invents no reachability. */
	const sg_belief_horizon_kernel_t *kernels;
	size_t kernel_count;
	const sg_belief_evidence_t *evidence;
	size_t evidence_count;
	/* Both caller-owned scratch spans are reducer-local, non-overlapping, and
	 * may be overwritten. State and state.particles remain unchanged on every
	 * non-APPLIED result. */
	sg_belief_particle_t *scratch_first;
	sg_belief_particle_t *scratch_second;
	size_t scratch_capacity;
	/* Optional caller-owned replacement for the committed distribution. It must
	 * not overlap state or scratch storage. Null retains current state storage. */
	sg_belief_particle_t *commit_storage;
	size_t commit_capacity;
} sg_belief_frame_t;

typedef enum sg_belief_reduce_result_e
{
	SG_BELIEF_REDUCE_APPLIED = 0,
	SG_BELIEF_REDUCE_DUPLICATE,
	SG_BELIEF_REDUCE_REJECTED_INVALID,
	SG_BELIEF_REDUCE_REJECTED_STALE,
	SG_BELIEF_REDUCE_REJECTED_AUTHORITY,
	SG_BELIEF_REDUCE_CAPACITY
} sg_belief_reduce_result_t;

typedef struct sg_belief_reduction_s
{
	sg_belief_reduce_result_t result;
	uint64_t committed_revision;
	size_t particle_count;
	size_t required_particle_capacity;
	size_t required_scratch_capacity;
	size_t validated_phase_spans;
	size_t validated_horizon_entries;
	size_t evaluated_outcomes;
	float confidence;
} sg_belief_reduction_t;

/* Prediction output storage belongs to the caller and never aliases state. */
typedef struct sg_belief_prediction_s
{
	uint64_t at_time_ms;
	size_t particle_count;
	size_t particle_capacity;
	size_t required_particle_capacity;
	float confidence;
	float total_weight;
	sg_belief_particle_t *particles;
} sg_belief_prediction_t;

static inline int SG_BeliefTeamValid(uint8_t team)
{
	return team == 1U || team == 2U;
}

static inline int SG_BeliefFloatValid(float value)
{
	return isfinite(value) != 0;
}

static inline int SG_BeliefParticleValid(const sg_belief_particle_t *particle)
{
	uint8_t axis;

	if (!particle || particle->phase.phase_id == SG_DESTINATION_FIELD_NO_PHASE ||
	    particle->phase.cell_id == SG_DESTINATION_FIELD_NO_CELL ||
	    particle->movement_state < SG_BELIEF_MOTION_UNKNOWN ||
	    particle->movement_state >= SG_BELIEF_MOTION_COUNT ||
	    particle->reserved != 0U || particle->reserved2 != 0U ||
	    particle->future_time_ms == 0U || particle->weight <= 0.0f ||
	    particle->weight > 1.0f || !SG_BeliefFloatValid(particle->weight) ||
	    particle->spread_radius < 0.0f ||
	    !SG_BeliefFloatValid(particle->spread_radius))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_BeliefFloatValid(particle->position[axis]) ||
		    !SG_BeliefFloatValid(particle->velocity[axis]) ||
		    !SG_BeliefFloatValid(particle->acceleration[axis]) ||
		    !SG_BeliefFloatValid(particle->orientation[axis]))
			return 0;
	return 1;
}

/* Shape validation for downstream consumers. Reducer admission additionally
 * binds every phase to the immutable runtime snapshot. */
static inline int SG_BeliefStateValid(const sg_belief_state_t *state)
{
	size_t index;
	double sum = 0.0;

	if (!state || !SG_BeliefTeamValid(state->audience_team) ||
	    !SG_BeliefTeamValid(state->target_team) ||
	    state->target_client >= SG_BELIEF_MAX_CLIENTS ||
	    state->particle_capacity == 0U ||
	    state->particle_count > state->particle_capacity || !state->particles ||
	    state->generation == 0U || state->revision == 0U ||
	    state->rune_identity == 0U || state->topology_revision == 0U ||
	    state->updated_at_ms == 0U || state->confidence < 0.0f ||
	    state->confidence > 1.0f || !SG_BeliefFloatValid(state->confidence) ||
	    state->total_weight < 0.0f || state->total_weight > 1.0f ||
	    !SG_BeliefFloatValid(state->total_weight) ||
	    state->policy.confidence_decay_ms == 0U ||
	    state->policy.diffusion_fraction < 0.0f ||
	    state->policy.diffusion_fraction > 1.0f ||
	    !SG_BeliefFloatValid(state->policy.diffusion_fraction) ||
	    state->policy.spread_growth_per_ms < 0.0f ||
	    !SG_BeliefFloatValid(state->policy.spread_growth_per_ms))
		return 0;
	if ((state->last_evidence_sequence == 0U &&
	     (state->latest_provenance.evidence_id != 0U ||
	      state->latest_observed_at_ms != 0U ||
	      state->latest_valid_until_ms != 0U ||
	      state->latest_evidence_confidence != 0.0f)) ||
	    (state->last_evidence_sequence != 0U &&
	     (state->latest_provenance.authenticated != 1U ||
	      state->latest_provenance.evidence_sequence !=
		state->last_evidence_sequence ||
	      state->latest_provenance.audience_team != state->audience_team ||
	      state->latest_source < SG_BELIEF_SOURCE_SIGHT ||
	      state->latest_source >= SG_BELIEF_SOURCE_COUNT ||
	      state->latest_observed_at_ms == 0U ||
	      state->latest_observed_at_ms >
		state->latest_provenance.authenticated_at_ms ||
	      state->latest_provenance.authenticated_at_ms >
		state->updated_at_ms ||
	      state->latest_valid_until_ms < state->latest_observed_at_ms ||
	      state->latest_evidence_confidence <= 0.0f ||
	      state->latest_evidence_confidence > 1.0f ||
	      !SG_BeliefFloatValid(state->latest_evidence_confidence))))
		return 0;
	for (index = 0U; index < state->particle_count; index++)
	{
		if (!SG_BeliefParticleValid(&state->particles[index]) ||
		    state->particles[index].future_time_ms != state->updated_at_ms)
			return 0;
		sum += (double)state->particles[index].weight;
	}
	if (state->particle_count == 0U)
		return state->total_weight == 0.0f && state->confidence == 0.0f;
	return fabs(sum - 1.0) <= (double)SG_BELIEF_WEIGHT_EPSILON &&
		fabsf(state->total_weight - 1.0f) <= SG_BELIEF_WEIGHT_EPSILON &&
		state->confidence > 0.0f;
}

int SG_BeliefStateInit(const sg_rune_runtime_snapshot_t *snapshot,
	sg_belief_state_t *state, const sg_belief_state_config_t *config,
	sg_belief_particle_t *storage, size_t capacity);
sg_belief_reduce_result_t SG_BeliefReduce(
	const sg_rune_runtime_snapshot_t *snapshot, sg_belief_state_t *state,
	const sg_belief_frame_t *frame, sg_belief_reduction_t *out);
int SG_BeliefPredict(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state, uint64_t at_time_ms,
	sg_belief_particle_t *storage, size_t capacity,
	sg_belief_prediction_t *out);

#endif /* SG_BELIEF_CONTRACT_H */
