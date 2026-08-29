#ifndef SG_DESTINATION_H
#define SG_DESTINATION_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_rune_model.h"

#define SG_RUNTIME_CONTRACT_VERSION UINT16_C(6)
#define SG_DESTINATION_COST_INFINITE UINT32_MAX
#define SG_DESTINATION_NO_CELL UINT32_MAX
#define SG_DESTINATION_NO_PHASE UINT32_MAX
#define SG_DESTINATION_NO_REGION UINT32_MAX

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

typedef enum sg_destination_flag_location_e
{
	SG_DESTINATION_FLAG_HOME = 0,
	SG_DESTINATION_FLAG_CURRENT,
	SG_DESTINATION_FLAG_LOCATION_COUNT
} sg_destination_flag_location_t;

typedef enum sg_destination_carrier_selector_e
{
	SG_DESTINATION_CARRIER_ANY = 0,
	SG_DESTINATION_CARRIER_EXACT,
	SG_DESTINATION_CARRIER_SELECTOR_COUNT
} sg_destination_carrier_selector_t;

typedef struct sg_destination_flag_ref_s
{
	uint8_t team;
	uint8_t location;
	uint16_t reserved;
} sg_destination_flag_ref_t;

typedef struct sg_destination_item_ref_s
{
	uint64_t item_id;
} sg_destination_item_ref_t;

typedef struct sg_destination_carrier_ref_s
{
	uint16_t client_id;
	uint8_t team;
	uint8_t selector;
} sg_destination_carrier_ref_t;

typedef struct sg_destination_post_ref_s
{
	uint32_t region_id;
} sg_destination_post_ref_t;

typedef struct sg_destination_point_ref_s
{
	uint64_t point_id;
} sg_destination_point_ref_t;

typedef struct sg_destination_ref_s
{
	sg_destination_kind_t kind;
	union
	{
		sg_destination_flag_ref_t flag;
		sg_destination_item_ref_t item;
		sg_destination_carrier_ref_t carrier;
		sg_destination_post_ref_t post;
		sg_destination_point_ref_t point;
	} value;
} sg_destination_ref_t;

typedef struct sg_phase_coordinate_s
{
	uint32_t phase_id;
	uint32_t cell_id;
} sg_phase_coordinate_t;

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
	uint64_t identity;
	uint64_t topology_revision;
	uint32_t cell_count;
	uint32_t phase_count;
	uint32_t region_count;
	const sg_rune_model_t *model;
	const sg_phase_coordinate_t *phases;
} sg_rune_runtime_snapshot_t;

typedef struct sg_rune_state_chart_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_state_chart_id_t;
typedef sg_rune_state_chart_id_t sg_rune_state_chart_ref_t;

typedef struct sg_rune_state_domain_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_state_domain_id_t;
typedef sg_rune_state_domain_id_t sg_rune_state_domain_ref_t;

typedef struct sg_destination_terminal_domain_s
{
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_domain_ref_t domain;
} sg_destination_terminal_domain_t;

typedef struct sg_destination_interval_s
{
	float min_value;
	float max_value;
} sg_destination_interval_t;

typedef struct sg_destination_interval3_s
{
	sg_destination_interval_t x;
	sg_destination_interval_t y;
	sg_destination_interval_t z;
} sg_destination_interval3_t;

typedef struct sg_destination_terminal_anchor_s
{
	/* Exact owner/content binding remains part of the key even when capture
	 * sets overlap. Coordinates are canonical binary32 state coordinates. */
	uint64_t owner_identity;
	sg_destination_ref_t destination;
	uint64_t destination_generation;
	float position[3];
	float velocity[3];
	float local_elapsed_ms;
} sg_destination_terminal_anchor_t;

typedef struct sg_destination_terminal_capture_s
{
	sg_destination_terminal_anchor_t anchor;
	sg_destination_interval3_t position_offset;
	sg_destination_interval3_t velocity;
	sg_destination_interval_t local_elapsed_ms;
} sg_destination_terminal_capture_t;

