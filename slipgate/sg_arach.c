/*
 * sg_arach.c -- ARACHNOTRON: the brain with legs. First walking cut.
 *
 * This is deliberately the minimum that PLAYS: load the rune, flood one
 * cost field per flag, spawn a real client, and every ClientThink descend
 * the field toward the enemy flag -- run home when carrying. No combat, no
 * hook, no speed tricks yet. If a bot cannot get base to base on the rune,
 * nothing fancier deserves to exist; this is the walking skeleton the rest
 * grows on, and it is A/B-able against the legacy bots from the first
 * frame.
 *
 *   sv sg add        spawn a SLIPGATE bot (team by botctfteam, like legacy)
 *   sv sg remove     remove them all
 *
 * Movement per think: pick among the current seed's outgoing links (and
 * staying put) by field value at the destination, steer at the chosen
 * link's endpoint, run at full command. Jump links jump. Arrival needs no
 * test -- the next think re-reads position and field wherever we ended up,
 * which is the whole point of descending a field instead of chasing nodes.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "g_tourney.h"              /* Match_Mode -- the clock read's one caveat */
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"       /* human orders replace the role quota */
#include "slipgate/sg_persona.h"    /* the roster's names, wired to behaviour */

/*
 * The client lifecycle and the glue's edict helpers are declared where the
 * legacy bot spawner declares them -- locally, the way this tree does it
 * (see bl_spawn.c:26-28). Same functions, same signatures.
 */
void		ClientThink(edict_t *ent, usercmd_t *ucmd);
void		Cmd_Kill_f(edict_t *ent);
void		Cmd_Hook_f(edict_t *ent);
void		ClientDisconnect(edict_t *ent);
qboolean	ClientConnect(edict_t *ent, char *userinfo);
void		ClientBegin(edict_t *ent);
void		ClientUserinfoChanged(edict_t *ent, char *userinfo);
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"

#define FIELD_INF       0x3fffffff
#include "slipgate/sg_bot.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_descend.h"


float	sg_grab_time[2] = { -1000.0f, -1000.0f };  /* per team */
float	sg_push_until[2];   /* the conductor's window (sg_wavepush) */
static rune_t	*sg_rune;

rune_t *SG_Rune(void)
{
	return sg_rune;
}


static char		sg_rune_map[64];

const char *SG_RuneMapName(void)
{
	return sg_rune_map;
}

/* one cost field per flag: cost_ms from every seed TO the flag seed */
static int		*sg_field_red;
static int		*sg_field_blue;

/* ------------------------------------------------------------------ rune */

unsigned char *sg_human_use; /* per-link human traffic tier (0-255)
                                     * from the demo corpus; NULL = none */
unsigned char *sg_def_post[2];  /* per-seed human defensive dwell
                                        * tier by team (.dpo plane 0/1):
                                        * where humans actually stand while
                                        * their flag is home -- 19% of it
                                        * within 250u of the stand, the
                                        * rest on the approaches */
unsigned char *sg_def_icept[2]; /* per-seed steal-response END
                                        * positions (.dpo plane 2/3): the
                                        * spots humans run to when the
                                        * flag leaves, aimed at the
                                        * carrier's future, not his now */
/* .dpo plane accessor for sg_fields (the arrays are file-static here) */
unsigned char *SG_DefPlane(int post, int team1)
{
	if (team1 < 0 || team1 > 1)
		return NULL;
	return post ? sg_def_post[team1] : sg_def_icept[team1];
}

unsigned char *sg_human_escape; /* the ESCAPEE's cut: only the flag
                                        * carrier's own entity trajectory in
                                        * the 20s after each steal (.hme) --
                                        * the roads humans actually flee on,
                                        * as opposed to .hml's hunter-heavy
                                        * POV-agnostic window */
unsigned char *sg_human_live; /* same, cut from the 20s windows
                                      * after a steal: how humans move
                                      * when a flag is OUT (.hml) */

/*
 * THE ESCAPE PRIORS (sg_escapeprior, enhancement 6, escapepriors.py).
 * Which WAY humans leave a stand they just robbed, per map and per stolen
 * flag, as an eight-bucket compass distribution: counts of the bearing
 * from the stand to where the human carrier actually was three seconds
 * after the grab. Mined from 268 client demos / 1549 usable steals.
 *
 * Held here as raw counts, one distribution for the CURRENT map, chosen
 * at load time by the same key order the mining tool writes:
 * "<map>:<stolen flag colour>" first, plain "<map>" as the fallback. A
 * CTF map is usually a mirror of itself, so the two stands' exits are
 * mirror bearings of one habit; pooling them is a real loss of signal
 * (measured over the corpus: the pooled entry's bucket entropy is
 * 0.3-0.8 bits higher than either colour's on most maps), and the
 * carrier always knows which stand he just robbed.
 */
#define SG_ESC_BUCKETS	8
static int	sg_escape_count[2][SG_ESC_BUCKETS];  /* [0]=red flag stolen, [1]=blue */
static int	sg_escape_total[2];                  /* 0 = no prior for that flag */


/*
 * A deliberately tiny reader for the one shape escapepriors.py writes:
 * find the quoted key, then read eight integers out of the bracket that
 * follows it. No JSON library, and no pretence of being one -- anything
 * that is not exactly the expected shape leaves the prior unset and the
 * pricing silent, which is the same outcome as a missing file.
 *
 * Matching the key WITH its quotes is what keeps "lmctf01" from matching
 * inside "lmctf01:red" or "lmctf01b", and the tool guarantees no map name
 * appears anywhere in the file outside the maps object.
 */
static qboolean Escape_Parse(const char *buf, const char *key, int *out)
{
	const char *p;
	char quoted[80];
	int i, got[SG_ESC_BUCKETS];

	Com_sprintf(quoted, sizeof(quoted), "\"%s\"", key);
	p = strstr(buf, quoted);
	if (!p)
		return false;
	p += strlen(quoted);
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	if (*p != '[')
		return false;
	p++;
	for (i = 0; i < SG_ESC_BUCKETS; i++)
	{
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
		       *p == ',')
			p++;
		if (*p < '0' || *p > '9')
			return false;
		got[i] = atoi(p);
		while (*p >= '0' && *p <= '9')
			p++;
	}
	for (i = 0; i < SG_ESC_BUCKETS; i++)
		out[i] = got[i];
	return true;
}

static void Escape_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	char lower[64], key[80];
	static char buf[32768];
	cvar_t *gamedir = gi.cvar("gamedir", "", 0);
	size_t n;
	FILE *f;
	int c, i, k;

	memset(sg_escape_count, 0, sizeof(sg_escape_count));
	sg_escape_total[0] = sg_escape_total[1] = 0;

	Com_sprintf(path, sizeof(path), "%s/escape-priors.json",
	            gamedir->string[0] ? gamedir->string : ".");
	f = fopen(path, "rb");
	if (!f)
		return;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;

	/* the file is keyed in lower case; a server that spelled the map
	 * LMCTF35 on the map command still means the same map */
	for (i = 0; mapname[i] && i < (int)sizeof(lower) - 1; i++)
		lower[i] = (char)tolower((unsigned char)mapname[i]);
	lower[i] = 0;

	for (k = 0; k < 2; k++)
	{
		Com_sprintf(key, sizeof(key), "%s:%s", lower,
		            k == 0 ? "red" : "blue");
		if (!Escape_Parse(buf, key, sg_escape_count[k]) &&
		    !Escape_Parse(buf, lower, sg_escape_count[k]))
			continue;
		for (c = 0, i = 0; i < SG_ESC_BUCKETS; i++)
			c += sg_escape_count[k][i];
		sg_escape_total[k] = c;
	}
	if (sg_escape_total[0] > 0 || sg_escape_total[1] > 0)
		gi.dprintf("rune: escape bearings loaded (%s: red n=%d, blue n=%d)\n",
		           path, sg_escape_total[0], sg_escape_total[1]);
}

rune_t *Rune_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	FILE *f;
	rune_t *r;
	int i;
	cvar_t *gamedir = gi.cvar("gamedir", "", 0);

	Com_sprintf(path, sizeof(path), "%s/maps/%s.rune",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (!f)
		return NULL;

	r = gi.TagMalloc(sizeof(rune_t), TAG_LEVEL);
	if (fread(&r->hdr, sizeof(r->hdr), 1, f) != 1 ||
	    r->hdr.magic != RUNE_MAGIC || r->hdr.version != RUNE_VERSION)
	{
		fclose(f);
		return NULL;
	}
	r->seeds = gi.TagMalloc(sizeof(rune_seed_t) * r->hdr.num_seeds, TAG_LEVEL);
	r->links = gi.TagMalloc(sizeof(rune_link_t) * r->hdr.num_links, TAG_LEVEL);
	if (fread(r->seeds, sizeof(rune_seed_t), r->hdr.num_seeds, f) != (size_t)r->hdr.num_seeds ||
	    fread(r->links, sizeof(rune_link_t), r->hdr.num_links, f) != (size_t)r->hdr.num_links)
	{
		fclose(f);
		return NULL;
	}
	fclose(f);

	/* per-seed link chains */
	r->first_link = gi.TagMalloc(sizeof(int) * r->hdr.num_seeds, TAG_LEVEL);
	r->next_link = gi.TagMalloc(sizeof(int) * r->hdr.num_links, TAG_LEVEL);
	for (i = 0; i < r->hdr.num_seeds; i++)
		r->first_link[i] = -1;
	for (i = r->hdr.num_links - 1; i >= 0; i--)
	{
		r->next_link[i] = r->first_link[r->links[i].from];
		r->first_link[r->links[i].from] = i;
	}

	/*
	 * The human sidecar (<map>.hmn, humanbake.py): one traffic tier per
	 * link, log-scaled 0-255, cut from 59 hours of recorded human play.
	 * Optional -- a missing or mismatched sidecar leaves the array NULL
	 * and every consumer prices as before.
	 */
	sg_human_use = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.hmn",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		int hh[4];

		if (fread(hh, sizeof(int), 4, f) == 4 &&
		    hh[0] == 0x484D4E31 && hh[2] == r->hdr.num_links)
		{
			sg_human_use = gi.TagMalloc(r->hdr.num_links, TAG_LEVEL);
			if (fread(sg_human_use, 1, r->hdr.num_links, f) !=
			    (size_t)r->hdr.num_links)
				sg_human_use = NULL;
			else
				gi.dprintf("rune: human prior loaded (%s)\n", path);
		}
		fclose(f);
	}
	sg_human_live = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.hml",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		int hh[4];

		if (fread(hh, sizeof(int), 4, f) == 4 &&
		    hh[0] == 0x484D4C31 && hh[2] == r->hdr.num_links)
		{
			sg_human_live = gi.TagMalloc(r->hdr.num_links, TAG_LEVEL);
			if (fread(sg_human_live, 1, r->hdr.num_links, f) !=
			    (size_t)r->hdr.num_links)
				sg_human_live = NULL;
			else
				gi.dprintf("rune: flag-live prior loaded (%s)\n", path);
		}
		fclose(f);
	}
	sg_human_escape = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.hme",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		int hh[4];

		if (fread(hh, sizeof(int), 4, f) == 4 &&
		    hh[0] == 0x484D4531 && hh[2] == r->hdr.num_links)
		{
			sg_human_escape = gi.TagMalloc(r->hdr.num_links, TAG_LEVEL);
			if (fread(sg_human_escape, 1, r->hdr.num_links, f) !=
			    (size_t)r->hdr.num_links)
				sg_human_escape = NULL;
			else
				gi.dprintf("rune: escape prior loaded (%s)\n", path);
		}
		fclose(f);
	}
	sg_def_post[0] = sg_def_post[1] = NULL;
	sg_def_icept[0] = sg_def_icept[1] = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.dpo",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		int hh[4];

		/* four per-seed planes: post tier red/blue, intercept tier
		 * red/blue; validates on seed count, not links */
		if (fread(hh, sizeof(int), 4, f) == 4 &&
		    hh[0] == 0x314F5044 && hh[2] == r->hdr.num_seeds)
		{
			int ns = r->hdr.num_seeds, k, ok = 1;
			unsigned char *planes[4];

			for (k = 0; k < 4; k++)
			{
				planes[k] = gi.TagMalloc(ns, TAG_LEVEL);
				if (fread(planes[k], 1, ns, f) != (size_t)ns)
					ok = 0;
			}
			if (ok)
			{
				sg_def_post[0] = planes[0];
				sg_def_post[1] = planes[1];
				sg_def_icept[0] = planes[2];
				sg_def_icept[1] = planes[3];
				gi.dprintf("rune: defense prior loaded (%s)\n", path);
			}
		}
		fclose(f);
	}
	Escape_Load(mapname);
	return r;
}

