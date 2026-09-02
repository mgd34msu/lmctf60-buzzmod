/* sg_bot_frame.c -- the era-4 bot driver.
 *
 * Once per server frame, for every bot: decide the role, turn the role into
 * a world destination, find the body's cell and the destination's cell,
 * take the field's step, and let the executor turn that step into the
 * command the host's own client think consumes.  Combat owns the view
 * unless the step needs it.  Nothing here knows the map except through the
 * RUNE runtime. */
#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"

#include <math.h>
#include <string.h>

#include "sg_bot_orders.h"
#include "sg_bot_callout.h"
#include "sg_bot_host.h"
#include "sg_bot_combat.h"
#include "sg_bot_items.h"
#include "sg_bot_cvars.h"
#include "sg_engine_facts.h"
#include "sg_rune_flight.h"
#include "sg_rune_level.h"
#include "sg_rune_mechanisms.h"
#include "sg_tactic_controller.h"
#include "sg_bot_util.h"

void Cmd_Hook_f(edict_t *ent);
void ClientThink(edict_t *ent, usercmd_t *ucmd);

static qboolean sg_level_setup_attempted;

/* ---- level ------------------------------------------------------------- */

/* Loads this level's RUNE once; a second call while it is current for
 * the same map is a no-op, so adding a bot never reloads the artifact. */
qboolean SG_LevelSetup(void)
{
	if (SG_RuneLevelCurrent() &&
		!strcmp(sg_rune_level.mapname, level.mapname))
		return true;
	sg_level_setup_attempted = true;
	return SG_RuneLevelBegin(level.mapname) ? true : false;
}

void SG_LevelSetupAfterRuneWrite(void)
{
	if (!SG_RuneLevelCurrent())
		(void)SG_LevelSetup();
}

void SG_LevelChange(void)
{
	int i;

	SG_RuneLevelClear();
	SG_OrdersReset();
	SG_BotCombatReset();
	sg_level_setup_attempted = false;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active)
			SG_BotSlotInit(&sg_bots[i]);
}

void SG_RuneLevelStorageWillFree(void)
{
	SG_RuneLevelClear();
	sg_level_setup_attempted = false;
}

void SG_BotSlotInit(sg_bot_t *bot)
{
	edict_t *ent = bot->ent;
	qboolean active = bot->active;
	unsigned long long token = bot->instance_token;
	int lives = bot->lives;

	memset(bot, 0, sizeof(*bot));
	bot->ent = ent;
	bot->active = active;
	bot->instance_token = token;
	bot->lives = lives;
	bot->cell = SG_RUNE_CX_INDEX_NONE;
	bot->destination_cell = SG_RUNE_CX_INDEX_NONE;
	bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
	bot->step.kind = SG_RUNE_STEP_HOLD;
	bot->role = SG_ROLE_ATTACK;
	bot->last_role = SG_ROLE_ATTACK;
}

/* ---- roles and destinations --------------------------------------------- */

static qboolean BotCarrying(edict_t *e)
{
	return e && e->client && ClientHasFlag(e) != NULL;
}

static edict_t *TeamCarrier(int team)
{
	int client;

	for (client = 0; client < game.maxclients; client++)
	{
		edict_t *entity = &g_edicts[client + 1];

		if (entity->inuse && entity->client &&
			entity->client->ctf.teamnum == team && ClientHasFlag(entity))
			return entity;
	}
	return NULL;
}

typedef struct team_pass_s
{
	int assigned[SG_MAXBOTS];
	int previous[SG_MAXBOTS];  /* the assignment before the last event */
	/* The strategy holds until an event: per team, what the last pass saw. */
	unsigned situation[2];
	int situation_valid[2];
	/* A powerup a teammate has seen standing, and how long that is trusted. */
	edict_t *powerup[2];
	float powerup_known_until[2];
} team_pass_t;

static team_pass_t sg_team_pass;

static unsigned TeamSituation(int team)
{
	unsigned key = 2166136261U;
	int i;
	edict_t *ours = TeamCarrier(SG_EnemyTeam(team));    /* carries our flag */
	edict_t *theirs = TeamCarrier(team);                /* carries theirs */
	edict_t *our_flag = ctf_flagsearch(team);
	edict_t *their_flag = ctf_flagsearch(SG_EnemyTeam(team));
	int our_state = ours ? 1 : (our_flag && VectorLength(our_flag->homeposition) > 0.0f &&
		!VectorCompare(our_flag->s.origin, our_flag->homeposition) ? 2 : 0);
	int their_state = theirs ? 1 : (their_flag && VectorLength(their_flag->homeposition) > 0.0f &&
		!VectorCompare(their_flag->s.origin, their_flag->homeposition) ? 2 : 0);

	key = (key ^ (unsigned)our_state) * 16777619U;
	key = (key ^ (unsigned)their_state) * 16777619U;
	key = (key ^ (unsigned)(ours ? ours->s.number : 0)) * 16777619U;
	key = (key ^ (unsigned)(theirs ? theirs->s.number : 0)) * 16777619U;
	key = (key ^ (unsigned)(sg_team_pass.powerup[SG_TeamIdx(team) ? 1 : 0] ?
		sg_team_pass.powerup[SG_TeamIdx(team) ? 1 : 0]->s.number : 0)) * 16777619U;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *e = sg_bots[i].ent;
		unsigned present = sg_bots[i].active && e && e->client &&
			e->client->ctf.teamnum == team ? 1U : 0U;
		unsigned ordered = present ? (unsigned)(SG_OrderedRole(e) + 1) : 0U;

		key = (key ^ (present | (ordered << 1))) * 16777619U;
	}
	return key;
}

#define ROLE_KEEP_SCALE 0.6f     /* a held role counts this much nearer */

static uint32_t StandingCellNear(const vec3_t point);
static qboolean FlagNowOrHome(int team, vec3_t out);
static float DistanceTo(const edict_t *e, const vec3_t point);

#define ESCORT_GAP_SECONDS 1.0f   /* an escort holds this far from its point */
#define ESCORT_STANDOFF 96.0f     /* and this far from a carrier that stands */
#define ESCORT_AHEAD 128.0f       /* the escort's point runs this far ahead of a moving carrier */
#define CARRIER_DETOUR_HEALTH 40  /* under this a carrier will go for health */
#define DEFEND_PATROL_RADIUS 640.0f /* a posted defender stocks up within this of the flag */
#define DEFEND_PATROL_SECONDS 8.0f  /* and gives a pickup this long */

uint32_t SG_BotStandingCellNear(const vec3_t point)
{
	return StandingCellNear(point);
}

static int RoleFor(sg_bot_t *bot, qboolean carrying)
{
	int slot = (int)(bot - sg_bots);
	int role;

	if (carrying)
		return SG_ROLE_CARRY;
	role = slot >= 0 && slot < SG_MAXBOTS ? sg_team_pass.assigned[slot] : -1;
	if (role < 0 || role >= SG_ROLES)
		role = SG_ROLE_ATTACK;
	if (role == SG_ROLE_DEFEND)
		bot->def_stand = true;
	return role;
}

/* Where a team's flag is now: with its carrier, or wherever the game's one
 * flag entity for that team stands (its base, or where it was dropped). */
static qboolean FlagNow(int team, vec3_t out)
{
	edict_t *flag = ctf_flagsearch(team);
	edict_t *carrier = TeamCarrier(SG_EnemyTeam(team));

	if (carrier)
	{
		VectorCopy(carrier->s.origin, out);
		return true;
	}
	if (flag)
	{
		VectorCopy(flag->s.origin, out);
		return true;
	}
	return false;
}

/* The flag's stand: the game keeps it on the flag entity, which itself
 * moves when the flag is dropped. */
static qboolean FlagHome(int team, vec3_t out)
{
	edict_t *flag = ctf_flagsearch(team);

	if (!flag)
		return false;
	/* At home the flag entity itself marks the stand (it dropped to the
	 * floor on spawn); away, the spawn point does. */
	if (VectorLength(flag->homeposition) > 0.0f &&
		!ctf_flagatposition(flag->homeposition, flag->s.origin))
		VectorCopy(flag->homeposition, out);
	else
		VectorCopy(flag->s.origin, out);
	return true;
}

/* The team pass: once per frame per team, before any bot thinks.  Roles
 * are assigned across the team's bots so that our flag is never left
 * unguarded, a taken flag is chased by the bots nearest it, our carrier is
 * escorted by the bot nearest to it, and the rest attack.  Human orders
 * override the assignment for the bot they name. */
static float DistanceTo(const edict_t *e, const vec3_t point)
{
	vec3_t delta;

	VectorSubtract(point, e->s.origin, delta);
	return VectorLength(delta);
}

/* The nearest free bot to a point for a role.  The bot that held the role
 * last frame counts as nearer than it is, so a role does not flip between
 * two bots at like distances every frame. */
static int NearestUnassigned(int team, const vec3_t point, int role)
{
	int i, best = -1;
	float best_distance = 1.0e30f;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *e = sg_bots[i].ent;
		float distance;

		if (!sg_bots[i].active || !e || !e->client ||
			e->client->ctf.teamnum != team || sg_team_pass.assigned[i] >= 0 ||
			e->health <= 0)
			continue;
		distance = DistanceTo(e, point);
		if (sg_team_pass.previous[i] == role)
			distance *= ROLE_KEEP_SCALE;
		if (distance < best_distance)
		{
			best_distance = distance;
			best = i;
		}
	}
	return best;
}

#define POWERUP_MEMORY 45.0f     /* a sighting is trusted this long */
#define POWERUP_SIGHT 1500.0f    /* and only within this distance */

/* The quad and the techs: objectives, not detours.  A tech in hand makes
 * the others worthless to that body. */
static qboolean PowerupItem(const edict_t *item)
{
	if (!item->inuse || !item->item || item->solid != SOLID_TRIGGER ||
		(item->svflags & SVF_NOCLIENT) || !(item->item->flags & IT_POWERUP))
		return false;
	if (!item->classname || (strcmp(item->classname, "item_quad") != 0 &&
		strstr(item->classname, "_rune") == NULL))
		return false;
	/* Standing on floor: a tech in flight or in the lava is nowhere to go. */
	{
		uint32_t cell = SG_BotStandingCellNear(item->s.origin);

		return cell != SG_RUNE_CX_INDEX_NONE &&
			(sg_rune_level.artifact.complex.cells[cell].semantics & SG_RUNE_CX_CELL_SUPPORTED);
	}
}

static qboolean PowerupWanted(const edict_t *e, const edict_t *item)
{
	if (!item->classname || !strstr(item->classname, "_rune"))
		return true;
	return e->client->rune == NULL;
}

/* Every powerup standing in view of a teammate becomes known to the team;
 * one is remembered at a time, the nearest to a bot when several. */
static void SightPowerups(int team)
{
	int side = SG_TeamIdx(team) ? 1 : 0;
	int i;
	edict_t *known = sg_team_pass.powerup[side];

	if (known && (!PowerupItem(known) || level.time > sg_team_pass.powerup_known_until[side]))
	{
		known = NULL;
		sg_team_pass.powerup[side] = NULL;
	}
	for (i = game.maxclients + 1; i < globals.num_edicts; i++)
	{
		edict_t *item = &g_edicts[i];
		int b;

		if (!PowerupItem(item))
			continue;
		for (b = 0; b < SG_MAXBOTS; b++)
		{
			edict_t *e = sg_bots[b].ent;
			vec3_t eye;
			trace_t tr;

			if (!sg_bots[b].active || !e || !e->client || e->health <= 0 ||
				e->client->ctf.teamnum != team)
				continue;
			if (item == known)
			{
				sg_team_pass.powerup_known_until[side] = level.time + POWERUP_MEMORY;
				break;
			}
			if (DistanceTo(e, item->s.origin) > POWERUP_SIGHT ||
				!gi.inPVS(e->s.origin, item->s.origin))
				continue;
			VectorCopy(e->s.origin, eye);
			eye[2] += (float)e->viewheight;
			tr = gi.trace(eye, NULL, NULL, item->s.origin, e, MASK_OPAQUE);
			if (tr.fraction < 1.0f)
				continue;
			if (!known)
			{
				sg_team_pass.powerup[side] = item;
				sg_team_pass.powerup_known_until[side] = level.time + POWERUP_MEMORY;
				known = item;
				SG_BotCalloutPowerup(e, item);
			}
			break;
		}
	}
}


