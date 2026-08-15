/* sg_replay.c -- see sg_replay.h for the ownership boundary. */
#include "q_shared.h"
#include "slipgate/sg_replay.h"

static qboolean ReplayFiniteVec(const vec3_t value)
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static qboolean ReplayBoolValid(qboolean value)
{
	return value == false || value == true;
}

static qboolean ReplayAngleShortSafe(float angle)
{
	return isfinite(angle) && angle >= -180.0f && angle < 180.0f;
}

/* This is the exact post-decode control contract, not merely a range that
 * keeps ANGLE2SHORT defined.  Re-encoding must reproduce both stored shorts,
 * graph-hook pitch stays within +/-89, and roll is never part of the view. */
static qboolean ReplayHookViewValid(const vec3_t view_angles)
{
	return ReplayFiniteVec(view_angles) &&
	       view_angles[PITCH] >= -89.0f && view_angles[PITCH] <= 89.0f &&
	       ReplayAngleShortSafe(view_angles[PITCH]) &&
	       ReplayAngleShortSafe(view_angles[YAW]) &&
	       view_angles[PITCH] ==
	           SHORT2ANGLE((short)ANGLE2SHORT(view_angles[PITCH])) &&
	       view_angles[YAW] ==
	           SHORT2ANGLE((short)ANGLE2SHORT(view_angles[YAW])) &&
	       view_angles[ROLL] == 0.0f;
}

static qboolean ReplayPoseValid(const sg_replay_pose_t *pose)
{
	return pose && ReplayFiniteVec(pose->origin) &&
	       ReplayFiniteVec(pose->velocity) && ReplayBoolValid(pose->grounded) &&
	       pose->waterlevel >= 0 && pose->waterlevel <= 3;
}

static qboolean ReplayExpectedTime(int value, int quantum)
{
	return value == SG_REPLAY_TIME_DISCOVER ||
	       (value >= 0 && quantum > 0 && (value % quantum) == 0);
}

static sg_replay_status_t ReplayFail(sg_replay_progress_t *progress,
	sg_replay_reason_t reason)
{
	if (progress)
	{
		progress->status = SG_REPLAY_FAILED;
		progress->reason = reason;
		progress->step_pending = false;
	}
	return SG_REPLAY_FAILED;
}

static sg_replay_status_t ReplayHookBoundaryHazard(
	sg_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	qboolean check_fall);

