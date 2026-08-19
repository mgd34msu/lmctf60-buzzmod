/* sg_compound_swim_live.c -- see sg_compound_swim_live.h. */
#include "q_shared.h"

#include <limits.h>
#include <stdint.h>

#include "slipgate/sg_compound_swim_live.h"

static sg_compound_swim_live_result_t CompoundSwimLiveResult(
	sg_compound_swim_live_outcome_t outcome,
	sg_compound_swim_live_failure_t failure,
	sg_replay_reason_t replay_reason, qboolean command_ready)
{
	sg_compound_swim_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = replay_reason;
	result.command_ready = command_ready;
	return result;
}

static qboolean CompoundSwimLiveHostValid(
	const sg_compound_swim_live_host_t *host)
{
	return host && host->bind && host->source_checkpoint &&
	       host->suffix_checkpoint && host->acquire && host->authorize &&
	       host->at_top && host->hold_open && host->outside_sweep &&
	       host->sweep_segment_clear && host->prove_suffix && host->release;
}

static qboolean CompoundSwimLiveFinite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static qboolean CompoundSwimLiveBoolValid(qboolean value)
{
	return value == false || value == true;
}

static qboolean CompoundSwimLivePoseValid(const sg_replay_pose_t *pose)
{
	return pose && CompoundSwimLiveFinite3(pose->origin) &&
	       CompoundSwimLiveFinite3(pose->velocity) &&
	       CompoundSwimLiveBoolValid(pose->grounded) &&
	       pose->waterlevel >= 0 && pose->waterlevel <= 3;
}

static qboolean CompoundSwimLiveObservationValid(
	const sg_replay_observation_t *observation)
{
	return observation &&
	       CompoundSwimLiveBoolValid(observation->contact_clear) &&
	       CompoundSwimLiveBoolValid(observation->ground_support_valid) &&
	       CompoundSwimLiveBoolValid(
	           observation->drop_arrival_contact_clear) &&
	       CompoundSwimLiveBoolValid(
	           observation->drop_recovery_contact_clear) &&
	       CompoundSwimLiveBoolValid(observation->drop_recovery_admitted) &&
	       CompoundSwimLiveBoolValid(observation->drop_landing_observed) &&
	       CompoundSwimLiveBoolValid(observation->contaminated) &&
	       CompoundSwimLiveBoolValid(observation->door_passed) &&
	       CompoundSwimLiveBoolValid(observation->hook_rope_valid) &&
	       (!observation->hook_rope_valid ||
	        observation->hook_rope_length >= 0);
}

static qboolean CompoundSwimLiveZero3(const float value[3])
{
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };

	return value && memcmp(value, zero, sizeof(zero)) == 0;
}