/* What the team's strategy depends on, as one number: where each flag is
 * (home, carried, dropped), who carries, and which bots are on the team.
 * The same number means no event: the roles stand. */
static void TeamPass(int team)
{
	int i, count = 0, defenders = 0;
	edict_t *our_carrier = TeamCarrier(team);
	edict_t *their_carrier = TeamCarrier(SG_EnemyTeam(team));
	vec3_t home, flag_now;
	qboolean have_home = FlagHome(team, home);
	int side = SG_TeamIdx(team) ? 1 : 0;
	unsigned situation;

	SightPowerups(team);
	situation = TeamSituation(team);

	/* No event since the last pass: the strategy stands.  A bot's role is
	 * its own until a flag moves, a carrier changes, or the roster does;
	 * the steps under it change as they like. */
	if (sg_team_pass.situation_valid[side] && sg_team_pass.situation[side] == situation)
	{
		for (i = 0; i < SG_MAXBOTS; i++)
		{
			edict_t *e = sg_bots[i].ent;

			if (!sg_bots[i].active || !e || !e->client || e->client->ctf.teamnum != team)
				continue;
			if (ClientHasFlag(e))
				sg_team_pass.assigned[i] = SG_ROLE_CARRY;
			else if (sg_team_pass.assigned[i] < 0 || sg_team_pass.assigned[i] >= SG_ROLES ||
				sg_team_pass.assigned[i] == SG_ROLE_CARRY)
				sg_team_pass.assigned[i] = SG_ROLE_ATTACK;
		}
		return;
	}
	sg_team_pass.situation[side] = situation;
	sg_team_pass.situation_valid[side] = 1;
	if (sg_cv.debug && sg_cv.debug->value)
		gi.dprintf("SGTEAM %s: event, roles reassigned (our flag %s, theirs %s)\n",
			team == CTF_TEAM_RED ? "red" : "blue",
			their_carrier ? "taken" : "home or dropped",
			our_carrier ? "carried by us" : "home or dropped");
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *e = sg_bots[i].ent;

		if (!sg_bots[i].active || !e || !e->client ||
			e->client->ctf.teamnum != team)
			continue;
		sg_team_pass.assigned[i] = -1;
		if (ClientHasFlag(e))
			sg_team_pass.assigned[i] = SG_ROLE_CARRY;
		else
		{
			int forced = SG_OrderedRole(e);

			if (forced >= 0 && forced < SG_ROLES &&
				(forced != SG_ROLE_ESCORT || SG_OrderEscortTarget(e)))
				sg_team_pass.assigned[i] = forced;
		}
		if (sg_team_pass.assigned[i] == SG_ROLE_DEFEND)
			defenders++;
		count++;
	}
	if (count == 0)
		return;
	/* Our flag is out: the two bots nearest to it recover. */
	if (their_carrier && FlagNow(team, flag_now))
	{
		int recoverers = count >= 4 ? 2 : 1;

		while (recoverers-- > 0)
		{
			int slot = NearestUnassigned(team, flag_now, SG_ROLE_RECOVER);

			if (slot < 0)
				break;
			sg_team_pass.assigned[slot] = SG_ROLE_RECOVER;
		}
	}
	/* Someone stays home when nobody was told to and there are enough of
	 * us; with three or more, one defender; with five or more, two. */
	if (have_home && defenders == 0 && count >= 3)
	{
		int want = count >= 5 ? 2 : 1;

		while (want-- > 0)
		{
			int slot = NearestUnassigned(team, home, SG_ROLE_DEFEND);

			if (slot < 0)
				break;
			sg_team_pass.assigned[slot] = SG_ROLE_DEFEND;
		}
	}
	/* A powerup known to be standing: the nearest free bot that can use it
	 * goes, and the team hears it. */
	if (sg_team_pass.powerup[side])
	{
		int best = -1;
		float best_distance = INFINITY;

		for (i = 0; i < SG_MAXBOTS; i++)
		{
			edict_t *e = sg_bots[i].ent;
			float distance;

			if (!sg_bots[i].active || !e || !e->client || e->client->ctf.teamnum != team ||
				sg_team_pass.assigned[i] >= 0 || !PowerupWanted(e, sg_team_pass.powerup[side]))
				continue;
			distance = DistanceTo(e, sg_team_pass.powerup[side]->s.origin);
			if (distance < best_distance)
			{
				best_distance = distance;
				best = i;
			}
		}
		if (best >= 0)
			sg_team_pass.assigned[best] = SG_ROLE_POWERUP;
	}
	/* Our carrier gets every free bot but one as escort: the run home is
	 * the team's whole point while it lasts, and one stays at the enemy's
	 * stand for the flag's return. */
	if (our_carrier)
	{
		int free = 0, escorts;

		for (i = 0; i < SG_MAXBOTS; i++)
			if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->client &&
				sg_bots[i].ent->client->ctf.teamnum == team &&
				sg_team_pass.assigned[i] < 0)
				free++;
		escorts = free - 1 > 0 ? free - 1 : (free > 0 ? 1 : 0);
		while (escorts-- > 0)
		{
			int slot = NearestUnassigned(team, our_carrier->s.origin, SG_ROLE_ESCORT);

			if (slot < 0)
				break;
			sg_team_pass.assigned[slot] = SG_ROLE_ESCORT;
		}
	}
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->client &&
			sg_bots[i].ent->client->ctf.teamnum == team &&
			sg_team_pass.assigned[i] < 0)
			sg_team_pass.assigned[i] = SG_ROLE_ATTACK;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->client &&
			sg_bots[i].ent->client->ctf.teamnum == team)
			sg_team_pass.previous[i] = sg_team_pass.assigned[i];
}

/* Which of its team's defenders this bot is, in roster order: the first
 * takes the best post, the second the next. */
static int DefendRank(const sg_bot_t *bot)
{
	int slot = (int)(bot - sg_bots), i, rank = 0;

	for (i = 0; i < slot && i < SG_MAXBOTS; i++)
	{
		const sg_bot_t *other = &sg_bots[i];

		if (other->ent && other->ent->inuse && other->ent->client &&
			sg_team_pass.assigned[i] == SG_ROLE_DEFEND &&
			other->ent->client->ctf.teamnum == bot->ent->client->ctf.teamnum)
			rank++;
	}
	return rank;
}

static qboolean DestinationFor(sg_bot_t *bot, int role, vec3_t out)
{
	edict_t *e = bot->ent;
	int team = e->client->ctf.teamnum;
	int enemy = SG_EnemyTeam(team);
	edict_t *target;

	switch (role)
	{
	case SG_ROLE_CARRY:
		return FlagHome(team, out);
	case SG_ROLE_RECOVER:
		return FlagNowOrHome(team, out);
	case SG_ROLE_ESCORT:
		target = SG_OrderEscortTarget(e);
		if (!target)
			target = TeamCarrier(team);
		if (target)
		{
			/* Ahead of a moving carrier, to clear what it is running into;
			 * on it when it stands. */
			vec3_t ahead;
			float speed = VectorLength(target->velocity);

			VectorCopy(target->s.origin, out);
			if (speed <= 50.0f)
			{
				/* A standing carrier is kept company from a body length
				 * off, on the side this body is already on: never on top
				 * of it, never in its run-up. */
				vec3_t off, point;
				float flat;

				VectorSubtract(e->s.origin, target->s.origin, off);
				off[2] = 0.0f;
				flat = VectorLength(off);
				if (flat > 1.0f)
				{
					VectorScale(off, ESCORT_STANDOFF / flat, off);
					VectorAdd(target->s.origin, off, point);
					if (StandingCellNear(point) != SG_RUNE_CX_INDEX_NONE)
						VectorCopy(point, out);
				}
				return true;
			}
			if (speed > 50.0f)
			{
				vec3_t point;
				uint32_t ahead_cell;

				VectorScale(target->velocity, ESCORT_AHEAD / speed, ahead);
				ahead[2] = 0.0f;
				VectorAdd(out, ahead, point);
				/* Only where there is floor to stand on, and only where
				 * this body can get to from where it is. */
				ahead_cell = StandingCellNear(point);
				if (ahead_cell != SG_RUNE_CX_INDEX_NONE && bot->cell != SG_RUNE_CX_INDEX_NONE)
				{
					const sg_rune_field_t *to_ahead = SG_RuneLevelField(ahead_cell);

					if (to_ahead && to_ahead->cost[SG_RUNE_FIELD_STATE(bot->cell, 0)] < INFINITY)
					{
						VectorCopy(point, out);
						return true;
					}
				}
				/* Nowhere ahead to be: behind the carrier then, off its line,
				 * never on it. */
				VectorScale(ahead, -0.75f, ahead);
				VectorAdd(target->s.origin, ahead, point);
				if (StandingCellNear(point) != SG_RUNE_CX_INDEX_NONE)
					VectorCopy(point, out);
			}
			return true;
		}
		return FlagNowOrHome(enemy, out);
	case SG_ROLE_DEFEND:
		target = TeamCarrier(enemy);
		bot->post_facing_valid = false;
		bot->post_cell = SG_RUNE_CX_INDEX_NONE;
		if (target)
		{
			VectorCopy(target->s.origin, out);
			return true;
		}
		/* The flag at home: stand where the rune says the approaches are
		 * covered, the second defender where the first leaves gaps. */
		if (FlagHome(team, out))
		{
			uint32_t flag_cell = StandingCellNear(out);
			vec3_t post, facing;
			int posted = 0;

			if (flag_cell != SG_RUNE_CX_INDEX_NONE &&
				SG_RuneLevelDefendPost(flag_cell, DefendRank(bot), post, facing,
					&bot->post_cell))
			{
				VectorCopy(post, out);
				VectorCopy(facing, bot->post_facing);
				bot->post_facing_valid = true;
				posted = 1;
			}
			/* Posted and nothing in sight: stock up on what stands within a
			 * short walk of the flag, then back to the post.  A well armed
			 * attacker meets a defender with a blaster otherwise. */
			if (posted && !bot->engaged_last)
			{
				if (bot->patrolling && level.time < bot->patrol_until)
				{
					VectorCopy(bot->patrol_point, out);
					bot->post_cell = SG_RUNE_CX_INDEX_NONE;
					bot->post_facing_valid = false;
					return true;
				}
				bot->patrolling = 0U;
				if (bot->step.kind == SG_RUNE_STEP_ARRIVED &&
					SG_BotItemNear(bot, out, DEFEND_PATROL_RADIUS, bot->patrol_point))
				{
					bot->patrolling = 1U;
					bot->patrol_until = level.time + DEFEND_PATROL_SECONDS;
					VectorCopy(bot->patrol_point, out);
					bot->post_cell = SG_RUNE_CX_INDEX_NONE;
					bot->post_facing_valid = false;
					return true;
				}
			}
			else
				bot->patrolling = 0U;
			if (sg_cv.debug && sg_cv.debug->value && level.framenum % 100 == 0)
				gi.dprintf("SGPOST %s defends flag at (%.0f %.0f %.0f) cell %u rank %d: %s\n",
					e->client->pers.netname, out[0], out[1], out[2],
					(unsigned int)flag_cell, DefendRank(bot), posted ? "posted" : "at the flag");
			return true;
		}
		return false;
	case SG_ROLE_POWERUP:
	{
		edict_t *item = sg_team_pass.powerup[SG_TeamIdx(team) ? 1 : 0];

		if (item && PowerupItem(item))
		{
			VectorCopy(item->s.origin, out);
			return true;
		}
		return FlagNowOrHome(enemy, out);
	}
	default:
		/* Attacking while a teammate carries the enemy flag: the enemy's
		 * stand, to be there when the flag returns, not on the carrier. */
		if (TeamCarrier(team))
			return FlagHome(enemy, out);
		return FlagNowOrHome(enemy, out);
	}
}

