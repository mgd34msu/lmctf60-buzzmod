/* sg_door_approach.c -- pure fixed-step law for direct-trigger door entry. */
#include "../g_local.h"

#include <math.h>
#include <string.h>

#include "sg_door_approach.h"

static sg_door_approach_result_t DoorApproach_Result(
	const sg_door_approach_state_t *state, sg_door_approach_reason_t reason,
	int drive, int snap_required)
{
	sg_door_approach_result_t result;

	memset(&result, 0, sizeof(result));
	result.phase = state
	    ? (sg_door_approach_phase_t)state->phase : SG_DOOR_APPROACH_FAILED;
	result.reason = reason;
	result.drive = drive;
	result.snap_required = snap_required;
	return result;
}

static sg_door_approach_result_t DoorApproach_Fail(
	sg_door_approach_state_t *state, sg_door_approach_reason_t reason)
{
	if (state)
		state->phase = SG_DOOR_APPROACH_FAILED;
	return DoorApproach_Result(state, reason, 0, 0);
}

int SG_DoorApproachPmoveEqual(const pmove_state_t *left,
	const pmove_state_t *right)
{
	int axis;

	if (!left || !right || left->pm_type != right->pm_type ||
	    left->pm_flags != right->pm_flags || left->pm_time != right->pm_time ||
	    left->gravity != right->gravity)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (left->origin[axis] != right->origin[axis] ||
		    left->velocity[axis] != right->velocity[axis] ||
		    left->delta_angles[axis] != right->delta_angles[axis])
			return 0;
	return 1;
}