static qboolean ReplayHarmfulLiquid(const sg_replay_pose_t *pose)
{
	return pose && pose->waterlevel > 0 &&
	       (pose->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
}

static qboolean ReplayWithin(const vec3_t origin, const vec3_t destination,
	float radius, float z_limit)
{
	float dx, dy, dz;

	if (!ReplayFiniteVec(origin) || !ReplayFiniteVec(destination) ||
	    !isfinite(radius) || !isfinite(z_limit))
		return false;
	dx = destination[0] - origin[0];
	dy = destination[1] - origin[1];
	dz = destination[2] - origin[2];
	return dx * dx + dy * dy < radius * radius &&
	       dz > -z_limit && dz < z_limit;
}

static byte ReplayExitSpeed(const sg_replay_pose_t *pose)
{
	float speed, encoded;

	if (!ReplayPoseValid(pose))
		return 0;
	speed = sqrtf(pose->velocity[0] * pose->velocity[0] +
	              pose->velocity[1] * pose->velocity[1]);
	encoded = speed * 0.25f;
	return (byte)(encoded > 255.0f ? 255 : encoded);
}

static void ReplayCommandClear(usercmd_t *command)
{
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
}

static qboolean ReplayFixedViewCommand(const sg_replay_pose_t *pose,
	const vec3_t view_angles, usercmd_t *command)
{
	if (!ReplayPoseValid(pose) || !ReplayHookViewValid(view_angles) || !command)
		return false;
	ReplayCommandClear(command);
	command->angles[PITCH] = ANGLE2SHORT(view_angles[PITCH]) -
	                         pose->pms.delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(view_angles[YAW]) -
	                       pose->pms.delta_angles[YAW];
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	return true;
}

/* DROP's v3 generator/proof byte is canonical: M_PI remains a double, so the
 * atan2f product is rounded as float and the division plus ANGLE2SHORT continue
 * in double precision.  The revision-2 live shadow uses this same expression
 * and compares the complete logical usercmd before production executes it. */
static qboolean ReplayDropPlanarCommand(const sg_replay_pose_t *pose,
	const vec3_t target, usercmd_t *command)
{
	float dx, dy;
	short yaw_command;

	if (!ReplayPoseValid(pose) || !ReplayFiniteVec(target) || !command)
		return false;
	dx = target[0] - pose->origin[0];
	dy = target[1] - pose->origin[1];
	if (!SG_DropReplayPlanarYawCommand(dx, dy,
	        pose->pms.delta_angles[YAW], &yaw_command))
		return false;
	ReplayCommandClear(command);
	command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
	command->angles[YAW] = yaw_command;
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	command->forwardmove = 400;
	return true;
}

qboolean SG_DropReplayPlanarYawCommand(float dx, float dy,
	short delta_yaw, short *command_yaw)
{
	double yaw;

	if (!command_yaw || !isfinite(dx) || !isfinite(dy))
		return false;
	yaw = atan2f(dy, dx) * 180.0f / M_PI;
	if (!isfinite(yaw))
		return false;
	*command_yaw = ANGLE2SHORT(yaw) - delta_yaw;
	return true;
}

/* Hook settlement historically pins the divisor to float.  Keep this path
 * separate from DROP so neither action silently inherits the other's angle
 * rounding at the serialized usercmd-short boundary. */
static qboolean ReplayHookPlanarCommand(const sg_replay_pose_t *pose,
	const vec3_t target, usercmd_t *command)
{
	float dx, dy, yaw;

	if (!ReplayPoseValid(pose) || !ReplayFiniteVec(target) || !command)
		return false;
	dx = target[0] - pose->origin[0];
	dy = target[1] - pose->origin[1];
	yaw = atan2f(dy, dx) * 180.0f / (float)M_PI;
	if (!isfinite(yaw))
		return false;
	ReplayCommandClear(command);
	command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(yaw) -
	                       pose->pms.delta_angles[YAW];
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	command->forwardmove = 400;
	return true;
}

static qboolean ReplaySwimCommand(const sg_replay_pose_t *pose,
	const vec3_t destination, usercmd_t *command)
{
	float dx, dy, dz, horizontal, yaw, pitch;

	if (!ReplayPoseValid(pose) || !ReplayFiniteVec(destination) || !command)
		return false;
	dx = destination[0] - pose->origin[0];
	dy = destination[1] - pose->origin[1];
	dz = destination[2] - pose->origin[2];
	horizontal = sqrtf(dx * dx + dy * dy);
	if (!isfinite(horizontal) || !isfinite(dz) ||
	    (horizontal < 0.01f && fabsf(dz) < 0.01f))
		return false;
	yaw = atan2f(dy, dx) * 180.0f / (float)M_PI;
	pitch = -atan2f(dz, horizontal) * 180.0f / (float)M_PI;
	if (pitch > SG_REPLAY_SWIM_PITCH_LIMIT)
		pitch = SG_REPLAY_SWIM_PITCH_LIMIT;
	if (pitch < -SG_REPLAY_SWIM_PITCH_LIMIT)
		pitch = -SG_REPLAY_SWIM_PITCH_LIMIT;

	ReplayCommandClear(command);
	command->angles[PITCH] = ANGLE2SHORT(pitch) -
	                         pose->pms.delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(yaw) -
	                       pose->pms.delta_angles[YAW];
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	command->forwardmove = 400;
	return true;
}

static qboolean ReplayObservationValid(const sg_replay_observation_t *observation)
{
	return observation && ReplayBoolValid(observation->contact_clear) &&
	       ReplayBoolValid(observation->ground_support_valid) &&
	       ReplayBoolValid(observation->drop_arrival_contact_clear) &&
	       ReplayBoolValid(observation->drop_recovery_contact_clear) &&
	       ReplayBoolValid(observation->drop_recovery_admitted) &&
	       ReplayBoolValid(observation->drop_landing_observed) &&
	       ReplayBoolValid(observation->contaminated) &&
	       ReplayBoolValid(observation->door_passed) &&
	       ReplayBoolValid(observation->hook_rope_valid) &&
	       (!observation->hook_rope_valid ||
	        observation->hook_rope_length >= 0);
}

/* DROP owns no wait phase: a door transition after one of its commands is an
 * immediate proof failure. */
static sg_replay_status_t ReplayDropObservation(
	sg_replay_progress_t *progress,
	const sg_replay_observation_t *observation)
{
	if (observation->contaminated)
		return ReplayFail(progress, SG_REPLAY_REASON_CONTAMINATED);
	if (observation->door_passed)
		return ReplayFail(progress, SG_REPLAY_REASON_DOOR_PASSED);
	return progress->status;
}

/* SWIM and HOOK must finish consuming the exact proved command/event stream.
 * Door passage is remembered and rejects only an otherwise valid terminal;
 * contamination still ends the witness at the step where it is observed. */
static sg_replay_status_t ReplayLatchedObservation(
	sg_replay_progress_t *progress,
	const sg_replay_observation_t *observation)
{
	if (observation->contaminated)
		return ReplayFail(progress, SG_REPLAY_REASON_CONTAMINATED);
	if (observation->door_passed)
		progress->door_passed_latched = true;
	return progress->status;
}

const char *SG_ReplayReasonName(sg_replay_reason_t reason)
{
	switch (reason)
	{
	case SG_REPLAY_REASON_NONE: return "none";
	case SG_REPLAY_REASON_INVALID_ARGUMENT: return "invalid argument";
	case SG_REPLAY_REASON_INVALID_STATE: return "invalid state";
	case SG_REPLAY_REASON_INVALID_CONTROL: return "invalid control";
	case SG_REPLAY_REASON_NONFINITE_POSE: return "non-finite pose";
	case SG_REPLAY_REASON_CONTAMINATED: return "contaminated";
	case SG_REPLAY_REASON_DOOR_PASSED: return "door passed";
	case SG_REPLAY_REASON_HAZARDOUS_LIQUID: return "hazardous liquid";
	case SG_REPLAY_REASON_DAMAGING_FALL: return "damaging fall";
	case SG_REPLAY_REASON_APPROACH_TIMEOUT: return "drop approach timeout";
	case SG_REPLAY_REASON_TRAVEL_TIMEOUT: return "drop travel timeout";
	case SG_REPLAY_REASON_ACTION_TIMEOUT: return "action timeout";
	case SG_REPLAY_REASON_BELOW_DESTINATION: return "below destination";
	case SG_REPLAY_REASON_SHALLOW_WATER_CONTACT:
		return "shallow water contact";
	case SG_REPLAY_REASON_SHORT_LANDING: return "short landing";
	case SG_REPLAY_REASON_RECOVERY_LOST: return "recovery support lost";
	case SG_REPLAY_REASON_ZERO_TIME_ARRIVAL: return "zero-time arrival";
	case SG_REPLAY_REASON_TIMING_MISMATCH: return "timing mismatch";
	case SG_REPLAY_REASON_HOOK_ATTACH_TIMING: return "hook attach timing";
	case SG_REPLAY_REASON_HOOK_EVENT_ORDER: return "hook event order";
	case SG_REPLAY_REASON_HOOK_RELEASE_BEFORE_PULL:
		return "hook release before pull";
	case SG_REPLAY_REASON_HOOK_RELEASE_MISSED: return "hook release missed";
	case SG_REPLAY_REASON_HOOK_PULL_TIMEOUT: return "hook pull timeout";
	case SG_REPLAY_REASON_HOOK_SETTLE_TIMEOUT: return "hook settle timeout";
	case SG_REPLAY_REASON_HOOK_TERMINAL_LOST: return "hook terminal lost";
	default: return "unknown";
	}
}

/* Literal pure half of P_FallDelta. */
float SG_ReplayFallDelta(float old_velocity_z, float velocity_z,
	qboolean grounded, int waterlevel)
{
	float delta;

	if (old_velocity_z < 0.0f && velocity_z > old_velocity_z && !grounded)
		delta = old_velocity_z;
	else
	{
		if (!grounded)
			return 0.0f;
		delta = velocity_z - old_velocity_z;
	}
	delta = delta * delta * 0.0001f;
	if (waterlevel == 3)
		return 0.0f;
	if (waterlevel == 2)
		delta *= 0.25f;
	if (waterlevel == 1)
		delta *= 0.5f;
	return delta;
}

qboolean SG_DropReplayArrived(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	qboolean supported;

	if (!spec || !ReplayBoolValid(spec->destination_water) ||
	    !ReplayPoseValid(pose) ||
	    !ReplayObservationValid(observation) ||
	    ReplayHarmfulLiquid(pose) ||
	    !observation->drop_arrival_contact_clear ||
	    !ReplayWithin(pose->origin, spec->destination,
	                  SG_REPLAY_ARRIVE_RADIUS, SG_REPLAY_ARRIVE_Z))
		return false;
	if (spec->destination_water)
		return pose->waterlevel == 3;
	/* Runtime rejects a disallowed dynamic ground even if water overlaps it;
	 * an ungrounded body may finish on the proof's depth-two water support. */
	supported = pose->grounded ? observation->ground_support_valid :
	                            pose->waterlevel >= 2;
	return supported;
}

qboolean SG_DropReplayRecoveryReady(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	if (!spec || !ReplayBoolValid(spec->destination_water) ||
	    !ReplayPoseValid(pose) ||
	    !ReplayObservationValid(observation) ||
	    spec->destination_water ||
	    !pose->grounded || !observation->ground_support_valid ||
	    pose->waterlevel != 0 || ReplayHarmfulLiquid(pose) ||
	    !observation->drop_recovery_contact_clear)
		return false;
	return ReplayWithin(pose->origin, spec->destination,
	                    SG_RUNE_PROOF_DROP_RECOVERY_RADIUS,
	                    SG_RUNE_PROOF_DROP_RECOVERY_Z);
}

qboolean SG_SwimReplayArrived(const sg_swim_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	if (!spec || !ReplayBoolValid(spec->destination_water) ||
	    !ReplayPoseValid(pose) ||
	    !ReplayObservationValid(observation) ||
	    ReplayHarmfulLiquid(pose) || !observation->contact_clear ||
	    !ReplayWithin(pose->origin, spec->destination,
	                  SG_REPLAY_ARRIVE_RADIUS, SG_REPLAY_ARRIVE_Z))
		return false;
	if (spec->destination_water)
		return pose->waterlevel >= 2 && (pose->watertype & CONTENTS_WATER);
	return pose->grounded && pose->waterlevel < 2;
}

qboolean SG_HookReplaySettled(const sg_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	if (!spec || !ReplayPoseValid(pose) ||
	    !ReplayObservationValid(observation) ||
	    ReplayHarmfulLiquid(pose) || !observation->contact_clear ||
	    (!pose->grounded && pose->waterlevel < 2))
		return false;
	return ReplayWithin(pose->origin, spec->destination,
	                    SG_REPLAY_ARRIVE_RADIUS, SG_REPLAY_ARRIVE_Z);
}

qboolean SG_HookReplayReleaseReady(const sg_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	if (!spec || !ReplayPoseValid(pose) ||
	    !ReplayObservationValid(observation) ||
	    !observation->hook_rope_valid)
		return false;
	return ReplayWithin(pose->origin, spec->destination,
	                    SG_REPLAY_HOOK_DEST_RADIUS,
	                    SG_REPLAY_HOOK_DEST_Z) ||
	       observation->hook_rope_length < SG_REPLAY_HOOK_RELEASE_ROPE;
}

static qboolean ReplayDropSpecValid(const sg_drop_replay_spec_t *spec)
{
	return spec && ReplayFiniteVec(spec->destination) &&
	       ReplayFiniteVec(spec->lip) &&
	       ReplayBoolValid(spec->destination_water) &&
	       (spec->expected_arrival_ms == SG_REPLAY_TIME_DISCOVER ||
	        (spec->expected_arrival_ms > 0 &&
	         spec->expected_arrival_ms < SG_REPLAY_DROP_TOTAL_MS &&
	         (spec->expected_arrival_ms % SG_REPLAY_FRAME_MS) == 0));
}

sg_replay_status_t SG_DropReplayBegin(sg_drop_replay_state_t *state,
	const sg_drop_replay_spec_t *spec, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	if (!state)
		return SG_REPLAY_FAILED;
	memset(state, 0, sizeof(*state));
	state->progress.status = SG_REPLAY_RUNNING;
	state->progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
	state->walkoff_ms = SG_REPLAY_TIME_DISCOVER;
	if (!ReplayDropSpecValid(spec) || !ReplayObservationValid(observation))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!ReplayPoseValid(pose) || !isfinite(old_frame_z))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->spec = *spec;
	state->progress.old_frame_z = old_frame_z;
	/* Source contamination is known before the first command.  Door evidence
	 * intentionally retains DROP's post-command rejection policy: the live
	 * adapter carries a source door bit into its first 25 ms observation. */
	if (observation->contaminated)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_CONTAMINATED);
	/* Drop_Rollout judges harmful source liquid at its elapsed-zero boundary. */
	if (ReplayHarmfulLiquid(pose))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HAZARDOUS_LIQUID);
	return state->progress.status;
}

