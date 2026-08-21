#include "q_shared.h"

#include <limits.h>
#include <stdint.h>

#include "slipgate/sg_compound_drop_live.h"
#include "slipgate/sg_compound_drop_live_internal.h"

typedef struct sg_compound_drop_live_contact_s
{
	const sg_replay_observation_t *observation;
} sg_compound_drop_live_contact_t;

typedef struct sg_compound_drop_live_delegate_s
{
	sg_compound_drop_live_state_t *state;
	const sg_compound_drop_live_host_t *host;
	const sg_replay_pose_t *pose;
	qboolean destination_water;
} sg_compound_drop_live_delegate_t;

static qboolean CompoundDropLiveArrival(
	const sg_drop_replay_spec_t *spec, const sg_replay_pose_t *pose,
	void *context)
{
	const sg_compound_drop_live_contact_t *contact =
		(const sg_compound_drop_live_contact_t *)context;

	return spec && pose && contact && contact->observation &&
	       contact->observation->drop_arrival_contact_clear;
}

static qboolean CompoundDropLiveRecovery(
	const sg_drop_replay_spec_t *spec, const sg_replay_pose_t *pose,
	void *context)
{
	const sg_compound_drop_live_contact_t *contact =
		(const sg_compound_drop_live_contact_t *)context;

	return spec && pose && contact && contact->observation &&
	       contact->observation->drop_recovery_contact_clear;
}

static qboolean CompoundDropLiveProofValid(
	const sg_compound_drop_live_proof_t *proof)
{
	return proof && proof->arrival_ms > 0 &&
	       proof->arrival_ms < SG_REPLAY_DROP_TOTAL_MS &&
	       proof->arrival_ms % SG_REPLAY_FRAME_MS == 0 &&
	       proof->sweep_clear_ms > 0 &&
	       proof->sweep_clear_ms <= proof->arrival_ms &&
	       proof->sweep_clear_ms % SG_REPLAY_FRAME_MS == 0;
}

static qboolean CompoundDropLiveProofMatchesBinding(
	const sg_compound_drop_live_proof_t *proof,
	const sg_compound_publication_binding_t *binding)
{
	return CompoundDropLiveProofValid(proof) && binding &&
	       proof->arrival_ms == binding->arrival_ms &&
	       proof->sweep_clear_ms == binding->sweep_clear_ms &&
	       proof->exit_speed == binding->link.exit_speed;
}

static qboolean CompoundDropLiveBeginReplay(
	sg_compound_drop_live_state_t *state, const sg_replay_pose_t *pose,
	const vec3_t destination, qboolean destination_water,
	int expected_arrival_ms, float old_frame_z,
	sg_compound_drop_live_replay_t kind, qboolean ground_support_valid)
{
	sg_drop_live_result_t result;
	if (!state || !pose ||
	    state->snapshot.binding.link_index > (uint32_t)INT_MAX ||
	    state->drop_link != -1)
		return false;
	if (kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY)
		memset(&state->drop_events, 0, sizeof(state->drop_events));
	result = SG_DropLiveBegin(&state->replay, &state->drop_active,
	    &state->drop_link, (int)state->snapshot.binding.link_index,
	    destination, state->snapshot.binding.link.anchor,
	    state->snapshot.binding.link.heading, destination_water,
	    expected_arrival_ms, pose, ground_support_valid, old_frame_z,
	    &state->drop_events);
	if (result.outcome != SG_DROP_LIVE_RUNNING)
		return false;
	state->replay_kind = kind;
	state->last_sweep_contact_ms = 0;
	state->sweep_clear = false;
	state->arrived = false;
	return true;
}

