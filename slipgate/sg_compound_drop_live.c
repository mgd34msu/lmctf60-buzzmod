/* sg_compound_drop_live.c -- see sg_compound_drop_live.h. */
#include "q_shared.h"

#include <limits.h>
#include <stdint.h>

#include "slipgate/sg_compound_drop_live.h"
#include "slipgate/sg_compound_drop_live_internal.h"

qboolean SG_DeclaredCommand(const vec3_t origin, const vec3_t target,
	const pmove_state_t *pms, usercmd_t *cmd);

sg_compound_drop_live_result_t CompoundDropLiveResult(
	sg_compound_drop_live_outcome_t outcome,
	sg_compound_drop_live_failure_t failure,
	sg_replay_reason_t replay_reason, qboolean command_ready)
{
	sg_compound_drop_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = replay_reason;
	result.command_ready = command_ready;
	return result;
}

qboolean CompoundDropLiveHostValid(
	const sg_compound_drop_live_host_t *host)
{
	return host && host->bind && host->source_checkpoint &&
	       host->suffix_checkpoint && host->acquire && host->authorize &&
	       host->activate && host->at_top && host->hold_open &&
	       host->outside_sweep &&
	       host->ground_support && host->sweep_segment_clear &&
	       host->prove_suffix && host->release && host->orphan &&
	       host->drop_shadow;
}

static qboolean CompoundDropLiveFinite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static qboolean CompoundDropLiveBoolValid(qboolean value)
{
	return value == false || value == true;
}

qboolean CompoundDropLivePoseValid(const sg_replay_pose_t *pose)
{
	return pose && CompoundDropLiveFinite3(pose->origin) &&
	       CompoundDropLiveFinite3(pose->velocity) &&
	       CompoundDropLiveBoolValid(pose->grounded) &&
	       pose->waterlevel >= 0 && pose->waterlevel <= 3;
}

static qboolean CompoundDropLiveRepeatedTriggerPhase(
	const sg_compound_drop_live_state_t *state)
{
	if (!state || !state->command_pending)
		return false;
	if (state->outer.phase == SG_COMPOUND_OPENING)
		return state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_NONE &&
		       state->zero_command_pending;
	return (state->outer.phase == SG_COMPOUND_SUFFIX_LEASED ||
	        state->outer.phase == SG_COMPOUND_SUFFIX_CLEAR ||
	        state->outer.phase == SG_COMPOUND_RELEASE_READY) &&
	       state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX &&
	       !state->zero_command_pending;
}

qboolean CompoundDropLiveObservationValid(
	const sg_replay_observation_t *observation)
{
	return observation &&
	       CompoundDropLiveBoolValid(observation->contact_clear) &&
	       CompoundDropLiveBoolValid(observation->ground_support_valid) &&
	       CompoundDropLiveBoolValid(
	           observation->drop_arrival_contact_clear) &&
	       CompoundDropLiveBoolValid(
	           observation->drop_recovery_contact_clear) &&
	       CompoundDropLiveBoolValid(observation->drop_recovery_admitted) &&
	       CompoundDropLiveBoolValid(observation->drop_landing_observed) &&
	       CompoundDropLiveBoolValid(observation->contaminated) &&
	       CompoundDropLiveBoolValid(observation->door_passed) &&
	       CompoundDropLiveBoolValid(observation->hook_rope_valid) &&
	       (!observation->hook_rope_valid ||
	        observation->hook_rope_length >= 0);
}

qboolean CompoundDropLiveEventsFromObservation(
	const sg_replay_observation_t *observation,
	sg_drop_live_events_t *events)
{
	if (!CompoundDropLiveObservationValid(observation) || !events)
		return false;
	memset(events, 0, sizeof(*events));
	return SG_DropLiveEventsLatch(events, observation->contaminated,
	    observation->door_passed);
}


static qboolean CompoundDropLiveLattice3(const float value[3])
{
	int axis;

	if (!CompoundDropLiveFinite3(value))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = value[axis] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (scaled < (float)SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    scaled > (float)SG_RUNE_PROOF_WORLD_FIXED_MAX ||
		    scaled != (float)(int)scaled)
			return false;
	}
	return true;
}