int Rune_NearestSeed(rune_t *r, vec3_t p)
{
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		vec3_t d;
		float dd;

		VectorSubtract(r->seeds[i].origin, p, d);
		if (d[2] > 96.0f || d[2] < -96.0f)
			continue;
		dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] * 0.25f;
		if (dd < bestd)
		{
			bestd = dd;
			best = i;
		}
	}
	return best;
}

/* --------------------------------------------------------------- fields */

/*
 * THE WAY TO AIR (waves 415-419 forensics). The gurgle override kicked
 * straight up, and straight up is exactly wrong under the smap05 shelf
 * overhang: a drowning bot pinned itself to a ceiling at spd=0 with its
 * nose pointed at rock until "sank like a rock". Air is a GRAPH question
 * -- so answer it once per map: a multi-source relaxation from every dry
 * seed backward through swimmable links gives each water seed its next
 * hop toward breathable surface. The override then swims the actual way
 * out, overhangs and all. NULL on maps with no water; -1 for a water
 * seed with no path (a sealed pool -- then straight up remains the only
 * prayer and the old behavior stands).
 */
int	*sg_airnext;

static void Air_Build(void)
{
	int i, li, changed, passes = 0;
	int n = sg_rune->hdr.num_seeds;
	int *dist;

	sg_airnext = gi.TagMalloc(sizeof(int) * n, TAG_LEVEL);
	dist = gi.TagMalloc(sizeof(int) * n, TAG_LEVEL);
	for (i = 0; i < n; i++)
	{
		sg_airnext[i] = -1;
		dist[i] = (sg_rune->seeds[i].flags & RSF_WATER) ? 0x7fffff : 0;
	}
	/*
	 * THE SHELF BREATHES (waves 424-429: bimodal massacres on the same
	 * binary -- the basin's boundary to DRY land is hook-only, so the
	 * dry-seeds-only target set left every cistern seed at -1 and the
	 * fallback pitch-up died under the overhang exactly as before). A
	 * water seed with any link at all to a dry seed is standing where a
	 * head clears the surface: wading depth. Those are air too.
	 */
	for (li = 0; li < sg_rune->hdr.num_links; li++)
	{
		rune_link_t *l = &sg_rune->links[li];

		if ((sg_rune->seeds[l->from].flags & RSF_WATER) &&
		    !(sg_rune->seeds[l->to].flags & RSF_WATER))
			dist[l->from] = 0;
	}
	do
	{
		changed = 0;
		for (li = 0; li < sg_rune->hdr.num_links; li++)
		{
			rune_link_t *l = &sg_rune->links[li];

			if (l->action != RL_SWIM && l->action != RL_RUN &&
			    l->action != RL_JUMP)
				continue;
			if (!(sg_rune->seeds[l->from].flags & RSF_WATER))
				continue;
			if (dist[l->to] < 0x7fffff &&
			    dist[l->from] > dist[l->to] + 1)
			{
				dist[l->from] = dist[l->to] + 1;
				sg_airnext[l->from] = l->to;
				changed = 1;
			}
		}
	} while (changed && ++passes < 64);
	gi.TagFree(dist);
}

qboolean SG_LevelSetup(void)
{
	if (sg_rune && Q_stricmp(sg_rune_map, level.mapname) == 0)
		return true;

	/* once per map, ahead of the rune: a map with no rune still answers
	 * `sv sg weights`, and the admin editing the file between maps expects
	 * the next map to be running it */
	Weights_Load();

	sg_rune = Rune_Load(level.mapname);
	if (!sg_rune)
	{
		gi.dprintf("slipgate: no rune for %s -- run 'sv rune' first\n",
		           level.mapname);
		return false;
	}
	Air_Build();        /* every water seed learns its way to air */
	Danger_Load();      /* what past matches taught about this map */
	/* Com_sprintf is the tree's own bounded copy (q_shared.c) and always
	 * terminates; strncpy at sizeof-1 does not, which is what -Wall's
	 * stringop-truncation was reporting here. Same call Rune_Load uses. */
	Com_sprintf(sg_rune_map, sizeof(sg_rune_map), "%s", level.mapname);

	if (!Fields_Setup(sg_rune))
	{
		gi.dprintf("slipgate: field setup failed (no flags?)\n");
		sg_rune = NULL;
		sg_human_use = NULL;
		return false;
	}
	Caco_Reset();

	gi.dprintf("slipgate: rune %s, %d seeds, %d links, all fields up\n",
	           sg_rune->hdr.mapname, sg_rune->hdr.num_seeds,
	           sg_rune->hdr.num_links);
	return true;
}

/* ----------------------------------------------------------------- body */



/* the role whose surface is being evaluated this frame -- SLIPGATE runs
 * its bots strictly serially, so a file-static carries it into the
 * detour arithmetic without widening every signature on the path */
int sg_cur_role;


/*
 * Intercept micro-positioning: being ON the carrier's escape line is the
 * naive hold -- it closes at rope speed and blocks your own team's shots.
 * The right ground sits ACROSS the motion: off the axis (the carrier
 * crosses the view laterally instead of head-on), above it (a missed
 * rocket still splashes the floor, and escape ropes mostly pull UP), and
 * beside a narrow crossing (few links out = a corridor the projection
 * says they must thread, where speed stops helping them). Scored over
 * the projection set's members and their link-neighbors; the axis is the
 * set's own deepest-to-shallowest line. Falls back to the projected seed
 * itself when the set is degenerate.
 */
static int Intercept_HoldSeed(int team, int fallback)
{
	sg_proj_t *pr = &sg_caco_proj[team - 1];
	vec3_t axis;
	float axlen, bestscore = -1.0f;
	int i, best = -1;

	if (pr->n < 2 || pr->client < 0)
		return fallback;

	VectorSubtract(sg_rune->seeds[pr->seed[0]].origin,
	               sg_rune->seeds[pr->seed[pr->n - 1]].origin, axis);
	axis[2] = 0.0f;
	axlen = VectorLength(axis);
	if (axlen < 64.0f)
		return fallback;            /* no meaningful motion to be across */
	axis[0] /= axlen; axis[1] /= axlen;

	for (i = 0; i < pr->n; i++)
	{
		int p = pr->seed[i], li, fan = 0;
		float choke;

		if (p < 0 || p >= sg_rune->hdr.num_seeds)
			continue;
		for (li = sg_rune->first_link[p]; li >= 0; li = sg_rune->next_link[li])
			fan++;
		choke = 600.0f / (4.0f + (float)fan);

		for (li = sg_rune->first_link[p]; li >= 0; li = sg_rune->next_link[li])
		{
			int c = sg_rune->links[li].to;
			vec3_t off;
			float lat, dz, score;

			VectorSubtract(sg_rune->seeds[c].origin,
			               sg_rune->seeds[p].origin, off);
			dz = off[2];
			off[2] = 0.0f;
			/* perpendicular component of the offset against the axis */
			lat = fabsf(off[0] * axis[1] - off[1] * axis[0]);
			if (lat > 250.0f)
				lat = 250.0f;
			score = lat + choke;
			if (dz > 0.0f)
				score += (dz > 200.0f) ? 200.0f : dz;
			if (score > bestscore)
			{
				bestscore = score;
				best = c;
			}
		}
	}
	return (best >= 0) ? best : fallback;
}


/*
 * (d) THE DETOUR BUDGET for the mega, Worth_Quad's own arithmetic applied to
 * the mega's own fields.
 *
 * The triangle is the one Detour_Value evaluates for the per-item classes and
 * it is exact here for the same reason: to_mega[k] was flooded FROM pad k, so
 * it reads as cost from anywhere TO that pad, and the far leg is the goal field
 * sampled at the pad's own seed.
 *
 *     detour = cost_to_pad + pad_to_goal - direct
 *     value  = worth / (1 + max(0, detour) / 1500)
 *
 * plus one thing the class arithmetic does not have: a HARD ceiling. The decay
 * alone never quite reaches zero, and a mega on the far side of the map would
 * still tug a little at every seed forever. Four seconds of extra road is the
 * bound -- past that the bot is not detouring for the mega, it is going to the
 * mega and calling the flag a detour, which is the failure mode this whole
 * feature has to not have.
 */
#define SG_MEGA_PATIENCE	12.0f   /* seconds an offer may stand unspent */
#define SG_MEGA_BACKOFF		20.0f   /* ...and the refusal after, the pad's own
                                     * respawn (SetRespawn 20, g_items.c:596) */


/*
 * Role assignment: the owner's quota, then the two situational roles.
 *
 * Two in five defend (nearest-rounded), carrier counts toward defence, a side
 * of one attacks. Rank is slot order among SLIPGATE bots of the team, so the
 * assignment is stable frame to frame.
 *
 * Precedence, in order:
 *
 *   CARRY     I have the flag. Nothing else applies.
 *   DEFEND    my rank falls inside the quota -- the post is kept whatever
 *             else is happening; the situational roles are drawn from the
 *             attacking share only, which is what "attackers convert" means.
 *   RECOVER   our own flag is astray. EVERY attacker converts: getting it
 *             back outranks escorting, so no escort is named this frame.
 *   ESCORT    we have a live carrier who is not me: exactly one attacker,
 *             the lowest-ranked one that is not the carrier itself.
 *   ATTACK    everyone else.
 */
