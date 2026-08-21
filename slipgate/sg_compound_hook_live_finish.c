#include "slipgate/sg_compound_hook_live_internal.h"
#include "slipgate/sg_compound_action_publication.h"

#include <limits.h>
#include <string.h>

static qboolean CompoundHookLiveBoltValid(
	const sg_compound_hook_live_bolt_t *bolt)
{
	return bolt && bolt->key > 0 && bolt->generation != 0U;
}
static qboolean CompoundHookLiveBoltEqual(
	const sg_compound_hook_live_bolt_t *first,
	const sg_compound_hook_live_bolt_t *second)
{
	return first && second && first->key == second->key &&
	       first->generation == second->generation;
}
static qboolean CompoundHookLiveBeginSwim(
	sg_compound_hook_live_state_t *state, const sg_replay_pose_t *pose,
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
static qboolean CompoundHookLiveDuplicate(
	const sg_compound_hook_live_state_t *state,
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
static void CompoundHookLiveRemember(sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event, int frame_serial)
{
	state->last_event = event;
	state->last_event_frame_serial = frame_serial;
}
static qboolean CompoundHookLivePoseAtAnchor(
	const sg_replay_pose_t *pose, const float anchor[3])
{
	int axis;

	if (!pose || !anchor)
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled;

		if (!isfinite(anchor[axis]))
			return false;
		scaled = anchor[axis] * (float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;
		if (scaled < SHRT_MIN || scaled > SHRT_MAX ||
		    pose->origin[axis] != anchor[axis] ||
		    pose->pms.origin[axis] != (short)scaled)
			return false;
	}
	return true;
}
static qboolean CompoundHookLiveRepeatedTriggerPhase(
	const sg_compound_hook_live_state_t *state)
{
	if (!state || !state->command_pending || !state->command_approved)
		return false;
	if (state->outer.phase == SG_COMPOUND_OPENING)
		return state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING;
	return (state->outer.phase == SG_COMPOUND_SUFFIX_LEASED ||
	        state->outer.phase == SG_COMPOUND_SUFFIX_CLEAR) &&
	       state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX;
}
static qboolean SnapshotValid(
	const sg_compound_hook_live_snapshot_t *snapshot, uint32_t link_index,
	sg_hook_replay_spec_t *hook_spec)
{
	const sg_compound_publication_binding_t *binding;
	if (!snapshot || !hook_spec || link_index > (uint32_t)INT_MAX ||
	    snapshot->trigger_key <= 0 || snapshot->mover_key <= 0)
		return false;
	binding = &snapshot->binding;
	if (binding->link_index != link_index ||
	    binding->mechanism_index == SG_COMPOUND_PUBLICATION_INDEX_NONE ||
	    binding->link.action != RL_DOOR_HOOK ||
	    binding->link.mode != RLCM_PREOPEN ||
	    binding->touch_ms <= 0 ||
	    binding->touch_ms % SG_REPLAY_STEP_MS != 0 ||
	    binding->touch_frame_end_ms % SG_REPLAY_FRAME_MS != 0 ||
	    binding->total_cost_ms <= 0 ||
	    binding->total_cost_ms > RUNE_MAX_COST_MS ||
	    binding->mover_top_ms != binding->touch_frame_end_ms +
	                              binding->suffix_start_ms)
		return false;
	return SG_CompoundHookPublicationPlan(binding, hook_spec);
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

sg_compound_hook_live_result_t SG_CompoundHookLiveTouch(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, int frame_serial)
{
	sg_replay_status_t status;

	if (state && pose && observation && state->guard_owned &&
	    state->local_owned && trigger_key == state->snapshot.trigger_key &&
	    frame_serial > 0 && CompoundHookLiveRepeatedTriggerPhase(state))
	{
		if (CompoundHookLiveAuthorized(state, host) !=
		    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
			return CompoundHookLiveOwnedFailure(state,
			    SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
			    SG_REPLAY_REASON_INVALID_STATE);
		return CompoundHookLiveResult(SG_COMPOUND_HOOK_LIVE_WAIT,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE,
		    false);
	}
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
	        state->snapshot.binding.touch_ms ||
	    !CompoundHookLivePoseAtAnchor(pose,
	        state->snapshot.binding.link.mechanism_anchor))
		return CompoundHookLiveOwnedFailure(state, SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
		                    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (!CompoundHookLiveObserveSweep(state, host, pose,
	        state->transaction_elapsed_ms + SG_REPLAY_STEP_MS))
		return CompoundHookLiveOwnedFailure(state,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
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
	long long expected_frame;
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
	expected_frame = (long long)state->touch_frame_serial +
	                 (state->snapshot.binding.mover_top_ms -
	                  state->snapshot.binding.touch_frame_end_ms) /
	                     SG_REPLAY_FRAME_MS;
	if (state->bolt_linked || state->outer.phase != SG_COMPOUND_TOP ||
	    expected_frame != frame_serial ||
	    state->command_pending)
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
	state->sweep_outside_since_ms = state->transaction_elapsed_ms;
	state->segment_clear_ready = false;
	state->recovery_sweep_dirty = false;
	CompoundHookLiveRemember(state, SG_COMPOUND_HOOK_LIVE_EVENT_LINKED, frame_serial);
	return CompoundHookLiveActive(state, false);
}


static qboolean HookEventFrameValid(
	const sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event, int frame_serial)
{
	long long expected;
	int wait_ms;

	switch (event)
	{
	case SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED:
		wait_ms = state->transaction_elapsed_ms -
		          state->snapshot.binding.mover_top_ms;
		expected = (long long)state->last_event_frame_serial + 1 +
		           wait_ms / SG_REPLAY_FRAME_MS;
		return state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_LINKED &&
		       wait_ms >= 0 && wait_ms % SG_REPLAY_FRAME_MS == 0 &&
		       expected == frame_serial;
	case SG_COMPOUND_HOOK_LIVE_EVENT_PULL:
		return (state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED &&
		        frame_serial == state->last_event_frame_serial) ||
		       (state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_PULL &&
		        (long long)frame_serial ==
		            (long long)state->last_event_frame_serial + 1);
	case SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE:
		expected = (long long)state->pull_frame_serial +
		           (state->hook.pull_ms + SG_REPLAY_FRAME_MS - 1) /
		               SG_REPLAY_FRAME_MS;
		return state->last_event == SG_COMPOUND_HOOK_LIVE_EVENT_PULL &&
		       state->pull_frame_serial > 0 && state->hook.pull_ms > 0 &&
		       expected == frame_serial;
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
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_PULL &&
	    state->pull_frame_serial == 0)
		state->pull_frame_serial = frame_serial;
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
	if (state->outer.phase == SG_COMPOUND_RELEASE_READY ||
	    CompoundHookLiveClear(state, host))
		return CompoundHookLiveFinishRelease(state, host, true);
	if (!state->recovery_sweep_dirty)
	{
		state->recovery_sweep_dirty = true;
		state->segment_clear_ready = false;
		state->sweep_outside_since_ms = -1;
	}
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
