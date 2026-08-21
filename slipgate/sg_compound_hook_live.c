#include "slipgate/sg_compound_action_publication.h"
#include "slipgate/sg_compound_hook_live.h"

#include <limits.h>
#include <string.h>

static sg_compound_hook_live_result_t Result(
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

static qboolean HostValid(const sg_compound_hook_live_host_t *host)
{
	return host && host->bind && host->acquire && host->authorize &&
	       host->hold_open && host->body_clear && host->bolt_clear &&
	       host->release && host->orphan && host->hook_shadow;
}
static qboolean BoltValid(const sg_compound_hook_live_bolt_t *bolt)
{
	return bolt && bolt->key > 0 && bolt->generation != 0U;
}
static qboolean BoltEqual(const sg_compound_hook_live_bolt_t *first,
	const sg_compound_hook_live_bolt_t *second)
{
	return first && second && first->key == second->key &&
	       first->generation == second->generation;
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
	    binding->mover_top_ms != binding->touch_frame_end_ms +
	                              binding->suffix_start_ms)
		return false;
	memset(&proof, 0, sizeof(proof));
	proof.spec = snapshot->hook_proof;
	return SG_CompoundHookPublicationPlan(binding, &proof, hook_spec);
}
static sg_compound_hook_live_host_result_t Current(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	sg_compound_hook_live_snapshot_t current;
	sg_compound_hook_live_host_result_t result;
	if (!state || !HostValid(host))
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
static sg_compound_hook_live_host_result_t Authorized(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	if (Current(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	return host->authorize(host->context, &state->snapshot);
}
static sg_compound_hook_live_result_t Active(
	const sg_compound_hook_live_state_t *state, qboolean command_ready)
{
	return Result(state && state->recovering ?
	              SG_COMPOUND_HOOK_LIVE_RECOVERING :
	              SG_COMPOUND_HOOK_LIVE_RUNNING,
	              SG_COMPOUND_HOOK_LIVE_FAILURE_NONE,
	              SG_REPLAY_REASON_NONE, command_ready);
}
static sg_compound_hook_live_result_t OwnedFailure(
	sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_failure_t failure, sg_replay_reason_t reason)
{
	if (!state || !state->guard_owned || !state->local_owned ||
	    state->outer.phase == SG_COMPOUND_NONE)
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED, failure, reason, false);
	if (state->outer.phase != SG_COMPOUND_RECOVER)
		(void)SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ABORT);
	state->failure = failure;
	state->replay_reason = reason;
	state->recovering = true;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
	state->command_pending = false;
	state->command_approved = false;
	state->swim_active = false;
	state->swim_link = -1;
	SG_HookLiveDeactivate(&state->hook, &state->hook_active,
	                      &state->hook_link);
	SG_HookLiveCommandGuardClear(&state->hook_command_guard);
	state->sweep_clear = false;
	state->arrived = false;
	return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING, failure, reason, false);
}
static qboolean BeginSwim(sg_compound_hook_live_state_t *state,
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
static qboolean Duplicate(const sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial)
{
	return state && state->guard_owned && state->local_owned &&
	       state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX &&
	       state->hook_active && state->bolt_linked &&
	       event == state->last_event &&
	       frame_serial == state->last_event_frame_serial &&
	       BoltEqual(&state->bolt, bolt);
}
static void Remember(sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event, int frame_serial)
{
	state->last_event = event;
	state->last_event_frame_serial = frame_serial;
}
static qboolean Clear(const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	const sg_compound_hook_live_bolt_t *bolt =
		state->bolt_linked ? &state->bolt : NULL;
	return host->body_clear(host->context, &state->snapshot, bolt) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED &&
	       host->bolt_clear(host->context, &state->snapshot, bolt) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}
static sg_compound_hook_live_result_t FinishRelease(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, qboolean recovered)
{
	if (!Clear(state, host))
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP, SG_REPLAY_REASON_NONE,
		    false);
	if (recovered && state->outer.phase == SG_COMPOUND_RECOVER &&
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RECOVERED))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (!SG_CompoundReleaseReady(&state->outer))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->release(host->context, &state->snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE, SG_REPLAY_REASON_NONE,
		    false);
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RELEASED))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	state->guard_owned = false;
	state->local_owned = false;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
	return Result(recovered ? SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED :
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
	if (!state || !pose || !observation || !HostValid(host) ||
	    state->guard_owned || state->outer.phase != SG_COMPOUND_NONE)
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	bound = host->bind(host->context, link_index, &candidate.snapshot);
	if (bound != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(bound == SG_COMPOUND_HOOK_LIVE_HOST_DENIED ?
		              SG_COMPOUND_HOOK_LIVE_WAIT :
		              SG_COMPOUND_HOOK_LIVE_REJECTED,
		              SG_COMPOUND_HOOK_LIVE_FAILURE_BINDING,
		              SG_REPLAY_REASON_NONE, false);
	if (!SnapshotValid(&candidate.snapshot, link_index,
	                   &candidate.hook_spec))
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_PLAN,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	candidate.swim_link = -1;
	candidate.hook_link = -1;
	SG_HookLiveCommandGuardClear(&candidate.hook_command_guard);
	if (!SG_CompoundBegin(&candidate.outer, (int)link_index,
	                      candidate.snapshot.mover_key, RL_DOOR_HOOK,
	                      RLCM_PREOPEN) ||
	    !SG_CompoundAdvance(&candidate.outer, SG_COMPOUND_EVENT_APPROACH) ||
	    !BeginSwim(&candidate, pose, observation,
	        candidate.snapshot.binding.link.mechanism_anchor, true,
	        candidate.snapshot.binding.source.old_frame_z,
	        SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH))
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    candidate.swim.progress.reason, false);
	if (host->acquire(host->context, &candidate.snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_WAIT,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ACQUIRE, SG_REPLAY_REASON_NONE,
		    false);
	candidate.guard_owned = true;
	candidate.local_owned = true;
	*state = candidate;
	return Active(state, false);
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
	    !state->guard_owned || !state->local_owned || !HostValid(host))
		return state && state->guard_owned ?
		       OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (state->command_pending ||
	    state->transaction_elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	switch (state->control)
	{
	case SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH:
	case SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY:
		status = SG_SwimReplayPreStep(&state->swim, pose, command);
		if (status != SG_REPLAY_RUNNING)
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    state->swim.progress.reason);
		break;
	case SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING:
		if (!SG_HookReplayFixedViewCommand(pose,
		        state->hook_spec.view_angles, command))
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_CONTROL);
		break;
	case SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX:
		if (state->hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH &&
		    state->transaction_elapsed_ms -
		        state->snapshot.binding.mover_top_ms -
		        state->hook.progress.elapsed_ms >=
		    SG_REPLAY_HOOK_FLIGHT_MAX_MS - state->hook.flight_body_ms)
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
		hook_result = (state->hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH ?
		    SG_HookLiveWaitAttachStep : SG_HookLivePreStep)(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true, pose, observation,
		    host->hook_shadow, command, &state->hook_command_guard);
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    hook_result.replay_reason);
		break;
	default:
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	}
	state->expected_command = *command;
	state->command_pending = true;
	state->command_approved = false;
	return Active(state, true);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveApproveCommand(
	sg_compound_hook_live_state_t *state, const usercmd_t *command)
{
	sg_hook_live_result_t hook_result;
	if (!state || !state->guard_owned || !state->local_owned ||
	    !state->command_pending || state->command_approved || !command)
		return state && state->guard_owned ?
		       OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE) :
		       Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX)
	{
		hook_result = SG_HookLiveValidateStoredFinalCommand(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true,
		    &state->hook_command_guard, command);
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    hook_result.replay_reason);
	}
	else if (!CommandEqual(&state->expected_command, command))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    SG_REPLAY_REASON_INVALID_CONTROL);
	state->command_approved = true;
	return Active(state, false);
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
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
		state->sweep_clear = true;
		if (!SG_CompoundAdvance(&state->outer,
		                        SG_COMPOUND_EVENT_SWEEP_CLEAR))
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (hook_outcome == SG_HOOK_LIVE_ARRIVED)
	{
		state->arrived = true;
		if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ARRIVED))
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (SG_CompoundReleaseReady(&state->outer))
	{
		if (!state->hook_released || !Clear(state, host))
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
		return FinishRelease(state, host, false);
	}
	if (SG_CompoundSuffixNeedsHold(elapsed,
	        state->snapshot.binding.sweep_clear_ms) &&
	    host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
		                    SG_REPLAY_REASON_INVALID_STATE);
	return Active(state, false);
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
	int next_ms;
	qboolean waiting_attach;
	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->local_owned || !HostValid(host))
		return state && state->guard_owned ?
		       OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (!state->command_pending || !state->command_approved ||
	    state->transaction_elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	next_ms = state->transaction_elapsed_ms + SG_REPLAY_STEP_MS;
	if (((next_ms % SG_REPLAY_FRAME_MS) == 0) != boundary ||
	    (boundary && next_ms == state->last_boundary_ms))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	control = state->control;
	waiting_attach = control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX &&
	                 state->hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH;
	memset(&hook_result, 0, sizeof(hook_result));
	hook_result.outcome = SG_HOOK_LIVE_RUNNING;
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH ||
	    control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY)
		swim_status = SG_SwimReplayPostStep(&state->swim, pose, observation);
	else if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX &&
	         !waiting_attach)
		hook_result = SG_HookLivePostStep(&state->hook,
		    &state->hook_active, &state->hook_link,
		    (int)state->snapshot.binding.link_index, true, pose, observation);
	state->command_pending = false;
	state->command_approved = false;
	state->transaction_elapsed_ms = next_ms;
	if (boundary)
		state->last_boundary_ms = next_ms;
	if ((control == SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH ||
	     control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY) &&
	    swim_status == SG_REPLAY_FAILED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    state->swim.progress.reason);
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH &&
	    swim_status == SG_REPLAY_ARRIVED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (waiting_attach)
	{
		if (boundary && host->hold_open(host->context, &state->snapshot,
		                  SG_COMPOUND_HOLD_LEASE_MS) !=
		                  SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE);
		return Active(state, false);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING && boundary)
	{
		if (next_ms == state->snapshot.binding.mover_top_ms)
		{
			if (!SG_CompoundAdvance(&state->outer,
			                        SG_COMPOUND_EVENT_TOP))
				return OwnedFailure(state,
				    SG_COMPOUND_HOOK_LIVE_FAILURE_TOP,
				    SG_REPLAY_REASON_INVALID_STATE);
			state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
		}
		else if (next_ms > state->snapshot.binding.mover_top_ms)
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_TOP,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
	}
	if (control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX)
	{
		if (hook_result.outcome != SG_HOOK_LIVE_RUNNING &&
		    hook_result.outcome != SG_HOOK_LIVE_ARRIVED)
			return OwnedFailure(state,
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
			return OwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE);
		if (swim_status == SG_REPLAY_ARRIVED && Clear(state, host))
			return FinishRelease(state, host, true);
	}
	return Active(state, false);
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
	int frame_serial)
{
	if (!state || !state->guard_owned || !state->local_owned ||
	    !HostValid(host))
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (state->outer.phase != SG_COMPOUND_APPROACH ||
	    state->command_pending || trigger_key != state->snapshot.trigger_key ||
	    frame_serial <= 0 ||
	    state->transaction_elapsed_ms != state->snapshot.binding.touch_ms ||
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_TOUCH))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    SG_REPLAY_REASON_TIMING_MISMATCH);
	state->touch_frame_serial = frame_serial;
	return Active(state, false);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveActivate(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial)
{
	if (!state || !state->guard_owned || !state->local_owned ||
	    !HostValid(host))
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (state->outer.phase != SG_COMPOUND_TOUCHED ||
	    trigger_key != state->snapshot.trigger_key ||
	    mover_key != state->snapshot.mover_key ||
	    frame_serial != state->touch_frame_serial ||
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ACTIVATE))
		return OwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	state->swim_active = false;
	state->swim_link = -1;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING;
	return Active(state, false);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveLinked(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_hook_live_result_t hook_result;
	if (Duplicate(state, SG_COMPOUND_HOOK_LIVE_EVENT_LINKED,
	              bolt, frame_serial))
		return Active(state, false);
	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->local_owned || !HostValid(host) || !BoltValid(bolt))
		return state && state->guard_owned ?
		       OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_LINK,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (state->bolt_linked && !BoltEqual(&state->bolt, bolt))
		return OwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (state->bolt_linked || state->outer.phase != SG_COMPOUND_TOP ||
	    frame_serial <= 0 || state->command_pending ||
	    Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_LINK,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
		                    SG_REPLAY_REASON_INVALID_STATE);
	hook_result = SG_HookLiveBegin(&state->hook, &state->hook_active,
	    &state->hook_link, (int)state->snapshot.binding.link_index, true,
	    &state->hook_spec, pose, observation,
	    state->snapshot.binding.suffix.old_frame_z,
	    &state->hook_command_guard);
	if (hook_result.outcome != SG_HOOK_LIVE_RUNNING ||
	    !SG_CompoundAdvance(&state->outer,
	                        SG_COMPOUND_EVENT_SUFFIX_BEGIN))
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    hook_result.replay_reason);
	state->bolt = *bolt;
	state->bolt_linked = true;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX;
	Remember(state, SG_COMPOUND_HOOK_LIVE_EVENT_LINKED, frame_serial);
	return Active(state, false);
}

