#include "slipgate/sg_compound_hook_live_internal.h"

#include <limits.h>
#include <string.h>

sg_compound_hook_live_result_t CompoundHookLiveResult(
	sg_compound_hook_live_outcome_t outcome,
	sg_compound_hook_live_failure_t failure,
	sg_replay_reason_t reason, qboolean command_ready)
{
	sg_compound_hook_live_result_t result;
	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = reason;
	result.command_ready = command_ready;
	return result;
}

qboolean CompoundHookLiveHostValid(const sg_compound_hook_live_host_t *host)
{
	return host && host->bind && host->acquire && host->authorize &&
	       host->hold_open && host->body_clear && host->bolt_clear &&
	       host->release && host->orphan && host->abort_bolt &&
	       host->source_checkpoint &&
	       host->suffix_checkpoint && host->event_authorize &&
	       host->sweep_segment && host->hook_shadow;
}
sg_compound_hook_live_host_result_t CompoundHookLiveCurrent(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	sg_compound_hook_live_snapshot_t current;
	sg_compound_hook_live_host_result_t result;
	if (!state || !CompoundHookLiveHostValid(host))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	memset(&current, 0, sizeof(current));
	result = host->bind(host->context, state->snapshot.binding.link_index,
	                    &current);
	if (result != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return result;
	return memcmp(&current, &state->snapshot, sizeof(current)) == 0 ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
}
sg_compound_hook_live_host_result_t CompoundHookLiveAuthorized(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	if (CompoundHookLiveCurrent(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	return host->authorize(host->context, &state->snapshot);
}
sg_compound_hook_live_result_t CompoundHookLiveActive(
	const sg_compound_hook_live_state_t *state, qboolean command_ready)
{
	return CompoundHookLiveResult(state && state->recovering ?
	              SG_COMPOUND_HOOK_LIVE_RECOVERING :
	              SG_COMPOUND_HOOK_LIVE_RUNNING,
	              SG_COMPOUND_HOOK_LIVE_FAILURE_NONE,
	              SG_REPLAY_REASON_NONE, command_ready);
}

static qboolean CompoundHookLiveSweepBool(qboolean value)
{
	return value == false || value == true;
}

qboolean CompoundHookLiveObserveSweep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose, int next_ms)
{
	sg_compound_hook_live_sweep_t sweep;
	qboolean safe;

	if (!state || !host || !pose || !state->command_origin_valid ||
	    next_ms != state->transaction_elapsed_ms + SG_REPLAY_STEP_MS)
		return false;
	memset(&sweep, 0, sizeof(sweep));
	if (host->sweep_segment(host->context, &state->snapshot,
	        state->command_origin, pose->origin, &sweep) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ||
	    !CompoundHookLiveSweepBool(sweep.start_outside) ||
	    !CompoundHookLiveSweepBool(sweep.end_outside) ||
	    !CompoundHookLiveSweepBool(sweep.crossed))
		return false;
	safe = sweep.start_outside && sweep.end_outside && !sweep.crossed;
	state->command_segment_checked = true;
	if (!safe)
	{
		qboolean suffix_egress =
			state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX &&
			(state->outer.phase == SG_COMPOUND_SUFFIX_LEASED ||
			 state->outer.phase == SG_COMPOUND_SUFFIX_CLEAR) &&
			!state->sweep_clear;
		qboolean recovery_egress = state->recovering &&
			!state->sweep_clear;

		state->sweep_outside_since_ms = -1;
		state->segment_clear_ready = false;
		state->recovery_sweep_dirty = true;
		return suffix_egress || recovery_egress;
	}
	if (state->sweep_outside_since_ms < 0)
		state->sweep_outside_since_ms = state->transaction_elapsed_ms;
	if (next_ms - state->sweep_outside_since_ms >= SG_REPLAY_FRAME_MS)
		state->segment_clear_ready = true;
	return true;
}
sg_compound_hook_live_result_t CompoundHookLiveOwnedFailure(
	sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_failure_t failure, sg_replay_reason_t reason)
{
	if (!state || !state->guard_owned || !state->local_owned ||
	    state->outer.phase == SG_COMPOUND_NONE)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    failure, reason, false);
	if (state->outer.phase != SG_COMPOUND_RECOVER)
		(void)SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ABORT);
	state->failure = failure;
	state->replay_reason = reason;
	state->recovering = true;
	state->aborted_command_pending = state->command_pending;
	if (!state->command_pending)
	{
		state->control = state->transaction_elapsed_ms %
		        SG_REPLAY_FRAME_MS ?
		    SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING :
		    SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
		state->command_approved = false;
		state->command_replay_consumed = false;
		state->swim_active = false;
		state->swim_link = -1;
		SG_HookLiveDeactivate(&state->hook, &state->hook_active,
		                      &state->hook_link);
		SG_HookLiveCommandGuardClear(&state->hook_command_guard);
	}
	state->sweep_clear = false;
	state->arrived = false;
	return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING, failure, reason, false);
}
qboolean CompoundHookLiveClear(const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	const sg_compound_hook_live_bolt_t *bolt =
		state->bolt_linked ? &state->bolt : NULL;
	sg_compound_hook_live_host_result_t body =
		host->body_clear(host->context, &state->snapshot, bolt);
	sg_compound_hook_live_host_result_t hook =
		host->bolt_clear(host->context, &state->snapshot, bolt);

	return (!state->recovery_sweep_dirty || state->segment_clear_ready) &&
	       body == SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED &&
	       hook == SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}
