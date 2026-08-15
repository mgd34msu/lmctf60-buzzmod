/* sg_hook_live.c -- see sg_hook_live.h for the live/replay boundary. */
#include "q_shared.h"
#include "slipgate/sg_hook_live.h"

static sg_hook_live_result_t HookLiveResult(sg_hook_live_outcome_t outcome,
	sg_hook_live_failure_t failure, sg_replay_reason_t replay_reason)
{
	sg_hook_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = replay_reason;
	return result;
}

static qboolean HookLiveOwnerValid(const sg_hook_replay_state_t *replay,
	const qboolean *active, const int *replay_link)
{
	return replay && active && replay_link;
}

static qboolean HookLiveCommandEqual(const usercmd_t *first,
	const usercmd_t *second)
{
	int axis;

	if (!first || !second || first->msec != second->msec ||
	    first->buttons != second->buttons ||
	    first->forwardmove != second->forwardmove ||
	    first->sidemove != second->sidemove ||
	    first->upmove != second->upmove || first->impulse != second->impulse ||
	    first->lightlevel != second->lightlevel)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (first->angles[axis] != second->angles[axis])
			return false;
	return true;
}

static void HookLiveCommandGuardStore(sg_hook_live_command_guard_t *guard,
	int action_link, const usercmd_t *command)
{
	SG_HookLiveCommandGuardClear(guard);
	if (!guard || !command)
		return;
	guard->expected = *command;
	guard->action_link = action_link;
	guard->pending = true;
}

static qboolean HookLiveCanonicalFailure(sg_replay_reason_t reason)
{
	switch (reason)
	{
	case SG_REPLAY_REASON_CONTAMINATED:
	case SG_REPLAY_REASON_DOOR_PASSED:
	case SG_REPLAY_REASON_HAZARDOUS_LIQUID:
	case SG_REPLAY_REASON_DAMAGING_FALL:
	case SG_REPLAY_REASON_TIMING_MISMATCH:
	case SG_REPLAY_REASON_HOOK_ATTACH_TIMING:
	case SG_REPLAY_REASON_HOOK_EVENT_ORDER:
	case SG_REPLAY_REASON_HOOK_RELEASE_BEFORE_PULL:
	case SG_REPLAY_REASON_HOOK_RELEASE_MISSED:
	case SG_REPLAY_REASON_HOOK_PULL_TIMEOUT:
	case SG_REPLAY_REASON_HOOK_SETTLE_TIMEOUT:
	case SG_REPLAY_REASON_HOOK_TERMINAL_LOST:
		return true;
	default:
		return false;
	}
}

static sg_hook_live_result_t HookLiveFallback(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, sg_hook_live_failure_t failure,
	sg_replay_reason_t replay_reason)
{
	SG_HookLiveDeactivate(replay, active, replay_link);
	return HookLiveResult(SG_HOOK_LIVE_FALLBACK, failure, replay_reason);
}

static sg_hook_live_result_t HookLiveReducerFailure(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	sg_hook_live_failure_t failure)
{
	sg_replay_reason_t reason = replay ? replay->progress.reason :
	                                      SG_REPLAY_REASON_INVALID_ARGUMENT;

	if (!HookLiveCanonicalFailure(reason))
		return HookLiveFallback(replay, active, replay_link, failure, reason);
	SG_HookLiveDeactivate(replay, active, replay_link);
	return HookLiveResult(SG_HOOK_LIVE_FAILED, failure, reason);
}

static sg_hook_live_result_t HookLiveValidateOwner(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current)
{
	if (!HookLiveOwnerValid(replay, active, replay_link))
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!*active || *replay_link != action_link)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_STATE);
	if (!identity_current)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_IDENTITY, SG_REPLAY_REASON_INVALID_STATE);
	return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

const char *SG_HookLiveFailureName(sg_hook_live_failure_t failure)
{
	switch (failure)
	{
	case SG_HOOK_LIVE_FAILURE_NONE: return "none";
	case SG_HOOK_LIVE_FAILURE_OWNER: return "owner";
	case SG_HOOK_LIVE_FAILURE_LINK: return "link";
	case SG_HOOK_LIVE_FAILURE_IDENTITY: return "identity";
	case SG_HOOK_LIVE_FAILURE_BEGIN: return "begin";
	case SG_HOOK_LIVE_FAILURE_REDUCER_CONTROL: return "reducer-control";
	case SG_HOOK_LIVE_FAILURE_LEGACY_CONTROL: return "legacy-control";
	case SG_HOOK_LIVE_FAILURE_COMMAND_DIFFERENTIAL:
		return "command-differential";
	case SG_HOOK_LIVE_FAILURE_POSTSTEP: return "poststep";
	case SG_HOOK_LIVE_FAILURE_ATTACH: return "attach";
	case SG_HOOK_LIVE_FAILURE_PULL: return "pull";
	case SG_HOOK_LIVE_FAILURE_RELEASE: return "release";
	case SG_HOOK_LIVE_FAILURE_FINAL_COMMAND: return "final-command";
	default: return "unknown";
	}
}