static sg_role_t SG_Role(sg_bot_t *bot, qboolean carrying)
{
	int team = bot->ent->client->ctf.teamnum;
	int size = 0, defenders_wanted, my_rank = 0, i;
	int my_client = (int)(bot->ent - g_edicts) - 1;
	sg_belief_carrier_t *own = &sg_caco_team_belief.carrier[team - 1];

	if (carrying)
		return SG_ROLE_CARRY;

	/*
	 * A standing order from a HUMAN teammate replaces the quota: the
	 * player said "defend" and defending is what happens, for the order's
	 * lifetime (sg_chat.c owns expiry: 90s, disconnect, team change).
	 * Only the flag outranks a human -- a carrier carries.
	 */
	{
		int forced = SG_ChatOrderedRole(bot->ent);

		if (forced >= 0 &&
		    (forced != SG_ROLE_ESCORT || SG_ChatEscortTarget(bot->ent)))
			return (sg_role_t)forced;
	}

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->inuse)
			continue;
		if (sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		if (&sg_bots[i] == bot)
			my_rank = size;
		/* where in the ranking our own carrier sits, if it is one of ours */
		size++;
	}

	/*
	 * STRATEGY BY GAME STATE, the way the demos play it. Four states from
	 * two common-knowledge bits (each flag home or astray -- the HUD tells
	 * everyone), and each state names its shape. The old static 2-in-5
	 * quota played every state identically; games are won by playing them
	 * differently.
	 *
	 *   both home        2 DEFEND, rest ATTACK. The base shape.
	 *   theirs astray    2 DEFEND (the return-kill wave is coming),
	 *   (ours home)      1 ESCORT walks the carrier in, rest ATTACK their
	 *                    base -- they cannot score while we hold theirs,
	 *                    and the next steal queues behind this capture.
	 *   ours astray      1 DEFEND holds the stand for the return; the
	 *   (theirs home)    rest RECOVER. An empty stand needs a watchman,
	 *                    not a garrison.
	 *   both astray      the decisive state. 1 DEFEND stand-watch,
	 *                    1 ESCORT keeps our carrier alive, rest RECOVER --
	 *                    the standoff breaks on exactly one event, their
	 *                    carrier's death, and ours must survive to convert
	 *                    it.
	 */
	{
		qboolean ours_astray =
		    (sg_caco_team_belief.flag[team - 1].state == SG_FLAG_ASTRAY);
		qboolean theirs_astray =
		    (sg_caco_team_belief.flag[2 - team].state == SG_FLAG_ASTRAY);
		qboolean have_carrier = (own->client >= 0 &&
		                         sg_fields.our_carrier_valid[team - 1]);

		defenders_wanted = ours_astray ? 1 : 2;

		/*
		 * TEAM SKEW (sg_teamskew, rung-4 tell #2: team-mirror symmetry).
		 * All three set-#1 judges read "identical AI on both sides" off
		 * the sheets: balanced escort means, alternating presses,
		 * role-locked plateaus that never rotate. Real pub teams are
		 * lopsided and DRIFT -- one side runs attack-heavy for a few
		 * minutes, then reshuffles. Each team rolls a defender-count
		 * skew of -1, 0, or +1 that rerolls every ~3 minutes on
		 * independent per-team clocks, so the two sides' role mixes
		 * decorrelate and wander the way two unrelated rosters do.
		 * The existing states below still own the astray cases.
		 */
		if (sg_cv.teamskew->value > 0.0f && size >= 4)
		{
			static float ts_until[2];
			static int ts_skew[2];
			int ts = (team == CTF_TEAM_RED) ? 0 : 1;

			if (level.time >= ts_until[ts])
			{
				ts_skew[ts] = (rand() % 3) - 1;
				ts_until[ts] = level.time + 150.0f +
				               (float)(rand() % 90);
			}
			defenders_wanted += ts_skew[ts];
			if (defenders_wanted < 0)
				defenders_wanted = 0;
			if (defenders_wanted > size - 1)
				defenders_wanted = size - 1;
		}

		if (size <= 1)
			defenders_wanted = 0;
		else if (size == 2)
		{
			/*
			 * DUEL ROLES (sg_duelroles, wave 285+). The hardcoded 1
			 * was a catch-22 the 268-283 census convicted: dw stuck
			 * at 1 in 131/138 transitions, zero duel caps in 16
			 * waves, while the statue defender sat p90=173u from
			 * its post -- 2v2 could never push together because
			 * dw=0 required already holding the flag. Under the
			 * flag, duel teams run the same state machine as
			 * everyone: theirs-astray pushes BOTH bots (they cannot
			 * score while we hold theirs), ours-astray keeps the
			 * watchman. Off, the old pin stands.
			 */
			if (sg_cv.duelroles->value)
				defenders_wanted = theirs_astray ? 0 : 1;
			else
				defenders_wanted = 1;
		}
		/* a live carrier on our side counts toward the defensive share */
		if (own->client >= 0 && !ours_astray)
			defenders_wanted--;
		if (defenders_wanted < 0)
			defenders_wanted = 0;

		/*
		 * CLOCKPLAY (sg_clockplay). The score and the clock, finally in
		 * the quota. It lands here, last, on the FINAL number: the flag
		 * states above decide the shape of the fight and the scoreline
		 * only leans it, so a late lead adds its body to whatever the
		 * state already asked for rather than replacing it.
		 *
		 * One body, never more -- a lean, not a formation change -- and
		 * the team keeps at least one attacker whatever the lead is,
		 * because a side with nobody in the enemy base cannot end the
		 * game, only survive it, and surviving runs out at zero.
		 */
		{
			int shift = Clock_DefendShift(team);

			if (shift)
			{
				defenders_wanted += shift;
				if (defenders_wanted < 0)
					defenders_wanted = 0;
				if (size > 1 && defenders_wanted > size - 1)
					defenders_wanted = size - 1;
				else if (size <= 1)
					defenders_wanted = 0;
			}
		}

		/* role-flap diagnostic: two bots alternated DEFEND/ATTACK every
		 * frame of it18 (600 flips/600 samples) -- print the decision
		 * inputs on each change so the oscillating input names itself */
		if (sg_cv.debug->value)
		{
			static int last_dw[SG_MAXBOTS], last_oc[SG_MAXBOTS];
			int me = (int)(bot - sg_bots);

			if (me >= 0 && me < SG_MAXBOTS &&
			    (last_dw[me] != defenders_wanted ||
			     last_oc[me] != own->client))
			{
				gi.dprintf("ROLEIN %s dw=%d rank=%d own=%d astray=%d size=%d\n",
				           bot->ent->client->pers.netname,
				           defenders_wanted, my_rank, own->client,
				           (int)ours_astray, size);
				last_dw[me] = defenders_wanted;
				last_oc[me] = own->client;
			}
		}

		if (my_rank < defenders_wanted)
		{
			/* the FIRST defender is the statue on the stand; a second
			 * is the patrol -- it never pins, so the surface walks it
			 * around the base picking up armor and covering approaches */
			bot->def_stand = (my_rank == 0);
			return SG_ROLE_DEFEND;
		}

		/*
		 * One escort whenever we have a live carrier that is not me --
		 * and the escort is the NEAREST eligible body, not a rank slot.
		 * The rank-slot version handed the job to whoever sat at a fixed
		 * position in the scan order: dead, respawning, or across the
		 * map. The waves 71-72 census reads accordingly -- no escort at
		 * all for 30-100% of carry seconds, median distance 430-1860
		 * when one existed, and mactf06's entire 28-second carry walked
		 * naked. Every bot runs the same argmin over the same shared
		 * positions; the incumbent gets a 300-unit head start so the
		 * job does not flap between two equidistant mates.
		 */
		if (have_carrier && own->client != my_client)
		{
			edict_t *car_ent = g_edicts + own->client + 1;
			float bestd = 1e30f;
			int best_i = -1, rank_i = 0, k;

			for (k = 0; k < SG_MAXBOTS; k++)
			{
				vec3_t ed;
				float dd;

				if (!sg_bots[k].active || !sg_bots[k].ent ||
				    !sg_bots[k].ent->inuse)
					continue;
				if (sg_bots[k].ent->client->ctf.teamnum != team)
					continue;
				if (rank_i++ < defenders_wanted)
					continue;       /* defenders keep the base */
				if ((int)(sg_bots[k].ent - g_edicts) - 1 == own->client)
					continue;       /* the carrier escorts nobody */
				if (sg_bots[k].ent->deadflag)
					continue;
				VectorSubtract(sg_bots[k].ent->s.origin,
				               car_ent->s.origin, ed);
				dd = VectorLength(ed);
				if (sg_bots[k].last_role == (int)SG_ROLE_ESCORT)
					dd -= 300.0f;
				if (dd < bestd)
				{
					bestd = dd;
					best_i = k;
				}
			}
			if (best_i >= 0 && &sg_bots[best_i] == bot)
			{
				/*
				 * ESCORT DOSE (sg_escortdose, rung-4 set #1 tell #1,
				 * named by all three judges on every bot sheet): the
				 * fleet escorts EVERY carry at 0.33-0.75 escort
				 * fraction while pub humans run flags alone (0.02-
				 * 0.32, "classic lone-wolf hero run"). The machinery
				 * out-organizes the population it imitates. The dose
				 * is the percent of carries that get an escort AT
				 * ALL; the roll happens once per carry (rerolled when
				 * the carrier changes) so a carry is escorted or
				 * abandoned for its whole life, like a pub decides.
				 */
				static int esc_carrier[2] = { -1, -1 };
				static qboolean esc_on[2] = { true, true };
				int et = (team == CTF_TEAM_RED) ? 0 : 1;
				int cc = own->client;

				if (esc_carrier[et] != cc)
				{
					esc_carrier[et] = cc;
					esc_on[et] = ((rand() % 100) <
					    (int)sg_cv.escortdose->value);
				}
				if (esc_on[et])
					return SG_ROLE_ESCORT;
			}
		}

		if (ours_astray)
			return SG_ROLE_RECOVER;
		(void)theirs_astray;    /* shape only differs via the states above */
		return SG_ROLE_ATTACK;
	}
}




/*
 * The chain's shape, all of it fitted rather than derived -- the ANGLE is
 * the engine's and is computed above; these are the preferences around it.
 *
 * SG_AS_PERIOD     seconds per full swing. A hop off 270 up under 800
 *                  gravity is airborne 0.675s, so one period is two hops:
 *                  one shoulder per flight, which is the cadence a player
 *                  swaps strafe keys on.
 * SG_AS_VIEWSHARE  how much of the angle the VIEW carries; the input
 *                  carries the rest, exactly as forward+strafe does.
 * SG_AS_VIEWMAX    and the ceiling on that, in degrees. At the period
 *                  above this peaks near 150 deg/s of view movement --
 *                  inside sg_turnrate's 600 and inside a human wrist.
 * SG_AS_CORR       heading error, in degrees, that saturates the bias on
 *                  the swing. The bias is what keeps the mean of the S on
 *                  the road instead of walking it off one shoulder.
 * SG_AS_ABORT      heading error that ends the chain outright: past this
 *                  the body needs to turn, not to harvest.
 * SG_AS_RUN        straight road a chain wants before it commits, units.
 * SG_AS_HOLD       and the fraction of that a chain already running is
 *                  held to, so the road test cannot chatter a chain to
 *                  death across its own bar.
 * SG_AS_FLOOR      2D speed under which there is nothing to chain.
 * SG_AS_FLAGKEEP   never this close to either stand: speed is for TRAVEL.
 * SG_AS_MINCHAIN   a chain shorter than this is not worth a log line.
 */


/*
 * One physics step of movement, decided from where the bot actually is.
 *
 * The caller has already put the plain command in place -- forward down the
 * view, no strafe -- so every early return here leaves honest, unaltered
 * running. Three things can be added to it:
 *
 *   ground strafe   accel 10, the strong half of this engine
 *   air strafe      accel 1, the same derivation, only A changes
 *   landing jump    Pmove runs PM_CheckJump before PM_Friction, and a jump
 *                   clears groundentity -- which is the condition PM_Friction
 *                   tests before applying any ground friction at all. A jump
 *                   issued on the step the bot touches down therefore pays no
 *                   friction; one issued a step late pays speed * 6 * ft.
 *                   That single step is the whole of bunny hopping here.
 */


/*
 * Is there a straight, clear road ahead worth committing a hop chain to?
 *
 * The same walk the pursuit point makes -- plain RUN links, no rounding
 * anchors, strictly down the field -- collected into a chain, and then two
 * questions asked of the point `want` units of ARC down it:
 *
 *   the chord      how far that point actually is in a straight line. The
 *                  seed centers are beads on a road and the polyline
 *                  through them zigzags even where the road does not (the
 *                  pursuit census: 40 deg/s of churn on geometrically
 *                  straight chain), so leg-by-leg bend angles measure the
 *                  beads, not the road. Chord over arc does not: a road
 *                  that goes somewhere gives back most of what was walked.
 *   the room       the fan's own player-box trace, run to that point. A
 *                  chain in the air cannot dodge, so the corridor has to
 *                  be there before the first hop, not discovered on the
 *                  third.
 *
 * SG_AS_CHORD is the fraction of the arc the chord has to keep, and the
 * chord also has to point within SG_AS_BEND of the heading the body is
 * actually steering on -- a road that doubles back scores well on chord
 * alone.
 */




/*
 * The duel, priced onto the same surface as everything else.
 *
 * Dueling is not a mode the body enters; it is two more terms in the sum, in
 * the same milliseconds every other term is denominated in (Surface_At). What
 * combat supplies is the target's believed position, the range the weapon in
 * hand wants, and what being seen costs right now (SG_CombatDuel, sg_combat.h).
 *
 * SG_DUEL_RANGE_MS   value per unit of range error. Half the carrier's own
 *                    threat repulsion above, which prices a step toward a
 *                    believed contact at 3.0 per unit: a carrier being caught
 *                    loses the match, a fighter standing a hundred units off
 *                    its best range loses some damage. The relation between
 *                    the two is the claim; the absolute is fitted.
 * SG_DUEL_COVER_MS   value for standing where the target can see you, scaled
 *                    by exposure. At exposure 1 it is 900 ms -- comparable to
 *                    the 1500 ms scale the detour arithmetic uses for an item
 *                    worth taking, so cover competes with a pickup and loses
 *                    to the objective. Fitted.
 */


