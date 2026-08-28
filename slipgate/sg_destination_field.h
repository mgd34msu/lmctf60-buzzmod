/* Runtime destination and phase-space field contracts for the RUNE v2 boundary. */
#ifndef SG_DESTINATION_FIELD_H
#define SG_DESTINATION_FIELD_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_rune_model.h"

#define SG_RUNTIME_CONTRACT_VERSION UINT16_C(3)
#define SG_DESTINATION_FIELD_INF UINT32_MAX
#define SG_DESTINATION_FIELD_NO_CELL UINT32_MAX
#define SG_DESTINATION_FIELD_NO_PHASE UINT32_MAX
#define SG_DESTINATION_FIELD_MAX_REGION_LEVEL 8U

typedef enum sg_destination_kind_e
{
	SG_DESTINATION_FLAG = 0,
	SG_DESTINATION_ITEM,
	SG_DESTINATION_WEAPON,
	SG_DESTINATION_ARMOR,
	SG_DESTINATION_POWERUP,
	SG_DESTINATION_CARRIER,
	SG_DESTINATION_ESCORT,
	SG_DESTINATION_INTERCEPT,
	SG_DESTINATION_DEFENSIVE_POST,
	SG_DESTINATION_LEARNED_POINT,
	SG_DESTINATION_WAYPOINT,
	SG_DESTINATION_KIND_COUNT
} sg_destination_kind_t;

typedef enum sg_destination_motion_e
{
	SG_DESTINATION_STATIC = 0,
	SG_DESTINATION_MOVING = 1
} sg_destination_motion_t;

/* A phase is a configuration-space state, not merely a containing cell. */
typedef struct sg_phase_coordinate_s
{
	uint32_t phase_id;
	uint32_t cell_id;
} sg_phase_coordinate_t;

/* RUNE capability families are not tactical action capabilities. Keeping the
 * bits in a distinct type prevents direct assignment to a tactical mask. */
typedef struct sg_field_capability_family_mask_s
{
	uint32_t bits;
} sg_field_capability_family_mask_t;

#define SG_FIELD_CAPABILITY_FAMILY_BIT(family) \
	((sg_field_capability_family_mask_t){ UINT32_C(1) << (family) })
#define SG_FIELD_CAPABILITY_FAMILY_MASK \
	((UINT32_C(1) << SG_RUNE_CAPABILITY_FAMILY_COUNT) - UINT32_C(1))

typedef struct sg_destination_pose_s
{
	sg_phase_coordinate_t phase;
	float position[3];
	float velocity[3];
	uint64_t sample_time_ms;
	uint32_t region_id;
} sg_destination_pose_t;

typedef struct sg_destination_handle_s
{
	uint64_t id;
	uint64_t generation;
	sg_destination_kind_t kind;
	sg_destination_motion_t motion;
	uint8_t valid;
	uint8_t reserved[3];
	sg_destination_pose_t pose;
} sg_destination_handle_t;

typedef struct sg_rune_runtime_snapshot_s
{
	/* identity is a process-local handle. Exact content authority remains in
	 * the immutable model and its loader-owned artifact binding. */
	uint64_t identity;
	uint64_t topology_revision;
	uint32_t cell_count;
	uint32_t phase_count;
	uint32_t region_count;
	const sg_rune_model_t *model;
	const sg_phase_coordinate_t *phases;
} sg_rune_runtime_snapshot_t;

typedef struct sg_field_sample_s
{
	sg_phase_coordinate_t phase;
	sg_phase_coordinate_t next_phase;
	/* cost_ms is the total static RUNE-edge cost from phase, through
	 * next_phase, to the destination phase. It excludes the host-validated
	 * residual from that phase to the exact pose. */
	uint32_t cost_ms;
	sg_field_capability_family_mask_t capability_families;
	sg_rune_phase_transition_kind_t phase_transition_kind;
	float direction[3];
	float velocity_direction[3];
	uint8_t finite;
	uint8_t reserved[3];
} sg_field_sample_t;

