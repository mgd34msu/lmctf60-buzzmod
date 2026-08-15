/* sg_swim_live.c -- see sg_swim_live.h for the live/replay boundary. */
#include "q_shared.h"
#include "slipgate/sg_swim_live.h"

static sg_swim_live_result_t SwimLiveResult(sg_swim_live_outcome_t outcome,
	sg_swim_live_failure_t failure, sg_replay_reason_t replay_reason)
{
	sg_swim_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = replay_reason;
	return result;
}

static qboolean SwimLiveOwnerValid(const sg_swim_replay_state_t *replay,
	const qboolean *active, const int *replay_link)
{
	return replay && active && replay_link;
}

static qboolean SwimLiveCommandEqual(const usercmd_t *first,
	const usercmd_t *second)
{
	int axis;

	if (!first || !second || first->msec != second->msec ||
	    first->buttons != second->buttons ||
	    first->forwardmove != second->forwardmove ||
	    first->sidemove != second->sidemove ||
	    first->upmove != second->upmove ||
	    first->impulse != second->impulse ||
	    first->lightlevel != second->lightlevel)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (first->angles[axis] != second->angles[axis])
			return false;
	return true;
}

static qboolean SwimLiveHarmfulLiquid(const sg_replay_pose_t *pose)
{
	return pose && pose->waterlevel > 0 &&
	       (pose->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
}

static sg_replay_observation_t SwimLiveObservation(qboolean contact_clear)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.contact_clear = contact_clear;
	/* Ordinary live SWIM has no legacy trigger-contamination or door-passage
	 * event latch.  False is therefore the only behavior-neutral observation. */
	observation.contaminated = false;
	observation.door_passed = false;
	return observation;
}

static sg_swim_live_result_t SwimLiveFallback(
	sg_swim_replay_state_t *replay, qboolean *active, int *replay_link,
	sg_swim_live_failure_t failure, sg_replay_reason_t replay_reason)
{
	SG_SwimLiveDeactivate(replay, active, replay_link);
	return SwimLiveResult(SG_SWIM_LIVE_FALLBACK, failure, replay_reason);
}

const char *SG_SwimLiveFailureName(sg_swim_live_failure_t failure)
{
	switch (failure)
	{
	case SG_SWIM_LIVE_FAILURE_NONE: return "none";
	case SG_SWIM_LIVE_FAILURE_OWNER: return "owner";
	case SG_SWIM_LIVE_FAILURE_LINK: return "link";
	case SG_SWIM_LIVE_FAILURE_CADENCE: return "cadence";
	case SG_SWIM_LIVE_FAILURE_BEGIN: return "begin";
	case SG_SWIM_LIVE_FAILURE_REDUCER_CONTROL: return "reducer-control";
	case SG_SWIM_LIVE_FAILURE_LEGACY_CONTROL: return "legacy-control";
	case SG_SWIM_LIVE_FAILURE_COMMAND_DIFFERENTIAL:
		return "command-differential";
	case SG_SWIM_LIVE_FAILURE_POSTSTEP: return "poststep";
	case SG_SWIM_LIVE_FAILURE_HAZARDOUS_LIQUID:
		return "hazardous-liquid";
	case SG_SWIM_LIVE_FAILURE_BOUNDARY: return "boundary";
	default: return "unknown";
	}
}

void SG_SwimLiveReset(sg_swim_replay_state_t *replay, qboolean *active,
	int *replay_link, qboolean *validated, int *proved_ms, int *elapsed_ms)
{
	if (replay)
		memset(replay, 0, sizeof(*replay));
	if (active)
		*active = false;
	if (replay_link)
		*replay_link = -1;
	if (validated)
		*validated = false;
	if (proved_ms)
		*proved_ms = 0;
	if (elapsed_ms)
		*elapsed_ms = 0;
}

void SG_SwimLiveDeactivate(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link)
{
	(void)replay; /* Preserve the failed reducer progress for stable diagnostics. */
	if (active)
		*active = false;
	if (replay_link)
		*replay_link = -1;
}

void SG_SwimLivePose(sg_replay_pose_t *pose, const pmove_state_t *pms,
	const vec3_t origin, const vec3_t velocity, qboolean grounded,
	int watertype, int waterlevel)
{
	if (!pose)
		return;
	memset(pose, 0, sizeof(*pose));
	if (pms)
		pose->pms = *pms;
	if (origin)
		VectorCopy(origin, pose->origin);
	if (velocity)
		VectorCopy(velocity, pose->velocity);
	pose->grounded = grounded;
	pose->watertype = watertype;
	pose->waterlevel = waterlevel;
}