void CompoundHookLiveClearLocal(sg_compound_hook_live_state_t *state)
{
	if (!state)
		return;
	state->guard_owned = false;
	state->local_owned = false;
	state->swim_active = false;
	state->hook_active = false;
	state->swim_link = -1;
	state->hook_link = -1;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
	state->failure = SG_COMPOUND_HOOK_LIVE_FAILURE_NONE;
	state->replay_reason = SG_REPLAY_REASON_NONE;
	state->command_pending = false;
	state->command_approved = false;
	state->command_replay_consumed = false;
	state->aborted_command_pending = false;
	state->recovering = false;
	state->sweep_clear = false;
	state->arrived = false;
	state->command_origin_valid = false;
	state->command_segment_checked = false;
	state->segment_clear_ready = false;
	state->recovery_sweep_dirty = false;
	state->sweep_outside_since_ms = 0;
	state->bolt_linked = false;
	state->bolt_abort_applied = false;
	state->hook_released = false;
	SG_HookLiveCommandGuardClear(&state->hook_command_guard);
}
sg_compound_hook_live_result_t CompoundHookLiveFinishRelease(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, qboolean recovered)
{
	if (!CompoundHookLiveClear(state, host))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP, SG_REPLAY_REASON_NONE,
		    false);
	if (recovered && state->outer.phase == SG_COMPOUND_RECOVER &&
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RECOVERED))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (!SG_CompoundReleaseReady(&state->outer))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->release(host->context, &state->snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE, SG_REPLAY_REASON_NONE,
		    false);
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RELEASED))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	CompoundHookLiveClearLocal(state);
	return CompoundHookLiveResult(recovered ? SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED :
	                          SG_COMPOUND_HOOK_LIVE_COMPLETE,
	              SG_COMPOUND_HOOK_LIVE_FAILURE_NONE,
	              SG_REPLAY_REASON_NONE, false);
}

sg_compound_hook_live_result_t SG_CompoundHookLivePreStep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	sg_hook_live_result_t hook_result;
	sg_replay_status_t status;
	if (!state || !pose || !observation || !command ||
	    !state->guard_owned || !state->local_owned || !CompoundHookLiveHostValid(host))
		return state && state->guard_owned ?
		       CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (state->command_pending ||
	    state->transaction_elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if ((!state->recovering && state->transaction_elapsed_ms >
	         state->snapshot.binding.total_cost_ms - SG_REPLAY_STEP_MS) ||
	    state->transaction_elapsed_ms > RUNE_MAX_COST_MS - SG_REPLAY_STEP_MS)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_ACTION_TIMEOUT);
	if (state->control != SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING &&
	    CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	switch (state->control)
	{
	case SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH:
	case SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY:
		status = SG_SwimReplayPreStep(&state->swim, pose, command);
		if (status != SG_REPLAY_RUNNING)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    state->swim.progress.reason);
		break;
	case SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING:
		if (!SG_HookReplayFixedViewCommand(pose,
		        state->hook_spec.view_angles, command))
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_CONTROL);
		break;
	case SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING:
		SG_HookLiveZeroCommand(command);
		break;
	case SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX:
		if (state->hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH &&
		    state->transaction_elapsed_ms -
		        state->snapshot.binding.mover_top_ms -
		        state->hook.progress.elapsed_ms >=
		    SG_REPLAY_HOOK_FLIGHT_MAX_MS - state->hook.flight_body_ms)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
		hook_result = (state->hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH ?
		    SG_HookLiveWaitAttachStep : SG_HookLivePreStep)(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true, pose, observation,
		    host->hook_shadow, command, &state->hook_command_guard);
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    hook_result.replay_reason);
		break;
	default:
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	}
	state->expected_command = *command;
	VectorCopy(pose->origin, state->command_origin);
	state->command_origin_valid = true;
	state->command_segment_checked = false;
	state->command_pending = true;
	state->command_approved = false;
	state->command_replay_consumed = false;
	return CompoundHookLiveActive(state, true);
}