int SG_DoorApproachWaterSafe(int waterlevel, int watertype)
{
	if (waterlevel < 0 || waterlevel > 1 ||
	    (watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return 0;
	return waterlevel == 0 || (watertype & CONTENTS_WATER) != 0;
}

int SG_DoorApproachInsideCapsule(const short source_q8[3],
	const short anchor_q8[3], const short point_q8[3])
{
	double segment[3], relative[3], distance[3];
	double length_squared = 0.0;
	double projection = 0.0;
	double distance_squared = 0.0;
	int axis;

	if (!source_q8 || !anchor_q8 || !point_q8)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		segment[axis] = (double)((int)anchor_q8[axis] -
		    (int)source_q8[axis]);
		relative[axis] = (double)((int)point_q8[axis] -
		    (int)source_q8[axis]);
		length_squared += segment[axis] * segment[axis];
		projection += relative[axis] * segment[axis];
	}
	if (length_squared > 0.0)
	{
		projection /= length_squared;
		if (projection < 0.0)
			projection = 0.0;
		else if (projection > 1.0)
			projection = 1.0;
	}
	else
		projection = 0.0;
	for (axis = 0; axis < 3; axis++)
	{
		distance[axis] = relative[axis] - projection * segment[axis];
		distance_squared += distance[axis] * distance[axis];
	}
	return distance_squared <=
	    (double)SG_DOOR_APPROACH_CAPSULE_Q8 *
	    (double)SG_DOOR_APPROACH_CAPSULE_Q8;
}

static int DoorApproach_Rest(const pmove_state_t *pms)
{
	return pms && pms->velocity[0] == 0 && pms->velocity[1] == 0 &&
	       pms->velocity[2] == 0;
}

static int DoorApproach_ExactAnchor(const sg_door_approach_state_t *state,
	const pmove_state_t *pms)
{
	return state && pms && pms->origin[0] == state->anchor_q8[0] &&
	       pms->origin[1] == state->anchor_q8[1] &&
	       pms->origin[2] == state->anchor_q8[2];
}

static int DoorApproach_NearAnchor(const sg_door_approach_state_t *state,
	const pmove_state_t *pms)
{
	int dx, dy, dz;

	if (!state || !pms)
		return 0;
	dx = (int)pms->origin[0] - state->anchor_q8[0];
	dy = (int)pms->origin[1] - state->anchor_q8[1];
	dz = (int)pms->origin[2] - state->anchor_q8[2];
	/* Horizontal <=2u and vertical <=2u, all on the q8 lattice. */
	return dx * dx + dy * dy <= 16 * 16 && dz >= -16 && dz <= 16;
}

static sg_door_approach_reason_t DoorApproach_PoseReason(
	const sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation, int require_expected)
{
	if (!state || !observation)
		return SG_DOOR_APPROACH_REASON_ARGUMENT;
	if (observation->pms.pm_type != PM_NORMAL ||
	    (observation->pms.pm_flags & PMF_DUCKED) ||
	    observation->pms.pm_time)
		return SG_DOOR_APPROACH_REASON_STATE;
	if (!SG_DoorApproachWaterSafe(observation->waterlevel,
	        observation->watertype) || observation->hazardous_liquid)
		return SG_DOOR_APPROACH_REASON_WATER;
	if (!observation->population_stable)
		return SG_DOOR_APPROACH_REASON_POPULATION;
	if (!observation->sweep_clear)
		return SG_DOOR_APPROACH_REASON_SWEEP;
	if (!SG_DoorApproachInsideCapsule(state->source_q8,
	        state->anchor_q8, observation->pms.origin))
		return SG_DOOR_APPROACH_REASON_CORRIDOR;
	if (require_expected &&
	    (!SG_DoorApproachPmoveEqual(&state->expected_pms,
	         &observation->pms) ||
	     state->expected_waterlevel != observation->waterlevel ||
	     state->expected_watertype != observation->watertype))
		return SG_DOOR_APPROACH_REASON_POSE;
	return SG_DOOR_APPROACH_REASON_NONE;
}

void SG_DoorApproachReset(sg_door_approach_state_t *state)
{
	if (state)
		memset(state, 0, sizeof(*state));
}

sg_door_approach_result_t SG_DoorApproachBegin(
	sg_door_approach_state_t *state, const short source_q8[3],
	const short anchor_q8[3],
	const sg_door_approach_observation_t *observation)
{
	sg_door_approach_reason_t reason;
	int axis;

	if (!state || !source_q8 || !anchor_q8 || !observation)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_ARGUMENT);
	SG_DoorApproachReset(state);
	for (axis = 0; axis < 3; axis++)
	{
		state->source_q8[axis] = source_q8[axis];
		state->anchor_q8[axis] = anchor_q8[axis];
	}
	state->phase = SG_DOOR_APPROACH_WALK;
	state->expected_pms = observation->pms;
	state->expected_watertype = observation->watertype;
	state->expected_waterlevel = observation->waterlevel;
	state->old_frame_z = observation->pms.velocity[2] * 0.125f;
	reason = DoorApproach_PoseReason(state, observation, 0);
	if (reason != SG_DOOR_APPROACH_REASON_NONE ||
	    !observation->grounded || !observation->static_support ||
	    !DoorApproach_Rest(&observation->pms) ||
	    observation->pms.origin[0] != source_q8[0] ||
	    observation->pms.origin[1] != source_q8[1] ||
	    observation->pms.origin[2] != source_q8[2])
		return DoorApproach_Fail(state, reason != SG_DOOR_APPROACH_REASON_NONE
		    ? reason : SG_DOOR_APPROACH_REASON_SUPPORT);
	return DoorApproach_Result(state, SG_DOOR_APPROACH_REASON_NONE, 1, 0);
}

