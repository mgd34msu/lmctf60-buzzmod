#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rocketjump_game.h"
#include "slipgate/sg_rocketjump_impact.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_util.h"

#include <limits.h>

void ClientThink(edict_t *ent, usercmd_t *ucmd);

#define SG_ROCKETJUMP_IMPACT_EPSILON 2.0f

static sg_bot_t *RocketJumpBotForEntity(const edict_t *entity)
{
	int slot;

	if (!entity)
		return NULL;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == entity)
			return &sg_bots[slot];
	return NULL;
}

static qboolean RocketJumpStageBotIdle(const sg_bot_t *bot)
{
	const sg_mover_ticket_t *ticket;

	if (!bot || SG_CompoundSwimGameOwns(bot))
		return false;
	ticket = &bot->compound_guard.ticket;
	if (ticket->epoch != 0U || ticket->serial != 0U)
		return false;
	if (bot->hook_phase >= 2 ||
	    SG_RocketJumpPhasePhysical(bot->rocketjump.phase) ||
	    bot->jump_started || bot->drop_started ||
	    bot->drop_replay_active || bot->swim_replay_active ||
	    bot->swim_validated || bot->declared_touched ||
	    bot->declared_triggered || bot->declared_activated ||
	    bot->declared_guard_paused)
		return false;
	return true;
}

static qboolean RocketJumpQ8(float value, short *fixed_out)
{
	float scaled;

	if (!fixed_out || !isfinite(value))
		return false;
	scaled = value * 8.0f;
	if (scaled < (float)SHRT_MIN || scaled > (float)SHRT_MAX ||
	    scaled != (float)(int)scaled)
		return false;
	*fixed_out = (short)scaled;
	return true;
}

static qboolean RocketJumpVectorQ8(const vec3_t value, short fixed_out[3])
{
	int axis;

	if (!value || !fixed_out)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!RocketJumpQ8(value[axis], &fixed_out[axis]))
			return false;
	return true;
}

static void RocketJumpVectorQ8Truncated(const vec3_t value,
	short fixed_out[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float scaled = value[axis] * 8.0f;

		if (scaled < (float)SHRT_MIN)
			scaled = (float)SHRT_MIN;
		else if (scaled > (float)SHRT_MAX)
			scaled = (float)SHRT_MAX;
		fixed_out[axis] = (short)scaled;
	}
}

static qboolean RocketJumpObservation(edict_t *entity,
	sg_rocketjump_observation_t *observation)
{
	static gitem_t *launcher;
	static gitem_t *rockets;

	if (!observation)
		return false;
	memset(observation, 0, sizeof(*observation));
	if (!entity || !entity->inuse || !entity->client)
		return false;
	if (!launcher)
	{
		launcher = FindItem("Rocket Launcher");
		rockets = FindItem("Rockets");
	}
	if (!launcher || !rockets ||
	    !RocketJumpVectorQ8(entity->s.origin, observation->origin_q8))
		return false;
	RocketJumpVectorQ8Truncated(entity->velocity,
	    observation->velocity_q8);
	observation->alive = entity->deadflag == DEAD_NO && entity->health > 0;
	observation->grounded = entity->groundentity != NULL;
	observation->dry = entity->waterlevel == 0;
	observation->immutable_support = entity->groundentity == g_edicts ||
	    SG_ImmutableSupport(entity->groundentity);
	observation->normal_move = entity->client->ps.pmove.pm_type == PM_NORMAL &&
	    entity->client->ps.pmove.pm_time == 0;
	observation->standing =
	    !(entity->client->ps.pmove.pm_flags & PMF_DUCKED) &&
	    entity->viewheight == SG_RUNE_PROOF_ROCKETJUMP_VIEWHEIGHT;
	observation->jump_released =
	    !(entity->client->ps.pmove.pm_flags & PMF_JUMP_HELD);
	observation->right_handed =
	    entity->client->pers.hand == RIGHT_HANDED;
	observation->quad_active =
	    entity->client->quad_framenum > level.framenum;
	observation->standard_weapon_law = true;
#ifdef WEAP_BALANCE_OK
	observation->standard_weapon_law =
	    !((int)ctfflags->value & CTF_WEAP_BALANCE);
#endif
	observation->launcher_owned =
	    entity->client->pers.inventory[ITEM_INDEX(launcher)] > 0;
	observation->launcher_selected =
	    entity->client->pers.weapon == launcher;
	observation->weapon_ready = observation->launcher_selected &&
	    entity->client->weaponstate == WEAPON_READY;
	observation->health = entity->health;
	observation->rockets =
	    entity->client->pers.inventory[ITEM_INDEX(rockets)];
	return true;
}