static int CompoundDropLiveDelegate(void *context, int link_index,
	int suffix_action)
{
	sg_compound_drop_live_delegate_t *delegate =
		(sg_compound_drop_live_delegate_t *)context;
	sg_compound_drop_live_state_t *state;

	if (!delegate || !delegate->state || !delegate->pose ||
	    suffix_action != RL_DROP)
		return 0;
	state = delegate->state;
	if (link_index != (int)state->snapshot.binding.link_index)
		return 0;
	return CompoundDropLiveBeginReplay(state, delegate->pose,
	    state->snapshot.binding.destination_seed.origin,
	    delegate->destination_water, state->proof.arrival_ms,
	    state->snapshot.binding.suffix.old_frame_z,
	    SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX,
	    delegate->host->ground_support(delegate->host->context,
	        &state->snapshot, delegate->pose) ==
	        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED);
}

static void CompoundDropLiveClearTerminalFlags(
	sg_compound_drop_live_state_t *state)
{
	state->recovering = false;
	state->failure = SG_COMPOUND_DROP_LIVE_FAILURE_NONE;
	state->replay_reason = SG_REPLAY_REASON_NONE;
	state->sweep_clear = false;
	state->arrived = false;
	state->last_sweep_contact_ms = 0;
}

static sg_compound_drop_live_result_t CompoundDropLiveFinishRelease(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	qboolean recovered)
{
	sg_compound_drop_live_host_result_t released;

	if (recovered && state->outer.phase == SG_COMPOUND_RECOVER &&
	    !SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RECOVERED))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!SG_CompoundReleaseReady(&state->outer))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_INVALID_STATE);
	released = host->release(host->context, &state->snapshot);
	if (released != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_NONE, false);
	if (!SG_CompoundAdvance(&state->outer, SG_COMPOUND_EVENT_RELEASED))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_RELEASE,
		    SG_REPLAY_REASON_INVALID_STATE);
	state->guard_owned = false;
	state->command_pending = false;
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	state->command_segment_checked = false;
	SG_DropLiveDeactivate(&state->replay, &state->drop_active,
	    &state->drop_link);
	state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
	CompoundDropLiveClearTerminalFlags(state);
	return CompoundDropLiveResult(
	    recovered ? SG_COMPOUND_DROP_LIVE_SAFE_STOPPED :
	                SG_COMPOUND_DROP_LIVE_COMPLETE,
	    SG_COMPOUND_DROP_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE, false);
}

static sg_compound_drop_live_result_t CompoundDropLiveStartSuffix(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose)
{
	sg_compound_drop_live_delegate_t delegate;
	sg_compound_drop_live_host_result_t result;
	qboolean destination_water;

	if (host->at_top(host->context, &state->snapshot) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (host->suffix_checkpoint(host->context, &state->snapshot,
	                            &state->angle_bias) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_SUFFIX_CHECKPOINT,
		    SG_REPLAY_REASON_INVALID_STATE);
	memset(&state->proof, 0, sizeof(state->proof));
	result = host->prove_suffix(host->context, &state->snapshot, pose, false,
	                            &state->proof);
	if (result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED ||
	    !CompoundDropLiveProofMatchesBinding(&state->proof,
	                                         &state->snapshot.binding))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	if (host->outside_sweep(host->context, &state->snapshot, pose) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE);
	destination_water =
		(state->snapshot.binding.destination_seed.flags & RSF_WATER) != 0;
	delegate.state = state;
	delegate.host = host;
	delegate.pose = pose;
	delegate.destination_water = destination_water;
	if (!SG_CompoundDelegateSuffix(&state->outer,
	        (int)state->snapshot.binding.link_index,
	        state->snapshot.mover_key, CompoundDropLiveDelegate, &delegate))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    state->replay.progress.reason);
	return CompoundDropLiveActiveResult(state, false);
}

