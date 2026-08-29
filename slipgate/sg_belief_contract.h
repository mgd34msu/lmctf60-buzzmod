#ifndef SG_BELIEF_CONTRACT_H
#define SG_BELIEF_CONTRACT_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_destination_field.h"

#define SG_BELIEF_MAX_CLIENTS 256U
#define SG_BELIEF_WEIGHT_EPSILON 0.000001f
#define SG_BELIEF_ORIENTATION_LIMIT_DEGREES 360.0f

/* The host owns spawn_generation: it is minted once after a completed client
 * spawn and never reused. client_id scopes that generation to one client
 * slot, so an old observation cannot name a later occupant of that slot. */
typedef struct sg_belief_life_identity_s
{
	uint32_t client_id;
	uint32_t reserved;
	uint64_t spawn_generation;
} sg_belief_life_identity_t;

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
	uint8_t reserved[6];
	sg_belief_life_identity_t issuer_life;
	uint64_t evidence_id;
	uint64_t evidence_sequence;
	uint64_t authenticated_at_ms;
	uint64_t rune_identity;
	uint64_t topology_revision;
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
	uint8_t reserved[7];
	sg_belief_life_identity_t target_life;
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
	uint8_t reserved[6];
	sg_belief_life_identity_t target_life;
	uint64_t initialized_at_ms;
	sg_belief_policy_t policy;
} sg_belief_state_config_t;

/* particles is caller-owned mutable storage. It must remain alive and exclusive
 * to this track until the track is retired. Frame inputs never escape a call. */
typedef struct sg_belief_state_s
{
	uint8_t audience_team;
	uint8_t target_team;
	uint8_t reserved[6];
	sg_belief_life_identity_t target_life;
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

typedef enum sg_belief_horizon_step_kind_e
{
	SG_BELIEF_HORIZON_PHASE_TRANSITION = 0,
	SG_BELIEF_HORIZON_CAPABILITY_KERNEL,
	SG_BELIEF_HORIZON_STEP_KIND_COUNT
} sg_belief_horizon_step_kind_t;

/* One topology witness step through an accepted immutable RUNE record. */
typedef struct sg_belief_horizon_step_s
{
	sg_phase_coordinate_t from;
	sg_phase_coordinate_t to;
	sg_belief_horizon_step_kind_t kind;
	uint32_t record_index;
} sg_belief_horizon_step_t;

/* One weighted outcome in a host-certified complete horizon kernel. The step
 * span proves every summarized movement against accepted RUNE structure. */
typedef struct sg_belief_horizon_entry_s
{
	sg_phase_coordinate_t from;
	sg_phase_coordinate_t to;
	float displacement[3];
	float likelihood;
	size_t first_step;
	size_t step_count;
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
	const sg_belief_horizon_step_t *steps;
	size_t step_count;
} sg_belief_horizon_kernel_t;

typedef struct sg_belief_frame_s
{
	uint64_t sequence;
	uint64_t expected_revision;
	uint64_t expected_generation;
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
	SG_BELIEF_REDUCE_CAPACITY,
	SG_BELIEF_REDUCE_OVERFLOW
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
	size_t validated_horizon_steps;
	size_t evaluated_outcomes;
	float confidence;
} sg_belief_reduction_t;

typedef enum sg_belief_predict_result_e
{
	SG_BELIEF_PREDICT_APPLIED = 0,
	SG_BELIEF_PREDICT_REJECTED_INVALID,
	SG_BELIEF_PREDICT_CAPACITY,
	SG_BELIEF_PREDICT_OVERFLOW
} sg_belief_predict_result_t;

typedef struct sg_belief_prediction_request_s
{
	uint64_t at_time_ms;
	/* A nonempty chain exactly tiles [state.updated_at_ms, at_time_ms]. An
	 * empty chain requests same-phase kinematic aging only. */
	const sg_belief_horizon_kernel_t *kernels;
	size_t kernel_count;
	/* Scratch is caller-owned, disposable, and never aliases authority,
	 * state, destination storage, or the other scratch span. */
	sg_belief_particle_t *scratch_first;
	sg_belief_particle_t *scratch_second;
	size_t scratch_capacity;
	sg_belief_particle_t *particles;
	size_t particle_capacity;
} sg_belief_prediction_request_t;

typedef struct sg_belief_prediction_subject_s
{
	uint8_t audience_team;
	uint8_t target_team;
} sg_belief_prediction_subject_t;

typedef struct sg_belief_prediction_source_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t state_generation;
	uint64_t state_revision;
	uint64_t state_time_ms;
} sg_belief_prediction_source_t;