static qboolean CompoundDropLivePlanValid(
	const sg_compound_drop_live_snapshot_t *snapshot, uint32_t link_index)
{
	const sg_compound_publication_binding_t *binding;
	const rune_link_t *link;
	long long touch_frame_end, total;

	if (!snapshot)
		return false;
	binding = &snapshot->binding;
	link = &binding->link;
	if (binding->link_index != link_index || link_index > (uint32_t)INT_MAX ||
	    snapshot->trigger_key <= 0 || snapshot->mover_key <= 0 ||
	    binding->mechanism_index == SG_COMPOUND_PUBLICATION_INDEX_NONE ||
	    link->from < 0 || link->to < 0 || link->action != RL_DOOR_DROP ||
	    link->provenance != RL_CONTRACTED || link->mode != RLCM_PREOPEN ||
	    link->min_speed != 0 ||
	    link->heading_slack != SG_RUNE_PROOF_DROP_CONTROL_MARKER ||
	    !CompoundDropLiveLattice3(link->anchor) ||
	    !CompoundDropLiveLattice3(link->mechanism_anchor) ||
	    !CompoundDropLiveLattice3(binding->canonical_hint) ||
	    !CompoundDropLiveFinite3(binding->source_seed.origin) ||
	    !CompoundDropLiveFinite3(binding->destination_seed.origin) ||
	    (binding->source_seed.flags & RSF_WATER) != 0 ||
	    (binding->source_seed.flags & ~(RSF_WATER)) != 0 ||
	    (binding->destination_seed.flags & ~(RSF_WATER)) != 0 ||
	    binding->touch_ms <= 0 ||
	    binding->touch_ms > RUNE_MAX_COST_MS ||
	    binding->touch_ms % SG_REPLAY_STEP_MS != 0 ||
	    binding->touch_frame_end_ms <= 0 ||
	    binding->touch_frame_end_ms > RUNE_MAX_COST_MS ||
	    binding->touch_frame_end_ms % SG_REPLAY_FRAME_MS != 0 ||
	    binding->mover_top_ms < 2 * SG_REPLAY_FRAME_MS ||
	    binding->mover_top_ms > RUNE_MAX_COST_MS ||
	    binding->mover_top_ms % SG_REPLAY_FRAME_MS != 0 ||
	    binding->suffix_start_ms < SG_REPLAY_FRAME_MS ||
	    binding->suffix_start_ms !=
		binding->mover_top_ms - SG_REPLAY_FRAME_MS ||
	    binding->arrival_ms <= 0 ||
	    binding->arrival_ms >= SG_REPLAY_DROP_TOTAL_MS ||
	    binding->arrival_ms % SG_REPLAY_FRAME_MS != 0 ||
	    binding->sweep_clear_ms <= 0 ||
	    binding->sweep_clear_ms > binding->arrival_ms ||
	    binding->sweep_clear_ms % SG_REPLAY_FRAME_MS != 0 ||
	    binding->total_cost_ms < RUNE_MIN_COST_MS ||
	    binding->total_cost_ms > RUNE_MAX_COST_MS ||
	    binding->total_cost_ms % SG_REPLAY_FRAME_MS != 0)
		return false;
	touch_frame_end = ((long long)binding->touch_ms +
	                   SG_REPLAY_FRAME_MS - 1LL) /
	                  SG_REPLAY_FRAME_MS * SG_REPLAY_FRAME_MS;
	total = (long long)binding->touch_frame_end_ms +
	        binding->suffix_start_ms + binding->arrival_ms;
	return touch_frame_end == binding->touch_frame_end_ms &&
	       total == binding->total_cost_ms &&
	       binding->total_cost_ms == link->cost_ms &&
	       binding->sweep_clear_ms == link->sweep_clear_ms &&
	       binding->link.exit_speed == link->exit_speed;
}