static sg_compound_drop_live_result_t CompoundDropLiveSuffixBoundary(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, sg_replay_status_t status)
{
	sg_compound_drop_live_host_result_t outside;
	int elapsed = state->replay.progress.elapsed_ms;
	qboolean recovered =
		state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY;

	outside = host->outside_sweep(host->context, &state->snapshot, pose);
	if (outside != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED &&
	    outside != SG_COMPOUND_DROP_LIVE_HOST_DENIED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (!state->sweep_clear)
	{
		if (elapsed < state->proof.sweep_clear_ms)
		{
			if (state->last_sweep_contact_ms > 0 &&
			    outside == SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
				return CompoundDropLiveOwnedFailure(state,
				    SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
				    SG_REPLAY_REASON_TIMING_MISMATCH);
		}
		else if (elapsed == state->proof.sweep_clear_ms &&
		         state->last_sweep_contact_ms > 0 &&
		         outside == SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		{
			state->sweep_clear = true;
			if (!recovered && !SG_CompoundAdvance(&state->outer,
			        SG_COMPOUND_EVENT_SWEEP_CLEAR))
				return CompoundDropLiveOwnedFailure(state,
				    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
				    SG_REPLAY_REASON_INVALID_STATE);
		}
		else if (elapsed >= state->proof.sweep_clear_ms)
		{
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		}
	}
	else if (outside != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
	{
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	}
	if (status == SG_REPLAY_ARRIVED)
	{
		if (state->replay.progress.exit_speed != state->proof.exit_speed)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		state->arrived = true;
		if (!recovered && !SG_CompoundAdvance(&state->outer,
		        SG_COMPOUND_EVENT_ARRIVED))
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (state->sweep_clear && state->arrived)
	{
		if (outside != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE);
		if (!recovered && state->transaction_elapsed_ms !=
		    state->snapshot.binding.total_cost_ms)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		return CompoundDropLiveFinishRelease(state, host, recovered);
	}
	if (SG_CompoundSuffixNeedsHold(elapsed, state->proof.sweep_clear_ms) &&
	    host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE);
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t SG_CompoundDropLiveBoundary(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_compound_drop_live_contact_t contact;
	sg_drop_live_result_t live_result;
	sg_replay_status_t status = SG_REPLAY_RUNNING;
	sg_compound_drop_live_result_t sweep;
	int boundary_ms;
	qboolean elapsed_valid = true;
	qboolean observation_valid;
	qboolean pose_valid;
	qboolean aborted;

	if (!state || !pose || !observation || !state->guard_owned ||
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
	boundary_ms = state->transaction_elapsed_ms;
	if (state->command_pending)
	{
		elapsed_valid = CompoundDropLiveAdvanceTime(
			state->transaction_elapsed_ms, &boundary_ms);
		if (elapsed_valid && boundary_ms % SG_REPLAY_FRAME_MS != 0)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
			    SG_REPLAY_REASON_INVALID_STATE);
	}
	if (elapsed_valid &&
	    (boundary_ms <= 0 || boundary_ms % SG_REPLAY_FRAME_MS != 0 ||
	     boundary_ms == state->last_boundary_ms))
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE);
	aborted = state->aborted_command_pending;
	pose_valid = CompoundDropLivePoseValid(pose);
	observation_valid = CompoundDropLiveObservationValid(observation);
	if (!aborted && CompoundDropLiveAuthorized(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (state->command_pending)
	{
		if (!state->zero_command_pending &&
		    state->replay_kind != SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH)
		{
			if (!CompoundDropLiveEventsFromObservation(observation,
			        &state->drop_events))
				return CompoundDropLiveOwnedFailure(state,
				    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
				    SG_REPLAY_REASON_INVALID_ARGUMENT);
			contact.observation = observation;
			live_result = SG_DropLiveBoundary(&state->replay,
			    &state->drop_active, &state->drop_link,
			    (int)state->snapshot.binding.link_index, pose,
			    observation->ground_support_valid, &state->drop_events,
			    CompoundDropLiveArrival, CompoundDropLiveRecovery,
			    &contact);
			if (live_result.outcome == SG_DROP_LIVE_ARRIVED)
				status = SG_REPLAY_ARRIVED;
			else if (live_result.outcome != SG_DROP_LIVE_RUNNING)
				status = SG_REPLAY_FAILED;
		}
		state->command_pending = false;
		state->zero_command_pending = false;
		state->aborted_command_pending = false;
		if (elapsed_valid)
			state->transaction_elapsed_ms = boundary_ms;
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
			/* This pending command is now consumed.  Its earlier endpoint
			 * certificate cannot authorize a later recovery chord. */
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
			SG_DropLiveDeactivate(&state->replay,
			    &state->drop_active, &state->drop_link);
			state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
			status = SG_REPLAY_RUNNING;
		}
		else if (status == SG_REPLAY_FAILED)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    live_result.replay_reason);
		if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH &&
		    state->outer.phase == SG_COMPOUND_OPENING)
		{
			state->zero_frame_old_z = pose->velocity[2];
			state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
		}
	}
	if (!elapsed_valid)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
		    SG_REPLAY_REASON_ACTION_TIMEOUT);
	if (!pose_valid || !observation_valid)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    !pose_valid ? SG_REPLAY_REASON_NONFINITE_POSE :
		                  SG_REPLAY_REASON_INVALID_ARGUMENT);
	state->last_boundary_ms = boundary_ms;
	if (state->outer.phase == SG_COMPOUND_APPROACH)
	{
		if (boundary_ms < state->snapshot.binding.touch_frame_end_ms)
			return CompoundDropLiveActiveResult(state, false);
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
		    SG_REPLAY_REASON_TIMING_MISMATCH);
	}
	if (state->outer.phase == SG_COMPOUND_TOUCHED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ACTIVATION,
		    SG_REPLAY_REASON_INVALID_STATE);
	if (state->outer.phase == SG_COMPOUND_OPENING)
	{
		if (pose->waterlevel > 0 &&
		    (pose->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_HAZARDOUS_LIQUID);
		if (SG_ReplayFallDelta(state->zero_frame_old_z, pose->velocity[2],
		                       pose->grounded, pose->waterlevel) >
		    SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
			    SG_REPLAY_REASON_DAMAGING_FALL);
		state->zero_frame_old_z = pose->velocity[2];
		if (boundary_ms == state->snapshot.binding.touch_frame_end_ms +
		                   state->snapshot.binding.suffix_start_ms)
		{
			if (!SG_CompoundAdvance(&state->outer,
			                        SG_COMPOUND_EVENT_TOP))
				return CompoundDropLiveOwnedFailure(state,
				    SG_COMPOUND_DROP_LIVE_FAILURE_TOP,
				    SG_REPLAY_REASON_INVALID_STATE);
			return CompoundDropLiveStartSuffix(state, host, pose);
		}
		if (boundary_ms > state->snapshot.binding.touch_frame_end_ms +
		                  state->snapshot.binding.suffix_start_ms)
			return CompoundDropLiveOwnedFailure(state,
			    SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
			    SG_REPLAY_REASON_TIMING_MISMATCH);
		return CompoundDropLiveActiveResult(state, false);
	}
	if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX ||
	    state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY)
		return CompoundDropLiveSuffixBoundary(state, host, pose, status);
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t SG_CompoundDropLiveRecover(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, float old_frame_z)
{
	sg_compound_drop_live_host_result_t outside, proof_result;
	qboolean destination_water;

	if (!state || !pose || !state->guard_owned ||
	    !CompoundDropLiveHostValid(host) || !isfinite(old_frame_z) ||
	    !CompoundDropLivePoseValid(pose) ||
	    (state->outer.phase != SG_COMPOUND_RECOVER &&
	     state->outer.phase != SG_COMPOUND_RELEASE_READY))
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
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (CompoundDropLiveAuthorized(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (state->outer.phase == SG_COMPOUND_RELEASE_READY)
	{
		if (host->at_top(host->context, &state->snapshot) !=
		        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED ||
		    host->hold_open(host->context, &state->snapshot,
		                    SG_COMPOUND_HOLD_LEASE_MS) !=
		        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
			return CompoundDropLiveResult(
			    SG_COMPOUND_DROP_LIVE_RECOVERING,
			    SG_COMPOUND_DROP_LIVE_FAILURE_HOLD,
			    SG_REPLAY_REASON_INVALID_STATE, false);
		/* A failed release leaves the lease in RELEASE_READY, but its earlier
		 * outside certificate describes the pose from that failed attempt only.
		 * Re-observe the caller's current pose after renewing the hold and
		 * immediately before retrying the ownership mutation. */
		outside = host->outside_sweep(host->context, &state->snapshot, pose);
		if (outside != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
			return CompoundDropLiveResult(
			    SG_COMPOUND_DROP_LIVE_RECOVERING,
			    SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
			    SG_REPLAY_REASON_INVALID_STATE, false);
		return CompoundDropLiveFinishRelease(state, host,
		                                    state->recovering);
	}
	if (state->replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY)
		return CompoundDropLiveActiveResult(state, false);
	outside = host->outside_sweep(host->context, &state->snapshot, pose);
	if (outside == SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveFinishRelease(state, host, true);
	if (state->transaction_elapsed_ms % SG_REPLAY_STEP_MS != 0 ||
	    (state->transaction_elapsed_ms % SG_REPLAY_FRAME_MS == 0 &&
	     state->transaction_elapsed_ms > 0 &&
	     state->last_boundary_ms != state->transaction_elapsed_ms))
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (outside != SG_COMPOUND_DROP_LIVE_HOST_DENIED ||
	    host->at_top(host->context, &state->snapshot) !=
	        SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_TOP,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	if (host->hold_open(host->context, &state->snapshot,
	                    SG_COMPOUND_HOLD_LEASE_MS) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_HOLD,
		    SG_REPLAY_REASON_INVALID_STATE, false);
	memset(&state->proof, 0, sizeof(state->proof));
	proof_result = host->prove_suffix(host->context, &state->snapshot, pose,
	                                  true, &state->proof);
	if (proof_result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED ||
	    !CompoundDropLiveProofValid(&state->proof))
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF,
		    SG_REPLAY_REASON_NONE, false);
	destination_water =
		(state->snapshot.binding.destination_seed.flags & RSF_WATER) != 0;
	if (!CompoundDropLiveBeginReplay(state, pose,
	        state->snapshot.binding.destination_seed.origin,
	        destination_water, state->proof.arrival_ms, old_frame_z,
	        SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY,
	        host->ground_support(host->context, &state->snapshot, pose) ==
	            SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED))
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_RECOVERING,
		    SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
		    state->replay.progress.reason, false);
	return CompoundDropLiveActiveResult(state, false);
}

sg_compound_drop_live_result_t SG_CompoundDropLiveOrphan(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int bolt_key)
{
	sg_compound_drop_live_host_result_t result;

	if (!state || !state->guard_owned || !CompoundDropLiveHostValid(host))
		return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_REJECTED,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ORPHAN,
		    SG_REPLAY_REASON_INVALID_ARGUMENT, false);
	if (CompoundDropLiveCurrent(state, host) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ORPHAN,
		    SG_REPLAY_REASON_INVALID_STATE);
	result = host->orphan(host->context, &state->snapshot, bolt_key);
	if (result != SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return CompoundDropLiveOwnedFailure(state,
		    SG_COMPOUND_DROP_LIVE_FAILURE_ORPHAN,
		    SG_REPLAY_REASON_INVALID_STATE);
	SG_DropLiveDeactivate(&state->replay, &state->drop_active,
	    &state->drop_link);
	state->guard_owned = false;
	state->command_pending = false;
	state->zero_command_pending = false;
	state->aborted_command_pending = false;
	state->command_segment_checked = false;
	state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
	memset(&state->outer, 0, sizeof(state->outer));
	CompoundDropLiveClearTerminalFlags(state);
	return CompoundDropLiveResult(SG_COMPOUND_DROP_LIVE_SAFE_STOPPED,
	    SG_COMPOUND_DROP_LIVE_FAILURE_NONE, SG_REPLAY_REASON_NONE, false);
}

qboolean SG_CompoundDropLiveOwns(
	const sg_compound_drop_live_state_t *state, uint32_t link_index,
	int mover_key)
{
	return state && state->guard_owned && link_index <= (uint32_t)INT_MAX &&
	       SG_CompoundOwns(&state->outer, (int)link_index, mover_key);
}