sg_replay_status_t SG_DropReplayPreStep(sg_drop_replay_state_t *state,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	float dx, dy, yaw, walk_x, walk_y;
	vec3_t target;

	if (!state || !pose || !command)
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (state->progress.step_pending)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);

	dx = state->spec.lip[0] - pose->origin[0];
	dy = state->spec.lip[1] - pose->origin[1];
	yaw = state->spec.heading * (360.0f / 256.0f);
	walk_x = cosf(yaw * (float)M_PI / 180.0f);
	walk_y = sinf(yaw * (float)M_PI / 180.0f);
	if (!state->walkoff &&
	    (dx * dx + dy * dy <=
	         SG_REPLAY_DROP_HANDOFF_RADIUS * SG_REPLAY_DROP_HANDOFF_RADIUS ||
	     dx * walk_x + dy * walk_y <= 0.0f ||
	     (state->progress.elapsed_ms > 0 && !pose->grounded)))
	{
		state->walkoff = true;
		state->walkoff_ms = state->progress.elapsed_ms;
	}
	if (!state->walkoff &&
	    state->progress.elapsed_ms >= SG_REPLAY_DROP_APPROACH_MS)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_APPROACH_TIMEOUT);
	if (state->walkoff && state->progress.elapsed_ms - state->walkoff_ms >=
	                          SG_REPLAY_DROP_TRAVEL_MS)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_TRAVEL_TIMEOUT);
	if (state->progress.elapsed_ms >= SG_REPLAY_DROP_TOTAL_MS)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_ACTION_TIMEOUT);

	if (state->recovery)
	{
		VectorCopy(state->spec.destination, target);
		if (!ReplayDropPlanarCommand(pose, target, command))
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_INVALID_CONTROL);
	}
	else if (state->walkoff)
	{
		ReplayCommandClear(command);
		command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
		command->angles[YAW] = ANGLE2SHORT(yaw) -
		                       pose->pms.delta_angles[YAW];
		command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
		command->forwardmove = 400;
	}
	else
	{
		VectorCopy(state->spec.lip, target);
		if (!ReplayDropPlanarCommand(pose, target, command))
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_INVALID_CONTROL);
	}
	state->progress.step_pending = true;
	return state->progress.status;
}