/* ---- the frame ------------------------------------------------------------ */

static short Move(float value)
{
	if (!isfinite(value))
		return 0;
	if (value > 400.0f)
		value = 400.0f;
	else if (value < -400.0f)
		value = -400.0f;
	return (short)lrintf(value);
}

static sg_tactic_hook_phase_t HookPhase(const sg_bot_t *bot, const edict_t *e)
{
	if (e->client->hookstate == 1 && e->client->hook)
		return SG_TACTIC_HOOK_IN_FLIGHT;
	if (e->client->hookstate == 2 && e->client->hook)
		return SG_TACTIC_HOOK_ATTACHED;
	if (bot->hook_phase == 3 && e->client->hookstate == 0)
		return SG_TACTIC_HOOK_COAST;
	return SG_TACTIC_HOOK_IDLE;
}

static void ThinkDead(sg_bot_t *bot, edict_t *e)
{
	usercmd_t cmd;

	if (!bot->death_taught)
	{
		bot->death_taught = true;
		bot->lives++;
	}
	bot->cell = SG_RUNE_CX_INDEX_NONE;
	bot->destination_cell = SG_RUNE_CX_INDEX_NONE;
	bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	bot->step.kind = SG_RUNE_STEP_HOLD;
	bot->hook_phase = 0;
	bot->hook_entity = NULL;
	SG_BotCombatResetClient(e);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;
	/* Respawn consumes a fresh latched press, so pulse attack at 5 Hz. */
	cmd.buttons = (((int)(level.time * 10.0f)) & 2) ? BUTTON_ATTACK : 0;
	ClientThink(e, &cmd);
}

/* The cell a body would stand in at or near a point: flags, items, and
 * players sit at various heights over their floor, so try the standing
 * origin above the point first, then the point, then higher and lower. */
static uint32_t StandingCellNear(const vec3_t point)
{
	static const float rises[] = { 24.0f, 0.0f, 48.0f, -24.0f, 72.0f, 12.0f };
	uint32_t index, first = SG_RUNE_CX_INDEX_NONE;

	/* The first floor cell any probe lands in; failing that, the first
	 * cell at all (a trigger volume over a floor is not a place to stand). */
	for (index = 0U; index < sizeof(rises) / sizeof(rises[0]); index++)
	{
		vec3_t probe;
		uint32_t cell;

		VectorCopy(point, probe);
		probe[2] += rises[index];
		cell = SG_RuneLevelLocate(probe, 0, NULL);
		if (cell == SG_RUNE_CX_INDEX_NONE ||
			(sg_rune_level.artifact.complex.cells[cell].semantics &
				SG_RUNE_CX_CELL_HAZARD))
			continue;   /* lava or slime is no place to go */
		if (sg_rune_level.artifact.complex.cells[cell].semantics &
			SG_RUNE_CX_CELL_SUPPORTED)
			return cell;
		if (first == SG_RUNE_CX_INDEX_NONE)
			first = cell;
	}
	/* Nothing to stand on at the point: the floor under it, as an item
	 * dropped there would find it. */
	{
		vec3_t down;
		trace_t tr;

		VectorCopy(point, down);
		down[2] -= 1024.0f;
		tr = gi.trace(point, NULL, NULL, down, NULL, MASK_SOLID);
		if (tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f)
		{
			vec3_t on;
			uint32_t cell;

			VectorCopy(tr.endpos, on);
			on[2] += 24.5f;
			cell = SG_RuneLevelLocate(on, 0, NULL);
			if (cell != SG_RUNE_CX_INDEX_NONE &&
				(sg_rune_level.artifact.complex.cells[cell].semantics & SG_RUNE_CX_CELL_SUPPORTED))
				return cell;
		}
	}
	return first;
}

/* A flag lying where no body can stand (in lava, off the world) is not a
 * destination: whoever wants it goes to its home and waits for its return. */
static qboolean FlagNowOrHome(int team, vec3_t out)
{
	if (FlagNow(team, out) && StandingCellNear(out) != SG_RUNE_CX_INDEX_NONE)
		return true;
	return FlagHome(team, out);
}

/* A crossing that failed on this body is avoided for a while. */
#define AVOID_SECONDS 60.0f
#define LAUNCH_COMMIT 48.0f       /* a launch is kept while the body is this near its run-up */
#define RUN_UP_MIN 48.0f          /* a launch's run-up sits at least this far behind its portal */
#define RELEASE_LIVE_TOLERANCE 0.0f  /* a live release arc is checked as it is: the record carried the margins */
#define RIDE_OVERSHOOT 32.0f          /* past the bite by this much: the ride has failed, let go */
#define RIDE_STALL_SECONDS 1.5f
#define RIDE_STALL_DISTANCE 24.0f

static void Avoid(sg_bot_t *bot, uint32_t capability)
{
	int i, oldest = 0;

	if (capability == SG_RUNE_CX_INDEX_NONE)
		return;
	for (i = 0; i < SG_BOT_AVOID; i++)
	{
		if (bot->avoid[i] == capability)
		{
			bot->avoid_until[i] = level.time + AVOID_SECONDS;
			return;
		}
		if (bot->avoid_until[i] < bot->avoid_until[oldest])
			oldest = i;
	}
	bot->avoid[oldest] = capability;
	bot->avoid_until[oldest] = level.time + AVOID_SECONDS;
}

static uint32_t Avoided(const sg_bot_t *bot, uint32_t list[SG_BOT_AVOID])
{
	uint32_t count = 0U;
	int i;

	for (i = 0; i < SG_BOT_AVOID; i++)
		if (bot->avoid_until[i] > level.time)
			list[count++] = bot->avoid[i];
	return count;
}

/* Where the body's live flight ends, by the exact tracer from its cell
 * with the velocity it has now.  Safe when it lands on a floor that is
 * neither lava nor slime. */
static int LiveFlight(const sg_bot_t *bot, const edict_t *e, const vec3_t velocity,
	sg_rune_flight_t *flight)
{
	if (bot->cell == SG_RUNE_CX_INDEX_NONE)
		return 0;
	return SG_RuneFlightTrace(&sg_rune_level.artifact.complex,
		&sg_rune_level.artifact.law, bot->cell, e->s.origin, velocity, flight);
}

static qboolean FlightSafe(const sg_rune_flight_t *flight)
{
	const sg_rune_cx_view_t *cx = &sg_rune_level.artifact.complex;

	if (flight->outcome == SG_RUNE_FLIGHT_WATER)
		return true;
	if (flight->outcome != SG_RUNE_FLIGHT_LANDED ||
		flight->landing_cell >= cx->cell_count)
		return false;
	return (cx->cells[flight->landing_cell].semantics & SG_RUNE_CX_CELL_SUPPORTED) &&
		!(cx->cells[flight->landing_cell].semantics & SG_RUNE_CX_CELL_HAZARD);
}

/* Whether letting go where the body hangs now, with no speed, lands it well. */
#define HANG_PATIENCE 3.0f

static qboolean HangDropSafe(const sg_bot_t *bot, const edict_t *e)
{
	static const vec3_t still = { 0.0f, 0.0f, 0.0f };
	sg_rune_flight_t drop;

	if (bot->cell == SG_RUNE_CX_INDEX_NONE)
		return true;
	return SG_RuneFlightLandsRobustly(&sg_rune_level.artifact.complex,
		&sg_rune_level.artifact.law, bot->cell, e->s.origin, still, 0.0f, 0, &drop) ?
		true : false;
}

/* A body falling into harm fires its rope at the best bite it knows: of
 * the hook rides recorded from the floor it left, the anchor most nearly
 * ahead and above.  The ride's own records are not the point; the bite
 * is, and the hanging drop from every recorded bite was checked safe. */
#define RESCUE_ABOVE 32.0f
#define RESCUE_RANGE 1000.0f

static qboolean RescueAnchor(const sg_bot_t *bot, const edict_t *e, vec3_t anchor_out)
{
	const sg_rune_router_t *router = &sg_rune_level.router;
	const sg_rune_move_table_t *move = &sg_rune_level.artifact.movement;
	uint32_t from = bot->flight_from, slot;
	float best = -2.0f;
	vec3_t ahead;
	qboolean found = false;

	if (from == SG_RUNE_CX_INDEX_NONE || from >= sg_rune_level.artifact.complex.cell_count)
		return false;
	/* A fall straight down has no way ahead: any high bite will do. */
	VectorCopy(e->velocity, ahead);
	ahead[2] = 0.0f;
	if (VectorNormalize(ahead) < 1.0f)
		VectorClear(ahead);
	for (slot = router->departure_first[from]; slot < router->departure_first[from + 1U];
		slot++)
	{
		const sg_rune_move_capability_t *record =
			&move->capabilities[router->departures[slot]];
		vec3_t to;
		float flat, score;

		if (record->kind != SG_RUNE_MOVE_HOOK)
			continue;
		if (VectorCompare(record->anchor, bot->rescue_failed))
			continue;
		VectorSubtract(record->anchor, e->s.origin, to);
		if (to[2] < RESCUE_ABOVE || VectorLength(to) > RESCUE_RANGE)
			continue;
		flat = sqrtf(to[0] * to[0] + to[1] * to[1]);
		/* The rope holds the body a hold's length short of the bite; from
		 * there it drops.  A bite whose hang drops into harm again is no
		 * rescue: the body would hang, let go, and fall as before. */
		{
			vec3_t hang, still = { 0.0f, 0.0f, 0.0f };
			float length = VectorLength(to);
			uint32_t hang_cell;
			sg_rune_flight_t drop;

			VectorMA(record->anchor, -SG_FACT_HOOK_HOLD / length, to, hang);
			hang[2] -= (float)e->viewheight;
			hang_cell = SG_RuneLevelLocate(hang, 0, NULL);
			if (hang_cell == SG_RUNE_CX_INDEX_NONE ||
				!SG_RuneFlightTrace(&sg_rune_level.artifact.complex,
					&sg_rune_level.artifact.law, hang_cell, hang, still, &drop) ||
				!FlightSafe(&drop))
				continue;
		}
		/* Ahead of the fall and high: the pull lifts the body out. */
		score = flat > 1.0f ? (to[0] * ahead[0] + to[1] * ahead[1]) / flat : 1.0f;
		score += to[2] / VectorLength(to);
		if (score > best)
		{
			best = score;
			VectorCopy(record->anchor, anchor_out);
			found = true;
		}
	}
	return found;
}

#define RESCUE_TRIES 3            /* ropes a single fall may fire */

/* Whether a body hanging on a rescue rope could catch itself again on
 * another bite: the bite it hangs from is not offered twice. */
static qboolean AnotherRescue(sg_bot_t *bot, const edict_t *e)
{
	vec3_t probe;

	VectorCopy(bot->rescue_anchor, bot->rescue_failed);
	return RescueAnchor(bot, e, probe);
}