static qboolean RocketJumpWitness(int link_index,
	sg_rocketjump_witness_t *witness)
{
	rune_t *rune = SG_Rune();
	rune_link_t *link;
	int axis;

	if (!witness)
		return false;
	memset(witness, 0, sizeof(*witness));
	if (!rune || !rune->links || !rune->seeds || link_index < 0 ||
	    link_index >= rune->hdr.num_links)
		return false;
	link = &rune->links[link_index];
	if (link->action != RL_ROCKETJUMP || link->from < 0 || link->to < 0 ||
	    link->from >= rune->hdr.num_seeds || link->to >= rune->hdr.num_seeds)
		return false;
	witness->link_index = link_index;
	for (axis = 0; axis < 3; axis++)
	{
		if (!RocketJumpQ8(rune->seeds[link->from].origin[axis],
		        &witness->source_q8[axis]) ||
		    !RocketJumpQ8(rune->seeds[link->to].origin[axis],
		        &witness->destination_q8[axis]))
			return false;
	}
	if (link->anchor[0] != (float)(short)link->anchor[0] ||
	    link->anchor[1] != (float)(short)link->anchor[1] ||
	    link->anchor[2] != (float)(byte)link->anchor[2])
		return false;
	witness->pitch = (short)link->anchor[0];
	witness->yaw = (short)link->anchor[1];
	witness->health_price = (byte)link->anchor[2];
	witness->cost_ms = (unsigned short)link->cost_ms;
	return true;
}

static qboolean RocketJumpArrived(const sg_bot_t *bot)
{
	edict_t *entity;
	vec3_t destination;
	int axis;

	if (!bot || !(entity = bot->ent) || !entity->inuse || !entity->client)
		return false;
	for (axis = 0; axis < 3; axis++)
		destination[axis] =
		    bot->rocketjump.witness.destination_q8[axis] * 0.125f;
	return SG_RocketJumpArrived(entity->s.origin, destination,
	    entity->groundentity != NULL, entity->waterlevel,
	    entity->groundentity, entity);
}

static void RocketJumpCommand(const sg_rocketjump_live_state_t *state,
	const edict_t *entity, sg_rocketjump_command_t control,
	usercmd_t *command)
{
	vec3_t destination, delta;
	float yaw;
	int axis;

	memset(command, 0, sizeof(*command));
	command->msec = SG_ROCKETJUMP_STEP_MS;
	if (control == SG_ROCKETJUMP_COMMAND_FIRE)
	{
		command->angles[PITCH] = state->witness.pitch -
		    entity->client->ps.pmove.delta_angles[PITCH];
		command->angles[YAW] = state->witness.yaw -
		    entity->client->ps.pmove.delta_angles[YAW];
		command->angles[ROLL] =
		    -entity->client->ps.pmove.delta_angles[ROLL];
		command->upmove = 400;
		command->buttons = BUTTON_ATTACK;
		return;
	}
	if (control != SG_ROCKETJUMP_COMMAND_FLIGHT)
		return;
	for (axis = 0; axis < 3; axis++)
		destination[axis] = state->witness.destination_q8[axis] * 0.125f;
	VectorSubtract(destination, entity->s.origin, delta);
	yaw = atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI;
	command->angles[PITCH] =
	    -entity->client->ps.pmove.delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(yaw) -
	    entity->client->ps.pmove.delta_angles[YAW];
	command->angles[ROLL] =
	    -entity->client->ps.pmove.delta_angles[ROLL];
	command->forwardmove = 400;
}