static qboolean CompoundSwimLiveLattice3(const float value[3])
{
	int axis;

	if (!CompoundSwimLiveFinite3(value))
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

static qboolean CompoundSwimLivePlanValid(
	const sg_compound_swim_live_snapshot_t *snapshot, uint32_t link_index)
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
	    link->from < 0 || link->to < 0 || link->action != RL_DOOR_SWIM ||
	    link->provenance != RL_CONTRACTED || link->mode != RLCM_PREOPEN ||
	    link->min_speed != 0 || link->heading != 0 ||
	    link->heading_slack != 0 || !CompoundSwimLiveZero3(link->anchor) ||
	    !CompoundSwimLiveLattice3(link->mechanism_anchor) ||
	    !CompoundSwimLiveLattice3(binding->canonical_hint) ||
	    !CompoundSwimLiveFinite3(binding->source_seed.origin) ||
	    !CompoundSwimLiveFinite3(binding->destination_seed.origin) ||
	    (binding->source_seed.flags & RSF_WATER) == 0 ||
	    (binding->source_seed.flags & ~RSF_WATER) != 0 ||
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
	    binding->arrival_ms >= SG_REPLAY_SWIM_LIMIT_MS ||
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

static qboolean CompoundSwimLiveProofValid(
	const sg_compound_swim_live_proof_t *proof)
{
	return proof && proof->arrival_ms > 0 &&
	       proof->arrival_ms < SG_REPLAY_SWIM_LIMIT_MS &&
	       proof->arrival_ms % SG_REPLAY_FRAME_MS == 0 &&
	       proof->sweep_clear_ms > 0 &&
	       proof->sweep_clear_ms <= proof->arrival_ms &&
	       proof->sweep_clear_ms % SG_REPLAY_FRAME_MS == 0;
}

static qboolean CompoundSwimLiveProofMatchesBinding(
	const sg_compound_swim_live_proof_t *proof,
	const sg_compound_publication_binding_t *binding)
{
	return CompoundSwimLiveProofValid(proof) && binding &&
	       proof->arrival_ms == binding->arrival_ms &&
	       proof->sweep_clear_ms == binding->sweep_clear_ms &&
	       proof->exit_speed == binding->link.exit_speed;
}

static qboolean CompoundSwimLiveSnapshotEqual(
	const sg_compound_swim_live_snapshot_t *first,
	const sg_compound_swim_live_snapshot_t *second)
{
	/* bind's contract requires a completely initialized copy of the immutable
	 * publication image, so padding bytes are stable identity bytes too. */
	return first && second && memcmp(first, second, sizeof(*first)) == 0;
}

static sg_compound_swim_live_host_result_t CompoundSwimLiveCurrent(
	const sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host)
{
	sg_compound_swim_live_snapshot_t current;
	sg_compound_swim_live_host_result_t result;

	if (!state || !CompoundSwimLiveHostValid(host))
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	memset(&current, 0, sizeof(current));
	result = host->bind(host->context, state->snapshot.binding.link_index,
	                    &current);
	if (result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return result;
	return CompoundSwimLiveSnapshotEqual(&current, &state->snapshot) ?
	       SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
}

static sg_compound_swim_live_host_result_t CompoundSwimLiveAuthorized(
	const sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host)
{
	if (CompoundSwimLiveCurrent(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	return host->authorize(host->context, &state->snapshot);
}

static sg_compound_swim_live_result_t CompoundSwimLiveOwnedFailure(
	sg_compound_swim_live_state_t *state,
	sg_compound_swim_live_failure_t failure,
	sg_replay_reason_t replay_reason)
{
	if (!state || !state->guard_owned ||
	    state->outer.phase == SG_COMPOUND_NONE)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    failure, replay_reason, false);
	if (state->outer.phase != SG_COMPOUND_RECOVER)
		(void)SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ABORT);
	state->failure = failure;
	state->replay_reason = replay_reason;
	state->recovering = true;
	state->aborted_command_pending = state->command_pending;
	if (!state->command_pending)
	{
		state->replay_kind = SG_COMPOUND_SWIM_LIVE_REPLAY_NONE;
		state->zero_command_pending = false;
	}
	state->sweep_clear = false;
	state->arrived = false;
	state->last_sweep_contact_ms = 0;
	return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
	    failure, replay_reason, false);
}

static sg_compound_swim_live_result_t CompoundSwimLiveActiveResult(
	const sg_compound_swim_live_state_t *state, qboolean command_ready)
{
	return CompoundSwimLiveResult(
	    state && state->recovering ? SG_COMPOUND_SWIM_LIVE_RECOVERING :
	                               SG_COMPOUND_SWIM_LIVE_RUNNING,
	    SG_COMPOUND_SWIM_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE,
	    command_ready);
}

static qboolean CompoundSwimLiveAdvanceTime(int elapsed_ms, int *next_ms)
{
	if (!next_ms || elapsed_ms < 0 ||
	    elapsed_ms > INT_MAX - SG_REPLAY_STEP_MS)
		return false;
	*next_ms = elapsed_ms + SG_REPLAY_STEP_MS;
	return true;
}

static sg_replay_observation_t CompoundSwimLiveApproachObservation(
	const sg_replay_observation_t *observation)
{
	sg_replay_observation_t copy;

	memset(&copy, 0, sizeof(copy));
	if (observation)
		copy = *observation;
	copy.contact_clear = false;
	return copy;
}

static qboolean CompoundSwimLivePoseAtAnchor(
	const sg_replay_pose_t *pose, const float anchor[3])
{
	int axis;

	if (!pose || !CompoundSwimLiveLattice3(anchor))
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

static qboolean CompoundSwimLiveBeginReplay(
	sg_compound_swim_live_state_t *state, const sg_replay_pose_t *pose,
	const vec3_t destination, qboolean destination_water,
	int expected_arrival_ms, float old_frame_z,
	sg_compound_swim_live_replay_t kind)
{
	sg_swim_replay_spec_t spec;
	sg_replay_observation_t observation;

	memset(&spec, 0, sizeof(spec));
	VectorCopy(destination, spec.destination);
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = expected_arrival_ms;
	memset(&observation, 0, sizeof(observation));
	observation.contact_clear = false;
	if (SG_SwimReplayBegin(&state->replay, &spec, pose, &observation,
	                       old_frame_z) != SG_REPLAY_RUNNING)
		return false;
	state->replay_kind = kind;
	state->last_sweep_contact_ms = 0;
	state->sweep_clear = false;
	state->arrived = false;
	return true;
}

typedef struct sg_compound_swim_live_delegate_s
{
	sg_compound_swim_live_state_t *state;
	const sg_replay_pose_t *pose;
	qboolean destination_water;
} sg_compound_swim_live_delegate_t;

static int CompoundSwimLiveDelegate(void *context, int link_index,
	int suffix_action)
{
	sg_compound_swim_live_delegate_t *delegate =
		(sg_compound_swim_live_delegate_t *)context;
	sg_compound_swim_live_state_t *state;

	if (!delegate || !delegate->state || !delegate->pose ||
	    suffix_action != RL_SWIM)
		return 0;
	state = delegate->state;
	if (link_index != (int)state->snapshot.binding.link_index)
		return 0;
	return CompoundSwimLiveBeginReplay(state, delegate->pose,
	    state->snapshot.binding.destination_seed.origin,
	    delegate->destination_water, state->proof.arrival_ms,
	    state->snapshot.binding.suffix.old_frame_z,
	    SG_COMPOUND_SWIM_LIVE_REPLAY_SUFFIX);
}

static sg_compound_swim_live_result_t CompoundSwimLiveFinishRelease(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	qboolean recovered)
{
	sg_compound_swim_live_host_result_t released;

	if (recovered && state->outer.phase == SG_COMPOUND_RECOVER &&
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RECOVERED))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!SG_CompoundReleaseReady(&state->outer))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_INVALID_STATE);
	released = host->release(host->context, &state->snapshot);
	if (released != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_NONE, false);
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RELEASED))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_INVALID_STATE);
	state->guard_owned = false;
	state->command_pending = false;
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	state->command_segment_checked = false;
	state->replay_kind = SG_COMPOUND_SWIM_LIVE_REPLAY_NONE;
	return CompoundSwimLiveResult(
	    recovered ? SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED :
	                SG_COMPOUND_SWIM_LIVE_COMPLETE,
	    SG_COMPOUND_SWIM_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE, false);
}

