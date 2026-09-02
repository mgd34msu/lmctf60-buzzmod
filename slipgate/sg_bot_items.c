#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_items.h"

#include <math.h>
#include <string.h>

#include "sg_rune_level.h"
#include "sg_bot_util.h"
#include "sg_bot_cvars.h"

#define DETOUR_RADIUS 900.0f      /* farther than this is a trip, not a detour */
#define WORTH_FLOOR 12.0f         /* worth per second of detour below which no */
#define RUN_SPEED 300.0f

int ArmorIndex(edict_t *ent);

/* What this item is worth to this body now, in health-equivalent points. */
static float Worth(const edict_t *self, const edict_t *item)
{
	const gitem_t *it = item->item;
	gclient_t *client = self->client;

	if (!it)
		return 0.0f;
	/* A flag is an objective, never a pickup on the way.  The game spawns
	 * the flag entity with classname "flag"; the item definition says
	 * item_flag_team*. */
	if ((it->classname && !strncmp(it->classname, "item_flag", 9)) ||
		(item->classname && (!strcmp(item->classname, "flag") ||
			!strncmp(item->classname, "item_flag", 9))))
		return 0.0f;
	if (it->flags & IT_WEAPON)
	{
		int owned = client->pers.inventory[ITEM_INDEX(it)] > 0;

		if (!strcmp(it->pickup_name, "Blaster") ||
			!strcmp(it->pickup_name, "Grappling Hook"))
			return 0.0f;
		if (owned)
			return it->ammo ? 6.0f : 0.0f;   /* the ammo it comes with */
		if (!strcmp(it->pickup_name, "Railgun") ||
			!strcmp(it->pickup_name, "Rocket Launcher"))
			return 60.0f;
		if (!strcmp(it->pickup_name, "Chaingun") ||
			!strcmp(it->pickup_name, "HyperBlaster") ||
			!strcmp(it->pickup_name, "Super Shotgun"))
			return 40.0f;
		return 20.0f;
	}
	if (it->flags & IT_AMMO)
	{
		int have = client->pers.inventory[ITEM_INDEX(it)];

		if (have >= 30)
			return 0.0f;
		return 20.0f * (1.0f - (float)have / 30.0f);
	}
	if (it->flags & IT_ARMOR)
	{
		const gitem_armor_t *armor = it->info;
		int index = ArmorIndex((edict_t *)self);
		int have = index ? client->pers.inventory[index] : 0;
		int gain;

		if (!armor)
			return 0.0f;
		gain = armor->max_count - have;
		if (gain <= 0)
			gain = armor->base_count / 4;    /* a shard-sized top-up at most */
		return (float)gain * 0.6f;
	}
	if (it->flags & IT_POWERUP)
		return 80.0f;
	if (it->classname && !strncmp(it->classname, "item_health", 11))
	{
		int gain = item->count > 0 ? item->count : 10;
		int deficit = self->max_health - self->health;

		if (item->style & 2)         /* HEALTH_IGNORE_MAX: mega and stims */
			return (float)gain * (deficit > 0 ? 1.0f : 0.5f);
		if (deficit <= 0)
			return 0.0f;
		return (float)(gain < deficit ? gain : deficit) * 1.2f;
	}
	return 0.0f;
}

static qboolean Available(const edict_t *item)
{
	return item->inuse && item->item && item->solid == SOLID_TRIGGER &&
		!(item->svflags & SVF_NOCLIENT);
}

int SG_BotItemDetour(sg_bot_t *bot, const sg_rune_field_t *goal_field,
	vec3_t point_out)
{
	edict_t *self = bot->ent;
	int i;
	float best_rate = WORTH_FLOOR;
	edict_t *best = NULL;
	float here_cost;

	if (!self || !self->client || !goal_field || !goal_field->cost ||
		bot->cell == SG_RUNE_CX_INDEX_NONE)
		return 0;
	here_cost = goal_field->cost[SG_RUNE_FIELD_STATE(bot->cell,
		bot->crouching)];
	for (i = game.maxclients + 1; i < globals.num_edicts; i++)
	{
		edict_t *item = &g_edicts[i];
		float worth, distance, there_cost, detour, rate;
		uint32_t cell;
		vec3_t lowered;

		if (!Available(item))
			continue;
		/* A carrier goes only for health, and only when it is hurt. */
		if (bot->role == SG_ROLE_CARRY && !(item->item && item->item->classname &&
			!strncmp(item->item->classname, "item_health", 11)))
			continue;
		distance = SG_DistXY(self->s.origin, item->s.origin);
		if (distance > DETOUR_RADIUS)
			continue;
		worth = Worth(self, item);
		if (worth <= 0.0f)
			continue;
		/* The item's cell: a body standing at the item. */
		VectorCopy(item->s.origin, lowered);
		lowered[2] += 24.0f;
		cell = SG_RuneLevelLocate(lowered, 0, NULL);
		if (cell == SG_RUNE_CX_INDEX_NONE)
		{
			lowered[2] += 24.0f;
			cell = SG_RuneLevelLocate(lowered, 0, NULL);
		}
		if (cell == SG_RUNE_CX_INDEX_NONE)
		{
			lowered[2] = item->s.origin[2];
			cell = SG_RuneLevelLocate(lowered, 0, NULL);
		}
		if (cell == SG_RUNE_CX_INDEX_NONE)
			continue;
		there_cost = goal_field->cost[SG_RUNE_FIELD_STATE(cell, 0)];
		if (!(there_cost < INFINITY))
			continue;
		detour = distance / RUN_SPEED + there_cost - here_cost;
		if (detour < 0.5f)
			detour = 0.5f;
		rate = worth / detour;
		if (rate > best_rate)
		{
			best_rate = rate;
			best = item;
		}
	}
	if (!best)
		return 0;
	if (sg_cv.debug && sg_cv.debug->value && bot->detour_logged != best)
	{
		bot->detour_logged = best;
		gi.dprintf("SGITEM %s detours for %s at (%.0f %.0f %.0f) rate %.1f\n",
			self->client->pers.netname, best->classname ? best->classname : "?",
			best->s.origin[0], best->s.origin[1], best->s.origin[2], best_rate);
	}
	VectorCopy(best->s.origin, point_out);
	point_out[2] += 24.0f;
	return 1;
}