static void RocketJumpReport(const sg_bot_t *bot, const char *event)
{
	if (sg_cv.debug->value && bot && bot->ent)
		sg_host.dprint("RJLIVE %s event=%s link=%d phase=%d elapsed=%d "
		    "failure=%s origin=(%.0f %.0f %.0f) velocity=(%.0f %.0f %.0f) "
		    "grounded=%d\n", bot->ent->client->pers.netname, event,
		    bot->rocketjump.witness.link_index,
		    (int)bot->rocketjump.phase, bot->rocketjump.elapsed_ms,
		    SG_RocketJumpLiveFailureName(bot->rocketjump.failure),
		    bot->ent->s.origin[0], bot->ent->s.origin[1],
		    bot->ent->s.origin[2], bot->ent->velocity[0],
		    bot->ent->velocity[1], bot->ent->velocity[2],
		    bot->ent->groundentity != NULL);
}

int SG_RocketJumpGameOwns(const sg_bot_t *bot)
{
	return bot && SG_RocketJumpLiveOwns(&bot->rocketjump);
}

static qboolean RocketJumpBegin(sg_bot_t *bot, int link_index)
{
	sg_rocketjump_witness_t witness;
	sg_rocketjump_observation_t observation;

	if (!bot || !RocketJumpWitness(link_index, &witness) ||
	    !RocketJumpObservation(bot->ent, &observation) ||
	    !SG_RocketJumpLiveBegin(&bot->rocketjump, &witness, &observation))
		return false;
	RocketJumpReport(bot, "begin");
	return true;
}

static qboolean RocketJumpConsumeTerminal(sg_bot_t *bot)
{
	const char *event;

	if (!bot || (bot->rocketjump.phase != SG_ROCKETJUMP_COMPLETE &&
	             bot->rocketjump.phase != SG_ROCKETJUMP_FAILED))
		return false;
	event = bot->rocketjump.phase == SG_ROCKETJUMP_COMPLETE ?
	    "complete" : "failed";
	RocketJumpReport(bot, event);
	SG_RocketJumpLiveReset(&bot->rocketjump);
	bot->commit_link = -1;
	return true;
}

int SG_RocketJumpGameEmit(sg_bot_t *bot, int selected_link)
{
	edict_t *entity;
	int step;

	if (!bot || !(entity = bot->ent) || !entity->client)
		return 0;
	if (RocketJumpConsumeTerminal(bot))
		return 1;
	if (!SG_RocketJumpGameOwns(bot) && !RocketJumpBegin(bot, selected_link))
		return 0;
	if (bot->rocketjump.phase == SG_ROCKETJUMP_FLIGHT)
	{
		qboolean arrived = RocketJumpArrived(bot);

		RocketJumpReport(bot, "flight");
		if (!SG_RocketJumpLiveBoundary(&bot->rocketjump, arrived,
		        entity->groundentity != NULL))
		{
			RocketJumpReport(bot, "failed-boundary");
			(void)RocketJumpConsumeTerminal(bot);
			return 1;
		}
		if (bot->rocketjump.phase == SG_ROCKETJUMP_COMPLETE)
		{
			RocketJumpReport(bot, "arrived");
			(void)RocketJumpConsumeTerminal(bot);
			return 1;
		}
	}
	for (step = 0; step < SG_ROCKETJUMP_FRAME_STEPS; step++)
	{
		sg_rocketjump_observation_t observation;
		sg_rocketjump_command_t control;
		usercmd_t command;

		if (!RocketJumpObservation(entity, &observation))
		{
			SG_RocketJumpLiveReset(&bot->rocketjump);
			bot->commit_link = -1;
			return 1;
		}
		control = SG_RocketJumpLiveCommand(&bot->rocketjump, &observation);
		if (control == SG_ROCKETJUMP_COMMAND_ZERO)
		{
			RocketJumpReport(bot, "failed-command");
			(void)RocketJumpConsumeTerminal(bot);
			return 1;
		}
		if (control == SG_ROCKETJUMP_COMMAND_EQUIP && step == 0)
		{
			gitem_t *launcher = FindItem("Rocket Launcher");

			if (launcher && launcher->use)
				launcher->use(entity, launcher);
		}
		RocketJumpCommand(&bot->rocketjump, entity, control, &command);
		ClientThink(entity, &command);
		if (!SG_RocketJumpGameOwns(bot))
		{
			RocketJumpReport(bot, "terminal-callback");
			(void)RocketJumpConsumeTerminal(bot);
			return 1;
		}
		if (!SG_RocketJumpLiveStep(&bot->rocketjump,
		        SG_ROCKETJUMP_STEP_MS))
		{
			RocketJumpReport(bot, "failed-time");
			(void)RocketJumpConsumeTerminal(bot);
			return 1;
		}
	}
	return 1;
}

