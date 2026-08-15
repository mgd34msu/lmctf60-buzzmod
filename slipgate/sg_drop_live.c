/* sg_drop_live.c -- see sg_drop_live.h for the live/replay boundary. */
#include "q_shared.h"
#include "slipgate/sg_drop_live.h"

static sg_drop_live_result_t DropLiveResult(sg_drop_live_outcome_t outcome,
	sg_drop_live_failure_t failure, sg_replay_reason_t replay_reason)
{
	sg_drop_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = replay_reason;
	return result;
}

qboolean SG_DropLiveEventsLatch(sg_drop_live_events_t *events,
	qboolean contaminated, qboolean door_passed)
{
	if (!events || (contaminated != false && contaminated != true) ||
	    (door_passed != false && door_passed != true))
		return false;
	if (contaminated)
		events->contaminated = true;
	if (door_passed)
		events->door_passed = true;
	return true;
}

/* Command admission owns the clear-before-observe ordering.  A source door
 * snapshot is deliberately deferred past Begin, then installed only after
 * stale prior-command events are cleared for the first real 25 ms step. */
qboolean SG_DropLiveEventsBeginCommand(sg_drop_live_events_t *events,
	qboolean *source_door_pending)
{
	qboolean pending;

	if (!events || !source_door_pending ||
	    (*source_door_pending != false && *source_door_pending != true))
		return false;
	pending = *source_door_pending;
	memset(events, 0, sizeof(*events));
	*source_door_pending = false;
	return SG_DropLiveEventsLatch(events, false, pending);
}

static qboolean DropLiveOwnerValid(const sg_drop_replay_state_t *replay,
	const qboolean *active, const int *replay_link)
{
	return replay && active && replay_link;
}