/* Any recorded bite above the body within rope range, the highest of the
 * nearest few cells' rides: for a body with no floor it left to remember. */
static qboolean NearbyAnchor(const sg_bot_t *bot, const edict_t *e, vec3_t anchor_out)
{
	const sg_rune_router_t *router = &sg_rune_level.router;
	const sg_rune_move_table_t *move = &sg_rune_level.artifact.movement;
	uint32_t cell, count = sg_rune_level.artifact.complex.cell_count;
	float best = -INFINITY;
	qboolean found = false;

	(void)bot;
	for (cell = 0U; cell < count; cell++)
	{
		const float *centre = &router->cell_center[cell * 3U];
		uint32_t slot;

		if (fabsf(centre[0] - e->s.origin[0]) > 256.0f ||
			fabsf(centre[1] - e->s.origin[1]) > 256.0f)
			continue;
		for (slot = router->departure_first[cell]; slot < router->departure_first[cell + 1U];
			slot++)
		{
			const sg_rune_move_capability_t *record =
				&move->capabilities[router->departures[slot]];
			vec3_t to;
			float score;

			if (record->kind != SG_RUNE_MOVE_HOOK)
				continue;
			VectorSubtract(record->anchor, e->s.origin, to);
			if (to[2] < RESCUE_ABOVE || VectorLength(to) > RESCUE_RANGE)
				continue;
			score = to[2] - 0.5f * sqrtf(to[0] * to[0] + to[1] * to[1]);
			if (score > best)
			{
				best = score;
				VectorCopy(record->anchor, anchor_out);
				found = true;
			}
		}
	}
	return found;
}

/* The step for this frame: on a floor, the field's step; in the air, the
 * flight the body is on (or a fresh trace of where it is falling), steered
 * toward its landing. */