typedef enum sg_field_terminal_residual_status_e
{
	SG_FIELD_TERMINAL_RESIDUAL_NOT_APPLICABLE = 0,
	SG_FIELD_TERMINAL_RESIDUAL_EXACT,
	SG_FIELD_TERMINAL_RESIDUAL_UNKNOWN,
	SG_FIELD_TERMINAL_RESIDUAL_STATUS_COUNT
} sg_field_terminal_residual_status_t;

typedef struct sg_field_terminal_residual_s
{
	sg_field_terminal_residual_status_t status;
	uint32_t upper_ms;
} sg_field_terminal_residual_t;

typedef struct sg_field_query_result_s
{
	sg_field_sample_t sample;
	sg_field_terminal_residual_t terminal_residual;
} sg_field_query_result_t;

typedef struct sg_destination_field_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t generation;
	uint64_t computed_at_ms;
	sg_destination_handle_t destination;
	const sg_field_sample_t *samples;
	uint32_t sample_count;
	uint8_t complete;
	uint8_t reserved[3];
} sg_destination_field_t;

typedef struct sg_field_update_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	const uint32_t *affected_regions;
	uint32_t affected_region_count;
	uint8_t region_level;
	uint8_t incremental;
	uint8_t reserved[2];
} sg_field_update_t;

static inline int SG_DestinationKindValid(sg_destination_kind_t kind)
{
	return kind >= SG_DESTINATION_FLAG && kind < SG_DESTINATION_KIND_COUNT;
}

static inline int SG_DestinationMotionValid(sg_destination_motion_t motion)
{
	return motion == SG_DESTINATION_STATIC || motion == SG_DESTINATION_MOVING;
}

static inline int SG_DestinationFloatValid(float value)
{
	return isfinite(value) != 0;
}

static inline int SG_PhaseCoordinateValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase)
{
	return snapshot && phase && snapshot->phases &&
	       phase->phase_id < snapshot->phase_count &&
	       phase->cell_id < snapshot->cell_count &&
	       snapshot->phases[phase->phase_id].phase_id == phase->phase_id &&
	       snapshot->phases[phase->phase_id].cell_id == phase->cell_id;
}

static inline int SG_DestinationPoseValid(const sg_destination_pose_t *pose)
{
	uint32_t axis;

	if (!pose || pose->phase.phase_id == SG_DESTINATION_FIELD_NO_PHASE ||
	    pose->phase.cell_id == SG_DESTINATION_FIELD_NO_CELL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_DestinationFloatValid(pose->position[axis]) ||
		    !SG_DestinationFloatValid(pose->velocity[axis]))
			return 0;
	return 1;
}

static inline int SG_DestinationHandleValid(
	const sg_destination_handle_t *handle)
{
	return handle && handle->valid == 1U && handle->id != 0U &&
	       handle->generation != 0U && SG_DestinationKindValid(handle->kind) &&
	       SG_DestinationMotionValid(handle->motion) &&
	       SG_DestinationPoseValid(&handle->pose) &&
	       ((handle->motion == SG_DESTINATION_STATIC &&
	         handle->pose.sample_time_ms == 0U) ||
	        (handle->motion == SG_DESTINATION_MOVING &&
	         handle->pose.sample_time_ms != 0U));
}

static inline int SG_DestinationSameTarget(
	const sg_destination_handle_t *left,
	const sg_destination_handle_t *right)
{
	return SG_DestinationHandleValid(left) && SG_DestinationHandleValid(right) &&
	       left->id == right->id && left->kind == right->kind;
}

