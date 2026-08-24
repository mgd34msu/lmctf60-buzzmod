/* sg_timed_vault_transaction.c -- pure timed-vault transaction reducer. */
#include "sg_timed_vault_transaction.h"

#include <limits.h>
#include <string.h>

static int TimedVaultBoolean(uint8_t value)
{
	return value <= 1U;
}

static int TimedVaultKey(uint32_t key)
{
	return key != 0U && key != UINT32_MAX;
}

static int TimedVaultSpecValid(const sg_timed_vault_spec_t *spec)
{
	return spec && TimedVaultKey(spec->source_key) &&
	       TimedVaultKey(spec->short_relay_key) &&
	       TimedVaultKey(spec->restore_relay_key) &&
	       TimedVaultKey(spec->fanout_identity) &&
	       spec->source_key != spec->short_relay_key &&
	       spec->source_key != spec->restore_relay_key &&
	       spec->short_relay_key != spec->restore_relay_key &&
	       spec->dispatch_target_count != 0U &&
	       spec->device_target_count != 0U && spec->door_leaf_count == 2U;
}

static unsigned TimedVaultMechanismEventCount(
	const sg_timed_vault_observation_t *observation)
{
	return (unsigned)observation->touch_authenticated +
	       (unsigned)observation->dispatch_authenticated +
	       (unsigned)observation->short_relay_authenticated +
	       (unsigned)observation->restore_relay_authenticated;
}

static int TimedVaultObservationShapeValid(
	const sg_timed_vault_observation_t *observation)
{
	unsigned event_count;

	if (!observation || !TimedVaultBoolean(observation->alive) ||
	    !TimedVaultBoolean(observation->connected) ||
	    !TimedVaultBoolean(observation->binding_current) ||
	    !TimedVaultBoolean(observation->touch_authenticated) ||
	    !TimedVaultBoolean(observation->dispatch_authenticated) ||
	    !TimedVaultBoolean(observation->short_relay_authenticated) ||
	    !TimedVaultBoolean(observation->flag_pickup) ||
	    !TimedVaultBoolean(observation->restore_relay_authenticated) ||
	    !TimedVaultBoolean(observation->body_clear))
		return 0;
	event_count = TimedVaultMechanismEventCount(observation);
	if (event_count > 1U)
		return 0;
	if (event_count == 0U)
		return observation->event_source_key == 0U &&
		       observation->event_fanout_identity == 0U &&
		       observation->event_target_count == 0U;
	if (!TimedVaultKey(observation->event_source_key))
		return 0;
	if (observation->touch_authenticated)
		return observation->event_fanout_identity == 0U &&
		       observation->event_target_count == 0U;
	return TimedVaultKey(observation->event_fanout_identity);
}

static int TimedVaultFrameAdd(uint32_t frame, uint32_t amount,
	uint32_t *result)
{
	if (frame > UINT32_MAX - amount)
		return 0;
	*result = frame + amount;
	return 1;
}

static sg_timed_vault_reduction_t TimedVaultInvalid(void)
{
	sg_timed_vault_reduction_t reduction;

	memset(&reduction, 0, sizeof(reduction));
	reduction.state.phase = SG_TIMED_VAULT_PHASE_TERMINAL;
	reduction.state.outcome = SG_TIMED_VAULT_OUTCOME_FAILED;
	reduction.state.reason = SG_TIMED_VAULT_REASON_INVALID;
	return reduction;
}

static void TimedVaultFail(sg_timed_vault_state_t *state,
	sg_timed_vault_reason_t reason)
{
	if (state->outcome != SG_TIMED_VAULT_OUTCOME_FAILED)
	{
		state->outcome = SG_TIMED_VAULT_OUTCOME_FAILED;
		state->reason = reason;
	}
	if (state->restoration == SG_TIMED_VAULT_RESTORATION_NONE ||
	    state->restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED)
		state->phase = SG_TIMED_VAULT_PHASE_TERMINAL;
	else if (state->restoration == SG_TIMED_VAULT_RESTORATION_OBSERVED)
		state->phase = SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR;
	else
		state->phase = SG_TIMED_VAULT_PHASE_WAIT_RESTORE;
}