static qboolean CompoundDropLiveSnapshotEqual(
	const sg_compound_drop_live_snapshot_t *first,
	const sg_compound_drop_live_snapshot_t *second)
{
	/* bind's contract requires a completely initialized copy of the immutable
	 * publication image, so padding bytes are stable identity bytes too. */
	return first && second && memcmp(first, second, sizeof(*first)) == 0;
}

sg_compound_drop_live_host_result_t CompoundDropLiveCurrent(
	const sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host)
{
	sg_compound_drop_live_snapshot_t current;
	sg_compound_drop_live_host_result_t result;

	if (!state || !CompoundDropLiveHostValid(host))
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	memset(&current, 0, sizeof(current));
	result = host->bind(host->context, state->snapshot.binding.link_index,
	                    &current);
	if (result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return result;
	return CompoundDropLiveSnapshotEqual(&current, &state->snapshot) ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_ERROR;
}

sg_compound_drop_live_host_result_t CompoundDropLiveAuthorized(
	const sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host)
{
	if (CompoundDropLiveCurrent(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	return host->authorize(host->context, &state->snapshot);
}

sg_compound_drop_live_result_t CompoundDropLiveOwnedFailure(
	sg_compound_drop_live_state_t *state,
	sg_compound_drop_live_failure_t failure,
	sg_replay_reason_t replay_reason)
{
	if (!state || !state->guard_owned ||
	    state->outer.phase == SG_COMPOUND_NONE)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    failure, replay_reason, false);
	if (state->outer.phase != SG_COMPOUND_RECOVER)
		(void)SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ABORT);
	state->failure = failure;
	state->replay_reason = replay_reason;
	state->recovering = true;
	state->aborted_command_pending = state->command_pending;
	if (!state->command_pending)
	{
		SG_DropLiveDeactivate(&state->replay, &state->drop_active,
		    &state->drop_link);
		state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
		state->zero_command_pending = false;
	}
	state->sweep_clear = false;
	state->arrived = false;
	state->last_sweep_contact_ms = 0;
	return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
	    failure, replay_reason, false);
}

sg_compound_drop_live_result_t CompoundDropLiveActiveResult(
	const sg_compound_drop_live_state_t *state, qboolean command_ready)
{
	return CompoundDropLiveResult(
	    state && state->recovering ? SG_COMPOUND_DROP_LIVE_RECOVERING :
	                               SG_COMPOUND_DROP_LIVE_RUNNING,
	    SG_COMPOUND_DROP_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE,
	    command_ready);
}

qboolean CompoundDropLiveAdvanceTime(int elapsed_ms, int *next_ms)
{
	if (!next_ms || elapsed_ms < 0 ||
	    elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS)
		return false;
	*next_ms = elapsed_ms + SG_REPLAY_STEP_MS;
	return true;
}

static qboolean CompoundDropLivePoseAtAnchor(
	const sg_replay_pose_t *pose, const float anchor[3])
{
	int axis;

	if (!pose || !CompoundDropLiveLattice3(anchor))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = anchor[axis] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (pose->origin[axis] != anchor[axis] ||
		    pose->pms.origin[axis] != (short)scaled)
			return false;
	}
	return true;
}



