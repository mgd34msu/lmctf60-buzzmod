/* sg_timed_vault_game.c -- authenticate stock timed-vault event timing. */
#include "sg_timed_vault_game.h"

#include <limits.h>
#include <string.h>

static int GameBoolean(uint8_t value)
{
	return value <= 1U;
}

static int GameSpecValid(const sg_timed_vault_game_spec_t *spec)
{
	return spec && spec->link_index != UINT32_MAX &&
	       spec->short_relay_generation != 0U &&
	       spec->restore_relay_generation != 0U &&
	       spec->activator_identity != 0U && spec->short_ticket_id != 0U &&
	       spec->restore_ticket_id != 0U &&
	       spec->short_ticket_id != spec->restore_ticket_id;
}

static int GameObservationValid(
	const sg_timed_vault_game_observation_t *observation)
{
	return observation && GameBoolean(observation->alive) &&
	       GameBoolean(observation->connected) &&
	       GameBoolean(observation->binding_current) &&
	       GameBoolean(observation->body_clear) &&
	       GameBoolean(observation->bot_controlled);
}

static sg_timed_vault_observation_t GameObservation(
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	if (!live)
		return observation;
	observation.frame = live->frame;
	observation.binding_source_key = live->binding_source_key;
	observation.binding_fanout_identity = live->binding_fanout_identity;
	observation.door_top_count = live->door_top_count;
	observation.alive = live->alive;
	observation.connected = live->connected;
	observation.binding_current = live->binding_current;
	observation.body_clear = live->body_clear;
	return observation;
}

static int GameTag(sg_delayed_use_ticket_t *ticket, uint64_t ticket_id,
	uint64_t activator_identity, uint32_t source_key,
	uint32_t source_generation, uint32_t fanout_identity,
	uint32_t link_index, uint16_t event_order, int durable)
{
	sg_delayed_use_ticket_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.ticket_id = ticket_id;
	spec.activator_identity = activator_identity;
	spec.source_key = source_key;
	spec.source_generation = source_generation;
	spec.fanout_identity = fanout_identity;
	spec.link_index = link_index;
	spec.event_order = event_order;
	spec.durable = durable ? 1U : 0U;
	return SG_DelayedUseTicketTag(ticket, &spec);
}

static sg_timed_vault_command_t GameReduce(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	sg_timed_vault_reduction_t reduction;

	if (!state || !observation)
		return SG_TIMED_VAULT_COMMAND_NONE;
	reduction = SG_TimedVaultReduce(&state->transaction, observation);
	state->transaction = reduction.state;
	state->command = reduction.command;
	return reduction.command;
}

sg_timed_vault_game_begin_result_t SG_TimedVaultGameBegin(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_spec_t *spec,
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;
	sg_timed_vault_reduction_t reduction;

	if (!state)
		return SG_TIMED_VAULT_GAME_REJECTED;
	memset(state, 0, sizeof(*state));
	if (!GameObservationValid(live))
		return SG_TIMED_VAULT_GAME_REJECTED;
	if (!live->bot_controlled)
		return SG_TIMED_VAULT_GAME_BYPASS;
	if (!GameSpecValid(spec))
		return SG_TIMED_VAULT_GAME_REJECTED;
	observation = GameObservation(live);
	observation.touch_authenticated = 1U;
	observation.event_source_key = spec->transaction.source_key;
	reduction = SG_TimedVaultBegin(&spec->transaction, &observation);
	if (reduction.state.phase != SG_TIMED_VAULT_PHASE_WAIT_DISPATCH ||
	    reduction.state.outcome != SG_TIMED_VAULT_OUTCOME_PENDING)
		return SG_TIMED_VAULT_GAME_REJECTED;
	state->spec = *spec;
	state->transaction = reduction.state;
	state->command = reduction.command;
	return SG_TIMED_VAULT_GAME_ACTIVE;
}