static sg_compound_swim_live_result_t CompoundSwimLiveSweepStep(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose)
{
	sg_compound_swim_live_host_result_t segment;

	segment = host->sweep_segment_clear(host->context, &state->snapshot,
	    state->command_origin, pose->origin);
	if (segment != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED &&
	    segment != SG_COMPOUND_SWIM_LIVE_HOST_DENIED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (segment == SG_COMPOUND_SWIM_LIVE_HOST_DENIED)
	{
		/* Approach and the pre-TOP zero frame are proved outside in the
		 * generator and may not touch the mover sweep.  Recovery padding can
		 * begin inside, while suffix/recovery replay records the exact last
		 * contact required by its published clearance time. */
		if (state->replay_kind != SG_COMPOUND_SWIM_LIVE_REPLAY_SUFFIX &&
		    state->replay_kind != SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY &&
		    state->outer.phase != SG_COMPOUND_RECOVER)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		if (state->sweep_clear)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		state->last_sweep_contact_ms = state->replay.progress.elapsed_ms;
	}
	return CompoundSwimLiveActiveResult(state, false);
}

static sg_compound_swim_live_result_t CompoundSwimLiveConsumeSweep(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose)
{
	if (state->command_segment_checked &&
	    memcmp(state->command_segment_end, pose->origin,
	           sizeof(state->command_segment_end)) == 0)
		return CompoundSwimLiveActiveResult(state, false);
	return CompoundSwimLiveSweepStep(state, host, pose);
}

const char *SG_CompoundSwimLiveFailureName(
	sg_compound_swim_live_failure_t failure)
{
	switch (failure)
	{
	case SG_COMPOUND_SWIM_LIVE_FAILURE_NONE: return "none";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT: return "argument";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_BINDING: return "binding";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_PLAN: return "plan";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_SOURCE_CHECKPOINT:
		return "source-checkpoint";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_ACQUIRE: return "acquire";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY: return "authority";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE: return "cadence";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH: return "touch";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_ACTIVATION: return "activation";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY: return "replay";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_TOP: return "top";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_HOLD: return "hold";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_SUFFIX_CHECKPOINT:
		return "suffix-checkpoint";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_REPROOF: return "reproof";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP: return "sweep";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING: return "timing";
	case SG_COMPOUND_SWIM_LIVE_FAILURE_RELEASE: return "release";
	default: return "unknown";
	}
}

sg_compound_swim_live_result_t SG_CompoundSwimLiveBegin(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose)
{
	sg_compound_swim_live_state_t candidate =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_host_result_t result;

	if (!state || !pose || !CompoundSwimLiveHostValid(host) ||
	    state->guard_owned || state->outer.phase != SG_COMPOUND_NONE)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	result = host->bind(host->context, link_index, &candidate.snapshot);
	if (result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(
		    result == SG_COMPOUND_SWIM_LIVE_HOST_DENIED ?
		        SG_COMPOUND_SWIM_LIVE_WAIT : SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_BINDING,
		    SG_REPLAY_REASON_NONE, false);
	if (!CompoundSwimLivePlanValid(&candidate.snapshot, link_index))
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_PLAN,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	result = host->source_checkpoint(host->context, &candidate.snapshot,
	                                 &candidate.angle_bias);
	if (result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_SOURCE_CHECKPOINT,
		    SG_REPLAY_REASON_NONE, false);
	if (!SG_CompoundBegin(&candidate.outer, (int)link_index,
	                      candidate.snapshot.mover_key, RL_DOOR_SWIM,
	                      RLCM_PREOPEN) ||
	    !SG_CompoundAdvance(&candidate.outer, SG_COMPOUND_EVENT_APPROACH) ||
	    !CompoundSwimLiveBeginReplay(&candidate, pose,
	        candidate.snapshot.binding.link.mechanism_anchor, true,
	        SG_REPLAY_TIME_DISCOVER,
	        candidate.snapshot.binding.source.old_frame_z,
	        SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH))
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    candidate.replay.progress.reason, false);
	result = host->acquire(host->context, &candidate.snapshot);
	if (result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(
		    result == SG_COMPOUND_SWIM_LIVE_HOST_DENIED ?
		        SG_COMPOUND_SWIM_LIVE_WAIT : SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ACQUIRE,
		    SG_REPLAY_REASON_NONE, false);
	candidate.guard_owned = true;
	candidate.zero_frame_old_z =
		candidate.snapshot.binding.source.old_frame_z;
	*state = candidate;
	return CompoundSwimLiveActiveResult(state, false);
}

sg_compound_swim_live_result_t SG_CompoundSwimLivePreStep(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	sg_replay_status_t status;

	if (command)
	{
		memset(command, 0, sizeof(*command));
		command->msec = SG_REPLAY_STEP_MS;
	}
	if (!state || !pose || !command || !state->guard_owned ||
	    !CompoundSwimLiveHostValid(host))
	{
		if (state && state->guard_owned)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
			    SG_REPLAY_REASON_INVALID_ARGUMENT);
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	}
	if (state->command_pending)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!CompoundSwimLivePoseValid(pose))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_NONFINITE_POSE);
	if (CompoundSwimLiveAuthorized(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	VectorCopy(pose->origin, state->command_origin);
	VectorClear(state->command_segment_end);
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	state->command_segment_checked = false;
	if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH ||
	    state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_SUFFIX ||
	    state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY)
	{
		status = SG_SwimReplayPreStep(&state->replay, pose, command);
		if (status != SG_REPLAY_RUNNING)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    state->replay.progress.reason);
	}
	else if (state->outer.phase == SG_COMPOUND_TOUCHED ||
	         state->outer.phase == SG_COMPOUND_OPENING ||
	         state->outer.phase == SG_COMPOUND_RECOVER)
	{
		state->zero_command_pending = true;
	}
	else
	{
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	}
	state->command_pending = true;
	return CompoundSwimLiveActiveResult(state, true);
}