sg_replay_status_t SG_DropReplayPostStep(sg_drop_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	qboolean arrived, recovery_start;
	qboolean airborne_after, aligned_contact;

	if (!state || !pose || !ReplayObservationValid(observation))
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (!state->progress.step_pending)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	state->progress.step_pending = false;
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->progress.elapsed_ms += SG_REPLAY_STEP_MS;
	if (ReplayDropObservation(&state->progress, observation) ==
	    SG_REPLAY_FAILED)
		return state->progress.status;

	/* Recovery support loss is immediate at 25/50/75 ms.  At an aligned
	 * production boundary, terminal gets first refusal before the complete
	 * recovery envelope is revalidated below. */
	if (state->recovery &&
	    (state->progress.elapsed_ms % SG_REPLAY_FRAME_MS) != 0 &&
	    (!pose->grounded || !observation->ground_support_valid))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_RECOVERY_LOST);
	airborne_after = state->airborne ||
	    (state->walkoff && !pose->grounded);
	state->airborne = airborne_after;
	if (pose->origin[2] <
	    state->spec.destination[2] - SG_REPLAY_DROP_BELOW_Z)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_BELOW_DESTINATION);
	/* Drop_Rollout's loop does not inspect a state reached at 4500 ms. */
	if (state->progress.elapsed_ms >= SG_REPLAY_DROP_TOTAL_MS)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_ACTION_TIMEOUT);
	if ((state->progress.elapsed_ms % SG_REPLAY_FRAME_MS) != 0)
		return state->progress.status;
	if (ReplayHarmfulLiquid(pose))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HAZARDOUS_LIQUID);

	arrived = state->walkoff && airborne_after &&
	          SG_DropReplayArrived(&state->spec, pose, observation);
	/* Revision 2 gives the terminal its first and only chance before contact
	 * policy.  A wet destination can never splice a dry shelf recovery, while
	 * a dry destination may admit exactly one supported, clear, dry recovery. */
	recovery_start = !arrived && !state->recovery && state->walkoff &&
	                 airborne_after && !state->spec.destination_water &&
	                 SG_DropReplayRecoveryReady(&state->spec, pose,
	                                            observation);
	if (!arrived && state->recovery &&
	    !SG_DropReplayRecoveryReady(&state->spec, pose, observation))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_RECOVERY_LOST);
	if (!arrived && state->spec.destination_water && airborne_after &&
	    pose->waterlevel > 0 && pose->waterlevel < 3)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_SHALLOW_WATER_CONTACT);
	aligned_contact = pose->grounded || pose->waterlevel >= 2;
	if (!arrived && !state->recovery && state->walkoff && airborne_after &&
	    aligned_contact && !recovery_start)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_SHORT_LANDING);
	if (SG_ReplayFallDelta(state->progress.old_frame_z, pose->velocity[2],
	                       pose->grounded, pose->waterlevel) >
	        SG_RUNE_PROOF_DAMAGING_FALL_DELTA &&
	    !arrived && !recovery_start)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_DAMAGING_FALL);
	state->progress.old_frame_z = pose->velocity[2];
	if (recovery_start)
		state->recovery = true;
	if (arrived)
	{
		if (state->spec.expected_arrival_ms != SG_REPLAY_TIME_DISCOVER &&
		    state->progress.elapsed_ms != state->spec.expected_arrival_ms)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_TIMING_MISMATCH);
		state->progress.arrival_ms = state->progress.elapsed_ms;
		state->progress.exit_speed = ReplayExitSpeed(pose);
		state->progress.status = SG_REPLAY_ARRIVED;
		return state->progress.status;
	}
	if (state->spec.expected_arrival_ms != SG_REPLAY_TIME_DISCOVER &&
	    state->progress.elapsed_ms >= state->spec.expected_arrival_ms)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_TIMING_MISMATCH);
	return state->progress.status;
}