static int TimedVaultMechanismAuthenticated(
	sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation,
	uint32_t expected_source_key, uint16_t expected_target_count)
{
	if (observation->event_source_key != expected_source_key)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_SOURCE_DRIFT);
		return 0;
	}
	if (observation->event_fanout_identity != state->spec.fanout_identity)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_FANOUT_DRIFT);
		return 0;
	}
	if (observation->event_target_count != expected_target_count)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_PARTIAL_EVENT);
		return 0;
	}
	return 1;
}

static void TimedVaultCheckOwner(sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	if (!observation->alive)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_DEAD);
	else if (!observation->connected)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_DISCONNECTED);
	else if (!observation->binding_current)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_BINDING_DRIFT);
	else if (observation->binding_source_key != state->spec.source_key)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_SOURCE_DRIFT);
	else if (observation->binding_fanout_identity !=
	    state->spec.fanout_identity)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_FANOUT_DRIFT);
}

static void TimedVaultAcceptDispatch(sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	int dispatched_on_time;
	uint32_t ready_frame;
	uint32_t lease_deadline_frame;
	uint32_t restore_frame;

	if (state->dispatch_seen)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
		return;
	}
	if (observation->event_source_key != state->spec.source_key)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_SOURCE_DRIFT);
		return;
	}
	dispatched_on_time = observation->frame == state->dispatch_frame;
	state->dispatch_seen = 1U;
	state->restoration = SG_TIMED_VAULT_RESTORATION_REQUIRED;
	state->dispatch_frame = observation->frame;
	if (!TimedVaultFrameAdd(state->dispatch_frame,
	        SG_TIMED_VAULT_SHORT_RELAY_FRAMES, &ready_frame) ||
	    !TimedVaultFrameAdd(ready_frame,
	        SG_TIMED_VAULT_SAFE_LEASE_FRAMES, &lease_deadline_frame) ||
	    !TimedVaultFrameAdd(state->dispatch_frame,
	        SG_TIMED_VAULT_RESTORE_RELAY_FRAMES, &restore_frame))
	{
		state->ready_frame = UINT32_MAX;
		state->lease_deadline_frame = UINT32_MAX;
		state->restore_frame = UINT32_MAX;
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_FRAME_DRIFT);
		return;
	}
	state->ready_frame = ready_frame;
	state->lease_deadline_frame = lease_deadline_frame;
	state->restore_frame = restore_frame;
	if (!TimedVaultMechanismAuthenticated(state, observation,
	    state->spec.source_key, state->spec.dispatch_target_count))
		return;
	if (!dispatched_on_time)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	else if (state->outcome == SG_TIMED_VAULT_OUTCOME_PENDING)
		state->phase = SG_TIMED_VAULT_PHASE_WAIT_READY;
	else
		state->phase = SG_TIMED_VAULT_PHASE_WAIT_RESTORE;
}

static void TimedVaultAcceptShortRelay(sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	if (state->short_relay_seen)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
		return;
	}
	if (!state->dispatch_seen)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
		return;
	}
	if (!TimedVaultMechanismAuthenticated(state, observation,
	    state->spec.short_relay_key, state->spec.device_target_count))
		return;
	if (observation->door_top_count != state->spec.door_leaf_count)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_PARTIAL_EVENT);
		return;
	}
	state->short_relay_seen = 1U;
	if (observation->frame != state->ready_frame)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	else if (state->outcome == SG_TIMED_VAULT_OUTCOME_PENDING)
		state->phase = SG_TIMED_VAULT_PHASE_ACTIVE;
	else
		state->phase = SG_TIMED_VAULT_PHASE_WAIT_RESTORE;
}

