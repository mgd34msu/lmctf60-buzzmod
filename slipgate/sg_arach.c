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
#include "slipgate/sg_goal.h"


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
int	sg_escape_count[2][SG_ESC_BUCKETS];  /* [0]=red flag stolen, [1]=blue */
int	sg_escape_total[2];                  /* 0 = no prior for that flag */


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
	sg_belief_carrier_t *own = &sg_caco_team_belief.carrier[SG_TeamIdx(team)];

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
		    (sg_caco_team_belief.flag[SG_TeamIdx(team)].state == SG_FLAG_ASTRAY);
		qboolean theirs_astray =
		    (sg_caco_team_belief.flag[SG_TeamIdx(SG_EnemyTeam(team))].state ==
		     SG_FLAG_ASTRAY);
		qboolean have_carrier = (own->client >= 0 &&
		                         sg_fields.our_carrier_valid[SG_TeamIdx(team)]);

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
			int ts = SG_TeamIdx(team);

			if (SG_TimerReady(ts_until[ts]))
			{
				ts_skew[ts] = (rand() % 3) - 1;
				SG_TimerArm(&ts_until[ts], 150.0f +
				            (float)(rand() % 90));
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
				int et = SG_TeamIdx(team);
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
	bot->patrol_seed = -1;          /* the next life picks its own legs */
	bot->patrol_until = 0.0f;
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
		SG_TimerArm(&bot->tilt_until, bot->tilt_window);
		SG_TimerArm(&bot->tilt_caution_until, SG_TILT_CAUTION +
		    (SG_TILT_CAUTION4 - SG_TILT_CAUTION) * sk);
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
			SG_Mark(&bot->prev_seed_time);
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
				int pti = SG_TeamIdx(team);

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
















void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	const int *goal_field, *support = NULL, *intercept = NULL;
	sg_role_t role;
	int team, bestlink = -1;
	qboolean carrying;

	/* the few frame terms still born outside the context: the seeds the
	 * stages take through it, and the one flow flag read here */
	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
	qboolean	rally_hold = false;         /* attacker waiting for a partner */
	qboolean	think_over;                 /* a stage ended the frame */
	float		post_yaw = 0.0f;            /* facing the likeliest approach */
	float		post_sight = -1.0f;         /* clear distance down that facing;
	                                         * WEAPONS.md 2.4-D3 picks the
	                                         * pre-held weapon from it */

	/* the duel terms, read once per frame and priced per candidate seed */
	qboolean	duel = false;               /* combat has a live or fresh target */
	vec3_t		duel_org;                   /* where it is believed to be */
	float		duel_want = 0.0f;           /* range the weapon in hand wants */
	float		duel_expo = 0.0f;           /* what being seen costs, 0 to ~1 */

	/* the think context: the container these frame locals are migrating
	 * into, loaded before each converted stage and read back after */
	sg_think_t	tc;

	memset(&tc.cmd, 0, sizeof(tc.cmd));
	tc.cmd.msec = 100;

	if (Think_Dead(bot, e, &tc.cmd))
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

	/* the context carries the stage contract from here down; the frame
	 * identity loads first, each stage adds what it resolves */
	tc.e = e;
	tc.role = role;
	tc.team = team;
	tc.carrying = carrying;

	Think_LiveWeights(bot, &tc);    /* fills tc.live */
	tc.w = &tc.live;

	tc.support = support;
	tc.intercept = intercept;

	Think_Objective(bot, &tc);

	goal_field = tc.goal_field;


	rally_hold = Think_ApproachBand(bot, &tc);

	Think_InterceptField(role, team, &support, &intercept);

	bot->term_brake = 1.0f;         /* terminal braking re-earned every frame */
	bot->terminal = false;

	Think_TrackSeed(bot, e, team);
	if (bot->seed < 0)
	{
		ClientThink(e, &tc.cmd);
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

	/* descend the surface: my seed vs every seed one proven link away.
	 * PickLink reads the think context; these locals are migrating into
	 * it stage by stage, so the context is loaded from them here and the
	 * results read back below until every stage speaks context natively. */
	tc.precision = precision;
	tc.duel = duel;
	VectorCopy(duel_org, tc.duel_org);
	tc.duel_want = duel_want;
	tc.duel_expo = duel_expo;
	tc.rally_hold = rally_hold;

	bestlink = Think_PickLink(bot, &tc);

	/* the context already holds PickLink's results; seed the in/out terms
	 * CommitLink owns and read every one back for the stages below */
	tc.think_over = false;
	tc.hold_post = hold_post;
	tc.post_yaw = post_yaw;
	tc.post_sight = post_sight;

	bestlink = Think_CommitLink(bot, &tc);

	think_over = tc.think_over;
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
	/* the context already holds every movement input except the frame's
	 * view seed; bestlink re-loads because CommitLink may have overridden
	 * the picker's choice through its return value */
	tc.bestlink = bestlink;
	tc.view_yaw = 0.0f;
	tc.view_pitch = 0.0f;

	Think_Move(bot, &tc);
	Think_Emit(bot, &tc);
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
	if (SG_TimerPending(last_time) ||
	    (sg_rune && Q_stricmp(sg_rune_map, level.mapname) != 0))
		SG_LevelChange();
	SG_Mark(&last_time);
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