/*
 * The lateral weave. Period per bot so a squad does not oscillate in phase and
 * present one wide target; the spread is 0.4 to 0.85 s, which is fast enough
 * that a 650 u/s rocket aimed where the bot was arrives where it is not, and
 * slow enough that ground friction is not eating the whole reversal. 300 is
 * pm_maxspeed's own wishspeed clamp (the strafe work above uses 400 pre-clamp
 * for direction only; here the magnitude is the point). All three fitted.
 */



/*
 * Has anything landed on this body since `since`? The damage ring
 * (sg_caco.c, four entries per client) already books every hit T_Damage
 * delivers, seen shooter or not, so the spawn beat needs no sense of its
 * own: the question "did the world just object" is exactly the one the ring
 * was built to answer, and it answers it for splash and falls and the rail
 * from a room away alike.
 */
qboolean Beat_HurtSince(edict_t *e, float since)
{
	int ci, k;

	if (!e || !e->client)
		return false;
	ci = (int)(e->client - game.clients);
	if (ci < 0 || ci >= SG_DMG_CLIENTS)
		return false;
	for (k = 0; k < SG_DMG_RING; k++)
		if (sg_caco_damage[ci][k].attacker >= 0 &&
		    sg_caco_damage[ci][k].time > since)
			return true;
	return false;
}

/* ----------------------------------------------------------------- the mega
 *
 * (c) THE ROLE GATE. The state half of the price lives with combat
 * (SG_WorthMega); this is the half that knows what the bot is FOR, and every
 * branch of it errs the same way -- toward not detouring. A mega taken is
 * worth 100 points of margin; a mega taken by the wrong bot at the wrong
 * moment is a flag, and those do not trade evenly.
 *
 *   CARRY    never. The carrier's job is the ground between here and home and
 *            nothing else; legcarrier dose 3 already says a healthy carrier
 *            does not shop, and this says the hurt one does not either -- a
 *            carrier stopping for 100 hp is a carrier standing still on a pad
 *            the enemy knows the location of.
 *   ESCORT   never, and for the same reason from the other side: the escort
 *            role exists only while there IS a live carrier of ours, so an
 *            escort on an errand is a carrier without a screen.
 *   RECOVER  never. Our flag is astray; there is no lull to spend.
 *   ATTACK   yes, except under the conductor's downbeat -- the push is a bar
 *            the team steps off on together and detours wait for the next one.
 *            "Pre-push" is exactly what the un-armed window is.
 *   DEFEND   yes on a lull, which is the same six seconds of no believed
 *            contact the pad wait already asks for. A defender who can hear
 *            somebody is a defender at the stand.
 *
 * And nobody in a fight: a bot with a live duel has a better use for the next
 * four seconds than a walk.
 */
static float Mega_Worth(sg_bot_t *bot, edict_t *e, sg_role_t role)
{
	int team = e->client->ctf.teamnum;

	if (!SG_MegaOn() || sg_fields.mega_count <= 0)
		return 0.0f;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return 0.0f;

	if (role == SG_ROLE_CARRY || role == SG_ROLE_ESCORT ||
	    role == SG_ROLE_RECOVER)
		return 0.0f;
	if (role == SG_ROLE_ATTACK &&
	    level.time < sg_push_until[team - CTF_TEAM_RED])
		return 0.0f;
	if (role == SG_ROLE_DEFEND)
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			if (sg_caco_enemies[team - 1][s].client >= 0 &&
			    level.time - sg_caco_enemies[team - 1][s].seen_time < 6.0f)
				return 0.0f;    /* not a lull */
	}
	if (bot->engaged_last || SG_CombatDuel(e, NULL, NULL, NULL))
		return 0.0f;

	return SG_WorthMega(e);
}

/*
 * THE CORPSE FRAME (split from SG_BotThink, 2026-08-11 standards pass;
 * body verbatim). Everything a dead bot owes the world: teach the danger
 * and tilt ledgers once, drop every live claim, pulse the respawn
 * button. Returns true when this frame belonged to a corpse and the
 * think ends with it.
 */
static qboolean Think_Dead(sg_bot_t *bot, edict_t *e, usercmd_t *cmd)
{
	if (!e->deadflag)
		return false;

	/* my own death, at my own seed: the most honest sighting there
	 * is, and the danger dimension's only teacher */
	if (bot->seed >= 0 && !bot->death_taught)
	{
		Danger_Learn(e->client->ctf.teamnum, bot->seed);
		Tilt_Note(e, bot);      /* the same death, remembered personally */
		bot->death_taught = true;
	}
	bot->seed = -1;
	bot->view_on = false;   /* respawn snaps the view fresh */
	bot->railhold_since = 0.0f;     /* a corpse is not waiting on a lane */
	bot->railhold_enemy = -1;
	/* a chain that ended in a death ended; the frame that would have
	 * closed it never ran, and a stale start would date the next one */
	bot->as_since = 0.0f;
	bot->as_phase = 0.0f;
	/* a corpse is not standing anybody's pad: release the lease early
	 * rather than making the next claimant wait it out */
	Lead_Abort(bot, "died");
	/* no reading, so the respawn's jump to 100 is not a pickup; the
	 * back-off dies with the life that earned it */
	bot->mega_on = false;
	bot->mega_hp = 0;
	bot->mega_since = 0.0f;
	bot->mega_next = 0.0f;
	bot->beat_ready = true; /* dead HERE, on this level: the spawn beat
	                         * has something to be the far side of */
	/*
	 * PULSE the trigger, never hold it. Respawn keys off
	 * latched_buttons -- fresh presses only (p_client.c:3203) -- and
	 * a button held from the first dead frame latches exactly once,
	 * before respawn_time has elapsed, then never again: the corpse
	 * waits forever for a press that cannot re-arrive. Observed live
	 * the moment a human watched a body instead of a stat line.
	 * Toggling at 5Hz lands a fresh latch every other frame.
	 */
	cmd->buttons = (((int)(level.time * 10.0f)) & 2)
	              ? BUTTON_ATTACK : 0;
	ClientThink(e, cmd);
	return true;
}

/*
 * THE RESPAWN EDGE (same split, body verbatim): the first live frame
 * after a death, where the tilt clocks start -- a window started on the
 * corpse would spend a second and a half of itself lying on the floor.
 */
static void Think_RespawnEdge(sg_bot_t *bot, edict_t *e)
{
	if (bot->death_taught && sg_cv.tilt->value > 0.0f)
	{
		/* the caution runs shorter for the better shooter: the same
		 * span the threat clock uses, and for the same reason -- the
		 * skill-4 bot gets his composure back first. Skill is read
		 * through combat's own accessor (0..400) so there is exactly
		 * one skill model in this tree. */
		float sk = (float)SG_CombatSkill(e) / 400.0f;   /* 0..1 */

		if (sk < 0.0f) sk = 0.0f;
		if (sk > 1.0f) sk = 1.0f;
		bot->tilt_until = level.time + bot->tilt_window;
		bot->tilt_caution_until = level.time + SG_TILT_CAUTION +
		    (SG_TILT_CAUTION4 - SG_TILT_CAUTION) * sk;
	}
}

/*
 * THE APPROACH BAND (split from SG_BotThink, 2026-08-11 standards pass;
 * body verbatim): the rally, the broadcast surge, and the flying cook --
 * everything an attacker decides between two and five seconds out.
 * Returns whether the bot holds its ground waiting on a partner.
 */