sg_swim_live_result_t SG_SwimLiveBegin(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const vec3_t destination, qboolean destination_water,
	int expected_arrival_ms, const sg_replay_pose_t *pose,
	float old_frame_z)
{
	sg_swim_replay_spec_t spec;
	sg_replay_observation_t observation;
	sg_replay_status_t status;

	if (!SwimLiveOwnerValid(replay, active, replay_link))
		return SwimLiveResult(SG_SWIM_LIVE_FALLBACK,
		    SG_SWIM_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	SG_SwimLiveDeactivate(replay, active, replay_link);
	if (action_link < 0)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_ARGUMENT);
	memset(&spec, 0, sizeof(spec));
	if (destination)
		VectorCopy(destination, spec.destination);
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = expected_arrival_ms;
	/* Arrival/contact is intentionally not sampled at elapsed zero. */
	observation = SwimLiveObservation(false);
	status = SG_SwimReplayBegin(replay, &spec, pose, &observation,
	                            old_frame_z);
	if (status != SG_REPLAY_RUNNING)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_BEGIN, replay->progress.reason);
	*active = true;
	*replay_link = action_link;
	return SwimLiveResult(SG_SWIM_LIVE_RUNNING,
	                      SG_SWIM_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_swim_live_result_t SG_SwimLivePreStep(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, const vec3_t legacy_destination,
	sg_swim_live_command_fn legacy_command, usercmd_t *command)
{
	usercmd_t reducer_cmd, legacy_cmd;
	sg_replay_status_t status;
	qboolean legacy_ok;
	byte legacy_msec;

	if (!command || !legacy_command)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	legacy_msec = command->msec;
	memset(&legacy_cmd, 0, sizeof(legacy_cmd));
	legacy_cmd.msec = legacy_msec;
	legacy_ok = pose && legacy_destination &&
	    legacy_command(pose->origin, legacy_destination, &pose->pms,
	                   &legacy_cmd);
	/* Construct the current action's initialized legacy byte stream before
	 * trusting reducer ownership.  A stale active/link pair must deactivate,
	 * but it must not turn the already-selected action into a zero command. */
	*command = legacy_cmd;
	if (!SwimLiveOwnerValid(replay, active, replay_link))
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	memset(&reducer_cmd, 0, sizeof(reducer_cmd));
	reducer_cmd.msec = SG_REPLAY_STEP_MS;
	status = SG_SwimReplayPreStep(replay, pose, &reducer_cmd);
	if (!legacy_ok)
	{
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_LEGACY_CONTROL,
		    status == SG_REPLAY_FAILED ? replay->progress.reason :
		                                 SG_REPLAY_REASON_INVALID_CONTROL);
	}
	if (status != SG_REPLAY_RUNNING)
	{
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_REDUCER_CONTROL, replay->progress.reason);
	}
	if (!SwimLiveCommandEqual(&reducer_cmd, &legacy_cmd))
	{
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	}
	*command = reducer_cmd;
	return SwimLiveResult(SG_SWIM_LIVE_RUNNING,
	                      SG_SWIM_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_swim_live_result_t SG_SwimLivePostStep(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose)
{
	sg_replay_observation_t observation;
	sg_replay_status_t status;

	if (!SwimLiveOwnerValid(replay, active, replay_link))
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	if (!replay->progress.step_pending ||
	    ((replay->progress.elapsed_ms + SG_REPLAY_STEP_MS) %
	     SG_REPLAY_FRAME_MS) == 0)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_CADENCE, SG_REPLAY_REASON_INVALID_STATE);
	/* The old path samples swim_hazard once before the four-command loop.  If
	 * this command entered lava/slime, stop comparing but let the caller emit
	 * the remaining legacy commands; the next outer frame retains the existing
	 * 60 second shelf and escape policy. */
	if (SwimLiveHarmfulLiquid(pose))
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_HAZARDOUS_LIQUID,
		    SG_REPLAY_REASON_HAZARDOUS_LIQUID);
	observation = SwimLiveObservation(false);
	status = SG_SwimReplayPostStep(replay, pose, &observation);
	if (status != SG_REPLAY_RUNNING)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_POSTSTEP, replay->progress.reason);
	return SwimLiveResult(SG_SWIM_LIVE_RUNNING,
	                      SG_SWIM_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_swim_live_result_t SG_SwimLiveBoundary(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	int live_elapsed_ms, const sg_replay_pose_t *pose,
	sg_swim_live_arrival_fn legacy_arrival, void *context)
{
	sg_swim_live_result_t result;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	int boundary_ms;

	if (!SwimLiveOwnerValid(replay, active, replay_link) || !legacy_arrival)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	boundary_ms = replay->progress.elapsed_ms + SG_REPLAY_STEP_MS;
	if (replay->progress.status != SG_REPLAY_RUNNING ||
	    !replay->progress.step_pending ||
	    (boundary_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    live_elapsed_ms != boundary_ms)
		return SwimLiveFallback(replay, active, replay_link,
		    SG_SWIM_LIVE_FAILURE_CADENCE, SG_REPLAY_REASON_INVALID_STATE);

	result = SwimLiveResult(SG_SWIM_LIVE_RUNNING,
	                        SG_SWIM_LIVE_FAILURE_NONE,
	                        SG_REPLAY_REASON_NONE);
	/* This is the sole legacy arrival/contact call for a valid reducer frame,
	 * and the caller invokes this function only after the intervening mover
	 * pass.  No elapsed-zero or 25/50/75 contact trace exists. */
	result.legacy_arrived = legacy_arrival(&replay->spec, pose, context);
	result.arrival_sampled = true;
	observation = SwimLiveObservation(result.legacy_arrived);
	status = SG_SwimReplayPostStep(replay, pose, &observation);
	if (status == SG_REPLAY_ARRIVED)
	{
		SG_SwimLiveDeactivate(replay, active, replay_link);
		result.outcome = SG_SWIM_LIVE_ARRIVED;
		return result;
	}
	if (status != SG_REPLAY_RUNNING)
	{
		result.outcome = SG_SWIM_LIVE_FALLBACK;
		result.failure = SG_SWIM_LIVE_FAILURE_BOUNDARY;
		result.replay_reason = replay->progress.reason;
		SG_SwimLiveDeactivate(replay, active, replay_link);
		return result;
	}
	return result;
}

void SG_SwimLiveZeroFrame(usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS])
{
	int step;

	if (!commands)
		return;
	memset(commands, 0,
	       sizeof(*commands) * (size_t)SG_SWIM_LIVE_FRAME_STEPS);
	for (step = 0; step < SG_SWIM_LIVE_FRAME_STEPS; step++)
		commands[step].msec = SG_REPLAY_STEP_MS;
}