static qboolean CommandEqual(const usercmd_t *first,
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

static void DiscardUnconsumedCommand(sg_compound_hook_live_state_t *state)
{
	memset(&state->expected_command, 0, sizeof(state->expected_command));
	state->command_pending = false;
	state->command_approved = false;
	state->command_replay_consumed = false;
	state->aborted_command_pending = false;
	SG_HookLiveCommandGuardClear(&state->hook_command_guard);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveApproveCommand(
	sg_compound_hook_live_state_t *state, const usercmd_t *command)
{
	sg_hook_live_result_t hook_result;

	if (!state || !state->guard_owned || !state->local_owned ||
	    !state->command_pending || state->command_approved)
		return state && state->guard_owned ?
		       CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE) :
		       CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (!command)
	{
		DiscardUnconsumedCommand(state);
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	}
	if (state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX)
	{
		hook_result = SG_HookLiveValidateStoredFinalCommand(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true,
		    &state->hook_command_guard, command);
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
		{
			DiscardUnconsumedCommand(state);
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    hook_result.replay_reason);
		}
	}
	else if (!CommandEqual(&state->expected_command, command))
	{
		DiscardUnconsumedCommand(state);
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_INVALID_CONTROL);
	}
	state->command_approved = true;
	return CompoundHookLiveActive(state, false);
}


