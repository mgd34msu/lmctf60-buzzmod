/* sg_relay_wall_live.c -- delayed-event adapter for temporary relay walls. */
#include "sg_relay_wall_live.h"

#include <string.h>

static int LiveSpecValid(const sg_relay_wall_live_spec_t *spec)
{
	return spec && spec->link_index != UINT32_MAX &&
	       spec->button_generation != 0U &&
	       spec->restoration_generation != 0U &&
	       spec->activator_identity != 0U && spec->dwell_ticket_id != 0U &&
	       spec->restoration_ticket_id != 0U &&
	       spec->dwell_ticket_id != spec->restoration_ticket_id &&
	       spec->speaker_events_per_fanout != 0U;
}

static int LiveTag(sg_delayed_use_ticket_t *ticket, uint64_t ticket_id,
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

int SG_RelayWallLiveBegin(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_live_spec_t *spec,
	const sg_relay_wall_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	if (!LiveSpecValid(spec) ||
	    !SG_RelayWallBegin(&state->transaction, &spec->transaction,
	        observation))
		return 0;
	state->spec = *spec;
	return 1;
}

int SG_RelayWallLiveArmDwell(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_observation_t *observation)
{
	if (!state || state->dwell_ticket.state != SG_DELAYED_USE_TICKET_EMPTY ||
	    SG_RelayWallStep(&state->transaction, observation) !=
	        SG_RELAY_WALL_COMMAND_ZERO ||
	    state->transaction.timeline.phase !=
	        SG_MECHANISM_TIMELINE_DELAY_PENDING)
		return 0;
	return LiveTag(&state->dwell_ticket, state->spec.dwell_ticket_id,
	    state->spec.activator_identity,
	    state->spec.transaction.timeline.source_key,
	    state->spec.button_generation,
	    state->spec.transaction.timeline.fanout_identity,
	    state->spec.link_index, 1U, 0);
}

int SG_RelayWallLiveConsumeDwell(sg_relay_wall_live_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	void *context, sg_relay_wall_live_dispatch_fn dispatch)
{
	sg_relay_wall_live_effect_t effect;
	sg_relay_wall_observation_t observation;

	if (!state || !dispatch ||
	    SG_DelayedUseTicketConsume(&state->dwell_ticket,
	        ticket_observation) != SG_DELAYED_USE_TICKET_AUTHORIZED)
		return 0;
	/* Arm the authored restoration obligation before the first callback can
	 * remove collision.  A partial or externally perturbed fanout can then
	 * fail the active transaction without losing the only safe close event. */
	if (!LiveTag(&state->restoration_ticket,
	        state->spec.restoration_ticket_id,
	        state->spec.activator_identity,
	        state->spec.transaction.restoration_source_key,
	        state->spec.restoration_generation,
	        state->spec.transaction.timeline.fanout_identity,
	        state->spec.link_index, 2U, 1))
		return 0;
	memset(&effect, 0, sizeof(effect));
	if (!dispatch(context, state->spec.transaction.activation_source_key,
	        1U, &effect) || effect.wall_open != 1U ||
	    effect.hurt_disabled != 1U ||
	    effect.speaker_events != state->spec.speaker_events_per_fanout)
		return 0;
	memset(&observation, 0, sizeof(observation));
	observation.timeline.frame = state->transaction.timeline.ready_frame;
	observation.timeline.source_key =
	    state->spec.transaction.timeline.source_key;
	observation.timeline.fanout_identity =
	    state->spec.transaction.timeline.fanout_identity;
	observation.timeline.alive = 1U;
	observation.timeline.connected = 1U;
	observation.timeline.binding_current = 1U;
	observation.timeline.approach_arrived = 1U;
	observation.timeline.activation_authenticated = 1U;
	observation.timeline.mechanism_active = 1U;
	observation.wall_key = state->spec.transaction.wall_key;
	observation.event_source_key =
	    state->spec.transaction.activation_source_key;
	observation.wall_open = 1U;
	observation.active_transition_authenticated = 1U;
	observation.body_clear = 1U;
	if (SG_RelayWallStep(&state->transaction, &observation) !=
	        SG_RELAY_WALL_COMMAND_ZERO ||
	    state->transaction.status != SG_RELAY_WALL_RUNNING ||
	    !state->transaction.wall_open)
		return 0;
	return 1;
}

int SG_RelayWallLiveEgress(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_observation_t *observation)
{
	return state && SG_RelayWallStep(&state->transaction, observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE &&
	    state->transaction.status == SG_RELAY_WALL_RESTORING;
}

int SG_RelayWallLiveAbort(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_observation_t *observation)
{
	return state && SG_RelayWallStep(&state->transaction, observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE &&
	    state->transaction.status == SG_RELAY_WALL_RESTORING;
}

int SG_RelayWallLiveRetireActivator(sg_relay_wall_live_state_t *state,
	uint64_t activator_identity)
{
	int durable;

	if (!state || activator_identity == 0U ||
	    activator_identity != state->spec.activator_identity)
		return 0;
	(void)SG_DelayedUseTicketRetireActivator(&state->dwell_ticket,
	    activator_identity);
	durable = SG_DelayedUseTicketRetireActivator(
	    &state->restoration_ticket, activator_identity);
	return durable || state->restoration_ticket.state ==
	    SG_DELAYED_USE_TICKET_CONSUMED;
}

sg_relay_wall_live_restore_result_t SG_RelayWallLiveConsumeRestoration(
	sg_relay_wall_live_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	void *context, sg_relay_wall_live_body_clear_fn body_clear,
	sg_relay_wall_live_dispatch_fn dispatch)
{
	sg_relay_wall_live_effect_t effect;
	sg_relay_wall_observation_t observation;

	if (!state || !ticket_observation || !body_clear || !dispatch ||
	    state->transaction.status != SG_RELAY_WALL_RESTORING ||
	    state->restoration_ticket.state != SG_DELAYED_USE_TICKET_ARMED)
		return SG_RELAY_WALL_LIVE_REJECTED;
	if (!body_clear(context, state->spec.transaction.wall_key))
		return SG_RELAY_WALL_LIVE_WAIT_CLEAR;
	if (SG_DelayedUseTicketConsume(&state->restoration_ticket,
	        ticket_observation) != SG_DELAYED_USE_TICKET_AUTHORIZED)
		return SG_RELAY_WALL_LIVE_REJECTED;
	memset(&effect, 0, sizeof(effect));
	if (!dispatch(context, state->spec.transaction.restoration_source_key,
	        2U, &effect) || effect.wall_open != 0U ||
	    effect.hurt_disabled != 0U ||
	    effect.speaker_events != state->spec.speaker_events_per_fanout)
		return SG_RELAY_WALL_LIVE_REJECTED;
	memset(&observation, 0, sizeof(observation));
	observation.timeline.frame = state->transaction.timeline.last_frame;
	observation.timeline.source_key =
	    state->spec.transaction.timeline.source_key;
	observation.timeline.fanout_identity =
	    state->spec.transaction.timeline.fanout_identity;
	observation.timeline.binding_current = 1U;
	observation.wall_key = state->spec.transaction.wall_key;
	observation.event_source_key =
	    state->spec.transaction.restoration_source_key;
	observation.restoration_authenticated = 1U;
	observation.body_clear = 1U;
	(void)SG_RelayWallStep(&state->transaction, &observation);
	return (state->transaction.status == SG_RELAY_WALL_COMPLETE ||
	        state->transaction.status == SG_RELAY_WALL_FAILED)
	    ? SG_RELAY_WALL_LIVE_RESTORED : SG_RELAY_WALL_LIVE_REJECTED;
}