typedef struct sg_destination_static_patch_s
{
	sg_destination_terminal_domain_t domain;
	sg_destination_terminal_capture_t capture;
} sg_destination_static_patch_t;

typedef struct sg_destination_tube_segment_s
{
	uint64_t valid_from_ms;
	uint64_t valid_until_ms;
	sg_destination_terminal_domain_t domain;
	sg_destination_terminal_capture_t capture;
} sg_destination_tube_segment_t;

typedef struct sg_destination_moving_tube_s
{
	uint64_t trajectory_identity;
	const sg_destination_tube_segment_t *segments;
	size_t segment_count;
} sg_destination_moving_tube_t;

typedef enum sg_destination_terminal_kind_e
{
	SG_DESTINATION_TERMINAL_STATIC_PATCH = 0,
	SG_DESTINATION_TERMINAL_MOVING_TUBE,
	SG_DESTINATION_TERMINAL_KIND_COUNT
} sg_destination_terminal_kind_t;

typedef struct sg_destination_terminal_s
{
	/* Identity of the authenticated publisher that owns this semantic
	 * destination record. Every embedded capture anchor must bind to it. */
	uint64_t owner_identity;
	sg_destination_ref_t destination;
	uint64_t generation;
	sg_destination_terminal_kind_t kind;
	union
	{
		sg_destination_static_patch_t static_patch;
		sg_destination_moving_tube_t moving_tube;
	} value;
} sg_destination_terminal_t;

static inline int SG_DestinationKindValid(sg_destination_kind_t kind)
{
	return kind >= SG_DESTINATION_FLAG && kind < SG_DESTINATION_KIND_COUNT;
}

static inline int SG_DestinationRefValid(const sg_destination_ref_t *ref)
{
	if (!ref || !SG_DestinationKindValid(ref->kind))
		return 0;
	switch (ref->kind)
	{
	case SG_DESTINATION_FLAG:
		return (ref->value.flag.team == 1U || ref->value.flag.team == 2U) &&
			ref->value.flag.location < SG_DESTINATION_FLAG_LOCATION_COUNT &&
			ref->value.flag.reserved == 0U;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return ref->value.item.item_id != 0U;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return (ref->value.carrier.team == 1U ||
			ref->value.carrier.team == 2U) &&
			ref->value.carrier.selector <
				SG_DESTINATION_CARRIER_SELECTOR_COUNT &&
			((ref->value.carrier.selector == SG_DESTINATION_CARRIER_ANY &&
			  ref->value.carrier.client_id == UINT16_MAX) ||
			 (ref->value.carrier.selector == SG_DESTINATION_CARRIER_EXACT &&
			  ref->value.carrier.client_id < UINT16_MAX));
	case SG_DESTINATION_DEFENSIVE_POST:
		return ref->value.post.region_id != UINT32_MAX;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return ref->value.point.point_id != 0U;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static inline int SG_DestinationMotionValid(sg_destination_motion_t motion)
{
	return motion == SG_DESTINATION_STATIC || motion == SG_DESTINATION_MOVING;
}

static inline int SG_DestinationFloatValid(float value)
{
	return isfinite(value) != 0 && (value != 0.0f || !signbit(value));
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

	if (!pose || pose->phase.phase_id == SG_DESTINATION_NO_PHASE ||
	    pose->phase.cell_id == SG_DESTINATION_NO_CELL ||
	    pose->region_id == SG_DESTINATION_NO_REGION)
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
	       handle->reserved[0] == 0U && handle->reserved[1] == 0U &&
	       handle->reserved[2] == 0U &&
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
	    snapshot->phase_count == 0U || snapshot->region_count == 0U ||
	    !snapshot->phases ||
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

int SG_DestinationTerminalValid(const sg_destination_terminal_t *terminal);
int SG_DestinationTerminalCaptureValidFor(
	const sg_destination_terminal_capture_t *capture,
	const sg_destination_ref_t *destination, uint64_t generation);

#endif