sg_compound_swim_live_result_t SG_CompoundSwimLiveAuthorizeTouch(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose, int frame_serial)
{
	sg_compound_swim_live_result_t sweep;

	if (!state || !pose || !state->guard_owned || frame_serial < 0 ||
	    !CompoundSwimLivePoseValid(pose) ||
	    state->outer.phase != SG_COMPOUND_APPROACH ||
	    state->replay_kind != SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH ||
	    !state->command_pending || state->zero_command_pending ||
	    state->replay.progress.elapsed_ms + SG_REPLAY_STEP_MS !=
	        state->snapshot.binding.touch_ms ||
	    trigger_key != state->snapshot.trigger_key ||
	    !CompoundSwimLivePoseAtAnchor(pose,
	        state->snapshot.binding.link.mechanism_anchor))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (CompoundSwimLiveAuthorized(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_INVALID_STATE);
	/* Touch/door_use can mutate the mover synchronously inside ClientThink.
	 * Prove the just-executed approach chord before publishing TOUCH. */
	sweep = CompoundSwimLiveSweepStep(state, host, pose);
	if (sweep.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING &&
	    sweep.failure != SG_COMPOUND_SWIM_LIVE_FAILURE_NONE)
		return sweep;
	VectorCopy(pose->origin, state->command_segment_end);
	state->command_segment_checked = true;
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_TOUCH))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_INVALID_STATE);
	state->touch_frame_serial = frame_serial;
	return CompoundSwimLiveActiveResult(state, false);
}

