/*
 * sg_lead.c -- the early-return errand (sg_itemlead): get back to the pad
 * early like humans, not at T exactly.  Moved verbatim from sg_arach.c in
 * the 2026-08-11 standards pass.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_lead_random.h"

/* -------------------------------------------------- the early-return errand
 *
 * THE OWNER'S RULING, 2026-08-05: a bot whose team clock is armed "should GET
 * BACK EARLY like humans, not arrive at T exactly".
 *
 * The pricing already knew the clock existed -- Worth_Quad (sg_combat.c) prices
 * an empty pedestal at zero unless it is within SG_QUAD_CAMP_LEAD of coming
 * back -- but a weight can only bend a route the bot was already walking. What
 * a player who timed the quad does is not a bend: he stops doing the thing he
 * was doing, leaves with time in hand, and stands NEAR the pad facing the door
 * until it spawns. That is an errand, so it is written as one.
 *
 * WHAT BEING WRONG COSTS, which is where the gates come from. Leaving for a pad
 * is leaving a job. The errand is refused to a carrier, refused while our own
 * flag is out, refused while our team has a live carrier who needs bodies
 * around him, refused to anyone in a fight or freshly hit -- every one of those
 * is a thing the team loses while one bot admires its own timing -- and refused
 * to everybody but ONE bot per team per pad, because five bots on one pedestal
 * is not anticipation, it is a queue.
 *
 * WHY POWERUP PADS ONLY. The errand needs three things and only the powerup
 * rows carry all three: a per-team respawn clock (sg_caco_items, armed under
 * sg_itemcomm by a callout that was actually spoken), a spawn position that is
 * map knowledge, and enough worth to justify the walk. Red armour's clock lives
 * in sg_chat.c's private watch table, which this file cannot read and which has
 * no route field behind it; the day it moves into the belief table this code
 * picks it up with no edit. Runes are excluded by construction -- the ruling
 * gives them no clock at all, so there is never a T to be early for.
 */

static qboolean Lead_On(void)
{
	/* the clock belongs to itemcomm; with no clock there is nothing to be
	 * early for, and the two cvars are one feature in two halves */
	if (!SG_ItemComm())
		return false;
	return (sg_cv.itemlead->value > 0.0f) ? true : false;
}

static void Lead_RetireRoute(sg_bot_t *bot)
{
	rune_t *r = SG_Rune();

	if (!bot)
		return;
	/* A strategy transition may discard an ordinary road.  Serialized action
	 * controllers keep their authority until their own terminal boundary. */
	if (r && r->links && bot->commit_link >= 0 &&
	    bot->commit_link < r->hdr.num_links &&
	    r->links[bot->commit_link].action == RL_RUN)
	{
		bot->commit_link = -1;
		bot->commit_until = 0.0f;
	}
	bot->tac_seed = -1;
}

void Lead_Abort(sg_bot_t *bot, const char *why)
{
	edict_t *e = bot->ent;

	if (!bot->lead_ent)
		return;
	if (sg_cv.debug->value)
		sg_host.dprint("ITEMLEAD %s abort (%s)\n", e->client->pers.netname, why);

	/*
	 * Hand the pad back instead of letting the lease run out on its own. The
	 * expiry is the safety net for a bot that stops asking without saying so
	 * (a disconnect, a level change); a bot that KNOWS it is not going should
	 * not make the next claimant wait a second for the news.
	 */
	if (e && e->client && bot->lead_slot >= 0 &&
	    bot->lead_slot < sg_caco_num_items)
	{
		int ti = (e->client->ctf.teamnum == CTF_TEAM_BLUE) ? 1 : 0;
		int cl = (int)(e - g_edicts) - 1;

		if (sg_caco_items[ti][bot->lead_slot].claimed_by == cl)
		{
			sg_caco_items[ti][bot->lead_slot].claimed_until = 0.0f;
			sg_caco_items[ti][bot->lead_slot].claimed_by = -1;
		}
	}

	bot->lead_ent = 0;
	bot->lead_slot = -1;
	bot->lead_seed = -1;
	bot->lead_at = 0.0f;
	bot->lead_state = SG_LEAD_WAITING;
	bot->lead_seen_up_at = -1.0f;
	bot->lead_inferred_until = 0.0f;
	/* an errand starting or ending is a STRATEGY change, and the tactical
	 * waypoint's own contract is that a strategy change retires it -- without
	 * this the bot descends the previous goal's flood for up to ten seconds
	 * after changing its mind */
	Lead_RetireRoute(bot);
}