static sg_compound_hook_live_result_t SweepBoundary(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	sg_hook_live_outcome_t hook_outcome)
{
	int elapsed = state->hook.progress.elapsed_ms;
	qboolean body_clear =
		host->body_clear(host->context, &state->snapshot,
		                 state->bolt_linked ? &state->bolt : NULL) ==
		SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
	if (!state->sweep_clear && state->segment_clear_ready &&
	    elapsed < state->snapshot.binding.sweep_clear_ms)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (!state->sweep_clear &&
	    elapsed > state->snapshot.binding.sweep_clear_ms)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (!state->sweep_clear &&
	    elapsed == state->snapshot.binding.sweep_clear_ms)
	{
		if (!body_clear || !state->segment_clear_ready)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
		state->sweep_clear = true;
		if (!SG_CompoundAdvance(&state->outer,
		                        SG_COMPOUND_EVENT_SWEEP_CLEAR))
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (hook_outcome == SG_HOOK_LIVE_ARRIVED)
	{
		state->arrived = true;
		if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ARRIVED))
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (SG_CompoundReleaseReady(&state->outer))
	{
		if (!state->hook_released || !CompoundHookLiveClear(state, host))
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
		return CompoundHookLiveFinishRelease(state, host, false);
	}
	if (SG_CompoundSuffixNeedsHold(elapsed,
	        state->snapshot.binding.sweep_clear_ms) &&
	    host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
		                    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundHookLiveActive(state, false);
}

static sg_compound_hook_live_result_t ConsumeStep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, qboolean boundary)
{
	sg_compound_hook_live_control_t control;
	sg_hook_live_result_t hook_result;
	sg_replay_status_t swim_status = SG_REPLAY_RUNNING;
	sg_replay_status_t opening_status = SG_REPLAY_RUNNING;
	int next_ms;
	qboolean waiting_attach;
	qboolean replay_consumed;
	qboolean aborted;
	qboolean sweep_ok;
	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->local_owned || !CompoundHookLiveHostValid(host))
		return state && state->guard_owned ?
		       CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (!state->command_pending || !state->command_approved ||
	    state->transaction_elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	next_ms = state->transaction_elapsed_ms + SG_REPLAY_STEP_MS;
	if (((next_ms % SG_REPLAY_FRAME_MS) == 0) != boundary ||
	    (boundary && next_ms == state->last_boundary_ms))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	aborted = state->aborted_command_pending;
	if (!aborted && state->control != SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING &&
	    CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	sweep_ok = state->command_segment_checked ||
	           CompoundHookLiveObserveSweep(state, host, pose, next_ms);
	control = state->control;
	replay_consumed = state->command_replay_consumed;
	waiting_attach = control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX &&
	                 state->hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH;
	memset(&hook_result, 0, sizeof(hook_result));
	hook_result.outcome = SG_HOOK_LIVE_RUNNING;
	if (!replay_consumed &&
	    (control == SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH ||
	     control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY))
		swim_status = SG_SwimReplayPostStep(&state->swim, pose, observation);
	else if (!replay_consumed &&
	         control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX)
	{
		if (waiting_attach)
			hook_result = SG_HookLiveWaitAttachPostStep(&state->hook,
			    &state->hook_active, &state->hook_link,
			    (int)state->snapshot.binding.link_index, true, pose,
			    observation, boundary);
		else
			hook_result = SG_HookLivePostStep(&state->hook,
			    &state->hook_active, &state->hook_link,
			    (int)state->snapshot.binding.link_index, true, pose,
			    observation);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING)
		opening_status = SG_ReplayFrameSafetyPostStep(
		    &state->opening_safety, pose, observation, boundary);
	state->command_pending = false;
	state->command_approved = false;
	state->command_replay_consumed = false;
	state->aborted_command_pending = false;
	state->command_origin_valid = false;
	state->command_segment_checked = false;
	state->transaction_elapsed_ms = next_ms;
	if (boundary)
		state->last_boundary_ms = next_ms;
	if (!sweep_ok)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (aborted)
	{
		state->swim_active = false;
		state->swim_link = -1;
		SG_HookLiveDeactivate(&state->hook, &state->hook_active,
		                      &state->hook_link);
		SG_HookLiveCommandGuardClear(&state->hook_command_guard);
		state->control = boundary ? SG_COMPOUND_HOOK_LIVE_CONTROL_NONE :
		                            SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING;
		if (boundary && host->hold_open(host->context, &state->snapshot,
		                  SG_COMPOUND_HOLD_LEASE_MS) !=
		                  SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE);
		return CompoundHookLiveActive(state, false);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING)
	{
		if (boundary)
		{
			if (host->hold_open(host->context, &state->snapshot,
			                  SG_COMPOUND_HOLD_LEASE_MS) !=
			    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
				return CompoundHookLiveOwnedFailure(state,
				    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
				    SG_REPLAY_REASON_INVALID_STATE);
			state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
		}
		return CompoundHookLiveActive(state, false);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING &&
	    opening_status != SG_REPLAY_RUNNING)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    state->opening_safety.reason);
	if ((control == SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH ||
	     control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY) &&
	    swim_status == SG_REPLAY_FAILED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    state->swim.progress.reason);
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH &&
	    swim_status == SG_REPLAY_ARRIVED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (waiting_attach)
	{
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    hook_result.replay_reason);
		if (boundary && host->hold_open(host->context, &state->snapshot,
		                  SG_COMPOUND_HOLD_LEASE_MS) !=
		                  SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE);
		return CompoundHookLiveActive(state, false);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING && boundary)
	{
		if (next_ms == state->snapshot.binding.mover_top_ms)
		{
			if (!SG_CompoundAdvance(&state->outer,
			                        SG_COMPOUND_EVENT_TOP))
				return CompoundHookLiveOwnedFailure(state,
				    SG_COMPOUND_HOOK_LIVE_FAILURE_TOP,
				    SG_REPLAY_REASON_INVALID_STATE);
			state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
		}
		else if (next_ms > state->snapshot.binding.mover_top_ms)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_TOP,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX)
	{
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING &&
		    hook_result.outcome != SG_HOOK_LIVE_ARRIVED)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    hook_result.replay_reason);
		if (boundary)
			return SweepBoundary(state, host, hook_result.outcome);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY && boundary)
	{
		if (host->hold_open(host->context, &state->snapshot,
		                    SG_COMPOUND_HOLD_LEASE_MS) !=
		    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE);
		if (swim_status == SG_REPLAY_ARRIVED && CompoundHookLiveClear(state, host))
			return CompoundHookLiveFinishRelease(state, host, true);
	}
	return CompoundHookLiveActive(state, false);
}

sg_compound_hook_live_result_t SG_CompoundHookLivePostStep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	return ConsumeStep(state, host, pose, observation, false);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveBoundary(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	return ConsumeStep(state, host, pose, observation, true);
}