static qboolean DropLiveCommandEqual(const usercmd_t *first,
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

static qboolean DropLiveCanonicalFailure(sg_replay_reason_t reason)
{
	switch (reason)
	{
	case SG_REPLAY_REASON_CONTAMINATED:
	case SG_REPLAY_REASON_DOOR_PASSED:
	case SG_REPLAY_REASON_HAZARDOUS_LIQUID:
	case SG_REPLAY_REASON_DAMAGING_FALL:
	case SG_REPLAY_REASON_APPROACH_TIMEOUT:
	case SG_REPLAY_REASON_TRAVEL_TIMEOUT:
	case SG_REPLAY_REASON_ACTION_TIMEOUT:
	case SG_REPLAY_REASON_BELOW_DESTINATION:
	case SG_REPLAY_REASON_SHALLOW_WATER_CONTACT:
	case SG_REPLAY_REASON_SHORT_LANDING:
	case SG_REPLAY_REASON_RECOVERY_LOST:
	case SG_REPLAY_REASON_ZERO_TIME_ARRIVAL:
	case SG_REPLAY_REASON_TIMING_MISMATCH:
		return true;
	default:
		return false;
	}
}

static qboolean DropLiveObservation(qboolean ground_support_valid,
	const sg_drop_live_events_t *events,
	sg_replay_observation_t *observation)
{
	if (!events || !observation ||
	    (events->contaminated != false && events->contaminated != true) ||
	    (events->door_passed != false && events->door_passed != true))
		return false;
	memset(observation, 0, sizeof(*observation));
	observation->ground_support_valid = ground_support_valid;
	observation->contaminated = events->contaminated;
	observation->door_passed = events->door_passed;
	return true;
}

static sg_drop_live_result_t DropLiveFallback(
	sg_drop_replay_state_t *replay, qboolean *active, int *replay_link,
	sg_drop_live_failure_t failure, sg_replay_reason_t replay_reason)
{
	SG_DropLiveDeactivate(replay, active, replay_link);
	return DropLiveResult(SG_DROP_LIVE_FALLBACK, failure, replay_reason);
}

static sg_drop_live_result_t DropLiveReducerFailure(
	sg_drop_replay_state_t *replay, qboolean *active, int *replay_link,
	sg_drop_live_failure_t failure)
{
	sg_replay_reason_t reason = replay ? replay->progress.reason :
	                                      SG_REPLAY_REASON_INVALID_ARGUMENT;

	if (!DropLiveCanonicalFailure(reason))
		return DropLiveFallback(replay, active, replay_link, failure, reason);
	SG_DropLiveDeactivate(replay, active, replay_link);
	return DropLiveResult(SG_DROP_LIVE_FAILED, failure, reason);
}

const char *SG_DropLiveFailureName(sg_drop_live_failure_t failure)
{
	switch (failure)
	{
	case SG_DROP_LIVE_FAILURE_NONE: return "none";
	case SG_DROP_LIVE_FAILURE_OWNER: return "owner";
	case SG_DROP_LIVE_FAILURE_LINK: return "link";
	case SG_DROP_LIVE_FAILURE_CADENCE: return "cadence";
	case SG_DROP_LIVE_FAILURE_BEGIN: return "begin";
	case SG_DROP_LIVE_FAILURE_REDUCER_CONTROL: return "reducer-control";
	case SG_DROP_LIVE_FAILURE_SHADOW_CONTROL: return "shadow-control";
	case SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL:
		return "command-differential";
	case SG_DROP_LIVE_FAILURE_POSTSTEP: return "poststep";
	case SG_DROP_LIVE_FAILURE_BOUNDARY: return "boundary";
	default: return "unknown";
	}
}

void SG_DropLiveReset(sg_drop_replay_state_t *replay, qboolean *active,
	int *replay_link, sg_drop_live_events_t *events)
{
	if (replay)
		memset(replay, 0, sizeof(*replay));
	if (active)
		*active = false;
	if (replay_link)
		*replay_link = -1;
	if (events)
		memset(events, 0, sizeof(*events));
}

void SG_DropLiveDeactivate(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link)
{
	(void)replay; /* Retain failed progress for stable debug evidence. */
	if (active)
		*active = false;
	if (replay_link)
		*replay_link = -1;
}

void SG_DropLivePose(sg_replay_pose_t *pose, const pmove_state_t *pms,
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

sg_drop_live_result_t SG_DropLiveBegin(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const vec3_t destination, const vec3_t lip, byte heading,
	qboolean destination_water, int expected_arrival_ms,
	const sg_replay_pose_t *pose, qboolean ground_support_valid,
	float old_frame_z, const sg_drop_live_events_t *events)
{
	sg_drop_replay_spec_t spec;
	sg_replay_observation_t observation;
	sg_replay_status_t status;

	if (!DropLiveOwnerValid(replay, active, replay_link))
		return DropLiveResult(SG_DROP_LIVE_FALLBACK,
		    SG_DROP_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	SG_DropLiveDeactivate(replay, active, replay_link);
	if (action_link < 0)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_ARGUMENT);
	memset(&spec, 0, sizeof(spec));
	if (destination)
		VectorCopy(destination, spec.destination);
	if (lip)
		VectorCopy(lip, spec.lip);
	spec.heading = heading;
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = expected_arrival_ms;
	if (!DropLiveObservation(ground_support_valid, events, &observation))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_BEGIN, SG_REPLAY_REASON_INVALID_ARGUMENT);
	status = SG_DropReplayBegin(replay, &spec, pose, &observation,
	                            old_frame_z);
	if (status != SG_REPLAY_RUNNING)
		return DropLiveReducerFailure(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_BEGIN);
	*active = true;
	*replay_link = action_link;
	return DropLiveResult(SG_DROP_LIVE_RUNNING, SG_DROP_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_drop_live_result_t SG_DropLivePreStep(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, sg_drop_live_command_fn shadow_command,
	usercmd_t *command)
{
	usercmd_t reducer_command, shadow;
	sg_replay_status_t status;
	qboolean shadow_ok;
	byte shadow_msec;

	if (!command || !shadow_command)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	shadow_msec = command->msec;
	memset(&shadow, 0, sizeof(shadow));
	shadow.msec = shadow_msec;
	if (!DropLiveOwnerValid(replay, active, replay_link))
	{
		*command = shadow;
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	}
	if (!*active || *replay_link != action_link)
	{
		shadow_ok = shadow_command(replay, pose, &shadow);
		*command = shadow;
		(void)shadow_ok;
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	}
	memset(&reducer_command, 0, sizeof(reducer_command));
	reducer_command.msec = SG_REPLAY_STEP_MS;
	status = SG_DropReplayPreStep(replay, pose, &reducer_command);
	shadow_ok = shadow_command(replay, pose, &shadow);
	*command = shadow;
	if (status != SG_REPLAY_RUNNING)
		return DropLiveReducerFailure(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_REDUCER_CONTROL);
	if (!shadow_ok)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_SHADOW_CONTROL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	if (!DropLiveCommandEqual(&reducer_command, &shadow))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	*command = reducer_command;
	return DropLiveResult(SG_DROP_LIVE_RUNNING, SG_DROP_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_drop_live_result_t SG_DropLiveValidateFinalCommand(
	sg_drop_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, const usercmd_t *expected, const usercmd_t *command)
{
	if (!expected || !command ||
	    !DropLiveOwnerValid(replay, active, replay_link))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	if (!DropLiveCommandEqual(expected, command))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	return DropLiveResult(SG_DROP_LIVE_RUNNING, SG_DROP_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_drop_live_result_t SG_DropLivePostStep(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, qboolean ground_support_valid,
	const sg_drop_live_events_t *events)
{
	sg_replay_observation_t observation;
	sg_replay_status_t status;

	if (!DropLiveOwnerValid(replay, active, replay_link))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	if (!replay->progress.step_pending ||
	    ((replay->progress.elapsed_ms + SG_REPLAY_STEP_MS) %
	     SG_REPLAY_FRAME_MS) == 0)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_CADENCE, SG_REPLAY_REASON_INVALID_STATE);
	if (!DropLiveObservation(ground_support_valid, events, &observation))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_POSTSTEP,
		    SG_REPLAY_REASON_INVALID_ARGUMENT);
	status = SG_DropReplayPostStep(replay, pose, &observation);
	if (status != SG_REPLAY_RUNNING)
		return DropLiveReducerFailure(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_POSTSTEP);
	return DropLiveResult(SG_DROP_LIVE_RUNNING, SG_DROP_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_drop_live_result_t SG_DropLiveBoundary(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, qboolean ground_support_valid,
	const sg_drop_live_events_t *events,
	sg_drop_live_contact_fn arrival, sg_drop_live_contact_fn recovery,
	void *context)
{
	sg_drop_live_result_t result;
	sg_replay_observation_t observation, probe;
	sg_replay_status_t status;
	qboolean airborne_after, was_recovery, sample_recovery;
	int boundary_ms;

	if (!DropLiveOwnerValid(replay, active, replay_link) || !arrival ||
	    !recovery)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	boundary_ms = replay->progress.elapsed_ms + SG_REPLAY_STEP_MS;
	if (replay->progress.status != SG_REPLAY_RUNNING ||
	    !replay->progress.step_pending ||
	    (boundary_ms % SG_REPLAY_FRAME_MS) != 0)
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_CADENCE, SG_REPLAY_REASON_INVALID_STATE);

	result = DropLiveResult(SG_DROP_LIVE_RUNNING, SG_DROP_LIVE_FAILURE_NONE,
	                        SG_REPLAY_REASON_NONE);
	if (!DropLiveObservation(ground_support_valid, events, &observation))
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_BOUNDARY,
		    SG_REPLAY_REASON_INVALID_ARGUMENT);
	/* Event rejection precedes terminal geometry in the reducer.  Preserve that
	 * order in the live adapter as well: a contaminated/door boundary performs
	 * no arrival or recovery trace before it fails. */
	if (observation.contaminated || observation.door_passed)
	{
		status = SG_DropReplayPostStep(replay, pose, &observation);
		if (status != SG_REPLAY_RUNNING)
			return DropLiveReducerFailure(replay, active, replay_link,
			    SG_DROP_LIVE_FAILURE_BOUNDARY);
		return DropLiveFallback(replay, active, replay_link,
		    SG_DROP_LIVE_FAILURE_BOUNDARY, SG_REPLAY_REASON_INVALID_STATE);
	}
	observation.drop_recovery_admitted = !replay->spec.destination_water;
	observation.drop_landing_observed = pose &&
	    (pose->grounded || pose->waterlevel >= 2);
	airborne_after = replay->airborne ||
	    (replay->walkoff && pose && !pose->grounded);
	/* Preserve legacy short-circuit order.  Each callback includes its own
	 * geometry/support gate.  A supported handoff is not a landing: the first
	 * terminal trace is owned only after an airborne pose has been observed. */
	if (replay->walkoff && airborne_after)
	{
		result.arrived = arrival(&replay->spec, pose, context);
		result.arrival_sampled = true;
	}
	observation.contact_clear = result.arrived;
	observation.drop_arrival_contact_clear = result.arrived;

	was_recovery = replay->recovery;
	probe = observation;
	probe.drop_recovery_contact_clear = true;
	sample_recovery = !result.arrived && replay->walkoff &&
	    airborne_after && !replay->spec.destination_water &&
	    (was_recovery ||
	     SG_DropReplayRecoveryReady(&replay->spec, pose, &probe));
	if (sample_recovery)
	{
		result.recovery_ready = recovery(&replay->spec, pose, context);
		result.recovery_sampled = true;
		observation.drop_recovery_contact_clear = result.recovery_ready;
	}
	status = SG_DropReplayPostStep(replay, pose, &observation);
	result.recovery_started = !was_recovery && replay->recovery;
	if (status == SG_REPLAY_ARRIVED)
	{
		SG_DropLiveDeactivate(replay, active, replay_link);
		result.outcome = SG_DROP_LIVE_ARRIVED;
		return result;
	}
	if (status != SG_REPLAY_RUNNING)
	{
		sg_drop_live_result_t failure = DropLiveReducerFailure(replay, active,
		    replay_link, SG_DROP_LIVE_FAILURE_BOUNDARY);

		failure.arrival_sampled = result.arrival_sampled;
		failure.arrived = result.arrived;
		failure.recovery_sampled = result.recovery_sampled;
		failure.recovery_ready = result.recovery_ready;
		failure.recovery_started = result.recovery_started;
		return failure;
	}
	return result;
}

void SG_DropLiveZeroCommand(usercmd_t *command)
{
	if (!command)
		return;
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
}
