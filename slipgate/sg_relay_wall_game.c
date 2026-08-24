/* sg_relay_wall_game.c -- engine boundary for authenticated relay walls. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_relay_wall_game.h"
#include "slipgate/sg_relay_wall_live.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"

#include <limits.h>
#include <string.h>

typedef struct sg_relay_wall_game_state_s
{
	sg_relay_wall_live_state_t live;
	const rune_t *rune;
	edict_t *owner;
	edict_t *activator;
	uint64_t activator_identity;
	uint32_t link_index;
	uint32_t button_key;
	uint32_t button_generation;
	uint32_t immediate_key;
	uint32_t restoration_key;
	uint32_t restoration_generation;
	uint32_t wall_key;
	uint8_t deferred;
} sg_relay_wall_game_state_t;

typedef struct relay_wall_dispatch_s
{
	sg_relay_wall_game_state_t *state;
	sg_rune_mechanism_binding_t binding;
	edict_t *source;
	edict_t *activator;
	uint16_t event_order;
	uint16_t speakers;
} relay_wall_dispatch_t;

static sg_relay_wall_game_state_t *relay_wall_scheduling;
static uint64_t relay_wall_next_ticket;

static sg_bot_t *RelayWallBotForEntity(edict_t *entity)
{
	int index;

	if (!entity || !entity->inuse || !entity->client ||
	    !SG_OwnsBot(entity))
		return NULL;
	for (index = 0; index < SG_MAXBOTS; index++)
		if (sg_bots[index].active && sg_bots[index].ent == entity &&
		    sg_bots[index].instance_token != 0U)
			return &sg_bots[index];
	return NULL;
}

static int RelayWallBotCurrent(const sg_relay_wall_game_state_t *state)
{
	sg_bot_t *bot;

	return state && (bot = RelayWallBotForEntity(state->activator)) != NULL &&
	       bot->instance_token == state->activator_identity;
}

static int RelayWallFrames(uint32_t milliseconds, uint32_t *frames_out)
{
	if (frames_out)
		*frames_out = 0U;
	if (!frames_out || milliseconds == 0U || milliseconds % 100U != 0U)
		return 0;
	*frames_out = milliseconds / 100U;
	return *frames_out != 0U;
}

static int RelayWallTicketPair(uint64_t *first_out, uint64_t *second_out)
{
	if (!first_out || !second_out || relay_wall_next_ticket > UINT64_MAX - 2U)
		return 0;
	*first_out = ++relay_wall_next_ticket;
	*second_out = ++relay_wall_next_ticket;
	return *first_out != 0U && *second_out != 0U;
}

static int RelayWallSpeakerCount(
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key,
	uint16_t *count_out)
{
	uint32_t ordinal;
	uint32_t count = 0U;

	if (count_out)
		*count_out = 0U;
	if (!binding || !count_out)
		return 0;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *node;

		if (!edge || edge->from_key != source_key ||
		    edge->kind != SG_MECH_EDGE_TARGET)
			continue;
		node = SG_RuneMechanismNodeByKey(binding->rune, edge->to_key);
		if (!node)
			return 0;
		if (node->kind == SG_MECH_NODE_TARGET_SPEAKER)
			count++;
	}
	if (count == 0U || count > UINT16_MAX)
		return 0;
	*count_out = (uint16_t)count;
	return 1;
}

static int RelayWallBinding(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding_out)
{
	return rune && binding_out && SG_Rune() == rune &&
	       SG_RunePhysicsCompatible(rune) &&
	       SG_RuneMechanismBindingCaptureOwned(rune, link_index,
	           binding_out) &&
	       binding_out->plan->controller_kind ==
	           SG_MECHANISM_CONTROLLER_RELAY_DOOR &&
	       binding_out->link->action == RL_BUTTON_DOOR &&
	       binding_out->link->mode == RLCM_PREOPEN &&
	       binding_out->destination_node && binding_out->egress_node &&
	       binding_out->mover_node;
}

static int RelayWallGeneration(edict_t *entity, uint32_t key,
	uint32_t generation)
{
	uint32_t current_key;
	uint32_t current_generation;

	return SG_MechCatalogEntityGeneration(entity, &current_key,
	           &current_generation) && current_key == key &&
	       current_generation == generation;
}

static void RelayWallTicketObservation(
	const sg_relay_wall_game_state_t *state,
	const edict_t *delayed,
	const sg_delayed_use_ticket_t *ticket,
	const sg_rune_mechanism_binding_t *binding,
	sg_delayed_use_ticket_observation_t *observation)
{
	edict_t *source;

	memset(observation, 0, sizeof(*observation));
	if (!state || !delayed || !ticket || !binding)
		return;
	*observation = (sg_delayed_use_ticket_observation_t){
		.ticket_id = ticket->spec.ticket_id,
		.activator_identity = state->activator_identity,
		.source_key = delayed->sg_delayed_source_key,
		.source_generation = delayed->sg_delayed_source_generation,
		.fanout_identity = binding->plan->closure_crc32,
		.link_index = state->link_index,
		.event_order = ticket->spec.event_order,
		.fanout_current = SG_RuneMechanismBindingCurrent(binding) ? 1U : 0U,
		.activator_current = RelayWallBotCurrent(state) ? 1U : 0U
	};
	source = SG_RuneMechanismBindingResolveNode(binding,
	    observation->source_key);
	observation->source_current = source && RelayWallGeneration(source,
	    observation->source_key, observation->source_generation) ? 1U : 0U;
}

static int RelayWallDelayedCopyCurrent(const edict_t *delayed,
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key)
{
	edict_t *source = SG_RuneMechanismBindingResolveNode(binding, source_key);

	return delayed && source && delayed->classname &&
	       strcmp(delayed->classname, "DelayedUse") == 0 &&
	       delayed->think == Think_Delay && delayed->target == source->target &&
	       delayed->message == source->message &&
	       delayed->killtarget == source->killtarget;
}

static int RelayWallBodyClear(void *raw_context, uint32_t wall_key)
{
	relay_wall_dispatch_t *context = raw_context;
	edict_t *wall;
	int index;

	if (!context || wall_key != context->state->wall_key ||
	    !(wall = SG_RuneMechanismBindingResolveNode(&context->binding,
	        wall_key)))
		return 0;
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *body = &g_edicts[index];

		if (!body->inuse || body == wall || body == context->state->owner ||
		    body->solid != SOLID_BBOX)
			continue;
		if (body->absmax[0] > wall->absmin[0] &&
		    body->absmin[0] < wall->absmax[0] &&
		    body->absmax[1] > wall->absmin[1] &&
		    body->absmin[1] < wall->absmax[1] &&
		    body->absmax[2] > wall->absmin[2] &&
		    body->absmin[2] < wall->absmax[2])
			return 0;
	}
	return 1;
}

static int RelayWallInvokeTarget(void *raw_context, edict_t *target,
	uint32_t target_key, uint32_t target_ordinal)
{
	relay_wall_dispatch_t *context = raw_context;
	const rune_mechanism_node_t *node;

	(void)target_ordinal;
	if (!context || !target ||
	    !(node = SG_RuneMechanismNodeByKey(context->binding.rune,
	        target_key)))
		return 0;
	if (target->use)
		target->use(target, context->source, context->activator);
	if (node->kind == SG_MECH_NODE_TARGET_SPEAKER)
		context->speakers++;
	return 1;
}

static int RelayWallDispatch(void *raw_context, uint32_t source_key,
	uint16_t event_order, sg_relay_wall_live_effect_t *effect_out)
{
	relay_wall_dispatch_t *context = raw_context;
	edict_t *wall;
	edict_t *hurt = NULL;
	uint32_t ordinal;

	if (!context || !effect_out || event_order != context->event_order ||
	    source_key != (event_order == 1U ? context->state->immediate_key
	                                  : context->state->restoration_key) ||
	    !(context->source = SG_RuneMechanismBindingResolveNode(
	        &context->binding, source_key)))
		return 0;
	if (event_order == 1U)
	{
		relay_wall_scheduling = context->state;
		G_UseTargets(SG_RuneMechanismBindingResolveNode(&context->binding,
		    context->state->restoration_key), context->activator);
		relay_wall_scheduling = NULL;
		if (!context->state->owner ||
		    context->state->owner->sg_relay_wall_live != context->state)
			return 0;
	}
	context->speakers = 0U;
	if (!SG_RuneMechanismBindingDispatchTargets(&context->binding,
	        source_key, RelayWallInvokeTarget, context))
		return 0;
	wall = SG_RuneMechanismBindingResolveNode(&context->binding,
	    context->state->wall_key);
	for (ordinal = 0U; ordinal < context->binding.plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(&context->binding, ordinal);
		const rune_mechanism_node_t *node;

		if (!edge || edge->from_key != source_key ||
		    !(node = SG_RuneMechanismNodeByKey(context->binding.rune,
		        edge->to_key)))
			continue;
		if (node->kind == SG_MECH_NODE_TRIGGER_HURT)
		{
			if (hurt)
				return 0;
			hurt = SG_RuneMechanismBindingResolveNode(&context->binding,
			    node->key);
		}
	}
	if (!wall || !hurt)
		return 0;
	effect_out->speaker_events = context->speakers;
	effect_out->wall_open = wall->solid == SOLID_NOT ? 1U : 0U;
	effect_out->hurt_disabled = hurt->solid == SOLID_NOT ? 1U : 0U;
	return 1;
}

static sg_relay_wall_game_target_result_t RelayWallRunDelayed(
	edict_t *delayed)
{
	sg_relay_wall_game_state_t *state = delayed->sg_relay_wall_live;
	sg_rune_mechanism_binding_t binding;
	sg_delayed_use_ticket_observation_t ticket_observation;
	relay_wall_dispatch_t dispatch;

	if (!state || state->owner != delayed ||
	    !RelayWallBinding(state->rune, state->link_index, &binding))
		return SG_RELAY_WALL_GAME_HANDLED;
	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.state = state;
	dispatch.binding = binding;
	dispatch.activator = RelayWallBotCurrent(state) ? state->activator : NULL;
	state->deferred = 0U;
	if (delayed->sg_delayed_source_key == state->button_key)
	{
		edict_t *restoration_owner;

		if (!RelayWallDelayedCopyCurrent(delayed, &binding,
		        state->button_key))
			return SG_RELAY_WALL_GAME_HANDLED;
		RelayWallTicketObservation(state, delayed,
		    &state->live.dwell_ticket,
		    &binding, &ticket_observation);
		dispatch.event_order = 1U;
		if (!SG_RelayWallLiveConsumeDwell(&state->live,
		        &ticket_observation, &dispatch, RelayWallDispatch))
			return SG_RELAY_WALL_GAME_HANDLED;
		restoration_owner = state->owner;
		if (restoration_owner != delayed)
			delayed->sg_relay_wall_live = NULL;
		return SG_RELAY_WALL_GAME_HANDLED;
	}
	if (delayed->sg_delayed_source_key == state->restoration_key)
	{
		sg_relay_wall_observation_t observation;
		sg_relay_wall_live_restore_result_t result;
		edict_t *wall = SG_RuneMechanismBindingResolveNode(&binding,
		    state->wall_key);

		if (!RelayWallDelayedCopyCurrent(delayed, &binding,
		        state->restoration_key))
			return SG_RELAY_WALL_GAME_HANDLED;
		memset(&observation, 0, sizeof(observation));
		observation.timeline.frame = (uint32_t)level.framenum;
		observation.timeline.source_key = state->button_key;
		observation.timeline.fanout_identity = binding.plan->closure_crc32;
		observation.timeline.alive = RelayWallBotCurrent(state) ? 1U : 0U;
		observation.timeline.connected = observation.timeline.alive;
		observation.timeline.binding_current = 1U;
		observation.timeline.mechanism_active =
		    wall && wall->solid == SOLID_NOT ? 1U : 0U;
		observation.wall_key = state->wall_key;
		observation.wall_open = observation.timeline.mechanism_active;
		observation.body_clear = RelayWallBodyClear(&dispatch,
		    state->wall_key) ? 1U : 0U;
		(void)SG_RelayWallLiveAbort(&state->live, &observation);
		RelayWallTicketObservation(state, delayed,
		    &state->live.restoration_ticket, &binding,
		    &ticket_observation);
		dispatch.event_order = 2U;
		result = SG_RelayWallLiveConsumeRestoration(&state->live,
		    &ticket_observation, &dispatch, RelayWallBodyClear,
		    RelayWallDispatch);
		if (result == SG_RELAY_WALL_LIVE_WAIT_CLEAR)
		{
			state->deferred = 1U;
			delayed->nextthink = level.time + FRAMETIME;
		}
		return SG_RELAY_WALL_GAME_HANDLED;
	}
	return SG_RELAY_WALL_GAME_HANDLED;
}

sg_relay_wall_game_target_result_t SG_RelayWallGameHandleTargets(
	edict_t *source, edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot;

	if (source && source->sg_relay_wall_live)
		return RelayWallRunDelayed(source);
	if (relay_wall_scheduling)
	{
		if (source && source->s.number ==
		        (int)relay_wall_scheduling->restoration_key &&
		    activator == relay_wall_scheduling->activator)
			return SG_RELAY_WALL_GAME_ALLOW_STOCK;
		return SG_RELAY_WALL_GAME_NOT_OWNED;
	}
	bot = RelayWallBotForEntity(activator);
	if (!bot || !RelayWallBinding(SG_Rune(), (uint32_t)bot->commit_link,
	        &binding) || source != binding.entry_entity)
		return SG_RELAY_WALL_GAME_NOT_OWNED;
	if (!bot->declared_started || !bot->declared_touched ||
	    bot->declared_guard_paused || source->activator != activator ||
	    source->delay <= 0.0f || source->killtarget || source->message ||
	    SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) !=
	        SG_COMPOUND_GUARD_OK)
		return SG_RELAY_WALL_GAME_HANDLED;
	return SG_RELAY_WALL_GAME_ALLOW_STOCK;
}

void SG_RelayWallGameTagDelayedTarget(edict_t *source, edict_t *activator,
	edict_t *delayed)
{
	sg_relay_wall_game_state_t *state;
	sg_rune_mechanism_binding_t binding;
	sg_relay_wall_live_spec_t spec;
	sg_relay_wall_observation_t observation;
	sg_bot_t *bot;
	uint32_t trigger_frames;
	uint32_t cooldown_frames;
	uint32_t lease_frames;
	uint64_t dwell_ticket;
	uint64_t restoration_ticket;
	uint16_t speakers;

	if (!source || !delayed)
		return;
	if (relay_wall_scheduling)
	{
		state = relay_wall_scheduling;
		if (source->s.number != (int)state->restoration_key ||
		    activator != state->activator)
			return;
		delayed->sg_relay_wall_live = state;
		delayed->sg_delayed_source_key = state->restoration_key;
		delayed->sg_delayed_source_generation =
		    state->restoration_generation;
		state->owner = delayed;
		return;
	}
	bot = RelayWallBotForEntity(activator);
	if (!bot || !RelayWallBinding(SG_Rune(), (uint32_t)bot->commit_link,
	        &binding) || source != binding.entry_entity ||
	    !RelayWallFrames((uint32_t)binding.entry_node->delay_ms,
	        &trigger_frames) ||
	    !RelayWallFrames(binding.plan->cooldown_ms, &cooldown_frames) ||
	    !RelayWallFrames((uint32_t)binding.egress_node->delay_ms,
	        &lease_frames) ||
	    !RelayWallSpeakerCount(&binding, binding.destination_node->key,
	        &speakers) ||
	    !RelayWallTicketPair(&dwell_ticket, &restoration_ticket) ||
	    !sg_host.level_alloc || !sg_host.level_free)
		return;
	memset(&spec, 0, sizeof(spec));
	if (!SG_MechCatalogEntityGeneration(source,
	        &spec.transaction.timeline.source_key,
	        &spec.button_generation) ||
	    !SG_MechCatalogEntityGeneration(binding.egress_entity,
	        &spec.transaction.restoration_source_key,
	        &spec.restoration_generation))
		return;
	spec.transaction.timeline.fanout_identity = binding.plan->closure_crc32;
	spec.transaction.timeline.approach_timeout_frames = 1U;
	spec.transaction.timeline.activation_timeout_frames = 1U;
	spec.transaction.timeline.trigger_delay_frames = trigger_frames;
	spec.transaction.timeline.cooldown_frames = cooldown_frames;
	spec.transaction.timeline.lease_frames = lease_frames;
	spec.transaction.timeline.egress_timeout_frames = lease_frames;
	spec.transaction.wall_key = binding.mover_node->key;
	spec.transaction.activation_source_key = binding.destination_node->key;
	spec.link_index = (uint32_t)bot->commit_link;
	spec.activator_identity = bot->instance_token;
	spec.dwell_ticket_id = dwell_ticket;
	spec.restoration_ticket_id = restoration_ticket;
	spec.speaker_events_per_fanout = speakers;
	state = sg_host.level_alloc((int)sizeof(*state));
	if (!state)
		return;
	memset(state, 0, sizeof(*state));
	memset(&observation, 0, sizeof(observation));
	observation.timeline.frame = (uint32_t)level.framenum;
	observation.timeline.source_key =
	    spec.transaction.timeline.source_key;
	observation.timeline.fanout_identity = binding.plan->closure_crc32;
	observation.timeline.alive = 1U;
	observation.timeline.connected = 1U;
	observation.timeline.binding_current = 1U;
	observation.timeline.approach_arrived = 1U;
	observation.timeline.activation_authenticated = 1U;
	observation.wall_key = spec.transaction.wall_key;
	observation.body_clear = 1U;
	if (!SG_RelayWallLiveBegin(&state->live, &spec, &observation) ||
	    !SG_RelayWallLiveArmDwell(&state->live, &observation))
	{
		sg_host.level_free(state);
		return;
	}
	state->rune = binding.rune;
	state->owner = delayed;
	state->activator = activator;
	state->activator_identity = bot->instance_token;
	state->link_index = (uint32_t)bot->commit_link;
	state->button_key = binding.entry_node->key;
	state->button_generation = spec.button_generation;
	state->immediate_key = binding.destination_node->key;
	state->restoration_key = binding.egress_node->key;
	state->restoration_generation = spec.restoration_generation;
	state->wall_key = binding.mover_node->key;
	delayed->sg_relay_wall_live = state;
	delayed->sg_delayed_source_key = state->button_key;
	delayed->sg_delayed_source_generation = state->button_generation;
}

int SG_RelayWallGameDelayedUseDurable(const edict_t *delayed)
{
	const sg_relay_wall_game_state_t *state = delayed
	    ? delayed->sg_relay_wall_live : NULL;

	return state && state->owner == delayed &&
	       delayed->sg_delayed_source_key == state->restoration_key &&
	       state->live.restoration_ticket.spec.durable == 1U;
}

int SG_RelayWallGameDelayedUseDeferred(const edict_t *delayed)
{
	const sg_relay_wall_game_state_t *state = delayed
	    ? delayed->sg_relay_wall_live : NULL;

	return state && state->owner == delayed && state->deferred;
}

void SG_RelayWallGameRetireActivator(edict_t *delayed,
	edict_t *activator)
{
	sg_relay_wall_game_state_t *state = delayed
	    ? delayed->sg_relay_wall_live : NULL;

	if (!state || state->owner != delayed || state->activator != activator)
		return;
	(void)SG_RelayWallLiveRetireActivator(&state->live,
	    state->activator_identity);
	if (SG_RelayWallGameDelayedUseDurable(delayed))
		delayed->activator = NULL;
}

void SG_RelayWallGameEntityFreed(edict_t *entity)
{
	sg_relay_wall_game_state_t *state = entity
	    ? entity->sg_relay_wall_live : NULL;

	if (!state)
		return;
	entity->sg_relay_wall_live = NULL;
	if (state->owner == entity && sg_host.level_free)
		sg_host.level_free(state);
}