static inline int SG_RuneRuntimeSnapshotValid(
	const sg_rune_runtime_snapshot_t *snapshot)
{
	uint32_t index;

	if (!snapshot || snapshot->identity == 0U || !snapshot->model ||
	    snapshot->topology_revision == 0U || snapshot->cell_count == 0U ||
	    snapshot->phase_count == 0U || !snapshot->phases ||
	    snapshot->model->version != SG_RUNE_MODEL_VERSION ||
	    snapshot->model->schema_tag != SG_RUNE_MODEL_SCHEMA_TAG ||
	    (snapshot->model->flags & (SG_RUNE_MODEL_IMMUTABLE |
	      SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS)) !=
	     (SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
	      SG_RUNE_MODEL_NO_RUNTIME_ACTORS) ||
	    snapshot->model->completeness.state != SG_RUNE_COMPLETENESS_COMPLETE ||
	    snapshot->model->cell_count != snapshot->cell_count ||
	    snapshot->model->phase_count != snapshot->phase_count ||
	    !snapshot->model->cells || !snapshot->model->phases)
		return 0;
	for (index = 0U; index < snapshot->phase_count; index++)
		if (snapshot->phases[index].phase_id != index ||
		    snapshot->phases[index].cell_id >= snapshot->cell_count)
			return 0;
	return 1;
}

static inline int SG_FieldSampleShapeValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_sample_t *sample)
{
	uint32_t axis;

	if (!snapshot || !sample ||
	    !SG_PhaseCoordinateValid(snapshot, &sample->phase) ||
	    (sample->capability_families.bits &
	     ~SG_FIELD_CAPABILITY_FAMILY_MASK) != 0U ||
	    sample->phase_transition_kind < SG_RUNE_PHASE_TRANSITION_NONE ||
	    sample->phase_transition_kind >= SG_RUNE_PHASE_TRANSITION_KIND_COUNT ||
	    (sample->finite != 0U && sample->finite != 1U) ||
	    (sample->finite == 1U &&
	     (!SG_PhaseCoordinateValid(snapshot, &sample->next_phase) ||
	      sample->cost_ms >= SG_DESTINATION_FIELD_INF ||
	      (sample->cost_ms == 0U &&
	       (sample->capability_families.bits != 0U ||
	        sample->phase_transition_kind != SG_RUNE_PHASE_TRANSITION_NONE)) ||
	      (sample->cost_ms != 0U &&
	       sample->capability_families.bits == 0U &&
	       sample->phase_transition_kind == SG_RUNE_PHASE_TRANSITION_NONE))) ||
	    (sample->finite == 0U &&
	     (sample->next_phase.phase_id != SG_DESTINATION_FIELD_NO_PHASE ||
	      sample->next_phase.cell_id != SG_DESTINATION_FIELD_NO_CELL ||
	      sample->cost_ms != SG_DESTINATION_FIELD_INF ||
	      sample->capability_families.bits != 0U ||
	      sample->phase_transition_kind != SG_RUNE_PHASE_TRANSITION_NONE)))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_DestinationFloatValid(sample->direction[axis]) ||
		    !SG_DestinationFloatValid(sample->velocity_direction[axis]) ||
		    (sample->finite == 0U &&
		     (sample->direction[axis] != 0.0f ||
		      sample->velocity_direction[axis] != 0.0f)))
			return 0;
	return 1;
}

static inline int SG_FieldSampleValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_sample_t *sample)
{
	return SG_RuneRuntimeSnapshotValid(snapshot) &&
	       SG_FieldSampleShapeValid(snapshot, sample);
}