static qboolean ReplaySwimSpecValid(const sg_swim_replay_spec_t *spec)
{
	return spec && ReplayFiniteVec(spec->destination) &&
	       ReplayBoolValid(spec->destination_water) &&
	       (spec->expected_arrival_ms == SG_REPLAY_TIME_DISCOVER ||
	        (spec->expected_arrival_ms > 0 &&
	         spec->expected_arrival_ms < SG_REPLAY_SWIM_LIMIT_MS &&
	         (spec->expected_arrival_ms % SG_REPLAY_FRAME_MS) == 0));
}

sg_replay_status_t SG_SwimReplayBegin(sg_swim_replay_state_t *state,
	const sg_swim_replay_spec_t *spec, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	if (!state)
		return SG_REPLAY_FAILED;
	memset(state, 0, sizeof(*state));
	state->progress.status = SG_REPLAY_RUNNING;
	state->progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
	if (!ReplaySwimSpecValid(spec) || !ReplayObservationValid(observation))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!ReplayPoseValid(pose) || !isfinite(old_frame_z))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->spec = *spec;
	state->progress.old_frame_z = old_frame_z;
	if (ReplayLatchedObservation(&state->progress, observation) ==
	    SG_REPLAY_FAILED)
		return state->progress.status;
	if (ReplayHarmfulLiquid(pose))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HAZARDOUS_LIQUID);
	if (SG_SwimReplayArrived(spec, pose, observation))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_ZERO_TIME_ARRIVAL);
	return state->progress.status;
}

sg_replay_status_t SG_SwimReplayPreStep(sg_swim_replay_state_t *state,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	if (!state || !pose || !command)
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (state->progress.step_pending)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	if (state->progress.elapsed_ms >= SG_REPLAY_SWIM_LIMIT_MS)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_ACTION_TIMEOUT);
	if (!ReplaySwimCommand(pose, state->spec.destination, command))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_INVALID_CONTROL);
	state->progress.step_pending = true;
	return state->progress.status;
}

sg_replay_status_t SG_SwimReplayPostStep(sg_swim_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	qboolean arrived;

	if (!state || !pose || !ReplayObservationValid(observation))
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (!state->progress.step_pending)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	state->progress.step_pending = false;
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->progress.elapsed_ms += SG_REPLAY_STEP_MS;
	if (ReplayLatchedObservation(&state->progress, observation) ==
	    SG_REPLAY_FAILED)
		return state->progress.status;
	/* SG_OracleSwimTraverse exits its loop without inspecting the 3000 ms
	 * pose, although per-step contamination has already been observed. */
	if (state->progress.elapsed_ms >= SG_REPLAY_SWIM_LIMIT_MS)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_ACTION_TIMEOUT);
	if ((state->progress.elapsed_ms % SG_REPLAY_FRAME_MS) != 0)
		return state->progress.status;
	if (ReplayHarmfulLiquid(pose))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HAZARDOUS_LIQUID);
	arrived = SG_SwimReplayArrived(&state->spec, pose, observation);
	if (SG_ReplayFallDelta(state->progress.old_frame_z, pose->velocity[2],
	                       pose->grounded, pose->waterlevel) >
	    SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_DAMAGING_FALL);
	state->progress.old_frame_z = pose->velocity[2];
	if (arrived)
	{
		if (state->spec.expected_arrival_ms != SG_REPLAY_TIME_DISCOVER &&
		    state->progress.elapsed_ms != state->spec.expected_arrival_ms)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_TIMING_MISMATCH);
		state->progress.arrival_ms = state->progress.elapsed_ms;
		state->progress.exit_speed = ReplayExitSpeed(pose);
		if (state->progress.door_passed_latched)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_DOOR_PASSED);
		state->progress.status = SG_REPLAY_ARRIVED;
		return state->progress.status;
	}
	if (state->spec.expected_arrival_ms != SG_REPLAY_TIME_DISCOVER &&
	    state->progress.elapsed_ms >= state->spec.expected_arrival_ms)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_TIMING_MISMATCH);
	return state->progress.status;
}