void Lead_NoteItemTaken(edict_t *taker, edict_t *item)
{
	int i, item_ent;

	if (!taker || !item)
		return;
	item_ent = (int)(item - g_edicts);
	if (item_ent <= 0 || item_ent >= globals.num_edicts)
		return;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bot_t *bot = &sg_bots[i];

		if (!bot->active || !bot->ent || bot->lead_ent != item_ent)
			continue;
		Lead_Abort(bot, bot->ent == taker ? "picked" : "other taken");
	}
}

void Lead_NoteItemRejected(edict_t *taker, edict_t *item)
{
	int i, item_ent;

	if (!taker || !item)
		return;
	item_ent = (int)(item - g_edicts);
	if (item_ent <= 0 || item_ent >= globals.num_edicts)
		return;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bot_t *bot = &sg_bots[i];

		if (!bot->active || bot->ent != taker || bot->lead_ent != item_ent)
			continue;
		Lead_Abort(bot, "pickup rejected");
		return;
	}
}

qboolean Lead_PickupTarget(const sg_bot_t *bot, vec3_t target)
{
	edict_t *item;

	if (!bot || !target || bot->lead_state != SG_LEAD_SPAWNED ||
	    bot->lead_ent <= 0 || bot->lead_ent >= globals.num_edicts)
		return false;
	item = g_edicts + bot->lead_ent;
	if (!item->inuse || !item->classname ||
	    (strcmp(item->classname, "item_quad") != 0 &&
	     strcmp(item->classname, "item_invulnerability") != 0) ||
	    !G_PowerupPickupEligible(item, bot->ent))
		return false;
	VectorCopy(item->s.origin, target);
	return true;
}

/*
 * The route to a pad that is NOT standing. The class and per-item fields are
 * flooded from items CACO believes are up (sg_fields.c Class_Build), which is
 * exactly the set an empty pedestal is not in -- so the errand floods its own,
 * the way the escort and the interposition do a few hundred lines below. One
 * flood per frame per errand, and at most one errand per team.
 */
static qboolean Lead_Flood(int *field, int padseed, int here)
{
	int cost = 0;

	if (!SG_Rune() || padseed < 0 || padseed >= SG_Rune()->hdr.num_seeds)
		return false;
	Field_Flood(SG_Rune(), field, &padseed, &cost, 1);
	if (here < 0 || here >= SG_Rune()->hdr.num_seeds)
		return false;
	return (field[here] < SG_FIELD_INF) ? true : false;
}

/*
 * The whole errand, once per frame. Returns the field to route on while one is
 * running and NULL otherwise; the caller substitutes it for the role's own
 * goal field, which is what makes the pad a live goal rather than a preference.
 */