void SG_HookLiveReset(sg_hook_replay_state_t *replay, qboolean *active,
	int *replay_link, sg_hook_live_command_guard_t *guard)
{
	if (replay)
		memset(replay, 0, sizeof(*replay));
	if (active)
		*active = false;
	if (replay_link)
		*replay_link = -1;
	SG_HookLiveCommandGuardClear(guard);
}

void SG_HookLiveDeactivate(sg_hook_replay_state_t *replay, qboolean *active,
	int *replay_link)
{
	(void)replay; /* Preserve terminal reducer evidence for the host logger. */
	if (active)
		*active = false;
	if (replay_link)
		*replay_link = -1;
}

void SG_HookLiveCommandGuardClear(sg_hook_live_command_guard_t *guard)
{
	if (!guard)
		return;
	memset(guard, 0, sizeof(*guard));
	guard->action_link = -1;
}

void SG_HookLiveZeroCommand(usercmd_t *command)
{
	if (command)
	{
		memset(command, 0, sizeof(*command));
		command->msec = SG_REPLAY_STEP_MS;
	}
}

sg_hook_live_result_t SG_HookLiveBegin(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation,
	float old_frame_z, sg_hook_live_command_guard_t *guard)
{
	sg_replay_status_t status;

	SG_HookLiveCommandGuardClear(guard);
	if (!guard || !HookLiveOwnerValid(replay, active, replay_link))
		return HookLiveResult(SG_HOOK_LIVE_FALLBACK, SG_HOOK_LIVE_FAILURE_OWNER,
		                      SG_REPLAY_REASON_INVALID_ARGUMENT);
	SG_HookLiveDeactivate(replay, active, replay_link);
	if (action_link < 0)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_LINK, SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (!identity_current)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_IDENTITY, SG_REPLAY_REASON_INVALID_ARGUMENT);
	status = SG_HookReplayBegin(replay, spec, pose, observation, old_frame_z);
	if (status != SG_REPLAY_RUNNING)
		return HookLiveReducerFailure(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_BEGIN);
	*active = true;
	*replay_link = action_link;
	return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_hook_live_result_t SG_HookLivePreStep(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation,
	sg_hook_live_command_fn legacy_command, usercmd_t *command,
	sg_hook_live_command_guard_t *guard)
{
	sg_hook_live_result_t owner;
	sg_replay_status_t status;
	usercmd_t reducer_command, legacy;
	qboolean legacy_ok;
	byte legacy_msec;

	SG_HookLiveCommandGuardClear(guard);
	if (!guard || !command || !legacy_command)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	legacy_msec = command->msec;
	memset(&legacy, 0, sizeof(legacy));
	legacy.msec = legacy_msec;
	owner = HookLiveValidateOwner(replay, active, replay_link, action_link,
	                              identity_current);
	if (owner.outcome != SG_HOOK_LIVE_RUNNING)
	{
		(void)legacy_command(replay, pose, observation, &legacy);
		*command = legacy;
		return owner;
	}
	/* Build the independent legacy command before the reducer can mutate any
	 * phase or arrival latch.  In particular, settlement zero-fill must not
	 * learn its condition from state->arrived_in_frame after PreStep. */
	legacy_ok = legacy_command(replay, pose, observation, &legacy);
	memset(&reducer_command, 0, sizeof(reducer_command));
	reducer_command.msec = SG_REPLAY_STEP_MS;
	status = SG_HookReplayPreStep(replay, pose, observation, &reducer_command);
	*command = legacy;
	if (status != SG_REPLAY_RUNNING)
		return HookLiveReducerFailure(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_REDUCER_CONTROL);
	if (!legacy_ok)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_LEGACY_CONTROL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	if (!HookLiveCommandEqual(&reducer_command, &legacy))
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	HookLiveCommandGuardStore(guard, action_link, &reducer_command);
	*command = reducer_command;
	return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_hook_live_result_t SG_HookLiveWaitAttachStep(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation,
	sg_hook_live_command_fn legacy_command, usercmd_t *command,
	sg_hook_live_command_guard_t *guard)
{
	sg_hook_live_result_t owner;
	usercmd_t shadow, legacy;
	qboolean legacy_ok;
	byte legacy_msec;

	SG_HookLiveCommandGuardClear(guard);
	if (!guard || !command || !legacy_command)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_OWNER, SG_REPLAY_REASON_INVALID_ARGUMENT);
	legacy_msec = command->msec;
	memset(&legacy, 0, sizeof(legacy));
	legacy.msec = legacy_msec;
	owner = HookLiveValidateOwner(replay, active, replay_link, action_link,
	                              identity_current);
	if (owner.outcome != SG_HOOK_LIVE_RUNNING)
	{
		(void)legacy_command(replay, pose, observation, &legacy);
		*command = legacy;
		return owner;
	}
	/* WAIT_ATTACH is an event barrier, not an assertion that the engine bolt
	 * has already attached.  Keep the live reducer exactly parked there while
	 * comparing the legacy writer with the frozen, state-free view renderer. */
	legacy_ok = legacy_command(replay, pose, observation, &legacy);
	*command = legacy;
	if (replay->progress.status != SG_REPLAY_RUNNING ||
	    replay->progress.step_pending ||
	    replay->phase != SG_HOOK_REPLAY_WAIT_ATTACH)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_ATTACH, SG_REPLAY_REASON_INVALID_STATE);
	memset(&shadow, 0, sizeof(shadow));
	shadow.msec = SG_REPLAY_STEP_MS;
	if (!SG_HookReplayFixedViewCommand(pose, replay->spec.view_angles,
	                                   &shadow))
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_REDUCER_CONTROL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	if (!legacy_ok)
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_LEGACY_CONTROL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	if (!HookLiveCommandEqual(&shadow, &legacy))
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	HookLiveCommandGuardStore(guard, action_link, &shadow);
	*command = shadow;
	return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_hook_live_result_t SG_HookLiveValidateFinalCommand(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current, const usercmd_t *expected,
	const usercmd_t *command)
{
	sg_hook_live_result_t owner = HookLiveValidateOwner(replay, active,
	    replay_link, action_link, identity_current);

	if (owner.outcome != SG_HOOK_LIVE_RUNNING)
		return owner;
	if (!expected || !command || !HookLiveCommandEqual(expected, command))
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_FINAL_COMMAND,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_hook_live_result_t SG_HookLiveValidateStoredFinalCommand(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current,
	sg_hook_live_command_guard_t *guard, const usercmd_t *command)
{
	sg_hook_live_result_t result;

	if (!guard || !guard->pending || guard->action_link != action_link)
	{
		SG_HookLiveCommandGuardClear(guard);
		return HookLiveFallback(replay, active, replay_link,
		    SG_HOOK_LIVE_FAILURE_FINAL_COMMAND,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	}
	result = SG_HookLiveValidateFinalCommand(replay, active, replay_link,
	    action_link, identity_current, &guard->expected, command);
	/* Every ClientThink requires a new approval, including after a fallback. */
	SG_HookLiveCommandGuardClear(guard);
	return result;
}

sg_hook_live_result_t SG_HookLivePostStep(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_hook_live_result_t owner = HookLiveValidateOwner(replay, active,
	    replay_link, action_link, identity_current);
	sg_replay_status_t status;

	if (owner.outcome != SG_HOOK_LIVE_RUNNING)
		return owner;
	status = SG_HookReplayPostStep(replay, pose, observation);
	if (status == SG_REPLAY_RUNNING)
		return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
		                      SG_REPLAY_REASON_NONE);
	if (status == SG_REPLAY_ARRIVED)
	{
		SG_HookLiveDeactivate(replay, active, replay_link);
		return HookLiveResult(SG_HOOK_LIVE_ARRIVED, SG_HOOK_LIVE_FAILURE_NONE,
		                      SG_REPLAY_REASON_NONE);
	}
	return HookLiveReducerFailure(replay, active, replay_link,
	    SG_HOOK_LIVE_FAILURE_POSTSTEP);
}

static sg_hook_live_result_t HookLiveEvent(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose,
	sg_hook_live_failure_t failure, int event)
{
	sg_hook_live_result_t owner = HookLiveValidateOwner(replay, active,
	    replay_link, action_link, identity_current);
	sg_replay_status_t status;

	if (owner.outcome != SG_HOOK_LIVE_RUNNING)
		return owner;
	switch (event)
	{
	case 0: status = SG_HookReplayAttached(replay, pose); break;
	case 1: status = SG_HookReplayPullApplied(replay, pose); break;
	default: status = SG_HookReplayReleaseApplied(replay, pose); break;
	}
	if (status != SG_REPLAY_RUNNING)
		return HookLiveReducerFailure(replay, active, replay_link, failure);
	return HookLiveResult(SG_HOOK_LIVE_RUNNING, SG_HOOK_LIVE_FAILURE_NONE,
	                      SG_REPLAY_REASON_NONE);
}

sg_hook_live_result_t SG_HookLiveAttached(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose)
{
	return HookLiveEvent(replay, active, replay_link, action_link,
	                     identity_current, pose, SG_HOOK_LIVE_FAILURE_ATTACH, 0);
}

sg_hook_live_result_t SG_HookLivePullApplied(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose)
{
	return HookLiveEvent(replay, active, replay_link, action_link,
	                     identity_current, pose, SG_HOOK_LIVE_FAILURE_PULL, 1);
}

sg_hook_live_result_t SG_HookLiveReleaseApplied(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose)
{
	return HookLiveEvent(replay, active, replay_link, action_link,
	                     identity_current, pose, SG_HOOK_LIVE_FAILURE_RELEASE, 2);
}