/* Prediction particle storage belongs to the caller. It is written only when
 * prediction returns APPLIED. The result may report capacity needs, and
 * scratch may be overwritten on any result. */
typedef struct sg_belief_prediction_s
{
	uint64_t at_time_ms;
	sg_belief_life_identity_t target_life;
	sg_belief_prediction_subject_t subject;
	sg_belief_prediction_source_t source;
	size_t particle_count;
	size_t particle_capacity;
	size_t required_particle_capacity;
	size_t required_scratch_capacity;
	size_t validated_phase_spans;
	size_t validated_horizon_entries;
	size_t validated_horizon_steps;
	size_t evaluated_outcomes;
	float confidence;
	float total_weight;
	sg_belief_particle_t *particles;
} sg_belief_prediction_t;

static inline int SG_BeliefTeamValid(uint8_t team)
{
	return team == 1U || team == 2U;
}

static inline int SG_BeliefLifeIdentityValid(
	const sg_belief_life_identity_t *life)
{
	return life && life->client_id < SG_BELIEF_MAX_CLIENTS &&
		life->reserved == 0U && life->spawn_generation != 0U;
}

static inline int SG_BeliefLifeIdentityEqual(
	const sg_belief_life_identity_t *left,
	const sg_belief_life_identity_t *right)
{
	return SG_BeliefLifeIdentityValid(left) &&
		SG_BeliefLifeIdentityValid(right) &&
		left->client_id == right->client_id &&
		left->spawn_generation == right->spawn_generation;
}

static inline int SG_BeliefLifeIdentityEmpty(
	const sg_belief_life_identity_t *life)
{
	return life && life->client_id == 0U && life->reserved == 0U &&
		life->spawn_generation == 0U;
}

static inline int SG_BeliefReservedZero(const uint8_t *reserved, size_t count)
{
	size_t index;

	if (!reserved)
		return 0;
	for (index = 0U; index < count; index++)
		if (reserved[index] != 0U)
			return 0;
	return 1;
}

static inline int SG_BeliefFloatValid(float value)
{
	return isfinite(value) != 0;
}

static inline int SG_BeliefRangeBounds(const void *pointer, size_t count,
	size_t element_size, uintptr_t *begin, uintptr_t *end)
{
	size_t bytes;
	uintptr_t first;

	if (!pointer || !begin || !end || count == 0U || element_size == 0U ||
	    count > SIZE_MAX / element_size)
		return 0;
	bytes = count * element_size;
	first = (uintptr_t)pointer;
	if (bytes > UINTPTR_MAX - first)
		return 0;
	*begin = first;
	*end = first + bytes;
	return 1;
}

static inline int SG_BeliefRangeDisjointFromArray(uintptr_t mutable_begin,
	uintptr_t mutable_end, const void *array, size_t count,
	size_t element_size)
{
	uintptr_t array_begin;
	uintptr_t array_end;

	if (count == 0U)
		return 1;
	if (!SG_BeliefRangeBounds(array, count, element_size, &array_begin,
	    &array_end))
		return 0;
	return mutable_begin >= array_end || array_begin >= mutable_end;
}