static void TimedVaultAcceptPickup(sg_timed_vault_state_t *state)
{
	if (state->pickup_seen)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
		return;
	}
	if (!state->short_relay_seen ||
	    (state->phase != SG_TIMED_VAULT_PHASE_ACTIVE &&
	     state->phase != SG_TIMED_VAULT_PHASE_EGRESS))
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
		return;
	}
	state->pickup_seen = 1U;
	state->phase = SG_TIMED_VAULT_PHASE_EGRESS;
}

static void TimedVaultAcceptRestore(sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	if (state->restore_relay_seen)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
		return;
	}
	if (!state->dispatch_seen)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
		return;
	}
	if (!TimedVaultMechanismAuthenticated(state, observation,
	    state->spec.restore_relay_key, state->spec.device_target_count))
		return;
	state->restore_relay_seen = 1U;
	state->restoration = SG_TIMED_VAULT_RESTORATION_OBSERVED;
	if (observation->frame != state->restore_frame)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	if (observation->body_clear)
	{
		state->restoration = SG_TIMED_VAULT_RESTORATION_DISCHARGED;
		state->phase = SG_TIMED_VAULT_PHASE_TERMINAL;
		if (state->outcome == SG_TIMED_VAULT_OUTCOME_PENDING)
			state->outcome = SG_TIMED_VAULT_OUTCOME_SUCCEEDED;
	}
	else
		state->phase = SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR;
}

static void TimedVaultCheckMissedEvents(sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	if (!state->dispatch_seen && observation->frame >= state->dispatch_frame)
	{
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_MISSING_EVENT);
		return;
	}
	if (state->dispatch_seen && !state->short_relay_seen &&
	    observation->frame >= state->ready_frame)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_MISSING_EVENT);
	if (state->restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED &&
	    !state->restore_relay_seen &&
	    observation->frame >= state->restore_frame)
		TimedVaultFail(state, SG_TIMED_VAULT_REASON_MISSING_EVENT);
}

static sg_timed_vault_command_t TimedVaultCommand(
	const sg_timed_vault_state_t *state)
{
	if (state->outcome == SG_TIMED_VAULT_OUTCOME_FAILED)
		return SG_TIMED_VAULT_COMMAND_NONE;
	switch (state->phase)
	{
	case SG_TIMED_VAULT_PHASE_WAIT_DISPATCH:
	case SG_TIMED_VAULT_PHASE_WAIT_READY:
	case SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR:
		return SG_TIMED_VAULT_COMMAND_HOLD;
	case SG_TIMED_VAULT_PHASE_ACTIVE:
		return SG_TIMED_VAULT_COMMAND_ENTER;
	case SG_TIMED_VAULT_PHASE_EGRESS:
		return SG_TIMED_VAULT_COMMAND_EGRESS;
	case SG_TIMED_VAULT_PHASE_INVALID:
	case SG_TIMED_VAULT_PHASE_WAIT_RESTORE:
	case SG_TIMED_VAULT_PHASE_TERMINAL:
	default:
		return SG_TIMED_VAULT_COMMAND_NONE;
	}
}