int SG_RocketJumpGameStageAuthenticatedProbe(int link_index)
{
	sg_rocketjump_witness_t witness;
	sg_bot_t *bot = NULL;
	edict_t *entity;
	gitem_t *launcher, *rockets;
	int axis, slot;

	if (!sg_cv.debug || sg_cv.debug->value <= 0.0f ||
	    !RocketJumpWitness(link_index, &witness))
		return 0;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent &&
		    sg_bots[slot].ent->inuse && sg_bots[slot].ent->client &&
		    sg_bots[slot].ent->deadflag == DEAD_NO &&
		    RocketJumpStageBotIdle(&sg_bots[slot]))
		{
			bot = &sg_bots[slot];
			break;
		}
	if (!bot)
		return 0;
	entity = bot->ent;
	launcher = FindItem("Rocket Launcher");
	rockets = FindItem("Rockets");
	if (!launcher || !rockets)
		return 0;

	gi.unlinkentity(entity);
	for (axis = 0; axis < 3; axis++)
	{
		entity->s.origin[axis] = witness.source_q8[axis] * 0.125f;
		entity->s.old_origin[axis] = entity->s.origin[axis];
		entity->client->ps.pmove.origin[axis] = witness.source_q8[axis];
		entity->client->ps.pmove.velocity[axis] = 0;
	}
	VectorClear(entity->velocity);
	VectorClear(entity->client->oldvelocity);
	entity->groundentity = g_edicts;
	entity->groundentity_linkcount = g_edicts->linkcount;
	entity->waterlevel = 0;
	entity->watertype = 0;
	entity->health = entity->max_health > 100 ? entity->max_health : 100;
	entity->deadflag = DEAD_NO;
	entity->viewheight = SG_RUNE_PROOF_ROCKETJUMP_VIEWHEIGHT;
	entity->client->ps.pmove.pm_type = PM_NORMAL;
	entity->client->ps.pmove.pm_flags &= ~(PMF_DUCKED | PMF_JUMP_HELD);
	entity->client->ps.pmove.pm_time = 0;
	entity->client->pers.hand = RIGHT_HANDED;
	entity->client->quad_framenum = 0;
	entity->client->pers.inventory[ITEM_INDEX(launcher)] = 1;
	entity->client->pers.inventory[ITEM_INDEX(rockets)] = 5;
	entity->client->pers.weapon = launcher;
	entity->client->newweapon = NULL;
	entity->client->ammo_index = ITEM_INDEX(rockets);
	entity->client->weaponstate = WEAPON_READY;
	entity->client->ps.gunindex = gi.modelindex(launcher->view_model);
	entity->client->weapon_thunk = false;
	entity->client->buttons = 0;
	entity->client->oldbuttons = 0;
	entity->client->latched_buttons = 0;
	memset(&bot->rocketjump, 0, sizeof(bot->rocketjump));
	bot->seed = SG_Rune()->links[link_index].from;
	VectorCopy(entity->s.origin, bot->last_origin);
	bot->commit_link = link_index;
	bot->sticky_link = link_index;
	bot->commit_until = level.time + 5.0f;
	bot->latch_until = level.time + 5.0f;
	gi.linkentity(entity);

	if (!SG_RocketJumpGameEmit(bot, link_index) ||
	    bot->rocketjump.phase != SG_ROCKETJUMP_FLIGHT)
		return 0;
	sg_host.dprint("RJLIVE %s event=probe-staged link=%d\n",
	    entity->client->pers.netname, link_index);
	return 1;
}

static qboolean RocketJumpProjectileKey(const edict_t *projectile,
	sg_rocketjump_projectile_key_t *key_out)
{
	if (!key_out)
		return false;
	memset(key_out, 0, sizeof(*key_out));
	return SG_MechCatalogEntityGeneration(projectile, &key_out->key,
	    &key_out->generation) ? true : false;
}