static qboolean ReplayHookSpecValid(const sg_hook_replay_spec_t *spec)
{
	int max_settle;

	if (!spec || !ReplayFiniteVec(spec->bite) ||
	    !ReplayFiniteVec(spec->destination) ||
	    !ReplayHookViewValid(spec->view_angles) ||
	    spec->flight_ms < SG_REPLAY_FRAME_MS ||
	    spec->flight_ms > SG_REPLAY_HOOK_FLIGHT_MAX_MS ||
	    (spec->flight_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    spec->settle_limit_ms < SG_RUNE_PROOF_HOOK_DRY_SETTLE_MS ||
	    spec->settle_limit_ms > SG_RUNE_PROOF_HOOK_WATER_SETTLE_MS ||
	    !ReplayExpectedTime(spec->expected_release_ms, SG_REPLAY_STEP_MS) ||
	    !ReplayExpectedTime(spec->expected_pull_ms, SG_REPLAY_FRAME_MS) ||
	    !ReplayExpectedTime(spec->expected_settle_arrival_ms,
	                        SG_REPLAY_STEP_MS) ||
	    !ReplayExpectedTime(spec->expected_settle_ms, SG_REPLAY_FRAME_MS))
		return false;
	if (spec->expected_release_ms != SG_REPLAY_TIME_DISCOVER &&
	    (spec->expected_release_ms <= 0 ||
	     spec->expected_release_ms > SG_REPLAY_HOOK_PULL_LIMIT_MS))
		return false;
	if (spec->expected_pull_ms != SG_REPLAY_TIME_DISCOVER &&
	    (spec->expected_pull_ms <= 0 ||
	     spec->expected_pull_ms > SG_REPLAY_HOOK_PULL_LIMIT_MS))
		return false;
	max_settle = ((spec->settle_limit_ms + SG_REPLAY_FRAME_MS - 1) /
	              SG_REPLAY_FRAME_MS) * SG_REPLAY_FRAME_MS;
	if (spec->expected_settle_arrival_ms != SG_REPLAY_TIME_DISCOVER &&
	    spec->expected_settle_arrival_ms > max_settle)
		return false;
	if (spec->expected_settle_ms != SG_REPLAY_TIME_DISCOVER &&
	    (spec->expected_settle_ms <= 0 ||
	     spec->expected_settle_ms > max_settle))
		return false;
	if (spec->expected_release_ms != SG_REPLAY_TIME_DISCOVER &&
	    spec->expected_pull_ms != SG_REPLAY_TIME_DISCOVER &&
	    spec->expected_release_ms > spec->expected_pull_ms)
		return false;
	if (spec->expected_settle_arrival_ms != SG_REPLAY_TIME_DISCOVER &&
	    spec->expected_settle_ms != SG_REPLAY_TIME_DISCOVER &&
	    spec->expected_settle_arrival_ms > spec->expected_settle_ms)
		return false;
	return true;
}

sg_replay_status_t SG_HookReplayBegin(sg_hook_replay_state_t *state,
	const sg_hook_replay_spec_t *spec, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	int preattach_ms;

	if (!state)
		return SG_REPLAY_FAILED;
	memset(state, 0, sizeof(*state));
	state->progress.status = SG_REPLAY_RUNNING;
	state->progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
	state->release_ms = SG_REPLAY_TIME_DISCOVER;
	state->settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	if (!ReplayHookSpecValid(spec) || !ReplayObservationValid(observation))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!ReplayPoseValid(pose) || !isfinite(old_frame_z))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->spec = *spec;
	state->progress.old_frame_z = old_frame_z;
	if (ReplayLatchedObservation(&state->progress, observation) ==
	    SG_REPLAY_FAILED)
		return state->progress.status;
	if (SG_ReplayFallDelta(old_frame_z, pose->velocity[2], pose->grounded,
	                       pose->waterlevel) >
	    SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_DAMAGING_FALL);
	state->progress.old_frame_z = pose->velocity[2];
	preattach_ms = spec->flight_ms >= SG_REPLAY_FRAME_MS ?
	               spec->flight_ms - SG_REPLAY_FRAME_MS : 0;
	state->phase = preattach_ms > 0 ? SG_HOOK_REPLAY_FLIGHT :
	                                  SG_HOOK_REPLAY_WAIT_ATTACH;
	return state->progress.status;
}

sg_replay_status_t SG_HookReplayAttached(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose)
{
	int preattach_ms;

	if (!state || !pose)
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	preattach_ms = state->spec.flight_ms >= SG_REPLAY_FRAME_MS ?
	               state->spec.flight_ms - SG_REPLAY_FRAME_MS : 0;
	if (state->progress.step_pending ||
	    state->phase != SG_HOOK_REPLAY_WAIT_ATTACH ||
	    state->flight_body_ms != preattach_ms)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->attach_pms = pose->pms;
	state->attach_grounded = pose->grounded;
	state->attach_watertype = pose->watertype;
	state->attach_waterlevel = pose->waterlevel;
	state->phase = SG_HOOK_REPLAY_ATTACH_FRAME;
	state->phase_step = 0;
	return state->progress.status;
}

sg_replay_status_t SG_HookReplayPullApplied(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose)
{
	if (!state || !pose)
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (state->progress.step_pending ||
	    state->phase != SG_HOOK_REPLAY_WAIT_PULL)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->progress.old_frame_z = pose->velocity[2];
	state->phase = SG_HOOK_REPLAY_PULL_FRAME;
	state->phase_step = 0;
	return state->progress.status;
}