static qboolean Think_ApproachBand(sg_bot_t *bot, edict_t *e,
                                   sg_role_t role, int team,
                                   const int *goal_field)
{
	qboolean hold = false;

	/*
	 * THE RALLY. Wave 61's arrival census: three quarters of all attacks
	 * reach the enemy base ALONE -- one body against three or more armed
	 * defenders at the stand, dead every time, which is why floors sit
	 * under 300 while steals stay near one a wave. An attacker in the
	 * approach band (2-5s of field) with no partner inside 6s and at
	 * least two enemies believed alive holds its ground -- twelve
	 * seconds at most, gone the moment a mate closes or the wait times
	 * out. Solo pushes still happen; they just stop being the ONLY kind.
	 */
	/*
	 * THE CONDUCTOR (sg_wavepush, A/B wave 198+). The rally waits for
	 * partners; the conductor makes partners exist. Once every 40
	 * seconds, when three or more attackers are alive and the nearest
	 * is within striking range, the team calls a downbeat: a 12-second
	 * window in which every rally releases at once and item detours
	 * stop pulling attackers sideways. Arrivals stack into a wave --
	 * the census's 75-percent-alone number is the target -- and the
	 * respawn-surge rule still cancels every wait it ever cancelled.
	 */
	/*
	 * v2, THE BROADCAST SURGE. v1's metronome read negative (steals
	 * 1.3 vs 2.2 pooled 198-200): a downbeat on a clock suppresses the
	 * organic rally pairing and marches under-armed waves into rooms
	 * that were never thin. The surge rule was always the true clock --
	 * a defender dead near their own stand IS the window -- but it
	 * released only the one attacker who happened to be in the band.
	 * Now the kill rings the whole team's bell: every rally releases
	 * into the same respawn-wide window, detours pause only during the
	 * eight seconds the window is actually open.
	 */
	if (sg_cv.wavepush->value &&
	    role == SG_ROLE_ATTACK &&
	    level.time >= sg_push_until[team - CTF_TEAM_RED])
	{
		int et9 = (team == CTF_TEAM_RED) ? 1 : 0;
		edict_t *ef9 = G_Find(NULL, FOFS(classname),
		                      (team == CTF_TEAM_RED) ? "info_flag_blue"
		                                             : "info_flag_red");

		if (ef9 && level.time - sg_caco_death_time[et9] < 2.0f)
		{
			vec3_t dp9;

			VectorSubtract(sg_caco_death_org[et9], ef9->s.origin, dp9);
			if (VectorLength(dp9) < 1200.0f)
			{
				sg_push_until[team - CTF_TEAM_RED] = level.time + 8.0f;
				if (sg_cv.debug->value)
					gi.dprintf("PUSH team=%d surge\n", team);
			}
		}
	}

	if (role == SG_ROLE_ATTACK && bot->seed >= 0 &&
	    goal_field[bot->seed] > 2000 && goal_field[bot->seed] < 8000 &&
	    goal_field[bot->seed] < SG_FIELD_INF &&
	    level.time < sg_push_until[team - CTF_TEAM_RED])
	{
		/* the bell rang: no waiting, no rally, run the window */
		bot->rally_since = 0.0f;
		goto rally_done;
	}

	if (role == SG_ROLE_ATTACK && bot->seed >= 0 &&
	    goal_field[bot->seed] > 2000 && goal_field[bot->seed] < 5000 &&
	    goal_field[bot->seed] < SG_FIELD_INF)
	{
		int bi, mates_near = 0, mates_coming = 0;

		/*
		 * First cut waited only when two enemies were freshly SEEN and
		 * gave up after 12s -- but an attacker sneaking in alone has
		 * usually seen nobody, and a trailing mate 8-12s of field back
		 * cannot close inside the cap: wave 63 paired almost nothing.
		 * The census already proved solo arrival means death against
		 * ANY defense, so the belief gate is gone. Wait exactly when a
		 * partner is genuinely en route (inside 14s of field), as long
		 * as it takes them to close -- capped at 20s -- and push solo
		 * without ceremony when nobody is coming at all.
		 */
		for (bi = 0; bi < SG_MAXBOTS; bi++)
		{
			sg_bot_t *mb = &sg_bots[bi];

			if (!mb->active || mb == bot || !mb->ent || !mb->ent->inuse)
				continue;
			if (mb->ent->client->ctf.teamnum != team)
				continue;
			if (mb->last_role != (int)SG_ROLE_ATTACK ||
			    mb->last_goalcost < 0)
				continue;
			if (mb->last_goalcost < 6000)
				mates_near++;
			else if (mb->last_goalcost < 20000)
				/* THE APPEAL. The 20s horizon was convicted (1.6 -> 1.0
				 * steals/wave, waves 63-67) and shrunk to a 6s sync --
				 * but that trial ran in the corpse-wait era, when a
				 * 'partner en route' was usually a body that would never
				 * stand up. Bots respawn now; partners genuinely arrive.
				 * Retried at the full horizon on fresh evidence. */
				mates_coming++;
		}
		{
			/*
			 * THE SURGE: a defender dead near their own stand opens a
			 * respawn-wide window, and waves 84-85 show the thief dying
			 * 3-5 seconds after the grab to the respawn stream -- the
			 * window is the only time the room is thin. A fresh enemy
			 * death (< 6s) within 1200 of the enemy flag cancels the
			 * wait: push NOW, paired or not.
			 */
			int et = (team == CTF_TEAM_RED) ? 1 : 0;    /* victim = them */
			edict_t *ef = G_Find(NULL, FOFS(classname),
			                     (team == CTF_TEAM_RED) ? "info_flag_blue"
			                                            : "info_flag_red");

			if (ef && level.time - sg_caco_death_time[et] < 6.0f)
			{
				vec3_t dd2;

				VectorSubtract(sg_caco_death_org[et], ef->s.origin, dd2);
				if (VectorLength(dd2) < 1200.0f)
				{
					bot->rally_since = 0.0f;
					goto rally_done;
				}
			}
		}
		if (mates_near == 0 && mates_coming > 0)
		{
			if (bot->rally_since <= 0.0f)
			{
				int ci2, best_cover = -1;
				float bestd2 = 1e30f;

				bot->rally_since = level.time;
				/*
				 * Wave 65 paired seven pushes on lmctf09 and stole
				 * nothing: the waiter froze wherever the band caught it,
				 * mid-corridor, lit, and the pairing died before it
				 * formed. The rune has measured exposure since the
				 * generator's census pass -- the wait belongs at the
				 * darkest seed within reach.
				 */
				for (ci2 = 0; ci2 < sg_rune->hdr.num_seeds; ci2++)
				{
					vec3_t cd;
					float dsq;

					if (sg_rune->seeds[ci2].area_hint > 60)
						continue;
					VectorSubtract(sg_rune->seeds[ci2].origin,
					               e->s.origin, cd);
					dsq = cd[0] * cd[0] + cd[1] * cd[1]
					    + cd[2] * cd[2] * 4.0f;
					if (dsq < bestd2 && dsq < 800.0f * 800.0f)
					{
						bestd2 = dsq;
						best_cover = ci2;
					}
				}
				bot->rally_cover = best_cover;
				if (sg_cv.debug->value)
					gi.dprintf("RALLY %s waits (%d coming, cover=%d)\n",
					           e->client->pers.netname, mates_coming,
					           best_cover);
			}
			if (level.time - bot->rally_since < 15.0f)
				hold = true;
		}
		else
		{
			if (bot->rally_since > 0.0f &&
			    sg_cv.debug->value)
				gi.dprintf("RALLY %s released after %.1fs (near=%d)\n",
				           e->client->pers.netname,
				           level.time - bot->rally_since, mates_near);
			bot->rally_since = 0.0f;
		}
rally_done:;

		/*
		 * THE FLYING COOK, truly at the band this time (the wave-230
		 * relocation never landed -- its edit died in a chain that
		 * kept going; the ledger is corrected). Every attacker in
		 * the approach band cooks on the run at full volume: the
		 * silent cook keeps the eyes on the route, the stand takes
		 * the throw, failure costs nothing, and the successes
		 * accrue -- sixty-five to five.
		 */
		if (sg_cv.flycook->value &&
		    bot->nade_phase == 0 && level.time >= bot->nade_next)
		{
			static gitem_t *nades9;
			edict_t *nf9;

			if (!nades9)
				nades9 = FindItem("Grenades");
			nf9 = G_Find(NULL, FOFS(classname),
			             (team == CTF_TEAM_RED) ? "info_flag_blue"
			                                    : "info_flag_red");
			if (nades9 && nf9 &&
			    e->client->pers.inventory[ITEM_INDEX(nades9)] > 0)
			{
				/* wave 238: the target book. Clean arcs delivered
				 * bombs to an empty pedestal (237: median pop 588
				 * with clear flights) -- defenders POST 400-600 off
				 * the stand. The danger field already knows where
				 * the deaths happen: aim at the hottest seed within
				 * 600 of their stand, the sentry's actual post. */
				int ns13 = Rune_NearestSeed(sg_rune, nf9->s.origin);
				int s13, best13 = -1, bv13 = 0;

				VectorCopy(nf9->s.origin, bot->nade_at);
				if (ns13 >= 0)
				{
					for (s13 = 0; s13 < sg_rune->hdr.num_seeds &&
					     s13 < SG_MAX_SEEDS; s13++)
					{
						vec3_t dd13;

						VectorSubtract(sg_rune->seeds[s13].origin,
						               nf9->s.origin, dd13);
						if (VectorLength(dd13) > 600.0f)
							continue;
						if (Danger_Field(team)[s13] > bv13)
						{
							bv13 = Danger_Field(team)[s13];
							best13 = s13;
						}
					}
					if (best13 >= 0)
						VectorCopy(sg_rune->seeds[best13].origin,
						           bot->nade_at);
				}
				bot->nade_at[2] += 56.0f;
				nades9->use(e, nades9);
				bot->nade_phase = 1;
				bot->nade_until = level.time + 0.5f;
			}
		}
	}
	else
		bot->rally_since = 0.0f;
	return hold;
}


static int intercept_field[SG_MAX_SEEDS];

/*
 * THE INTERCEPT SURFACE (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): everyone but the carrier supports the carrier,
 * and when an enemy thief is believed live, floods the hold ground
 * across its projected motion.
 */
static void Think_InterceptField(sg_role_t role, int team,
                                 const int **support_out,
                                 const int **intercept_out)
{
	if (role != SG_ROLE_CARRY)
	{
		sg_belief_carrier_t *ec = &sg_caco_team_belief.enemy_carrier[team - 1];

		*support_out = sg_fields.our_carrier[team - 1];
		if (ec->seed >= 0)
		{
			int cost = 0;
			int hold = Intercept_HoldSeed(team, ec->seed);

			/* the hold ground across the thief's projected motion --
			 * or their believed position when the projection is thin */
			Field_Flood(sg_rune, intercept_field, &hold, &cost, 1);
			*intercept_out = intercept_field;
		}
	}
}


/*
 * WHERE AM I ON THE RUNE (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): seed relocation on 48 units of travel, the
 * previous-seed memory the dither reads, and the pit trace.
 */
static void Think_TrackSeed(sg_bot_t *bot, edict_t *e, int team)
{
	vec3_t d;

	/* where am I on the rune? */
	VectorSubtract(e->s.origin, bot->last_origin, d);
	if (bot->seed < 0 || VectorLength(d) > 48.0f)
	{
		int was = bot->seed;

		bot->seed = Rune_NearestSeed(sg_rune, e->s.origin);
		VectorCopy(e->s.origin, bot->last_origin);
		if (was >= 0 && bot->seed != was)
		{
			bot->prev_seed = was;
			bot->prev_seed_time = level.time;
			bot->dither_salt = (unsigned)(rand() & 0x7fffffff);

			/*
			 * PITTRACE (sg_debug): the moment a bot's seed enters the
			 * masked sub-stand region, say who, from where, in what role,
			 * chasing what tactical waypoint. Three flat nulls said the
			 * pit traffic rides neither the waypoint surface nor the
			 * descent steps nor the flag flood -- this line names the
			 * actual carrier of the traffic.
			 */
			if (sg_cv.debug->value && team >= 1 && team <= 2)
			{
				int pti = (team == CTF_TEAM_RED) ? 0 : 1;

				if (sg_fields.shelf_cliff[pti] &&
				    sg_fields.shelf_cliff[pti][bot->seed] > 0 &&
				    !(sg_fields.shelf_cliff[pti][was] > 0))
					gi.dprintf("PITTRACE %s role=%s seed %d->%d z=%.0f "
					           "tac_seed=%d tac_role=%d hook=%d\n",
					           e->client->pers.netname,
					           sg_role_names[bot->last_role],
					           was, bot->seed, e->s.origin[2],
					           bot->tac_seed, bot->tac_role,
					           bot->hook_phase);
			}
		}
	}
}


/*
 * THE CARRY BOOKENDS (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): grab and loss edges -- carry clocks, exit-lane
 * snapshot, escape priors, the unconditional last_role update.
 */
static void Think_CarryBookends(sg_bot_t *bot, edict_t *e,
                                sg_role_t role, int team,
                                qboolean carrying)
{
	/* carry bookends: the STATE here is game logic, not telemetry -- the
	 * breakout gauge and the progress guard read it whether or not anyone
	 * is watching (it lived inside the debug gate until wave 141, which
	 * would have blinded both on any quiet server) */
	if (carrying && !bot->was_carrying)
	{
		bot->carry_start = level.time;
		bot->carry_startcost = -1;  /* gauged on first samples below */
		bot->carry_bestcost = -1;
		bot->carry_lost_at = 0.0f;
		sg_grab_time[team - CTF_TEAM_RED] = level.time;

		/* exit-lane asymmetry: snapshot the roads ridden in on, then
		 * roll this carry's coin (sg_exitasym, default 0 = never) */
		bot->exitasym_n = (bot->inlinks_n < 16) ? bot->inlinks_n : 16;
		memcpy(bot->exitasym_set, bot->inlinks, sizeof(bot->exitasym_set));
		bot->exitasym_armed = (random() * 100.0f <
		                       sg_cv.exitasym->value);

		/*
		 * HUMAN ESCAPE PRIORS (sg_escapeprior, enhancement 6). The
		 * corpus says a human leaving a robbed stand does not pick a
		 * uniform direction -- on lmctf41's red stand 76% of 30 human
		 * steals left east, on smap26's 74% left north -- and it also
		 * says he does not pick the SAME one every time. An argmin
		 * carrier has the opposite failing in both directions: one
		 * exit, always, and no reason for it to be the one people use.
		 *
		 * So the exit is DRAWN, once per carry, from that map's mined
		 * distribution, and the draw only tilts pricing: the bucket
		 * drawn gets its own measured probability as a discount for
		 * the next three seconds (the window the bearings were mined
		 * over), and every other road stays exactly as priced. A
		 * bucket humans used a fifth of the time gets drawn a fifth of
		 * the time and bends the price a fifth as hard as a bucket
		 * they used always -- the distribution's shape survives into
		 * behaviour instead of collapsing to its mode.
		 *
		 * The draw is a hash of the body, its life, and the clock, not
		 * random(): two carriers grabbing at once must draw
		 * independently, and one carrier must draw the same exit for
		 * the whole three seconds no matter how many times the fan is
		 * priced in between.
		 */
		bot->escprior_bucket = -1;
		bot->escprior_until = 0.0f;
		bot->escprior_dose = 0.0f;
		if (sg_rune && sg_cv.escapeprior->value > 0.0f)
		{
			/* the flag this carrier now holds is the ENEMY flag, and
			 * its stand is the one he just robbed */
			int fk = (team == CTF_TEAM_RED) ? 1 : 0;   /* 0 red, 1 blue */
			int stand = (team == CTF_TEAM_RED)
			                ? sg_fields.blue_flag_seed
			                : sg_fields.red_flag_seed;

			if (sg_escape_total[fk] > 0 && stand >= 0 &&
			    stand < sg_rune->hdr.num_seeds)
			{
				unsigned h = ((unsigned)(e - g_edicts) * 2654435761u) ^
				             ((unsigned)(bot->lives + bot->legs) * 40503u) ^
				             ((unsigned)(level.time * 10.0f) * 2246822519u);
				int b, acc = 0, pick;

				h ^= h >> 13;
				h *= 2654435761u;
				h ^= h >> 16;
				pick = (int)(h % (unsigned)sg_escape_total[fk]);
				for (b = 0; b < SG_ESC_BUCKETS - 1; b++)
				{
					acc += sg_escape_count[fk][b];
					if (pick < acc)
						break;
				}
				VectorCopy(sg_rune->seeds[stand].origin,
				           bot->escprior_org);
				bot->escprior_bucket = b;
				bot->escprior_until = level.time + 3.0f;
				bot->escprior_dose =
				    sg_cv.escapeprior->value / 100.0f *
				    ((float)sg_escape_count[fk][b] /
				     (float)sg_escape_total[fk]);
				if (bot->escprior_dose > 0.9f)
					bot->escprior_dose = 0.9f;
				if (sg_cv.debug->value)
					gi.dprintf("ESCPRIOR %s bucket=%d p=%d/%d dose=%.2f\n",
					           e->client->pers.netname, b,
					           sg_escape_count[fk][b],
					           sg_escape_total[fk], bot->escprior_dose);
			}
		}
	}
	else if (!carrying && bot->was_carrying)
	{
		bot->exitasym_armed = false;
		bot->escprior_bucket = -1;
	}
	if (sg_cv.debug->value)
	{
		if (carrying && !bot->was_carrying)
		{
			gi.dprintf("CARRY %s begins\n", e->client->pers.netname);
			/* the grab's honesty, on the record: how many defenders
			 * the last census believed present, and whether the
			 * patience valve had already expired (a FORCED grab into
			 * a room the hold never cleared). If parity grabs are
			 * ~all forced, the strict hold never wins at parity and
			 * the doctrine pivot is evidence, not taste. */
			gi.dprintf("GRABMODE %s room=%d %s\n",
			           e->client->pers.netname, bot->last_room,
			           (bot->strict_since > 0.0f &&
			            level.time - bot->strict_since >= 20.0f)
			               ? "forced" : "clean");
		}
		else if (!carrying && bot->was_carrying)
			gi.dprintf("CARRY %s ends after %.1fs\n",
			           e->client->pers.netname,
			           level.time - bot->carry_start);
		if ((int)role != bot->last_role && role == SG_ROLE_ESCORT)
			gi.dprintf("ESCORT %s begins\n", e->client->pers.netname);
	}
	/*
	 * Unconditionally: last_role feeds the rally's partner census, the
	 * escort head-count, and the wavepush attacker census. It sat inside
	 * the debug gate above until the 2026-08-11 standards pass -- on any
	 * server running sg_debug 0 (the fleet included) it never updated,
	 * and every one of those censuses read a stale role forever.
	 */
	bot->last_role = (int)role;
	bot->was_carrying = carrying;
}


