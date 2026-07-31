/*
 * sg_caco.c -- CACO: the eye. Belief, not omniscience.
 *
 * Everything a SLIPGATE bot decides from goes through here. The rule is the
 * one a fair player lives under:
 *
 *   - flag ON its stand or not: common knowledge, it is on everyone's HUD
 *     (LMCTF maintains that via ctf_flagathome / matchstate on the score
 *     displays); WHERE a flag is when it is not home is not.
 *   - a dropped flag's position, an enemy carrier's position: known only if
 *     some teammate has actually seen it -- PVS plus a trace, the same
 *     visibility LMCTF's own code uses -- and the belief carries the time it
 *     was seen so it ages.
 *   - own team's carrier: teammates know who carries (HUD icon) and learn
 *     position from sightings.
 *
 * Team belief is shared per team: bots on one team pool their sightings the
 * way humans pool callouts. Aging past SG_BELIEF_STALE clears a position
 * back to unknown.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"

sg_team_belief_t sg_caco_team_belief;   /* [0]=red beliefs about red flag etc */

static float caco_next_scan;

/*
 * Can this bot see that entity? The same test the game itself makes for
 * sight: line from eyes to target center unobstructed by the world.
 */
static qboolean Caco_Visible(edict_t *viewer, edict_t *target)
{
	vec3_t eye, mid;
	trace_t tr;

	VectorCopy(viewer->s.origin, eye);
	eye[2] += viewer->viewheight;
	VectorAdd(target->absmin, target->absmax, mid);
	VectorScale(mid, 0.5f, mid);

	if (!gi.inPVS(eye, mid))
		return false;
	tr = gi.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}

/*
 * The flag entities: LMCTF spawns classname "flag" with flagteam set.
 * ctf_flagathome answers "is it sitting on its stand" -- that part is HUD
 * knowledge and read directly; positions when astray are only taken from
 * sightings below.
 */
static void Caco_ScanFlags(rune_t *r, edict_t *viewer, int viewer_team)
{
	edict_t *e = NULL;

	while ((e = G_Find(e, FOFS(classname), "flag")) != NULL)
	{
		int fi = (e->flagteam == CTF_TEAM_RED) ? 0 : 1;
		sg_belief_flag_t *b = &sg_caco_team_belief.flag[fi];

		/* common knowledge: home or not (HUD) */
		if (ctf_flagathome(e))
		{
			b->state = SG_FLAG_HOME;
			b->where_seed = -1;
			continue;
		}

		/* not home. carried or dropped is also HUD-level in LMCTF
		 * (the score line shows taken flags), but WHERE requires sight. */
		if (b->state == SG_FLAG_HOME)
		{
			b->state = SG_FLAG_ASTRAY;
			b->where_seed = -1;         /* until someone sees it */
			b->seen_time = 0.0f;
		}

		if (viewer && Caco_Visible(viewer, e))
		{
			b->where_seed = Rune_NearestSeed(r, e->s.origin);
			b->seen_time = level.time;
		}
	}
}

/*
 * Carriers: LMCTF marks them with EF_FLAG1/EF_FLAG2 on the player entity
 * (p_view.c G_SetClientEffects). Which is HUD knowledge as identity; the
 * position needs a sighting by a teammate of the believing team.
 */
static void Caco_ScanCarriers(rune_t *r, edict_t *viewer, int viewer_team)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *p = g_edicts + 1 + i;
		int carried, enemy_of;

		if (!p->inuse || !p->client)
			continue;
		if (!(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
			continue;

		/* which team's flag does this player carry? EF_FLAG1 is red's */
		carried = (p->s.effects & EF_FLAG1) ? 0 : 1;
		/* the team whose flag is carried wants to know the carrier */
		enemy_of = carried;

		if (p->client->ctf.teamnum == viewer_team)
		{
			/* our own carrier: identity is team knowledge, position from
			 * sighting (including the carrier seeing themself) */
			sg_belief_carrier_t *c =
				&sg_caco_team_belief.carrier[viewer_team - 1];

			c->client = i;
			if (viewer && (viewer == p || Caco_Visible(viewer, p)))
			{
				c->seed = Rune_NearestSeed(r, p->s.origin);
				c->seen_time = level.time;
			}
		}
		else if (viewer && viewer->client &&
		         viewer->client->ctf.teamnum == enemy_of + 1)
		{
			/* an enemy carrying OUR flag: the belief that matters most */
			sg_belief_carrier_t *c =
				&sg_caco_team_belief.enemy_carrier[enemy_of];

			if (Caco_Visible(viewer, p))
			{
				c->client = i;
				c->seed = Rune_NearestSeed(r, p->s.origin);
				c->seen_time = level.time;
			}
		}
	}
}

static void Caco_Age(void)
{
	int i;

	for (i = 0; i < 2; i++)
	{
		if (sg_caco_team_belief.flag[i].where_seed >= 0 &&
		    level.time - sg_caco_team_belief.flag[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.flag[i].where_seed = -1;

		if (sg_caco_team_belief.carrier[i].seed >= 0 &&
		    level.time - sg_caco_team_belief.carrier[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.carrier[i].seed = -1;

		if (sg_caco_team_belief.enemy_carrier[i].seed >= 0 &&
		    level.time - sg_caco_team_belief.enemy_carrier[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.enemy_carrier[i].seed = -1;

		/* a carrier who died stops being one */
		if (sg_caco_team_belief.carrier[i].client >= 0)
		{
			edict_t *p = g_edicts + 1 + sg_caco_team_belief.carrier[i].client;

			if (!p->inuse || !p->client ||
			    !(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
				sg_caco_team_belief.carrier[i].client = -1;
		}
	}
}

/*
 * Each SLIPGATE bot contributes its sight each frame; the shared scan of
 * flag home-state runs on a cadence with no viewer at all (HUD knowledge
 * needs no eyes).
 */
void Caco_See(rune_t *r, edict_t *viewer)
{
	if (!viewer || !viewer->client)
		return;
	Caco_ScanFlags(r, viewer, viewer->client->ctf.teamnum);
	Caco_ScanCarriers(r, viewer, viewer->client->ctf.teamnum);
}

void Caco_Frame(rune_t *r)
{
	if (level.time >= caco_next_scan)
	{
		caco_next_scan = level.time + 0.5f;
		Caco_ScanFlags(r, NULL, 0);     /* HUD-level state only */
		Caco_Age();
	}
}

void Caco_Reset(void)
{
	int i;

	memset(&sg_caco_team_belief, 0, sizeof(sg_caco_team_belief));
	for (i = 0; i < 2; i++)
	{
		sg_caco_team_belief.flag[i].where_seed = -1;
		sg_caco_team_belief.carrier[i].client = -1;
		sg_caco_team_belief.carrier[i].seed = -1;
		sg_caco_team_belief.enemy_carrier[i].client = -1;
		sg_caco_team_belief.enemy_carrier[i].seed = -1;
	}
	caco_next_scan = 0.0f;
}
