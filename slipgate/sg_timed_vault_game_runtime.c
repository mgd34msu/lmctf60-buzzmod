/* sg_timed_vault_game_runtime.c -- engine boundary for timed flag vaults. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_timed_vault_game.h"
#include "slipgate/sg_timed_vault_game_runtime.h"
#include "slipgate/sg_util.h"

#include <limits.h>
#include <string.h>

typedef struct sg_timed_vault_game_runtime_state_s
{
	sg_timed_vault_game_state_t game;
	const rune_t *rune;
	edict_t *owner;
	edict_t *activator;
	edict_t *short_delayed;
	edict_t *restore_delayed;
	uint64_t activator_identity;
	uint32_t link_index;
	uint32_t source_key;
	uint32_t short_key;
	uint32_t restore_key;
	uint32_t short_generation;
	uint32_t restore_generation;
	uint8_t short_needs_restore;
	uint8_t failed_closed;
	uint8_t restore_authorized;
	uint8_t restore_dispatched;
	uint8_t deferred;
} sg_timed_vault_game_runtime_state_t;

typedef struct timed_vault_dispatch_s
{
	sg_timed_vault_game_runtime_state_t *state;
	sg_rune_mechanism_binding_t binding;
	edict_t *source;
	edict_t *activator;
	uint16_t target_count;
	uint16_t mutated_count;
	uint8_t normalize_restore;
} timed_vault_dispatch_t;

static sg_timed_vault_game_runtime_state_t *timed_vault_scheduling;
static uint64_t timed_vault_next_ticket;

static sg_bot_t *TimedVaultBotForEntity(edict_t *entity)
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

static int TimedVaultBotCurrent(
	const sg_timed_vault_game_runtime_state_t *state)
{
	sg_bot_t *bot;

	return state && (bot = TimedVaultBotForEntity(state->activator)) != NULL &&
	       bot->instance_token == state->activator_identity;
}

static int TimedVaultBinding(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding_out)
{
	return rune && binding_out && SG_Rune() == rune &&
	       SG_RunePhysicsCompatible(rune) &&
	       SG_RuneMechanismBindingCaptureOwned(rune, link_index,
	           binding_out) &&
	       binding_out->plan->controller_kind ==
	           SG_MECHANISM_CONTROLLER_TIMED_VAULT &&
	       binding_out->link->action == RL_BUTTON_DOOR &&
	       binding_out->link->mode == RLCM_PREOPEN &&
	       binding_out->destination_node && binding_out->egress_node &&
	       binding_out->mover_node;
}

static int TimedVaultGeneration(edict_t *entity, uint32_t key,
	uint32_t generation)
{
	uint32_t current_key;
	uint32_t current_generation;

	return SG_MechCatalogEntityGeneration(entity, &current_key,
	           &current_generation) && current_key == key &&
	       current_generation == generation;
}

static uint16_t TimedVaultTargetCount(
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key)
{
	uint32_t count = 0U;
	uint32_t ordinal;

	if (!binding)
		return 0U;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);

		if (!edge)
			return 0U;
		if (edge->from_key == source_key &&
		    edge->kind == SG_MECH_EDGE_TARGET)
		{
			if (count == UINT16_MAX)
				return 0U;
			count++;
		}
	}
	return (uint16_t)count;
}

static int TimedVaultTicketPair(uint64_t *short_out,
	uint64_t *restore_out)
{
	if (!short_out || !restore_out ||
	    timed_vault_next_ticket > UINT64_MAX - 2U)
		return 0;
	*short_out = ++timed_vault_next_ticket;
	*restore_out = ++timed_vault_next_ticket;
	return *short_out != 0U && *restore_out != 0U;
}

static int TimedVaultDoors(
	const sg_rune_mechanism_binding_t *binding,
	edict_t *doors[SG_RUNE_BINDING_MAX_MOVERS], size_t *count_out)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;

	if (count_out)
		*count_out = 0U;
	if (!binding || !doors || !count_out ||
	    !SG_RuneMechanismBindingMoverKeys(binding, keys, &count) ||
	    count != 2U)
		return 0;
	for (index = 0U; index < count; index++)
	{
		doors[index] = SG_RuneMechanismBindingResolveNode(binding,
		    keys[index]);
		if (!doors[index])
			return 0;
	}
	*count_out = count;
	return 1;
}

static uint8_t TimedVaultDoorTopCount(
	const sg_rune_mechanism_binding_t *binding)
{
	edict_t *doors[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;
	uint8_t top = 0U;

	if (!TimedVaultDoors(binding, doors, &count))
		return 0U;
	for (index = 0U; index < count; index++)
		if (doors[index]->moveinfo.state == SG_PLAT_STATE_TOP)
			top++;
	return top;
}

static int TimedVaultBodyClear(
	const sg_rune_mechanism_binding_t *binding)
{
	edict_t *doors[SG_RUNE_BINDING_MAX_MOVERS];
	size_t door_count;
	size_t door_index;
	int body_index;

	if (!TimedVaultDoors(binding, doors, &door_count))
		return 0;
	for (door_index = 0U; door_index < door_count; door_index++)
		if (doors[door_index]->moveinfo.state != SG_PLAT_STATE_BOTTOM)
			return 0;
	for (body_index = 1; body_index < globals.num_edicts; body_index++)
	{
		edict_t *body = &g_edicts[body_index];
		int is_door = 0;

		if (!body->inuse || body->solid != SOLID_BBOX)
			continue;
		for (door_index = 0U; door_index < door_count; door_index++)
			if (body == doors[door_index])
				is_door = 1;
		if (is_door)
			continue;
		for (door_index = 0U; door_index < door_count; door_index++)
			if (body->absmax[0] > doors[door_index]->absmin[0] &&
			    body->absmin[0] < doors[door_index]->absmax[0] &&
			    body->absmax[1] > doors[door_index]->absmin[1] &&
			    body->absmin[1] < doors[door_index]->absmax[1] &&
			    body->absmax[2] > doors[door_index]->absmin[2] &&
			    body->absmin[2] < doors[door_index]->absmax[2])
				return 0;
	}
	return 1;
}

static sg_timed_vault_game_observation_t TimedVaultObservation(
	const sg_timed_vault_game_runtime_state_t *state,
	const sg_rune_mechanism_binding_t *binding, uint32_t frame)
{
	sg_timed_vault_game_observation_t observation;
	int current = state && binding &&
		SG_RuneMechanismBindingCurrent(binding);
	int owner_current = TimedVaultBotCurrent(state);

	memset(&observation, 0, sizeof(observation));
	observation.frame = frame;
	observation.binding_source_key = state ? state->source_key : 0U;
	observation.binding_fanout_identity = current
		? binding->plan->closure_crc32 : 0U;
	observation.alive = owner_current && state->activator->health > 0 &&
		!state->activator->deadflag ? 1U : 0U;
	observation.connected = owner_current ? 1U : 0U;
	observation.binding_current = current ? 1U : 0U;
	observation.door_top_count = current
		? TimedVaultDoorTopCount(binding) : 0U;
	observation.body_clear = current && TimedVaultBodyClear(binding) ? 1U : 0U;
	observation.bot_controlled = 1U;
	return observation;
}

static void TimedVaultTicketObservation(
	const sg_timed_vault_game_runtime_state_t *state,
	const edict_t *delayed, const sg_delayed_use_ticket_t *ticket,
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
		.activator_current = TimedVaultBotCurrent(state) ? 1U : 0U
	};
	source = SG_RuneMechanismBindingResolveNode(binding,
	    observation->source_key);
	observation->source_current = source && TimedVaultGeneration(source,
	    observation->source_key, observation->source_generation) ? 1U : 0U;
}

static int TimedVaultDelayedCopyCurrent(const edict_t *delayed,
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key)
{
	edict_t *source = SG_RuneMechanismBindingResolveNode(binding, source_key);

	return delayed && source && delayed->classname &&
	       strcmp(delayed->classname, "DelayedUse") == 0 &&
	       delayed->think == Think_Delay && delayed->target == source->target &&
	       delayed->message == source->message &&
	       delayed->killtarget == source->killtarget;
}

static int TimedVaultInvokeTarget(void *raw_context, edict_t *target,
	uint32_t target_key, uint32_t target_ordinal)
{
	timed_vault_dispatch_t *context = raw_context;

	(void)target_key;
	(void)target_ordinal;
	if (!context || !target || !target->use ||
	    context->target_count == UINT16_MAX)
		return 0;
	if (!context->normalize_restore ||
	    (target->classname && strcmp(target->classname, "target_laser") == 0
	        ? (target->spawnflags & 1) == 0
	        : target->classname &&
	          strcmp(target->classname, "target_speaker") == 0
	            ? target->s.sound == 0
	            : 1))
	{
		target->use(target, context->source, context->activator);
		context->mutated_count++;
	}
	context->target_count++;
	return 1;
}

static int TimedVaultDispatchTargets(timed_vault_dispatch_t *dispatch,
	uint32_t source_key, uint16_t expected_count)
{
	if (!dispatch)
		return 0;
	dispatch->target_count = 0U;
	dispatch->mutated_count = 0U;
	return SG_RuneMechanismBindingDispatchTargets(&dispatch->binding,
	           source_key, TimedVaultInvokeTarget, dispatch) &&
	       dispatch->target_count == expected_count;
}

static void TimedVaultDetachAndFree(
	sg_timed_vault_game_runtime_state_t *state)
{
	if (!state)
		return;
	if (state->activator && state->activator->sg_timed_vault_live == state)
		state->activator->sg_timed_vault_live = NULL;
	if (state->short_delayed &&
	    state->short_delayed->sg_timed_vault_live == state)
		state->short_delayed->sg_timed_vault_live = NULL;
	if (state->restore_delayed &&
	    state->restore_delayed->sg_timed_vault_live == state)
		state->restore_delayed->sg_timed_vault_live = NULL;
	if (sg_host.level_free)
		sg_host.level_free(state);
}

static void TimedVaultCancelShort(
	sg_timed_vault_game_runtime_state_t *state)
{
	edict_t *short_delayed;

	if (!state || !(short_delayed = state->short_delayed))
		return;
	state->short_delayed = NULL;
	short_delayed->sg_timed_vault_live = NULL;
	G_FreeEdict(short_delayed);
}

static sg_timed_vault_runtime_target_result_t TimedVaultRunDelayed(
	edict_t *delayed)
{
	sg_timed_vault_game_runtime_state_t *state =
		delayed->sg_timed_vault_live;
	sg_rune_mechanism_binding_t binding;
	sg_delayed_use_ticket_observation_t ticket;
	sg_timed_vault_game_observation_t observation;
	timed_vault_dispatch_t dispatch;

	if (!state)
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	if (!TimedVaultBinding(state->rune, state->link_index, &binding))
	{
		if (delayed == state->restore_delayed)
		{
			state->deferred = 1U;
			delayed->nextthink = level.time + FRAMETIME;
		}
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	}
	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.state = state;
	dispatch.binding = binding;
	dispatch.source = delayed;
	dispatch.activator = TimedVaultBotCurrent(state)
		? state->activator : NULL;
	state->deferred = 0U;
	observation = TimedVaultObservation(state, &binding,
	    (uint32_t)level.framenum);
	if (delayed == state->short_delayed &&
	    delayed->sg_delayed_source_key == state->short_key)
	{
		if (!TimedVaultDelayedCopyCurrent(delayed, &binding,
		        state->short_key))
			return SG_TIMED_VAULT_RUNTIME_HANDLED;
		TimedVaultTicketObservation(state, delayed,
		    &state->game.short_ticket, &binding, &ticket);
		if (!SG_TimedVaultGameShortRelay(&state->game, &ticket,
		        &observation))
			return SG_TIMED_VAULT_RUNTIME_HANDLED;
		if (!TimedVaultDispatchTargets(&dispatch, state->short_key,
		        state->game.spec.transaction.device_target_count))
			state->failed_closed = 1U;
		if (dispatch.mutated_count != 0U)
			state->short_needs_restore = 1U;
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	}
	if (delayed == state->restore_delayed &&
	    delayed->sg_delayed_source_key == state->restore_key)
	{
		if (!state->restore_dispatched)
		{
			if (!state->restore_authorized)
			{
				TimedVaultTicketObservation(state, delayed,
				    &state->game.restore_ticket, &binding, &ticket);
				if (!SG_TimedVaultGameRestoreRelay(&state->game, &ticket,
				        &observation))
				{
					state->deferred = 1U;
					delayed->nextthink = level.time + FRAMETIME;
					return SG_TIMED_VAULT_RUNTIME_HANDLED;
				}
				state->restore_authorized = 1U;
			}
			dispatch.normalize_restore = 1U;
			if (state->short_needs_restore &&
			    !TimedVaultDispatchTargets(&dispatch, state->restore_key,
			        state->game.spec.transaction.device_target_count))
			{
				state->deferred = 1U;
				delayed->nextthink = level.time + FRAMETIME;
				return SG_TIMED_VAULT_RUNTIME_HANDLED;
			}
			state->restore_dispatched = 1U;
		}
		else
			(void)SG_TimedVaultGameStep(&state->game, &observation);
		if (state->game.transaction.restoration !=
		    SG_TIMED_VAULT_RESTORATION_DISCHARGED)
		{
			state->deferred = 1U;
			delayed->nextthink = level.time + FRAMETIME;
		}
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	}
	return SG_TIMED_VAULT_RUNTIME_HANDLED;
}

static sg_timed_vault_game_runtime_state_t *TimedVaultBegin(
	const sg_rune_mechanism_binding_t *binding, sg_bot_t *bot,
	edict_t *activator)
{
	sg_timed_vault_game_runtime_state_t *state;
	sg_timed_vault_game_spec_t spec;
	sg_timed_vault_game_observation_t observation;
	uint64_t short_ticket;
	uint64_t restore_ticket;

	if (!binding || !bot || !activator || !sg_host.level_alloc ||
	    !sg_host.level_free || activator->sg_timed_vault_live ||
	    TimedVaultTargetCount(binding, binding->entry_node->key) != 4U ||
	    TimedVaultTargetCount(binding, binding->destination_node->key) != 9U ||
	    TimedVaultTargetCount(binding, binding->egress_node->key) != 9U ||
	    !TimedVaultTicketPair(&short_ticket, &restore_ticket))
		return NULL;
	memset(&spec, 0, sizeof(spec));
	spec.transaction.source_key = binding->entry_node->key;
	spec.transaction.short_relay_key = binding->destination_node->key;
	spec.transaction.restore_relay_key = binding->egress_node->key;
	spec.transaction.fanout_identity = binding->plan->closure_crc32;
	spec.transaction.dispatch_target_count = 4U;
	spec.transaction.device_target_count = 9U;
	spec.transaction.door_leaf_count = 2U;
	spec.link_index = binding->link_index;
	spec.activator_identity = bot->instance_token;
	spec.short_ticket_id = short_ticket;
	spec.restore_ticket_id = restore_ticket;
	if (!SG_MechCatalogEntityGeneration(binding->destination_entity,
	        &spec.transaction.short_relay_key,
	        &spec.short_relay_generation) ||
	    !SG_MechCatalogEntityGeneration(binding->egress_entity,
	        &spec.transaction.restore_relay_key,
	        &spec.restore_relay_generation))
		return NULL;
	state = sg_host.level_alloc((int)sizeof(*state));
	if (!state)
		return NULL;
	memset(state, 0, sizeof(*state));
	state->rune = binding->rune;
	state->owner = NULL;
	state->activator = activator;
	state->activator_identity = bot->instance_token;
	state->link_index = binding->link_index;
	state->source_key = binding->entry_node->key;
	state->short_key = binding->destination_node->key;
	state->restore_key = binding->egress_node->key;
	state->short_generation = spec.short_relay_generation;
	state->restore_generation = spec.restore_relay_generation;
	observation = TimedVaultObservation(state, binding,
	    (uint32_t)bot->declared_touch_frame);
	if (SG_TimedVaultGameBegin(&state->game, &spec, &observation) !=
	        SG_TIMED_VAULT_GAME_ACTIVE)
	{
		sg_host.level_free(state);
		return NULL;
	}
	activator->sg_timed_vault_live = state;
	return state;
}

sg_timed_vault_runtime_target_result_t SG_TimedVaultRuntimeHandleTargets(
	edict_t *source, edict_t *activator)
{
	sg_timed_vault_game_runtime_state_t *state;
	sg_rune_mechanism_binding_t binding;
	sg_timed_vault_game_observation_t observation;
	timed_vault_dispatch_t dispatch;
	sg_bot_t *bot;
	int entry_dispatched;

	if (source && source->sg_timed_vault_live)
		return TimedVaultRunDelayed(source);
	if (timed_vault_scheduling)
	{
		if (source && activator == timed_vault_scheduling->activator &&
		    (source->s.number == (int)timed_vault_scheduling->short_key ||
		     source->s.number == (int)timed_vault_scheduling->restore_key))
			return SG_TIMED_VAULT_RUNTIME_ALLOW_STOCK;
		return SG_TIMED_VAULT_RUNTIME_NOT_OWNED;
	}
	bot = TimedVaultBotForEntity(activator);
	if (!bot || !TimedVaultBinding(SG_Rune(), (uint32_t)bot->commit_link,
	        &binding) || source != binding.entry_entity)
		return SG_TIMED_VAULT_RUNTIME_NOT_OWNED;
	if (!bot->declared_started || !bot->declared_touched ||
	    bot->declared_guard_paused || source->activator != activator ||
	    source->delay != 0.0f || source->killtarget || source->message ||
	    SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) !=
	        SG_COMPOUND_GUARD_OK)
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	state = TimedVaultBegin(&binding, bot, activator);
	if (!state)
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	observation = TimedVaultObservation(state, &binding,
	    (uint32_t)level.framenum);
	if (!SG_TimedVaultGameDispatch(&state->game, &observation))
	{
		TimedVaultDetachAndFree(state);
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	}
	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.state = state;
	dispatch.binding = binding;
	dispatch.source = source;
	dispatch.activator = activator;
	timed_vault_scheduling = state;
	entry_dispatched = TimedVaultDispatchTargets(&dispatch,
	    state->source_key, 4U);
	timed_vault_scheduling = NULL;
	if (!entry_dispatched || !state->short_delayed ||
	    !state->restore_delayed)
	{
		state->failed_closed = 1U;
		if (state->restore_delayed)
		{
			state->owner = state->restore_delayed;
			TimedVaultCancelShort(state);
			return SG_TIMED_VAULT_RUNTIME_HANDLED;
		}
		TimedVaultCancelShort(state);
		TimedVaultDetachAndFree(state);
		return SG_TIMED_VAULT_RUNTIME_HANDLED;
	}
	state->owner = state->restore_delayed;
	return SG_TIMED_VAULT_RUNTIME_HANDLED;
}

void SG_TimedVaultRuntimeTagDelayedTarget(edict_t *source,
	edict_t *activator, edict_t *delayed)
{
	sg_timed_vault_game_runtime_state_t *state = timed_vault_scheduling;

	if (!state || !source || !delayed || activator != state->activator)
		return;
	if (source->s.number == (int)state->short_key && !state->short_delayed)
	{
		state->short_delayed = delayed;
		delayed->sg_timed_vault_live = state;
		delayed->sg_delayed_source_key = state->short_key;
		delayed->sg_delayed_source_generation = state->short_generation;
	}
	else if (source->s.number == (int)state->restore_key &&
	         !state->restore_delayed)
	{
		state->restore_delayed = delayed;
		delayed->sg_timed_vault_live = state;
		delayed->sg_delayed_source_key = state->restore_key;
		delayed->sg_delayed_source_generation = state->restore_generation;
	}
}

int SG_TimedVaultRuntimeDelayedUseDurable(const edict_t *delayed)
{
	const sg_timed_vault_game_runtime_state_t *state = delayed
		? delayed->sg_timed_vault_live : NULL;

	return state && delayed == state->restore_delayed &&
	       state->game.restore_ticket.spec.durable == 1U;
}

int SG_TimedVaultRuntimeDelayedUseDeferred(const edict_t *delayed)
{
	const sg_timed_vault_game_runtime_state_t *state = delayed
		? delayed->sg_timed_vault_live : NULL;

	return state && delayed == state->restore_delayed && state->deferred;
}

void SG_TimedVaultRuntimeRetireActivator(edict_t *delayed,
	edict_t *activator)
{
	sg_timed_vault_game_runtime_state_t *state = delayed
		? delayed->sg_timed_vault_live : NULL;

	if (!state || state->activator != activator)
		return;
	(void)SG_TimedVaultGameRetireActivator(&state->game,
	    state->activator_identity);
	if (activator && activator->sg_timed_vault_live == state)
		activator->sg_timed_vault_live = NULL;
	if (SG_TimedVaultRuntimeDelayedUseDurable(delayed))
		delayed->activator = NULL;
}

void SG_TimedVaultRuntimeEntityFreed(edict_t *entity)
{
	sg_timed_vault_game_runtime_state_t *state = entity
		? entity->sg_timed_vault_live : NULL;

	if (!state)
		return;
	entity->sg_timed_vault_live = NULL;
	if (state->short_delayed == entity)
		state->short_delayed = NULL;
	if (state->activator == entity)
		state->activator = NULL;
	if (state->restore_delayed == entity || state->owner == entity)
	{
		state->restore_delayed = NULL;
		TimedVaultDetachAndFree(state);
	}
}

static void TimedVaultObserveCarrier(edict_t *activator)
{
	sg_timed_vault_game_runtime_state_t *state = activator
		? activator->sg_timed_vault_live : NULL;
	sg_rune_mechanism_binding_t binding;
	sg_timed_vault_game_observation_t observation;

	if (!state || state->activator != activator ||
	    !TimedVaultBinding(state->rune, state->link_index, &binding))
		return;
	observation = TimedVaultObservation(state, &binding,
	    (uint32_t)level.framenum);
	if (ClientHasFlag(activator) && !state->game.transaction.pickup_seen &&
	    observation.frame > state->game.transaction.last_frame)
		(void)SG_TimedVaultGameFlagPickup(&state->game, &observation);
	else if (observation.frame > state->game.transaction.last_frame &&
	         !(observation.frame == state->game.transaction.ready_frame &&
	           !state->game.transaction.short_relay_seen) &&
	         !(observation.frame == state->game.transaction.restore_frame &&
	           !state->game.transaction.restore_relay_seen))
		(void)SG_TimedVaultGameStep(&state->game, &observation);
}

int SG_TimedVaultRuntimeApplyCommand(edict_t *activator,
	usercmd_t *command)
{
	sg_timed_vault_game_runtime_state_t *state = activator
		? activator->sg_timed_vault_live : NULL;
	sg_timed_vault_command_t owner;

	if (!state || !command)
		return 0;
	TimedVaultObserveCarrier(activator);
	owner = state->failed_closed ? SG_TIMED_VAULT_COMMAND_HOLD :
		SG_TimedVaultGameCommand(&state->game);
	if (owner != SG_TIMED_VAULT_COMMAND_HOLD)
		return 0;
	command->forwardmove = 0;
	command->sidemove = 0;
	command->upmove = 0;
	return 1;
}

int SG_TimedVaultRuntimeCommandFor(const edict_t *activator)
{
	const sg_timed_vault_game_runtime_state_t *state = activator
		? activator->sg_timed_vault_live : NULL;

	return state ? (int)(state->failed_closed ? SG_TIMED_VAULT_COMMAND_HOLD :
	       SG_TimedVaultGameCommand(&state->game)) :
	       (int)SG_TIMED_VAULT_COMMAND_NONE;
}
