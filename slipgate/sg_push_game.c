#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_push_game.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_util.h"

#include <limits.h>

void ClientThink(edict_t *ent, usercmd_t *ucmd);

static sg_bot_t *PushBotForEntity(const edict_t *entity)
{
	int slot;

	for (slot = 0; entity && slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == entity)
			return &sg_bots[slot];
	return NULL;
}

static qboolean PushObservation(edict_t *entity,
	sg_push_observation_t *observation)
{
	if (!entity || !entity->inuse || !entity->client || !observation)
		return false;
	memset(observation, 0, sizeof(*observation));
	memcpy(observation->origin_q8, entity->client->ps.pmove.origin,
		sizeof(observation->origin_q8));
	observation->alive = entity->deadflag == DEAD_NO && entity->health > 0;
	observation->grounded = entity->groundentity != NULL;
	observation->dry = entity->waterlevel == 0;
	return true;
}

static qboolean PushWitness(int link_index, sg_push_witness_t *witness)
{
	rune_t *rune = SG_Rune();
	sg_rune_mechanism_binding_t binding;
	const rune_link_t *link;
	int axis;

	if (!witness)
		return false;
	memset(witness, 0, sizeof(*witness));
	if (!rune || !rune->links || !rune->seeds || link_index < 0 ||
	    link_index >= rune->hdr.num_links ||
	    !SG_RuneMechanismBindingCapture(rune, (uint32_t)link_index,
	        &binding) || !binding.link || !binding.plan ||
	    !binding.entry_node || !binding.entry_entity)
		return false;
	link = &rune->links[link_index];
	if (link->action != RL_PUSH || binding.link != link ||
	    binding.plan->controller_kind != SG_MECHANISM_CONTROLLER_PUSH ||
	    link->from < 0 || link->to < 0 ||
	    link->from >= rune->hdr.num_seeds || link->to >= rune->hdr.num_seeds)
		return false;
	witness->link_index = link_index;
	witness->entry_key = binding.entry_node->key;
	witness->cost_ms = (unsigned short)link->cost_ms;
	memcpy(witness->push_velocity, binding.entry_node->push_velocity,
		sizeof(witness->push_velocity));
	for (axis = 0; axis < 3; axis++)
	{
		float source = rune->seeds[link->from].origin[axis] * 8.0f;
		float destination = rune->seeds[link->to].origin[axis] * 8.0f;

		if (!isfinite(source) || !isfinite(destination) ||
		    source < SHRT_MIN || source > SHRT_MAX ||
		    destination < SHRT_MIN || destination > SHRT_MAX ||
		    source != (float)(short)source ||
		    destination != (float)(short)destination)
			return false;
		witness->source_q8[axis] = (short)source;
		witness->destination_q8[axis] = (short)destination;
	}
	return true;
}

static qboolean PushArrived(const sg_bot_t *bot,
	const sg_push_observation_t *observation)
{
	return bot && observation && observation->grounded && observation->dry &&
	       SG_PushArrivalEnvelope(observation->origin_q8,
	           bot->push.witness.destination_q8);
}

static edict_t *PushEntry(const sg_bot_t *bot)
{
	sg_rune_mechanism_binding_t binding;

	if (!bot || bot->push.witness.link_index < 0 ||
	    !SG_RuneMechanismBindingCapture(SG_Rune(),
	        (uint32_t)bot->push.witness.link_index, &binding) ||
	    !binding.link || !binding.plan || !binding.entry_entity ||
	    binding.link->action != RL_PUSH ||
	    binding.plan->controller_kind != SG_MECHANISM_CONTROLLER_PUSH)
		return NULL;
	return binding.entry_entity;
}

static void PushReport(const sg_bot_t *bot, const char *event)
{
	if (sg_cv.debug && sg_cv.debug->value && bot && bot->ent)
		sg_host.dprint("PUSHLIVE %s event=%s link=%d phase=%d elapsed=%d "
		    "failure=%s origin=(%.0f %.0f %.0f) velocity=(%.0f %.0f %.0f)\n",
		    bot->ent->client->pers.netname, event,
		    bot->push.witness.link_index, (int)bot->push.phase,
		    bot->push.elapsed_ms, SG_PushLiveFailureName(bot->push.failure),
		    bot->ent->s.origin[0], bot->ent->s.origin[1],
		    bot->ent->s.origin[2], bot->ent->velocity[0],
		    bot->ent->velocity[1], bot->ent->velocity[2]);
}

int SG_PushGameOwns(const sg_bot_t *bot)
{
	return bot && SG_PushLiveOwns(&bot->push);
}

static qboolean PushSelected(int link_index)
{
	rune_t *rune = SG_Rune();

	return rune && rune->links && link_index >= 0 &&
	       link_index < rune->hdr.num_links &&
	       rune->links[link_index].action == RL_PUSH;
}