static void SelectStep(sg_bot_t *bot, edict_t *e, const vec3_t destination)
{
	const sg_rune_field_t *field;
	float violation;
	int crouching = (e->client->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	qboolean supported = e->groundentity != NULL;
	qboolean swimming = e->waterlevel >= 2;

	sg_rune_step_t held = bot->step;

	memset(&bot->step, 0, sizeof(bot->step));
	bot->step.kind = SG_RUNE_STEP_HOLD;
	bot->crouching = (uint8_t)crouching;
	bot->cell = SG_RuneLevelLocate(e->s.origin, crouching, &violation);
	if (bot->cell == SG_RUNE_CX_INDEX_NONE)
		return;
	/* The rope log: when it bites and when it comes back, in the terms the
	 * owner's own trace uses. */
	if (sg_cv.debug && sg_cv.debug->value && e->client->hookstate != bot->rope_state_logged)
	{
		if (e->client->hookstate == 2)
		{
			bot->rope_bit_at = level.time;
			gi.dprintf("SGROPE %s bit after=%.1f speed=%.0f\n", e->client->pers.netname,
				bot->rope_fired_at > 0.0f ? level.time - bot->rope_fired_at : 0.0f,
				VectorLength(e->velocity));
		}
		else if (e->client->hookstate == 0 && bot->rope_state_logged != 0)
			gi.dprintf("SGROPE %s off %s attached=%.1f speed=%.0f ground=%d at=(%.0f %.0f %.0f)\n",
				e->client->pers.netname, bot->rope_state_logged == 2 ? "released" : "missed",
				bot->rope_bit_at > 0.0f ? level.time - bot->rope_bit_at : 0.0f,
				VectorLength(e->velocity), e->groundentity != NULL,
				e->s.origin[0], e->s.origin[1], e->s.origin[2]);
		bot->rope_state_logged = (uint8_t)e->client->hookstate;
	}
	/* A rope that came back without ever carrying the body missed, or bit
	 * something that shook it off: the ride it was fired for is not tried
	 * again for a while, or the body fires at the same bite from the same
	 * spot until the flag returns. */
	if (e->client->hookstate == 0 && bot->hook_phase == 2 && !bot->fired_bit &&
		bot->fired_capability != SG_RUNE_CX_INDEX_NONE)
	{
		Avoid(bot, bot->fired_capability);
		if (sg_cv.debug && sg_cv.debug->value)
			gi.dprintf("SGBOT %s rope missed: ride avoided\n", e->client->pers.netname);
		bot->fired_capability = SG_RUNE_CX_INDEX_NONE;
		bot->hook_phase = 0;
		bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	}
	/* A launch being lined up is kept while the body is near its run-up
	 * point: the cells at a floor's edge are small, and a body settling on
	 * the point drifts across them, where the field would send it off
	 * again and it would come back and drift again. */
	if (held.kind == SG_RUNE_STEP_CROSS && held.run_up_present && supported &&
		e->client->hookstate == 0 &&
		(held.move_kind == SG_RUNE_MOVE_HOOK || held.move_kind == SG_RUNE_MOVE_JUMP ||
		 held.move_kind == SG_RUNE_MOVE_DROP || held.move_kind == SG_RUNE_MOVE_ROCKET_JUMP) &&
		held.capability != SG_RUNE_CX_INDEX_NONE)
	{
		float dx = held.run_up[0] - e->s.origin[0], dy = held.run_up[1] - e->s.origin[1];
		uint32_t avoid[SG_BOT_AVOID];
		uint32_t avoid_count = Avoided(bot, avoid), i;
		qboolean avoided = false;

		for (i = 0U; i < avoid_count; i++)
			if (avoid[i] == held.capability)
				avoided = true;
		/* Past the portal along the launch line, the launch is done with or
		 * missed: the field decides afresh from wherever the body is. */
		if (held.launch_present)
		{
			float lx = held.launch[0], ly = held.launch[1];
			float length = sqrtf(lx * lx + ly * ly);

			if (length > 1.0f &&
				((e->s.origin[0] - held.target[0]) * lx +
				 (e->s.origin[1] - held.target[1]) * ly) / length > 0.0f)
				avoided = true;
		}
		if (!avoided && dx * dx + dy * dy < (LAUNCH_COMMIT + RUN_UP_MIN) * (LAUNCH_COMMIT + RUN_UP_MIN))
		{
			bot->step = held;
			bot->step.crouching_now = (uint8_t)crouching;
			bot->airborne = 0U;
			bot->ride_since = 0.0f;
			return;
		}
	}
	/* The destination's cell, resolved again when the point moves. */
	if (bot->destination_cell == SG_RUNE_CX_INDEX_NONE ||
		VectorLength(bot->destination) == 0.0f ||
		fabsf(destination[0] - bot->destination[0]) > 24.0f ||
		fabsf(destination[1] - bot->destination[1]) > 24.0f ||
		fabsf(destination[2] - bot->destination[2]) > 48.0f)
	{
		uint32_t resolved = bot->role == SG_ROLE_DEFEND &&
			bot->post_cell != SG_RUNE_CX_INDEX_NONE ? bot->post_cell :
			StandingCellNear(destination);

		/* A point with no floor near it (a carrier mid-air, a flag in the
		 * lava) keeps the last cell the route was going to. */
		if (resolved != SG_RUNE_CX_INDEX_NONE ||
			bot->destination_cell == SG_RUNE_CX_INDEX_NONE)
		{
			VectorCopy(destination, bot->destination);
			bot->destination_cell = resolved;
		}
	}
	if (bot->destination_cell == SG_RUNE_CX_INDEX_NONE)
	{
		if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
			gi.dprintf("SGBOT %s destination (%.0f %.0f %.0f) has no cell\n",
				e->client->pers.netname, destination[0], destination[1],
				destination[2]);
		return;
	}
	/* A rope out or attached: the hook crossing goes on, whatever is under
	 * the body, until the rope is let go; then the body is on a flight to
	 * the record's landing. */
	if (e->client->hookstate != 0 && bot->rescue)
	{
		/* A rescue rope: ride it to the bite and hang; the release is
		 * judged by the live flight like any ride.  A rope that pulls the
		 * body nowhere is let go of, and that bite is not tried again. */
		if (e->client->hookstate == 2)
		{
			vec3_t moved, to_bite;

			if (bot->ride_since <= 0.0f)
			{
				bot->ride_since = level.time;
				bot->hang_since = 0.0f;
				VectorCopy(e->s.origin, bot->ride_origin);
			}
			VectorSubtract(bot->rescue_anchor, e->s.origin, to_bite);
			to_bite[2] -= (float)e->viewheight;
			if (VectorLength(to_bite) <= SG_FACT_HOOK_HOLD + 8.0f)
			{
				if (bot->hang_since <= 0.0f)
					bot->hang_since = level.time;
			}
			else
				bot->hang_since = 0.0f;
			VectorSubtract(e->s.origin, bot->ride_origin, moved);
			if (VectorLength(moved) >= RIDE_STALL_DISTANCE && bot->hang_since <= 0.0f)
			{
				bot->ride_since = level.time;
				VectorCopy(e->s.origin, bot->ride_origin);
			}
			else if ((level.time - bot->ride_since > RIDE_STALL_SECONDS ||
				(bot->hang_since > 0.0f && level.time - bot->hang_since > RIDE_STALL_SECONDS)) &&
				(HangDropSafe(bot, e) ||
				 ((bot->hang_since <= 0.0f ||
				   level.time - bot->hang_since > HANG_PATIENCE) &&
				  bot->rescue_spent < RESCUE_TRIES &&
				  AnotherRescue(bot, e))))
			{
				ctf_hook_abort(e);
				bot->hook_phase = 3;
				bot->hook_entity = NULL;
				VectorCopy(bot->rescue_anchor, bot->rescue_failed);
				bot->rescue = 0U;
				bot->ride_since = 0.0f;
				if (sg_cv.debug && sg_cv.debug->value)
					gi.dprintf("SGBOT %s rescue rope let go\n", e->client->pers.netname);
				goto grounded;
			}
		}
		bot->airborne = (uint8_t)!supported;
		bot->step.kind = SG_RUNE_STEP_CROSS;
		bot->step.cell = bot->cell;
		bot->step.portal = SG_RUNE_CX_INDEX_NONE;
		bot->step.capability = SG_RUNE_CX_INDEX_NONE;
		bot->step.move_kind = SG_RUNE_MOVE_HOOK;
		bot->step.crouching_now = (uint8_t)crouching;
		VectorCopy(bot->rescue_anchor, bot->step.target);
		VectorCopy(bot->rescue_anchor, bot->step.hook_point);
		bot->step.hook_point_present = 1U;
		bot->step.hook_release_distance = 0.0f;
		return;
	}
	if (e->client->hookstate != 0 && bot->flight_capability != SG_RUNE_CX_INDEX_NONE)
	{
		const sg_rune_move_capability_t *record =
			&sg_rune_level.artifact.movement.capabilities[bot->flight_capability];

		if (record->kind == SG_RUNE_MOVE_HOOK)
		{
			/* A rope that pulls the body nowhere is let go, and that ride
			 * is not tried again for a while. */
			if (e->client->hookstate == 2)
			{
				vec3_t moved;

				if (bot->ride_since <= 0.0f)
				{
					bot->ride_since = level.time;
					bot->ride_nearest = 0.0f;
					bot->hang_since = 0.0f;
					VectorCopy(e->s.origin, bot->ride_origin);
				}
				VectorSubtract(e->s.origin, bot->ride_origin, moved);
				if (VectorLength(moved) >= RIDE_STALL_DISTANCE)
					bot->fired_bit = 1U;   /* the rope carried the body: a ride */
				{
					/* Past the bite and still on the rope: the body sailed by
					 * where it should have let go.  Let go now, wherever it
					 * lands, rather than swing back into the wall. */
					vec3_t to_bite;
					float distance;

					VectorSubtract(record->anchor, e->s.origin, to_bite);
					distance = VectorLength(to_bite);
					if (bot->ride_nearest <= 0.0f || distance < bot->ride_nearest)
						bot->ride_nearest = distance;
					else if (distance > bot->ride_nearest + RIDE_OVERSHOOT)
					{
						ctf_hook_abort(e);
						bot->hook_phase = 3;
						bot->hook_entity = NULL;
						Avoid(bot, bot->flight_capability);
						bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
						bot->ride_since = 0.0f;
						bot->ride_nearest = 0.0f;
						if (sg_cv.debug && sg_cv.debug->value)
							gi.dprintf("SGBOT %s rope let go past the bite\n",
								e->client->pers.netname);
						goto grounded;
					}
				}
				{
					/* Held at the bite: the clock of the hang runs from the
					 * first such frame, however the body sways there. */
					vec3_t to_bite;

					VectorSubtract(record->anchor, e->s.origin, to_bite);
					to_bite[2] -= (float)e->viewheight;
					if (VectorLength(to_bite) <= SG_FACT_HOOK_HOLD + 8.0f)
					{
						if (bot->hang_since <= 0.0f)
							bot->hang_since = level.time;
					}
					else
						bot->hang_since = 0.0f;
				}
				if (VectorLength(moved) >= RIDE_STALL_DISTANCE && bot->hang_since <= 0.0f)
				{
					bot->ride_since = level.time;
					VectorCopy(e->s.origin, bot->ride_origin);
				}
				else if (level.time - bot->ride_since > RIDE_STALL_SECONDS ||
					(bot->hang_since > 0.0f && level.time - bot->hang_since > RIDE_STALL_SECONDS))
				{
					vec3_t to_bite;
					qboolean hanging;

					/* At the bite the rope holds the body still: that is the
					 * ride's end, and the hanging drop was checked at
					 * generation.  Stalled anywhere else, the ride failed. */
					VectorSubtract(record->anchor, e->s.origin, to_bite);
					to_bite[2] -= (float)e->viewheight;
					hanging = VectorLength(to_bite) <= SG_FACT_HOOK_HOLD + 8.0f;
					/* Hanging over harm: hold on.  Let go only once patience
					 * runs out and another rope can catch the fall. */
					if (hanging && !HangDropSafe(bot, e))
					{
						vec3_t probe;

						if (level.time - bot->hang_since < HANG_PATIENCE ||
							bot->rescue_spent >= RESCUE_TRIES)
							goto ride_on;
						VectorCopy(record->anchor, bot->rescue_failed);
						if (!RescueAnchor(bot, e, probe))
							goto ride_on;
					}
					{
						/* Let go at the bite, the body drops where it hangs.
						 * Unless that drop is the ride's own landing, the ride
						 * did not do what its record said: it is avoided, or
						 * the field sends the body up the same rope again. */
						qboolean reached = false;

						if (hanging)
						{
							static const vec3_t still = { 0.0f, 0.0f, 0.0f };
							sg_rune_flight_t drop;

							if (LiveFlight(bot, e, still, &drop) &&
								drop.outcome == SG_RUNE_FLIGHT_LANDED &&
								drop.landing_cell == record->destination)
								reached = true;
						}
						ctf_hook_abort(e);
						bot->hook_phase = 3;
						bot->hook_entity = NULL;
						if (!reached)
							Avoid(bot, bot->flight_capability);
						bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
						bot->ride_since = 0.0f;
						if (sg_cv.debug && sg_cv.debug->value)
							gi.dprintf("SGBOT %s rope %s\n", e->client->pers.netname,
								hanging ? (reached ? "let go at the bite" :
									"let go at the bite: ride avoided") :
								"stalled: ride avoided");
					}
					goto grounded;
				}
			}
			else
				bot->ride_since = 0.0f;
ride_on:
			bot->airborne = (uint8_t)!supported;
			bot->step.kind = SG_RUNE_STEP_CROSS;
			bot->step.cell = bot->cell;
			bot->step.portal = SG_RUNE_CX_INDEX_NONE;
			bot->step.capability = bot->flight_capability;
			bot->step.move_kind = SG_RUNE_MOVE_HOOK;
			bot->step.crouching_now = (uint8_t)crouching;
			VectorCopy(bot->flight_landing, bot->step.target);
			VectorCopy(record->anchor, bot->step.hook_point);
			bot->step.hook_point_present = 1U;
			bot->step.hook_release_distance = record->parameter;
			return;
		}
	}
grounded:
	bot->ride_since = 0.0f;
	bot->hang_since = 0.0f;
	/* In lava or slime: the rope, at the best bite known from the floor
	 * the body left, or from any ride recorded around here.  Nothing else
	 * gets it out. */
	if ((sg_rune_level.artifact.complex.cells[bot->cell].semantics & SG_RUNE_CX_CELL_HAZARD) &&
		e->client->hookstate == 0 && !bot->rescue && SG_BotHookReady(e))
	{
		if (RescueAnchor(bot, e, bot->rescue_anchor) ||
			NearbyAnchor(bot, e, bot->rescue_anchor))
		{
			bot->rescue = 1U;
			if (sg_cv.debug && sg_cv.debug->value)
				gi.dprintf("SGBOT %s rope out of the lava\n", e->client->pers.netname);
		}
	}
	bot->airborne = (uint8_t)(!supported && !swimming);
	if (bot->airborne)
	{
		sg_rune_flight_t live;
		int traced = LiveFlight(bot, e, e->velocity, &live);

		/* Riding a chosen flight: steer to its landing.  Otherwise trace
		 * where this fall goes and steer there. */
		if (bot->flight_capability == SG_RUNE_CX_INDEX_NONE)
		{
			if (traced && (live.outcome == SG_RUNE_FLIGHT_LANDED ||
				live.outcome == SG_RUNE_FLIGHT_WATER))
				VectorCopy(live.landing, bot->flight_landing);
			else
				VectorCopy(e->s.origin, bot->flight_landing);
		}
		/* Falling into lava or slime, or out of the world: the rope. */
		if (traced && live.outcome == SG_RUNE_FLIGHT_HARM &&
			e->client->hookstate == 0 && !bot->rescue && bot->rescue_spent < RESCUE_TRIES &&
			SG_BotHookReady(e) && RescueAnchor(bot, e, bot->rescue_anchor))
		{
			bot->rescue = 1U;
			bot->rescue_spent++;
			/* The launch that led here is not to be trusted for a while. */
			if (bot->flight_capability != SG_RUNE_CX_INDEX_NONE)
				Avoid(bot, bot->flight_capability);
			if (sg_cv.debug && sg_cv.debug->value)
				gi.dprintf("SGBOT %s rope out to save a fall into harm\n",
					e->client->pers.netname);
		}
		/* A hop or a fall that lands on floor: the route goes on from the
		 * landing cell while the body is still in the air.  A walk there is
		 * steered at now; a ride there is fired now, from the air, the way
		 * a human does, and the pull takes the momentum along. */
		if (bot->flight_capability == SG_RUNE_CX_INDEX_NONE && !bot->rescue &&
			traced && live.outcome == SG_RUNE_FLIGHT_LANDED &&
			live.landing_cell < sg_rune_level.artifact.complex.cell_count &&
			(sg_rune_level.artifact.complex.cells[live.landing_cell].semantics &
				SG_RUNE_CX_CELL_SUPPORTED) &&
			bot->destination_cell != SG_RUNE_CX_INDEX_NONE)
		{
			const sg_rune_field_t *field = SG_RuneLevelField(bot->destination_cell);
			sg_rune_step_t next;
			uint32_t avoid[SG_BOT_AVOID];
			uint32_t avoid_count = Avoided(bot, avoid);

			if (field && SG_RuneStepSelectAvoiding(&sg_rune_level.router, field,
				live.landing_cell, 0, destination, avoid, avoid_count, &next) &&
				next.kind == SG_RUNE_STEP_CROSS)
			{
				if (next.move_kind == SG_RUNE_MOVE_HOOK && e->client->hookstate == 0 &&
					SG_BotHookReady(e))
				{
					bot->step = next;
					bot->step.cell = bot->cell;
					bot->step.crouching_now = (uint8_t)crouching;
					bot->step.run_up_present = 0U;
					bot->flight_capability = next.capability;
					{
						const float *landing = &sg_rune_level.router.cell_center[
							sg_rune_level.router.destination[next.capability] * 3U];

						VectorCopy(landing, bot->flight_landing);
					}
					return;
				}
				if (next.move_kind == SG_RUNE_MOVE_WALK || next.move_kind == SG_RUNE_MOVE_RAMP ||
					next.move_kind == SG_RUNE_MOVE_CROUCH)
					VectorCopy(next.target, bot->flight_landing);
			}
		}
		bot->step.kind = SG_RUNE_STEP_CROSS;
		bot->step.cell = bot->cell;
		bot->step.portal = SG_RUNE_CX_INDEX_NONE;
		bot->step.capability = bot->flight_capability;
		bot->step.move_kind = SG_RUNE_MOVE_AIR_CONTROL;
		bot->step.crouching_now = (uint8_t)crouching;
		VectorCopy(bot->flight_landing, bot->step.target);
		return;
	}
	bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	bot->flight_from = bot->cell;
	bot->rescue = 0U;
	bot->rescue_spent = 0U;
	/* Into or out of the enemy base, the route keeps out of the lines the
	 * enemy's defenders hold where it can. */
	field = NULL;
	if (bot->role == SG_ROLE_ATTACK || bot->role == SG_ROLE_CARRY ||
		bot->role == SG_ROLE_ESCORT)
	{
		vec3_t enemy_home;

		if (FlagHome(SG_EnemyTeam(e->client->ctf.teamnum), enemy_home))
		{
			uint32_t enemy_flag_cell = StandingCellNear(enemy_home);

			if (enemy_flag_cell != SG_RUNE_CX_INDEX_NONE)
				field = SG_RuneLevelFieldExposed(bot->destination_cell, enemy_flag_cell);
		}
	}
	if (!field)
		field = SG_RuneLevelField(bot->destination_cell);
	if (!field)
		return;
	/* An item worth the detour becomes the destination for now; the goal's
	 * own field prices the detour.  A carrier never detours unless it is
	 * about to die, and then only for health. */
	if (bot->role != SG_ROLE_CARRY || e->health < CARRIER_DETOUR_HEALTH)
	{
		vec3_t item_point;

		if (SG_BotItemDetour(bot, field, item_point))
		{
			uint32_t item_cell = StandingCellNear(item_point);
			const sg_rune_field_t *item_field = item_cell != SG_RUNE_CX_INDEX_NONE ?
				SG_RuneLevelField(item_cell) : NULL;

			if (item_field)
			{
				uint32_t avoid[SG_BOT_AVOID];
				uint32_t avoid_count = Avoided(bot, avoid);

				(void)SG_RuneStepSelectAvoiding(&sg_rune_level.router, item_field,
					bot->cell, crouching, item_point, avoid, avoid_count, &bot->step);
				goto flight;
			}
		}
	}
	{
		uint32_t avoid[SG_BOT_AVOID];
		uint32_t avoid_count = Avoided(bot, avoid);

		(void)SG_RuneStepSelectAvoiding(&sg_rune_level.router, field, bot->cell,
			crouching, destination, avoid, avoid_count, &bot->step);
	}
	/* An escort keeps a second behind the carrier rather than on top of
	 * it: close enough to fight what reaches the carrier, out of its way. */
	if (bot->role == SG_ROLE_ESCORT && bot->step.kind == SG_RUNE_STEP_CROSS &&
		bot->step.cost_to_go < ESCORT_GAP_SECONDS)
	{
		bot->step.kind = SG_RUNE_STEP_ARRIVED;
		bot->step.portal = SG_RUNE_CX_INDEX_NONE;
		bot->step.capability = SG_RUNE_CX_INDEX_NONE;
		VectorCopy(e->s.origin, bot->step.target);
	}
flight:
	/* Standing where the complex knows no floor (an entity's top, a brush
	 * model): walk to the nearest floor the field reaches from. */
	if (bot->step.kind == SG_RUNE_STEP_UNREACHABLE && supported)
	{
		vec3_t point;
		const sg_rune_field_t *route = SG_RuneLevelField(bot->destination_cell);

		if (route && SG_RuneFieldNearestReachable(&sg_rune_level.router,
			&sg_rune_level.locator, route, e->s.origin, 640.0f, point) !=
			SG_RUNE_CX_INDEX_NONE)
		{
			memset(&bot->step, 0, sizeof(bot->step));
			bot->step.kind = SG_RUNE_STEP_CROSS;
			bot->step.cell = bot->cell;
			bot->step.portal = SG_RUNE_CX_INDEX_NONE;
			bot->step.capability = SG_RUNE_CX_INDEX_NONE;
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
			bot->step.crouching_now = (uint8_t)crouching;
			VectorCopy(point, bot->step.target);
		}
	}
	if (bot->step.kind == SG_RUNE_STEP_CROSS &&
		(bot->step.move_kind == SG_RUNE_MOVE_JUMP ||
		 bot->step.move_kind == SG_RUNE_MOVE_DROP ||
		 bot->step.move_kind == SG_RUNE_MOVE_ROCKET_JUMP ||
		 bot->step.move_kind == SG_RUNE_MOVE_HOOK ||
		 bot->step.move_kind == SG_RUNE_MOVE_EXTERNAL_FORCE))
	{
		const sg_rune_move_capability_t *record =
			&sg_rune_level.artifact.movement.capabilities[bot->step.capability];

		/* A flight: its landing is where the executor steers once the
		 * body leaves the floor. */
		if (record->seconds > 0.0f)
		{
			const float *landing =
				&sg_rune_level.router.cell_center[record->destination * 3U];

			bot->flight_capability = bot->step.capability;
			VectorCopy(landing, bot->flight_landing);
		}
		/* The record launched at the portal at full run speed; a body needs
		 * a frame's run to be at that speed.  From a cell too small for it,
		 * the run-up moves back along the launch line onto whatever floor
		 * is there. */
		if (bot->step.run_up_present && bot->step.launch_present)
		{
			float lx = bot->step.launch[0], ly = bot->step.launch[1];
			float length = sqrtf(lx * lx + ly * ly);

			if (length > 1.0f)
			{
				float behind = ((bot->step.target[0] - bot->step.run_up[0]) * lx +
					(bot->step.target[1] - bot->step.run_up[1]) * ly) / length;

				if (behind < RUN_UP_MIN)
				{
					vec3_t back;

					back[0] = bot->step.target[0] - lx / length * RUN_UP_MIN;
					back[1] = bot->step.target[1] - ly / length * RUN_UP_MIN;
					back[2] = bot->step.run_up[2];
					if (SG_BotStandingCellNear(back) != SG_RUNE_CX_INDEX_NONE)
					{
						bot->step.run_up[0] = back[0];
						bot->step.run_up[1] = back[1];
					}
				}
			}
		}
	}
}

#define MOVER_STATE_TOP 0
#define MOVER_STATE_UP 2

/* The step goes through a mechanism: what the body does about it now,
 * from the mechanism's live state. */
static void ApplyMechanism(sg_bot_t *bot, edict_t *e)
{
	const sg_rune_move_capability_t *capability;
	const sg_rune_mech_t *record;
	edict_t *mover;
	vec3_t point;

	if (bot->step.kind != SG_RUNE_STEP_CROSS ||
		bot->step.capability == SG_RUNE_CX_INDEX_NONE)
	{
		bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
		return;
	}
	capability = &sg_rune_level.artifact.movement.capabilities[bot->step.capability];
	if (capability->mechanism == SG_RUNE_CX_INDEX_NONE)
	{
		bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
		return;
	}
	record = &sg_rune_level.artifact.mechanisms.records[capability->mechanism];
	mover = SG_RuneLevelMechanismEdict(capability->mechanism);
	switch (record->kind)
	{
	case SG_RUNE_MECH_PUSH:
		/* Walk into the pad; the launch is the map's. */
		VectorCopy(record->origin, point);
		point[2] = record->mins[2] + 24.0f;
		VectorCopy(point, bot->step.target);
		bot->step.move_kind = SG_RUNE_MOVE_WALK;
		break;
	case SG_RUNE_MECH_TELEPORTER:
		VectorCopy(record->origin, bot->step.target);
		bot->step.target[2] += 24.0f;
		bot->step.move_kind = SG_RUNE_MOVE_WALK;
		break;
	case SG_RUNE_MECH_PLATFORM:
		if (!mover)
			break;
		if (e->groundentity == mover)
		{
			/* Riding: wait for the top, then walk off toward the field's
			 * destination floor. */
			if (mover->moveinfo.state != MOVER_STATE_TOP)
			{
				bot->step.kind = SG_RUNE_STEP_HOLD;
				break;
			}
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
			break;
		}
		/* Get on: the middle of its top where it is now. */
		bot->step.target[0] = record->origin[0];
		bot->step.target[1] = record->origin[1];
		bot->step.target[2] = mover->absmax[2] + 24.0f;
		bot->step.move_kind = SG_RUNE_MOVE_WALK;
		break;
	case SG_RUNE_MECH_TRAIN:
		if (!mover)
			break;
		if (e->groundentity == mover)
		{
			vec3_t delta;

			VectorSubtract(bot->step.target, mover->s.origin, delta);
			delta[2] = 0.0f;
			if (VectorLength(delta) > 96.0f)
			{
				bot->step.kind = SG_RUNE_STEP_HOLD;   /* ride */
				break;
			}
			bot->step.move_kind = SG_RUNE_MOVE_WALK;   /* step off */
			break;
		}
		{
			vec3_t delta;

			VectorSubtract(mover->s.origin, e->s.origin, delta);
			delta[2] = 0.0f;
			if (VectorLength(delta) > 160.0f)
			{
				/* Not here yet: wait where the field says to board. */
				const float *center =
					&sg_rune_level.router.cell_center[bot->step.cell * 3U];

				VectorCopy(center, bot->step.target);
				bot->step.target[2] += 24.0f;
				bot->step.kind = SG_RUNE_STEP_ARRIVED;
				break;
			}
			bot->step.target[0] = (mover->absmin[0] + mover->absmax[0]) * 0.5f;
			bot->step.target[1] = (mover->absmin[1] + mover->absmax[1]) * 0.5f;
			bot->step.target[2] = mover->absmax[2] + 24.0f;
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
		}
		break;
	case SG_RUNE_MECH_DOOR:
	{
		int open = !mover || mover->moveinfo.state == MOVER_STATE_TOP ||
			mover->moveinfo.state == MOVER_STATE_UP;

		if (open || record->activation == SG_RUNE_MECH_ACTIVATE_TOUCH)
		{
			bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
			break;
		}
		if (record->activation == SG_RUNE_MECH_ACTIVATE_SHOT)
		{
			SG_BotCombatShootAt(e, record->origin);
			bot->step.kind = SG_RUNE_STEP_HOLD;
			break;
		}
		if (record->activation == SG_RUNE_MECH_ACTIVATE_TARGETED &&
			record->activator != SG_RUNE_CX_INDEX_NONE)
		{
			const sg_rune_mech_t *worker =
				&sg_rune_level.artifact.mechanisms.records[record->activator];

			if (bot->task_mechanism != capability->mechanism)
			{
				bot->task_mechanism = capability->mechanism;
				bot->task_since = level.time;
			}
			if (level.time - bot->task_since > 12.0f)
			{
				/* It did not open for us: try the door itself. */
				bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
				break;
			}
			if (worker->activation == SG_RUNE_MECH_ACTIVATE_SHOT)
			{
				SG_BotCombatShootAt(e, worker->origin);
				bot->step.kind = SG_RUNE_STEP_HOLD;
				break;
			}
			VectorCopy(worker->origin, bot->step.target);
			bot->step.target[2] = worker->mins[2] + 24.0f;
			if (bot->step.target[2] < worker->origin[2] - 32.0f)
				bot->step.target[2] = worker->origin[2];
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
			break;
		}
		bot->step.kind = SG_RUNE_STEP_HOLD;
		break;
	}
	default:
		break;
	}
}

/* Teammates do not walk through each other: a teammate close ahead pushes
 * the direction sideways, away from it, the more the closer. */
#define GIVE_WAY_REACH 56.0f

#define GIVE_WAY_LOOK 40.0f       /* the step aside is checked this far ahead for floor */
#define HOOK_RUN_LOOK 96.0f       /* a body running under its bolt needs this much floor ahead: room to stop */
#define VIEW_FOLLOWS_SPEED 0.3f   /* the view turns with the run only above this command speed */
#define HOOK_BITE_SLACK 12.0f     /* the bolt may stop this short of the bite (the bite is two units off its face) */
#define HOP_SPEED 250.0f          /* a bunny hop needs this much run */
#define HOP_ROUTE_LEFT 1.5f       /* and this much route left, in seconds */
#define HOP_MAX_SECONDS 0.9f      /* and must land within this */
#define HOP_RUN_ON_SECONDS 0.15f  /* and the floor must go on under the run after it */
#define YIELD_REACH 72.0f         /* a standing body this near a teammate carrier steps away */

static void GiveWay(const edict_t *e, float direction[3])
{
	int i;
	float push[2] = { 0.0f, 0.0f };

	for (i = 1; i <= game.maxclients; i++)
	{
		const edict_t *other = &g_edicts[i];
		float dx, dy, flat, ahead, side, weight;

		if (other == e || !other->inuse || !other->client || other->health <= 0 ||
			other->client->ctf.teamnum != e->client->ctf.teamnum)
			continue;
		dx = other->s.origin[0] - e->s.origin[0];
		dy = other->s.origin[1] - e->s.origin[1];
		if (fabsf(other->s.origin[2] - e->s.origin[2]) > 48.0f)
			continue;
		flat = sqrtf(dx * dx + dy * dy);
		if (flat >= GIVE_WAY_REACH || flat < 1.0f)
			continue;
		ahead = (dx * direction[0] + dy * direction[1]) / flat;
		if (ahead < 0.3f)
			continue;   /* beside or behind: no need */
		/* Which side it is on, from our direction; push to the other. */
		side = direction[0] * dy - direction[1] * dx;
		weight = (GIVE_WAY_REACH - flat) / GIVE_WAY_REACH;
		if (side >= 0.0f)
		{
			push[0] += direction[1] * weight;
			push[1] += -direction[0] * weight;
		}
		else
		{
			push[0] += -direction[1] * weight;
			push[1] += direction[0] * weight;
		}
	}
	if (push[0] != 0.0f || push[1] != 0.0f)
	{
		float x = direction[0] + push[0], y = direction[1] + push[1];
		float length = sqrtf(x * x + y * y);

		if (length > 1e-3f)
		{
			/* Only onto floor: a step aside at a ledge is a step off it.
			 * The other side is tried, then the way is kept. */
			float sides[2][2] = { { x / length, y / length },
				{ (direction[0] - push[0]), (direction[1] - push[1]) } };
			int side;

			length = sqrtf(sides[1][0] * sides[1][0] + sides[1][1] * sides[1][1]);
			if (length > 1e-3f)
			{
				sides[1][0] /= length;
				sides[1][1] /= length;
			}
			for (side = 0; side < 2; side++)
			{
				vec3_t ahead;
				uint32_t cell;

				ahead[0] = e->s.origin[0] + sides[side][0] * GIVE_WAY_LOOK;
				ahead[1] = e->s.origin[1] + sides[side][1] * GIVE_WAY_LOOK;
				ahead[2] = e->s.origin[2];
				cell = SG_RuneLevelLocate(ahead, 0, NULL);
				if (cell == SG_RUNE_CX_INDEX_NONE ||
					!(sg_rune_level.artifact.complex.cells[cell].semantics &
						SG_RUNE_CX_CELL_SUPPORTED))
					continue;
				direction[0] = sides[side][0];
				direction[1] = sides[side][1];
				return;
			}
		}
	}
}

static void Emit(sg_bot_t *bot, edict_t *e)
{
	usercmd_t cmd;
	sg_tactic_body_t body;
	sg_tactic_command_t command;
	qboolean engaged = false;
	qboolean moving;

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;
	memset(&body, 0, sizeof(body));
	VectorCopy(e->s.origin, body.origin);
	VectorCopy(e->velocity, body.velocity);
	body.view_height = (float)e->viewheight;
	body.supported = e->groundentity != NULL ? 1U : 0U;
	body.waterlevel = (uint8_t)e->waterlevel;
	body.crouched = (e->client->ps.pmove.pm_flags & PMF_DUCKED) != 0 ? 1U : 0U;
	body.hook_phase = HookPhase(bot, e);
	body.launcher_ready = SG_BotLauncherReady(e) ? 1U : 0U;
	body.hook_ready = SG_BotHookReady(e) ? 1U : 0U;
	if (bot->step.hook_point_present)
	{
		vec3_t way, ahead;
		float flat;

		VectorSubtract(bot->step.hook_point, e->s.origin, way);
		way[2] = 0.0f;
		flat = VectorLength(way);
		if (flat > 1.0f)
		{
			VectorScale(way, HOOK_RUN_LOOK / flat, way);
			VectorAdd(e->s.origin, way, ahead);
			/* Floor there means a supported cell that is no hazard at the
			 * body's own height: the loose probe that finds a stand for a
			 * flag would call the far side of a lava pit floor. */
			{
				uint32_t cell = SG_RuneLevelLocate(ahead, 0, NULL);

				body.floor_toward_hook = cell != SG_RUNE_CX_INDEX_NONE &&
					(sg_rune_level.artifact.complex.cells[cell].semantics &
						(SG_RUNE_CX_CELL_SUPPORTED | SG_RUNE_CX_CELL_HAZARD)) ==
						SG_RUNE_CX_CELL_SUPPORTED ? 1U : 0U;
			}
		}
	}
	body.gravity = sv_gravity->value;
	body.frame_ms = sg_rune_level.artifact.law.frame_ms;
	body.substep_ms = sg_rune_level.artifact.law.substep_ms;
	body.law = &sg_rune_level.artifact.law;
	if (!SG_TacticControl(&bot->step, &body, &command))
	{
		memset(&command, 0, sizeof(command));
		command.status = SG_TACTIC_COMMAND_HOLD;
	}
	moving = command.status != SG_TACTIC_COMMAND_HOLD && command.speed > 0.0f &&
		isfinite(command.direction[0]) && isfinite(command.direction[1]);
	/* A body standing in a teammate carrier's way steps out of it: the
	 * carrier's run home is the team's, and bodies block each other. */
	if (!moving && !BotCarrying(e))
	{
		edict_t *carrier = TeamCarrier(e->client->ctf.teamnum);

		if (carrier && carrier != e)
		{
			vec3_t away;
			float flat;

			VectorSubtract(e->s.origin, carrier->s.origin, away);
			away[2] = 0.0f;
			flat = VectorLength(away);
			if (flat < YIELD_REACH && flat > 1.0f)
			{
				vec3_t ahead;

				VectorScale(away, 1.0f / flat, away);
				ahead[0] = e->s.origin[0] + away[0] * GIVE_WAY_LOOK;
				ahead[1] = e->s.origin[1] + away[1] * GIVE_WAY_LOOK;
				ahead[2] = e->s.origin[2];
				if (SG_BotStandingCellNear(ahead) != SG_RUNE_CX_INDEX_NONE)
				{
					VectorCopy(away, command.direction);
					command.speed = 0.6f;
					command.status = SG_TACTIC_COMMAND_MOVE;
					moving = true;
				}
			}
		}
	}
	if (moving)
		GiveWay(e, command.direction);
	/* The view follows the way the body goes only at a real pace: a nudge
	 * of a unit toward a point is no reason to turn. */
	if (moving && command.speed >= VIEW_FOLLOWS_SPEED)
	{
		float yaw = atan2f(command.direction[1], command.direction[0]) *
			180.0f / (float)M_PI;

		cmd.angles[YAW] = (short)(ANGLE2SHORT(yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
	}
	else if (moving)
	{
		cmd.angles[YAW] = (short)(ANGLE2SHORT(e->client->v_angle[YAW]) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(ANGLE2SHORT(e->client->v_angle[PITCH]) -
			e->client->ps.pmove.delta_angles[PITCH]);
	}
	if (command.aim_owned)
	{
		cmd.angles[YAW] = (short)(ANGLE2SHORT(command.yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(ANGLE2SHORT(command.pitch) -
			e->client->ps.pmove.delta_angles[PITCH]);
	}
	else
		SG_BotCombatFrame(e, &cmd, &engaged);
	bot->engaged_last = engaged;
	/* Nothing in sight and not going anywhere: the view stays where it is
	 * rather than snapping to nothing. */
	if (!engaged && !moving && !command.aim_owned)
	{
		cmd.angles[YAW] = (short)(ANGLE2SHORT(e->client->v_angle[YAW]) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(ANGLE2SHORT(e->client->v_angle[PITCH]) -
			e->client->ps.pmove.delta_angles[PITCH]);
	}
	/* Posted and nothing in sight: face where the approaches are. */
	if (!engaged && !moving && bot->post_facing_valid &&
		bot->step.kind == SG_RUNE_STEP_ARRIVED)
	{
		float yaw = atan2f(bot->post_facing[1] - e->s.origin[1],
			bot->post_facing[0] - e->s.origin[0]) * 180.0f / (float)M_PI;

		cmd.angles[YAW] = (short)(ANGLE2SHORT(yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(0 - e->client->ps.pmove.delta_angles[PITCH]);
	}
	if (moving)
	{
		float command_yaw = (float)SHORT2ANGLE(((int)cmd.angles[YAW] +
			e->client->ps.pmove.delta_angles[YAW]) & 65535);
		float radians = command_yaw * (float)M_PI / 180.0f;

		cmd.forwardmove = Move(400.0f * command.speed *
			(command.direction[0] * cosf(radians) +
			 command.direction[1] * sinf(radians)));
		cmd.sidemove = Move(400.0f * command.speed *
			(command.direction[0] * sinf(radians) -
			 command.direction[1] * cosf(radians)));
	}
	/* Bunny hopping: on a plain walk at speed with no launch ahead, hop
	 * whenever the exact tracer says the hop lands cleanly on floor within
	 * a second.  Air control keeps the run, and the ground's friction is
	 * skipped for the flight's length. */
	if (command.up == 0.0f && moving && body.supported && e->client->hookstate == 0 &&
		bot->step.kind == SG_RUNE_STEP_CROSS && !bot->step.ease &&
		(bot->step.move_kind == SG_RUNE_MOVE_WALK || bot->step.move_kind == SG_RUNE_MOVE_RAMP) &&
		bot->step.cost_to_go > HOP_ROUTE_LEFT && bot->cell != SG_RUNE_CX_INDEX_NONE)
	{
		float speed = sqrtf(e->velocity[0] * e->velocity[0] + e->velocity[1] * e->velocity[1]);

		if (speed >= HOP_SPEED)
		{
			vec3_t launch;
			sg_rune_flight_t hop;

			launch[0] = e->velocity[0];
			launch[1] = e->velocity[1];
			launch[2] = sg_rune_level.artifact.law.jump_velocity;
			if (SG_RuneFlightLandsRobustly(&sg_rune_level.artifact.complex,
				&sg_rune_level.artifact.law, bot->cell, e->s.origin, launch,
				RELEASE_LIVE_TOLERANCE, 1, &hop) && hop.seconds < HOP_MAX_SECONDS)
			{
				/* And the run after the landing stays on floor: a body at hop
				 * speed covers another body length before it can turn. */
				vec3_t on, flat;
				sg_rune_flight_t next;

				VectorCopy(hop.landing, on);
				on[2] += 1.0f;
				flat[0] = launch[0];
				flat[1] = launch[1];
				flat[2] = 0.0f;
				if (hop.landing_cell < sg_rune_level.artifact.complex.cell_count &&
					SG_RuneFlightTrace(&sg_rune_level.artifact.complex,
						&sg_rune_level.artifact.law, hop.landing_cell, on, flat, &next) &&
					next.outcome == SG_RUNE_FLIGHT_LANDED && next.seconds < HOP_RUN_ON_SECONDS)
					command.up = 1.0f;
			}
		}
	}
	bot->logged_status = (uint8_t)command.status;
	VectorCopy(command.direction, bot->logged_direction);
	bot->logged_speed = command.speed;
	bot->logged_up = command.up;
	cmd.upmove = Move(400.0f * command.up);
	if (command.attack)
		cmd.buttons |= BUTTON_ATTACK;
	ClientThink(e, &cmd);
	if (command.want_launcher)
		SG_BotRequestLauncher(e);
	if (bot->rescue && e->client->hookstate == 0 && bot->hook_phase != 2 &&
		SG_BotHookReady(e))
	{
		/* The rescue rope: aimed and fired here, whatever the step said. */
		float yaw, pitch;
		vec3_t eye;

		VectorCopy(e->s.origin, eye);
		eye[2] += (float)e->viewheight;
		yaw = atan2f(bot->rescue_anchor[1] - eye[1], bot->rescue_anchor[0] - eye[0]) *
			180.0f / (float)M_PI;
		pitch = -atan2f(bot->rescue_anchor[2] - eye[2],
			sqrtf((bot->rescue_anchor[0] - eye[0]) * (bot->rescue_anchor[0] - eye[0]) +
				(bot->rescue_anchor[1] - eye[1]) * (bot->rescue_anchor[1] - eye[1]))) *
			180.0f / (float)M_PI;
		e->client->v_angle[YAW] = yaw;
		e->client->v_angle[PITCH] = pitch;
		command.hook_fire = 1U;
		command.hook_release = 0U;
	}
	/* No rope at a bite the eye cannot see: a bolt through a wall is a
	 * miss and a second of standing for nothing. */
	if (command.hook_fire && bot->step.hook_point_present)
	{
		vec3_t eye;
		trace_t tr;

		VectorCopy(e->s.origin, eye);
		eye[2] += (float)e->viewheight;
		tr = gi.trace(eye, NULL, NULL, bot->step.hook_point, e, MASK_SOLID);
		if (tr.fraction < 1.0f && VectorLength(tr.endpos) > 0.0f)
		{
			vec3_t short_of;

			VectorSubtract(bot->step.hook_point, tr.endpos, short_of);
			if (VectorLength(short_of) > HOOK_BITE_SLACK)
			{
				command.hook_fire = 0U;
				if (bot->flight_capability != SG_RUNE_CX_INDEX_NONE && !bot->rescue)
				{
					Avoid(bot, bot->flight_capability);
					if (sg_cv.debug && sg_cv.debug->value)
						gi.dprintf("SGBOT %s bite out of sight: ride avoided\n",
							e->client->pers.netname);
				}
			}
		}
	}
	if (command.hook_fire && SG_BotHookReady(e))
	{
		if (sg_cv.debug && sg_cv.debug->value)
			gi.dprintf("SGROPE %s fired speed=%.0f ground=%d %s at=(%.0f %.0f %.0f) bite=(%.0f %.0f %.0f)\n",
				e->client->pers.netname, VectorLength(e->velocity), e->groundentity != NULL,
				bot->rescue ? "rescue" : "ride", e->s.origin[0], e->s.origin[1], e->s.origin[2],
				bot->step.hook_point[0], bot->step.hook_point[1], bot->step.hook_point[2]);
		bot->rope_fired_at = level.time;
		bot->rope_bit_at = 0.0f;
		Cmd_Hook_f(e);
		bot->hook_phase = 2;
		bot->release_held_logged = 0U;
		bot->hook_entity = e->client->hook;
		bot->fired_capability = bot->rescue ? SG_RUNE_CX_INDEX_NONE : bot->flight_capability;
		bot->fired_bit = 0U;
	}
	else if (command.hook_release && e->client->hookstate != 0)
	{
		sg_rune_flight_t live;
		qboolean let_go = true;

		/* Let go only where the body, with the velocity it has now, lands
		 * on a floor; otherwise ride on to the bite and hang there. */
		/* The record's arc was checked with margins when it was made; the
		 * live check catches a gross deviation, not a marginal one, so it
		 * runs at the speed the body has. */
		if (e->client->hookstate == 2 && bot->cell != SG_RUNE_CX_INDEX_NONE)
			let_go = SG_RuneFlightLandsRobustly(&sg_rune_level.artifact.complex,
				&sg_rune_level.artifact.law, bot->cell, e->s.origin, e->velocity,
				RELEASE_LIVE_TOLERANCE, 1, &live) ? true : false;
		if (sg_cv.debug && sg_cv.debug->value && !let_go && !bot->release_held_logged)
		{
			bot->release_held_logged = 1U;
			gi.dprintf("SGROPE %s release held: live arc fails at speed=%.0f v=(%.0f %.0f %.0f) from (%.0f %.0f %.0f) cell=%u outcome=%d clips=%u landing=(%.0f %.0f %.0f) cell %u sem=0x%x\n",
				e->client->pers.netname, VectorLength(e->velocity),
				e->velocity[0], e->velocity[1], e->velocity[2],
				e->s.origin[0], e->s.origin[1], e->s.origin[2], (unsigned)bot->cell,
				(int)live.outcome, (unsigned)live.clips, live.landing[0], live.landing[1], live.landing[2],
				(unsigned)live.landing_cell,
				live.landing_cell < sg_rune_level.artifact.complex.cell_count ?
					(unsigned)sg_rune_level.artifact.complex.cells[live.landing_cell].semantics : 0U);
		}
		if (let_go)
		{
			if (sg_cv.debug && sg_cv.debug->value)
				gi.dprintf("SGROPE %s let go speed=%.0f vz=%.0f ground=%d\n",
					e->client->pers.netname, VectorLength(e->velocity), e->velocity[2],
					e->groundentity != NULL);
			ctf_hook_abort(e);
			bot->hook_phase = 3;
			bot->hook_entity = NULL;
			bot->rescue = 0U;
		}
	}
	if (bot->hook_phase == 3 && e->groundentity && e->client->hookstate == 0)
		bot->hook_phase = 0;
}

/* A body that has not moved a body length in two seconds while it has a
 * crossing to make is stuck: forget the destination's cell so the route is
 * taken again from where the body actually is, and hop once. */
static qboolean Stuck(sg_bot_t *bot, edict_t *e)
{
	float dx = e->s.origin[0] - bot->stuck_origin[0];
	float dy = e->s.origin[1] - bot->stuck_origin[1];
	float dz = e->s.origin[2] - bot->stuck_origin[2];

	/* Only a plain contact crossing can be stuck: a body easing to a
	 * point, lining up a launch, or holding a rope is slow on purpose. */
	if (dx * dx + dy * dy + dz * dz > 32.0f * 32.0f ||
		(bot->step.kind != SG_RUNE_STEP_CROSS &&
		 bot->step.kind != SG_RUNE_STEP_ARRIVED) ||
		bot->step.ease || bot->step.run_up_present || e->client->hookstate != 0 ||
		(bot->step.kind == SG_RUNE_STEP_CROSS &&
		 bot->step.move_kind != SG_RUNE_MOVE_WALK &&
		 bot->step.move_kind != SG_RUNE_MOVE_CROUCH &&
		 bot->step.move_kind != SG_RUNE_MOVE_RAMP &&
		 bot->step.move_kind != SG_RUNE_MOVE_SWIM))
	{
		VectorCopy(e->s.origin, bot->stuck_origin);
		bot->stuck_since = level.time;
		return false;
	}
	if (level.time - bot->stuck_since < 2.0f)
		return false;
	bot->stuck_since = level.time;
	bot->destination_cell = SG_RUNE_CX_INDEX_NONE;
	/* The crossing the body cannot make is not offered again for a while:
	 * the field finds another way round. */
	if (bot->step.kind == SG_RUNE_STEP_CROSS &&
		bot->step.capability != SG_RUNE_CX_INDEX_NONE)
	{
		Avoid(bot, bot->step.capability);
		if (sg_cv.debug && sg_cv.debug->value)
			gi.dprintf("SGBOT %s stuck on crossing %u: avoided\n",
				e->client->pers.netname, (unsigned)bot->step.capability);
	}
	return true;
}

void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	qboolean carrying;
	vec3_t destination;

	if (!e || !e->client)
		return;
	if (e->deadflag)
	{
		ThinkDead(bot, e);
		return;
	}
	bot->death_taught = false;
	carrying = BotCarrying(e);
	bot->role = RoleFor(bot, carrying);
	SG_BotCalloutRole(bot, bot->role);
	bot->last_role = bot->role;
	if (carrying && !bot->was_carrying)
		bot->carry_start = level.time;
	bot->was_carrying = carrying;
	if (!SG_RuneLevelCurrent() || !DestinationFor(bot, bot->role, destination))
	{
		if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
			gi.dprintf("SGBOT %s no destination: rune %d role %d own flag %p "
				"enemy flag %p\n", e->client->pers.netname, SG_RuneLevelCurrent(),
				bot->role, (void *)ctf_flagsearch(e->client->ctf.teamnum),
				(void *)ctf_flagsearch(SG_EnemyTeam(e->client->ctf.teamnum)));
		memset(&bot->step, 0, sizeof(bot->step));
		bot->step.kind = SG_RUNE_STEP_HOLD;
		Emit(bot, e);
		return;
	}
	SelectStep(bot, e, destination);
	ApplyMechanism(bot, e);
	if (Stuck(bot, e) && e->groundentity)
		bot->step.move_kind = SG_RUNE_MOVE_JUMP;
	/* Every decision that differs from the last one written, and a
	 * heartbeat every five seconds so a long crossing still shows. */
	if (sg_cv.debug && sg_cv.debug->value &&
		(sg_cv.debug->value >= 2.0f || level.framenum % 50 == 0 ||
		 bot->logged_kind != bot->step.kind ||
		 bot->logged_move != bot->step.move_kind ||
		 bot->logged_capability != bot->step.capability ||
		 fabsf(bot->logged_target[0] - bot->step.target[0]) > 4.0f ||
		 fabsf(bot->logged_target[1] - bot->step.target[1]) > 4.0f ||
		 fabsf(bot->logged_target[2] - bot->step.target[2]) > 4.0f))
	{
		bot->logged_kind = (uint8_t)bot->step.kind;
		bot->logged_move = (uint8_t)bot->step.move_kind;
		bot->logged_capability = bot->step.capability;
		VectorCopy(bot->step.target, bot->logged_target);
		gi.dprintf("SGBOT %s role=%d cell=%u dest=%u step=%s/%s cap=%u at=(%.0f %.0f %.0f) "
			"target=(%.0f %.0f %.0f) cost=%.1f st=%c%c v=%.0f hook=%d hp=%d cmd=%u(%.2f %.2f %.2f)x%.2f up=%.1f",
			e->client->pers.netname,
			bot->role, (unsigned int)bot->cell, (unsigned int)bot->destination_cell,
			SG_RuneStepKindString(bot->step.kind),
			bot->step.kind == SG_RUNE_STEP_CROSS ? SG_RuneMoveKindString(
				(sg_rune_move_kind_t)bot->step.move_kind) : "-",
			(unsigned int)bot->step.capability,
			e->s.origin[0], e->s.origin[1], e->s.origin[2], bot->step.target[0],
			bot->step.target[1], bot->step.target[2], bot->step.cost_to_go,
			(e->client->ps.pmove.pm_flags & PMF_DUCKED) ? 'C' : 'S',
			bot->step.crouching_next ? 'c' : 's', VectorLength(e->velocity),
			e->client->hookstate, e->health, (unsigned)bot->logged_status,
			bot->logged_direction[0], bot->logged_direction[1], bot->logged_direction[2],
			bot->logged_speed, bot->logged_up);
		if (bot->step.hook_point_present)
			gi.dprintf(" anchor=(%.0f %.0f %.0f) release=%.0f",
				bot->step.hook_point[0], bot->step.hook_point[1],
				bot->step.hook_point[2], bot->step.hook_release_distance);
		gi.dprintf("\n");
	}
	Emit(bot, e);
}

/* What a human does, every frame, in the same terms as the bots' lines:
 * where, how fast, on the ground or not, the rope, the crouch, the jump.
 * The owner's own movement is the standard the bots are measured against. */
void SG_HumanTrace(edict_t *ent, const usercmd_t *ucmd)
{
	if (!sg_cv.debug || !sg_cv.debug->value || !ent || !ent->client || (ent->flags & FL_BOT) ||
		ent->movetype == MOVETYPE_NOCLIP || ent->deadflag || !ucmd)
		return;
	gi.dprintf("SGHUMAN %s at=(%.0f %.0f %.0f) v=(%.0f %.0f %.0f) speed=%.0f ground=%d "
		"hook=%d duck=%d jump=%d fwd=%d side=%d yaw=%.0f pitch=%.0f cell=%u\n",
		ent->client->pers.netname, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2],
		ent->velocity[0], ent->velocity[1], ent->velocity[2],
		sqrtf(ent->velocity[0] * ent->velocity[0] + ent->velocity[1] * ent->velocity[1]),
		ent->groundentity != NULL, ent->client->hookstate,
		(ent->client->ps.pmove.pm_flags & PMF_DUCKED) != 0, ucmd->upmove > 0,
		ucmd->forwardmove, ucmd->sidemove, ent->client->v_angle[YAW],
		ent->client->v_angle[PITCH],
		(unsigned int)SG_RuneLevelLocate(ent->s.origin,
			(ent->client->ps.pmove.pm_flags & PMF_DUCKED) != 0, NULL));
}

void SG_RunFrame(void)
{
	int i;

	SG_CvarsInit();
	if (!sg_level_setup_attempted)
		(void)SG_LevelSetup();
	Botfill_Frame();
	TeamPass(CTF_TEAM_RED);
	TeamPass(CTF_TEAM_BLUE);
	SG_BotCalloutFrame();
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent;

		if (!sg_bots[i].active)
			continue;
		ent = sg_bots[i].ent;
		if (!ent || !ent->inuse || !ent->client || !(ent->flags & FL_BOT))
		{
			if (ent && ent->client && !ent->inuse && (ent->flags & FL_BOT))
				SG_BotHostFreeClient(ent);
			SG_DisownBot(ent);
			continue;
		}
		if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
			ent->client->ctf.teamnum != CTF_TEAM_BLUE)
		{
			SG_RetireBotForClient(ent);
			continue;
		}
		SG_BotThink(&sg_bots[i]);
	}
}