static inline int SG_BeliefMutableRangeDisjointFromRune(
	const sg_rune_runtime_snapshot_t *snapshot, const void *storage,
	size_t byte_count)
{
	const sg_rune_model_t *model;
	uintptr_t mutable_begin;
	uintptr_t mutable_end;

	if (!snapshot || !snapshot->model || !storage || byte_count == 0U ||
	    !SG_BeliefRangeBounds(storage, byte_count, 1U, &mutable_begin,
		&mutable_end))
		return 0;
	model = snapshot->model;
#define SG_BELIEF_DISJOINT(array, count) \
	SG_BeliefRangeDisjointFromArray(mutable_begin, mutable_end, (array), \
		(size_t)(count), sizeof(*(array)))
	if (!SG_BELIEF_DISJOINT(snapshot, 1U) ||
	    !SG_BELIEF_DISJOINT(model, 1U) ||
	    !SG_BELIEF_DISJOINT(snapshot->phases, snapshot->phase_count) ||
	    !SG_BELIEF_DISJOINT(model->planes, model->plane_count) ||
	    !SG_BELIEF_DISJOINT(model->portal_vertices,
		model->portal_vertex_count) ||
	    !SG_BELIEF_DISJOINT(model->phases, model->phase_count) ||
	    !SG_BELIEF_DISJOINT(model->phase_transitions,
		model->phase_transition_count) ||
	    !SG_BELIEF_DISJOINT(model->cells, model->cell_count) ||
	    !SG_BELIEF_DISJOINT(model->portals, model->portal_count) ||
	    !SG_BELIEF_DISJOINT(model->surfaces, model->surface_count) ||
	    !SG_BELIEF_DISJOINT(model->affordances, model->affordance_count) ||
	    !SG_BELIEF_DISJOINT(model->kernels, model->kernel_count) ||
	    !SG_BELIEF_DISJOINT(model->landmarks, model->landmark_count) ||
	    !SG_BELIEF_DISJOINT(model->mechanisms, model->mechanism_count))
	{
#undef SG_BELIEF_DISJOINT
		return 0;
	}
#undef SG_BELIEF_DISJOINT
	return 1;
}

static inline int SG_BeliefIntervalContains(
	const sg_rune_interval_t *interval, float value)
{
	return interval && SG_BeliefFloatValid(interval->min_value) &&
		SG_BeliefFloatValid(interval->max_value) &&
		interval->min_value <= interval->max_value &&
		value >= interval->min_value && value <= interval->max_value;
}

static inline int SG_BeliefPositionInsidePhaseCell(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase, const float position[3])
{
	const sg_rune_model_t *model;
	const sg_rune_cell_t *cell;
	size_t plane_index;
	size_t plane_end;
	size_t axis;

	if (!snapshot || !snapshot->model || !position ||
	    !SG_PhaseCoordinateValid(snapshot, phase))
		return 0;
	model = snapshot->model;
	cell = &model->cells[phase->cell_id];
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_BeliefFloatValid(position[axis]) ||
		    !SG_BeliefFloatValid(cell->bounds.mins.value[axis]) ||
		    !SG_BeliefFloatValid(cell->bounds.maxs.value[axis]) ||
		    cell->bounds.mins.value[axis] >=
			cell->bounds.maxs.value[axis] ||
		    position[axis] < cell->bounds.mins.value[axis] ||
		    position[axis] > cell->bounds.maxs.value[axis])
			return 0;
	if (!model->planes || cell->boundary_planes.count < 4U ||
	    cell->boundary_planes.count > SG_RUNE_MODEL_MAX_CELL_PLANES ||
	    cell->boundary_planes.first > model->plane_count ||
	    cell->boundary_planes.count >
		model->plane_count - cell->boundary_planes.first)
		return 0;
	plane_index = cell->boundary_planes.first;
	plane_end = plane_index + cell->boundary_planes.count;
	for (; plane_index < plane_end; plane_index++)
	{
		const sg_rune_plane_t *plane = &model->planes[plane_index];
		double distance = 0.0;
		double normal_squared = 0.0;

		if (!SG_BeliefFloatValid(plane->distance))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
		{
			if (!SG_BeliefFloatValid(plane->normal.value[axis]))
				return 0;
			distance += (double)position[axis] *
				(double)plane->normal.value[axis];
			normal_squared += (double)plane->normal.value[axis] *
				(double)plane->normal.value[axis];
		}
		if (!isfinite(distance) || !isfinite(normal_squared) ||
		    normal_squared <= 0.0 || distance > (double)plane->distance)
			return 0;
	}
	return 1;
}