static sg_compound_drop_live_result_t CompoundDropLiveSweepStep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose)
{
	sg_compound_drop_live_host_result_t segment;

	segment = host->sweep_segment_clear(host->context, &state->snapshot,
	    state->command_origin, pose->origin);
	if (segment != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED &&
	    segment != SG_COMPOUND_DROP_LIVE_HOST_DENIED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (segment == SG_COMPOUND_DROP_LIVE_HOST_DENIED)
	{
		/* Approach and the pre-TOP zero frame are proved outside in the
		 * generator and may not touch the mover sweep.  Recovery padding can
		 * begin inside, while suffix/recovery replay records the exact last
		 * contact required by its published clearance time. */
		if (state->replay_kind != SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX &&
		    state->replay_kind != SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY &&
		    state->outer.phase != SG_COMPOUND_RECOVER)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		if (state->sweep_clear)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		state->last_sweep_contact_ms = state->replay.progress.elapsed_ms;
	}
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t CompoundDropLiveConsumeSweep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose)
{
	if (state->command_segment_checked &&
	    memcmp(state->command_segment_end, pose->origin,
	           sizeof(state->command_segment_end)) == 0)
		return CompoundDropLiveActiveResult(state, false);
	return CompoundDropLiveSweepStep(state, host, pose);
}

const char *SG_CompoundDropLiveFailureName(
	sg_compound_drop_live_failure_t failure)
{
	switch (failure)
	{
	case SG_COMPOUND_DROP_LIVE_FAILURE_NONE: return "none";
	case SG_COMPOUND_DROP_LIVE_FAILURE_ARGUMENT: return "argument";
	case SG_COMPOUND_DROP_LIVE_FAILURE_BINDING: return "binding";
	case SG_COMPOUND_DROP_LIVE_FAILURE_PLAN: return "plan";
	case SG_COMPOUND_DROP_LIVE_FAILURE_SOURCE_CHECKPOINT:
		return "source-checkpoint";
	case SG_COMPOUND_DROP_LIVE_FAILURE_ACQUIRE: return "acquire";
	case SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY: return "authority";
	case SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE: return "cadence";
	case SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH: return "touch";
	case SG_COMPOUND_DROP_LIVE_FAILURE_ACTIVATION: return "activation";
	case SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY: return "replay";
	case SG_COMPOUND_DROP_LIVE_FAILURE_TOP: return "top";
	case SG_COMPOUND_DROP_LIVE_FAILURE_HOLD: return "hold";
	case SG_COMPOUND_DROP_LIVE_FAILURE_SUFFIX_CHECKPOINT:
		return "suffix-checkpoint";
	case SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF: return "reproof";
	case SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP: return "sweep";
	case SG_COMPOUND_DROP_LIVE_FAILURE_TIMING: return "timing";
	case SG_COMPOUND_DROP_LIVE_FAILURE_RELEASE: return "release";
	case SG_COMPOUND_DROP_LIVE_FAILURE_ORPHAN: return "orphan";
	default: return "unknown";
	}
}

sg_compound_drop_live_result_t SG_CompoundDropLiveBegin(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose)
{
	sg_compound_drop_live_state_t candidate =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_compound_drop_live_host_result_t result;

	if (!state || !pose || !CompoundDropLiveHostValid(host) ||
	    state->guard_owned || state->outer.phase != SG_COMPOUND_NONE)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	result = host->bind(host->context, link_index, &candidate.snapshot);
	if (result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(
		    result == SG_COMPOUND_DROP_LIVE_HOST_DENIED ?
		        SG_COMPOUND_DROP_LIVE_WAIT : SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_BINDING,
		    SG_REPLAY_REASON_NONE, false);
	if (!CompoundDropLivePlanValid(&candidate.snapshot, link_index))
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_PLAN,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	result = host->source_checkpoint(host->context, &candidate.snapshot,
	                                 &candidate.angle_bias);
	if (result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_SOURCE_CHECKPOINT,
		    SG_REPLAY_REASON_NONE, false);
	if (!SG_CompoundBegin(&candidate.outer, (int)link_index,
	                      candidate.snapshot.mover_key, RL_DOOR_DROP,
	                      RLCM_PREOPEN) ||
	    !SG_CompoundAdvance(&candidate.outer, SG_COMPOUND_EVENT_APPROACH) ||
	    !CompoundDropLivePoseAtAnchor(pose,
	        candidate.snapshot.binding.source_seed.origin))
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    candidate.replay.progress.reason, false);
	result = host->acquire(host->context, &candidate.snapshot);
	if (result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(
		    result == SG_COMPOUND_DROP_LIVE_HOST_DENIED ?
		        SG_COMPOUND_DROP_LIVE_WAIT : SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ACQUIRE,
		    SG_REPLAY_REASON_NONE, false);
	candidate.guard_owned = true;
	candidate.drop_link = -1;
	candidate.replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH;
	candidate.zero_frame_old_z =
		candidate.snapshot.binding.source.old_frame_z;
	*state = candidate;
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t SG_CompoundDropLivePreStep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	sg_drop_live_result_t live_result = { 0 };

	if (command)
	{
		memset(command, 0, sizeof(*command));
		command->msec = SG_REPLAY_STEP_MS;
	}
	if (!state || !pose || !command || !state->guard_owned ||
	    !CompoundDropLiveHostValid(host))
	{
		if (state && state->guard_owned)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_ARGUMENT,
			    SG_REPLAY_REASON_INVALID_ARGUMENT);
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	}
	if (state->command_pending)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!CompoundDropLivePoseValid(pose))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_NONFINITE_POSE);
	if (CompoundDropLiveAuthorized(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	VectorCopy(pose->origin, state->command_origin);
	VectorClear(state->command_segment_end);
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	state->command_segment_checked = false;
	if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH)
	{
		vec3_t delta;
		float horizontal;

		if (!SG_DeclaredCommand(pose->origin,
		        state->snapshot.binding.link.mechanism_anchor,
		        &pose->pms, command))
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_STATE);
		VectorSubtract(state->snapshot.binding.link.mechanism_anchor,
		               pose->origin, delta);
		delta[2] = 0.0f;
		horizontal = VectorLength(delta);
		if (horizontal <= 64.0f && command->forwardmove > 64)
			command->forwardmove = 64;
		if (horizontal > 0.01f && command->forwardmove == 0)
			command->forwardmove = 40;
	}
	else if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX ||
	    state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY)
	{
		live_result = SG_DropLivePreStep(&state->replay,
		    &state->drop_active, &state->drop_link,
		    (int)state->snapshot.binding.link_index, pose,
		    host->drop_shadow, command);
		if (live_result.outcome != SG_DROP_LIVE_RUNNING)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    live_result.replay_reason);
	}
	else if (state->outer.phase == SG_COMPOUND_TOUCHED ||
	         state->outer.phase == SG_COMPOUND_OPENING ||
	         state->outer.phase == SG_COMPOUND_RECOVER)
	{
		state->zero_command_pending = true;
	}
	else
	{
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	}
	state->command_pending = true;
	return CompoundDropLiveActiveResult(state, true);
}
sg_compound_drop_live_result_t SG_CompoundDropLiveAuthorizeTouch(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose, int frame_serial)
{
	sg_compound_drop_live_result_t sweep;

	/* The body can remain inside the automatic trigger while the door opens and
	 * during the first suffix commands.  Refuse those later host callbacks
	 * without changing the one authenticated TOUCH/ACTIVATE transaction or
	 * allowing door_use to run again. */
	if (state && pose && state->guard_owned && frame_serial >= 0 &&
	    CompoundDropLivePoseValid(pose) &&
	    CompoundDropLiveRepeatedTriggerPhase(state) &&
	    trigger_key == state->snapshot.trigger_key)
	{
		if (CompoundDropLiveAuthorized(state, host) !=
		    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
			    SG_REPLAY_REASON_INVALID_STATE);
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_WAIT,
		    SG_COMPOUND_DROP_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE, false);
	}

	if (!state || !pose || !state->guard_owned || frame_serial < 0 ||
	    !CompoundDropLivePoseValid(pose) ||
	    state->outer.phase != SG_COMPOUND_APPROACH ||
	    state->replay_kind != SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH ||
	    !state->command_pending || state->zero_command_pending ||
	    state->transaction_elapsed_ms + SG_REPLAY_STEP_MS !=
	        state->snapshot.binding.touch_ms ||
	    trigger_key != state->snapshot.trigger_key ||
	    !CompoundDropLivePoseAtAnchor(pose,
	        state->snapshot.binding.link.mechanism_anchor))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (CompoundDropLiveAuthorized(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_INVALID_STATE);
	/* Touch/door_use can mutate the mover synchronously inside ClientThink.
	 * Prove the just-executed approach chord before publishing TOUCH. */
	sweep = CompoundDropLiveSweepStep(state, host, pose);
	if (sweep.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING &&
	    sweep.failure != SG_COMPOUND_DROP_LIVE_FAILURE_NONE)
		return sweep;
	VectorCopy(pose->origin, state->command_segment_end);
	state->command_segment_checked = true;
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_TOUCH))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_INVALID_STATE);
	state->touch_frame_serial = frame_serial;
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t SG_CompoundDropLiveAuthorizeActivation(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial)
{
	if (!state || !state->guard_owned ||
	    state->outer.phase != SG_COMPOUND_TOUCHED ||
	    trigger_key != state->snapshot.trigger_key ||
	    mover_key != state->snapshot.mover_key ||
	    frame_serial < 0 || frame_serial != state->touch_frame_serial)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (CompoundDropLiveCurrent(state, host) !=
	        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED ||
	    host->activate(host->context, &state->snapshot) !=
	        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED ||
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ACTIVATE))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t SG_CompoundDropLivePostStep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_drop_live_result_t live_result;
	sg_compound_drop_live_result_t sweep;
	int next_elapsed_ms;
	qboolean elapsed_valid;
	qboolean pose_valid;
	qboolean observation_valid;
	qboolean aborted;

	live_result.outcome = SG_DROP_LIVE_RUNNING;

	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->command_pending || !CompoundDropLiveHostValid(host))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	elapsed_valid = CompoundDropLiveAdvanceTime(
		state->transaction_elapsed_ms, &next_elapsed_ms);
	/* The fourth 25 ms command is observed only after the intervening mover
	 * entity pass.  This applies equally to replay and literal zero commands. */
	if (elapsed_valid && next_elapsed_ms % SG_REPLAY_FRAME_MS == 0)
		return CompoundDropLiveActiveResult(state, false);
	aborted = state->aborted_command_pending;
	pose_valid = CompoundDropLivePoseValid(pose);
	observation_valid = CompoundDropLiveObservationValid(observation);
	if (!aborted && CompoundDropLiveAuthorized(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!state->zero_command_pending &&
	    state->replay_kind != SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH)
	{
		if (!CompoundDropLiveEventsFromObservation(observation,
		        &state->drop_events))
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_ARGUMENT);
		live_result = SG_DropLivePostStep(&state->replay,
		    &state->drop_active, &state->drop_link,
		    (int)state->snapshot.binding.link_index, pose,
		    observation->ground_support_valid, &state->drop_events);
	}
	state->command_pending = false;
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	if (elapsed_valid)
		state->transaction_elapsed_ms = next_elapsed_ms;
	if (pose_valid)
	{
		sweep = CompoundDropLiveConsumeSweep(state, host, pose);
		state->command_segment_checked = false;
		if (sweep.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING &&
		    sweep.failure != SG_COMPOUND_DROP_LIVE_FAILURE_NONE)
			return sweep;
	}
	else
	{
		/* The command has been consumed even though its endpoint is
		 * unusable.  Do not let a touch-time chord certificate leak into
		 * the retained recovery command that follows. */
		state->command_segment_checked = false;
	}
	if (!elapsed_valid)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
		    SG_REPLAY_REASON_ACTION_TIMEOUT);
	if (!pose_valid)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_NONFINITE_POSE);
	if (!observation_valid)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_NONE &&
	    observation->contaminated)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_CONTAMINATED);
	if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_NONE &&
	    observation->door_passed)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_DOOR_PASSED);
	if (aborted)
	{
		SG_DropLiveDeactivate(&state->replay, &state->drop_active,
		    &state->drop_link);
		state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
		return CompoundDropLiveActiveResult(state, false);
	}
	if (live_result.outcome != SG_DROP_LIVE_RUNNING)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    live_result.replay_reason);
	if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH &&
	    state->outer.phase == SG_COMPOUND_OPENING)
	{
		state->zero_frame_old_z = pose->velocity[2];
		state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
	}
	if (state->outer.phase == SG_COMPOUND_APPROACH &&
	    state->transaction_elapsed_ms >= state->snapshot.binding.touch_ms)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (state->outer.phase == SG_COMPOUND_TOUCHED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundDropLiveActiveResult(state, false);
}
