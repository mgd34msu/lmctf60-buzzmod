#include "slipgate/sg_compound_hook_live_internal.h"

static qboolean HookEventFrameValid(
	const sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event, int frame_serial)
{
	switch (event)
	{
	case SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED:
		return state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_LINKED &&
		       frame_serial > state->last_event_frame_serial;
	case SG_COMPOUND_HOOK_LIVE_EVENT_PULL:
		return state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED &&
		       frame_serial == state->last_event_frame_serial;
	case SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE:
		return state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_PULL &&
		       frame_serial > state->last_event_frame_serial;
	default:
		return false;
	}
}

static sg_compound_hook_live_result_t HookEvent(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose, sg_compound_hook_live_event_t event)
{
	sg_hook_live_result_t hook_result;
	if (!state || !pose || !state->guard_owned || !state->local_owned ||
	    !CompoundHookLiveHostValid(host) || !CompoundHookLiveBoltValid(bolt))
		return state && state->guard_owned ?
		       CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (!state->bolt_linked || !CompoundHookLiveBoltEqual(&state->bolt, bolt) ||
	    frame_serial <= 0 || frame_serial < state->last_event_frame_serial)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->event_authorize(host->context, &state->snapshot,
	                          event, bolt) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (CompoundHookLiveDuplicate(state, event, bolt, frame_serial))
		return CompoundHookLiveActive(state, false);
	if (!HookEventFrameValid(state, event, frame_serial))
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED &&
	    (state->command_pending || state->transaction_elapsed_ms <= 0 ||
	     state->transaction_elapsed_ms % SG_REPLAY_FRAME_MS != 0 ||
	     state->last_boundary_ms != state->transaction_elapsed_ms))
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
	switch (event)
	{
	case SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED:
		hook_result = SG_HookLiveAttached(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true, pose);
		break;
	case SG_COMPOUND_HOOK_LIVE_EVENT_PULL:
		hook_result = SG_HookLivePullApplied(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true, pose);
		break;
	case SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE:
		hook_result = SG_HookLiveReleaseApplied(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true, pose);
		break;
	default:
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT);
	}
	if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    hook_result.replay_reason);
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE)
	{
		state->hook_released = true;
		state->bolt_abort_applied = true;
	}
	CompoundHookLiveRemember(state, event, frame_serial);
	return CompoundHookLiveActive(state, false);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveAttached(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose)
{
	return HookEvent(state, host, bolt, frame_serial, pose,
	                 SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED);
}
sg_compound_hook_live_result_t SG_CompoundHookLivePullApplied(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose)
{
	return HookEvent(state, host, bolt, frame_serial, pose,
	                 SG_COMPOUND_HOOK_LIVE_EVENT_PULL);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveReleaseApplied(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose)
{
	return HookEvent(state, host, bolt, frame_serial, pose,
	                 SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveRecover(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->local_owned || !CompoundHookLiveHostValid(host) ||
	    (state->outer.phase != SG_COMPOUND_RECOVER &&
	     state->outer.phase != SG_COMPOUND_RELEASE_READY))
		return state && state->guard_owned ?
		       CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (state->command_pending)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING ||
	    state->transaction_elapsed_ms <= 0 ||
	    state->transaction_elapsed_ms % SG_REPLAY_FRAME_MS != 0 ||
	    state->last_boundary_ms != state->transaction_elapsed_ms)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->bolt_linked && !state->bolt_abort_applied)
	{
		if (host->abort_bolt(host->context, &state->snapshot,
		                     &state->bolt) !=
		    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
			return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE, false);
		state->bolt_abort_applied = true;
		state->hook_released = true;
	}
	if (state->outer.phase == SG_COMPOUND_RELEASE_READY || CompoundHookLiveClear(state, host))
		return CompoundHookLiveFinishRelease(state, host, true);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY)
		return CompoundHookLiveActive(state, false);
	if (!CompoundHookLiveBeginSwim(state, pose, observation,
	        state->snapshot.binding.destination_seed.origin, false,
	        old_frame_z, SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    state->swim.progress.reason, false);
	return CompoundHookLiveActive(state, false);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveOrphan(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	const sg_compound_hook_live_bolt_t *bolt;
	if (!state || !state->guard_owned || !state->local_owned ||
	    !CompoundHookLiveHostValid(host))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	bolt = state->bolt_linked ? &state->bolt : NULL;
	if (CompoundHookLiveCurrent(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ||
	    host->orphan(host->context, &state->snapshot, bolt) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ORPHAN,
		    SG_REPLAY_REASON_NONE, false);
	SG_CompoundReset(&state->outer);
	CompoundHookLiveClearLocal(state);
	return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED,
	              SG_COMPOUND_HOOK_LIVE_FAILURE_NONE,
	              SG_REPLAY_REASON_NONE, false);
}
qboolean SG_CompoundHookLiveOwns(
	const sg_compound_hook_live_state_t *state, uint32_t link_index,
	int mover_key)
{
	return state && state->guard_owned && state->local_owned &&
	       link_index <= (uint32_t)INT_MAX &&
	       SG_CompoundOwns(&state->outer, (int)link_index, mover_key);
}