sg_replay_status_t SG_HookReplayReleaseApplied(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose)
{
	if (!state || !pose)
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (state->progress.step_pending ||
	    state->phase != SG_HOOK_REPLAY_PULL_FRAME ||
	    !state->release_requested || state->release_applied)
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->release_applied = true;
	/* ctf_hook_abort clears both velocity Z and oldvelocity Z on support.  The
	 * adapter owns the actual pose write; this mirrors the history write. */
	if (pose->grounded)
		state->progress.old_frame_z = 0.0f;
	/* If readiness was discovered by substep four, PostStep had to return so
	 * the host could perform the abort.  The event now owns the remainder of
	 * that same production boundary; settlement never starts one frame late. */
	if (state->phase_step == SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS)
	{
		state->phase_step = 0;
		if (ReplayHookBoundaryHazard(state, pose, true) == SG_REPLAY_FAILED)
			return state->progress.status;
		if (state->spec.expected_pull_ms != SG_REPLAY_TIME_DISCOVER &&
		    state->pull_ms != state->spec.expected_pull_ms)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_TIMING_MISMATCH);
		state->phase = SG_HOOK_REPLAY_SETTLE;
		state->arrived_in_frame = false;
	}
	return state->progress.status;
}

sg_replay_status_t SG_HookReplayPreStep(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	qboolean settled;

	if (!state || !pose || !ReplayObservationValid(observation) || !command)
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (state->progress.step_pending)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	switch (state->phase)
	{
	case SG_HOOK_REPLAY_FLIGHT:
	case SG_HOOK_REPLAY_ATTACH_FRAME:
		if (!ReplayFixedViewCommand(pose, state->spec.view_angles, command))
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_INVALID_CONTROL);
		break;
	case SG_HOOK_REPLAY_WAIT_ATTACH:
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
	case SG_HOOK_REPLAY_WAIT_PULL:
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	case SG_HOOK_REPLAY_PULL_FRAME:
		if (state->release_requested && !state->release_applied)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_HOOK_EVENT_ORDER);
		if (!ReplayFixedViewCommand(pose, state->spec.view_angles, command))
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_INVALID_CONTROL);
		break;
	case SG_HOOK_REPLAY_SETTLE:
		settled = SG_HookReplaySettled(&state->spec, pose, observation);
		if (!state->arrived_in_frame && settled)
		{
			state->arrived_in_frame = true;
			state->settle_arrival_ms = state->settle_ms;
			if (state->spec.expected_settle_arrival_ms !=
			        SG_REPLAY_TIME_DISCOVER &&
			    state->settle_arrival_ms !=
			        state->spec.expected_settle_arrival_ms)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_TIMING_MISMATCH);
		}
		if (state->arrived_in_frame)
			ReplayCommandClear(command); /* literal zero-fill, including angles */
		else
		{
			if ((state->spec.expected_settle_arrival_ms !=
			         SG_REPLAY_TIME_DISCOVER &&
			     state->settle_ms >=
			         state->spec.expected_settle_arrival_ms) ||
			    (state->spec.expected_settle_ms != SG_REPLAY_TIME_DISCOVER &&
			     state->settle_ms >= state->spec.expected_settle_ms) ||
			    (state->phase_step == 0 &&
			     state->settle_ms >= state->spec.settle_limit_ms))
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_HOOK_SETTLE_TIMEOUT);
			if (!ReplayHookPlanarCommand(pose, state->spec.destination, command))
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_INVALID_CONTROL);
		}
		break;
	default:
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	}
	state->progress.step_pending = true;
	return state->progress.status;
}

static sg_replay_status_t ReplayHookBoundaryHazard(
	sg_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	qboolean check_fall)
{
	if (ReplayHarmfulLiquid(pose))
		return ReplayFail(&state->progress,
		                  SG_REPLAY_REASON_HAZARDOUS_LIQUID);
	if (check_fall &&
	    SG_ReplayFallDelta(state->progress.old_frame_z, pose->velocity[2],
	                       pose->grounded, pose->waterlevel) >
	        SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_DAMAGING_FALL);
	if (check_fall)
		state->progress.old_frame_z = pose->velocity[2];
	return state->progress.status;
}