/*
 * THE LIVE ROW (split from SG_BotThink, 2026-08-11 standards pass; body
 * verbatim): the fitted role row modulated by this bot's state -- combat
 * worths, the rune-threat bump, the patrol appetite.
 */
static void Think_LiveWeights(sg_bot_t *bot, edict_t *e, sg_role_t role,
                              int team, sg_weights_t *live)
{
	/*
	 * The role row is a BIAS, not an absolute. What an item is actually worth
	 * to THIS bot right now -- health as its own health drops, armour by
	 * deficit, a weapon when it has none worth the name, ammo against the
	 * floor of the weapon it holds, the quad against its respawn clock, a rune
	 * it is allowed to pick up -- is state, and SG_CombatWeights supplies it
	 * from WEAPONS.md 2.3. Every worth there is derived from a cited line of
	 * this tree; the row below stays exactly as fitted and is multiplied
	 * through. The result is clamped to the same [0, 2.0] the detour decay's
	 * 1500 ms scale (Detour_Value, above) makes meaningful.
	 */
	SG_CombatWeights(e, Weights_Row(role), live);
	/*
	 * Rune threat (WEAPONS.md 2.4-D4, the honest half): a sighted enemy
	 * glowing with RF_GLOW (p_view.c:792-794) holds SOME rune -- the glow
	 * never says which, so this is a generic bump to how much OUR side
	 * should want rune-class pickups, not the dossier's Damage-specific
	 * Resist play, which is unknowable from a sighting.
	 */
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s];

			if (en->client >= 0 && en->runed &&
			    level.time - en->seen_time < 15.0f)
			{
				/* generic: someone glows, runes matter more. When the
				 * inference chain can NAME the Damage rune in enemy
				 * hands, the dossier's full Resist posture applies
				 * (WEAPONS.md 2.4-D4: x1.80). */
				live->item[SG_FC_RUNE] *=
				    Caco_EnemyHasDamageRune(team) ? 1.80f : 1.45f;
				if (live->item[SG_FC_RUNE] > 2.0f)
					live->item[SG_FC_RUNE] = 2.0f;
				break;
			}
		}
	}
	/*
	 * The patrol's circuit is an appetite, not a waypoint list: a
	 * permanently item-hungry second defender oscillates between the
	 * stand's pull and whatever armor or health just respawned nearby,
	 * which IS the patrol (it13: without this, the unpinned patrol stood
	 * at its field minimum -- 592 samples at one point -- because
	 * standing there is what minimums are for).
	 */
	if (role == SG_ROLE_DEFEND && !bot->def_stand)
	{
		if (live->item[SG_FC_ARMOR] < 1.1f)  live->item[SG_FC_ARMOR] = 1.1f;
		if (live->item[SG_FC_HEALTH] < 1.0f) live->item[SG_FC_HEALTH] = 1.0f;
		if (live->item[SG_FC_AMMO] < 1.0f)   live->item[SG_FC_AMMO] = 1.0f;
	}
}


/*
 * THE OBJECTIVE (split from SG_BotThink, 2026-08-11 standards pass;
 * body verbatim): the role-to-goal-field switch -- carrier/defender
 * stands, the scoop, the interposition and formation stations, the
 * courier, the early return, the mega offer, and the tactics waypoint.
 * Emits the goal field, the route field, and its purity.
 */