sg_door_approach_result_t SG_DoorApproachPreStep(
	const sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation, int command_msec)
{
	sg_door_approach_reason_t reason;
	int drive;

	if (!state || !observation)
		return DoorApproach_Result(state,
		    SG_DOOR_APPROACH_REASON_ARGUMENT, 0, 0);
	if (state->phase != SG_DOOR_APPROACH_WALK &&
	    state->phase != SG_DOOR_APPROACH_FINALIZE)
		return DoorApproach_Result(state,
		    SG_DOOR_APPROACH_REASON_STATE, 0,
		    state->phase == SG_DOOR_APPROACH_SNAP);
	if (command_msec != SG_DOOR_APPROACH_STEP_MS)
		return DoorApproach_Result(state,
		    SG_DOOR_APPROACH_REASON_CADENCE, 0, 0);
	reason = DoorApproach_PoseReason(state, observation, 1);
	if (reason != SG_DOOR_APPROACH_REASON_NONE)
		return DoorApproach_Result(state, reason, 0, 0);
	if (state->phase == SG_DOOR_APPROACH_FINALIZE &&
	    (!DoorApproach_ExactAnchor(state, &observation->pms) ||
	     !DoorApproach_Rest(&observation->pms) || !observation->grounded ||
	     !observation->static_support))
		return DoorApproach_Result(state,
		    SG_DOOR_APPROACH_REASON_SNAP, 0, 0);
	drive = state->phase == SG_DOOR_APPROACH_WALK &&
	    (!state->touched || state->elapsed_ms >= state->resume_ms);
	return DoorApproach_Result(state, SG_DOOR_APPROACH_REASON_NONE,
	    drive, 0);
}

sg_door_approach_result_t SG_DoorApproachPostStep(
	sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation, int command_msec)
{
	sg_door_approach_reason_t reason;
	int next_elapsed;

	if (!state || !observation)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_ARGUMENT);
	if (state->phase != SG_DOOR_APPROACH_WALK &&
	    state->phase != SG_DOOR_APPROACH_FINALIZE)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_STATE);
	if (command_msec != SG_DOOR_APPROACH_STEP_MS)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_CADENCE);
	reason = DoorApproach_PoseReason(state, observation, 0);
	if (reason != SG_DOOR_APPROACH_REASON_NONE)
		return DoorApproach_Fail(state, reason);
	next_elapsed = state->elapsed_ms + command_msec;
	if (next_elapsed > SG_DOOR_APPROACH_LIMIT_MS)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_TIMEOUT);
	if (observation->grounded)
	{
		if (!observation->static_support)
			return DoorApproach_Fail(state,
			    SG_DOOR_APPROACH_REASON_SUPPORT);
		state->consecutive_air_ms = 0;
	}
	else
	{
		state->consecutive_air_ms += command_msec;
		if (state->consecutive_air_ms > SG_DOOR_APPROACH_MAX_AIR_MS)
			return DoorApproach_Fail(state,
			    SG_DOOR_APPROACH_REASON_AIR_TIME);
	}
	if ((next_elapsed % SG_DOOR_APPROACH_FRAME_MS) == 0)
	{
		if (!observation->fall_sampled || !isfinite(observation->fall_delta) ||
		    observation->fall_delta > 30.0f)
			return DoorApproach_Fail(state,
			    SG_DOOR_APPROACH_REASON_FALL);
		state->old_frame_z = observation->pms.velocity[2] * 0.125f;
	}
	else if (observation->fall_sampled)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_FALL);
	state->elapsed_ms = next_elapsed;
	state->expected_pms = observation->pms;
	state->expected_watertype = observation->watertype;
	state->expected_waterlevel = observation->waterlevel;
	if (observation->physical_touch && !state->touched)
	{
		state->touched = 1U;
		state->first_touch_ms = next_elapsed;
		state->resume_ms =
		    ((next_elapsed + SG_DOOR_APPROACH_FRAME_MS - 1) /
		     SG_DOOR_APPROACH_FRAME_MS) * SG_DOOR_APPROACH_FRAME_MS;
	}
	if (state->phase == SG_DOOR_APPROACH_WALK && state->touched &&
	    observation->grounded && observation->static_support &&
	    DoorApproach_Rest(&observation->pms) &&
	    DoorApproach_NearAnchor(state, &observation->pms))
	{
		state->phase = SG_DOOR_APPROACH_SNAP;
		state->finalize_ms =
		    (next_elapsed / SG_DOOR_APPROACH_FRAME_MS + 1) *
		    SG_DOOR_APPROACH_FRAME_MS;
		return DoorApproach_Result(state, SG_DOOR_APPROACH_REASON_NONE,
		    0, 1);
	}
	if (state->phase == SG_DOOR_APPROACH_FINALIZE)
	{
		if (!DoorApproach_ExactAnchor(state, &observation->pms) ||
		    !DoorApproach_Rest(&observation->pms) || !observation->grounded ||
		    !observation->static_support)
			return DoorApproach_Fail(state,
			    SG_DOOR_APPROACH_REASON_SNAP);
		if (next_elapsed == state->finalize_ms &&
		    (next_elapsed % SG_DOOR_APPROACH_FRAME_MS) == 0)
		{
			state->phase = SG_DOOR_APPROACH_COMPLETE;
			return DoorApproach_Result(state,
			    SG_DOOR_APPROACH_REASON_NONE, 0, 0);
		}
		if (next_elapsed > state->finalize_ms)
			return DoorApproach_Fail(state,
			    SG_DOOR_APPROACH_REASON_SNAP);
	}
	return DoorApproach_Result(state, SG_DOOR_APPROACH_REASON_NONE,
	    state->phase == SG_DOOR_APPROACH_WALK &&
	        (!state->touched || state->elapsed_ms >= state->resume_ms), 0);
}