sg_replay_status_t SG_HookReplayPostStep(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	qboolean ready;
	int preattach_ms;

	if (!state || !pose || !ReplayObservationValid(observation))
		return state ? ReplayFail(&state->progress,
		                          SG_REPLAY_REASON_INVALID_ARGUMENT) :
		               SG_REPLAY_FAILED;
	if (state->progress.status != SG_REPLAY_RUNNING)
		return state->progress.status;
	if (!state->progress.step_pending)
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	state->progress.step_pending = false;
	if (!ReplayPoseValid(pose))
		return ReplayFail(&state->progress, SG_REPLAY_REASON_NONFINITE_POSE);
	state->progress.elapsed_ms += SG_REPLAY_STEP_MS;
	if (ReplayLatchedObservation(&state->progress, observation) ==
	    SG_REPLAY_FAILED)
		return state->progress.status;

	switch (state->phase)
	{
	case SG_HOOK_REPLAY_FLIGHT:
		state->phase_step++;
		state->flight_body_ms += SG_REPLAY_STEP_MS;
		if (state->phase_step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS)
			return state->progress.status;
		state->phase_step = 0;
		if (ReplayHookBoundaryHazard(state, pose, true) == SG_REPLAY_FAILED)
			return state->progress.status;
		preattach_ms = state->spec.flight_ms >= SG_REPLAY_FRAME_MS ?
		               state->spec.flight_ms - SG_REPLAY_FRAME_MS : 0;
		if (state->flight_body_ms > preattach_ms)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
		if (state->flight_body_ms == preattach_ms)
			state->phase = SG_HOOK_REPLAY_WAIT_ATTACH;
		return state->progress.status;

	case SG_HOOK_REPLAY_ATTACH_FRAME:
		state->phase_step++;
		if (state->phase_step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS)
			return state->progress.status;
		state->phase_step = 0;
		if (ReplayHookBoundaryHazard(state, pose, false) == SG_REPLAY_FAILED)
			return state->progress.status;
		if (!observation->hook_rope_valid)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_INVALID_ARGUMENT);
		if (SG_HookReplayReleaseReady(&state->spec, pose, observation))
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_HOOK_RELEASE_BEFORE_PULL);
		state->phase = SG_HOOK_REPLAY_WAIT_PULL;
		return state->progress.status;

	case SG_HOOK_REPLAY_PULL_FRAME:
		state->phase_step++;
		state->pull_ms += SG_REPLAY_STEP_MS;
		if (!state->release_requested)
		{
			if (!observation->hook_rope_valid)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_INVALID_ARGUMENT);
			ready = SG_HookReplayReleaseReady(&state->spec, pose, observation);
			if (ready)
			{
				if (state->spec.expected_release_ms !=
				        SG_REPLAY_TIME_DISCOVER &&
				    state->pull_ms != state->spec.expected_release_ms)
					return ReplayFail(&state->progress,
					                  SG_REPLAY_REASON_TIMING_MISMATCH);
				state->release_requested = true;
				state->release_ms = state->pull_ms;
			}
			else if (state->spec.expected_release_ms !=
			             SG_REPLAY_TIME_DISCOVER &&
			         state->pull_ms >= state->spec.expected_release_ms)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_HOOK_RELEASE_MISSED);
		}
		if (state->phase_step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS)
			return state->progress.status;
		/* On substep four the release callback necessarily follows this return.
		 * Leave phase_step at four; the explicit ReleaseApplied event finalizes
		 * this same production boundary. */
		if (state->release_requested && !state->release_applied)
			return state->progress.status;
		state->phase_step = 0;
		if (ReplayHookBoundaryHazard(state, pose,
		                             state->release_requested) ==
		    SG_REPLAY_FAILED)
			return state->progress.status;
		if (state->release_requested)
		{
			if (!state->release_applied)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_HOOK_EVENT_ORDER);
			if (state->spec.expected_pull_ms != SG_REPLAY_TIME_DISCOVER &&
			    state->pull_ms != state->spec.expected_pull_ms)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_TIMING_MISMATCH);
			state->phase = SG_HOOK_REPLAY_SETTLE;
			state->arrived_in_frame = false;
			return state->progress.status;
		}
		if (state->pull_ms >= SG_REPLAY_HOOK_PULL_LIMIT_MS)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_HOOK_PULL_TIMEOUT);
		state->phase = SG_HOOK_REPLAY_WAIT_PULL;
		return state->progress.status;

	case SG_HOOK_REPLAY_SETTLE:
		state->phase_step++;
		state->settle_ms += SG_REPLAY_STEP_MS;
		if (!state->arrived_in_frame &&
		    SG_HookReplaySettled(&state->spec, pose, observation))
		{
			state->arrived_in_frame = true;
			state->settle_arrival_ms = state->settle_ms;
			if (state->spec.expected_settle_arrival_ms !=
			        SG_REPLAY_TIME_DISCOVER &&
			    state->settle_arrival_ms !=
			        state->spec.expected_settle_arrival_ms)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_TIMING_MISMATCH);
		}
		else if (!state->arrived_in_frame &&
		         state->spec.expected_settle_arrival_ms !=
		             SG_REPLAY_TIME_DISCOVER &&
		         state->settle_ms >=
		             state->spec.expected_settle_arrival_ms)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_TIMING_MISMATCH);
		if (state->phase_step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS)
			return state->progress.status;
		state->phase_step = 0;
		if (ReplayHookBoundaryHazard(state, pose, true) == SG_REPLAY_FAILED)
			return state->progress.status;
		if (state->arrived_in_frame)
		{
			if (!SG_HookReplaySettled(&state->spec, pose, observation))
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_HOOK_TERMINAL_LOST);
			if (state->spec.expected_settle_ms != SG_REPLAY_TIME_DISCOVER &&
			    state->settle_ms != state->spec.expected_settle_ms)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_TIMING_MISMATCH);
			state->progress.arrival_ms = state->settle_arrival_ms;
			state->progress.exit_speed = ReplayExitSpeed(pose);
			if (state->progress.door_passed_latched)
				return ReplayFail(&state->progress,
				                  SG_REPLAY_REASON_DOOR_PASSED);
			state->progress.status = SG_REPLAY_ARRIVED;
			return state->progress.status;
		}
		if (state->settle_ms >= state->spec.settle_limit_ms)
			return ReplayFail(&state->progress,
			                  SG_REPLAY_REASON_HOOK_SETTLE_TIMEOUT);
		state->arrived_in_frame = false;
		return state->progress.status;

	case SG_HOOK_REPLAY_WAIT_ATTACH:
	case SG_HOOK_REPLAY_WAIT_PULL:
	default:
		return ReplayFail(&state->progress, SG_REPLAY_REASON_INVALID_STATE);
	}
}