sg_compound_swim_live_result_t SG_CompoundSwimLiveAuthorizeActivation(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial)
{
	if (!state || !state->guard_owned ||
	    state->outer.phase != SG_COMPOUND_TOUCHED ||
	    trigger_key != state->snapshot.trigger_key ||
	    mover_key != state->snapshot.mover_key ||
	    frame_serial < 0 || frame_serial != state->touch_frame_serial)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (CompoundSwimLiveAuthorized(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED ||
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_ACTIVATE))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundSwimLiveActiveResult(state, false);
}

sg_compound_swim_live_result_t SG_CompoundSwimLivePostStep(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_replay_observation_t replay_observation;
	sg_replay_status_t status = SG_REPLAY_RUNNING;
	sg_compound_swim_live_result_t sweep;
	int next_elapsed_ms;
	qboolean elapsed_valid;
	qboolean pose_valid;
	qboolean observation_valid;
	qboolean aborted;

	if (!state || !pose || !observation || !state->guard_owned ||
	    !state->command_pending || !CompoundSwimLiveHostValid(host))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	elapsed_valid = CompoundSwimLiveAdvanceTime(
		state->transaction_elapsed_ms, &next_elapsed_ms);
	/* The fourth 25 ms command is observed only after the intervening mover
	 * entity pass.  This applies equally to replay and literal zero commands. */
	if (elapsed_valid && next_elapsed_ms % SG_REPLAY_FRAME_MS == 0)
		return CompoundSwimLiveActiveResult(state, false);
	aborted = state->aborted_command_pending;
	pose_valid = CompoundSwimLivePoseValid(pose);
	observation_valid = CompoundSwimLiveObservationValid(observation);
	if (!aborted && CompoundSwimLiveAuthorized(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!state->zero_command_pending)
	{
		replay_observation =
			state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH ?
			CompoundSwimLiveApproachObservation(observation) : *observation;
		status = SG_SwimReplayPostStep(&state->replay, pose,
		                                 &replay_observation);
	}
	state->command_pending = false;
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	if (elapsed_valid)
		state->transaction_elapsed_ms = next_elapsed_ms;
	if (pose_valid)
	{
		sweep = CompoundSwimLiveConsumeSweep(state, host, pose);
		state->command_segment_checked = false;
		if (sweep.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING &&
		    sweep.failure != SG_COMPOUND_SWIM_LIVE_FAILURE_NONE)
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
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
		    SG_REPLAY_REASON_ACTION_TIMEOUT);
	if (!pose_valid)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_NONFINITE_POSE);
	if (!observation_valid)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_INVALID_ARGUMENT);
	if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_NONE &&
	    observation->contaminated)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_CONTAMINATED);
	if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_NONE &&
	    observation->door_passed)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    SG_REPLAY_REASON_DOOR_PASSED);
	if (aborted)
	{
		state->replay_kind = SG_COMPOUND_SWIM_LIVE_REPLAY_NONE;
		return CompoundSwimLiveActiveResult(state, false);
	}
	if (status != SG_REPLAY_RUNNING)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    state->replay.progress.reason);
	if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH &&
	    state->outer.phase == SG_COMPOUND_OPENING)
	{
		state->zero_frame_old_z = state->replay.progress.old_frame_z;
		state->replay_kind = SG_COMPOUND_SWIM_LIVE_REPLAY_NONE;
	}
	if (state->outer.phase == SG_COMPOUND_APPROACH &&
	    state->transaction_elapsed_ms >= state->snapshot.binding.touch_ms)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (state->outer.phase == SG_COMPOUND_TOUCHED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundSwimLiveActiveResult(state, false);
}