static inline float SG_BeliefAccelerationLimit(
	const sg_rune_physics_parameters_t *physics,
	const sg_rune_phase_basis_t *basis,
	sg_belief_motion_state_t movement_state)
{
	int mover_relative;
	float limit;

	switch (movement_state)
	{
	case SG_BELIEF_MOTION_GROUND:
		return physics->ground_acceleration;
	case SG_BELIEF_MOTION_AIR:
		return physics->air_acceleration;
	case SG_BELIEF_MOTION_WATER:
		return physics->water_acceleration;
	case SG_BELIEF_MOTION_HOOK:
		return physics->hook_acceleration;
	case SG_BELIEF_MOTION_MOVER:
		return physics->external_acceleration;
	case SG_BELIEF_MOTION_UNKNOWN:
		mover_relative = basis->support == SG_RUNE_SUPPORT_MOVER ||
			basis->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE;
		if (basis->motion == SG_RUNE_MOTION_SUPPORTED)
			return mover_relative ? physics->external_acceleration :
				physics->ground_acceleration;
		if (basis->motion == SG_RUNE_MOTION_AIRBORNE)
		{
			limit = physics->air_acceleration;
			if (physics->hook_acceleration > limit)
				limit = physics->hook_acceleration;
			return limit;
		}
		if (basis->motion == SG_RUNE_MOTION_SWIMMING)
			return physics->water_acceleration;
		break;
	case SG_BELIEF_MOTION_COUNT:
		break;
	}
	return -1.0f;
}

static inline int SG_BeliefHorizontalVectorWithinLimit(
	const float vector[3], float limit)
{
	double magnitude_squared;

	if (!vector || !SG_BeliefFloatValid(vector[0]) ||
	    !SG_BeliefFloatValid(vector[1]) || !SG_BeliefFloatValid(limit) ||
	    limit < 0.0f)
		return 0;
	magnitude_squared = (double)vector[0] * (double)vector[0] +
		(double)vector[1] * (double)vector[1];
	return isfinite(magnitude_squared) &&
		magnitude_squared <= (double)limit * (double)limit;
}

static inline int SG_BeliefAccelerationWithinLimits(const float vector[3],
	float horizontal_limit, float vertical_control_limit, float gravity)
{
	double vertical_limit;

	if (!vector || !SG_BeliefFloatValid(vector[2]) ||
	    !SG_BeliefFloatValid(vertical_control_limit) ||
	    vertical_control_limit < 0.0f || !SG_BeliefFloatValid(gravity) ||
	    gravity < 0.0f)
		return 0;
	vertical_limit = (double)vertical_control_limit + (double)gravity;
	return isfinite(vertical_limit) &&
		SG_BeliefHorizontalVectorWithinLimit(vector, horizontal_limit) &&
		fabs((double)vector[2]) <= vertical_limit;
}

static inline int SG_BeliefMotionStateCompatible(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase,
	sg_belief_motion_state_t movement_state);

static inline int SG_BeliefKinematicsCompatible(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase,
	sg_belief_motion_state_t movement_state, const float velocity[3],
	const float acceleration[3], const float orientation[3])
{
	const sg_rune_phase_basis_t *basis;
	const sg_rune_physics_parameters_t *physics;
	const sg_rune_interval_t *velocity_axes[3];
	float acceleration_limit;
	double speed_squared = 0.0;
	size_t axis;

	if (!SG_BeliefMotionStateCompatible(snapshot, phase, movement_state) ||
	    !velocity || !acceleration || !orientation)
		return 0;
	basis = &snapshot->model->phases[phase->phase_id];
	physics = &snapshot->model->identity.physics;
	if (!SG_BeliefFloatValid(physics->gravity) || physics->gravity < 0.0f ||
	    !SG_BeliefFloatValid(physics->ground_acceleration) ||
	    physics->ground_acceleration < 0.0f ||
	    !SG_BeliefFloatValid(physics->air_acceleration) ||
	    physics->air_acceleration < 0.0f ||
	    !SG_BeliefFloatValid(physics->water_acceleration) ||
	    physics->water_acceleration < 0.0f ||
	    !SG_BeliefFloatValid(physics->hook_acceleration) ||
	    physics->hook_acceleration < 0.0f ||
	    !SG_BeliefFloatValid(physics->external_acceleration) ||
	    physics->external_acceleration < 0.0f ||
	    !SG_BeliefFloatValid(physics->max_velocity) ||
	    physics->max_velocity <= 0.0f)
		return 0;
	acceleration_limit = SG_BeliefAccelerationLimit(physics, basis,
		movement_state);
	if (!SG_BeliefFloatValid(acceleration_limit) || acceleration_limit < 0.0f)
		return 0;
	velocity_axes[0] = &basis->velocity.x;
	velocity_axes[1] = &basis->velocity.y;
	velocity_axes[2] = &basis->velocity.z;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!SG_BeliefFloatValid(velocity[axis]) ||
		    !SG_BeliefFloatValid(acceleration[axis]) ||
		    !SG_BeliefFloatValid(orientation[axis]) ||
		    !SG_BeliefIntervalContains(velocity_axes[axis], velocity[axis]) ||
		    fabsf(orientation[axis]) >
			SG_BELIEF_ORIENTATION_LIMIT_DEGREES)
			return 0;
		speed_squared += (double)velocity[axis] * (double)velocity[axis];
	}
	return isfinite(speed_squared) &&
		SG_BeliefAccelerationWithinLimits(acceleration, acceleration_limit,
			acceleration_limit, physics->gravity) &&
		speed_squared <= (double)physics->max_velocity *
			(double)physics->max_velocity;
}