sg_door_approach_result_t SG_DoorApproachSnapped(
	sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation)
{
	sg_door_approach_reason_t reason;
	int axis;

	if (!state || !observation || state->phase != SG_DOOR_APPROACH_SNAP)
		return DoorApproach_Fail(state, SG_DOOR_APPROACH_REASON_ARGUMENT);
	reason = DoorApproach_PoseReason(state, observation, 0);
	if (reason != SG_DOOR_APPROACH_REASON_NONE || !observation->grounded ||
	    !observation->static_support || !DoorApproach_Rest(&observation->pms) ||
	    !DoorApproach_NearAnchor(state, &observation->pms) ||
	    state->expected_waterlevel != observation->waterlevel ||
	    state->expected_watertype != observation->watertype)
		return DoorApproach_Fail(state, reason != SG_DOOR_APPROACH_REASON_NONE
		    ? reason : SG_DOOR_APPROACH_REASON_SNAP);
	state->expected_pms = observation->pms;
	for (axis = 0; axis < 3; axis++)
	{
		state->expected_pms.origin[axis] = state->anchor_q8[axis];
		state->expected_pms.velocity[axis] = 0;
	}
	state->phase = SG_DOOR_APPROACH_FINALIZE;
	return DoorApproach_Result(state, SG_DOOR_APPROACH_REASON_NONE, 0, 0);
}

const char *SG_DoorApproachReasonName(sg_door_approach_reason_t reason)
{
	switch (reason)
	{
	case SG_DOOR_APPROACH_REASON_NONE: return "none";
	case SG_DOOR_APPROACH_REASON_ARGUMENT: return "argument";
	case SG_DOOR_APPROACH_REASON_IDENTITY: return "identity";
	case SG_DOOR_APPROACH_REASON_CADENCE: return "cadence";
	case SG_DOOR_APPROACH_REASON_POSE: return "pose";
	case SG_DOOR_APPROACH_REASON_STATE: return "state";
	case SG_DOOR_APPROACH_REASON_WATER: return "water";
	case SG_DOOR_APPROACH_REASON_POPULATION: return "population";
	case SG_DOOR_APPROACH_REASON_SWEEP: return "sweep";
	case SG_DOOR_APPROACH_REASON_CORRIDOR: return "corridor";
	case SG_DOOR_APPROACH_REASON_SUPPORT: return "support";
	case SG_DOOR_APPROACH_REASON_AIR_TIME: return "air-time";
	case SG_DOOR_APPROACH_REASON_FALL: return "fall";
	case SG_DOOR_APPROACH_REASON_SNAP: return "snap";
	case SG_DOOR_APPROACH_REASON_TIMEOUT: return "timeout";
	default: return "unknown";
	}
}