static sg_compound_swim_live_result_t CompoundSwimLiveStartSuffix(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose)
{
	sg_compound_swim_live_delegate_t delegate;
	sg_compound_swim_live_host_result_t result;
	qboolean destination_water;

	if (host->at_top(host->context, &state->snapshot) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (host->suffix_checkpoint(host->context, &state->snapshot,
	                            &state->angle_bias) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_SUFFIX_CHECKPOINT,
		    SG_REPLAY_REASON_INVALID_STATE);
	memset(&state->proof, 0, sizeof(state->proof));
	result = host->prove_suffix(host->context, &state->snapshot, pose, false,
	                            &state->proof);
	if (result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED ||
	    !CompoundSwimLiveProofMatchesBinding(&state->proof,
	                                         &state->snapshot.binding))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPROOF,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (host->outside_sweep(host->context, &state->snapshot, pose) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE);
	destination_water =
		(state->snapshot.binding.destination_seed.flags & RSF_WATER) != 0;
	delegate.state = state;
	delegate.pose = pose;
	delegate.destination_water = destination_water;
	if (!SG_CompoundDelegateSuffix(&state->outer,
	        (int)state->snapshot.binding.link_index,
	        state->snapshot.mover_key, CompoundSwimLiveDelegate, &delegate))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    state->replay.progress.reason);
	return CompoundSwimLiveActiveResult(state, false);
}