const int *Lead_Field(sg_bot_t *bot, sg_role_t role, qboolean carrying,
	int ordered_role)
{
	static int			lead_field[SG_MAX_SEEDS];
	edict_t				*e = bot->ent;
	const sg_persona_t	*p;
	sg_belief_item_t	*b;
	float				lead, camp, travel, best_t = 0.0f;
	int					team, ti, cl, i, best = -1, padseed = -1;

	if (!Lead_On())
	{
		Lead_Abort(bot, "switched off");
		return NULL;
	}
	if (!SG_Rune() || bot->seed < 0 || !e->client)
		return NULL;

	team = e->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return NULL;
	ti = SG_TeamIdx(team);
	cl = (int)(e - g_edicts) - 1;
	if (cl < 0 || cl >= game.maxclients)
		return NULL;

	/*
	 * The jobs that outrank a pad. Said before the live-errand branch as well
	 * as before the commit, so one of these arriving mid-errand ends it on the
	 * frame it arrives -- which is the difference between a bot that times
	 * items and a bot that ignores the game to time items.
	 */
	if (carrying)
	{
		Lead_Abort(bot, "carrying");
		return NULL;
	}
	if (ordered_role >= 0)
	{
		/* A pad timer is optional preparation.  Any live human order is the
		 * bot's mission and retires an already-running errand immediately. */
		Lead_Abort(bot, "human order");
		return NULL;
	}
	if (sg_caco_team_belief.flag[ti][ti].state == SG_FLAG_ASTRAY ||
	    role == SG_ROLE_RECOVER)
	{
		Lead_Abort(bot, "our flag out");
		return NULL;
	}
	if (role == SG_ROLE_ESCORT)
	{
		Lead_Abort(bot, "escort duty");
		return NULL;
	}
	if (sg_caco_team_belief.carrier[ti].client >= 0)
	{
		Lead_Abort(bot, "carrier live");
		return NULL;
	}
	/*
	 * The watchman is not available for errands. The .dpo lesson this file
	 * already records -- never empty the stand for a post -- applies at least
	 * as hard to emptying it for a pedestal on the other side of the map. The
	 * patrol defender is free to go; it is the one whose whole job is not
	 * being in one place.
	 */
	if (role == SG_ROLE_DEFEND && bot->def_stand)
	{
		Lead_Abort(bot, "watchman");
		return NULL;
	}
	if (bot->engaged_last || SG_CombatDuel(e, NULL, NULL, NULL))
	{
		Lead_Abort(bot, "engaged");
		return NULL;
	}
	if (Beat_HurtSince(e, level.time - 3.0f))
	{
		Lead_Abort(bot, "taking fire");
		return NULL;
	}

	/* ------------------------------------------------ an errand in progress */
	if (bot->lead_ent > 0)
	{
		if (bot->lead_slot < 0 || bot->lead_slot >= sg_caco_num_items)
		{
			Lead_Abort(bot, "stale row");
			return NULL;
		}
		b = &sg_caco_items[ti][bot->lead_slot];
		if (b->ent != bot->lead_ent || b->ent <= 0 ||
		    b->ent >= globals.num_edicts)
		{
			/* the table was rebuilt under us (level change): the row names a
			 * different entity now and the errand is about nothing */
			Lead_Abort(bot, "stale row");
			return NULL;
		}
		if (!G_PowerupPickupEligible(&g_edicts[b->ent], e))
		{
			/* The game pickup law is the authority here.  A full inventory may
			 * make a once-valid errand impossible while the bot is travelling;
			 * release the team lease instead of camping an untakeable item. */
			Lead_Abort(bot, "powerup capacity");
			return NULL;
		}
		if (b->believed_up)
		{
			if (bot->lead_state != SG_LEAD_SPAWNED)
			{
				float hard_until = bot->lead_since + SG_LEAD_MAXWAIT;

				bot->lead_state = SG_LEAD_SPAWNED;
				/* A sighting newer than the one present at commitment confirms
				 * the physical spawn.  A clock-only inference gets a short,
				 * bounded chance to touch the entity and no second respawn wait. */
				if (b->seen_up_time > bot->lead_seen_up_at)
					bot->lead_inferred_until = 0.0f;
				else
				{
					bot->lead_inferred_until = level.time + SG_LEAD_INFER_GRACE;
					if (bot->lead_inferred_until > bot->lead_at + SG_LEAD_GRACE)
						bot->lead_inferred_until = bot->lead_at + SG_LEAD_GRACE;
					if (bot->lead_inferred_until > hard_until)
						bot->lead_inferred_until = hard_until;
				}
				bot->lead_seen_up_at = b->seen_up_time;
				Lead_RetireRoute(bot);
			}
			else if (b->seen_up_time > bot->lead_seen_up_at)
			{
				bot->lead_seen_up_at = b->seen_up_time;
				bot->lead_inferred_until = 0.0f;
			}
			if (bot->lead_inferred_until > 0.0f &&
			    SG_TimerReadyStrict(bot->lead_inferred_until))
			{
				Lead_Abort(bot, "spawn unconfirmed");
				return NULL;
			}
			if (SG_TimerReadyStrict(bot->lead_since + SG_LEAD_MAXWAIT))
			{
				Lead_Abort(bot, "missed pickup");
				return NULL;
			}
		}
		else if (bot->lead_state == SG_LEAD_SPAWNED)
		{
			Lead_Abort(bot, "other taken");
			return NULL;
		}
		if (bot->lead_state == SG_LEAD_WAITING &&
		    b->believed_respawn_time <= 0.0f)
		{
			Lead_Abort(bot, "clock gone");
			return NULL;
		}
		if (SG_TimerPending(b->claimed_until) && b->claimed_by != cl)
		{
			Lead_Abort(bot, "claim lost");
			return NULL;
		}
		if (bot->lead_state == SG_LEAD_WAITING)
			bot->lead_at = b->believed_respawn_time; /* a fresher call moves T */
		if (bot->lead_state == SG_LEAD_WAITING &&
		    SG_TimerReadyStrict(bot->lead_at + SG_LEAD_GRACE))
		{
			Lead_Abort(bot, "waited out");
			return NULL;
		}
		/* the miscall ceiling (owner's rule): T may slide as fresher
		 * calls land, but no errand stands the pad longer than
		 * SG_LEAD_MAXWAIT total -- a clock that keeps being wrong is a
		 * miscalled item, and the team needs the body back */
		if (SG_TimerReadyStrict(bot->lead_since + SG_LEAD_MAXWAIT))
		{
			Lead_Abort(bot, "miscalled");
			return NULL;
		}

		/* the lease, re-stamped: stop asking and the pad is free in a second */
		b->claimed_by = cl;
		SG_TimerArm(&b->claimed_until, SG_LEAD_LEASE);

		if (!Lead_Flood(lead_field, bot->lead_seed, bot->seed))
		{
			Lead_Abort(bot, "no route");
			return NULL;
		}
		return lead_field;
	}

	/* ------------------------------------------------------- committing one */
	if (SG_TimerPending(bot->lead_next))
		return NULL;
	SG_TimerArm(&bot->lead_next, SG_LEAD_RETRY);

	/*
	 * The lead itself. Four seconds is the floor a player leaves on at all;
	 * camp_tendency spends up to eight more, so Gate leaves early enough to
	 * settle and Fiend turns up as it lands. The jitter is rolled per attempt
	 * rather than per bot so two bots with the same persona on the same clock
	 * do not step off together every single time.
	 */
	p = SG_PersonaFor(e);
	camp = p ? p->camp_tendency : 0.5f;
	/* the cvar is a DOSE: 1 = the shipped earliness; higher arrives
	 * earlier (a persona-scaled multiplier on the whole window). The
	 * gate stays >0 -- see Lead_On. */
	bot->lead_random = SG_LeadRandomNext(bot->lead_random);
	lead = (SG_LEAD_BASE + camp * SG_LEAD_CAMP +
	        SG_LeadRandomUnit(bot->lead_random) * SG_LEAD_JITTER)
	     * sg_cv.itemlead->value;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		vec3_t	d;
		float	guess, candidate_travel;
		int	candidate_seed;

		b = &sg_caco_items[ti][i];
		if (b->cls != SG_BI_POWERUP || b->believed_up)
			continue;
		if (b->ent <= 0 || b->ent >= globals.num_edicts ||
		    !G_PowerupPickupEligible(&g_edicts[b->ent], e))
			continue;
		if (SG_TimerReady(b->believed_respawn_time))
			continue;
		if (SG_TimerPending(b->claimed_until) && b->claimed_by != cl)
			continue;                   /* somebody on this side has it */

		/*
		 * The cheap half of the travel estimate: straight line at pm_maxspeed.
		 * Corridors only ever make the real road longer, so this UNDERSTATES
		 * travel and therefore opens the window early -- which is what it is
		 * for. The honest number comes off the flood below, once, for the one
		 * candidate worth flooding for.
		 */
		VectorSubtract(b->org, e->s.origin, d);
		guess = VectorLength(d) / SG_LEAD_SPEED;
		if (SG_TimerPending(b->believed_respawn_time - guess - lead))
			continue;

		/* The earliest clock is not necessarily the earliest reachable pad.
		 * Prove each candidate before choosing: an isolated or wrong-component
		 * pad must not suppress a later clock the bot can actually contest. */
		candidate_seed = Rune_NearestSeed(SG_Rune(), b->org);
		if (!Lead_Flood(lead_field, candidate_seed, bot->seed))
			continue;
		candidate_travel = (float)lead_field[bot->seed] / 1000.0f;
		if (SG_TimerPending(b->believed_respawn_time - candidate_travel - lead))
			continue;

		if (best < 0 || b->believed_respawn_time < best_t)
		{
			best = i;
			best_t = b->believed_respawn_time;
			padseed = candidate_seed;
		}
	}

	if (best < 0)
		return NULL;

	b = &sg_caco_items[ti][best];
	/* The candidate loop reuses the scratch flood. Rebuild the winner so the
	 * returned route and the committed entity are the same exact pad. */
	if (!Lead_Flood(lead_field, padseed, bot->seed))
		return NULL;

	/* The honest travel time was already the admission gate; retain it for the
	 * commitment receipt and diagnostic. */
	travel = (float)lead_field[bot->seed] / 1000.0f;

	bot->lead_ent = b->ent;
	SG_Mark(&bot->lead_since);   /* the total-wait clock starts here */
	bot->lead_slot = best;
	bot->lead_seed = padseed;
	bot->lead_at = b->believed_respawn_time;
	bot->lead_state = SG_LEAD_WAITING;
	bot->lead_seen_up_at = b->seen_up_time;
	bot->lead_inferred_until = 0.0f;
	b->claimed_by = cl;
	SG_TimerArm(&b->claimed_until, SG_LEAD_LEASE);
	bot->tac_seed = -1;                 /* a new strategy retires the tactic */

	if (sg_cv.debug->value)
		sg_host.dprint("ITEMLEAD %s -> %s: T %.1f (in %.1fs) lead %.1fs "
		           "travel %.1fs\n",
		           e->client->pers.netname,
		           g_edicts[b->ent].classname ? g_edicts[b->ent].classname
		                                      : "item",
		           bot->lead_at, SG_TimerRemaining(bot->lead_at), lead, travel);
	return lead_field;
}