static void Think_Objective(sg_bot_t *bot, edict_t *e, sg_role_t role,
                            int team, qboolean carrying,
                            const sg_weights_t *w,
                            const int *support, const int *intercept,
                            const int **goal_out, const int **route_out,
                            qboolean *route_pure_out)
{
	const int *goal_field;
	const int *route_field;
	qboolean route_pure;

	/*
	 * The role's principal field:
	 *   carrier  -> own stand (the capture point)
	 *   defender -> own stand's surroundings (the home field IS the post)
	 *   recover  -> OUR flag where belief puts it: the -now field for our own
	 *               flag, which floods from the believed position when it is
	 *               astray and from home otherwise
	 *   escort   -> our carrier's believed position (the support field, used
	 *               here as the objective rather than as a side term)
	 *   attacker -> the enemy flag WHERE BELIEF PUTS IT (home stand, or the
	 *               spot it was last seen lying, via the -now field)
	 * When our flag is astray and its taker was seen, the intercept field
	 * (their believed position) joins the composition for non-carriers.
	 */
	if (role == SG_ROLE_CARRY || role == SG_ROLE_DEFEND)
	{
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                                    : sg_fields.to_blue_flag;

		/*
		 * FIELD-MODE DEFENSE (dose 3+, wave 307). The pricing bias (doses
		 * 1-2) read null, as the extraction predicted: it bends wandering
		 * instead of choosing a post. Field mode CHOOSES: the defender's
		 * whole goal becomes the corpus's top post seed (the existing
		 * near-goal hold then keeps it there), and while our flag is
		 * astray the goal becomes the top intercept seed -- the human
		 * response's END position, not the carrier's current one.
		 */
		if (role == SG_ROLE_DEFEND)
		{
			qboolean astray =
			    (sg_caco_team_belief.flag[team - 1].state == SG_FLAG_ASTRAY);

			if (!astray && sg_fields.to_post[team - 1] &&
			    sg_cv.defpost->value >= 3)
				goal_field = sg_fields.to_post[team - 1];
			else if (!astray && !bot->def_stand &&
			         sg_fields.to_lane[team - 1] &&
			         sg_cv.raillane->value)
				/* the second defender holds the computed rail lane; the
				 * watchman stays on the stand (the .dpo lesson: never
				 * empty the stand for a post) */
				goal_field = sg_fields.to_lane[team - 1];
			else if (astray && sg_fields.to_icept[team - 1] &&
			         sg_cv.defreact->value >= 3)
				goal_field = sg_fields.to_icept[team - 1];
		}
	}
	else if (role == SG_ROLE_RECOVER)
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag_now
		                                    : sg_fields.to_blue_flag_now;
	else if (role == SG_ROLE_ESCORT)
	{
		edict_t *ht = SG_ChatEscortTarget(e);

		goal_field = sg_fields.our_carrier[team - 1];

		/*
		 * THE SCOOP (sg_scoop, A/B wave 183+). Sixty-two parity drops:
		 * defense returned thirty-four, we re-scooped three. The
		 * dropped flag is a live steal lying on the ground for up to
		 * thirty seconds, the escort is standing beside the corpse --
		 * and it keeps descending a dead carrier's field while a
		 * defender walks over and touches the flag home. No live
		 * carrier plus enemy flag astray: the escort takes the
		 * attacker's -now field, which floods from the believed drop
		 * spot. First body to the flag wins the relay; ours is
		 * closest by construction.
		 */
		if (sg_cv.scoop->value &&
		    sg_caco_team_belief.carrier[team - 1].client < 0 &&
		    sg_caco_team_belief.flag[2 - team].state == SG_FLAG_ASTRAY)
		{
			goal_field = (team == CTF_TEAM_RED)
			    ? sg_fields.to_blue_flag_now
			    : sg_fields.to_red_flag_now;
			if (sg_cv.debug->value &&
			    level.time >= bot->next_report - 0.9f)
				gi.dprintf("SCOOP %s\n", e->client->pers.netname);
		}

		/*
		 * THE INTERPOSITION (sg_interpose, A/B wave 180+). The killer
		 * census's standing fact: carriers die to live defenders with
		 * escorts RIGHT THERE -- near the carrier, which is where the
		 * support field sends them, and nowhere in particular relative
		 * to the gun. A bodyguard does not stand next to the client; he
		 * stands on the line the bullet takes. With a live carrier and
		 * a fresh threat believed near it, the escort's goal becomes
		 * the MIDPOINT of carrier and threat: body on the line, rail
		 * eats the escort, carrier keeps the flag. Falls through to
		 * the ordinary screen when there is no named threat.
		 */
		if (sg_cv.interpose->value)
		{
			sg_belief_carrier_t *oc =
			    &sg_caco_team_belief.carrier[team - 1];

			if (oc->client >= 0 && oc->seed >= 0)
			{
				int s11, ts = -1;
				float td11, best11 = 1200.0f;

				for (s11 = 0; s11 < SG_MAX_ENEMY_TRACK; s11++)
				{
					sg_belief_enemy_t *en11 =
					    &sg_caco_enemies[team - 1][s11];
					vec3_t dd11;

					if (en11->client < 0 || en11->seed < 0 ||
					    level.time - en11->seen_time >= 4.0f)
						continue;
					VectorSubtract(
					    sg_rune->seeds[en11->seed].origin,
					    sg_rune->seeds[oc->seed].origin, dd11);
					td11 = VectorLength(dd11);
					if (td11 < best11)
					{
						best11 = td11;
						ts = s11;
					}
				}
				if (ts >= 0)
				{
					static int interpose_field[SG_MAX_SEEDS];
					vec3_t mid;
					int ms = -1, mc = 0;

					/*
					 * EXIT ESCORT (sg_interpose dose 2, wave 319). The
					 * forensics killed the midpoint: 6.8 INTERPOSE calls per
					 * carry-second, 2% of kills with a teammate on the kill
					 * line -- the midpoint of a carrier and a 269u threat is
					 * INSIDE the duel, unreachable from the escort's median
					 * 1131u start. Dose 2 occupies the EXIT: the seed a
					 * fixed cost-lead AHEAD of the carrier on its homeward
					 * field -- the door the carrier runs through next.
					 */
					/*
					 * THE FORMATION (sg_interpose dose 3, wave 326 -- the
					 * owner's design). Lead and trail are STATIONS on the
					 * carrier's own route, at fixed cost-offsets that move
					 * with it: the leader sweeps the parked defenders ahead
					 * (90% of carrier kills), the trailer bodies the chasers,
					 * and the spacing keeps both out of the rail-and-splash
					 * envelope that made the midpoint useless. Station by
					 * slot parity: even leads at -1300ms, odd trails at
					 * +900ms. Dose 2 (static exit seed) kept as history.
					 */
					if (sg_cv.interpose->value >= 3)
					{
						int *cf = (team == CTF_TEAM_RED)
						    ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
						int cc = cf[oc->seed], s13;
						int lead = ((int)(e->client - game.clients) & 1) ? 0 : 1;
						int wcost = lead ? cc - 1300 : cc + 900;
						int band = 450;
						float bd13 = -1.0f;

						if (wcost < 0)
							wcost = 0;  /* carrier nearly home: lead collapses to the stand */
						for (s13 = 0; s13 < sg_rune->hdr.num_seeds &&
						     s13 < SG_MAX_SEEDS; s13++)
						{
							vec3_t dd13;
							float dl13;

							if (cf[s13] >= SG_FIELD_INF ||
							    cf[s13] < wcost - band || cf[s13] > wcost + band)
								continue;
							VectorSubtract(sg_rune->seeds[s13].origin,
							    sg_rune->seeds[oc->seed].origin, dd13);
							dl13 = VectorLength(dd13);
							if (bd13 < 0.0f || dl13 < bd13)
							{
								bd13 = dl13;
								ms = s13;
							}
						}
					}
					else if (sg_cv.interpose->value >= 2)
					{
						int *cf = (team == CTF_TEAM_RED)
						    ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
						int cc = cf[oc->seed], s12;
						int want_lo = cc - 2200, want_hi = cc - 900;
						float bd12 = -1.0f;

						for (s12 = 0; s12 < sg_rune->hdr.num_seeds &&
						     s12 < SG_MAX_SEEDS; s12++)
						{
							vec3_t dd12;
							float dl12;

							if (cf[s12] >= SG_FIELD_INF ||
							    cf[s12] < want_lo || cf[s12] > want_hi)
								continue;
							VectorSubtract(sg_rune->seeds[s12].origin,
							    sg_rune->seeds[oc->seed].origin, dd12);
							dl12 = VectorLength(dd12);
							if (bd12 < 0.0f || dl12 < bd12)
							{
								bd12 = dl12;
								ms = s12;
							}
						}
					}

					if (ms < 0)
					{
						VectorAdd(
						    sg_rune->seeds[oc->seed].origin,
						    sg_rune->seeds[
						        sg_caco_enemies[team - 1][ts].seed].origin,
						    mid);
						VectorScale(mid, 0.5f, mid);
						ms = Rune_NearestSeed(sg_rune, mid);
					}
					if (ms >= 0)
					{
						Field_Flood(sg_rune, interpose_field,
						            &ms, &mc, 1);
						goal_field = interpose_field;
						if (sg_cv.debug->value &&
						    level.time >= bot->next_report - 0.9f)
							gi.dprintf("INTERPOSE %s seed=%d\n",
							           e->client->pers.netname, ms);
					}
				}
			}
		}
		if (ht && ht->inuse && ht->client && !ht->deadflag)
		{
			/*
			 * Escorting the HUMAN who said "cover me": their position is
			 * team knowledge, the same rule our own carrier lives under
			 * (sg_caco.c's header). Flooded fresh each frame, the same
			 * cheap on-demand flood the intercept field uses.
			 */
			static int escort_field[SG_MAX_SEEDS];
			int hs = Rune_NearestSeed(sg_rune, ht->s.origin), hc = 0;

			if (hs >= 0)
			{
				Field_Flood(sg_rune, escort_field, &hs, &hc, 1);
				goal_field = escort_field;
			}
		}
	}
	else
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_blue_flag_now
		                                    : sg_fields.to_red_flag_now;

	/*
	 * THE RUNE COURIER (wave 243). Candidacy is a lottery -- 107
	 * near-misses one rotation, zero the next -- because holders guard
	 * while carriers sprint. So candidacy itself becomes the errand: a
	 * non-carrier holding RESIST or REGEN while a live carrier runs
	 * bare re-goals onto the carrier's support field for up to eight
	 * seconds, closes, and the toss fires at 400. The rune rides to
	 * the flag on the courier's legs, not on luck.
	 */
	if (sg_cv.runetoss->value &&
	    role != SG_ROLE_CARRY && role != SG_ROLE_DEFEND &&
	    e->client->rune &&
	    (e->client->rune->runetype == RUNE_RESIST ||
	     e->client->rune->runetype == RUNE_REGEN) &&
	    level.time >= bot->runetoss_next)
	{
		sg_belief_carrier_t *rc0 = &sg_caco_team_belief.carrier[team - 1];

		if (rc0->client >= 0)
		{
			edict_t *ce0 = g_edicts + 1 + rc0->client;

			if (ce0->inuse && ce0->client && ce0->health > 0 &&
			    (!ce0->client->rune ||
			     (ce0->client->rune->runetype != RUNE_RESIST &&
			      ce0->client->rune->runetype != RUNE_REGEN)))
			{
				if (bot->runeconv_until <= 0.0f)
					bot->runeconv_until = level.time + 8.0f;
				if (level.time < bot->runeconv_until)
					goal_field = sg_fields.our_carrier[team - 1];
				else
				{
					bot->runeconv_until = 0.0f;
					bot->runetoss_next = level.time + 20.0f;
				}
			}
			else
				bot->runeconv_until = 0.0f;
		}
		else
			bot->runeconv_until = 0.0f;
	}

	/*
	 * THE EARLY RETURN (sg_itemlead, owner's ruling 2026-08-05). Last of the
	 * goal overrides on purpose: the errand is a thing a bot does when nothing
	 * else is happening, and every branch above -- the carrier's stand, the
	 * recovery, the scoop, the interposition, the courier -- is something
	 * happening. Lead_Field refuses the errand outright while any of the jobs
	 * it names is live, so the ordering here and the gates in there say the
	 * same thing twice, which is deliberate: this line is the one a reader
	 * finds first.
	 */
	{
		const int *lead = Lead_Field(bot, role, carrying);

		if (lead)
			goal_field = lead;
	}

	/*
	 * THE MEGA OFFER (sg_megaworth), resolved once for the whole frame and
	 * BEFORE the tactical waypoint is scored -- the waypoint is committed for
	 * up to ten seconds off one Surface_At sweep, so a term that arrived after
	 * it would not reach the route until the next commitment.
	 */
	sg_cur_mega = Mega_Worth(bot, e, role);

	/*
	 * NO CAMPING THE PAD, and no obsession either -- the offer is bounded in
	 * TIME as well as in road.
	 *
	 * The term's shape has a well at the pad: detour is zero standing on it
	 * and grows in every direction, which is what makes the bot walk there.
	 * Ordinarily the well destroys itself -- arriving means touching the
	 * item, the health goes to 200, SG_WorthMega reads 0 on that same frame
	 * and the well is gone. But a bot that cannot quite reach the pad (a lip
	 * the body will not climb, a door it cannot open) would otherwise sit in
	 * the well indefinitely, and sitting there is worth exactly nothing:
	 * MegaHealth_think bleeds the overheal back off at 1 hp/s from the moment
	 * of pickup and the pad itself is on a 20 s respawn, so the prize is not
	 * something you can wait for the way you wait for a quad. Twelve seconds
	 * of standing offer without a pickup is a route that is not working; drop
	 * it and refuse another for the pad's own respawn period.
	 */
	if (sg_cur_mega > 0.0f && level.time < bot->mega_next)
		sg_cur_mega = 0.0f;
	if (sg_cur_mega > 0.0f)
	{
		if (!bot->mega_on)
			bot->mega_since = level.time;
		else if (level.time - bot->mega_since > SG_MEGA_PATIENCE)
		{
			sg_cur_mega = 0.0f;
			bot->mega_next = level.time + SG_MEGA_BACKOFF;
			if (SG_MegaOn() && sg_cv.debug->value)
				gi.dprintf("MEGA %s give up: %.0fs on offer, no pickup\n",
				           e->client->pers.netname, SG_MEGA_PATIENCE);
		}
	}

	if (SG_MegaOn() && sg_cv.debug->value)
	{
		/* the commit: the frame the offer turns on. The detour reported is
		 * the best one standing from where the bot is now, in ms of extra
		 * road -- back-solved from the value, which is what the surface
		 * actually spends. */
		if (sg_cur_mega > 0.0f && !bot->mega_on && bot->seed >= 0)
		{
			int		pad = -1;
			float	val = Mega_Detour(bot->seed, goal_field, &pad);
			float	det = (val > 0.0f)
			              ? 1500.0f * (sg_cur_mega / val - 1.0f) : -1.0f;

			if (val > 0.0f)
				gi.dprintf("MEGA %s commit: pad %d hp %d worth %.2f "
				           "detour %.0fms pull %.0f\n",
				           e->client->pers.netname, pad, e->health,
				           sg_cur_mega, det, 1500.0f * val);
		}
		/*
		 * The take. No pickup hook is needed and none is added: the mega is
		 * the only thing in the game that moves a player's health by 100 in
		 * one frame (count 100 with HEALTH_IGNORE_MAX, g_items.c:598-604),
		 * and a respawn cannot forge it because the dead branch above zeroes
		 * mega_hp on the way through.
		 */
		if (bot->mega_hp > 0 && e->health - bot->mega_hp >= 90)
			gi.dprintf("MEGA %s take: hp %d -> %d\n",
			           e->client->pers.netname, bot->mega_hp, e->health);
	}
	bot->mega_on = (sg_cur_mega > 0.0f);
	bot->mega_hp = e->health;

	bot->last_goalcost = (bot->seed >= 0 &&
	                      goal_field[bot->seed] < SG_FIELD_INF)
	                     ? goal_field[bot->seed] : -1;

	/*
	 * STRATEGY AND TACTICS (sg_tactics, A/B wave 177+). The owner's
	 * architecture: strategy is long-term and hard to change -- the role
	 * and its destination field, already sticky at 0.3 changes a minute.
	 * Tactics are room-scale goals that SERVE it: a committed waypoint
	 * picked from the band 0.8-2.5 seconds down the strategic gradient,
	 * scored ONCE with the full composed surface -- items, danger,
	 * cover, all of it -- then held. Between commitments the per-frame
	 * descent runs on the waypoint's own flood, a single stable field
	 * with nothing to tie against: strategy and tactics stop fighting
	 * in one equation at ten hertz, which is where the Brownian walk
	 * was born. The waypoint retires on arrival, on strategy change,
	 * on staleness (10s), or on unreachability -- the smooth
	 * transition, priced at the tactical boundary and nowhere else.
	 */
	route_field = goal_field;
	route_pure = false;
	if (sg_cv.tactics->value &&
	    role != SG_ROLE_ESCORT &&
	    /* CARRY excluded (trial-prep audit): route_pure suppresses the
	     * danger and detour terms for 10s a commit -- the exact corridors
	     * cover/carrypress/legcarrier exist to keep carriers off */
	    role != SG_ROLE_CARRY && bot->seed >= 0 &&
	    goal_field[bot->seed] < SG_FIELD_INF &&
	    goal_field[bot->seed] >= 400)
	{
		static int tac_fields[SG_MAXBOTS][SG_MAX_SEEDS];
		int bi = (int)(bot - sg_bots);
		qboolean need;

		/* the waypoint must be scored with the FULL surface (the
		 * design's own guarantee) -- this global was previously
		 * whatever the prior bot in the serial frame left behind,
		 * making waypoint quality depend on iteration order */
		sg_route_pure_now = false;
		need = (bot->tac_seed < 0 ||
		                 bot->tac_role != (int)role ||
		                 /* a tac_time AHEAD of the level clock is a
		                  * previous map's timestamp (level.time resets
		                  * to 0 on changelevel; the bots[] array does
		                  * not) -- stale by definition */
		                 bot->tac_time > level.time ||
		                 level.time - bot->tac_time > 10.0f ||
		                 tac_fields[bi][bot->seed] >= SG_FIELD_INF ||
		                 tac_fields[bi][bot->seed] < 300);

		if (need)
		{
			/*
			 * The owner's five questions, as code. (1) this room's
			 * goal: the waypoint, picked from the band one room down
			 * the strategic gradient. (2) the NEXT room's goal: g2,
			 * picked the same way from the band beyond. (3)+(4) the
			 * next room reaches back into this one: each waypoint
			 * candidate pays the graph cost from itself to g2, so
			 * the door chosen out of this room is the one that faces
			 * onward -- a decision here made better because of what
			 * comes next. (5) every band descends the role's own
			 * strategic field: tactics can only ever serve strategy,
			 * never replace it.
			 */
			static int g2_field[SG_MAX_SEEDS];
			int s10, best10 = -1, g2 = -1, cur = goal_field[bot->seed];
			float bv10 = 1e30f, gv10 = 1e30f;

			for (s10 = 0; s10 < sg_rune->hdr.num_seeds &&
			     s10 < SG_MAX_SEEDS; s10++)
			{
				float sv;

				if (goal_field[s10] >= SG_FIELD_INF ||
				    goal_field[s10] > cur - 2500 ||
				    goal_field[s10] < cur - 4500)
					continue;
				sv = Surface_At(s10, w, goal_field, support,
				                intercept);
				if (sv < gv10)
				{
					gv10 = sv;
					g2 = s10;
				}
			}
			if (g2 >= 0)
			{
				int gc = 0;

				Field_Flood(sg_rune, g2_field, &g2, &gc, 1);
			}
			for (s10 = 0; s10 < sg_rune->hdr.num_seeds &&
			     s10 < SG_MAX_SEEDS; s10++)
			{
				float sv;

				if (goal_field[s10] >= SG_FIELD_INF ||
				    goal_field[s10] > cur - 800 ||
				    goal_field[s10] < cur - 2500)
					continue;
				sv = Surface_At(s10, w, goal_field, support,
				                intercept);
				if (g2 >= 0 && g2_field[s10] < SG_FIELD_INF)
					sv += 0.5f * (float)g2_field[s10];
				if (sv < bv10)
				{
					bv10 = sv;
					best10 = s10;
				}
			}
			if (best10 >= 0)
			{
				int cost10 = 0;

				bot->tac_seed = best10;
				bot->tac_time = level.time;
				bot->tac_role = (int)role;
				Field_Flood(sg_rune, tac_fields[bi],
				            &bot->tac_seed, &cost10, 1);
				if (sg_cv.debug->value)
					gi.dprintf("TACTIC %s seed=%d strat=%d\n",
					           e->client->pers.netname,
					           best10, goal_field[best10]);
			}
			else
				bot->tac_seed = -1;     /* no room ahead: strategy raw */
		}
		if (bot->tac_seed >= 0 &&
		    tac_fields[bi][bot->seed] < SG_FIELD_INF)
		{
			route_field = tac_fields[bi];
			route_pure = true;      /* tactics were priced at selection:
			                         * the walk itself stays pure */
		}
	}

	*goal_out = goal_field;
	*route_out = route_field;
	*route_pure_out = route_pure;
}