static sg_compound_swim_live_result_t CompoundSwimLiveSuffixBoundary(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose, sg_replay_status_t status)
{
	sg_compound_swim_live_host_result_t outside;
	int elapsed = state->replay.progress.elapsed_ms;
	qboolean recovered =
		state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY;

	outside = host->outside_sweep(host->context, &state->snapshot, pose);
	if (outside != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED &&
	    outside != SG_COMPOUND_SWIM_LIVE_HOST_DENIED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!state->sweep_clear)
	{
		if (elapsed < state->proof.sweep_clear_ms)
		{
			if (state->last_sweep_contact_ms > 0 &&
			    outside == SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
				return CompoundSwimLiveOwnedFailure(state,
				    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
				    SG_REPLAY_REASON_TIMING_MISMATCH);
		}
		else if (elapsed == state->proof.sweep_clear_ms &&
		         state->last_sweep_contact_ms > 0 &&
		         outside == SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		{
			state->sweep_clear = true;
			if (!recovered && !SG_CompoundAdvance(&state->outer,
			        SG_COMPOUND_EVENT_SWEEP_CLEAR))
				return CompoundSwimLiveOwnedFailure(state,
				    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
				    SG_REPLAY_REASON_INVALID_STATE);
		}
		else if (elapsed >= state->proof.sweep_clear_ms)
		{
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		}
	}
	else if (outside != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
	{
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	}
	if (status == SG_REPLAY_ARRIVED)
	{
		if (state->replay.progress.exit_speed != state->proof.exit_speed)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPROOF,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		state->arrived = true;
		if (!recovered && !SG_CompoundAdvance(&state->outer,
		        SG_COMPOUND_EVENT_ARRIVED))
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (state->sweep_clear && state->arrived)
	{
		if (outside != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
		if (!recovered && state->transaction_elapsed_ms !=
		    state->snapshot.binding.total_cost_ms)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		return CompoundSwimLiveFinishRelease(state, host, recovered);
	}
	if (SG_CompoundSuffixNeedsHold(elapsed, state->proof.sweep_clear_ms) &&
	    host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	        SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundSwimLiveActiveResult(state, false);
}

sg_compound_swim_live_result_t SG_CompoundSwimLiveBoundary(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_replay_observation_t replay_observation;
	sg_replay_status_t status = SG_REPLAY_RUNNING;
	sg_compound_swim_live_result_t sweep;
	int boundary_ms;
	qboolean elapsed_valid = true;
	qboolean observation_valid;
	qboolean pose_valid;
	qboolean aborted;

	if (!state || !pose || !observation || !state->guard_owned ||
	    !CompoundSwimLiveHostValid(host))
	{
		if (state && state->guard_owned)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
			    SG_REPLAY_REASON_INVALID_ARGUMENT);
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	}
	boundary_ms = state->transaction_elapsed_ms;
	if (state->command_pending)
	{
		elapsed_valid = CompoundSwimLiveAdvanceTime(
			state->transaction_elapsed_ms, &boundary_ms);
		if (elapsed_valid && boundary_ms % SG_REPLAY_FRAME_MS != 0)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (elapsed_valid &&
	    (boundary_ms <= 0 || boundary_ms % SG_REPLAY_FRAME_MS != 0 ||
	     boundary_ms == state->last_boundary_ms))
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	aborted = state->aborted_command_pending;
	pose_valid = CompoundSwimLivePoseValid(pose);
	observation_valid = CompoundSwimLiveObservationValid(observation);
	if (!aborted && CompoundSwimLiveAuthorized(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (state->command_pending)
	{
		if (!state->zero_command_pending)
		{
			replay_observation =
				state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH ?
				CompoundSwimLiveApproachObservation(observation) : *observation;
			status = SG_SwimReplayPostStep(&state->replay, pose,
			                                 &replay_observation);
		}
		state->command_pending = false;
		state->zero_command_pending = false;
		state->aborted_command_pending = false;
		if (elapsed_valid)
			state->transaction_elapsed_ms = boundary_ms;
		if (pose_valid)
		{
			sweep = CompoundSwimLiveConsumeSweep(state, host, pose);
			state->command_segment_checked = false;
			if (sweep.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING &&
			    sweep.failure != SG_COMPOUND_SWIM_LIVE_FAILURE_NONE)
				return sweep;
		}
		else
		{
			/* This pending command is now consumed.  Its earlier endpoint
			 * certificate cannot authorize a later recovery chord. */
			state->command_segment_checked = false;
		}
		if (!elapsed_valid)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_ACTION_TIMEOUT);
		if (!pose_valid)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_NONFINITE_POSE);
		if (!observation_valid)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_ARGUMENT);
		if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_NONE &&
		    observation->contaminated)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_CONTAMINATED);
		if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_NONE &&
		    observation->door_passed)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_DOOR_PASSED);
		if (aborted)
		{
			state->replay_kind = SG_COMPOUND_SWIM_LIVE_REPLAY_NONE;
			status = SG_REPLAY_RUNNING;
		}
		else if (status == SG_REPLAY_FAILED)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    state->replay.progress.reason);
		if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_APPROACH &&
		    state->outer.phase == SG_COMPOUND_OPENING)
		{
			state->zero_frame_old_z = state->replay.progress.old_frame_z;
			state->replay_kind = SG_COMPOUND_SWIM_LIVE_REPLAY_NONE;
		}
	}
	if (!elapsed_valid)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
		    SG_REPLAY_REASON_ACTION_TIMEOUT);
	if (!pose_valid || !observation_valid)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    !pose_valid ? SG_REPLAY_REASON_NONFINITE_POSE :
		                  SG_REPLAY_REASON_INVALID_ARGUMENT);
	state->last_boundary_ms = boundary_ms;
	if (state->outer.phase == SG_COMPOUND_APPROACH)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (state->outer.phase == SG_COMPOUND_TOUCHED)
		return CompoundSwimLiveOwnedFailure(state,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (state->outer.phase == SG_COMPOUND_OPENING)
	{
		if (pose->waterlevel > 0 &&
		    (pose->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_HAZARDOUS_LIQUID);
		if (SG_ReplayFallDelta(state->zero_frame_old_z, pose->velocity[2],
		                       pose->grounded, pose->waterlevel) >
		    SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_DAMAGING_FALL);
		state->zero_frame_old_z = pose->velocity[2];
		if (boundary_ms == state->snapshot.binding.touch_frame_end_ms +
		                   state->snapshot.binding.suffix_start_ms)
		{
			if (!SG_CompoundAdvance(&state->outer,
			                        SG_COMPOUND_EVENT_TOP))
				return CompoundSwimLiveOwnedFailure(state,
				    SG_COMPOUND_SWIM_LIVE_FAILURE_TOP,
				    SG_REPLAY_REASON_INVALID_STATE);
			return CompoundSwimLiveStartSuffix(state, host, pose);
		}
		if (boundary_ms > state->snapshot.binding.touch_frame_end_ms +
		                  state->snapshot.binding.suffix_start_ms)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		return CompoundSwimLiveActiveResult(state, false);
	}
	if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_SUFFIX ||
	    state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY)
		return CompoundSwimLiveSuffixBoundary(state, host, pose, status);
	return CompoundSwimLiveActiveResult(state, false);
}

