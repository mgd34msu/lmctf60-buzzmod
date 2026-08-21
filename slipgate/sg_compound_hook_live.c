#include "slipgate/sg_compound_action_publication.h"
#include "slipgate/sg_compound_hook_live.h"

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
	       host->hook_shadow;
}
qboolean CompoundHookLiveBoltValid(const sg_compound_hook_live_bolt_t *bolt)
{
	return bolt && bolt->key > 0 && bolt->generation != 0U;
}
qboolean CompoundHookLiveBoltEqual(const sg_compound_hook_live_bolt_t *first,
	const sg_compound_hook_live_bolt_t *second)
{
	return first && second && first->key == second->key &&
	       first->generation == second->generation;
}
static qboolean SnapshotValid(
	const sg_compound_hook_live_snapshot_t *snapshot, uint32_t link_index,
	sg_hook_replay_spec_t *hook_spec)
{
	const sg_compound_publication_binding_t *binding;
	sg_compound_hook_publication_proof_t proof;
	if (!snapshot || !hook_spec || link_index > (uint32_t)INT_MAX ||
	    snapshot->trigger_key <= 0 || snapshot->mover_key <= 0)
		return false;
	binding = &snapshot->binding;
	if (binding->link_index != link_index ||
	    binding->mechanism_index == SG_COMPOUND_PUBLICATION_INDEX_NONE ||
	    binding->link.action != RL_DOOR_HOOK ||
	    binding->link.mode != RLCM_PREOPEN ||
	    binding->touch_ms < SG_REPLAY_STEP_MS ||
	    binding->touch_ms >= binding->touch_frame_end_ms ||
	    binding->touch_ms % SG_REPLAY_STEP_MS != 0 ||
	    binding->touch_frame_end_ms % SG_REPLAY_FRAME_MS != 0 ||
	    binding->total_cost_ms <= 0 ||
	    binding->total_cost_ms > RUNE_MAX_COST_MS ||
	    binding->mover_top_ms != binding->touch_frame_end_ms +
	                              binding->suffix_start_ms)
		return false;
	memset(&proof, 0, sizeof(proof));
	proof.spec = snapshot->hook_proof;
	return SG_CompoundHookPublicationPlan(binding, &proof, hook_spec);
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
qboolean CompoundHookLiveBeginSwim(sg_compound_hook_live_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_hook_live_control_t control)
{
	sg_swim_replay_spec_t spec;
	if (!state || !pose || !observation || !destination)
		return false;
	memset(&spec, 0, sizeof(spec));
	VectorCopy(destination, spec.destination);
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	if (SG_SwimReplayBegin(&state->swim, &spec, pose, observation,
	                       old_frame_z) != SG_REPLAY_RUNNING)
		return false;
	state->swim_active = true;
	state->swim_link = (int)state->snapshot.binding.link_index;
	state->control = control;
	return true;
}
qboolean CompoundHookLiveDuplicate(const sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial)
{
	return state && state->guard_owned && state->local_owned &&
	       state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX &&
	       state->hook_active && state->bolt_linked &&
	       event == state->last_event &&
	       frame_serial == state->last_event_frame_serial &&
	       CompoundHookLiveBoltEqual(&state->bolt, bolt);
}
void CompoundHookLiveRemember(sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event, int frame_serial)
{
	state->last_event = event;
	state->last_event_frame_serial = frame_serial;
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

	return body == SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED &&
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

sg_compound_hook_live_result_t SG_CompoundHookLiveBegin(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_compound_hook_live_state_t candidate =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_host_result_t bound;
	if (!state || !pose || !observation || !CompoundHookLiveHostValid(host) ||
	    state->guard_owned || state->outer.phase != SG_COMPOUND_NONE)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	bound = host->bind(host->context, link_index, &candidate.snapshot);
	if (bound != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(bound == SG_COMPOUND_HOOK_LIVE_HOST_DENIED ?
		              SG_COMPOUND_HOOK_LIVE_WAIT :
		              SG_COMPOUND_HOOK_LIVE_REJECTED,
		              SG_COMPOUND_HOOK_LIVE_FAILURE_BINDING,
		              SG_REPLAY_REASON_NONE, false);
	if (!SnapshotValid(&candidate.snapshot, link_index,
	                   &candidate.hook_spec))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_PLAN,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	candidate.swim_link = -1;
	candidate.hook_link = -1;
	candidate.opening_safety.status = SG_REPLAY_RUNNING;
	SG_HookLiveCommandGuardClear(&candidate.hook_command_guard);
	if (!SG_CompoundBegin(&candidate.outer, (int)link_index,
	                      candidate.snapshot.mover_key, RL_DOOR_HOOK,
	                      RLCM_PREOPEN) ||
	    !SG_CompoundAdvance(&candidate.outer, SG_COMPOUND_EVENT_APPROACH) ||
	    !CompoundHookLiveBeginSwim(&candidate, pose, observation,
	        candidate.snapshot.binding.link.mechanism_anchor, true,
	        candidate.snapshot.binding.source.old_frame_z,
	        SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    candidate.swim.progress.reason, false);
	if (host->source_checkpoint(host->context, &candidate.snapshot,
	                            pose, observation) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SOURCE_CHECKPOINT,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (host->acquire(host->context, &candidate.snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_WAIT,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ACQUIRE, SG_REPLAY_REASON_NONE,
		    false);
	candidate.guard_owned = true;
	candidate.local_owned = true;
	*state = candidate;
	return CompoundHookLiveActive(state, false);
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
	state->command_pending = true;
	state->command_approved = false;
	state->command_replay_consumed = false;
	return CompoundHookLiveActive(state, true);
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
	if (!state->sweep_clear && elapsed >= state->snapshot.binding.sweep_clear_ms)
	{
		if (!body_clear)
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
	state->transaction_elapsed_ms = next_ms;
	if (boundary)
		state->last_boundary_ms = next_ms;
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

sg_compound_hook_live_result_t SG_CompoundHookLiveTouch(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, int frame_serial)
{
	sg_replay_status_t status;
	if (!state || !state->guard_owned || !state->local_owned ||
	    !pose || !observation || !CompoundHookLiveHostValid(host))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (state->outer.phase != SG_COMPOUND_APPROACH ||
	    state->control != SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH ||
	    !state->command_pending || !state->command_approved ||
	    state->command_replay_consumed ||
	    trigger_key != state->snapshot.trigger_key ||
	    frame_serial <= 0 ||
	    state->transaction_elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS ||
	    state->transaction_elapsed_ms + SG_REPLAY_STEP_MS !=
	        state->snapshot.binding.touch_ms)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    SG_REPLAY_REASON_TIMING_MISMATCH);
	status = SG_SwimReplayPostStep(&state->swim, pose, observation);
	if (status != SG_REPLAY_RUNNING && status != SG_REPLAY_ARRIVED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    state->swim.progress.reason);
	state->command_replay_consumed = true;
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_TOUCH))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    SG_REPLAY_REASON_INVALID_STATE);
	state->touch_frame_serial = frame_serial;
	return CompoundHookLiveActive(state, false);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveActivate(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial)
{
	if (!state || !state->guard_owned || !state->local_owned ||
	    !CompoundHookLiveHostValid(host))
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (state->outer.phase != SG_COMPOUND_TOUCHED ||
	    trigger_key != state->snapshot.trigger_key ||
	    mover_key != state->snapshot.mover_key ||
	    frame_serial != state->touch_frame_serial ||
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ACTIVATE))
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	state->swim_active = false;
	state->swim_link = -1;
	memset(&state->opening_safety, 0, sizeof(state->opening_safety));
	state->opening_safety.status = SG_REPLAY_RUNNING;
	state->opening_safety.old_frame_z = state->swim.progress.old_frame_z;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING;
	return CompoundHookLiveActive(state, false);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveLinked(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_hook_live_result_t hook_result;
	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->local_owned || !CompoundHookLiveHostValid(host) ||
	    !CompoundHookLiveBoltValid(bolt))
		return state && state->guard_owned ?
		       CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_LINK,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (CompoundHookLiveAuthorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_LINK,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->event_authorize(host->context, &state->snapshot,
	                          SG_COMPOUND_HOOK_LIVE_EVENT_LINKED, bolt) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (CompoundHookLiveDuplicate(state, SG_COMPOUND_HOOK_LIVE_EVENT_LINKED,
	              bolt, frame_serial))
		return CompoundHookLiveActive(state, false);
	if (state->bolt_linked || state->outer.phase != SG_COMPOUND_TOP ||
	    frame_serial <= state->touch_frame_serial || state->command_pending)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_LINK,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->suffix_checkpoint(host->context, &state->snapshot,
	                            pose, observation) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SUFFIX_CHECKPOINT,
		    SG_REPLAY_REASON_INVALID_STATE);
	hook_result = SG_HookLiveBegin(&state->hook, &state->hook_active,
	    &state->hook_link, (int)state->snapshot.binding.link_index, true,
	    &state->hook_spec, pose, observation,
	    state->snapshot.binding.suffix.old_frame_z,
	    &state->hook_command_guard);
	if (hook_result.outcome != SG_HOOK_LIVE_RUNNING ||
	    !SG_CompoundAdvance(&state->outer,
	                        SG_COMPOUND_EVENT_SUFFIX_BEGIN))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    hook_result.replay_reason);
	state->bolt = *bolt;
	state->bolt_linked = true;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX;
	CompoundHookLiveRemember(state, SG_COMPOUND_HOOK_LIVE_EVENT_LINKED, frame_serial);
	return CompoundHookLiveActive(state, false);
}