void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	usercmd_t cmd;
	const int *goal_field, *support = NULL, *intercept = NULL;
	const int *route_field;
	qboolean route_pure;
	const sg_weights_t *w;
	sg_role_t role;
	int team, bestlink = -1;
	float bestval;
	float incumbent_v = 1e30f;
	qboolean carrying;

	/* movement policy state for this frame */
	vec3_t		move_dir;                   /* heading the route wants */
	float		view_yaw = 0.0f, view_pitch = 0.0f;
	qboolean	have_move = false;          /* a direction to travel at all */
	qboolean	open_ahead = false;         /* room in front to hop into */
	qboolean	run_link = false;           /* chosen link is ground running */
	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
	qboolean	rally_hold = false;         /* attacker waiting for a partner */
	qboolean	think_over;                 /* a stage ended the frame */
	qboolean	rail_hold = false;          /* waiting out a railer's reload */
	int			rail_seed = -1;             /* where that railer is believed */
	int			rail_client = -1;           /* and who he is */
	float		rail_dose = 0.0f;           /* the cover surcharge, role-scaled */
	float		post_yaw = 0.0f;            /* facing the likeliest approach */
	float		post_sight = -1.0f;         /* clear distance down that facing;
	                                         * WEAPONS.md 2.4-D3 picks the
	                                         * pre-held weapon from it */
	sg_weights_t	live;                   /* the role row, modulated by state */
	int			door_hold = 0;              /* rotating door ahead: 1 stand,
	                                         * 2 back out of its swing arc */
	edict_t		*door_ent = NULL;           /* which door is being waited on */
	qboolean	drop_yaw_locked = false;    /* executing a drop: no fan */
	float		drop_yaw = 0.0f;
	qboolean	hook_brake = false;         /* slow to the proof's standing
	                                         * start before firing a rope */

	/* the duel terms, read once per frame and priced per candidate seed */
	qboolean	duel = false;               /* combat has a live or fresh target */
	vec3_t		duel_org;                   /* where it is believed to be */
	float		duel_want = 0.0f;           /* range the weapon in hand wants */
	float		duel_expo = 0.0f;           /* what being seen costs, 0 to ~1 */

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;

	if (Think_Dead(bot, e, &cmd))
		return;
	Think_RespawnEdge(bot, e);
	bot->death_taught = false;

	/* my eyes feed the team belief before I decide from it */
	Caco_See(sg_rune, e);

	team = e->client->ctf.teamnum;
	/* LMCTF has ONE flag item: "Enemy Flag" (g_items.c:2478). Carrying is
	 * the same inventory test ctf_flagtouch itself makes. */
	{
		static gitem_t *flagitem;
		if (!flagitem)
			flagitem = FindItem("Enemy Flag");
		carrying = flagitem &&
		           e->client->pers.inventory[ITEM_INDEX(flagitem)] > 0;
	}

	role = SG_Role(bot, carrying);

	Think_CarryBookends(bot, e, role, team, carrying);

	Think_LiveWeights(bot, e, role, team, &live);
	w = &live;

	Think_Objective(bot, e, role, team, carrying, w, support, intercept,
	                &goal_field, &route_field, &route_pure);


	rally_hold = Think_ApproachBand(bot, e, role, team, goal_field);

	Think_InterceptField(role, team, &support, &intercept);

	bot->term_brake = 1.0f;         /* terminal braking re-earned every frame */
	bot->terminal = false;

	Think_TrackSeed(bot, e, team);
	if (bot->seed < 0)
	{
		ClientThink(e, &cmd);
		return;
	}

	/*
	 * The precision case: no tricks on the final approach.
	 *
	 * A flag is a thirty-unit box and a hop chain covers eight hundred units a
	 * second; the legacy bots arrived with a second of route left and sailed
	 * straight over the top, lap after lap. The legacy adapter suppressed the
	 * tricks within about 700 units of a must-touch goal (bl_main.c:451-464).
	 * The SLIPGATE field is denominated in real milliseconds of traversal, so
	 * the same idea is stated in time: inside a second and a half of the
	 * objective, run plainly and be able to stop. Speed serves the objective.
	 */
	precision = (goal_field[bot->seed] < 1500);

	/*
	 * Ask combat whether there is a fight on, ONCE, before the fan is walked.
	 * The answer is last frame's -- SG_CombatFrame runs after the movement is
	 * decided, which is the order the constitution requires (combat modifies a
	 * usercmd the body already built) -- and a tenth of a second of staleness
	 * on a believed position that is already up to two seconds old changes
	 * nothing. The carrier is excluded outright: 2.4-D2 is flee, not fight,
	 * and its own repulsion term below already prices contact.
	 */
	if (role != SG_ROLE_CARRY)
		duel = SG_CombatDuel(e, duel_org, &duel_want, &duel_expo);

	/* descend the surface: my seed vs every seed one proven link away */
	bestlink = Think_PickLink(bot, e, role, team, carrying, &live, w,
	                          goal_field, route_field, route_pure,
	                          support, intercept, precision, duel,
	                          duel_org, duel_want, duel_expo, rally_hold,
	                          &bestval, &incumbent_v, &rail_seed,
	                          &rail_client, &rail_dose, &rail_hold);

	think_over = false;
	bestlink = Think_CommitLink(bot, e, role, team, carrying, &live, w,
	                            goal_field, precision, duel, duel_org,
	                            duel_want, duel_expo, bestval,
	                            incumbent_v, rail_seed, rail_client,
	                            rail_dose, bestlink, &cmd, &rally_hold,
	                            &rail_hold, &think_over, &hold_post,
	                            &post_yaw, &post_sight);
	if (think_over)
		return;
	/*
	 * The surface has a gradient EVERYWHERE. Where the rune is proven, the
	 * gradient is the best outgoing link. Where it is not -- field infinite,
	 * no improving link, graph hole -- the gradient degrades to the local
	 * one: straight at the goal, deflected around whatever the feelers hit.
	 * A player with no knowledge of the map still runs toward the enemy
	 * base; a bot that stands still because its database has a hole is not
	 * descending a surface, it is worshipping a graph.
	 */
	Think_Move(bot, e, role, team, carrying, &live, w, goal_field,
	           route_field, route_pure, bestlink, precision, hold_post,
	           rally_hold, rail_hold, post_yaw, post_sight, duel,
	           duel_org, duel_want, duel_expo, &cmd, move_dir,
	           &view_yaw, &view_pitch, &have_move, &open_ahead,
	           &run_link, &door_hold, &door_ent, &drop_yaw_locked,
	           &drop_yaw, &hook_brake);
	Think_Emit(bot, e, role, team, carrying, &live, w, goal_field,
	           route_field, route_pure, bestlink, precision, hold_post,
	           rally_hold, rail_hold, rail_seed, rail_client, rail_dose,
	           post_yaw, post_sight, duel, duel_org, duel_want,
	           duel_expo, move_dir, view_yaw, view_pitch, have_move,
	           open_ahead, run_link, door_hold, door_ent,
	           drop_yaw_locked, drop_yaw, hook_brake, &cmd);
}



void SG_RunFrame(void)
{
	int i;
	static float last_time;

	/*
	 * Level changes are detected here rather than by a hook in the spawn
	 * code: the rune and fields were TAG_LEVEL so the engine already freed
	 * them, and level.time restarting is the tell. Same map or different,
	 * every pointer we held is stale the moment this trips.
	 */
	if (level.time < last_time ||
	    (sg_rune && Q_stricmp(sg_rune_map, level.mapname) != 0))
		SG_LevelChange();
	last_time = level.time;
	SG_CombatWhy();
	Danger_Decay();

	if (sg_rune)
	{
		Caco_Frame(sg_rune);
		Fields_Refresh(sg_rune);
	}
	Botfill_Frame();
	/* the scoreline and the clock, before anybody decides a role from them */
	Clock_Frame();

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->inuse)
			SG_BotThink(&sg_bots[i]);
}

/* ---------------------------------------------------------------- spawn */


void SG_LevelChange(void)
{
	int i;

	Danger_Save();      /* the map's lessons outlive the level */

	/* rune and fields were TAG_LEVEL -- the engine freed them */
	sg_rune = NULL;
	sg_human_use = NULL;    /* TAG_LEVEL too: freed with its rune */
	sg_human_live = NULL;
	sg_field_red = sg_field_blue = NULL;
	sg_rune_map[0] = 0;

	/*
	 * The clockplay state is stamped in level.time, which restarts at 0 on
	 * the new map -- left alone, the next-read and next-latch stamps from
	 * minute 19 of the old map would gag both for the first nineteen
	 * minutes of this one. The posture goes with them: last map's lead is
	 * not this map's.
	 */
	Clock_LevelReset();
	Tilt_LevelReset();      /* TAG_LEVEL as well: the engine freed it */
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
		/*
		 * A grudge is about a PLACE, and seed 137 on the next map is a
		 * different place. Tilt dies with the level -- what survives a
		 * map change is the danger dimension, which is saved above and
		 * is the only thing here that has earned it.
		 */
		sg_bots[i].tilt_lane_n = 0;
		sg_bots[i].tilt_seed = -1;
		sg_bots[i].tilt_killer_seed = -1;
		sg_bots[i].tilt_until = 0.0f;
		sg_bots[i].tilt_caution_until = 0.0f;
		sg_bots[i].tilt_death_time = -1000.0f;
		sg_bots[i].tilt_window = 0.0f;
	}
}