sg_compound_swim_live_result_t SG_CompoundSwimLiveRecover(
	sg_compound_swim_live_state_t *state,
	const sg_compound_swim_live_host_t *host,
	const sg_replay_pose_t *pose, float old_frame_z)
{
	sg_compound_swim_live_host_result_t outside, proof_result;
	qboolean destination_water;

	if (!state || !pose || !state->guard_owned ||
	    !CompoundSwimLiveHostValid(host) || !isfinite(old_frame_z) ||
	    !CompoundSwimLivePoseValid(pose) ||
	    (state->outer.phase != SG_COMPOUND_RECOVER &&
	     state->outer.phase != SG_COMPOUND_RELEASE_READY))
	{
		if (state && state->guard_owned)
			return CompoundSwimLiveOwnedFailure(state,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
			    SG_REPLAY_REASON_INVALID_ARGUMENT);
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_REJECTED,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_ARGUMENT,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	}
	if (state->command_pending)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (CompoundSwimLiveAuthorized(state, host) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->outer.phase == SG_COMPOUND_RELEASE_READY)
	{
		if (host->at_top(host->context, &state->snapshot) !=
		        SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED ||
		    host->hold_open(host->context, &state->snapshot,
		                    SG_COMPOUND_HOLD_LEASE_MS) !=
		        SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
			return CompoundSwimLiveResult(
			    SG_COMPOUND_SWIM_LIVE_RECOVERING,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE, false);
		/* A failed release leaves the lease in RELEASE_READY, but its earlier
		 * outside certificate describes the pose from that failed attempt only.
		 * Re-observe the caller's current pose after renewing the hold and
		 * immediately before retrying the ownership mutation. */
		outside = host->outside_sweep(host->context, &state->snapshot, pose);
		if (outside != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
			return CompoundSwimLiveResult(
			    SG_COMPOUND_SWIM_LIVE_RECOVERING,
			    SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE, false);
		return CompoundSwimLiveFinishRelease(state, host,
		                                    state->recovering);
	}
	if (state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY)
		return CompoundSwimLiveActiveResult(state, false);
	outside = host->outside_sweep(host->context, &state->snapshot, pose);
	if (outside == SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveFinishRelease(state, host, true);
	if (state->transaction_elapsed_ms % SG_REPLAY_FRAME_MS != 0 ||
	    (state->transaction_elapsed_ms > 0 &&
	     state->last_boundary_ms != state->transaction_elapsed_ms))
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (outside != SG_COMPOUND_SWIM_LIVE_HOST_DENIED ||
	    host->at_top(host->context, &state->snapshot) !=
	        SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_TOP,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	memset(&state->proof, 0, sizeof(state->proof));
	proof_result = host->prove_suffix(host->context, &state->snapshot, pose,
	                                  true, &state->proof);
	if (proof_result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED ||
	    !CompoundSwimLiveProofValid(&state->proof))
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPROOF,
		    SG_REPLAY_REASON_NONE, false);
	destination_water =
		(state->snapshot.binding.destination_seed.flags & RSF_WATER) != 0;
	if (!CompoundSwimLiveBeginReplay(state, pose,
	        state->snapshot.binding.destination_seed.origin,
	        destination_water, state->proof.arrival_ms, old_frame_z,
	        SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY))
		return CompoundSwimLiveResult(SG_COMPOUND_SWIM_LIVE_RECOVERING,
		    SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY,
		    state->replay.progress.reason, false);
	return CompoundSwimLiveActiveResult(state, false);
}

qboolean SG_CompoundSwimLiveOwns(
	const sg_compound_swim_live_state_t *state, uint32_t link_index,
	int mover_key)
{
	return state && state->guard_owned && link_index <= (uint32_t)INT_MAX &&
	       SG_CompoundOwns(&state->outer, (int)link_index, mover_key);
}