void SG_RocketJumpGameFired(edict_t *owner, edict_t *projectile)
{
	sg_bot_t *bot = RocketJumpBotForEntity(owner);
	sg_rocketjump_projectile_key_t key;
	vec3_t direction, end;
	trace_t trace;
	short expected_q8[3];

	if (!bot || bot->rocketjump.phase != SG_ROCKETJUMP_ARMED || !projectile ||
	    projectile->owner != owner ||
	    !RocketJumpProjectileKey(projectile, &key))
		return;
	VectorCopy(projectile->velocity, direction);
	if (VectorNormalize(direction) <= 0.0f)
		return;
	VectorMA(projectile->s.origin, 8192.0f, direction, end);
	trace = sg_host.trace(projectile->s.origin, NULL, NULL, end, owner,
	                     MASK_SHOT);
	if (!SG_RocketJumpWorldImpact(&trace))
		return;
	RocketJumpVectorQ8Truncated(trace.endpos, expected_q8);
	if (!SG_RocketJumpLiveFired(&bot->rocketjump, key, expected_q8))
		return;
	RocketJumpReport(bot, "fired");
}

static qboolean RocketJumpExpectedImpact(const sg_bot_t *bot,
	const edict_t *projectile)
{
	vec3_t expected, delta;
	int axis;

	if (!bot || !projectile)
		return false;
	for (axis = 0; axis < 3; axis++)
		expected[axis] = bot->rocketjump.expected_impact_q8[axis] * 0.125f;
	VectorSubtract(projectile->s.origin, expected, delta);
	return VectorLength(delta) <= SG_ROCKETJUMP_IMPACT_EPSILON;
}

void SG_RocketJumpGameImpactBegin(edict_t *projectile, edict_t *other,
	const csurface_t *surface)
{
	sg_bot_t *bot;
	sg_rocketjump_projectile_key_t key;
	short velocity_q8[3];
	vec3_t expected;
	int axis;

	if (!projectile || !(bot = RocketJumpBotForEntity(projectile->owner)) ||
	    bot->rocketjump.phase != SG_ROCKETJUMP_FLIGHT ||
	    !RocketJumpProjectileKey(projectile, &key))
		return;
	for (axis = 0; axis < 3; axis++)
		expected[axis] = bot->rocketjump.expected_impact_q8[axis] * 0.125f;
	if (sg_cv.debug->value)
		sg_host.dprint("RJLIVE %s event=impact actual=(%.1f %.1f %.1f) "
		    "expected=(%.1f %.1f %.1f)\n",
		    bot->ent->client->pers.netname, projectile->s.origin[0],
		    projectile->s.origin[1], projectile->s.origin[2], expected[0],
		    expected[1], expected[2]);
	RocketJumpVectorQ8Truncated(bot->ent->velocity, velocity_q8);
	(void)SG_RocketJumpLiveImpactBegin(&bot->rocketjump, key,
	    SG_RocketJumpStaticWorldAuthenticated(other),
	    surface && (surface->flags & SURF_SKY),
	    SG_RocketJumpStaticWorldAuthenticated(other) &&
	        RocketJumpExpectedImpact(bot, projectile),
	    bot->ent->health, velocity_q8);
}

void SG_RocketJumpGameImpactEnd(edict_t *projectile)
{
	sg_bot_t *bot;
	sg_rocketjump_projectile_key_t key;
	short velocity_q8[3];

	if (!projectile || !(bot = RocketJumpBotForEntity(projectile->owner)) ||
	    !bot->rocketjump.impact_pending ||
	    !RocketJumpProjectileKey(projectile, &key))
		return;
	RocketJumpVectorQ8Truncated(bot->ent->velocity, velocity_q8);
	if (SG_RocketJumpLiveImpactEnd(&bot->rocketjump, key,
	        bot->ent->health, velocity_q8))
		RocketJumpReport(bot, "blast");
	else
		RocketJumpReport(bot, "failed-blast");
}

void SG_RocketJumpGameProjectileFreed(edict_t *projectile)
{
	sg_bot_t *bot;
	sg_rocketjump_projectile_key_t key;
	short velocity_q8[3] = { 0, 0, 0 };

	if (!projectile || !(bot = RocketJumpBotForEntity(projectile->owner)) ||
	    bot->rocketjump.phase != SG_ROCKETJUMP_FLIGHT ||
	    bot->rocketjump.impact_confirmed ||
	    !RocketJumpProjectileKey(projectile, &key))
		return;
	(void)SG_RocketJumpLiveImpactBegin(&bot->rocketjump, key, false, false,
	    false, bot->ent->health, velocity_q8);
	RocketJumpReport(bot, "projectile-freed");
}