static sg_compound_hook_live_result_t HookEvent(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose, sg_compound_hook_live_event_t event)
{
	sg_hook_live_result_t hook_result;
	if (Duplicate(state, event, bolt, frame_serial))
		return Active(state, false);
	if (!state || !pose || !state->guard_owned || !state->local_owned ||
	    !HostValid(host) || !BoltValid(bolt))
		return state && state->guard_owned ?
		       OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (!state->bolt_linked || !BoltEqual(&state->bolt, bolt) ||
	    frame_serial <= 0 || frame_serial < state->last_event_frame_serial)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
	if (Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		                    SG_REPLAY_REASON_INVALID_STATE);
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
		return OwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT);
	}
	if (hook_result.outcome != SG_HOOK_LIVE_RUNNING)
		return OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		                    hook_result.replay_reason);
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE)
		state->hook_released = true;
	Remember(state, event, frame_serial);
	return Active(state, false);
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
	    !state->local_owned || !HostValid(host) ||
	    (state->outer.phase != SG_COMPOUND_RECOVER &&
	     state->outer.phase != SG_COMPOUND_RELEASE_READY))
		return state && state->guard_owned ?
		       OwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		                    SG_REPLAY_REASON_INVALID_ARGUMENT) :
		       Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		           SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		           SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (state->command_pending)
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (Authorized(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->outer.phase == SG_COMPOUND_RELEASE_READY || Clear(state, host))
		return FinishRelease(state, host, true);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY)
		return Active(state, false);
	if (!BeginSwim(state, pose, observation,
	        state->snapshot.binding.destination_seed.origin, false,
	        old_frame_z, SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY))
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
		    state->swim.progress.reason, false);
	return Active(state, false);
}
sg_compound_hook_live_result_t SG_CompoundHookLiveOrphan(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	const sg_compound_hook_live_bolt_t *bolt;
	if (!state || !state->guard_owned || !state->local_owned ||
	    !HostValid(host))
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	bolt = state->bolt_linked ? &state->bolt : NULL;
	if (Current(state, host) != SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ||
	    host->orphan(host->context, &state->snapshot, bolt) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_ORPHAN,
		    SG_REPLAY_REASON_NONE, false);
	state->guard_owned = false;
	state->local_owned = false;
	state->command_pending = false;
	state->command_approved = false;
	state->control = SG_COMPOUND_HOOK_LIVE_CONTROL_NONE;
	state->swim_active = false;
	state->hook_active = false;
	SG_CompoundReset(&state->outer);
	return Result(SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED,
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