static inline int SG_BeliefMotionStateCompatible(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase,
	sg_belief_motion_state_t movement_state)
{
	const sg_rune_phase_basis_t *basis;
	int mover_relative;

	if (!snapshot || !snapshot->model || !snapshot->model->phases ||
	    !SG_PhaseCoordinateValid(snapshot, phase) ||
	    movement_state < SG_BELIEF_MOTION_UNKNOWN ||
	    movement_state >= SG_BELIEF_MOTION_COUNT)
		return 0;
	basis = &snapshot->model->phases[phase->phase_id];
	if (basis->motion < SG_RUNE_MOTION_SUPPORTED ||
	    basis->motion >= SG_RUNE_MOTION_COUNT ||
	    basis->support < SG_RUNE_SUPPORT_NONE ||
	    basis->support >= SG_RUNE_SUPPORT_COUNT ||
	    basis->reference_frame < SG_RUNE_FRAME_WORLD ||
	    basis->reference_frame >= SG_RUNE_FRAME_COUNT)
		return 0;
	if (movement_state == SG_BELIEF_MOTION_UNKNOWN)
		return 1;
	mover_relative = basis->support == SG_RUNE_SUPPORT_MOVER ||
		basis->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE;
	switch (movement_state)
	{
	case SG_BELIEF_MOTION_GROUND:
		return basis->motion == SG_RUNE_MOTION_SUPPORTED && !mover_relative;
	case SG_BELIEF_MOTION_AIR:
	case SG_BELIEF_MOTION_HOOK:
		return basis->motion == SG_RUNE_MOTION_AIRBORNE;
	case SG_BELIEF_MOTION_WATER:
		return basis->motion == SG_RUNE_MOTION_SWIMMING;
	case SG_BELIEF_MOTION_MOVER:
		return basis->motion == SG_RUNE_MOTION_SUPPORTED && mover_relative;
	case SG_BELIEF_MOTION_UNKNOWN:
	case SG_BELIEF_MOTION_COUNT:
		break;
	}
	return 0;
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
	    !SG_BeliefReservedZero(state->reserved, sizeof(state->reserved)) ||
	    !SG_BeliefLifeIdentityValid(&state->target_life) ||
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
	      !SG_BeliefReservedZero(state->latest_provenance.reserved,
		sizeof(state->latest_provenance.reserved)) ||
	      !SG_BeliefLifeIdentityEmpty(
		&state->latest_provenance.issuer_life) ||
	      state->latest_provenance.rune_identity != 0U ||
	      state->latest_provenance.topology_revision != 0U ||
	      state->latest_observed_at_ms != 0U ||
	      state->latest_valid_until_ms != 0U ||
	      state->latest_evidence_confidence != 0.0f)) ||
	    (state->last_evidence_sequence != 0U &&
	     (state->latest_provenance.authenticated != 1U ||
	      !SG_BeliefReservedZero(state->latest_provenance.reserved,
		sizeof(state->latest_provenance.reserved)) ||
	      !SG_BeliefLifeIdentityValid(
		&state->latest_provenance.issuer_life) ||
	      state->latest_provenance.evidence_sequence !=
		state->last_evidence_sequence ||
	      state->latest_provenance.audience_team != state->audience_team ||
	      state->latest_provenance.rune_identity != state->rune_identity ||
	      state->latest_provenance.topology_revision !=
		state->topology_revision ||
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
sg_belief_predict_result_t SG_BeliefPredict(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_belief_state_t *state,
	const sg_belief_prediction_request_t *request,
	sg_belief_prediction_t *out);

#endif /* SG_BELIEF_CONTRACT_H */