int SG_TimedVaultGameDispatch(sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;

	if (!state || !GameObservationValid(live) || !live->bot_controlled ||
	    state->short_ticket.state != SG_DELAYED_USE_TICKET_EMPTY ||
	    state->restore_ticket.state != SG_DELAYED_USE_TICKET_EMPTY ||
	    !GameTag(&state->short_ticket, state->spec.short_ticket_id,
	        state->spec.activator_identity,
	        state->spec.transaction.short_relay_key,
	        state->spec.short_relay_generation,
	        state->spec.transaction.fanout_identity,
	        state->spec.link_index, 1U, 0) ||
	    !GameTag(&state->restore_ticket, state->spec.restore_ticket_id,
	        state->spec.activator_identity,
	        state->spec.transaction.restore_relay_key,
	        state->spec.restore_relay_generation,
	        state->spec.transaction.fanout_identity,
	        state->spec.link_index, 2U, 1))
		return 0;
	observation = GameObservation(live);
	observation.dispatch_authenticated = 1U;
	observation.event_source_key = state->spec.transaction.source_key;
	observation.event_fanout_identity =
		state->spec.transaction.fanout_identity;
	observation.event_target_count =
		state->spec.transaction.dispatch_target_count;
	(void)GameReduce(state, &observation);
	return state->transaction.dispatch_seen &&
	       state->transaction.outcome == SG_TIMED_VAULT_OUTCOME_PENDING;
}

int SG_TimedVaultGameShortRelay(sg_timed_vault_game_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;

	if (!state || !GameObservationValid(live) || !live->bot_controlled)
		return 0;
	observation = GameObservation(live);
	if (SG_DelayedUseTicketConsume(&state->short_ticket,
	        ticket_observation) != SG_DELAYED_USE_TICKET_AUTHORIZED)
	{
		(void)GameReduce(state, &observation);
		return 0;
	}
	observation.short_relay_authenticated = 1U;
	observation.event_source_key = state->spec.transaction.short_relay_key;
	observation.event_fanout_identity =
		state->spec.transaction.fanout_identity;
	observation.event_target_count =
		state->spec.transaction.device_target_count;
	(void)GameReduce(state, &observation);
	return state->transaction.short_relay_seen &&
	       state->transaction.outcome == SG_TIMED_VAULT_OUTCOME_PENDING;
}

sg_timed_vault_command_t SG_TimedVaultGameFlagPickup(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;

	if (!state || !GameObservationValid(live) || !live->bot_controlled)
		return SG_TIMED_VAULT_COMMAND_NONE;
	observation = GameObservation(live);
	observation.flag_pickup = 1U;
	return GameReduce(state, &observation);
}

sg_timed_vault_command_t SG_TimedVaultGameStep(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;

	if (!state || !GameObservationValid(live) || !live->bot_controlled)
		return SG_TIMED_VAULT_COMMAND_NONE;
	observation = GameObservation(live);
	return GameReduce(state, &observation);
}

int SG_TimedVaultGameRestoreRelay(sg_timed_vault_game_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	const sg_timed_vault_game_observation_t *live)
{
	sg_timed_vault_observation_t observation;

	if (!state || !GameObservationValid(live) || !live->bot_controlled)
		return 0;
	observation = GameObservation(live);
	if (SG_DelayedUseTicketConsume(&state->restore_ticket,
	        ticket_observation) != SG_DELAYED_USE_TICKET_AUTHORIZED)
	{
		(void)GameReduce(state, &observation);
		return 0;
	}
	observation.restore_relay_authenticated = 1U;
	observation.event_source_key = state->spec.transaction.restore_relay_key;
	observation.event_fanout_identity =
		state->spec.transaction.fanout_identity;
	observation.event_target_count =
		state->spec.transaction.device_target_count;
	(void)GameReduce(state, &observation);
	return state->transaction.restore_relay_seen != 0U;
}

int SG_TimedVaultGameRetireActivator(sg_timed_vault_game_state_t *state,
	uint64_t activator_identity)
{
	int durable;

	if (!state || activator_identity == 0U ||
	    activator_identity != state->spec.activator_identity)
		return 0;
	(void)SG_DelayedUseTicketRetireActivator(&state->short_ticket,
		activator_identity);
	durable = SG_DelayedUseTicketRetireActivator(&state->restore_ticket,
		activator_identity);
	return durable || state->restore_ticket.state ==
		SG_DELAYED_USE_TICKET_CONSUMED;
}

sg_timed_vault_command_t SG_TimedVaultGameCommand(
	const sg_timed_vault_game_state_t *state)
{
	return state ? state->command : SG_TIMED_VAULT_COMMAND_NONE;
}