static qboolean PushBegin(sg_bot_t *bot, int link_index)
{
	sg_push_witness_t witness;
	sg_push_observation_t observation;

	if (!bot || !PushWitness(link_index, &witness) ||
	    !SG_BallisticSurvivable(bot->ent,
	        &SG_Rune()->links[link_index]) ||
	    !PushObservation(bot->ent, &observation) ||
	    !SG_PushLiveBegin(&bot->push, &witness, &observation))
		return false;
	PushReport(bot, "begin");
	return true;
}

static qboolean PushConsumeTerminal(sg_bot_t *bot)
{
	const char *event;

	if (!bot || (bot->push.phase != SG_PUSH_COMPLETE &&
	             bot->push.phase != SG_PUSH_FAILED))
		return false;
	event = bot->push.phase == SG_PUSH_COMPLETE ? "complete" : "failed";
	PushReport(bot, event);
	SG_PushLiveReset(&bot->push);
	bot->commit_link = -1;
	return true;
}

int SG_PushGameEmit(sg_bot_t *bot, int selected_link)
{
	edict_t *entity;
	int step;

	if (!bot || !(entity = bot->ent) || !entity->client)
		return 0;
	if (PushConsumeTerminal(bot))
		return 1;
	if (!SG_PushGameOwns(bot))
	{
		if (!PushSelected(selected_link))
			return 0;
		if (!PushBegin(bot, selected_link))
		{
			SG_PushLiveReset(&bot->push);
			bot->commit_link = -1;
			return 1;
		}
	}
	for (step = 0; step < SG_PUSH_FRAME_STEPS; step++)
	{
		sg_push_observation_t observation;
		usercmd_t command;

		if (!PushObservation(entity, &observation))
		{
			SG_PushLiveReset(&bot->push);
			bot->commit_link = -1;
			return 1;
		}
		if (bot->push.phase == SG_PUSH_FLIGHT &&
		    !SG_PushLiveBoundary(&bot->push,
		        PushArrived(bot, &observation), observation.grounded))
		{
			(void)PushConsumeTerminal(bot);
			return 1;
		}
		if (bot->push.phase == SG_PUSH_COMPLETE)
		{
			(void)PushConsumeTerminal(bot);
			return 1;
		}
		if (bot->push.phase == SG_PUSH_APPROACH &&
		    !SG_BallisticSurvivable(entity,
		        &SG_Rune()->links[bot->push.witness.link_index]))
		{
			SG_PushLiveReset(&bot->push);
			bot->commit_link = -1;
			return 1;
		}
		(void)SG_PushLiveCommand(&bot->push, &observation);
		if (!SG_PushGameOwns(bot))
		{
			(void)PushConsumeTerminal(bot);
			return 1;
		}
		memset(&command, 0, sizeof(command));
		command.msec = SG_PUSH_STEP_MS;
		if (bot->push.phase == SG_PUSH_APPROACH)
		{
			edict_t *entry = PushEntry(bot);
			vec3_t target;

			if (!entry)
			{
				SG_PushLiveReset(&bot->push);
				bot->commit_link = -1;
				return 1;
			}
			VectorAdd(entry->absmin, entry->absmax, target);
			VectorScale(target, 0.5f, target);
			if (!SG_DeclaredCommand(entity->s.origin, target,
			        &entity->client->ps.pmove, &command))
			{
				SG_PushLiveReset(&bot->push);
				bot->commit_link = -1;
				return 1;
			}
		}
		ClientThink(entity, &command);
		if (!SG_PushGameOwns(bot) ||
		    !SG_PushLiveStep(&bot->push, SG_PUSH_STEP_MS))
		{
			(void)PushConsumeTerminal(bot);
			return 1;
		}
	}
	return 1;
}

void SG_PushGameTouched(edict_t *trigger, edict_t *entity)
{
	sg_bot_t *bot = PushBotForEntity(entity);
	sg_rune_mechanism_binding_t binding;
	uint32_t key = 0U;
	uint32_t generation = 0U;

	if (!bot || (bot->push.phase != SG_PUSH_APPROACH &&
	             bot->push.phase != SG_PUSH_FLIGHT) || !trigger ||
	    !entity || !entity->client)
		return;
	if (!SG_MechCatalogEntityGeneration(trigger, &key, &generation) ||
	    generation == 0U ||
	    !SG_RuneMechanismBindingCapture(SG_Rune(),
	        (uint32_t)bot->push.witness.link_index, &binding) ||
	    binding.entry_entity != trigger ||
	    memcmp(entity->velocity, entity->client->oldvelocity,
	        sizeof(entity->velocity)) != 0)
	{
		key = 0U;
		(void)SG_PushLiveTouched(&bot->push, key, entity->velocity);
		return;
	}
	(void)SG_PushLiveTouched(&bot->push, key, entity->velocity);
}