static inline int SG_FieldQueryResultValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_query_result_t *result)
{
	if (!result || !SG_FieldSampleValid(snapshot, &result->sample) ||
	    result->terminal_residual.status <
		SG_FIELD_TERMINAL_RESIDUAL_NOT_APPLICABLE ||
	    result->terminal_residual.status >=
		SG_FIELD_TERMINAL_RESIDUAL_STATUS_COUNT)
		return 0;
	if (result->sample.finite == 0U)
		return result->terminal_residual.status ==
			SG_FIELD_TERMINAL_RESIDUAL_NOT_APPLICABLE &&
			result->terminal_residual.upper_ms == SG_DESTINATION_FIELD_INF;
	if (result->terminal_residual.status ==
		SG_FIELD_TERMINAL_RESIDUAL_EXACT)
		return result->terminal_residual.upper_ms < SG_DESTINATION_FIELD_INF;
	return result->terminal_residual.status ==
		SG_FIELD_TERMINAL_RESIDUAL_UNKNOWN &&
		result->terminal_residual.upper_ms == SG_DESTINATION_FIELD_INF;
}

static inline int SG_DestinationFieldValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field)
{
	uint32_t index;

	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !field ||
	    !SG_DestinationHandleValid(&field->destination) ||
	    !SG_PhaseCoordinateValid(snapshot, &field->destination.pose.phase) ||
	    field->rune_identity != snapshot->identity ||
	    field->topology_revision != snapshot->topology_revision ||
	    field->generation != field->destination.generation ||
	    field->computed_at_ms == 0U ||
	    (field->destination.motion == SG_DESTINATION_MOVING &&
	     field->computed_at_ms < field->destination.pose.sample_time_ms) ||
	    field->sample_count > snapshot->phase_count ||
	    (field->complete == 1U && field->sample_count != snapshot->phase_count) ||
	    (field->sample_count != 0U && !field->samples) ||
	    (field->complete != 0U && field->complete != 1U))
		return 0;
	for (index = 0U; index < field->sample_count; index++)
		if (!SG_FieldSampleShapeValid(snapshot, &field->samples[index]) ||
		    field->samples[index].phase.phase_id !=
			snapshot->phases[index].phase_id ||
		    field->samples[index].phase.cell_id !=
			snapshot->phases[index].cell_id)
			return 0;
	return 1;
}

static inline int SG_FieldUpdateValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_update_t *update)
{
	uint32_t index;

	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !update ||
	    update->rune_identity != snapshot->identity ||
	    update->topology_revision != snapshot->topology_revision ||
	    !SG_DestinationHandleValid(&update->before) ||
	    !SG_DestinationHandleValid(&update->after) ||
	    !SG_PhaseCoordinateValid(snapshot, &update->before.pose.phase) ||
	    !SG_PhaseCoordinateValid(snapshot, &update->after.pose.phase) ||
	    !SG_DestinationSameTarget(&update->before, &update->after) ||
	    update->after.generation <= update->before.generation ||
	    update->region_level > SG_DESTINATION_FIELD_MAX_REGION_LEVEL ||
	    (update->affected_region_count != 0U && !update->affected_regions) ||
	    (update->incremental == 1U && update->affected_region_count == 0U) ||
	    (update->incremental != 0U && update->incremental != 1U))
		return 0;
	for (index = 0U; index < update->affected_region_count; index++)
		if (snapshot->region_count != 0U &&
		    update->affected_regions[index] >= snapshot->region_count)
			return 0;
	return 1;
}

/* Query uses the exact live source pose. Inside the destination phase it
 * resolves terminal directions against destination.pose. A nontrivial
 * residual stays UNKNOWN until host movement supplies a conservative bound. */
int SG_FieldQuery(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field,
	const sg_destination_pose_t *source, sg_field_query_result_t *out);
int SG_FieldNeedsUpdate(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field,
	const sg_destination_handle_t *destination);
int SG_FieldCanReuseStatic(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field,
	const sg_destination_handle_t *destination);
/* Inputs are borrowed and immutable. The caller owns samples and keeps them
 * alive with out. Temporary solver storage never escapes this call. On
 * failure, out is zeroed and the caller must ignore samples. */
int SG_DestinationFieldSolve(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, uint64_t computed_at_ms,
	sg_field_sample_t *samples, uint32_t sample_capacity,
	sg_destination_field_t *out);

#endif /* SG_DESTINATION_FIELD_H */