sg_timed_vault_reduction_t SG_TimedVaultBegin(
	const sg_timed_vault_spec_t *spec,
	const sg_timed_vault_observation_t *observation)
{
	sg_timed_vault_reduction_t reduction;

	if (!TimedVaultSpecValid(spec) ||
	    !TimedVaultObservationShapeValid(observation) ||
	    !observation->alive || !observation->connected ||
	    !observation->binding_current ||
	    observation->binding_source_key != spec->source_key ||
	    observation->binding_fanout_identity != spec->fanout_identity ||
	    !observation->touch_authenticated ||
	    observation->dispatch_authenticated ||
	    observation->short_relay_authenticated || observation->flag_pickup ||
	    observation->restore_relay_authenticated ||
	    observation->event_source_key != spec->source_key)
		return TimedVaultInvalid();
	memset(&reduction, 0, sizeof(reduction));
	reduction.state.spec = *spec;
	reduction.state.phase = SG_TIMED_VAULT_PHASE_WAIT_DISPATCH;
	reduction.state.outcome = SG_TIMED_VAULT_OUTCOME_PENDING;
	reduction.state.touch_frame = observation->frame;
	reduction.state.last_frame = observation->frame;
	if (!TimedVaultFrameAdd(observation->frame,
	        SG_TIMED_VAULT_TOUCH_TO_DISPATCH_FRAMES,
	        &reduction.state.dispatch_frame) ||
	    !TimedVaultFrameAdd(reduction.state.dispatch_frame,
	        SG_TIMED_VAULT_SHORT_RELAY_FRAMES,
	        &reduction.state.ready_frame) ||
	    !TimedVaultFrameAdd(reduction.state.ready_frame,
	        SG_TIMED_VAULT_SAFE_LEASE_FRAMES,
	        &reduction.state.lease_deadline_frame) ||
	    !TimedVaultFrameAdd(reduction.state.dispatch_frame,
	        SG_TIMED_VAULT_RESTORE_RELAY_FRAMES,
	        &reduction.state.restore_frame) ||
	    reduction.state.lease_deadline_frame !=
	        reduction.state.restore_frame)
		return TimedVaultInvalid();
	reduction.command = SG_TIMED_VAULT_COMMAND_HOLD;
	return reduction;
}

sg_timed_vault_reduction_t SG_TimedVaultReduce(
	const sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	sg_timed_vault_reduction_t reduction;

	if (!state)
		return TimedVaultInvalid();
	reduction.state = *state;
	reduction.command = SG_TIMED_VAULT_COMMAND_NONE;
	if (state->phase == SG_TIMED_VAULT_PHASE_TERMINAL)
		return reduction;
	if (!TimedVaultSpecValid(&state->spec) ||
	    !TimedVaultObservationShapeValid(observation))
	{
		TimedVaultFail(&reduction.state, SG_TIMED_VAULT_REASON_INVALID);
		return reduction;
	}
	if (observation->frame <= state->last_frame)
	{
		TimedVaultFail(&reduction.state, SG_TIMED_VAULT_REASON_FRAME_DRIFT);
		return reduction;
	}
	reduction.state.last_frame = observation->frame;
	TimedVaultCheckOwner(&reduction.state, observation);

	if (observation->touch_authenticated)
		TimedVaultFail(&reduction.state,
		    SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
	if (observation->dispatch_authenticated)
		TimedVaultAcceptDispatch(&reduction.state, observation);
	if (observation->short_relay_authenticated)
		TimedVaultAcceptShortRelay(&reduction.state, observation);
	if (observation->flag_pickup)
		TimedVaultAcceptPickup(&reduction.state);
	if (observation->restore_relay_authenticated)
		TimedVaultAcceptRestore(&reduction.state, observation);

	if (reduction.state.restoration ==
	        SG_TIMED_VAULT_RESTORATION_OBSERVED &&
	    reduction.state.phase == SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR &&
	    observation->body_clear &&
	    !observation->restore_relay_authenticated)
	{
		reduction.state.restoration =
			SG_TIMED_VAULT_RESTORATION_DISCHARGED;
		reduction.state.phase = SG_TIMED_VAULT_PHASE_TERMINAL;
		if (reduction.state.outcome == SG_TIMED_VAULT_OUTCOME_PENDING)
			reduction.state.outcome = SG_TIMED_VAULT_OUTCOME_SUCCEEDED;
	}
	if (reduction.state.phase != SG_TIMED_VAULT_PHASE_TERMINAL)
		TimedVaultCheckMissedEvents(&reduction.state, observation);
	reduction.command = TimedVaultCommand(&reduction.state);
	return reduction;
}
