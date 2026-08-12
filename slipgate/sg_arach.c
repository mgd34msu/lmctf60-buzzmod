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


static float	sg_grab_time[2] = { -1000.0f, -1000.0f };  /* per team */
static float	sg_push_until[2];   /* the conductor's window (sg_wavepush) */
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

static unsigned char *sg_human_use; /* per-link human traffic tier (0-255)
                                     * from the demo corpus; NULL = none */
static unsigned char *sg_def_post[2];  /* per-seed human defensive dwell
                                        * tier by team (.dpo plane 0/1):
                                        * where humans actually stand while
                                        * their flag is home -- 19% of it
                                        * within 250u of the stand, the
                                        * rest on the approaches */
static unsigned char *sg_def_icept[2]; /* per-seed steal-response END
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

static unsigned char *sg_human_escape; /* the ESCAPEE's cut: only the flag
                                        * carrier's own entity trajectory in
                                        * the 20s after each steal (.hme) --
                                        * the roads humans actually flee on,
                                        * as opposed to .hml's hunter-heavy
                                        * POV-agnostic window */
static unsigned char *sg_human_live; /* same, cut from the 20s windows
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
 * The compass bucket of a planar direction: 45 degrees per bucket, bucket
 * 0 centred on +x and buckets advancing counter-clockwise (E NE N NW W SW
 * S SE). The fold into 0..2pi happens BEFORE the scale for the same
 * reason Heading_Quantize (sg_rune.c) folds -- a negative angle scaled and
 * truncated is not a wrap. escapepriors.py bearing_bucket() is this
 * function; the two must agree or the mined buckets name other exits.
 */
static int SG_Bearing8(float dx, float dy)
{
	float a = atan2f(dy, dx) * (180.0f / (float)M_PI) + 22.5f;

	while (a < 0.0f)
		a += 360.0f;
	return ((int)(a / 45.0f)) & 7;
}

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
static int	*sg_airnext;

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


/*
 * Detour worth: an item matters by how little it takes you off your road.
 *     value = worth / (1 + (cost_to_item + item_to_goal - direct) / scale)
 * All three terms are field lookups. An item dead on the route costs
 * nothing extra and pays full worth; one far off the road decays away.
 */
/*
 * Different runes are worth different things to different ROLES: a
 * defender wants staying power at the post (Resist, Regen), an attacker
 * and a carrier want the map to shrink (Haste), a recoverer wants the
 * re-kill (Damage). Class-level worth (Worth_Rune, sg_combat.c) prices
 * "a rune, given my state"; this table prices "THIS rune, given my job".
 * Values orbit 1.0 -- they are the fitted component, like every weight.
 */
static float Rune_RoleFactor(int role, int entnum)
{
	static const struct { const char *cls; float w[SG_ROLES]; } tab[] = {
		/*                  ATTACK DEFEND CARRY  RECOVER ESCORT */
		{ "damage_rune",  { 1.15f, 1.15f, 0.70f, 1.25f, 1.10f } },
		{ "haste_rune",   { 1.30f, 0.80f, 1.30f, 1.15f, 1.00f } },
		/* courier-starvation fix (10,098 candidates, 40 tosses: the
		 * bots that hold defensive runes are DEFEND, anchored home,
		 * while the toss needs a holder NEAR THE CARRIER; under
		 * sg_runetoss>=2 the escort out-prices the defender for
		 * resist/regen so handoffs can actually form) */
		{ "resist_rune",  { 0.90f, 1.25f, 1.20f, 1.00f, 1.15f } },
		{ "regen_rune",   { 0.90f, 1.15f, 1.05f, 0.90f, 1.00f } },
		{ "vampire_rune", { 1.05f, 1.00f, 0.80f, 1.05f, 1.00f } },
	};
	edict_t *e;
	int i;

	if (entnum <= 0 || entnum >= globals.num_edicts)
		return 1.0f;
	e = g_edicts + entnum;
	if (!e->classname)
		return 1.0f;
	for (i = 0; i < (int)(sizeof(tab) / sizeof(tab[0])); i++)
		if (strcmp(e->classname, tab[i].cls) == 0)
		{
			float w2 = tab[i].w[role];

			if (role == SG_ROLE_ESCORT && i >= 2 && i <= 3 &&
			    sg_cv.runetoss->value >= 2)
				w2 = 1.40f;
			return w2;
		}
	return 1.0f;
}

/* the role whose surface is being evaluated this frame -- SLIPGATE runs
 * its bots strictly serially, so a file-static carries it into the
 * detour arithmetic without widening every signature on the path */
static int sg_cur_role;
static int sg_cur_team;      /* for the shelf-cliff pricing */

static const int *sg_cur_danger;
static qboolean sg_route_pure_now;  /* tactics priced at selection: the
                                     * per-frame walk stays pure */
static qboolean sg_cur_push;        /* conductor's downbeat: march, no detours */
static int sg_cur_health;           /* pricing bot's health (dose gates) */
static float sg_cur_mega;           /* this bot's overheal worth this frame,
                                     * 0 = no mega detour on offer */

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

static float Detour_Value(int here, int cls, const int *goal_field,
                          float worth)
{
	int *ifld = sg_fields.item[cls];
	int to_item = ifld[here];
	int direct = goal_field[here];

	if (direct >= SG_FIELD_INF)
		return 0.0f;

	/*
	 * Where per-item fields exist (powerups, runes), the triangle is exact.
	 * per_item[cls][k] was flooded FROM item k, so it reads as cost from
	 * anywhere TO that item; the far leg is the goal field sampled at the
	 * item's own seed. The item that costs the least extra road wins -- not
	 * the nearest one, which is what a class field would have answered.
	 *
	 *     detour = cost_to_item + item_to_goal - direct
	 *     value  = worth / (1 + max(0, detour) / scale)
	 */
	if (sg_fields.per_item_count[cls] > 0)
	{
		float best = 0.0f;
		int k;

		for (k = 0; k < sg_fields.per_item_count[cls]; k++)
		{
			const int *kfld = sg_fields.per_item[cls][k];
			int kseed = sg_fields.per_item_seed[cls][k];
			int cost_to, item_to_goal, detour;
			float v;

			if (!kfld || kseed < 0)
				continue;
			cost_to = kfld[here];
			item_to_goal = goal_field[kseed];
			if (cost_to >= SG_FIELD_INF || item_to_goal >= SG_FIELD_INF)
				continue;

			detour = cost_to + item_to_goal - direct;
			if (detour < 0)
				detour = 0;      /* an item on the road is free, never a bonus */
			v = worth / (1.0f + (float)detour / 1500.0f);
			if (cls == SG_FC_RUNE)
				v *= Rune_RoleFactor(sg_cur_role,
				                     sg_fields.per_item_ent[cls][k]);
			if (v > best)
				best = v;
		}
		return best;
	}

	/*
	 * The other classes are flooded per class only: item_to_goal is
	 * unknowable once many interchangeable items share one field, which gives
	 * cost to the NEAREST of them, so the triangle is approximated by to_item
	 * alone against scale. Honest limitation, recorded -- it holds for the
	 * classes whose members are interchangeable (a health box is a health
	 * box), which is why identity-bearing classes got per-item fields.
	 */
	if (to_item >= SG_FIELD_INF)
		return 0.0f;
	return worth / (1.0f + (float)to_item / 1500.0f);
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
#define SG_MEGA_MAXDETOUR	4000    /* ms of extra road, hard refusal above */
#define SG_MEGA_PATIENCE	12.0f   /* seconds an offer may stand unspent */
#define SG_MEGA_BACKOFF		20.0f   /* ...and the refusal after, the pad's own
                                     * respawn (SetRespawn 20, g_items.c:596) */

static float Mega_Detour(int here, const int *goal_field, int *out_ent)
{
	float	best = 0.0f;
	int		direct = goal_field[here];
	int		k;

	if (out_ent)
		*out_ent = -1;
	if (direct >= SG_FIELD_INF)
		return 0.0f;

	for (k = 0; k < sg_fields.mega_count; k++)
	{
		const int	*kfld = sg_fields.to_mega[k];
		int			kseed = sg_fields.mega_seed[k];
		int			cost_to, pad_to_goal, detour;
		float		v;

		if (!kfld || kseed < 0)
			continue;
		cost_to = kfld[here];
		pad_to_goal = goal_field[kseed];
		if (cost_to >= SG_FIELD_INF || pad_to_goal >= SG_FIELD_INF)
			continue;

		detour = cost_to + pad_to_goal - direct;
		if (detour < 0)
			detour = 0;         /* a pad on the road is free, never a bonus */
		if (detour > SG_MEGA_MAXDETOUR)
			continue;
		v = sg_cur_mega / (1.0f + (float)detour / 1500.0f);
		if (v > best)
		{
			best = v;
			if (out_ent)
				*out_ent = sg_fields.mega_ent[k];
		}
	}
	return best;
}

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
 * The surface at one seed, for one bot: weighted composition of every
 * basis field. LOWER is better (fields are costs); items and support
 * subtract because they add value. This is V(x | bot) from the design,
 * evaluated at the handful of seeds one step away.
 */
static float Surface_At(int seed, const sg_weights_t *w,
                        const int *goal_field, const int *support,
                        const int *intercept)
{
	float v;
	int c;

	if (goal_field[seed] >= SG_FIELD_INF)
		return 1e30f;

	/*
	 * sg_atkobj (stage-2 volume lever 3): a per-server multiplier on the
	 * ATTACK role's objective pull, because the shared-gamedir weights
	 * file cannot A/B across a pair. 1.0 (default) is byte-identical;
	 * >1 makes attackers price the flag harder relative to items/cover,
	 * the hypothesis being that human-level steal VOLUME comes from
	 * commitment, not routes. Trialed like everything else.
	 */
	v = w->objective * (float)goal_field[seed];
	if (sg_cur_role == SG_ROLE_ATTACK)
	{
		float ao = sg_cv.atkobj->value / 100.0f;

		if (ao != 1.0f)
			v = w->objective * ao * (float)goal_field[seed];
	}

	/*
	 * THE SHELF PAYS ITS CLIFF (sg_shelfcost, steal-genesis study): an
	 * attacker pricing a seed under the enemy stand pays the measured
	 * climb-back asymmetry RL_DROP never charged. 101 attempts on that
	 * floor, 91% dead in 1.2s, zero steals -- removing a zero-yield room
	 * cannot cost caps. Zero everywhere on flat-stand maps.
	 */
	if (sg_cur_role == SG_ROLE_ATTACK &&
	    sg_cv.shelfcost->value > 0.0f)
	{
		int ti9 = (sg_cur_team == CTF_TEAM_RED) ? 0 : 1;

		if (sg_fields.shelf_cliff[ti9] &&
		    sg_fields.shelf_cliff[ti9][seed] > 0)
			v += sg_cv.shelfcost->value *
			     (float)sg_fields.shelf_cliff[ti9][seed];
	}

	/* tactics mode: the waypoint was scored with the full surface at
	 * commitment; between commitments the descent runs on its flood
	 * and nothing else -- one field, no ties, no scribble */
	if (sg_route_pure_now)
		return v;

	/*
	 * THE NAKED CARRY (sg_nakedcarry, A/B wave 166+). Every navigation
	 * link from map file to field flood verified sound tonight, yet
	 * parity carriers drift away from a correct destination -- and one
	 * price term is self-reinforcing: the learned danger field records
	 * every carrier death along the road home and taxes that road for
	 * the next carrier. At parity the deaths are dense, so the tax
	 * compounds until home costs more than wandering. Naked mode prices
	 * a carrier's step by time-to-home and NOTHING else: no danger, no
	 * item detours, no cover terms. If naked carriers close on their
	 * stands, the wall was the pricing; if they still wander, the
	 * descent itself goes under the microscope.
	 */
	if (sg_cur_role == SG_ROLE_CARRY &&
	    sg_cv.nakedcarry->value)
		return v;

	/* the danger dimension: learned, decayed, team-indexed (set by the
	 * caller alongside sg_cur_role); zero where nothing has died */
	if (sg_cur_danger && seed < SG_MAX_SEEDS)
		v += (float)sg_cur_danger[seed];

	for (c = 0; c < SG_FIELD_CLASSES; c++)
		if (w->item[c] > 0.0f)
			/* legcarrier dose 3: a healthy carrier does not shop --
			 * humans never detour mid-carry (corpus: 310 u/s flat).
			 * Hurt carriers keep the detour; health is worth a stop. */
			v -= (sg_cur_push ||
			      (sg_cur_role == SG_ROLE_CARRY && sg_cur_health > 60 &&
			       sg_cv.legcarrier->value >= 3.0f))
			     ? 0.0f :
			     1500.0f * Detour_Value(seed, c, goal_field, w->item[c]);

	/*
	 * THE MEGA (sg_megaworth). A separate term rather than a bend in the
	 * health class, because the two say opposite things at 100 hp: the class
	 * is worth 0.05 there and the mega is worth its whole budget. sg_cur_mega
	 * is already zero unless the role, the belief, the headroom and the fight
	 * all permit it (Mega_Worth), so this line is an OFFER and never a
	 * requirement -- the objective term is untouched and a bot that finds
	 * nothing cheap enough simply walks its road.
	 *
	 * Below the naked-carry and route-pure early returns above on purpose: a
	 * carrier does not shop, and a committed tactical waypoint was scored
	 * with this term already in it.
	 */
	if (sg_cur_mega > 0.0f && sg_fields.mega_count > 0)
		v -= 1500.0f * Mega_Detour(seed, goal_field, NULL);

	if (support && w->carrier_support > 0.0f && support[seed] < SG_FIELD_INF)
	{
		float csup = w->carrier_support;

		/*
		 * LONE WOLF (sg_lonewolf, rung-4 set #1 tell #1). All three
		 * judges read the same thing off every bot sheet: our carries
		 * run escorted 0.33-0.75 of their length while pub humans run
		 * flags alone at 0.02-0.32. sg_escortdose gated the ESCORT
		 * ROLE and moved nothing, because escort_fraction measures
		 * teammate PROXIMITY and the proximity is incidental -- every
		 * non-escort role also carries a carrier_support pull (defend
		 * 0.40, attack 0.10), so the whole team drifts down the
		 * carrier's lane whatever its assignment.
		 *
		 * This is the honest cut: REMOVE the artificial cohesion
		 * rather than add artificial dispersion. A pub is not a team
		 * fleeing its own carrier -- it is players whose objectives
		 * simply diverge. The dose scales the support pull for every
		 * role except the assigned escort, whose whole job it is.
		 * 1.0 == today's behavior exactly.
		 */
		if (sg_cur_role != SG_ROLE_ESCORT)
		{
			cvar_t *lw = sg_cv.lonewolf;

			if (lw && lw->value >= 0.0f)
				csup *= lw->value;
		}
		v += csup * (float)support[seed];
	}

	if (intercept && w->intercept > 0.0f && intercept[seed] < SG_FIELD_INF)
		v += w->intercept * (float)intercept[seed];

	return v;
}

/*
 * ------------------------------------------------------------ the policy
 *
 * Movement, closed-form, from the engine rather than from feel. This is the
 * legacy body's proven policy (bl_main.c:92-175, BotAirStrafe and its
 * derivation) moved into the SLIPGATE body unchanged in substance; only the
 * inputs are re-expressed in SLIPGATE terms -- the route direction is the
 * heading the surface descent chose, not a botlib bi->dir.
 *
 * pm_maxspeed caps wishspeed, not velocity. PM_Accelerate adds
 *
 *     accelspeed = accel * frametime * wishspeed
 *
 * along wishdir for as long as addspeed = wishspeed - (velocity . wishdir)
 * stays positive, so an input held off the direction of travel keeps that
 * term alive and the speed climbing. The smallest angle that still leaves
 * addspeed at the cap is
 *
 *     cos(theta) = (wishspeed - accelspeed) / speed
 *
 * On the ground accel is 10 and the limit is friction, speed * 6 * frametime,
 * which scales with speed while the gain at the best angle does not: they meet
 * near 370. Driving forwardmove straight down the heading converges on 300 and
 * stops there. In the air accel is 1 -- a tenth the rate, but no friction and
 * therefore no ceiling.
 *
 * Which way to lean is not a coin flip: leaning toward the side the route
 * turns accelerates and steers at once, so the velocity is pulled onto the
 * path instead of away from it. The view is not involved -- the bot names the
 * direction and decomposes it against the view it is already holding, so none
 * of this costs any aim.
 */
static void SG_Strafe(usercmd_t *cmd, vec3_t fwd, vec3_t right,
                      vec3_t vel, vec3_t dir,
                      float speed2d, float frametime, float accel)
{
	vec3_t	vdir, d;
	float	wishspeed = 300.0f;     /* pm_maxspeed clamps wishspeed to this */
	float	accelspeed, c, th, sn, cs, cross;

	if (speed2d < 1.0f)
		return;

	/*
	 * THE AIR CAP (sg_airgain, wave 296+). PM_AirAccelerate clamps the
	 * wishspeed IT uses to 30 (pmove.c:382) -- the whole strafe-jump
	 * exploit lives in that clamp: the projection gate is v.wish < 30,
	 * open at ANY speed for a wish pointed far enough off the velocity.
	 * This derivation with wishspeed 300 computes an air angle for an
	 * engine that does not exist; measured air gain ran NEGATIVE
	 * (-0.8/100ms vs the pub human's +3.0). In air, derive from the
	 * engine's real constant.
	 */
	if (accel < 5.0f && sg_cv.airgain->value)
		wishspeed = 30.0f;

	accelspeed = accel * frametime * wishspeed;

	/* dose 1 read NEGATIVE (296): the honest ~84-degree air lean turns
	 * the velocity off the route and the nav corrections eat more than
	 * the harvest pays. Dose 2 caps the lean at ~40 degrees: partial
	 * gain that stays roughly route-aligned. */
	#define SG_AIRLEAN_CAP 0.70f

	/*
	 * Below wishspeed - accelspeed there is no angle to find: addspeed is
	 * already saturated pointing straight down the route, so the input that
	 * accelerates hardest is also the one that steers, and leaning off it
	 * would only trade heading for nothing. Leave the caller's plain forward
	 * alone -- this is the whole of the low-speed case, and it is why the
	 * strafe is not a mode the bot enters and leaves.
	 */
	if (speed2d <= wishspeed - accelspeed)
		return;

	c = (wishspeed - accelspeed) / speed2d;
	if (c > 1.0f) c = 1.0f;
	if (c < -1.0f) c = -1.0f;
	th = acosf(c);

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	/* lean the way the route turns, so the gain also steers */
	cross = vdir[0] * dir[1] - vdir[1] * dir[0];
	if (cross < 0.0f)
		th = -th;

	if (accel < 5.0f &&
	    sg_cv.airgain->value >= 2 &&
	    th > SG_AIRLEAN_CAP)
		th = SG_AIRLEAN_CAP;

	sn = sinf(th);
	cs = cosf(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	/*
	 * Decomposed against the basis pmove will actually build (pitch/3, see
	 * the caller), so the engine reconstructs the direction that was asked
	 * for. 400 on both axes before the clamp: wishvel is scaled down to
	 * pm_maxspeed anyway, and what matters is the direction.
	 */
	cmd->forwardmove = (short)(DotProduct(fwd, d) * 400.0f);
	cmd->sidemove = (short)(DotProduct(right, d) * 400.0f);
}

/*
 * ------------------------------------------------- the air-strafe chain
 *
 * THE VIEW AND THE PATH TURN TOGETHER (sg_airstrafe, default 0 = off).
 *
 * What a human does that the body above does not: he rotates the VIEW and
 * the strafe key together while airborne, so the wish direction stays off
 * the velocity for the whole flight instead of for the instant the route
 * happens to bend. Chained across hops that is 800-1600 u/s on a pub
 * server; this fleet's sustained runs have never exceeded ground speed.
 *
 * WHICH ENGINE IS ACTUALLY RUNNING. SG_Strafe's air branch offers a
 * derivation from PM_AirAccelerate's 30-unit clamp (sg_arach.c:1984), and
 * that dose measured NEGATIVE. It had to: PM_AirAccelerate is only reached
 * when air acceleration is switched on, and it is not. The fleet's engine
 * is yquake2 (engines/yquake2/release/q2ded); in its pmove
 *
 *     src/common/pmove.c:62      float pm_airaccelerate = 0;
 *     src/server/sv_main.c:636   Cvar_Get("sv_airaccelerate", "0", LATCH)
 *
 * and PM_AirMove's airborne branch (pmove.c:673-680) reads
 *
 *     if (pm_airaccelerate) PM_AirAccelerate(wishdir, wishspeed, 10);
 *     else                  PM_Accelerate(wishdir, wishspeed, 1);
 *
 * so with the cvar at its default the air runs the SAME function the
 * ground does, at accel 1 instead of 10 and with PM_Friction skipped
 * because groundentity is NULL. (q2repro agrees line for line:
 * src/server/main.c:2145 and game3_pmove/template.c, PM_AirMove.) So the
 * angle to fly is the one SG_Strafe's own comment derives at
 * sg_arach.c:1958, with the engine's real constants:
 *
 *     addspeed   = wishspeed - (velocity . wishdir)      PM_Accelerate
 *     accelspeed = accel * frametime * wishspeed, capped at addspeed
 *
 *     cos(theta) = (wishspeed - accelspeed) / speed
 *
 * wishspeed is pm_maxspeed = 300 (PM_AirMove clamps wishvel to it before
 * accelerating), accel is 1, and frametime is ONE SUB-STEP -- 12 or 13 ms
 * at sg_subframes 8 -- so accelspeed = 1 * 0.0125 * 300 = 3.75 u/s.
 *
 *     speed  300    400    500    600    800   1000
 *     theta  9.0d  42.2d  53.6d  60.5d  68.3d  72.8d
 *
 * and the gain at that angle is accelspeed * cos(theta) = 3.75 * 296.25 /
 * speed ~= 1111 / speed per sub-step: 2.8 u/s per step at 400, and eighty
 * steps go by in a second. Below 296.25 there is no angle to find -- the
 * cap is not binding and straight ahead is already the fastest input --
 * which is exactly why this only ever touches a body that has already been
 * pushed through the ground cap by a hop.
 *
 * WHY THE VIEW HAS TO MOVE FOR ANY OF IT TO PAY. The angle is measured
 * from the direction of TRAVEL, so holding it drags the velocity off the
 * route at roughly the same rate it feeds it -- wave 296's finding, and the
 * reason its dose 2 capped the lean at 40 degrees and harvested little. A
 * human does not hold the lean, he SWINGS it: the view (and with it the
 * strafe axis) sweeps through the heading and out the other side, so the
 * path is a shallow S whose mean is the road and whose every instant is
 * off-axis. Here that swing is a sinusoid centred on the route heading,
 * amplitude theta, biased by the standing heading error so the swing that
 * corrects gets the longer half. The lean is a signed unit number; this
 * function turns it into the command.
 *
 * WHERE THE ROTATION IS SPENT. The view carries SG_AS_VIEWSHARE of theta
 * and the input carries the rest, exactly as a player's forward+strafe
 * diagonal does. The split costs nothing in physics because the wish
 * direction is decomposed against the view pmove will ACTUALLY use this
 * sub-step (bot->vy_cur, post-slew) rather than the view that was asked
 * for -- so a slewed, lagging, rate-limited view still reconstructs the
 * commanded direction to the degree. Nothing here writes velocity: the
 * whole gain comes out of cmd.forwardmove, cmd.sidemove and cmd.angles,
 * through the same PM_Accelerate a human client drives.
 */
typedef struct
{
	float		lean;       /* the sinusoid, -1..1; sign is the lean side */
	float		vy_cur;     /* the view yaw pmove will use THIS sub-step */
	float		vp_cur;     /* and its pitch, before the engine's /3 */
	qboolean	chain;      /* dose 2: hop chaining as well as the lean */
} sg_air_t;

#define SG_AIR_ACCEL	1.0f    /* PM_AirMove's airborne PM_Accelerate accel */

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
#define SG_AS_PERIOD	1.35f
#define SG_AS_VIEWSHARE	0.55f
#define SG_AS_VIEWMAX	32.0f
#define SG_AS_CORR		25.0f
#define SG_AS_ABORT		40.0f
#define SG_AS_RUN		320.0f
#define SG_AS_HOLD		0.70f   /* the road bar a live chain is held to */
#define SG_AS_FLOOR		240.0f
#define SG_AS_FLAGKEEP	220.0f
#define SG_AS_MINCHAIN	0.6f

static void SG_AirStrafeCmd(usercmd_t *cmd, const sg_air_t *air,
                            vec3_t vel, float speed2d, float frametime)
{
	vec3_t	basis, vf, vr, vdir, d;
	float	wishspeed = 300.0f;     /* pm_maxspeed clamps wishvel to this */
	float	accelspeed, c, th, sn, cs, fl;

	if (speed2d < 1.0f)
		return;

	accelspeed = SG_AIR_ACCEL * frametime * wishspeed;

	/*
	 * Under wishspeed - accelspeed the cap is not binding: addspeed is
	 * saturated pointing straight down the travel line and leaning off it
	 * would trade heading for nothing. Same early return SG_Strafe makes,
	 * and the reason this is not a mode the body enters and leaves.
	 */
	if (speed2d <= wishspeed - accelspeed)
		return;

	c = (wishspeed - accelspeed) / speed2d;
	if (c > 1.0f) c = 1.0f;
	if (c < -1.0f) c = -1.0f;
	th = acosf(c) * air->lean;   /* the optimum, swung by the sinusoid */

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	sn = sinf(th);
	cs = cosf(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	/*
	 * The basis pmove will build from the SLEWED view (PM_AirMove takes
	 * viewangles with PITCH divided by three), and the same reconstruction
	 * the per-sub-step course decomposition uses: forward's horizontal part
	 * is shortened by cos(pitch/3), so it is renormalised before the
	 * projection or the pair comes out skewed toward the strafe axis.
	 */
	basis[YAW] = air->vy_cur;
	basis[PITCH] = air->vp_cur;
	if (basis[PITCH] > 180.0f)
		basis[PITCH] -= 360.0f;
	basis[PITCH] /= 3.0f;
	basis[ROLL] = 0.0f;
	AngleVectors(basis, vf, vr, NULL);

	fl = sqrtf(vf[0] * vf[0] + vf[1] * vf[1]);
	if (fl < 0.01f)
		return;
	cmd->forwardmove = (short)(400.0f * (d[0] * vf[0] + d[1] * vf[1]) / fl);
	cmd->sidemove = (short)(400.0f * (d[0] * vr[0] + d[1] * vr[1]));
}

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
#define SG_PURSUIT_MAX 8    /* seeds of chain; 8 x 128u median link covers
                             * any lookahead worth trying */

/*
 * The point `look` units of arc down the polyline org -> chain[0] -> ...
 * -> chain[n-1], measured horizontally. A runner does not stare at his
 * next footprint and not at the horizon either: he holds a point a fixed
 * stride-count down the road, and the road bends under it. The fixed
 * arc-distance is the whole of the anti-zigzag: the seed centers keep
 * arriving 3.2 times a second, but the point they define moves
 * continuously.
 */
static qboolean SG_PursuitPoint(vec3_t org, vec3_t chain[], int n,
                                float look, vec3_t out)
{
	vec3_t	cur;
	float	rem = look;
	int		i;

	if (n <= 0)
		return false;
	VectorCopy(org, cur);
	for (i = 0; i < n; i++)
	{
		vec3_t	seg;
		float	len;

		VectorSubtract(chain[i], cur, seg);
		seg[2] = 0.0f;
		len = VectorLength(seg);
		if (len < 1.0f)
		{
			VectorCopy(chain[i], cur);
			continue;
		}
		if (len >= rem)
		{
			float f = rem / len;

			out[0] = cur[0] + (chain[i][0] - cur[0]) * f;
			out[1] = cur[1] + (chain[i][1] - cur[1]) * f;
			out[2] = cur[2] + (chain[i][2] - cur[2]) * f;
			return true;
		}
		rem -= len;
		VectorCopy(chain[i], cur);
	}
	VectorCopy(cur, out);       /* chain ran out: aim at its far end */
	return true;
}

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
#define SG_AS_BEND	30.0f       /* degrees the chord may sit off the route */
#define SG_AS_CHORD	0.80f       /* chord / arc a road has to keep */

/*
 * Standing inside `keep` of either flag stand. The chain's hard veto: a
 * body that is about to touch a flag needs to be able to stop on it, and
 * an air-strafe is a commitment to a heading for the length of a flight.
 * Both stands, not just the goal one -- arriving at speed and leaving at
 * speed are the same mistake at the same place.
 */
static qboolean SG_NearAFlag(edict_t *e, float keep)
{
	int	t;

	if (!sg_rune)
		return false;
	for (t = 0; t < 2; t++)
	{
		int		seed = t ? sg_fields.blue_flag_seed
		                 : sg_fields.red_flag_seed;
		vec3_t	fd;

		if (seed < 0)
			continue;
		VectorSubtract(sg_rune->seeds[seed].origin, e->s.origin, fd);
		if (VectorLength(fd) < keep)
			return true;
	}
	return false;
}

static qboolean SG_RunRoom(edict_t *e, int seed0, const int *route_field,
                           vec3_t dir, float want)
{
	vec3_t	chain[SG_PURSUIT_MAX], pp, pend, ch;
	trace_t	tr;
	float	acc = 0.0f, len;
	int		n = 0, cs = seed0;

	if (!sg_rune || cs < 0 || !route_field)
		return false;

	VectorCopy(sg_rune->seeds[cs].origin, chain[n]);
	n++;
	while (n < SG_PURSUIT_MAX && acc < want)
	{
		int			li5, nx5 = -1, nv5 = route_field[cs];
		rune_link_t	*l5;
		vec3_t		sgd;

		if (nv5 >= SG_FIELD_INF)
			break;
		for (li5 = sg_rune->first_link[cs]; li5 >= 0;
		     li5 = sg_rune->next_link[li5])
		{
			l5 = &sg_rune->links[li5];
			if (l5->action != RL_RUN)
				continue;
			if (l5->anchor[0] != 0.0f || l5->anchor[1] != 0.0f ||
			    l5->anchor[2] != 0.0f)
				continue;
			if (route_field[l5->to] < nv5)
			{
				nv5 = route_field[l5->to];
				nx5 = li5;
			}
		}
		if (nx5 < 0)
			break;
		cs = sg_rune->links[nx5].to;
		VectorCopy(sg_rune->seeds[cs].origin, chain[n]);
		VectorSubtract(chain[n], chain[n - 1], sgd);
		sgd[2] = 0.0f;
		acc += VectorLength(sgd);
		n++;
	}

	if (!SG_PursuitPoint(e->s.origin, chain, n, want, pp))
		return false;

	VectorSubtract(pp, e->s.origin, ch);
	ch[2] = 0.0f;
	len = VectorLength(ch);
	if (len < want * SG_AS_CHORD)
		return false;               /* the road bends inside the window */
	if ((ch[0] * dir[0] + ch[1] * dir[1]) / len <
	    cosf(SG_AS_BEND * (float)M_PI / 180.0f))
		return false;               /* and it has to go where we are going */

	/* the room, at the fan's own z-allowance: STEPSIZE, so stairs and
	 * ramps are road and not wall */
	pend[0] = pp[0];
	pend[1] = pp[1];
	pend[2] = e->s.origin[2] + 18.0f;
	tr = gi.trace(e->s.origin, e->mins, e->maxs, pend, e, MASK_PLAYERSOLID);
	/* a teammate is not terrain (the fan's exception, same reason) */
	if (tr.fraction < 1.0f && tr.ent && tr.ent->client && !tr.ent->deadflag)
		return true;
	return tr.fraction >= 1.0f;
}

static void SG_MovePolicy(edict_t *e, usercmd_t *cmd, vec3_t fwd,
                          vec3_t right, vec3_t dir,
                          qboolean open_ahead, qboolean run_link,
                          float frametime, const sg_air_t *air)
{
	float	sp2, sp, toward;
	int		pmf = e->client->ps.pmove.pm_flags;

	if (e->waterlevel > 1 || (pmf & PMF_DUCKED))
		return;

	sp2 = e->velocity[0] * e->velocity[0] + e->velocity[1] * e->velocity[1];
	if (sp2 < 200.0f * 200.0f)
		return;                 /* below this, straight ahead is the fastest
		                         * thing there is: addspeed is wide open */
	sp = sqrtf(sp2);

	/*
	 * The strafe leans off the direction of TRAVEL, so travel has to be
	 * roughly where the route wants to go before leaning off it means
	 * anything. A bot that needs to turn ninety degrees should turn, not
	 * harvest acceleration into the wall it is heading for.
	 */
	toward = (e->velocity[0] * dir[0] + e->velocity[1] * dir[1]) / sp;
	if (toward < 0.5f)
		return;

	if (e->groundentity)
	{
		/*
		 * Tap, never hold: PM_CheckJump sets PMF_JUMP_HELD when it fires and
		 * refuses every jump after it until a command arrives with upmove
		 * under 10. The caller releases after every step.
		 */
		/*
		 * 270, not 320: the audit under honest respawns (waves 84-86,
		 * 45k live samples) read fleet median 199 against a 300 run
		 * speed -- and this gate was the circle. Ground friction caps a
		 * run at 300, the hop is the only way THROUGH 300, and a hop
		 * gated at 320 waits for a speed that running cannot reach. Hop
		 * at the approach to the cap, let the air-strafe harvest do the
		 * exceeding: the carrier control group (median 310, straight
		 * sprints) already proved the ceiling is real.
		 */
		/*
		 * JUMP CHAINING (sg_airstrafe dose 2). Inside a live chain the
		 * gate is not "fast enough to be worth a hop" but "on the ground
		 * at all": PM_CheckJump runs BEFORE PM_Friction and a jump clears
		 * groundentity, which is the condition PM_Friction tests, so the
		 * step that leaves pays nothing and the step that stays pays
		 * speed * 6 * frametime. 270 was chosen to keep bots from hopping
		 * on the way up to running speed; a chain is already through it,
		 * and every ground step it spends waiting is the friction the
		 * whole feature exists to skip. TIME_LAND still binds -- the
		 * engine refuses the jump outright while it is set.
		 */
		if (run_link && open_ahead && !(pmf & PMF_TIME_LAND) &&
		    (sp > 270.0f || (air && air->chain)))
			cmd->upmove = 400;

		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 10.0f);
	}
	else
	{
		/*
		 * THE LANDING TICK (sg_landtick, wave 286+). The think runs at
		 * 10Hz but pmove executes 8 sub-steps per command -- a jump
		 * decided only on frames that BEGIN grounded is a 1-in-8
		 * lottery against a mid-frame touchdown, and every lost draw
		 * pays speed * 6 * ft in friction. The demo census priced it:
		 * bots lose 66 u/s per touchdown (humans 34-46) and chain half
		 * the relaunches of a pub player. The human technique is the
		 * fix: HOLD jump while falling, and the landing sub-step fires
		 * it frictionless (PMF_JUMP_HELD was already released the
		 * frame after the previous hop, so the hold is armed).
		 */
		/* 240, not the ground gate's 270: touch-loss drops the landing
		 * under 270 and disarms the very hold meant to prevent it --
		 * air speed at arm time understates speed at the touchdown the
		 * hold is FOR (wave 289-290 read: relaunches +45% where armed) */
		/* a chain holds the same jump for the same reason, without
		 * needing sg_landtick set: the hold IS the chain */
		if ((sg_cv.landtick->value ||
		     (air && air->chain)) &&
		    run_link && open_ahead &&
		    e->velocity[2] < 0.0f && sp > 240.0f)
			cmd->upmove = 400;

		if (air)
			SG_AirStrafeCmd(cmd, air, e->velocity, sp, frametime);
		else
			SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 1.0f);
	}
}

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
#define SG_DUEL_RANGE_MS	1.5f
#define SG_DUEL_COVER_MS	900.0f

/*
 * What one seed is worth to a bot in a fight, in the same milliseconds
 * Surface_At speaks. Applied to the seed the bot is STANDING on as well as to
 * every candidate: a term that only prices the alternatives makes staying put
 * free, and a bot at the wrong range that finds every step more expensive than
 * standing still is a bot that has been argued into never moving. The current
 * seed is measured from its own origin rather than from the bot's exact
 * position, so both sides of the comparison are the same measurement.
 */
static float Duel_Price(edict_t *e, vec3_t seed_org, vec3_t enemy_org,
                        float want, float expo)
{
	vec3_t	d, eyepoint;
	float	v;

	VectorSubtract(seed_org, enemy_org, d);
	v = SG_DUEL_RANGE_MS * fabsf(VectorLength(d) - want);

	if (expo > 0.0f)
	{
		trace_t tr;

		VectorCopy(seed_org, eyepoint);
		eyepoint[2] += e->viewheight;
		tr = gi.trace(eyepoint, NULL, NULL, enemy_org, e, MASK_OPAQUE);
		if (tr.fraction >= 1.0f)
			v += expo * SG_DUEL_COVER_MS;
	}
	return v;
}

/*
 * The lateral weave. Period per bot so a squad does not oscillate in phase and
 * present one wide target; the spread is 0.4 to 0.85 s, which is fast enough
 * that a 650 u/s rocket aimed where the bot was arrives where it is not, and
 * slow enough that ground friction is not eating the whole reversal. 300 is
 * pm_maxspeed's own wishspeed clamp (the strafe work above uses 400 pre-clamp
 * for direction only; here the magnitude is the point). All three fitted.
 */
#define SG_WEAVE_SIDE		300
#define SG_WEAVE_BASE		0.4f
#define SG_WEAVE_STEP		0.05f
#define SG_WEAVE_HOLD		150.0f	/* a step this short is a stand, not a run */

/*
 * IN-WORLD PLAN DRAWING (sg_drawplan, capability census gap 9).
 *
 * A route defect is obvious to the eye and nearly invisible in a log: an
 * orbit, a chain that doubles back on itself, a belief parked inside a
 * wall. Set sg_drawplan to a bot's client number PLUS ONE (or -1 for every
 * bot) and once a second that bot draws what it has committed to -- a beam
 * from the body to the current link's destination, a second beam on to
 * where the route field goes after that, and a short post at every enemy
 * position its own team still believes in. A spectator then watches the
 * decision instead of reconstructing it.
 *
 * Cost when the cvar is 0, which is always in a real match: one float
 * compare per bot per frame. The cvar itself is read inside, so it is read
 * at most once per bot per second and never on a hot path.
 *
 * The wire format is the mod's own bfg-laser emission, copied from
 * g_weapon.c (bfg_think, the TE_BFG_LASER block): svc_temp_entity, the
 * effect byte, two positions, one multicast. The scope is the single
 * deviation -- MULTICAST_ALL instead of that site's MULTICAST_PHS, because
 * the spectator doing the diagnosing is rarely within earshot of the bot he
 * is watching, and a debug overlay that is off by default can afford it.
 */
static void SG_PlanBeam(vec3_t from, vec3_t to)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BFG_LASER);
	gi.WritePosition(from);
	gi.WritePosition(to);
	gi.multicast(from, MULTICAST_ALL);
}

static void SG_DrawPlan(sg_bot_t *bot, int team, int link,
                        const int *route_field)
{
	edict_t	*e = bot->ent;
	vec3_t	a, b, c;
	int		dp, k, nx = -1;

	dp = (int)sg_cv.drawplan->value;
	if (!dp || !sg_rune || !e || !e->client || !e->inuse)
		return;
	if (dp > 0 && dp - 1 != (int)(e->client - game.clients))
		return;

	/*
	 * The committed link is the one this frame chose; when the final
	 * approach drops the link entirely (the last ten metres are a straight
	 * line) the incumbent is what the bot was last riding, and that is the
	 * honest thing to draw.
	 */
	if (link < 0)
		link = bot->sticky_link;

	VectorCopy(e->s.origin, a);
	a[2] += 16.0f;

	if (link >= 0 && link < sg_rune->hdr.num_links)
	{
		int to = sg_rune->links[link].to;

		VectorCopy(sg_rune->seeds[to].origin, b);
		b[2] += 16.0f;
		SG_PlanBeam(a, b);

		/*
		 * One more step down the same field, by the same first-order
		 * descent the lookahead aim uses -- so the second segment is
		 * the route the bot is about to take rather than a guess at it.
		 */
		if (route_field)
		{
			int li2, nv = route_field[to];

			for (li2 = sg_rune->first_link[to]; li2 >= 0;
			     li2 = sg_rune->next_link[li2])
			{
				if (route_field[sg_rune->links[li2].to] < nv)
				{
					nv = route_field[sg_rune->links[li2].to];
					nx = li2;
				}
			}
		}
		if (nx >= 0)
		{
			VectorCopy(sg_rune->seeds[sg_rune->links[nx].to].origin, c);
			c[2] += 16.0f;
			SG_PlanBeam(b, c);
		}
	}

	/* the belief the route was priced against: one post per sighting still
	 * inside the staleness window (sg_caco.c owns the table) */
	if (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE)
	{
		for (k = 0; k < SG_MAX_ENEMY_TRACK; k++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][k];

			if (en->client < 0 || en->seed < 0 ||
			    en->seed >= sg_rune->hdr.num_seeds)
				continue;
			if (level.time - en->seen_time > SG_BELIEF_STALE)
				continue;
			VectorCopy(sg_rune->seeds[en->seed].origin, b);
			VectorCopy(b, c);
			c[2] += 72.0f;
			SG_PlanBeam(b, c);
		}
	}
}

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


void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	usercmd_t cmd;
	const int *goal_field, *support = NULL, *intercept = NULL;
	const int *route_field;
	qboolean route_pure;
	const sg_weights_t *w;
	sg_role_t role;
	int team, li, bestlink = -1;
	float bestval;
	float incumbent_v = 1e30f;
	vec3_t want, d;
	qboolean carrying;

	/* movement policy state for this frame */
	vec3_t		basis_fwd, basis_right;     /* the basis pmove will build */
	vec3_t		move_dir;                   /* heading the route wants */
	float		view_yaw = 0.0f, view_pitch = 0.0f;
	qboolean	have_move = false;          /* a direction to travel at all */
	qboolean	open_ahead = false;         /* room in front to hop into */
	qboolean	run_link = false;           /* chosen link is ground running */
	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
	qboolean	rally_hold = false;         /* attacker waiting for a partner */
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
	int			sub_steps = 1, sub_msec = 0;
	float		slew_want_y = 0.0f, slew_want_p = 0.0f, slew_rate = 0.0f;

	/* the duel terms, read once per frame and priced per candidate seed */
	qboolean	duel = false;               /* combat has a live or fresh target */
	vec3_t		duel_org;                   /* where it is believed to be */
	float		duel_want = 0.0f;           /* range the weapon in hand wants */
	float		duel_expo = 0.0f;           /* what being seen costs, 0 to ~1 */
	qboolean	duel_hold = false;          /* the chosen step is short: weave */
	short		weave_side = 0;             /* this frame's strafe sign */

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
	/* life ticker for the route-jitter seed */
	if (e->health <= 0)
		bot->was_dead = 1;
	else if (bot->was_dead)
	{
		bot->was_dead = 0;
		bot->lives++;
		bot->inlinks_n = 0;     /* a new life rides in on its own roads */

		/*
		 * THE SPAWN BEAT (sg_spawnbeat, enhancement 7). Watched in
		 * chase-cam, the tell is not the route, it is the START of the
		 * route: the bot materialises and is already at full pace down a
		 * corridor it cannot have looked at yet. A player spawns, checks
		 * a shoulder, and THEN goes -- half a second of orientation that
		 * every human pays and no bot ever did.
		 *
		 * Half a second is the whole feature. The beat is skill-scaled
		 * because the better player pays less of it (0.9s at bot_skill
		 * 0, 0.4s at 4), the cvar is a multiplier on that so the beat
		 * can be widened without touching the ladder, and 0 -- the
		 * default -- is the fleet exactly as it shipped.
		 *
		 * Never on the first spawn of a level: joins already arrive
		 * staggered across their own greeting window, and sixteen bots
		 * all pausing on the opening whistle is a tell of its own.
		 * beat_ready is what makes that test honest (see its field).
		 */
		{
			float	mult = sg_cv.spawnbeat->value;

			if (mult > 0.0f && bot->beat_ready)
			{
				float	sk = (float)SG_CombatSkill(e) / 100.0f; /* 0..4 */
				float	dur = (0.9f - 0.5f * (sk / 4.0f)) * mult;

				if (dur > 2.0f)
					dur = 2.0f;     /* a knob, not a nap */
				bot->beat_from = level.time;
				bot->beat_until = level.time + dur;
				/* 60 to 120 degrees of sweep, stated as its half-width */
				bot->beat_arc = 30.0f + (float)(rand() % 31);
				bot->beat_sign = (rand() & 1) ? 1 : -1;
			}
			else
				bot->beat_until = 0.0f;
		}
	}
	/* the scoreboard ping a human would show from a near-local connection:
	 * stable per-session base with a +/-1 flicker, never outside 5-15
	 * (owner's ruling 2026-08-05: bots blend in everywhere, analytics
	 * included) */
	e->client->ping = bot->fake_ping + (rand() % 3) - 1;
	if (e->client->ping < 5) e->client->ping = 5;
	if (e->client->ping > 15) e->client->ping = 15;
	/* leg ticker: a new role is a new errand -- new opinion of the map */
	if ((int)role != bot->last_role_for_legs)
	{
		bot->last_role_for_legs = (int)role;
		bot->legs++;
	}

	sg_cur_role = role;             /* for the rune identity pricing */
	sg_cur_team = team;
	sg_cur_health = e->health;
	sg_cur_danger = Danger_Field(team);    /* the danger dimension, ours */
	/* downbeat live: attackers march, detours wait for the next bar */
	sg_cur_push = (role == SG_ROLE_ATTACK &&
	               level.time < sg_push_until[team - CTF_TEAM_RED]);
	sg_route_pure_now = route_pure;

	/*
	 * THE RAIL RHYTHM, resolved ONCE for the whole fan (sg_railrhythm).
	 * The candidate loop runs about twenty-five links wide at ten hertz;
	 * scanning the sighting table inside it would pay for the same answer
	 * twenty-five times. Off by default: SG_RailThreat returns false on
	 * the cvar read before it touches anything, and both the pricing term
	 * and the hold below are dead behind rail_seed < 0.
	 *
	 * Four seconds of sighting age, the same freshness the approach-cover
	 * term uses, and eye or ear both count. An ear placement is a region
	 * up to three hundred units wide, which is a room -- coarse for
	 * shooting at and good enough for "do not walk into that doorway
	 * yet", which is the only thing it is asked here.
	 */
	if (SG_RailThreat(team, 4.0f, &rail_client, &rail_seed))
	{
		/* a carrier is what rails punish: 274-279 put rails at the top of
		 * the carrier kill ledger, 2998 damage to rocket-direct's 2317.
		 * The dose it pays for a lit step is half again the rest of the
		 * team's. */
		rail_dose = sg_cv.railrhythm->value *
		            ((role == SG_ROLE_CARRY) ? 1.5f : 1.0f);
	}
	else
	{
		rail_seed = -1;
		rail_client = -1;
	}

	bestval = Surface_At(bot->seed, w, route_field, support, intercept);
	if (duel)
		bestval += Duel_Price(e, sg_rune->seeds[bot->seed].origin, duel_org,
		                      duel_want, duel_expo);
			/* the exposure dimension as a cover prior: a seed the map
			 * says everyone can SEE costs more while hurting, before
			 * any runtime trace confirms who is looking (area_hint,
			 * written by generation; 0 on old runes = no opinion) */
			if (duel_expo > 0.0f)
				bestval += duel_expo *
				    (float)sg_rune->seeds[bot->seed].area_hint * 1.8f;
	/*
	 * THE CARRIER DOES NOT SINK (pit forensics, waves 383-411: 83
	 * unopposed smap05 carries, 87% touched the mid-map basin, 33 ended
	 * "sank like a rock"). The flood's cheapest way out of that basin
	 * fires its long ropes from the BOTTOM of the water (seeds 541/545/
	 * 551/554 at z=-744), so the descent walks a carrier 250 units DOWN
	 * to reach a rope it then has four seconds of air to land. Breath
	 * doctrine is a motor override at the gurgle; it cannot un-choose
	 * the step that spent the air. A wet carrier prices every downward
	 * step out of contention -- but ONLY when some candidate is not
	 * downward, so a genuine one-way underwater tunnel still runs and
	 * no carrier is ever stranded.
	 */
	{
		qboolean sink_ban = false;

		if (role == SG_ROLE_CARRY && bot->seed >= 0 && e->waterlevel > 0)
		{
			int li2;
			float z0 = sg_rune->seeds[bot->seed].origin[2];

			for (li2 = sg_rune->first_link[bot->seed]; li2 >= 0;
			     li2 = sg_rune->next_link[li2])
				if (sg_rune->seeds[sg_rune->links[li2].to].origin[2] >=
				    z0 - 16.0f)
				{
					sink_ban = true;
					break;
				}
		}
		bot->sink_ban = sink_ban;
	}

	/*
	 * ANTI-LINGER (sg_unlinger, rung-4 cut #3). The forensics' surviving
	 * lead after two nulls: bot single-mate contact streaks beside the
	 * carrier run 3-10x longer than human ones (3.85-7.69s vs 0.68-
	 * 1.39s). The mechanism is not attraction -- the role gate and the
	 * support pull both nulled -- it is LINGERING: identical pacing on
	 * identical cheapest roads means a teammate that falls in beside the
	 * carrier simply stays there. Humans pass their carrier constantly
	 * (the relay pattern); they do not co-jog. So the cut is targeted:
	 * a non-escort continuously within 400u of its own carrier for
	 * >1.5s pays a surcharge on links that KEEP it there, until it
	 * separates. Passing stays free; only the co-jog is priced. The
	 * escort is exempt -- lingering is its entire job.
	 */
	{
		qboolean linger_hot = false;
		vec3_t car_org = { 0, 0, 0 };

		if ((sg_cv.unlinger->value > 0.0f ||
		     sg_cv.depace->value > 0.0f) &&
		    role != SG_ROLE_CARRY && role != SG_ROLE_ESCORT)
		{
			static gitem_t *lg_flag;
			edict_t *car = NULL;
			int ci;

			if (!lg_flag)
				lg_flag = FindItem("Enemy Flag");
			if (lg_flag)
				for (ci = 1; ci <= game.maxclients; ci++)
				{
					edict_t *ce = g_edicts + ci;

					if (!ce->inuse || !ce->client || ce == e ||
					    ce->client->ctf.teamnum != team || ce->deadflag)
						continue;
					if (ce->client->pers.inventory[
					        ITEM_INDEX(lg_flag)] > 0)
					{
						car = ce;
						break;
					}
				}
			if (car)
			{
				vec3_t cd;

				VectorCopy(car->s.origin, car_org);
				VectorSubtract(e->s.origin, car_org, cd);
				if (VectorLength(cd) < 400.0f)
				{
					if (bot->linger_since <= 0.0f)
						bot->linger_since = level.time;
					else if (level.time - bot->linger_since > 1.5f)
						linger_hot = true;
				}
				else
					bot->linger_since = 0.0f;
			}
			else
				bot->linger_since = 0.0f;
		}
		else
			bot->linger_since = 0.0f;
		bot->linger_hot = linger_hot;

	for (li = sg_rune->first_link[bot->seed]; li >= 0; li = sg_rune->next_link[li])
	{
		rune_link_t *l = &sg_rune->links[li];
		float v = Surface_At(l->to, w, route_field, support, intercept);
		int b;

		if (linger_hot)
		{
			vec3_t ld9;

			VectorSubtract(sg_rune->seeds[l->to].origin, car_org, ld9);
			if (VectorLength(ld9) < 400.0f)
				v += sg_cv.unlinger->value;
		}

		/*
		 * ROUTE DITHER (sg_routedither, rung-2 set #1 tell #2): the
		 * transition matrices show p=1.0 cells -- at a given seed this
		 * body always makes the identical next choice, and a judge
		 * reads the determinism off the sheet. A human's tie-break
		 * varies. Per-visit pseudo-noise under one hop of gradient
		 * (~125ms at dose 120): ties and near-ties resolve differently
		 * on different visits, the gradient itself never overruled.
		 * The salt rerolls on seed entry so the choice HOLDS within a
		 * visit -- no flip-flop -- and varies across visits.
		 */
		if (sg_cv.routedither->value > 0.0f)
		{
			unsigned dh = bot->dither_salt ^ (unsigned)li * 2654435761u;

			dh ^= dh >> 13; dh *= 2246822519u; dh ^= dh >> 16;
			v += sg_cv.routedither->value *
			     (float)(dh & 1023) / 1023.0f;
		}

		/*
		 * No ropes in the house. Wave 96, watched live: an attacker
		 * spinning in the flag room firing hooks at the walls while the
		 * flag sat unguarded a body-length away -- in-room hook links
		 * ping-pong a bot around the goal minimum, and rope-fire counts
		 * tripled the day the slew made firing cheap. Inside 600ms of
		 * the objective the legs beat any rope ritual; only a fleeing
		 * carrier keeps the choice.
		 */
		if (l->action == RL_HOOK && role != SG_ROLE_CARRY &&
		    goal_field[bot->seed] < 600 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
			continue;
		/*
		 * COVER ON THE APPROACH. lmctf58's attack front dies at 3.3s out
		 * with no stalls and no wedges -- moving freely into a covered
		 * sightline, game after game (waves 112-115). The rune has
		 * carried measured exposure on every seed since the census pass;
		 * it priced cover for hurting duelists only. Now the final
		 * approach pays for visible ground too: an attacker inside 4s of
		 * the goal, and a carrier anywhere on the run home, prefers the
		 * corridor to the courtyard whenever the costs are close.
		 */
		if (role == SG_ROLE_ATTACK && goal_field[bot->seed] < 4000 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
			/* 0.5, not 2.5: the lmctf58 audit caught this surcharge
			 * out-arguing the ~125/hop goal gradient (exposure bytes run
			 * 200+ on open approaches) -- six attackers orbited a pure
			 * flat run to the flag for ten minutes behind a wall made of
			 * preference. A preference stays UNDER the gradient. */
			v += 0.5f * (float)sg_rune->seeds[l->to].area_hint;

		/*
		 * SPREAD THE AXES. Two attackers on the same cheapest gradient
		 * arrive down the same corridor into the same sightline -- the
		 * perimeter maps (lmctf58, mactf06 before the pair-split) eat
		 * that single file forever. En route, the junior of any attacker
		 * pair pays for steps NEAR its senior: pressure splits into two
		 * axes with no explicit corridor model at all, and the sentry's
		 * dilemma starts before the threshold.
		 */
		if (role == SG_ROLE_ATTACK && bot->seed >= 0 &&
		    goal_field[bot->seed] < SG_FIELD_INF &&
		    goal_field[bot->seed] > 2500 && goal_field[bot->seed] < 12000)
		{
			int bi6;

			for (bi6 = 0; bi6 < SG_MAXBOTS; bi6++)
			{
				sg_bot_t *mb6 = &sg_bots[bi6];
				vec3_t md6;

				if (!mb6->active || mb6 == bot || !mb6->ent ||
				    !mb6->ent->inuse)
					continue;
				if (mb6->ent->client->ctf.teamnum != team)
					continue;
				if (mb6->last_role != (int)SG_ROLE_ATTACK)
					continue;
				if ((int)(mb6->ent - g_edicts) >= (int)(e - g_edicts))
					continue;       /* only the junior spreads */
				VectorSubtract(sg_rune->seeds[l->to].origin,
				               mb6->ent->s.origin, md6);
				if (VectorLength(md6) < 400.0f)
				{
					v += 150.0f;    /* was 800: six times the hop
					                 * gradient welded juniors to the
					                 * midfield (same audit) */
					break;
				}
			}
		}
		else if (role == SG_ROLE_CARRY)
			v += 0.4f * (float)sg_rune->seeds[l->to].area_hint; /* was 2.0: same audit */

		if (l->action == RL_HOOK && level.time < bot->hookban_until &&
		    e->waterlevel < 2)
			continue;           /* the rope is confiscated: walk -- but
			                     * never underwater, where walking does
			                     * not exist and the ban was a drowning
			                     * sentence (10 wedge deaths on the
			                     * lmctf05 pool floor, wave 111) */

		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == li && bot->bl_until[b] > level.time)
				break;
		if (b < SG_BL_MAX)
			continue;               /* shelved: the body could not run it */

		/*
		 * A rocket jump is bought with health (the proof's worst-case
		 * price rides in anchor[2]) and needs the launcher and a rocket.
		 * A candidate the body cannot pay for is not a candidate.
		 */
		if (l->action == RL_ROCKETJUMP)
		{
			static gitem_t *rj_rl, *rj_ammo;

			if (!rj_rl)
			{
				rj_rl = FindItem("Rocket Launcher");
				rj_ammo = FindItem("Rockets");
			}
			if (!rj_rl || !rj_ammo ||
			    !e->client->pers.inventory[ITEM_INDEX(rj_rl)] ||
			    e->client->pers.inventory[ITEM_INDEX(rj_ammo)] < 1 ||
			    e->health <= (int)l->anchor[2] + 25)
				continue;
		}

		/*
		 * The carrier's flee doctrine gets ears: a step toward a fresh
		 * believed contact -- seen or heard -- is priced as if it cost
		 * extra travel, up to ~1200ms for walking straight into them.
		 * Everyone else fights; the carrier's job is the capture point.
		 */
		/*
		 * THE WET ROUTE (sg_watercarry, wave 253). The lmctf01 census:
		 * thirteen of thirteen carrier deaths were rails on the dry
		 * corridors, while humans convert 71 percent there by swimming
		 * the moat -- underwater is the one country without railguns.
		 * A carrier prices swim links 800 cheaper; breath doctrine
		 * already owns the drowning risk.
		 */
		if (role == SG_ROLE_CARRY && l->action == RL_SWIM &&
		    sg_cv.watercarry->value)
			v -= 800.0f;

		/* the sink ban's teeth: 12000 exceeds the basin's worst gap
		 * (max eff link 4055 + field spread 2221), so any non-sinking
		 * candidate wins; the pre-pass above guarantees one exists */
		/* widened per the pre-registered fallback (416 forensics: two
		 * carriers FELL dry straight into the cistern -- the wet-trigger
		 * never saw a choice -- then hooked until the air ran out): a
		 * DESTINATION inside water counts as sinking regardless of how
		 * dry the carrier currently is, whenever it is also downward. */
		if ((bot->sink_ban ||
		     (role == SG_ROLE_CARRY &&
		      (l->action == RL_SWIM ||
		       (sg_rune->seeds[l->to].flags & RSF_WATER)))) &&
		    sg_rune->seeds[l->to].origin[2] <
		        sg_rune->seeds[bot->seed].origin[2] - 16.0f)
			v += 12000.0f;

		/*
		 * THE SHELF PAYS ITS CLIFF, at the layer that actually walks
		 * (sg_shelfcost, steal-genesis study). The first cut priced the
		 * waypoint surface and read a flat null in three waves: between
		 * commitments the descent runs on the flood alone, and the flood
		 * happily steps DOWN onto the zero-yield floor under the enemy
		 * stand (101 close approaches there, 91% dead in 1.2s, zero
		 * steals). Fourth cut, per PITTRACE: 74 of 89 pit entries were
		 * plain attack-role link descent, LATERAL at floor height -- and
		 * the field-layer surcharge alone made it worse, because link
		 * selection scores the DESTINATION's potential and the pit basin
		 * stays cheap (its hook out is free by design) while the corridor
		 * around it got dearer. So the movement layer now charges ANY
		 * step whose destination is a masked sub-stand seed, downward or
		 * flat; steps OUT of the pit still pay nothing -- a knocked-in
		 * bot climbs like it means it.
		 */
		if (sg_cv.shelfcost->value > 0.0f)
		{
			int shti = (team == CTF_TEAM_RED) ? 0 : 1;

			if (sg_fields.shelf_cliff[shti] &&
			    sg_fields.shelf_cliff[shti][l->to] > 0 &&
			    !(bot->seed >= 0 &&
			      sg_fields.shelf_cliff[shti][bot->seed] > 0 &&
			      sg_rune->seeds[l->to].origin[2] >
			          sg_rune->seeds[bot->seed].origin[2] + 16.0f))
				/* 60000, not 12000 (fifth cut): the flood surcharge
				 * props the whole low corridor to ~12000+ field units,
				 * so a 12000 step charge on the pit's tiny base was
				 * arithmetically COMPETITIVE with turning back -- the
				 * two layers cancelled at the lip. The step charge must
				 * dominate the field spread it created. */
				v += sg_cv.shelfcost->value * 60000.0f;
		}

		if (role == SG_ROLE_CARRY && l->action == RL_HOOK)
		{
			/*
			 * The carrier's ROPE is not everyone's rope: phase 1 is a
			 * standing aim frame with the flag on its back, and a miss
			 * re-runs the ritual (wave 58: 17s at 10ms/s against a 43%
			 * land rate). But the blanket surcharge overcorrected --
			 * waves 58-66 show five of fourteen carriers dying of the
			 * CLOCK, legs too slow for the long returns, while the rope
			 * at 800 u/s is the fastest thing in clear water. The aim
			 * frame is only deadly when somebody is watching: the
			 * surcharge now applies under fresh contact and stands down
			 * when the country is quiet.
			 */
			int s2;

			for (s2 = 0; s2 < SG_MAX_ENEMY_TRACK; s2++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s2];
				vec3_t pd;

				if (en->client < 0 || en->seed < 0 ||
				    level.time - en->seen_time >= 4.0f)
					continue;
				/*
				 * CLOSE contact only. 'Seen recently anywhere' kept
				 * pursued carriers on their legs the whole run home,
				 * and 32 games of the fast-respawn meta produced zero
				 * captures with rail-armed pursuit running them down.
				 * A pursuer 1500 units back cannot punish a half-second
				 * aim stand -- the rope at 800 u/s GAINS on them. Only
				 * an enemy believed inside 700 makes the standing frame
				 * a real gamble.
				 */
				VectorSubtract(sg_rune->seeds[en->seed].origin,
				               e->s.origin, pd);
				if (VectorLength(pd) < 700.0f)
				{
					/*
					 * THE FAST CARRY (sg_fastcarry, A/B wave 205+).
					 * The human corpus set the bar: a successful
					 * carry is 14 seconds of covering the WHOLE
					 * route, and humans convert 12.8 percent of
					 * steals doing it. Our carriers survive human
					 * lengths (interpose) and cover a third of the
					 * ground -- this 2000ms rope tax under contact
					 * was tuned in the era before escorts, screens,
					 * or the scoop existed to spend it. With a
					 * bodyguard on the line, the aim-stand gamble is
					 * priced at 500: the rope comes back to the run
					 * home.
					 */
					v += sg_cv.fastcarry->value
					     ? 500.0f : 2000.0f;
					break;
				}
			}
		}
		if (role == SG_ROLE_CARRY && bot->carry_startcost < 0 &&
		    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
			bot->carry_startcost = goal_field[bot->seed];

		/*
		 * THE PROGRESS GUARD. Wave 140's carry traces: the 53-second 5v1
		 * carry ran its cost 12800 down to 6400, fell into the pool, and
		 * finished the fight at 8623 -- a third of the route home handed
		 * back in one drop, then a crawl. A carrier that loses ground it
		 * already paid for is off its route (act=-1 frames, 61 of them
		 * that game); the shelf that priced the old position is stale
		 * testimony there. On a 2500-cost regression from the carry's
		 * best: wipe the shelf, re-arm the breakout gauge from here, and
		 * say so in the log. The descent replans from where the body
		 * actually is, not where the plan thought it would be.
		 */
		if (role == SG_ROLE_CARRY && bot->seed >= 0 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
		{
			int cc = goal_field[bot->seed];

			if (bot->carry_bestcost < 0 || cc < bot->carry_bestcost)
				bot->carry_bestcost = cc;
			else if (cc > bot->carry_bestcost + 2500 &&
			         level.time > bot->carry_lost_at + 2.0f)
			{
				int b2, was_best = bot->carry_bestcost;

				bot->carry_lost_at = level.time;
				/* wipe STALE testimony only: a shelf priced at the
				 * old position is hearsay here, but one recorded in
				 * the last three seconds is the body reporting from
				 * where it stands now, and un-shelving those sent
				 * carriers into retry-fail churn (offgraph frames
				 * 0->3->5->12%% across waves 141-144). Shelves live
				 * 120s, so age reads off the expiry. */
				for (b2 = 0; b2 < SG_BL_MAX; b2++)
					if (bot->bl_until[b2] < level.time + 117.0f)
						bot->bl_until[b2] = 0.0f;
				bot->carry_startcost = cc;
				bot->carry_bestcost = cc;
				if (sg_cv.debug->value)
					gi.dprintf("CARRYLOST %s best=%d now=%d org=(%.0f %.0f %.0f)\n",
					           e->client->pers.netname,
					           was_best, cc,
					           e->s.origin[0], e->s.origin[1],
					           e->s.origin[2]);
			}
		}
		if (role == SG_ROLE_CARRY &&
		    !(bot->carry_startcost > 0 && bot->seed >= 0 &&
		      goal_field[bot->seed] < SG_FIELD_INF &&
		      goal_field[bot->seed] >
		          bot->carry_startcost / 2))
		{
			/*
			 * THE BREAKOUT, gauged by STATE, not clock. Wave 69: nine
			 * carries, every one pinned at 0-10% of the way home, the
			 * flee doctrine's own pricing surcharging every exit of a
			 * hot flag room until the argmin oscillated between doors.
			 * A 10-second window (wave 70) freed the ones that broke
			 * fast and re-pinned the ones that didn't -- Trace, 72s at
			 * 1% -- so the clock is gone: the dodge stays silent until
			 * the carrier has actually cleared a quarter of the route
			 * home, and resumes in open country, where it was ever
			 * wise. lmctf05's carrier rode the silent window to 44%,
			 * three times the old ceiling; the gate now follows the
			 * body instead of the wall clock.
			 */
			int s;

			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s];

				if (en->client >= 0 && en->seed >= 0 &&
				    level.time - en->seen_time < 4.0f)
				{
					VectorSubtract(sg_rune->seeds[l->to].origin,
					               sg_rune->seeds[en->seed].origin, d);
					if (VectorLength(d) < 400.0f)
						/* 1.5, was 3.0: this loop now only speaks in
						 * open country past the breakout, where wave
						 * 71's three clock-outs at 64-98% home say the
						 * full dodge tax costs more match clock than
						 * it saves in blood */
						v += 1.5f * (400.0f - VectorLength(d));
				}
			}
		}
		/*
		 * The fighter's two terms, the mirror of the carrier's one.
		 *
		 * Range control: a candidate is priced by how far it puts the bot from
		 * the range the weapon in hand actually wants -- WEAPONS.md 2.1's
		 * ladders, read back out as a distance by SG_CombatDuel.
		 *
		 * Cover: a candidate the target can SEE costs what this bot's own
		 * state says being seen is worth -- near nothing when healthy and in
		 * band, the full 900 ms when hurt or holding the wrong gun for the
		 * distance. One MASK_OPAQUE ray per candidate, the same mask and the
		 * same shape as the sight gate itself (sg_caco.c:100-114). A fan runs
		 * about 25 links wide, and the whole term is skipped on every frame
		 * there is no fight, which is most of them.
		 */
		else if (duel &&
		         !(role == SG_ROLE_ATTACK &&
		           sg_cv.press->value) &&
		         /* CARRIER PRESS (sg_carrypress, wave 280+). The carry
		          * traces (274-279): 61%% of carrier frames make no
		          * homeward progress at ~190 u/s, and 48 of 49 carries
		          * die before the route's final tenth -- the carrier
		          * was still holding duel range against pursuers, the
		          * receding behavior the press cured for attackers in
		          * the only parity-cap A/B ever won. A fleeing carrier
		          * has no business pricing weapon range: forward. */
		         !(role == SG_ROLE_CARRY &&
		           sg_cv.carrypress->value))
		{
			/*
			 * THE PRESS (sg_press, A/B wave 169+). The travel
			 * decomposition (waves 164-168): twenty percent of ALL
			 * attacker distance is spent actively receding from the
			 * goal -- and range control is the suspect with the
			 * motive: an engaged attacker prices its steps toward the
			 * range its weapon wants, which for the long guns means
			 * BACKWARD, and at parity engagement never ends. Under
			 * press, attackers keep the aim, keep the weave, and keep
			 * walking forward; only defenders and escorts hold range
			 * discipline. The escort's 3.1 efficiency against the
			 * attacker's 1.6 was always this contrast.
			 */
			v += Duel_Price(e, sg_rune->seeds[l->to].origin, duel_org,
			                duel_want, duel_expo);
			/* the exposure dimension as a cover prior: a seed the map
			 * says everyone can SEE costs more while hurting, before
			 * any runtime trace confirms who is looking (area_hint,
			 * written by generation; 0 on old runes = no opinion) */
			if (duel_expo > 0.0f)
				v += duel_expo *
				    (float)sg_rune->seeds[l->to].area_hint * 1.8f;
		}

		/*
		 * THE HUMAN PRIOR (sg_humanprior, A/B wave 188+). Fifty-nine
		 * hours of recorded play, log-tiered per link: a candidate on
		 * a human highway prices up to ~380ms cheaper. Humans
		 * concentrate (top 1%% of transitions carry up to 13%% of all
		 * traffic) and their concentration encodes twenty years of
		 * knowing which roads survive contact -- the discount lets
		 * the descent inherit that without a single scripted route.
		 */
		if (sg_human_use &&
		    sg_cv.humanprior->value)
			v -= 1.5f * (float)sg_human_use[li];

		/*
		 * THE FLAG-LIVE PRIOR (sg_flagprior, A/B wave 213+). The
		 * global prior nulled -- but the corpus shows humans run 60%%
		 * DIFFERENT roads while a flag is out (carrywindows census),
		 * and those are the twenty seconds that decide every game.
		 * The discount applies only inside the window the evidence
		 * came from: either flag astray, up to ~380ms off the roads
		 * humans run when it matters.
		 */
		if (sg_human_live &&
		    sg_cv.flagprior->value &&
		    sg_cur_role != SG_ROLE_CARRY &&
		    (sg_caco_team_belief.flag[0].state == SG_FLAG_ASTRAY ||
		     sg_caco_team_belief.flag[1].state == SG_FLAG_ASTRAY))
			/* the cvar IS the dose. Wave 214 (dose 2): carrier route
			 * coverage FELL under the discount -- the window corpus
			 * is hunters' roads, not escapees' (POV-agnostic cut).
			 * The roads go to the roles they came from: hunters
			 * inherit them, the carrier keeps its pure homeward
			 * pricing. */
			v -= 1.5f * sg_cv.flagprior->value *
			     (float)sg_human_live[li];

		/*
		 * DEFENSE DWELL (sg_defpost, wave 286+). The corpus inverted
		 * the stand-freeze doctrine: only 19% of human defensive
		 * standing time is within 250u of the stand -- humans post on
		 * the APPROACHES. Cheap first cut per the extraction's own
		 * sequencing: defenders price steps toward high-dwell seeds
		 * cheaper (their team's plane), same idiom as every prior.
		 */
		if (sg_cur_role == SG_ROLE_DEFEND &&
		    sg_def_post[team - 1] &&
		    sg_cv.defpost->value > 0)
			v -= 1.5f * sg_cv.defpost->value *
			     (float)sg_def_post[team - 1][l->to];

		/*
		 * DEFENSE INTERCEPT (sg_defreact, wave 295+). The response
		 * census, n=1044: on a steal humans leave the post in 0.9s
		 * and run the ESCAPE CORRIDOR toward where the carrier will
		 * be -- aim-at-lead 0.48-0.68 vs aim-at-now ~0. Our defender
		 * already chases the believed CURRENT position (the flag
		 * field re-floods from it); this term bends that pursuit
		 * toward the corpus's learned cut-off seeds while our flag
		 * is astray. Direct chase is 8% of human responses.
		 */
		if (sg_cur_role == SG_ROLE_DEFEND &&
		    sg_def_icept[team - 1] &&
		    sg_caco_team_belief.flag[team - 1].state == SG_FLAG_ASTRAY &&
		    sg_cv.defreact->value > 0)
			v -= 1.5f * sg_cv.defreact->value *
			     (float)sg_def_icept[team - 1][l->to];

		/*
		 * THE ESCAPE PRIOR (sg_escapeprior, wave 284+). The missing
		 * corpus cut: .hml was POV-agnostic and therefore mostly the
		 * HUNTERS' roads (re-tested null twice). This one is only the
		 * flag carrier's own entity trajectory in the 20s after each
		 * steal -- the roads humans actually flee on. Applied to the
		 * carry role alone; cvar value is the dose, same scale as the
		 * other priors (1.5ms per tier point per dose).
		 */
		if (sg_human_escape &&
		    sg_cur_role == SG_ROLE_CARRY &&
		    sg_cv.escapeprior->value > 0)
			v -= 1.5f * sg_cv.escapeprior->value *
			     (float)sg_human_escape[li];

		/*
		 * APPROACH COVER (sg_approachcover, wave 314+). The carry
		 * forensics moved the fight: 90% of early carrier kills came
		 * from defenders ALREADY PARKED within 1000u of the robbed
		 * stand (81% there five seconds before), rail 47% from
		 * grounded shooters at 237u -- and the nearest enemy is 210u
		 * away at the grab. Cover bought after the grab arrives too
		 * late; the line must be chosen on the way IN. Same trace,
		 * same book as the carrier's, applied to the attacker against
		 * every fresh eye sighting near the target stand.
		 */
		if (sg_cur_role == SG_ROLE_ATTACK &&
		    sg_cv.approachcover->value > 0)
		{
			int acs;

			for (acs = 0; acs < SG_MAX_ENEMY_TRACK; acs++)
			{
				sg_belief_enemy_t *aen =
				    &sg_caco_enemies[team - 1][acs];
				vec3_t aeye, athr, aspan;
				trace_t actr;

				if (aen->client < 0 || aen->heard_only ||
				    level.time - aen->seen_time > 4.0f ||
				    aen->seed < 0)
					continue;
				VectorCopy(sg_rune->seeds[l->to].origin, aeye);
				aeye[2] += 22.0f;
				VectorCopy(sg_rune->seeds[aen->seed].origin, athr);
				athr[2] += 22.0f;
				VectorSubtract(athr, aeye, aspan);
				if (VectorLength(aspan) > 900.0f)
					continue;
				actr = gi.trace(aeye, NULL, NULL, athr, e, MASK_SOLID);
				if (actr.fraction >= 1.0f)
				{
					v += sg_cv.approachcover->value;
					break;  /* one exposure is enough to price */
				}
			}
		}

		/*
		 * RAIL COVER (sg_railrhythm). The other half of the counter-play,
		 * and the half that runs when there is no lane to time: a burst
		 * that ENDS somewhere the railer can see is a burst that ends in
		 * front of a loaded gun. The trace is the approach-cover trace --
		 * same eye height, same mask, same 900-unit gate, MASK_SOLID from
		 * the candidate seed to the believed post -- but the sighting was
		 * chosen once for the whole fan above, so this costs exactly one
		 * ray per candidate rather than one per candidate per enemy.
		 *
		 * Every role pays it. Approach cover is an attacker's term and
		 * carrier cover is a carrier's; a rail lane is neither, it is a
		 * fact about the room, and the defender walking back to a post
		 * across it dies the same way. The carrier's dose is the larger
		 * one, folded in where the sighting was resolved.
		 *
		 * A PREFERENCE, not a wall, for the reason the lmctf58 audit
		 * wrote down two terms above: the dose is the cvar's and it
		 * belongs under the ~125/hop goal gradient. There is no branch
		 * here that can make a seed unreachable.
		 */
		if (rail_seed >= 0)
		{
			vec3_t	reye, rthr, rspan;
			trace_t	rtr;

			VectorCopy(sg_rune->seeds[l->to].origin, reye);
			reye[2] += 22.0f;
			VectorCopy(sg_rune->seeds[rail_seed].origin, rthr);
			rthr[2] += 22.0f;
			VectorSubtract(rthr, reye, rspan);
			if (VectorLength(rspan) < 900.0f)
			{
				rtr = gi.trace(reye, NULL, NULL, rthr, e, MASK_SOLID);
				if (rtr.fraction >= 1.0f)
					v += rail_dose;
			}
		}

		/*
		 * CARRIER COVER (sg_carrycover, wave 274+). The 268-273 DMG
		 * ledger: rails are still the carrier's top killer (2998 dmg
		 * to rocket-direct's 2317), fired mostly by GROUNDED defenders
		 * at 135-415 units -- standing shots down clear lines. A rail
		 * needs line of sight; a human carrier buys cover with corners
		 * the way this graph buys speed with links. For the carrier
		 * only, while the team's freshest EYE sighting is under 3s
		 * old, a candidate step the sighted enemy can see costs the
		 * cvar's value in ms extra. One trace per candidate, against
		 * the one sighting that matters most.
		 */
		if (sg_cur_role == SG_ROLE_CARRY &&
		    sg_cv.carrycover->value > 0)
		{
			int			cs, best_cs = -1;
			float		best_t = -1.0f;

			for (cs = 0; cs < SG_MAX_ENEMY_TRACK; cs++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][cs];

				if (en->client >= 0 && !en->heard_only &&
				    level.time - en->seen_time < 3.0f &&
				    en->seen_time > best_t)
				{
					best_t = en->seen_time;
					best_cs = cs;
				}
			}
			if (best_cs >= 0)
			{
				vec3_t	eye, thr, span;
				trace_t	ctr;

				VectorCopy(sg_rune->seeds[l->to].origin, eye);
				eye[2] += 22.0f;
				VectorCopy(sg_rune->seeds[
				    sg_caco_enemies[team - 1][best_cs].seed].origin, thr);
				thr[2] += 22.0f;
				/* range gate (wave 279): the 268-277 ledger kills all
				 * sit inside ~800u -- a sighting across the map must
				 * not bend the route (dose 1200 showed the failure:
				 * 53-second carries that never arrive). */
				VectorSubtract(thr, eye, span);
				if (VectorLength(span) < 900.0f)
				{
					ctr = gi.trace(eye, NULL, NULL, thr, e, MASK_SOLID);
					if (ctr.fraction >= 1.0f)
						/* CLOCKPLAY scales the price, not the rule: a
						 * late lead pays 1.3x for the corner because
						 * the flag only has to get home once more, a
						 * late deficit pays 0.8x because a carrier
						 * still alive at the horn scored nothing.
						 * Exactly 1.0x -- the same float, the same
						 * route -- with sg_clockplay off. */
						v += sg_cv.carrycover->value *
						     Clock_CoverScale(team);
				}
			}
		}

		/*
		 * THE SWITCHING COST (sg_sticky, A/B wave 168+). The owner's
		 * diagnosis, measured: offense converts 1.6 ms of progress per
		 * unit walked against the escort's 3.1 on the same maps -- half
		 * of all offensive walking buys nothing -- and the chosen link
		 * changes every ~2.4 seconds. The surface offers ties, and the
		 * per-frame argmin flips between them: a bot following a LINE
		 * on the gradient, not the gradient. The incumbent route now
		 * holds its seat unless a challenger beats it by 15 percent --
		 * a mind-change gets priced at the moment it is made, which is
		 * the owner's wasted-distance penalty moved to where it can
		 * steer. Shelved, blocked, or completed incumbents pay nothing:
		 * displacement stays free when the route is actually dead.
		 */
		/*
		 * ROUTE JITTER (sg_routejitter, wave 359). The film verdict
		 * chain: rope-vs-brush (calibrated 8/8 judge) -> ribbon v1
		 * (lanes, not a band) -> ribbon v2+dose (pooled null: the
		 * steering re-centers whatever the aim does). The band humans
		 * paint is ROUTE diversity, not in-lane wander: near-optimal
		 * link chains differ per player and per run, where our argmin
		 * rides the single optimum every time. Each bot-life gets a
		 * deterministic per-link pricing tilt (cvar = max percent);
		 * ties and near-ties then split the population across
		 * different roads. Deterministic per life: no per-frame noise,
		 * no flapping -- a LIFE rides one opinion of the map.
		 */
		if (sg_cv.routejitter->value > 0.0f)
		{
			unsigned rj = ((unsigned)li * 2654435761u) ^
			              ((unsigned)(e - g_edicts) * 40503u) ^
			              ((unsigned)(bot->lives + bot->legs) * 9176u);

			rj = (rj >> 4) & 1023u;
			v *= 1.0f + ((float)rj / 1023.0f - 0.5f) * 0.02f *
			     sg_cv.routejitter->value;
		}

		/*
		 * NO IMMEDIATE BACKTRACK (sg_nobacktrack, wave 392 trial). The
		 * smap05 map-center orbit -- and the chronic ~130 suicides a
		 * wave behind it -- is two seeds on a field plateau electing
		 * each other forever at full sprint. The latch (wave 385-390)
		 * pooled null against it: holding a link longer does not help
		 * when the flap is BETWEEN legs. This prices the one link that
		 * returns to the seed just departed, for a few seconds, unless
		 * pricing leaves no other finite way down. A human does turn
		 * around sometimes; a human does not do-si-do.
		 */
		if (li >= 0 && sg_rune->links[li].to == bot->prev_seed &&
		    level.time - bot->prev_seed_time < 3.0f)
			v *= 1.0f + sg_cv.nobacktrack->value / 100.0f;

		/*
		 * NOT THROUGH THERE AGAIN (sg_tilt). The lane the last life
		 * ended in costs a third more for the first twenty-five
		 * seconds of this one -- fifty if the same lane took two
		 * lives inside a minute. A third is deliberately a
		 * PREFERENCE and not a wall: where the map offers a second
		 * road the bot takes it, and where it does not, the tilt
		 * loses to the gradient and the bot walks the only corridor
		 * there is, which is also what the human does after standing
		 * at the respawn swearing about it. Nothing here is
		 * permanent and nothing here is written down: the window
		 * runs out, the lane is forgotten, and the map's real
		 * lessons stay in the danger dimension where they belong.
		 */
		if (bot->tilt_lane_n > 0 && level.time < bot->tilt_until &&
		    sg_cv.tilt->value > 0.0f &&
		    Tilt_InLane(bot, l->to))
		{
			v *= SG_TILT_PRICE;

			/* one line in sixteen: a bot in a two-hop ball prices
			 * every candidate it owns, every frame, and the log is
			 * for reading */
			if (sg_cv.debug->value &&
			    !(bot->tilt_said++ & 15))
				gi.dprintf("TILTAVOID %s link=%d to=%d dseed=%d "
				           "left=%.1f%s\n",
				           e->client->pers.netname, li, l->to,
				           bot->tilt_seed,
				           bot->tilt_until - level.time,
				           (level.time < bot->tilt_caution_until)
				           ? " cautious" : "");
		}

		/*
		 * POST-DEATH CAUTION, the routing half. The approach-cover
		 * term below already teaches an attacker on the last leg to
		 * pay for open ground; for the few seconds after a respawn
		 * EVERY role pays it, at the same dose, whatever it is doing.
		 * A player who just died walks the wall side of the room for
		 * a while -- and that is the whole of the behaviour: he is
		 * not slower, not worse, and not hiding. The willingness half
		 * lives in sg_combat.c, through SG_TiltCaution.
		 */
		if (level.time < bot->tilt_caution_until &&
		    sg_cv.tilt->value > 0.0f)
			v += SG_TILT_COVER * (float)sg_rune->seeds[l->to].area_hint;

		/* EXIT-LANE ASYMMETRY (sg_exitasym). Humans tend to leave by a
		 * different lane than they came in on, but not always -- a coin
		 * flipped once per carry, the dose set by the cvar. */
		if (role == SG_ROLE_CARRY && bot->exitasym_armed)
		{
			int ea;

			for (ea = 0; ea < bot->exitasym_n; ea++)
				if (bot->exitasym_set[ea] == li)
				{
					v *= 1.5f;
					break;
				}
		}

		/*
		 * THE ESCAPE PRIOR (sg_escapeprior). The exit drawn at the
		 * grab, spent here: a candidate that leaves the robbed stand
		 * on the drawn compass bearing is cheaper by the human
		 * probability of that bearing, for the three seconds the
		 * bearings were mined over. The bearing is measured from the
		 * STAND, not from the body -- that is what the corpus
		 * measured, and it keeps the whole first leg pointed at one
		 * exit instead of re-deciding as the carrier drifts.
		 *
		 * Candidates inside 160 units of the stand carry no bearing
		 * worth the name and are left alone -- the same displacement
		 * floor escapepriors.py MIN_RUN_U demanded before it would
		 * believe a human's bearing. The v > 0 guard is for the one
		 * case a multiplicative discount inverts: a candidate the
		 * human-highway prior has already priced below zero would be
		 * made MORE expensive by scaling toward zero.
		 */
		if (role == SG_ROLE_CARRY && bot->escprior_bucket >= 0 &&
		    level.time < bot->escprior_until && v > 0.0f)
		{
			float ex = sg_rune->seeds[l->to].origin[0] - bot->escprior_org[0];
			float ey = sg_rune->seeds[l->to].origin[1] - bot->escprior_org[1];

			if (ex * ex + ey * ey > 160.0f * 160.0f &&
			    SG_Bearing8(ex, ey) == bot->escprior_bucket)
				v *= 1.0f - bot->escprior_dose;
		}

		if (bot->sticky_link == li &&
		    sg_cv.sticky->value)
			v *= 0.85f;

		if (li == bot->sticky_link)
			incumbent_v = v;

		if (v < bestval)
		{
			bestval = v;
			bestlink = li;
		}
	}
	}       /* anti-linger scope */

	/*
	 * THE LINK LATCH (sg_linklatch, wave 289+). The demo census: 87
	 * deg/s of heading noise, a 49% reversal rate, a full 180 every
	 * nine seconds -- a 10Hz argmin flapping across noise-level ties
	 * on a surface whose item terms refresh at 1Hz. The incumbent
	 * keeps its seat for the cvar's milliseconds unless a challenger
	 * beats it by 15%; a dead incumbent (infinite v, no longer offered
	 * from this seed) abdicates immediately. This is the re-decision
	 * cadence matched to the information's own refresh rate.
	 */
	if (sg_cv.linklatch->value > 0 &&
	    bestlink >= 0 && bot->sticky_link >= 0 &&
	    bestlink != bot->sticky_link &&
	    level.time < bot->latch_until &&
	    incumbent_v < 1e29f &&
	    bestval > incumbent_v * 0.85f)
	{
		bestlink = bot->sticky_link;
	}
	else if (bestlink != bot->sticky_link)
	{
		bot->latch_until = level.time +
		    sg_cv.linklatch->value / 1000.0f;
	}
	if (bestlink >= 0 && bestlink != bot->ribbon_link)
	{
		/* new leg: sample the lane offset once and hold it. The film
		 * verdict (calibrated blind judge, 8/8): every traversal lands
		 * on the SAME polyline -- a rope, where humans paint a 50-150u
		 * brush. Per-tick noise would be jitter, not diversity; the
		 * offset must PERSIST across the leg. */
		/* the leg just closed out goes into the exit-lane ring
		 * (sg_exitasym): this rollover is the only true per-link
		 * advance -- the role-change ticker fires far too rarely
		 * to remember an inbound route */
		if (bot->ribbon_link >= 0)
		{
			bot->inlinks[bot->inlinks_n % 16] = bot->ribbon_link;
			bot->inlinks_n++;
		}
		bot->ribbon_link = bestlink;
		bot->ribbon_off = ((float)(rand() % 2001) / 1000.0f - 1.0f) *
		                  sg_cv.ribbon->value;
		bot->ribbon_goal = bot->ribbon_off;
	}
	/* v2 drift: the film judge's verdict on v1 -- a fixed per-leg lane
	 * quantizes into railroads; a human band needs the offset to WANDER
	 * along the run. Low-frequency, trace-clamped downstream. */
	if (level.time >= bot->ribbon_next)
	{
		bot->ribbon_goal = ((float)(rand() % 2001) / 1000.0f - 1.0f) *
		                   sg_cv.ribbon->value;
		bot->ribbon_next = level.time +
		    1.0f + (float)(rand() % 100) / 100.0f;
	}
	bot->ribbon_off += 0.20f * (bot->ribbon_goal - bot->ribbon_off);
	bot->sticky_link = bestlink;

	/*
	 * THE LAST TEN METERS ARE A STRAIGHT LINE. An attacker at the goal
	 * minimum kept arguing with the link graph -- the argmin flaps
	 * between near-equal links and the bot orbits a flag it could
	 * TOUCH (wave 96, live witness: every defender dead, the attacker
	 * spinning beside the stand). Inside 400ms the graph has nothing
	 * left to teach: drop the link and let the aim fall through to the
	 * goal-entity fallback -- a straight walk, a touch, done. The
	 * carrier gets the same grace at its own stand.
	 */
	/*
	 * SEE THE FLAG, GO THROUGH THE FLAG (owner's order, 2026-08-11,
	 * sharpening wave 96): the 400ms cost gate still let a bot steer
	 * at seed centers while the flag stood in plain sight across the
	 * room. Cost is not the trigger anymore -- SIGHT is. An attacker
	 * with line of sight to the standing flag inside 512 drops the
	 * graph immediately and the aim falls through to the flag item
	 * (and through-extension past it). Seeing it is earned perception,
	 * so a visible dropped enemy flag qualifies the same (Rule 19).
	 */
	{
		qboolean flag_los = false;

		if (role == SG_ROLE_ATTACK)
		{
			edict_t *fent = SG_EnemyFlag(team);

			if (fent &&
			    SG_DistXY(fent->s.origin, e->s.origin) < 512.0f &&
			    SG_CanSee(e, fent->s.origin, 16.0f))
				flag_los = true;
		}

		if ((role == SG_ROLE_ATTACK || role == SG_ROLE_CARRY) &&
		    bot->seed >= 0 &&
		    ((goal_field[bot->seed] < SG_FIELD_INF &&
		      goal_field[bot->seed] < 400) || flag_los))
		{
		bestlink = -1;
		bot->terminal = true;

		/*
		 * THE CLEAN GRAB. Fifty-one of fifty-four carriers died at a
		 * median five percent of the way home (waves 111-113) --
		 * grabbing a hot room hands the flag to the respawn stream
		 * within seconds. A human clears the room first. An attacker
		 * inside touch range now holds at the threshold while a
		 * defender is believed alive within 900 of the stand -- combat
		 * runs free from the hold, the room fight happens BEFORE the
		 * grab -- and takes the flag the moment the room dies (the
		 * surge cancels every hold when a defender drops). Ten seconds
		 * caps the patience: a stalemate grab beats no grab.
		 */
		if (role == SG_ROLE_ATTACK)
		{
			int s3, room = 0;

			for (s3 = 0; s3 < SG_MAX_ENEMY_TRACK; s3++)
			{
				sg_belief_enemy_t *en3 = &sg_caco_enemies[team - 1][s3];
				vec3_t dd3;

				/* strict mode remembers twice as long: a sentry who
				 * ducks behind the pedestal for four seconds vanished
				 * from this count while remaining entirely alive, and
				 * the "cleared" room killed its carrier at 5%% of the
				 * route (waves 151-153). Absence of sighting is not
				 * evidence of death; eight seconds is patience, not
				 * paranoia. */
				if (en3->client < 0 || en3->seed < 0 ||
				    level.time - en3->seen_time >=
				        (sg_cv.strictgrab->value
				             ? 8.0f : 4.0f))
					continue;
				VectorSubtract(sg_rune->seeds[en3->seed].origin,
				               e->s.origin, dd3);
				if (VectorLength(dd3) < 900.0f)
					room++;
			}

			/*
			 * THE UNACCOUNTED MAN (strict only). The killer-recency
			 * census (waves 151-154): ten of thirteen carrier killers
			 * had not recently died -- live defenders the sighting
			 * census never saw, not the respawn stream. A room cannot
			 * be SIGHTED clear; but the scoreboard is public: count
			 * the enemy roster, subtract everyone believed anywhere
			 * fresh, and if a man is missing from the ledger, assume
			 * exactly one of the missing is home. The 20s patience
			 * valve still forces the grab eventually.
			 */
			if (sg_cv.strictgrab->value)
			{
				int s8, esz = 0, accounted = 0, i8;

				for (i8 = 0; i8 < game.maxclients; i8++)
				{
					edict_t *pe = g_edicts + 1 + i8;

					if (pe->inuse && pe->client &&
					    pe->client->ctf.teamnum ==
					        ((team == CTF_TEAM_RED) ? CTF_TEAM_BLUE
					                                : CTF_TEAM_RED))
						esz++;
				}
				for (s8 = 0; s8 < SG_MAX_ENEMY_TRACK; s8++)
				{
					sg_belief_enemy_t *en8 =
					    &sg_caco_enemies[team - 1][s8];

					if (en8->client >= 0 &&
					    level.time - en8->seen_time < 8.0f)
						accounted++;
				}
				if (esz > accounted)
					room++;
			}
			bot->last_room = room;
			/*
			 * Hold only when OUTNUMBERED at the stand. Wave 114: mactf06
			 * attackers reached 250 of the flag and stole nothing all
			 * game -- the threshold hold against a single sentry is a
			 * stalemate the sentry wins by existing. One defender: take
			 * the grab and make them turn their back to chase. Two or
			 * more: the room fight first, as before.
			 */
			/*
			 * A/B, waves 118+: ALWAYS fight the room first. The killer
			 * census flipped the theory -- all 93 carrier deaths across
			 * seven waves came from SURVIVORS, zero from the respawn
			 * stream. Grabbing past a live sentry hands them a free rail
			 * into a fleeing back; the sprint never mattered. The fight
			 * happens before the flag moves, at any defender count, and
			 * the surge still grabs the instant one drops.
			 */
			if (room >= 1)
			{
				/*
				 * THE PAIR SPLITS THE SENTRY. When two attackers stand
				 * at the threshold, holding them BOTH just gives the
				 * sentry one target at a time. The lower client index
				 * fights -- holds the sentry's eyes -- and the other
				 * skips the hold entirely and circles to the grab. A
				 * sentry cannot watch both; whichever it picks loses
				 * something. Solo attackers fight first, as the killer
				 * census demands.
				 */
				int bi5, mate_holding = 0;

				for (bi5 = 0; bi5 < SG_MAXBOTS; bi5++)
				{
					sg_bot_t *mb5 = &sg_bots[bi5];

					if (!mb5->active || mb5 == bot || !mb5->ent ||
					    !mb5->ent->inuse)
						continue;
					if (mb5->ent->client->ctf.teamnum != team)
						continue;
					if (mb5->last_role == (int)SG_ROLE_ATTACK &&
					    mb5->last_goalcost >= 0 &&
					    mb5->last_goalcost < 1200 &&
					    (int)(mb5->ent - g_edicts) <
					        (int)(e - g_edicts))
						mate_holding = 1;
				}
				if (!mate_holding)
				{
					if (bot->rally_since <= 0.0f)
						bot->rally_since = level.time;
					if (level.time - bot->rally_since < 10.0f)
						rally_hold = true;
				}

				/*
				 * THE STRICT GRAB (sg_strictgrab 1, A/B wave 151+).
				 * Wave 150's verdict on the current doctrine: parity
				 * carriers die at a median ZERO percent of the route --
				 * at the pedestal -- because both sanctioned grabs
				 * take the flag under live guns: the 10s stalemate
				 * grab, and the pair-split circle-grab into a watched
				 * room. Strict mode holds while ANY defender is
				 * believed alive in the room, mate or no mate, twenty
				 * seconds of patience before conceding to the old
				 * rule. Three 5v5 servers run strict against two on
				 * current; the steals-vs-caps trade decides.
				 */
				if (room >= 1 &&
				    sg_cv.strictgrab->value)
				{
					/*
					 * THE CROWD VALVE (sg_crowdhold, wave 343). The 7v7
					 * forensics: carriers there die at 4.2s median with the
					 * WHOLE route left and 2+ enemies in the room -- the 20s
					 * patience expires into a crowd the room fight can never
					 * clear at that density, and the forced grab is a death
					 * sentence (1 cap in 23 carries; 5v5 converts 36%). With
					 * the valve, patience only concedes while the room holds
					 * at most the cvar's count; a fuller room re-arms the
					 * clock -- no grab into a crowd, ever.
					 */
					if (sg_cv.crowdhold->value > 0 &&
					    room > (int)sg_cv.crowdhold->value)
						bot->strict_since = level.time;
					if (bot->strict_since <= 0.0f)
						bot->strict_since = level.time;
					if (level.time - bot->strict_since < 20.0f)
						rally_hold = true;
				}
				else
					bot->strict_since = 0.0f;

				/*
				 * THE PRE-BREACH BOMB. Threshold duels run 99-58 against
				 * us: a posted rail beats an arriving one, structurally.
				 * The unfair tool has sat in the loadout unthrown all
				 * campaign -- five hand grenades a spawn. During a
				 * threshold fight, cook one and lob it onto the sentry's
				 * believed post THROUGH cover. No line of sight, no duel:
				 * the room softens before the breach.
				 */
				/* THE FLYING COOK (sg_flycook, wave 228): the owner
				 * cooks on approach, not at a standstill -- the last
				 * seconds of the run double as the fuse, a death
				 * drops the live grenade where the fight is, and the
				 * threshold ceremony disappears. The cook engages in
				 * motion inside the approach band; the throw target
				 * stays the stand, which is where the run points
				 * anyway, so the view-pull steers nothing wrong. */
				if (rally_hold &&
				    bot->nade_phase == 0 &&
				    level.time >= bot->nade_next)
				{
					static gitem_t *nades;
					int s7;

					if (!nades)
						nades = FindItem("Grenades");
					if (nades &&
					    e->client->pers.inventory[ITEM_INDEX(nades)] > 0)
					{
						for (s7 = 0; s7 < SG_MAX_ENEMY_TRACK; s7++)
						{
							sg_belief_enemy_t *en7 =
							    &sg_caco_enemies[team - 1][s7];
							vec3_t nd7;
							float nl7;

							if (en7->client < 0 || en7->seed < 0 ||
							    level.time - en7->seen_time >= 5.0f)
								continue;
							VectorSubtract(
							    sg_rune->seeds[en7->seed].origin,
							    e->s.origin, nd7);
							nl7 = VectorLength(nd7);
							if (nl7 > 250.0f && nl7 < 800.0f)
							{
								/* NADEPOP's verdict on the stand doctrine
								 * (wave 140): 25 pops, mean 4847 units
								 * from the nearest enemy, two inside the
								 * blast radius -- the airburst shells a
								 * pedestal nobody stands on. The bomb now
								 * takes a FRESH sighting (under 2s) at
								 * face value and falls back to the stand
								 * only when the belief has gone stale --
								 * the ghost was the wrong target at ten
								 * seconds old, not at one. */
								if (level.time - en7->seen_time < 2.0f)
									VectorCopy(
									    sg_rune->seeds[en7->seed].origin,
									    bot->nade_at);
								else
								{
									edict_t *nf = G_Find(NULL,
									    FOFS(classname),
									    (team == CTF_TEAM_RED)
									        ? "info_flag_blue"
									        : "info_flag_red");

									if (nf)
										VectorCopy(nf->s.origin,
										           bot->nade_at);
									else
										VectorCopy(
										    sg_rune->seeds[en7->seed].origin,
										    bot->nade_at);
								}
								nades->use(e, nades);
								bot->nade_phase = 1;
								bot->nade_until = level.time + 0.5f;
								break;
							}
						}
					}
				}
			}
		}
	}
	}

	/*
	 * Commitment. The composed surface has saddles -- goal one way, a
	 * shotgun the other, health a third, the item terms refreshed every
	 * second -- and a per-frame argmin at a saddle flaps between near-equal
	 * links. Both t2 attackers churned a full match at one such point
	 * (seeds 429/430, the room north of the rotating door). A chosen step
	 * is HELD until it finishes, times out, or gets shelved; the surface
	 * proposes, the body disposes.
	 */
	if (bot->commit_link >= 0 && bot->commit_link < sg_rune->hdr.num_links)
	{
		rune_link_t *cl = &sg_rune->links[bot->commit_link];
		qboolean drop_commit = false;
		int b;

		VectorSubtract(sg_rune->seeds[cl->to].origin, e->s.origin, d);
		if (bot->seed == cl->to || VectorLength(d) < 48.0f)
			drop_commit = true;             /* arrived: step complete */
		/* or overachieved: hook landings scatter up to ~234 units from the
		 * dest seed -- if the field already prices this spot at or below
		 * the destination, the step served its purpose (holding on would
		 * re-fire the hook from its own landing zone; match 6 bounced at
		 * goal 9979 all game doing exactly that) */
		if (goal_field[bot->seed] <= goal_field[cl->to])
			drop_commit = true;
		if (level.time > bot->commit_until)
			drop_commit = true;
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == bot->commit_link &&
			    bot->bl_until[b] > level.time)
				drop_commit = true;
		if (drop_commit)
			bot->commit_link = -1;
		else
			bestlink = bot->commit_link;
	}
	if (bot->commit_link < 0 && bestlink >= 0)
	{
		bot->commit_link = bestlink;
		bot->commit_until = level.time + 3.0f;
	}

	/*
	 * A rail attempt outranks the argmin outright. Wave 53's ledger: 16
	 * RAILTRY, 1 RAILFAIL, 0 RAILWIN -- fifteen attempts silently stood
	 * down because futility and the shelf reshaped the surface mid-walk
	 * and the argmin handed back a different link before the proof's line
	 * got walked. The retry exists precisely because the surface's local
	 * answer failed here; letting the surface interrupt it is circular.
	 */
	if (bot->rail_stage > 0 && bot->rail_link >= 0 &&
	    bot->rail_link < sg_rune->hdr.num_links)
	{
		int b3;
		qboolean shelved = false;

		for (b3 = 0; b3 < SG_BL_MAX; b3++)
			if (bot->bl_link[b3] == bot->rail_link &&
			    bot->bl_until[b3] > level.time)
				shelved = true;
		if (!shelved)
			bestlink = bot->rail_link;
		else
			bot->rail_stage = 0;
	}

	/*
	 * Progress watch. The same link chosen for four seconds while the bot
	 * stays inside a 96-unit ball is an orbit -- a lip behind a railing,
	 * a door the rune cannot see, a ledge the feelers cannot round. The
	 * cause does not matter here: shelve the link for thirty seconds and
	 * the surface reroutes through the next-best gradient. (Field orbited
	 * one drop lip for a full match; the generator fix removes that class,
	 * this removes every class.)
	 */
	/*
	 * Route through a door already known dead: no 4-second trial needed,
	 * the verdict is in. Shelve on sight -- one link per frame drains a
	 * 25-link doorway fan in seconds, where the watch alone drained it
	 * slower than the shelf refilled (Trace, 117 shelves at seed 662,
	 * match 12: a shelve-expire-reshelve treadmill).
	 */
	if (bot->deaddoor_ahead)
	{
		/*
		 * Shelve ONLY a link that actually heads into the dead door. The
		 * first version shelved whatever bestlink was current whenever a
		 * dead door lay on the goal line -- at ten frames a second that
		 * emptied seed 429's whole fan into a 120-second shelf and left
		 * the bot orbiting on link=-1 for 23 aggregate minutes (batch,
		 * ports 28446-49). The goal line pointing at a door is a fact
		 * about the door; it is not a verdict on a link that leaves in
		 * another direction.
		 */
		if (bestlink >= 0)
		{
			vec3_t to_door, to_dest;
			float dy, ly;

			VectorSubtract(bot->deaddoor_spot, e->s.origin, to_door);
			VectorSubtract(sg_rune->seeds[sg_rune->links[bestlink].to].origin,
			               e->s.origin, to_dest);
			dy = atan2f(to_door[1], to_door[0]);
			ly = atan2f(to_dest[1], to_dest[0]);
			dy = dy - ly;
			while (dy > M_PI) dy -= 2.0f * (float)M_PI;
			while (dy < -M_PI) dy += 2.0f * (float)M_PI;
			if (fabsf(dy) < 0.6f)       /* ~35 degrees: the doorway cone */
			{
				int b, oldest = 0;

				for (b = 0; b < SG_BL_MAX; b++)
					if (bot->bl_until[b] < bot->bl_until[oldest])
						oldest = b;
				bot->bl_link[oldest] = bestlink;
				bot->bl_until[oldest] = level.time + 120.0f;
			}
		}
		bot->deaddoor_ahead = false;    /* one frame's verdict, once */
	}

	if (bestlink >= 0 && bestlink == bot->watch_link &&
	    !(role == SG_ROLE_DEFEND && goal_field[bot->seed] < 1500) &&
	    /* 1500, not 400: a PATROLLING defender runs full speed inside a
	     * confined orbit -- Slip circled seed 1704 at 250 u/s, goal 700,
	     * and the 400 cutoff fed the whole patrol to the shelf (iter 44,
	     * lmctf58: 314 firings, defense routes in rags). The patrol
	     * radius is part of the post. */
	    !bot->door_held_last && !bot->mate_block_last)
	{
		/* door_held_last: standing at a door on command is not the link's
		 * failure -- billing it to the link shelved seed 429's whole fan
		 * through the WATCH path even after the fast-drain was gated
		 * (batch 2: 753 attacker-seconds on link=-1) */
		VectorSubtract(e->s.origin, bot->watch_org, d);
		if (VectorLength(d) > 96.0f)
		{
			VectorCopy(e->s.origin, bot->watch_org);
			bot->watch_since = level.time;
		}
		else if (level.time - bot->watch_since > 4.0f)
		{
			int b, oldest = 0;

			for (b = 0; b < SG_BL_MAX; b++)
				if (bot->bl_until[b] < bot->bl_until[oldest])
					oldest = b;
			bot->bl_link[oldest] = bestlink;
			/* an honest traversal failure: 45s. The 120s figure is for
			 * links proven to head into a dead door, nothing else. */
			bot->bl_until[oldest] = level.time + 45.0f;
			if (sg_cv.debug->value)
				gi.dprintf("SHELVE %s link=%d at seed=%d\n",
				           e->client->pers.netname, bestlink, bot->seed);
			bot->watch_link = -1;
		}
	}
	else
	{
		bot->watch_link = bestlink;
		bot->watch_since = level.time;
		VectorCopy(e->s.origin, bot->watch_org);
	}

	/*
	 * The identity watch above cannot see a flap: commit holds a link for
	 * three seconds, the argmin at a saddle then hands back the OTHER
	 * near-equal link, and the four-second clock resets every swap while
	 * the body stands still for minutes (Fiend and Trace, one drop lip
	 * each, the whole of lmctf01 iter 41). This ball is on the body. Parked
	 * eight seconds -> shelve whatever link is current, then one more every
	 * two seconds while still parked: the flap-set at a saddle is two or
	 * three links and drains in seconds, nothing like the doorway-fan
	 * drain this system got burned by (that one shelved at 10Hz).
	 */
	/*
	 * THE REARGUARD. Waves 88-90: nineteen steals, zero captures, ten of
	 * fifteen carriers dead within ten percent of home -- killed in the
	 * flag room by the respawn stream while their escort dutifully
	 * followed them toward the exit, duplicating the carrier's path when
	 * the carrier needed the ROOM plugged behind it. For eight seconds
	 * after a grab, an escort still deep in the enemy base stands and
	 * fights where it is -- combat runs free, navigation holds -- and
	 * the respawn stream meets a gun instead of a fleeing back. Then it
	 * escorts, as before, in country where escorting means something.
	 */
	/*
	 * ...and not only the ESCORT. The route-fraction census (waves
	 * 141-148, 83 parity carries): 77%% of carriers die inside the first
	 * quarter of the route, median at 3%% -- the room, not the road. At
	 * the grab moment the fighter still wears ATTACK, and an attacker's
	 * post-grab field is the enemy flag ON OUR CARRIER'S BACK: it pulls
	 * him into the carrier's wake out the same door, a second target on
	 * one rail line. The hold now catches ATTACK too -- the fighter
	 * plugs the room he is already standing in, which was the pair-split
	 * doctrine's second half all along.
	 */
	if ((role == SG_ROLE_ESCORT || role == SG_ROLE_ATTACK) &&
	    level.time - sg_grab_time[team - CTF_TEAM_RED] < 8.0f &&
	    bot->seed >= 0)
	{
		int *att = (team == CTF_TEAM_RED) ? sg_fields.to_blue_flag
		                                  : sg_fields.to_red_flag;

		/* escorts hold at 3000 as before; an ATTACKER holds only from
		 * INSIDE the room (the threshold fighter reads under ~1200) --
		 * at 3000 the hold would freeze attackers still mid-corridor,
		 * parked on the rail lines they were built to cross */
		if (att && att[bot->seed] < (role == SG_ROLE_ATTACK ? 1500
		                                                    : 3000))
		{
			rally_hold = true;      /* stand and fight: the room is the job */
			/* once per engagement, not per frame: the hold's own
			 * evidence trail -- wave 149 moved no census and nothing
			 * could say whether the plug ever engaged at all */
			if (bot->rally_since <= 0.0f &&
			    sg_cv.debug->value)
				gi.dprintf("PLUG %s role=%d cost=%d\n",
				           e->client->pers.netname, (int)role,
				           att[bot->seed]);
			if (bot->rally_since <= 0.0f)
				bot->rally_since = level.time;
		}
	}

	/*
	 * THE FLAG HANDOFF (sg_handoff, census gap 10). The owner's rulings:
	 * "flag handoff can use drop, but it would be better to use the buzzmod
	 * toss", then "it is another valid way to pass the flag besides drop"
	 * and "we should probably limit the range of the flag toss a bit". A
	 * carrier about to die gives the flag to a teammate who is nearer home
	 * than it is, instead of dying with it in the open and handing the
	 * defense a free return.
	 *
	 * WHAT drop AND toss ACTUALLY DO HERE. They are the same act. "toss
	 * flag" is special-cased in Cmd_ItemToss_f (g_cmds.c) into
	 * ctf_playerdropflag because the flag item's toss slot is NULL; "drop
	 * flag" reaches ctf_playerdropflag through the item's drop slot
	 * (g_items.c). Both end in ctf_TossEnt, which lobs at a FIXED
	 * forward*200 with z=300 -- about 150 units of ground range, not
	 * settable from the command. So the command word is the owner's
	 * preference, and the RANGE CAP below is the whole of "limit the range
	 * a bit": the carrier does not attempt a pass it cannot make. Widening
	 * the lob itself would mean editing ctf_TossEnt, which also throws
	 * runes and every death-drop -- game code, not a bot decision.
	 *
	 * The receiver is priced on the CARRIER'S OWN home field (goal_field is
	 * to_{red,blue}_flag for a CARRY bot, set above and not reassigned for
	 * this role), so "nearer home" means nearer along the route rather than
	 * nearer in a straight line through a wall, and the line of sight is
	 * checked so the flag is not lobbed into a doorframe.
	 */
	if (sg_cv.handoff->value &&
	    role == SG_ROLE_CARRY && goal_field &&
	    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF &&
	    level.time >= bot->handoff_next &&
	    (bot->engaged_last || duel))
	{
		/*
		 * The bail-out health, skill-scaled: skill 4 backs itself to live
		 * and holds the flag down to 35; skill 0 lets go at 60. The low
		 * skill passes EARLIER on purpose -- it is the one least likely to
		 * finish the run, so its flag is worth more in better hands.
		 */
		float	sk = (float)SG_CombatSkill(e) / 100.0f;     /* 0 .. 4 */
		float	hp_thr = 60.0f + (35.0f - 60.0f) * (sk / 4.0f);

		if ((float)e->health < hp_thr)
		{
			int		mi, best = -1;
			int		my_cost = goal_field[bot->seed];
			int		best_cost = my_cost;
			vec3_t	eye;

			VectorCopy(e->s.origin, eye);
			eye[2] += e->viewheight;

			for (mi = 1; mi <= game.maxclients; mi++)
			{
				edict_t	*me = g_edicts + mi;
				vec3_t	md;
				float	mdist;
				int		ms, mc;
				trace_t	mtr;

				if (me == e || !me->inuse || !me->client)
					continue;
				if (me->deadflag || me->health <= 0)
					continue;
				if (me->client->ctf.teamnum != team)
					continue;
				/* observers and the not-yet-joined cannot receive */
				if (!ctf_validateplayer(me, CTF_TEAM_ANYTEAM))
					continue;

				VectorSubtract(me->s.origin, e->s.origin, md);
				mdist = VectorLength(md);
				if (mdist > 350.0f)     /* the cap (owner's ruling) */
					continue;

				ms = Rune_NearestSeed(sg_rune, me->s.origin);
				if (ms < 0 || goal_field[ms] >= SG_FIELD_INF)
					continue;
				mc = goal_field[ms];
				/* beat the carrier by a real margin, not field noise */
				if (mc + 300 >= best_cost)
					continue;

				mtr = gi.trace(eye, NULL, NULL, me->s.origin, e,
				               MASK_SOLID);
				if (mtr.fraction < 1.0f && mtr.ent != me)
					continue;

				best = mi;
				best_cost = mc;
			}

			if (best > 0)
			{
				edict_t	*re = g_edicts + best;
				vec3_t	hd;
				float	hy, hdist;
				char	*word;

				VectorSubtract(re->s.origin, e->s.origin, hd);
				hdist = VectorLength(hd);
				hy = atan2f(hd[1], hd[0]) * 180.0f / (float)M_PI;

				/*
				 * ctf_TossEnt aims the lob with client->v_angle, NOT with
				 * the usercmd -- pmove has not run yet this frame, so
				 * steering by cmd.angles alone would throw the flag along
				 * LAST frame's facing. Set the view angle the release
				 * actually reads (pitch flat, so the arc clears the floor
				 * instead of burying itself), and set the usercmd too so
				 * the body ends the frame facing where it threw.
				 */
				e->client->v_angle[YAW] = hy;
				e->client->v_angle[PITCH] = 0.0f;
				cmd.angles[YAW] = ANGLE2SHORT(hy)
				    - e->client->ps.pmove.delta_angles[YAW];

				/* the owner named both words: near enough to place it in
				 * a mate's hands is a drop, past that it is a throw */
				word = (hdist <= 150.0f) ? "drop" : "toss";
				SG_BotClientCommand((int)(e - g_edicts) - 1,
				                    word, "flag", NULL);

				/*
				 * The carry gauges belong to the carry that just ended --
				 * the same three the grab resets. The role itself follows
				 * next think, because SG_Role derives CARRY from actually
				 * holding the flag.
				 */
				bot->carry_startcost = -1;
				bot->carry_bestcost = -1;
				bot->carry_lost_at = 0.0f;
				bot->handoff_next = level.time + 10.0f;

				if (sg_cv.debug->value)
					gi.dprintf("HANDOFF %s -> %s %s dist=%.0f cost "
					           "%d->%d hp=%d thr=%.0f\n",
					           e->client->pers.netname,
					           re->client->pers.netname, word, hdist,
					           my_cost, best_cost, e->health, hp_thr);
			}
			else if (sg_cv.debug->value &&
			         level.time >= bot->next_report - 0.9f)
				gi.dprintf("HANDOFF %s no receiver hp=%d thr=%.0f\n",
				           e->client->pers.netname, e->health, hp_thr);
		}
	}

	/*
	 * THE RUNE HANDOFF (sg_runetoss, wave 240 -- the owner's recovered
	 * "extremely important": a teammate holding a defensive rune gives
	 * it to the carrier). A bot with RESIST or REGEN, within 300 of our
	 * live carrier who holds nothing better, faces the carrier for one
	 * frame and drops the rune into its path; the carrier's own item
	 * pricing (SG_FC_RUNE) takes it from the floor. One toss per bot
	 * per 20s; combat frames exempt -- a fight is not the moment.
	 */
	if (sg_cv.runetoss->value &&
	    role != SG_ROLE_CARRY && !duel &&
	    e->client->rune &&
	    (e->client->rune->runetype == RUNE_RESIST ||
	     e->client->rune->runetype == RUNE_REGEN) &&
	    level.time >= bot->runetoss_next)
	{
		sg_belief_carrier_t *rc = &sg_caco_team_belief.carrier[team - 1];

		if (rc->client >= 0)
		{
			edict_t *ce = g_edicts + 1 + rc->client;

			if (ce->inuse && ce->client && ce->health > 0 &&
			    (!ce->client->rune ||
			     (ce->client->rune->runetype != RUNE_RESIST &&
			      ce->client->rune->runetype != RUNE_REGEN)))
			{
				vec3_t rd14;

				VectorSubtract(ce->s.origin, e->s.origin, rd14);
				if (sg_cv.debug->value &&
				    level.time >= bot->next_report - 0.9f)
					gi.dprintf("RTCAND %s dist=%.0f\n",
					           e->client->pers.netname,
					           VectorLength(rd14));
				if (VectorLength(rd14) < 400.0f)
				{
					/* face the carrier for the toss frame: the
					 * flick, same as the bomb release */
					float ry = atan2f(rd14[1], rd14[0])
					           * 180.0f / (float)M_PI;

					cmd.angles[YAW] = ANGLE2SHORT(ry)
					    - e->client->ps.pmove.delta_angles[YAW];
					Drop_Rune(e, e->client->rune->item);
					bot->runetoss_next = level.time + 20.0f;
					if (sg_cv.debug->value)
						gi.dprintf("RUNETOSS %s to %s\n",
						           e->client->pers.netname,
						           ce->client->pers.netname);
				}
			}
		}
	}

	/*
	 * HOLD SHORT OF AN UNCAPPABLE STAND (both-flags doctrine, wave 175).
	 * A carrier whose own flag is astray cannot score by touching the
	 * stand -- but it marched there anyway and camped the most
	 * predictable spot on the map until something killed it. Now it
	 * closes to earshot of home (2500 of field) and HOLDS off-stand,
	 * fighting from wherever it stands, until the team returns the
	 * flag; the last steps happen when they can score. The standoff
	 * breaks on exactly one event, and ours must survive to convert it.
	 */
	if (role == SG_ROLE_CARRY &&
	    sg_caco_team_belief.flag[team - 1].state == SG_FLAG_ASTRAY &&
	    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF &&
	    goal_field[bot->seed] < 2500)
	{
		rally_hold = true;
		if (sg_cv.debug->value &&
		    level.time >= bot->next_report - 0.9f)
			gi.dprintf("CARRYHOLD %s cost=%d\n",
			           e->client->pers.netname, goal_field[bot->seed]);
	}

	/*
	 * A railhold clock AHEAD of the level clock is a previous map's
	 * timestamp: level.time restarts at zero on changelevel and sg_bots[]
	 * does not, the same trap the tactical waypoint's tac_time documents.
	 * Stale by definition, and cleared before anything below reads it.
	 */
	if (bot->railhold_since > level.time ||
	    bot->railhold_next > level.time + SG_RAIL_HOLD_GAP)
	{
		bot->railhold_since = 0.0f;
		bot->railhold_next = 0.0f;
		bot->railhold_enemy = -1;
	}

	/*
	 * TIMING THE CROSSING (sg_railrhythm). Last of the holds and
	 * deliberately the weakest of them: everything above -- the room
	 * fight, the plug, the standoff, a defender's post -- is a decision
	 * about the game, and this is a decision about one doorway. It yields
	 * to all of them and never argues with the terminal brake or an item
	 * errand.
	 *
	 * THE SHAPE OF IT. The step the surface just chose enters a believed
	 * railer's sight line, this bot is standing somewhere that same
	 * railer cannot see, and his last heard shot is old enough that the
	 * gun is loaded again. That is the moment a human waits -- not for
	 * long, and not for the shot to be aimed at him. Any rail going off
	 * anywhere opens the window (SG_RailCold reads the shot table, which
	 * the ear stamps for every slug in the PHS), and when it does the
	 * hold releases on the next frame and the crossing happens inside the
	 * reload.
	 *
	 * TWO TRACES, and only when a railer is already known and a step is
	 * already chosen. Both are the approach-cover ray: candidate seed to
	 * believed post, and body to believed post.
	 *
	 * THE CAP IS THE POINT. Patience runs 0.8s at skill 0 to 1.5s at
	 * skill 4, a carrier takes the top of that band, and the wait cannot
	 * be renewed while it is running -- so the worst this feature can
	 * cost a capture is a second and a half of one leg, once, against a
	 * lane that was going to be crossed in front of a loaded rail.
	 */
	if (rail_seed >= 0 && rail_client >= 0 && bestlink >= 0 &&
	    !rally_hold && !precision && bot->lead_ent == 0 &&
	    bot->seed >= 0 &&
	    (bot->railhold_since > 0.0f || level.time >= bot->railhold_next))
	{
		vec3_t	rthr, rstep, rbody;
		trace_t	rtr;

		VectorCopy(sg_rune->seeds[rail_seed].origin, rthr);
		rthr[2] += 22.0f;
		VectorCopy(sg_rune->seeds[sg_rune->links[bestlink].to].origin,
		           rstep);
		rstep[2] += 22.0f;
		VectorCopy(e->s.origin, rbody);
		rbody[2] += e->viewheight;

		/* is the crossing imminent -- does the next step enter his lane? */
		rtr = gi.trace(rstep, NULL, NULL, rthr, e, MASK_SOLID);
		if (rtr.fraction >= 1.0f)
		{
			/* and is there cover to wait in, here, right now? A bot
			 * already standing in his line gains nothing by stopping in
			 * it: waiting in the open is the worst of both. */
			rtr = gi.trace(rbody, NULL, NULL, rthr, e, MASK_SOLID);
			if (rtr.fraction < 1.0f && !SG_RailCold(team, rail_client))
			{
				if (bot->railhold_since <= 0.0f)
				{
					float sk = (float)SG_CombatSkill(e) / 100.0f;  /* 0..4 */

					bot->railhold_since = level.time;
					bot->railhold_patience =
					    (role == SG_ROLE_CARRY)
					        ? 1.5f
					        : 0.8f + (1.5f - 0.8f) * (sk / 4.0f);
					if (sg_cv.debug->value)
						gi.dprintf("RAILHOLD %s at seed=%d waits on "
						           "cl=%d seed=%d patience=%.1f%s\n",
						           e->client->pers.netname, bot->seed,
						           rail_client, rail_seed,
						           bot->railhold_patience,
						           (role == SG_ROLE_CARRY)
						               ? " carrier" : "");
				}
				/* re-stamped every waiting frame, not only at the arm:
				 * if the freshest railer changes identity mid-wait the
				 * release line must name the man actually waited out --
				 * and the patience clock deliberately does NOT restart,
				 * or a room with two railers in it would have no cap */
				bot->railhold_enemy = rail_client;
				if (level.time - bot->railhold_since <
				    bot->railhold_patience)
					rail_hold = true;
			}
		}
	}
	if (!rail_hold && bot->railhold_since > 0.0f)
	{
		/* one line per crossing, and it says which of the two things
		 * ended the wait: the rail going off (the window is open and the
		 * crossing is timed) or the patience running out (humans do not
		 * wait forever, and neither does this) */
		if (sg_cv.debug->value)
			gi.dprintf("RAILCROSS %s waited %.1fs on cl=%d (%s)\n",
			           e->client->pers.netname,
			           level.time - bot->railhold_since,
			           bot->railhold_enemy,
			           SG_RailCold(team, bot->railhold_enemy)
			               ? "window" : "patience");
		bot->railhold_since = 0.0f;
		bot->railhold_enemy = -1;
		bot->railhold_next = level.time + SG_RAIL_HOLD_GAP;
	}

	/*
	 * THE UNSTICK OF LAST RESORT. A rope through a doorway parked a bot
	 * on a wall ledge off the navigable mesh (wave 97, screenshot in
	 * hand) and every clever layer beneath this line -- watchdog,
	 * escape, futility, rail -- churned without physically freeing it.
	 * Fifteen seconds of true zero displacement, standing exempted only
	 * for a defender on post or a rally hold, and the bot does what
	 * every stuck player has done since 1997: kill, respawn, rejoin the
	 * war. A death costs less than a statue.
	 */
	VectorSubtract(e->s.origin, bot->wedge_org, d);
	if (VectorLength(d) > 96.0f)
	{
		VectorCopy(e->s.origin, bot->wedge_org);
		bot->wedge_since = level.time;
	}
	else if (level.time - bot->wedge_since > 15.0f &&
	         !(role == SG_ROLE_DEFEND &&
	           goal_field[bot->seed >= 0 ? bot->seed : 0] < 1500) &&
	         /* A LIVE CARRIER IS NEVER SUICIDED (carry forensics, 791
	          * episodes): 12 of the 19 parity carries that REACHED
	          * within 300u of home ended as WEDGEKILL orbits at the
	          * stand -- zero damage, no enemy inside 900u. The wedge
	          * valve was executing the very carries everything else
	          * exists to produce. The progress guard's shelf wipe is
	          * the carrier's remedy; a death hands the flag back. */
	         role != SG_ROLE_CARRY &&
	         /* and a rail-rhythm wait is the same class of standing as a
	          * rally: parked on purpose, briefly, by a bot that knows
	          * exactly why. It cannot reach fifteen seconds on its own --
	          * 1.5s of wait per 5.5s of refractory -- but a bot must never
	          * be killed for a stand this file asked it to make */
	         bot->railhold_since <= 0.0f &&
	         bot->rally_since <= 0.0f)
	{
		gi.dprintf("WEDGEKILL %s at (%.0f %.0f %.0f)\n",
		           e->client->pers.netname, e->s.origin[0],
		           e->s.origin[1], e->s.origin[2]);
		Cmd_Kill_f(e);
		bot->wedge_since = level.time;
		return;
	}

	VectorSubtract(e->s.origin, bot->stag_org, d);
	if (VectorLength(d) > 96.0f || !bot->nav_drove || bot->engaged_last)
	{
		/*
		 * Not displacement alone: the clock runs ONLY through frames where
		 * navigation was actually driving the legs and no fight owned the
		 * body. The first cut counted every parked second -- corner holds,
		 * duels, posts -- and shelved the fleet's routes to rags in one
		 * wave (iter 43: 784 firings, zero steals anywhere, kills gutted).
		 * Parked-while-driving is the deadlock class; parked-on-purpose
		 * is not a link's fault.
		 */
		VectorCopy(e->s.origin, bot->stag_org);
		bot->stag_since = level.time;
	}
	else if (bestlink >= 0 &&
	         !(role == SG_ROLE_DEFEND && goal_field[bot->seed] < 1500) &&
	    /* 1500, not 400: a PATROLLING defender runs full speed inside a
	     * confined orbit -- Slip circled seed 1704 at 250 u/s, goal 700,
	     * and the 400 cutoff fed the whole patrol to the shelf (iter 44,
	     * lmctf58: 314 firings, defense routes in rags). The patrol
	     * radius is part of the post. */
	         !bot->door_held_last && !bot->mate_block_last &&
	         level.time - bot->stag_since > 8.0f &&
	         level.time >= bot->stag_next)
	{
		int b, oldest = 0;

		/*
		 * A RUN link earns one retry THE PROOF'S WAY before the shelf.
		 * Seed 327's passage is a slit the phantom threads dead-center
		 * from the seed origin -- proofs deviate under 48 units, so no
		 * waypoint, and the feelers deflect off the slit's edges away
		 * from the one line that works (iter 51: 135 firings, zero
		 * waypointed links to store). Rail mode walks to the from-seed
		 * exactly as the proof did, then drives the straight line with
		 * the fan silenced. If THAT fails, the shelf and the futility
		 * lesson follow as before.
		 */
		if (sg_rune->links[bestlink].action == RL_RUN &&
		    bot->rail_link != bestlink)
		{
			bot->rail_link = bestlink;
			bot->rail_stage = 1;
			bot->rail_until = level.time + 4.0f;
			if (sg_cv.debug->value)
				gi.dprintf("RAILTRY %s link=%d seed=%d\n",
				           e->client->pers.netname, bestlink, bot->seed);
			bot->stag_next = level.time + 2.0f;
			VectorCopy(e->s.origin, bot->stag_org);
			bot->stag_since = level.time;
			goto stag_done;
		}
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_until[b] < bot->bl_until[oldest])
				oldest = b;
		bot->bl_link[oldest] = bestlink;
		bot->bl_until[oldest] = level.time + 45.0f;
		bot->stag_next = level.time + 2.0f;
		bot->commit_link = -1;
		SG_TeachLinkFutility(bestlink); /* the LINK failed, not the ground */
		/*
		 * A U-pocket defeats even the side latch: each detour side walks
		 * into the pocket's own wall and the latch just alternates walls
		 * (iter 48: Field, 102 firings at lmctf01 seed 1239, one pocket,
		 * one game). By the time the watchdog fires, local navigation has
		 * definitively failed -- so back OUT along the reverse of the
		 * current facing for 1.2s and retry the approach from open ground.
		 */
		/* jittered: identical retreats produce identical re-approaches,
		 * and an obstacle that beats one line beats it every time */
		bot->escape_yaw = e->s.angles[YAW] + 180.0f + (float)(rand() % 81 - 40);
		bot->escape_until = level.time + 1.0f + (float)(rand() % 9) * 0.1f;
		if (sg_cv.debug->value)
			gi.dprintf("STAGSHELVE %s link=%d at seed=%d\n",
			           e->client->pers.netname, bestlink, bot->seed);
	}
stag_done:
	bot->nav_drove = false;         /* the movement code below re-arms it */
	/* consumed by the watch above; the feelers re-raise it if the body is
	 * still there this frame */
	bot->mate_block_last = false;

	/*
	 * The wide-orbit detector. On arriving at a seed, check the ring: if
	 * this seed was here within 30 seconds and the goal has not improved
	 * since, the route is a cycle -- shelve the link about to be taken
	 * from it. Loops wider than the watch's ball (the lmctf01 carrier's
	 * 250-unit hook triangle) die here; honest revisits (a defender
	 * patrolling, a fight's back-and-forth) pass because their goal
	 * values move or their clocks expire.
	 */
	{
		static int last_seed_seen[SG_MAXBOTS];
		int me = (int)(bot - sg_bots);
		int gv = (goal_field[bot->seed] < SG_FIELD_INF)
		             ? goal_field[bot->seed] : 0x7ffffff;
		int v;

		/* every live entry keeps the best goal the bot has touched since
		 * that visit -- THIS is what distinguishes a loop from a route
		 * that passes a hallway twice. The first version compared the
		 * seed's own field value across visits, which is CONSTANT, and
		 * shelved every second visit on the map (campaign 2: steals
		 * 7 -> 1, lmctf03 shelves 434). */
		for (v = 0; v < SG_VISIT_RING; v++)
			if (bot->visit_seed[v] >= 0 && gv < bot->visit_min[v])
				bot->visit_min[v] = gv;

		if (me >= 0 && me < SG_MAXBOTS && bot->seed != last_seed_seen[me])
		{
			last_seed_seen[me] = bot->seed;
			/*
			 * CARRIERS ONLY. Even with the min-since test, a fighter
			 * repelled by live defense revisits without progress -- that
			 * is resistance, not a bad link, and no signal here can tell
			 * them apart (campaign 3: 4099 fires, 164 a game, routes
			 * shredded map-wide). A carrier's loop loses the flag and its
			 * fights are ones it fled; for the carrier the test is sound.
			 */
			for (v = 0; role == SG_ROLE_CARRY && v < SG_VISIT_RING; v++)
				if (bot->visit_seed[v] == bot->seed &&
				    level.time - bot->visit_time[v] < 30.0f &&
				    level.time - bot->visit_time[v] > 3.0f &&
				    bot->visit_min[v] >= bot->visit_goal[v] &&
				    bestlink >= 0)
				{
					/* back where it was, and it never once got closer
					 * in between: an orbit, whatever its diameter */
					int b, oldest = 0;

					for (b = 0; b < SG_BL_MAX; b++)
						if (bot->bl_until[b] < bot->bl_until[oldest])
							oldest = b;
					bot->bl_link[oldest] = bestlink;
					bot->bl_until[oldest] = level.time + 45.0f;
					if (sg_cv.debug->value)
						gi.dprintf("CYCLE %s seed=%d link=%d\n",
						           e->client->pers.netname, bot->seed,
						           bestlink);
					break;
				}
			bot->visit_seed[bot->visit_head] = bot->seed;
			bot->visit_goal[bot->visit_head] = gv;
			bot->visit_min[bot->visit_head] = gv;
			bot->visit_time[bot->visit_head] = level.time;
			bot->visit_head = (bot->visit_head + 1) % SG_VISIT_RING;
		}
	}

	/*
	 * A carrier must NEVER be stranded by its own shelf: trapped with the
	 * flag is the flag lost. Every link at a finite seed on the shelf and
	 * nothing improving left? Wipe the shelf and retry the least-bad
	 * option -- an orbit risked beats a guaranteed strand (mactf06 g3:
	 * the carrier hung airborne on link=-1 at goal 6684 until the flag
	 * timed out).
	 */
	if (role == SG_ROLE_CARRY && bestlink < 0 &&
	    goal_field[bot->seed] < SG_FIELD_INF)
	{
		int b, any = 0;

		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_until[b] > level.time)
				any++;
		if (any)
		{
			memset(bot->bl_until, 0, sizeof(bot->bl_until));
			if (sg_cv.debug->value)
				gi.dprintf("CLEARSHELF %s (carrying, stranded at %d)\n",
				           e->client->pers.netname, bot->seed);
		}
	}

	/*
	 * A defender that has reached its post stands it. The stand is the
	 * surface's minimum, so descent has nowhere left to go -- pushing
	 * forwardmove into the pedestal just grinds the wall (Caco spent 66
	 * straight seconds at spd=68 doing exactly that). Inside 400ms of the
	 * post: stop, and face the seed an attacker descending on the stand
	 * would arrive through -- the neighbor whose field value sits closest
	 * above this one. Combat still owns the view the moment anyone shows.
	 *
	 * That 400 is the pin radius, and it is therefore the one live number on
	 * this path that means "willingness to hold a post" -- so it is what
	 * camp_tendency scales. Gate widens it by up to 15% and Fiend narrows it
	 * by the same: the camper settles from farther out, the roamer keeps
	 * walking until it is standing on the thing. The errand release below is
	 * untouched, so a needy bot still leaves whatever its persona says. 400
	 * exactly when no persona applies.
	 */
	/*
	 * THE PAD WAIT (sg_itemlead). An errand that has ARRIVED is the same
	 * problem the post solved: the goal field's minimum is the pedestal, and
	 * descent with nowhere left to go grinds the body into it. A player who
	 * came back early does not stand ON the pad either -- he stops short of
	 * it, where the approaches are in front of him rather than behind, and
	 * waits. The standoff is 400ms of field, which at pm_maxspeed is about
	 * 120 units, and the facing below is the post's own: the neighbour an
	 * arrival would descend through, overridden by wherever a fresh contact
	 * says the noise actually is.
	 *
	 * First in the chain, so an errand's hold outranks the defender's -- a
	 * defender on an errand is standing the pad, not the stand, and its
	 * goal field says so.
	 */
	if (bot->lead_ent > 0 && goal_field[bot->seed] < SG_LEAD_STANDOFF)
		hold_post = true;
	else if (role == SG_ROLE_DEFEND && bot->def_stand &&
	    (float)goal_field[bot->seed] < 400.0f * SG_PersonaCampScale(e))
	{
		qboolean quiet = true;
		int s;

		/*
		 * A quiet post permits an errand. Quiet means no believed
		 * contact -- eye or ear -- in six seconds; the errand means the
		 * hold releases and the surface runs, and the surface already
		 * knows the way: the defend objective pulls back toward the
		 * stand, the need-weighted item terms pull toward the armor the
		 * defender is missing, and the sum walks out, grabs, and walks
		 * back without a single scripted step. The hold only pins a
		 * defender who has nothing worth fetching or no peace to fetch
		 * it in.
		 */
		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			if (sg_caco_enemies[team - 1][s].client >= 0 &&
			    level.time - sg_caco_enemies[team - 1][s].seen_time < 6.0f)
				quiet = false;
		if (quiet &&
		    (w->item[SG_FC_ARMOR] > 0.9f || w->item[SG_FC_HEALTH] > 0.9f ||
		     w->item[SG_FC_AMMO] > 0.9f))
			goto no_hold;   /* needy and unthreatened: run the errand */

		hold_post = true;
	}

	/*
	 * The facing, shared by both holds: whichever seed an arrival would
	 * descend on this one through -- the neighbour whose field value sits
	 * closest above ours. Combat still owns the view the moment anyone shows.
	 */
	if (hold_post)
	{
		int facev = 0x7fffffff, face = -1;

		for (li = sg_rune->first_link[bot->seed]; li >= 0;
		     li = sg_rune->next_link[li])
		{
			rune_link_t *l = &sg_rune->links[li];
			int v = goal_field[l->to];

			if (v > goal_field[bot->seed] && v < facev)
			{
				facev = v;
				face = l->to;
			}
		}
		if (face >= 0)
		{
			vec3_t	pdir, peye, pend;
			trace_t ptr;

			VectorSubtract(sg_rune->seeds[face].origin, e->s.origin, d);
			post_yaw = atan2f(d[1], d[0]) * 180.0f / M_PI;

			/*
			 * How far the post can SEE down that approach. WEAPONS.md 2.4-D3
			 * picks the pre-held weapon from this length and nothing else,
			 * because 1.1's spread saturation distances are hard numbers: a
			 * super shotgun is a sub-160 weapon and a railgun is the only
			 * thing that does not degrade past 900. MASK_OPAQUE is the same
			 * mask the sight gate uses (sg_caco.c:100-114), and 2000 is the
			 * engage range combat already refuses to fight beyond.
			 */
			VectorCopy(e->s.origin, peye);
			peye[2] += e->viewheight;
			pdir[0] = cosf(post_yaw * (float)M_PI / 180.0f);
			pdir[1] = sinf(post_yaw * (float)M_PI / 180.0f);
			pdir[2] = 0.0f;
			VectorMA(peye, 2000.0f, pdir, pend);
			ptr = gi.trace(peye, NULL, NULL, pend, e, MASK_OPAQUE);
			post_sight = 2000.0f * ptr.fraction;
		}

		/*
		 * A fresh contact on the belief table -- an ear included -- beats
		 * the static approach guess: face where the noise IS, not where
		 * the map says trouble usually comes from. The pre-held weapon
		 * keeps following post_sight; only the facing swings.
		 */
		{
			int s, best = -1;
			float bestt = 0.0f;

			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s];

				if (en->client >= 0 && en->seed >= 0 &&
				    level.time - en->seen_time < 4.0f &&
				    goal_field[en->seed] < 2500 &&
				    en->seen_time > bestt)
				{
					bestt = en->seen_time;
					best = en->seed;
				}
			}
			if (best >= 0)
			{
				VectorSubtract(sg_rune->seeds[best].origin, e->s.origin, d);
				post_yaw = atan2f(d[1], d[0]) * 180.0f / M_PI;
			}
		}
	}
no_hold:;

	/*
	 * Combat holds a weapon whether or not anyone is in sight, and a posted
	 * defender's is chosen by the sightline above (rule D3b: pre-select, do
	 * not pre-fire -- holding the right weapon costs nothing, raising one
	 * mid-contact costs a full weapon cycle). A negative sightline is "not
	 * posted", which has to be said every frame or a bot that leaves its stand
	 * would keep pre-selecting for a post it no longer holds.
	 */
	SG_CombatPost(e, hold_post ? post_sight : -1.0f);

	/*
	 * Whether this bot may hold a corner on a target it just lost. The role
	 * decides and nothing else: an attacker and a recoverer are already going
	 * that way, a defender may watch a doorway only while it is still on its
	 * own ground -- 2500 ms of the home field, the same order as the post's own
	 * 400 and the pre-spin's 1200 -- and the carrier and its escort never do,
	 * because both have a clock running that a camp does not serve. Said every
	 * frame, so a role change ends a hold on the frame it happens.
	 */
	SG_CombatPursuit(e, (qboolean)(role == SG_ROLE_ATTACK ||
	                               role == SG_ROLE_RECOVER ||
	                               (role == SG_ROLE_DEFEND &&
	                                goal_field[bot->seed] < 2500)));

	/*
	 * The ear (and teammates' eyes) arm everyone else too: a fresh contact
	 * on the belief table within a second and a half of travel means the
	 * idle hand should already hold the weapon that meeting calls for.
	 * Range is estimated by the straight line to the believed seed --
	 * corridors bend it, but a band estimate only has to be right to
	 * within a band.
	 */
	if (!hold_post)
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s];

			if (en->client >= 0 && en->seed >= 0 &&
			    level.time - en->seen_time < 3.0f &&
			    goal_field[en->seed] < SG_FIELD_INF &&
			    sg_fields.item[0] != NULL)   /* fields alive */
			{
				vec3_t ed;
				float dist;

				VectorSubtract(sg_rune->seeds[en->seed].origin,
				               e->s.origin, ed);
				dist = VectorLength(ed);
				if (dist < 1200.0f)
				{
					SG_CombatAlert(e, dist);
					break;
				}
			}
		}
	}

	/*
	 * Chaingun pre-spin (WEAPONS.md 2.4-D3a): the gun fires slow for its
	 * first second of spin-up (p_weapon.c Chaingun frames), so a defender
	 * who believes an enemy is closing on the post -- a teammate SAW one
	 * recently, within ~1200ms of travel by the post's own field -- starts
	 * the barrels before the corner, trading a few bullets for the full
	 * rate at first contact. Belief only: no sighting, no spin.
	 */
	if (hold_post && e->client->pers.weapon)
	{
		static gitem_t *cgitem;
		int s;

		if (!cgitem)
			cgitem = FindItem("Chaingun");
		if (e->client->pers.weapon == cgitem)
			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s];

				if (en->client >= 0 && en->seed >= 0 &&
				    level.time - en->seen_time < 3.0f &&
				    goal_field[en->seed] < 1200)
				{
					cmd.buttons |= BUTTON_ATTACK;
					break;
				}
			}
	}

	/*
	 * The surface has a gradient EVERYWHERE. Where the rune is proven, the
	 * gradient is the best outgoing link. Where it is not -- field infinite,
	 * no improving link, graph hole -- the gradient degrades to the local
	 * one: straight at the goal, deflected around whatever the feelers hit.
	 * A player with no knowledge of the map still runs toward the enemy
	 * base; a bot that stands still because its database has a hole is not
	 * descending a surface, it is worshipping a graph.
	 */
	{
		vec3_t aim;
		qboolean have_aim = false;
		qboolean aim_is_anchor = false;
		qboolean jump_now = false;

		/*
		 * Just let go of a rope: the prover steered forwardmove 400 at the
		 * destination until the phantom grounded (sg_rune.c:529-534), and
		 * the link was only recorded because that landing worked. The body
		 * flies the same approach instead of falling back down the wall it
		 * just climbed.
		 */
		/* rocket-jump phase steps run before the aim is built */
		if (bot->rj_phase)
		{
			static gitem_t *rj_rl2;

			if (!rj_rl2)
				rj_rl2 = FindItem("Rocket Launcher");
			if (level.time > bot->rj_deadline)
				bot->rj_phase = 0;
			else if (bot->rj_phase == 1 && e->client->pers.weapon == rj_rl2)
			{
				bot->rj_phase = 2;
				/* two weapon frames to guarantee the fire state runs */
				bot->rj_fire_until = level.time + 0.25f;
			}
			else if (bot->rj_phase == 2 && level.time > bot->rj_fire_until)
			{
				bot->rj_phase = 3;
				bot->rj_deadline = level.time + 2.5f;
			}
			else if (bot->rj_phase == 3 && e->groundentity)
			{
				bot->rj_phase = 0;
				bot->commit_link = -1;  /* the arc ended; argue fresh */
			}
		}

		/* flying the arc: the landing is the aim, as with a cut rope */
		if (bot->rj_phase == 3)
		{
			VectorCopy(bot->rj_dest, aim);
			have_aim = true;
		}

		/*
		 * THE PRE-TURN (sg_preturn, wave 224). The grammar census: a
		 * human flies with eyes a median 93 degrees off velocity --
		 * the body arrives already facing where it goes NEXT. During a
		 * cut-rope flight the aim slides from the landing spot to the
		 * best onward step from the landing seed, so touchdown exits
		 * at speed instead of standing to re-argue. Ballistics are
		 * unchanged -- only the eyes and the landing basis pre-align.
		 */
		if (sg_cv.preturn->value &&
		    ((bot->hook_phase == 3 && bot->flow_release) ||
		     bot->rj_phase == 3) &&
		    !e->groundentity && sg_rune)
		{
			int ls = Rune_NearestSeed(sg_rune,
			    (bot->rj_phase == 3) ? bot->rj_dest : bot->hook_dest);

			if (ls >= 0)
			{
				int li3, nx3 = -1, nv3;
				const int *rf3 = goal_field;

				nv3 = (rf3[ls] < SG_FIELD_INF) ? rf3[ls] : 0x7fffffff;
				for (li3 = sg_rune->first_link[ls]; li3 >= 0;
				     li3 = sg_rune->next_link[li3])
				{
					rune_link_t *l3 = &sg_rune->links[li3];

					if (l3->action != RL_RUN)
						continue;
					if (rf3[l3->to] < nv3)
					{
						nv3 = rf3[l3->to];
						nx3 = li3;
					}
				}
				if (nx3 >= 0)
				{
					/* one step: the dose curve peaked here (+46%%
					 * chains) and flattened at two (226: the far aim
					 * clips corridor corners). The next tile it is. */
					VectorCopy(
					    sg_rune->seeds[sg_rune->links[nx3].to].origin,
					    aim);
					have_aim = true;
				}
			}
		}

		if (bot->hook_phase == 3)
		{
			/*
			 * APEX CHAINING: hook short of the lip, cut, and at the top
			 * of the throw the next rope is already legal -- the classic
			 * chain (named exactly by the owner). A flow-cut flight ends
			 * its phase at the apex (vertical speed dying, still
			 * airborne), the surface argues the next step from the air,
			 * and if that step is a rope it fires right there.
			 */
			if (bot->flow_release && !e->groundentity &&
			    e->velocity[2] < 60.0f)
			{
				bot->hook_phase = 0;
				bot->flow_release = false;
				bot->commit_link = -1;
				/* ropetravel: a clean apex is a link in the chain --
				 * the next rope is legal on the next beat */
				if (sg_cv.ropetravel->value > 0.0f)
					bot->speedhook_next = level.time + 0.25f;
				if (sg_cv.debug->value)
					gi.dprintf("HOOKEND %s apex\n",
					           e->client->pers.netname);
			}
			else if (e->groundentity || level.time > bot->hook_deadline)
			{
				if (sg_cv.debug->value)
				{
					if (e->groundentity)
					{
						vec3_t ld;

						VectorSubtract(bot->hook_dest, e->s.origin, ld);
						gi.dprintf("HOOKLAND %s dist=%.0f dz=%.0f\n",
						           e->client->pers.netname,
						           sqrtf(ld[0] * ld[0] + ld[1] * ld[1]),
						           ld[2]);
					}
					else
						/* deadline hit still airborne: the throw
						 * never came down anywhere useful */
						gi.dprintf("HOOKEND %s drop\n",
						           e->client->pers.netname);
				}
				bot->hook_phase = 0;
				if (!bot->flow_release)
					bot->hook_landbrake = level.time + 0.3f;
				bot->flow_release = false;
				/* a rope ride ENDS its commitment: wherever this landing
				 * is, the next step is argued fresh from here */
				bot->commit_link = -1;
				/*
				 * HOOK PING-PONG SHELF (sg_hookpong, rung-2 revisit
				 * diagnosis 2026-08-11): 29% of all A-B-A events sit
				 * on three HOOK-heavy spans at 8-45x the human rate --
				 * grapple-decision oscillation, not field flatness,
				 * which is why pricing immediate returns had nothing
				 * to bite on. If this landing puts the body back where
				 * the PREVIOUS ride departed within 8s, the ridden
				 * link joins the shelf ring exactly like a failed
				 * anchor: the pair stops flapping for 45s and the
				 * surface argues a different road.
				 */
				if (sg_cv.hookpong->value > 0.0f &&
				    bot->hp_prev_land > 0.0f &&
				    level.time - bot->hp_prev_land < 8.0f &&
				    bot->hook_link >= 0)
				{
					vec3_t hpd;

					VectorSubtract(e->s.origin, bot->hp_prev_dep, hpd);
					if (VectorLength(hpd) < 250.0f)
					{
						int b9, old9 = 0;

						for (b9 = 0; b9 < SG_BL_MAX; b9++)
							if (bot->bl_until[b9] < bot->bl_until[old9])
								old9 = b9;
						bot->bl_link[old9] = bot->hook_link;
						bot->bl_until[old9] = level.time + 45.0f;
						SG_TeachLinkFutility(bot->hook_link);
						if (sg_cv.debug->value)
							gi.dprintf("HOOKPONG %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
					}
				}
				VectorCopy(bot->hp_cur_dep, bot->hp_prev_dep);
				bot->hp_prev_land = level.time;

				/* ropetravel: a grounded landing chains too -- the beat
				 * is slightly longer than the apex's because the legs
				 * carry a step before the next throw */
				if (sg_cv.ropetravel->value > 0.0f &&
				    e->groundentity && bot->hookfail_streak == 0)
					bot->speedhook_next = level.time + 0.35f;

				/*
				 * A ride that did not SERVE the field failed, and a
				 * failed anchor gets shelved on the spot. Without this,
				 * sibling anchors flap (each landing re-argues, picks
				 * the other, neither converts) and the 4s same-link
				 * watch never fires -- smap05's attackers rode ropes in
				 * place for 180 seconds a game at the water's edge.
				 */
				if (bot->hook_link >= 0 &&
				    bot->hook_link < sg_rune->hdr.num_links &&
				    bot->seed >= 0 &&
				    goal_field[bot->seed] < SG_FIELD_INF)
				{
					rune_link_t *hl = &sg_rune->links[bot->hook_link];

					if (goal_field[hl->to] < SG_FIELD_INF &&
					    goal_field[bot->seed] >
					        goal_field[hl->to] + 300)
					{
						int b, oldest = 0;

						for (b = 0; b < SG_BL_MAX; b++)
							if (bot->bl_until[b] < bot->bl_until[oldest])
								oldest = b;
						bot->bl_link[oldest] = bot->hook_link;
						bot->bl_until[oldest] = level.time + 60.0f;
						/*
						 * Two failed rides in a row and the rope is
						 * CONFISCATED: shelving one anchor at a time
						 * drained a doorway's fan of near-identical
						 * ropes failure by failure while the door
						 * itself stood open a walk away (wave 98,
						 * narrated live in capitals). Twenty seconds
						 * on the legs beats another lap of the wall.
						 */
						bot->hookfail_streak++;
						if (bot->hookfail_streak >= 2)
						{
							bot->hookban_until = level.time + 20.0f;
							bot->hookfail_streak = 0;
							if (sg_cv.debug->value)
								gi.dprintf("HOOKBAN %s 20s\n",
								           e->client->pers.netname);
						}
						if (sg_cv.debug->value)
							gi.dprintf("HOOKFAIL %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
					}
					else
					{
						/* the ride served: forgiveness */
						bot->hookfail_streak = 0;
					}
				}
				bot->hook_link = -1;
			}
			else
			{
				VectorCopy(bot->hook_dest, aim);
				have_aim = true;
			}
		}

		if (!have_aim && bestlink >= 0)
		{
			rune_link_t *l = &sg_rune->links[bestlink];

			VectorCopy(sg_rune->seeds[l->to].origin, aim);
			if (sg_cv.ribbon->value > 0.0f &&
			    l->action == RL_RUN && bot->ribbon_off != 0.0f)
			{
				vec3_t rdir, roff, rprobe;
				trace_t rtr;

				VectorSubtract(aim, e->s.origin, rdir);
				rdir[2] = 0.0f;
				if (VectorLength(rdir) > 32.0f)
				{
					float rl = VectorLength(rdir);

					roff[0] = -rdir[1] / rl * bot->ribbon_off;
					roff[1] = rdir[0] / rl * bot->ribbon_off;
					roff[2] = 0.0f;
					VectorAdd(aim, roff, rprobe);
					rtr = gi.trace(e->s.origin, e->mins, e->maxs, rprobe,
					               e, MASK_PLAYERSOLID);
					/* the corridor decides: a blocked offset collapses to the
					 * seed line -- the band is only as wide as the room */
					if (rtr.fraction >= 1.0f)
						VectorCopy(rprobe, aim);
				}
			}
			have_aim = true;

			/*
			 * THE LOOKAHEAD (sg_lookahead, A/B wave 178+). The Brownian
			 * hunt's terminal finding: unopposed attackers churn their
			 * COMMANDED heading 81 degrees a second while goals, seeds,
			 * links, and dodges all measure stable -- the body servos on
			 * the center of a seed it is about to overrun, overshoots,
			 * turns back, orbits. A runner does not stare at his next
			 * footprint; he looks down the road. When the immediate seed
			 * is close (inside 160) and the route continues, the aim
			 * slides one RUN link further down the gradient: the seed
			 * underfoot becomes something passed THROUGH, the heading
			 * derives from where the route goes next, and the fan still
			 * owns the last arm's-length via precision mode. Only plain
			 * runs chain -- hooks, jumps, drops keep exact aim, their
			 * geometry is the point.
			 */
			if (sg_cv.lookahead->value &&
			    !sg_cv.pursuit->value &&
			    l->action == RL_RUN && !precision)
			{
				vec3_t nd0;

				VectorSubtract(sg_rune->seeds[l->to].origin,
				               e->s.origin, nd0);
				nd0[2] = 0.0f;
				if (VectorLength(nd0) < 160.0f)
				{
					int li2, nx = -1, nv = route_field[l->to];

					for (li2 = sg_rune->first_link[l->to];
					     li2 >= 0; li2 = sg_rune->next_link[li2])
					{
						rune_link_t *l2 = &sg_rune->links[li2];

						if (l2->action != RL_RUN)
							continue;
						if (route_field[l2->to] < nv)
						{
							nv = route_field[l2->to];
							nx = li2;
						}
					}
					if (nx >= 0)
						VectorCopy(
						    sg_rune->seeds[sg_rune->links[nx].to].origin,
						    aim);
				}
			}
			/*
			 * A RUN link with a stored waypoint is one whose proof had to
			 * ROUND something -- the oracle's detour apex lives in the
			 * anchor (empty since the format was born, now earning rent).
			 * Steer via it until it is done, then at the destination; the
			 * fan still handles the last arm's-length. This is the body
			 * finally walking the line the proof actually walked, instead
			 * of the chord the proof never claimed.
			 */
			if (l->action == RL_RUN &&
			    (l->anchor[0] != 0.0f || l->anchor[1] != 0.0f ||
			     l->anchor[2] != 0.0f))
			{
				vec3_t wd;

				VectorSubtract(l->anchor, e->s.origin, wd);
				wd[2] = 0.0f;
				if (VectorLength(wd) > 48.0f)
				{
					VectorCopy(l->anchor, aim);
					aim_is_anchor = true;
				}
			}

			/*
			 * PURE PURSUIT (sg_pursuit, wave 311+). The demo census
			 * reconstructed the COMMANDED heading from the rune alone:
			 * aiming at the next seed center churns 68-78 deg/s at a
			 * 42-45%% reversal rate -- the whole of the Brownian walk,
			 * before the fan or combat touch it -- because the target
			 * sits a median 54-60 units out and jumps 3.2 times a
			 * second. Where the chain is GEOMETRICALLY STRAIGHT the
			 * body still turns 40 deg/s. The same reconstruction on a
			 * point held 300 units down the chain reads 36-43 deg at
			 * 29-31%%. The seed centers are beads on a road; steer at
			 * the road. The cvar value IS the arc distance.
			 */
			if (sg_cv.pursuit->value > 0.0f &&
			    !aim_is_anchor && l->action == RL_RUN && !precision &&
			    e->waterlevel < 2 && bot->hook_phase == 0 &&
			    bot->rj_phase == 0)
			{
				vec3_t	chain[SG_PURSUIT_MAX];
				float	look = sg_cv.pursuit->value;
				int		nchain = 0, cs = l->to, k;
				float	acc = 0.0f;

				VectorCopy(sg_rune->seeds[cs].origin, chain[nchain]);
				nchain++;
				while (nchain < SG_PURSUIT_MAX && acc < look)
				{
					int li5, nx5 = -1, nv5 = route_field[cs];
					vec3_t sgd;

					if (nv5 >= SG_FIELD_INF)
						break;
					for (li5 = sg_rune->first_link[cs]; li5 >= 0;
					     li5 = sg_rune->next_link[li5])
					{
						rune_link_t *l5 = &sg_rune->links[li5];

						/* plain runs only: jumps/ropes/drops are
						 * geometry executed exactly, and a pursuit
						 * point across a gap steers off the lip */
						if (l5->action != RL_RUN)
							continue;
						/* a link whose proof had to ROUND something is
						 * not a chord -- its anchor IS the route */
						if (l5->anchor[0] != 0.0f ||
						    l5->anchor[1] != 0.0f ||
						    l5->anchor[2] != 0.0f)
							continue;
						if (route_field[l5->to] < nv5)
						{
							nv5 = route_field[l5->to];
							nx5 = li5;
						}
					}
					if (nx5 < 0)
						break;
					cs = sg_rune->links[nx5].to;
					VectorCopy(sg_rune->seeds[cs].origin, chain[nchain]);
					VectorSubtract(chain[nchain], chain[nchain - 1], sgd);
					sgd[2] = 0.0f;
					acc += VectorLength(sgd);
					nchain++;
				}

				/*
				 * THE CORNER GUARD. A chord held 300 units out crosses
				 * whatever stands inside the bend. The fan's own
				 * player-box trace, run to the pursuit point; if the box
				 * does not fit, walk the point back down the chain a
				 * vertex at a time. At k == 1 the point IS the seed
				 * center -- the guard can only cost the improvement,
				 * never the safety.
				 */
				for (k = nchain; k >= 1; k--)
				{
					vec3_t pp, pend;
					trace_t ptr;

					if (!SG_PursuitPoint(e->s.origin, chain, k, look, pp))
						continue;
					pend[0] = pp[0];
					pend[1] = pp[1];
					/* sg_pursuitz: chord z-allowance. 8 vetoed every
					 * stair and ramp (311: guard collapsed the chord
					 * to the seed center most ticks); 18 is STEPSIZE */
					pend[2] = e->s.origin[2] +
					          (sg_cv.pursuitz->value);
					ptr = gi.trace(e->s.origin, e->mins, e->maxs, pend,
					               e, MASK_PLAYERSOLID);
					/* a teammate is not terrain, and a door is not a
					 * wall (the fan's two exceptions, same reasons) */
					if (ptr.fraction < 1.0f && ptr.ent &&
					    ((ptr.ent->client && !ptr.ent->deadflag) ||
					     (ptr.ent->classname &&
					      strncmp(ptr.ent->classname, "func_door",
					              9) == 0)))
						ptr.fraction = 1.0f;
					if (ptr.fraction >= 1.0f)
					{
						VectorCopy(pp, aim);
						/* guard census: k==nchain is the full chord,
						 * k==1 is a collapse to today's behavior */
						if (sg_cv.debug->value &&
						    level.time >= bot->next_report - 0.9f)
							gi.dprintf("PURSUITK %s k=%d n=%d\n",
							           e->client->pers.netname,
							           k, nchain);
						break;
					}
				}
			}
			/*
			 * EDGERIDE (sg_edgeride, owner ruling on Open Question #2).
			 * The forensics: 62% of the last rung-2 tell is human feet
			 * 10-40u past the seed edge on walkway margins. Ribbon
			 * offsets died here twice because lookahead and pursuit
			 * OVERWRITE the aim after the offset lands ("the steering
			 * re-centers whatever the aim does"). So the offset is
			 * re-applied LAST, to the final road point, per-leg side
			 * and amplitude from the ribbon state scaled to the cvar.
			 * The wall trace still collapses a blocked offset; the
			 * feeler fan and every fall guard run downstream untouched
			 * -- feet ride AT the safety boundary, never past it.
			 * Carrier exempt. Falls are the trial's kill switch.
			 */
			if (sg_cv.edgeride->value > 0.0f &&
			    l->action == RL_RUN && !precision &&
			    e->groundentity && bot->hook_phase == 0 &&
			    sg_cur_role != SG_ROLE_CARRY &&
			    bot->ribbon_off != 0.0f)
			{
				vec3_t edir, eoff, eprobe;
				trace_t etr;
				float el, esc;

				VectorSubtract(aim, e->s.origin, edir);
				edir[2] = 0.0f;
				el = VectorLength(edir);
				if (el > 48.0f)
				{
					esc = sg_cv.edgeride->value /
					      ((sg_cv.ribbon->value
					        > 0.0f)
					       ? sg_cv.ribbon->value
					       : 48.0f);
					eoff[0] = -edir[1] / el * bot->ribbon_off * esc;
					eoff[1] = edir[0] / el * bot->ribbon_off * esc;
					eoff[2] = 0.0f;
					VectorAdd(aim, eoff, eprobe);
					etr = gi.trace(e->s.origin, e->mins, e->maxs,
					               eprobe, e, MASK_PLAYERSOLID);
					if (etr.fraction >= 1.0f)
						VectorCopy(eprobe, aim);
				}
			}

			if (l->action == RL_JUMP && e->groundentity)
			{
				/*
				 * A momentum link's proof entered at 320 u/s and jumped
				 * off that speed; hopping without it lands in the gap.
				 * Hold the run until the body carries most of what the
				 * envelope claims (from-rest links claim zero and hop
				 * as they always did).
				 */
				float jh = sqrtf(e->velocity[0] * e->velocity[0] +
				                 e->velocity[1] * e->velocity[1]);

				if (jh >= (float)(l->min_speed * 4) * 0.8f)
					jump_now = true;
			}
			/* the landing hop belongs on running ground, not on a link
			 * whose traversal is itself a jump, a drop, a rope or a swim */
			if (l->action == RL_RUN)
			{
				run_link = true;

				/*
				 * THE SPEED HOOK -- the tech that made LMCTF movement an
				 * art: a short rope thrown high-ahead on an open runway,
				 * ridden three tenths of a second to the pull's 800 u/s,
				 * released, momentum kept. Fired ON THE RUN (no brake, no
				 * standing frame -- the anchor sits near the current view
				 * line so the slew arrives in a step or two), gated to
				 * long clear stretches when nobody is engaged and the
				 * legs are the bottleneck. Cooldown keeps it a burst,
				 * not a lifestyle.
				 */
				/*
				 * FREERIDE (sg_freeride, rung-2 off-graph tell, owner
				 * signed 2026-08-07). The speed hook was already the
				 * right tech; the judges' 3-18% human off-graph mass
				 * against our 0.026 was a DOSE gap: the burst was gated
				 * to the far half of the map (goal_field > 4000), one
				 * rope per 4s, and a straight-ahead probe only. Under
				 * freeride the runway reaches the approach (> 2000 --
				 * the no-ropes-in-the-house rule still owns the flag
				 * room), the cooldown halves, the speed window's top
				 * lifts to 560 (humans chain ropes off fast runs), and
				 * a missed straight probe retries once at +/-22 deg --
				 * the corner sling. Everything else -- persona taste,
				 * the worth bar, the plane test, hookban -- unchanged.
				 */
				if (bot->hook_phase == 0 && !bot->engaged_last &&
				    level.time >= bot->hookban_until &&
				    level.time >= bot->speedhook_next &&
				    e->groundentity && e->waterlevel == 0 &&
				    goal_field[bot->seed] >
				        ((sg_cv.freeride->value > 0.0f ||
				          sg_cv.ropetravel->value > 0.0f)
				             ? 2000 : 4000))
				{
					/*
					 * ROPE-PRIMARY TRAVEL (sg_ropetravel, rung-2's
					 * named blocker): humans TRAVEL by rope -- 3-18%
					 * of their samples are flight -- and the
					 * arithmetic killed every occasional-rope cut
					 * (0.3% of player time at doubled volume). Under
					 * ropetravel the successful ride's cooldown drops
					 * to a 0.25s chain beat (set at the landing/apex
					 * sites), the speed ceiling lifts to 700 so fast
					 * landings chain, and the apex-chaining machinery
					 * already in the tree does the rest. The rope
					 * becomes the gait; legs become the connector.
					 */
					float hsp2 = e->velocity[0] * e->velocity[0]
					           + e->velocity[1] * e->velocity[1];
					float hcap = (sg_cv.ropetravel->value > 0.0f) ? 700.0f :
					             (sg_cv.freeride->value
					              > 0.0f) ? 560.0f : 480.0f;

					if (hsp2 > 220.0f * 220.0f && hsp2 < hcap * hcap)
					{
						vec3_t hd, heye, hend;
						trace_t htr;
						float hyaw;
						int hfan;

						VectorSubtract(aim, e->s.origin, hd);
						hyaw = atan2f(hd[1], hd[0]);
						heye[0] = e->s.origin[0];
						heye[1] = e->s.origin[1];
						heye[2] = e->s.origin[2] + e->viewheight;
						/*
						 * WANDER THROW (sg_ropetravel 2). The wave
						 * reports killed the volume theory: fleet
						 * bots already land ~600 ropes a game and
						 * off-graph never moves, because proven-link
						 * flight rides exactly the air the node cloud
						 * was mined from. The human 0.03-0.18 is
						 * IDIOSYNCRATIC flight -- one-off arcs through
						 * space nobody routes through. Dose 2: one
						 * throw in ~7 widens the fan to +/-60 degrees
						 * and takes the arc for its own sake.
						 */
						int hwander =
						    (sg_cv.ropetravel->value
						     >= 2.0f && (rand() % 7) == 0);

						for (hfan = 0; hfan < 3; hfan++)
						{
						float hy2 = hyaw + (hwander
						    ? ((hfan - 1) * 1.05f)
						    : ((hfan == 1) ? 0.384f :
						       (hfan == 2) ? -0.384f : 0.0f));
						/*
						 * FREERIDE v2 (sg_freeride 2, rung-2 set #2:
						 * failed 16/18 on the same off-graph tell).
						 * v1 doubled ride volume and moved NOTHING,
						 * because a 30-degree rope skims the corridor
						 * and its flight never leaves the node cloud;
						 * human off-graph mass is HIGH arcs through
						 * open room air. v2 probes ~54 degrees up:
						 * shorter reach, higher anchor, and the swing
						 * itself is the off-graph flight.
						 */
						float hfar = (sg_cv.freeride->value >= 2.0f) ? 300.0f : 480.0f;
						float hup  = (sg_cv.freeride->value >= 2.0f) ? 420.0f : 280.0f;

						hend[0] = heye[0] + cosf(hy2) * hfar;
						hend[1] = heye[1] + sinf(hy2) * hfar;
						hend[2] = heye[2] + hup;    /* v1 ~30deg, v2 ~54deg */
						htr = gi.trace(heye, NULL, NULL, hend, e,
						               MASK_SOLID);
						/*
						 * The bar the optional rope has to clear, and the
						 * one number on this gate that is a PREFERENCE
						 * rather than a fact -- the speed window and the
						 * ground test are physics, but "is 170 units of
						 * rope worth the throw" is taste. Slip's taste is
						 * a shorter rope more often; Vore's is to keep
						 * running. Divided, not multiplied: enthusiasm
						 * LOWERS the bar. The cooldown below moves with
						 * it so an eager bot also comes back to it
						 * sooner, and both are 1.0 with no persona.
						 */
						if (htr.fraction >= 1.0f || htr.startsolid ||
						    htr.fraction * 560.0f <=
						        170.0f / SG_PersonaHookScale(e) ||
						    htr.plane.normal[2] >= 0.7f)
						{
							/* the side probes exist only under
							 * freeride; stock behavior is one look */
							if (sg_cv.freeride->value
							    > 0.0f ||
							    sg_cv.ropetravel->value > 0.0f)
								continue;
							break;
						}
						{
							/* wave 218 (sg_legcarrier): the burst is
							 * the OPTIONAL rope -- a standing aim for
							 * speed legs already have. The carrier
							 * keeps climb ropes and loses the
							 * ceremony; everyone else bursts on. */
							if (!(sg_cv.legcarrier->value &&
							      sg_cur_role == SG_ROLE_CARRY))
							{
								VectorCopy(htr.endpos, bot->hook_anchor);
								VectorCopy(aim, bot->hook_dest);
								VectorCopy(e->s.origin, bot->hp_cur_dep);
								VectorCopy(e->s.origin, bot->hp_cur_dep);
						bot->hook_phase = 1;
								bot->hook_deadline = level.time + 1.0f;
								bot->speedhook = true;
								bot->speedhook_next = level.time
								    + ((sg_cv.ropetravel->value > 0.0f) ? 1.0f :
								       (sg_cv.freeride->value > 0.0f) ? 2.0f : 4.0f)
								      / SG_PersonaHookScale(e);
							}
						}
						break;
						}
					}
				}
			}

			/*
			 * A hook link executes the way the rune proved it: aim at the
			 * STORED anchor, fire, ride the flat-800 pull, release near
			 * the destination or inside the brake band (the p_weapon.c
			 * ladder starts at 120), then steer the fall onto the landing.
			 * The view is the aim: LMCTF's Weapon_Hook_Fire fires along
			 * v_angle.
			 */
			if (l->action == RL_HOOK && bot->hook_phase == 0)
			{
				vec3_t ad;
				float alen;
				float hspd = sqrtf(e->velocity[0] * e->velocity[0] +
				                   e->velocity[1] * e->velocity[1]);

				vec3_t fsd;
				float fsdist;

				VectorSubtract(sg_rune->seeds[l->from].origin,
				               e->s.origin, fsd);
				fsd[2] = 0.0f;
				fsdist = VectorLength(fsd);

				/*
				 * The proof fired from THE SEED, at rest (ProveHook
				 * places the phantom on the seed origin and stands it
				 * still). Both halves matter: the rope-line clearance
				 * was traced from the seed's eye -- fired from 50 units
				 * away the same ray clips different geometry and the
				 * rope bites en route (smap05: 2552 attaches 150-650
				 * from their proven anchors) -- and the swing arc is
				 * entry-sensitive. Walk to the seed, brake, THEN fire:
				 * the shot the proof rolled is the shot the body takes.
				 */
				/* the walk-to-seed gate tried here was an over-fit to
				 * smap05: on lmctf03 it turned 95%-converting hooks into
				 * abort loops (it7: 547 misses on the healthy map). The
				 * brake and the eye aim stay; the pilgrimage goes. */
				(void)fsdist;
				if (hspd > 160.0f)
				{
					hook_brake = true;      /* fire next frame, slower */
					VectorCopy(l->anchor, aim);
				}
				else
				{
					VectorSubtract(l->anchor, e->s.origin, ad);
					alen = VectorLength(ad);
					if (alen > 1.0f)
					{
						VectorCopy(l->anchor, bot->hook_anchor);
						VectorCopy(sg_rune->seeds[l->to].origin,
						           bot->hook_dest);

						/*
						 * THE BALLISTIC ANCHOR. The rope sets speed to a
						 * flat 800 along itself, so a cut rope IS a
						 * projectile launch at 800 -- and where to hook
						 * for a fling that LANDS somewhere is therefore
						 * closed-form: solve the projectile equation for
						 * launch pitch to the destination at s=800, and
						 * the anchor is wherever that ray bites the
						 * world. Low solution first (flatter, faster),
						 * high if the low ray finds no bite. When
						 * neither ray bites within rope reach, the
						 * proof's stored anchor and a full ride remain
						 * -- physics first, terminus as the fallback.
						 */
						{
							vec3_t bd, eye2;
							float bx, by2, s2 = 800.0f * 800.0f;
							float grav2 = e->client->ps.pmove.gravity
							              ? (float)e->client->ps.pmove.gravity
							              : 800.0f;
							float disc;

							VectorSubtract(sg_rune->seeds[l->to].origin,
							               e->s.origin, bd);
							by2 = bd[2];
							bx = sqrtf(bd[0] * bd[0] + bd[1] * bd[1]);
							disc = s2 * s2 - grav2 *
							       (grav2 * bx * bx + 2.0f * by2 * s2);
							if (bx > 64.0f && disc > 0.0f)
							{
								float az = atan2f(bd[1], bd[0]);
								float root = sqrtf(disc);
								int lohi;

								VectorCopy(e->s.origin, eye2);
								eye2[2] += e->viewheight;
								for (lohi = 0; lohi < 2; lohi++)
								{
									float tanth = (s2 - (lohi ? -root : root))
									              / (grav2 * bx);
									float th = atanf(tanth);
									vec3_t ray, hit;
									trace_t btr;

									ray[0] = cosf(az) * cosf(th);
									ray[1] = sinf(az) * cosf(th);
									ray[2] = sinf(th);
									VectorMA(eye2, 700.0f, ray, hit);
									btr = gi.trace(eye2, NULL, NULL, hit,
									               e, MASK_SOLID);
									if (btr.fraction < 1.0f &&
									    !btr.startsolid &&
									    btr.fraction * 700.0f > 150.0f)
									{
										VectorCopy(btr.endpos,
										           bot->hook_anchor);
										break;
									}
								}
							}
						}
						bot->hook_link = bestlink;
						bot->hook_bite_logged = false;
						bot->hook_phase = 1;
						/* flight + climb budget: gravity fights the pull
						 * on a tall rope, so the real ascent runs well
						 * past the naive rope/800 figure -- a 1.5s margin
						 * cut every z=348 tower climb loose 176 short of
						 * its landing (A/B match, Slip, four identical
						 * shortfalls) */
						bot->hook_deadline =
						    level.time + alen / 800.0f + 3.0f;
					}
				}
			}

			/* a drop link goes via its stored lip, not the far endpoint */
			if (l->action == RL_DROP)
			{
				vec3_t lipd;
				float liph;

				VectorSubtract(l->anchor, e->s.origin, lipd);
				lipd[2] = 0.0f;
				liph = VectorLength(lipd);
				if (liph > 24.0f)
					VectorCopy(l->anchor, aim);
				/*
				 * The whole drop executes the way the prover walked it:
				 * straight at the lip, no fan (ProveDrop's approach walk
				 * steers dead at the lip, sg_rune.c), then off along the
				 * RECORDED heading (dd_last_heading, sg_rune.c:734). The
				 * fan, given a railing beside the gap, deflects off the
				 * exact line the proof demonstrated -- Phase orbited a
				 * balcony's proven drops 60-140 units from their lips,
				 * with a 96-unit lock radius it never entered.
				 */
				drop_yaw_locked = true;
				/* 8, not 24: the proof fell FROM the lip point, and at
				 * 20 units of lateral offset the recorded heading runs
				 * into the railing beside the gap (it13: attackers
				 * jittering on the balcony lip at goal 4700, dl=1) */
				drop_yaw = (liph > 8.0f)
				               ? atan2f(lipd[1], lipd[0]) * 180.0f / M_PI
				               : l->heading * (360.0f / 256.0f);
			}

			/*
			 * A rocket-jump link arms its sequence: raise the launcher,
			 * then one aim-and-fire frame on the PROVEN aim vector
			 * (anchor[0/1]; z = -sqrt(1-x^2-y^2), sg_rune.h), then fly
			 * the arc. The selection gate above already priced the
			 * health and checked the inventory.
			 */
			if (l->action == RL_ROCKETJUMP && bot->rj_phase == 0 &&
			    e->groundentity)
			{
				bot->rj_aim[0] = l->anchor[0];
				bot->rj_aim[1] = l->anchor[1];
				bot->rj_aim[2] = -sqrtf(1.0f -
				    l->anchor[0] * l->anchor[0] -
				    l->anchor[1] * l->anchor[1]);
				VectorCopy(sg_rune->seeds[l->to].origin, bot->rj_dest);
				bot->rj_phase = 1;
				bot->rj_deadline = level.time + 4.0f;
				bot->rj_use_next = 0.0f;
			}
		}

		/*
		 * Rocket-jump fire frame(s): the view IS the proven aim vector --
		 * down and behind, which is what throws the body forward -- while
		 * the jump and the trigger go down together. Weapon_RocketLauncher
		 * fires along v_angle on its fire frame, same contract as the hook.
		 */
		if (bot->rj_phase == 2)
		{
			float ry, rp;

			ry = atan2f(bot->rj_aim[1], bot->rj_aim[0]) * 180.0f / M_PI;
			rp = -asinf(bot->rj_aim[2]) * 180.0f / M_PI;
			cmd.angles[YAW] = ANGLE2SHORT(ry)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = ANGLE2SHORT(rp)
			                - e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = ry;
			view_pitch = rp;
			cmd.forwardmove = 0;
			cmd.sidemove = 0;
			cmd.upmove = 400;
			cmd.buttons |= BUTTON_ATTACK;
			have_move = false;
		}

		/* while aiming to fire, the cmd angles ARE the anchor bearing --
		 * this overrides the navigation view for exactly one frame */
		if (bot->hook_phase == 1)
		{
			vec3_t ad, hook_eye;
			float alen, ay, ap;

			/*
			 * From the EYES, not the origin: the proof drew its rope line
			 * from the phantom's eyes ("the rope line from the source's
			 * eyes wins", sg_rune.c), and 22 units of vertical aim bias
			 * against a wall-face anchor bites a DIFFERENT surface --
			 * every smap05 ride was landing exactly 143 under its ledge
			 * because the rope was attaching under the proven point.
			 */
			VectorCopy(e->s.origin, hook_eye);
			hook_eye[2] += e->viewheight;
			VectorSubtract(bot->hook_anchor, hook_eye, ad);
			alen = VectorLength(ad);
			ay = atan2f(ad[1], ad[0]) * 180.0f / M_PI;
			ap = -asinf(ad[2] / (alen > 1.0f ? alen : 1.0f)) * 180.0f / M_PI;
			cmd.angles[YAW] = ANGLE2SHORT(ay)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = ANGLE2SHORT(ap)
			                - e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = ay;
			view_pitch = ap;
		}
		else if (bot->seed >= 0 &&
		         goal_field[bot->seed] >= SG_FIELD_INF)
		{
			/*
			 * Off the known surface: the field is infinite here. The right
			 * move is not the goal line -- greedy steering at a distant
			 * goal orbits concave geometry forever, and did, for four
			 * minutes straight. It is the shortest walk ONTO the surface:
			 * the nearest seed where the field turns finite. From there
			 * the links take over.
			 */
			int i, best = -1;
			float bestd = 1e30f;

			for (i = 0; i < sg_rune->hdr.num_seeds; i++)
			{
				vec3_t dd;
				float dsq;

				if (goal_field[i] >= SG_FIELD_INF)
					continue;
				VectorSubtract(sg_rune->seeds[i].origin, e->s.origin, dd);
				dsq = dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2] * 4.0f;
				if (dsq < bestd)
				{
					bestd = dsq;
					best = i;
				}
			}
			if (best >= 0)
			{
				VectorCopy(sg_rune->seeds[best].origin, aim);
				have_aim = true;
			}
		}

		if (!have_aim)
		{
			/* last resort: the goal itself, by belief */
			edict_t *gf = NULL;

			/*
			 * TERMINAL HOMING (carry forensics, wave 312): the carrier
			 * at its own stand aims at the LIVE FLAG ITEM, not the
			 * info_flag_* spawn marker. The two usually coincide --
			 * but the item droptofloors and on several stands settles
			 * offset enough that a bot walking exactly onto the marker
			 * orbits 16u from the touch that scores (the 200-second
			 * wedge orbits, 12 of 19 parity arrivals). The engine's
			 * own redflag/blueflag pointers name the real entity.
			 */
			if (role == SG_ROLE_CARRY)
				gf = SG_OwnFlag(team);

			if (!gf && role == SG_ROLE_ATTACK)
			{
				/* enemy stand position is common knowledge */
				edict_t *marker = SG_FlagStand(team, false);
				edict_t *enemy_item = SG_EnemyFlag(team);

				gf = marker;
				/*
				 * THE ROUTE GOES THROUGH THE FLAG (owner's order,
				 * 2026-08-11 -- his third strike on this disease).
				 * Wave 312 taught the CARRIER to aim at the live item
				 * because droptofloor settles it off the marker far
				 * enough to orbit; the grab side never got the fix, so
				 * attackers still walked to the marker and circled a
				 * flag a body-length away (conduct audit: stand-grind
				 * cluster, 1.5% vs 3.3% conversion). Aim at the live
				 * item -- but only while it sits at home: a dropped
				 * flag elsewhere is Rule 19 intel the bot may not have.
				 */
				if (enemy_item && marker &&
				    SG_DistXY(enemy_item->s.origin,
				              marker->s.origin) < 64.0f)
					gf = enemy_item;
			}
			else if (!gf)
			{
				gf = G_Find(NULL, FOFS(classname),
				            (team == CTF_TEAM_RED) ? "info_flag_red"
				                                   : "info_flag_blue");
			}
			if (gf)
			{
				VectorCopy(gf->s.origin, aim);
				have_aim = true;

				/*
				 * GRAB THROUGH, NOT AT. Escape speed in the first five
				 * seconds of a carry measured 256 -- slower than plain
				 * running -- because the terminal walk aimed AT the
				 * flag: arrive, stop, turn, accelerate, die (365 samples
				 * across six ten-wide waves). A human runs THROUGH the
				 * stand at full stride, already on the exit line. Inside
				 * 160 units the aim shifts past the flag to the first
				 * seed of the route home; the touch happens mid-sprint
				 * and the grab inherits a running start.
				 */
				/*
				 * CAPTURE THROUGH (owner's point, wave 321): the same
				 * disease on the scoring touch -- converge, stop, cap.
				 * Inside 160 the carrier aims PAST its flag along the
				 * line it arrived on; the touch happens mid-stride.
				 */
				/*
				 * ...and the GRAB gets the same stride (owner's order,
				 * 2026-08-11): this block guarded on CARRY alone even
				 * though its own comment promised the attacker's grab a
				 * running start. The route now goes THROUGH the flag
				 * for both touches -- grab and cap.
				 */
				if ((role == SG_ROLE_CARRY || role == SG_ROLE_ATTACK) &&
				    bot->seed >= 0)
				{
					vec3_t fd7;
					float fl7;

					VectorSubtract(gf->s.origin, e->s.origin, fd7);
					fd7[2] = 0.0f;
					fl7 = VectorLength(fd7);
					if (fl7 > 1.0f && fl7 < 160.0f)
					{
						trace_t wtr;
						vec3_t wend;

						VectorScale(fd7, (fl7 + 150.0f) / fl7, fd7);
						VectorAdd(e->s.origin, fd7, wend);
						wend[2] = gf->s.origin[2] + 16.0f;
						/* flag against a wall (owner's edge case):
						 * the exit line stops where the room does --
						 * clamp the through-point at solid geometry,
						 * never closer than the flag itself */
						wtr = gi.trace(gf->s.origin, e->mins, e->maxs,
						               wend, e, MASK_PLAYERSOLID);
						if (wtr.fraction < 1.0f)
							VectorCopy(wtr.endpos, wend);
						VectorCopy(wend, aim);
						aim[2] = gf->s.origin[2];
					}
				}

				/*
				 * THE TERMINAL BRAKE (set-#4 forensics, wave 397: Field at
				 * its own stand, flag home, 118 seconds of full-sprint orbit
				 * at radius ~90). Pure physics: at 300 u/s under sg_turnrate
				 * the minimum turn radius IS that orbit -- a missile circling
				 * a target it aims at perfectly, and CAPTURE THROUGH feeds it
				 * by projecting the aim along an arrival line that rotates
				 * with the body. The human answer at every stand: brake into
				 * the turn. Throttle follows alignment inside 220u -- badly
				 * misaligned means slow, slow means tight, tight means the
				 * spiral ends in a touch. sg_termbrake 0 restores the orbit
				 * for A/B; the 5v0 canary's partial conversions are the
				 * standing instrument that has flagged this for 25 waves.
				 */
				if ((role == SG_ROLE_CARRY || role == SG_ROLE_ATTACK) &&
				    sg_cv.termbrake->value)
				{
					vec3_t tb, tv;
					float tbl, spd2;

					VectorSubtract(gf->s.origin, e->s.origin, tb);
					tb[2] = 0.0f;
					tbl = VectorLength(tb);
					VectorCopy(e->velocity, tv);
					tv[2] = 0.0f;
					spd2 = VectorLength(tv);
					if (tbl > 1.0f && tbl < 220.0f && spd2 > 120.0f)
					{
						float al = DotProduct(tv, tb) / (spd2 * tbl);

						if (al < 0.50f)
							bot->term_brake = 0.30f;
						else if (al < 0.85f)
							bot->term_brake = 0.55f;
					}
				}
				if (role == SG_ROLE_ATTACK && bot->seed >= 0)
				{
					vec3_t fd4;

					VectorSubtract(gf->s.origin, e->s.origin, fd4);
					fd4[2] = 0.0f;
					if (VectorLength(fd4) < 160.0f)
					{
						int fs = Rune_NearestSeed(sg_rune, gf->s.origin);
						int *home4 = (team == CTF_TEAM_RED)
						             ? sg_fields.to_red_flag
						             : sg_fields.to_blue_flag;

						if (fs >= 0)
						{
							int li4, best4 = -1, bv4 = home4[fs];

							for (li4 = sg_rune->first_link[fs]; li4 >= 0;
							     li4 = sg_rune->next_link[li4])
							{
								rune_link_t *l4 = &sg_rune->links[li4];

								if (home4[l4->to] < bv4)
								{
									bv4 = home4[l4->to];
									best4 = l4->to;
								}
							}
							if (best4 >= 0)
								VectorCopy(sg_rune->seeds[best4].origin,
								           aim);
						}
					}
				}
			}
		}

		if (have_aim)
		{
			vec3_t fwd, probe;
			trace_t tr;
			float best_open = -1.0f;
			float try_yaw, base_yaw, chosen_yaw;
			int k;

			VectorSubtract(aim, e->s.origin, want);
			base_yaw = atan2f(want[1], want[0]) * 180.0f / M_PI;
			chosen_yaw = base_yaw;

			/*
			 * Feelers: try the goal heading first, then fan out. Take the
			 * most open heading nearest the goal line. This is what makes
			 * the local gradient walk around a doorframe instead of into
			 * it.
			 */
			/* sg_fandense (wave 294+): the census read a full-speed wall
			 * bump every ~7 seconds, and the geometry that does it lives
			 * in the 30-degree blind wedge between the fan's first rays.
			 * Dense mode adds the 15s; the preference-decay ordering is
			 * preserved (nearest the goal line first). */
			{
			static const float fan_dense[11] = { 0, -15, 15, -30, 30, -60,
			                                     60, -100, 100, -145, 145 };
			static const float fan_base[9]   = { 0, -30, 30, -60, 60, -100,
			                                     100, -145, 145 };
			const float *fan = sg_cv.fandense->value
			                   ? fan_dense : fan_base;
			int fan_n = sg_cv.fandense->value ? 11 : 9;

			for (k = 0; k < fan_n; k++)
			{
				float score;

				float reach = 96.0f;

				/* dose 2: reach scales with speed -- a fixed 96u probe
				 * at 400 u/s is 0.24s of warning, and the census's
				 * full-speed wall bumps live exactly there */
				if (sg_cv.fandense->value >= 2)
				{
					float fsp = sqrtf(e->velocity[0] * e->velocity[0] +
					                  e->velocity[1] * e->velocity[1]);
					reach = 96.0f + (fsp > 274.0f ? (fsp - 274.0f) * 0.5f : 0.0f);
					if (reach > 220.0f) reach = 220.0f;
				}
				try_yaw = (base_yaw + fan[k]) * M_PI / 180.0f;
				fwd[0] = cosf(try_yaw); fwd[1] = sinf(try_yaw); fwd[2] = 0;
				VectorMA(e->s.origin, reach, fwd, probe);
				probe[2] += 8.0f;
				tr = gi.trace(e->s.origin, e->mins, e->maxs, probe,
				              e, MASK_PLAYERSOLID);
				/*
				 * A teammate is not terrain. Blocked by one on the goal
				 * line: remember it (the progress watch must not bill a
				 * friendly body to the link -- at 5v5 that billed 204-278
				 * shelves a match), and bias the walk to a side chosen by
				 * slot parity, so two bots meeting head-on pass on
				 * opposite shoulders instead of mirroring forever.
				 */
				if (k == 0 && tr.fraction < 1.0f && tr.ent &&
				    tr.ent->client && !tr.ent->deadflag &&
				    tr.ent->client->ctf.teamnum == team)
				{
					bot->mate_block_last = true;
					base_yaw += ((int)(e->client - game.clients) & 1)
					            ? 28.0f : -28.0f;
				}
				/*
				 * A closed door is not a wall: walking into it (its
				 * auto-spawned trigger, g_func.c Think_SpawnDoorTrigger,
				 * reaches ~60 units out) is precisely how it opens. The
				 * rune proved these routes with doors held open; a feeler
				 * that deflects off a door steers away from the only
				 * action that makes the route real. Every shelve cluster
				 * in match 7 sat beside a door complex. Doors that only a
				 * button opens will fail to yield and the progress watch
				 * shelves that link -- the net below the honesty.
				 */
				if (tr.fraction < 1.0f && tr.ent && tr.ent->classname &&
				    strncmp(tr.ent->classname, "func_door", 9) == 0)
				{
					int dd;
					qboolean dead = false;

					/* a door that already refused to yield from here is a
					 * wall: no fraction override, the fan walks around */
					for (dd = 0; dd < SG_DEAD_DOORS; dd++)
						if (bot->dead_door[dd] == tr.ent &&
						    bot->dead_door_until[dd] > level.time)
							dead = true;
					if (dead && k == 0)
					{
						bot->deaddoor_ahead = true;
						VectorCopy(tr.endpos, bot->deaddoor_spot);
					}
					if (!dead)
					{
						/*
						 * A ROTATING door swings through the space in
						 * front of it; a body pressing at it blocks the
						 * swing and the door reverses shut, forever
						 * (match 8: one bot, 75 shelves, jamming the door
						 * with its own face). Stand outside the arc, or
						 * back out of it, and let the floor trigger swing
						 * it. Sliding doors travel out of the path and
						 * are safe to press.
						 */
						if (k == 0 && strcmp(tr.ent->classname,
						                     "func_door_rotating") == 0)
						{
							door_hold = (tr.fraction * 96.0f < 64.0f) ? 2 : 1;
							door_ent = tr.ent;
						}
						tr.fraction = 1.0f;
					}
				}
				score = tr.fraction * (1.0f - 0.05f * (k > 0) * (k + 1));
				/*
				 * Side latch. A pillar dead ahead leaves -30 and +30 both
				 * open and equal; the winner then alternates as each
				 * sidestep swings the goal bearing, and the body flaps in
				 * place against the obstacle -- seed 327 on lmctf01, the
				 * main valley route, whole matches lost to one pillar
				 * (iter 44-45). Once a detour side is chosen it stays
				 * preferred for 0.7s: enough to clear a pillar, too short
				 * to matter anywhere else. An open goal line clears it.
				 */
				if (bot->fan_side && level.time < bot->fan_side_until &&
				    fan[k] * (float)bot->fan_side < 0.0f)
					score *= 0.6f;
				if (score > best_open)
				{
					best_open = score;
					chosen_yaw = base_yaw + fan[k];
				}
				if (tr.fraction >= 1.0f && k == 0)
				{
					bot->fan_side = 0;  /* goal line open: latch released */
					break;
				}
			}
			}
			if (chosen_yaw != base_yaw)
			{
				int side = (chosen_yaw > base_yaw) ? 1 : -1;

				if (bot->fan_side != side || level.time >= bot->fan_side_until)
					bot->fan_side_until = level.time + 0.7f;
				bot->fan_side = side;
			}

			/*
			 * THE STEADY HAND (sg_smooth, A/B wave 195+). The human
			 * calibration: 52 degrees median heading change per
			 * travelling second against every bot's 70-73 -- and the
			 * commanded direction churns 81, so the scribble is born
			 * here, where the fan re-picks the walk ten times a
			 * second. A human wrist turns through headings; it does
			 * not teleport between them. The walk heading now slews
			 * at 300 degrees a second -- fast enough for any corner
			 * at 1Hz, too slow to flap -- except in a fight, at a
			 * drop lip, in precision range, or in water, where the
			 * snap IS the skill.
			 */
			if (sg_cv.smooth->value &&
			    !duel && !precision && bot->hook_phase == 0 &&
			    e->waterlevel < 2)
			{
				float sdt = level.time - bot->nav_yaw_t;
				float sdy = chosen_yaw - bot->nav_yaw_cur;
				/* the cvar IS the slew rate in deg/s (owner's blend,
				 * wave 321): 1 keeps the legacy 300 */
				float srate = sg_cv.smooth->value;

				if (srate <= 1.0f)
					srate = 300.0f;
				while (sdy > 180.0f) sdy -= 360.0f;
				while (sdy < -180.0f) sdy += 360.0f;
				if (sdt > 0.0f && sdt < 0.5f)
				{
					float cap = srate * sdt;

					if (sdy > cap) sdy = cap;
					else if (sdy < -cap) sdy = -cap;
					chosen_yaw = bot->nav_yaw_cur + sdy;
				}
				bot->nav_yaw_cur = chosen_yaw;
				bot->nav_yaw_t = level.time;
			}
			else
			{
				bot->nav_yaw_cur = chosen_yaw;
				bot->nav_yaw_t = level.time;
			}

			/* at a drop lip the proven walk-off heading overrides the fan:
			 * the proof is a line, and the line is the record's */
			if (drop_yaw_locked)
				chosen_yaw = drop_yaw;

			/*
			 * A burst rope aims WHILE RUNNING: the exemption that kept
			 * the speedhook's legs moving also skipped the phase-1 aim
			 * writer, so the view never turned to the anchor, the fire
			 * gate waited for an arrival that could not come, and the
			 * bot stuck in phase 1 forever -- which gates out COMBAT
			 * too. Three of wave 92's five games went totally silent,
			 * every bot wedged on its first burst attempt. The burst
			 * view now steers to the anchor here (yaw now, pitch via
			 * swim-pitch's slot below), and phase 1 carries a hard
			 * deadline as the belt to this suspender.
			 */
			if (bot->hook_phase == 1 && bot->speedhook)
			{
				vec3_t sha;

				VectorSubtract(bot->hook_anchor, e->s.origin, sha);
				chosen_yaw = atan2f(sha[1], sha[0]) * 180.0f / (float)M_PI;
			}

			/*
			 * Rail mode: the retry that trusts the proof over the fan.
			 * Stage 1 walks to the link's from-seed (the proof's start);
			 * stage 2 drives the straight from->to line with the fan
			 * silenced -- pmove slides along the slit's edges exactly as
			 * the phantom's pmove did. Arrival, a better field value, or
			 * the clock ends it; a timeout hands the link to the shelf.
			 */
			if (bot->rail_stage > 0 && bestlink == bot->rail_link &&
			    bestlink >= 0)
			{
				rune_link_t *rl = &sg_rune->links[bestlink];
				vec3_t rd;

				if (level.time > bot->rail_until ||
				    bot->seed == rl->to)
				{
					if (level.time > bot->rail_until &&
					    bot->seed != rl->to)
					{
						int b2, old2 = 0;

						for (b2 = 0; b2 < SG_BL_MAX; b2++)
							if (bot->bl_until[b2] < bot->bl_until[old2])
								old2 = b2;
						bot->bl_link[old2] = bestlink;
						bot->bl_until[old2] = level.time + 45.0f;
						bot->commit_link = -1;
						SG_TeachLinkFutility(bestlink);
						if (sg_cv.debug->value)
							gi.dprintf("RAILFAIL %s link=%d seed=%d\n",
							           e->client->pers.netname,
							           bestlink, bot->seed);
					}
					else if (sg_cv.debug->value)
						gi.dprintf("RAILWIN %s link=%d\n",
						           e->client->pers.netname, bestlink);
					bot->rail_stage = 0;
				}
				else if (bot->rail_stage == 1)
				{
					VectorSubtract(sg_rune->seeds[rl->from].origin,
					               e->s.origin, rd);
					rd[2] = 0.0f;
					if (VectorLength(rd) < 24.0f)
					{
						bot->rail_stage = 2;
						bot->rail_until = level.time + 3.0f;
					}
					else
						chosen_yaw = atan2f(rd[1], rd[0])
						             * 180.0f / (float)M_PI;
				}
				if (bot->rail_stage == 2)
				{
					VectorSubtract(sg_rune->seeds[rl->to].origin,
					               e->s.origin, rd);
					chosen_yaw = atan2f(rd[1], rd[0])
					             * 180.0f / (float)M_PI;
				}
			}
			else if (bot->rail_stage > 0)
				bot->rail_stage = 0;    /* the surface moved on: stand down */

			/* backing out of a pocket overrides everything but the lip:
			 * the retreat only ends early if the goal line opens up */
			if (level.time < bot->escape_until && !drop_yaw_locked)
			{
				if (best_open >= 1.0f && chosen_yaw == base_yaw)
					bot->escape_until = 0.0f;
				else
					chosen_yaw = bot->escape_yaw;
			}

			/*
			 * THE AIM FRAME OWNS THE VIEW. Phase 1 wrote the anchor
			 * bearing above; this write, running after it, was flattening
			 * every rope's pitch to zero and pointing it down the goal
			 * line -- 1519 of 1533 bad bites flew off the aim line
			 * (iteration 23), every under-climb since the first match
			 * traces here. The aim frame is a standing frame, exactly
			 * the posture the proofs fired from.
			 */
			if (bot->hook_phase == 1 && !bot->speedhook)
			{
				cmd.forwardmove = 0;
				cmd.sidemove = 0;
				cmd.upmove = 0;
				have_move = false;
			}
			else
			{
			float swim_pitch = 0.0f;

			/*
			 * PM_WaterMove runs along the FULL view vector: with the
			 * pitch flattened to zero a swimming body can only paddle
			 * horizontally, and every swim link whose destination is
			 * above or below is physically unexecutable -- lmctf01
			 * carries 65k swim links and iter 41 shows zero ever taken,
			 * the attack fields plateauing at the water. Underwater the
			 * pitch belongs to the line to the target.
			 */
			if (e->waterlevel > 1 && have_aim)
			{
				vec3_t wd;
				float wh;

				VectorSubtract(aim, e->s.origin, wd);
				wh = sqrtf(wd[0] * wd[0] + wd[1] * wd[1]);
				swim_pitch = -atan2f(wd[2], wh) * 180.0f / (float)M_PI;
				if (swim_pitch > 85.0f) swim_pitch = 85.0f;
				if (swim_pitch < -85.0f) swim_pitch = -85.0f;
			}
			else if (bot->hook_phase == 1 && bot->speedhook)
			{
				vec3_t wd;
				float wh;

				VectorSubtract(bot->hook_anchor, e->s.origin, wd);
				wd[2] -= e->viewheight;
				wh = sqrtf(wd[0] * wd[0] + wd[1] * wd[1]);
				swim_pitch = -atan2f(wd[2], wh) * 180.0f / (float)M_PI;
				if (swim_pitch > 85.0f) swim_pitch = 85.0f;
				if (swim_pitch < -85.0f) swim_pitch = -85.0f;
			}

			cmd.angles[YAW] = ANGLE2SHORT(chosen_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = ANGLE2SHORT(swim_pitch)
			                  - e->client->ps.pmove.delta_angles[PITCH];
			cmd.forwardmove = 400;
			if (jump_now)
				cmd.upmove = 400;

			view_yaw = chosen_yaw;
			view_pitch = swim_pitch;
			bot->nav_drove = true;
			}
			move_dir[0] = cosf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[1] = sinf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[2] = 0.0f;
			have_move = true;

			/*
			 * Room to hop into. A landing jump commits the bot to whatever
			 * speed and heading it left with for the whole arc, so it is only
			 * worth taking where the way ahead is actually clear -- the same
			 * player-box trace the feelers use, run further out along the
			 * heading that was chosen.
			 */
			/*
			 * Carriers get a shorter clearance bar when sg_carryhop is
			 * set (the trial value IS the distance): escape corridors
			 * rarely offer 160 clear units, so the fleeing carrier --
			 * the one role whose touch-loss decides games -- was the
			 * role hopping least (wave 270 census: 38%% airtime vs the
			 * attackers' 51%%).
			 */
			{
				float hop_reach = 160.0f;
				if (sg_cur_role == SG_ROLE_CARRY &&
				    sg_cv.carryhop->value > 0)
					hop_reach = sg_cv.carryhop->value;
				VectorMA(e->s.origin, hop_reach, move_dir, probe);
			}
			probe[2] += 8.0f;
			tr = gi.trace(e->s.origin, e->mins, e->maxs, probe,
			              e, MASK_PLAYERSOLID);
			/* same rule as the feelers: a door ahead is not a wall, but
			 * do NOT hop at one -- arrive on foot, inside its trigger */
			open_ahead = (tr.fraction >= 1.0f);

			VectorSubtract(e->s.origin, bot->last_origin, d);
			if (VectorLength(d) < 4.0f)
			{
				bot->stuck_time += 0.1f;
				if (bot->stuck_time > 1.0f && e->groundentity)
					cmd.upmove = 400;   /* hop what the feelers missed */
			}
			else
				bot->stuck_time = 0.0f;
		}

		/* braking for a rope: kill the run so the fire happens from the
		 * standing start the proof used */
		if (hook_brake)
		{
			cmd.forwardmove = 0;
			cmd.sidemove = 0;
			cmd.upmove = 0;
			bot->nav_drove = false;
		}

		/* and braking OUT of a rope: hold the landing until the body is
		 * standing where the phantom stood, then argue the next step --
		 * except a carrier under legcarrier dose 2: the pace ledger
		 * (218-219: legs +59%% speed) says the flag pays for stillness
		 * with blood, and a landing run out beats a landing stood */
		if (level.time < bot->hook_landbrake && e->groundentity &&
		    !(sg_cur_role == SG_ROLE_CARRY &&
		      sg_cv.legcarrier->value >= 2.0f))
		{
			cmd.forwardmove = 0;
			cmd.sidemove = 0;
			cmd.upmove = 0;
			bot->nav_drove = false;
		}

		/* rotating door working its arc: hold ground (or yield the arc),
		 * keep facing it, and let the trigger under our feet do the work.
		 * A door still shut after 2.5 seconds is not going to open from
		 * this side (no trigger reaches here): remember it as a wall for
		 * thirty seconds and let the surface reroute. */
		if (door_hold && have_move)
		{
			cmd.forwardmove = (door_hold == 2) ? -200 : 0;
			cmd.upmove = 0;
			bot->door_held_last = true;
			bot->nav_drove = false;

			if (door_ent != bot->door_hold_ent)
			{
				bot->door_hold_ent = door_ent;
				bot->door_hold_since = level.time;
			}
			else if (level.time - bot->door_hold_since > 2.5f)
			{
				int dd, oldest = 0;

				for (dd = 0; dd < SG_DEAD_DOORS; dd++)
					if (bot->dead_door_until[dd] < bot->dead_door_until[oldest])
						oldest = dd;
				bot->dead_door[oldest] = door_ent;
				bot->dead_door_until[oldest] = level.time + 30.0f;
				bot->door_hold_ent = NULL;
				/* a door with no trigger on this side is one-way by the
				 * mapper's hand (lmctf03: both bd doors trigger only from
				 * the base side). The 30s memory reroutes THIS bot; the
				 * field funnels the rest of the team in behind it unless
				 * the corridor repricies globally. Same cure as the wall. */
				SG_TeachFutility(bot->seed);
				if (sg_cv.debug->value)
					gi.dprintf("DEADDOOR %s at (%.0f %.0f %.0f)\n",
					           e->client->pers.netname, e->s.origin[0],
					           e->s.origin[1], e->s.origin[2]);
			}
		}
		else
		{
			bot->door_hold_ent = NULL;
			bot->door_held_last = false;
		}

		/*
		 * BREATH OUTRANKS EVERYTHING. Twelve seconds of air is what the
		 * game gives; the lmctf01 moat tunnel costs ten at pace and any
		 * stall drowns the swimmer -- wave 55's first-ever carrier on
		 * that map 'sank like a rock' mid-return, and the census says
		 * drowning, not defense, is what kills conversions there. Four
		 * seconds from the gurgle, the route stops mattering: pitch up,
		 * kick for the surface, breathe, and let the field resume from
		 * wherever the gasp happened. The rope is the one thing faster
		 * than swimming, so a live pull is left alone.
		 */
		if (e->waterlevel >= 3 && bot->hook_phase != 2 &&
		    e->air_finished - level.time <
		        ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f))
		{
			int an = (sg_airnext && bot->seed >= 0)
			         ? sg_airnext[bot->seed] : -1;

			if (an >= 0)
			{
				/* swim the graph's way out, not the ceiling's */
				vec3_t ad;
				float ay, ap, al;

				VectorSubtract(sg_rune->seeds[an].origin, e->s.origin,
				               ad);
				al = VectorLength(ad);
				ay = atan2f(ad[1], ad[0]) * 180.0f / (float)M_PI;
				ap = (al > 1.0f)
				     ? -asinf(ad[2] / al) * 180.0f / (float)M_PI : -85.0f;
				cmd.angles[YAW] = ANGLE2SHORT(ay)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd.angles[PITCH] = ANGLE2SHORT(ap)
				                  - e->client->ps.pmove.delta_angles[PITCH];
				view_pitch = ap;
			}
			else
			{
				cmd.angles[PITCH] = ANGLE2SHORT(-85.0f)
				                  - e->client->ps.pmove.delta_angles[PITCH];
				view_pitch = -85.0f;
			}
			cmd.forwardmove = 400;
			cmd.upmove = 400;
			bot->nav_drove = false;     /* not the route's fault */
		}

		/*
		 * WAITING OUT A RELOAD (sg_railrhythm). The wait happens where
		 * the body already is, because the decision above only arms it
		 * from a spot the railer cannot see -- the last cover point is
		 * the one being stood on. No walk to it, no facing change: the
		 * eyes stay on the route and combat, which never stops, keeps
		 * whatever target it was holding.
		 *
		 * THE TERMINAL BRAKE OUTRANKS IT, literally: bot->term_brake is
		 * set earlier in this same stage and a value under 1.0 means the
		 * body is throttling into a link that has to be entered at a
		 * measured speed. A hold dropped on top of that would strand the
		 * bot mid-corner. The brake wins and the crossing goes ahead.
		 */
		if (rail_hold && have_move && bot->term_brake >= 1.0f)
		{
			cmd.forwardmove = 0;
			cmd.sidemove = 0;
			cmd.upmove = 0;
			bot->nav_drove = false;     /* the wait is not the route */
			bot->stuck_time = 0.0f;     /* nor is it being stuck */
		}

		/* rallying: get to cover first, stand there, face the push */
		if (rally_hold && have_move)
		{
			vec3_t cvd;
			qboolean at_cover = true;

			if (bot->rally_cover >= 0)
			{
				VectorSubtract(sg_rune->seeds[bot->rally_cover].origin,
				               e->s.origin, cvd);
				cvd[2] = 0.0f;
				at_cover = (VectorLength(cvd) < 48.0f);
			}
			if (at_cover)
			{
				cmd.forwardmove = 0;
				cmd.sidemove = 0;
				cmd.upmove = 0;
				bot->nav_drove = false;
				bot->stuck_time = 0.0f;
			}
			else
			{
				float cy = atan2f(cvd[1], cvd[0]) * 180.0f / (float)M_PI;

				cmd.angles[YAW] = ANGLE2SHORT(cy)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd.forwardmove = 400;
				view_yaw = cy;
				bot->nav_drove = false;     /* the wait is not the route */
			}
		}

		/* on post: whatever the descent wanted, guard duty overrides it */
		if (hold_post)
		{
			cmd.forwardmove = 0;
			cmd.sidemove = 0;
			cmd.upmove = 0;
			bot->nav_drove = false;
			cmd.angles[YAW] = ANGLE2SHORT(post_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = post_yaw;
			view_pitch = 0.0f;
			have_move = false;
			bot->stuck_time = 0.0f;
		}
	}

	/*
	 * The basis the engine will actually use, not a convenient one.
	 *
	 * Pmove builds it before every land move (pmove.c, PM_AirMove; quoted at
	 * bl_main.c:347-370): the view angles with PITCH divided by three, and
	 * then wishvel[i] = forward[i]*fmove + right[i]*smove for i in {0,1}. So
	 * forward's horizontal length is scaled by cos(pitch/3) while right stays
	 * fully horizontal. Solving against a pitch-zero basis makes the engine
	 * reconstruct a different direction than the one asked for -- shorter, and
	 * skewed toward the strafe axis, which lowers wishspeed and with it the
	 * acceleration. Here the navigation view is pitch zero, so the division
	 * changes nothing today; it is written the engine's way so that it stays
	 * correct the moment the body pitches.
	 */
	{
		vec3_t basis;

		basis[PITCH] = view_pitch;
		if (basis[PITCH] > 180.0f)
			basis[PITCH] -= 360.0f;
		basis[PITCH] /= 3.0f;
		basis[YAW] = view_yaw;
		basis[ROLL] = 0.0f;
		AngleVectors(basis, basis_fwd, basis_right, NULL);
	}

	/*
	 * The weave, decided here and applied per step below.
	 *
	 * A bot whose route has run out -- no improving link, or a destination it
	 * is already standing on top of -- has nothing left to spend its movement
	 * on, and standing still in a firefight is the one thing that is certainly
	 * wrong. So it oscillates sideways instead. The direction needs no work:
	 * combat has already put the view on the target, and pmove's own basis
	 * makes `right` exactly perpendicular to that view in the horizontal plane
	 * -- with roll zero, right = (sin yaw, -cos yaw, 0) regardless of pitch
	 * (AngleVectors, q_shared.c). sidemove alone is therefore across the enemy
	 * line by construction, and forwardmove is dropped so the weave adds no
	 * drift along it.
	 *
	 * The period is per bot, not per squad: four bots weaving on one clock is
	 * one wide target. Two-thirds of a rocket's flight time at close range,
	 * spread across ten phases by client number.
	 *
	 * Never for the carrier (2.4-D2 is a route, not a fight), never with a
	 * rope out (the hook SETS velocity -- an off-axis input accumulates into
	 * nothing), and never on the final approach, where the whole point is
	 * being able to stop on the flag.
	 */
	/* sg_noweave (A/B wave 176+): the Brownian census -- median 73
	 * degrees of heading change per travelling second, 32%% outright
	 * reversals -- with goals, seeds, and links all measured stable.
	 * The lateral oscillators are the remaining suspects, and they
	 * trigger on BELIEF-engagement, which at parity is nearly always.
	 * Dodging nobody's bullets is the owner's "unnecessary movement"
	 * by definition; this gate measures what the dodge layers cost. */
	if (duel && role != SG_ROLE_CARRY && !precision &&
	    bot->hook_phase == 0 &&
	    !sg_cv.noweave->value)
	{
		if (bestlink < 0)
			duel_hold = true;
		else
		{
			VectorSubtract(sg_rune->seeds[sg_rune->links[bestlink].to].origin,
			               e->s.origin, d);
			duel_hold = (VectorLength(d) < SG_WEAVE_HOLD);
		}

		if (duel_hold)
		{
			float period = SG_WEAVE_BASE + SG_WEAVE_STEP *
			               (float)((int)(e->client - game.clients) % 10);

			weave_side = (fmodf(level.time, period) < period * 0.5f)
			             ? SG_WEAVE_SIDE : -SG_WEAVE_SIDE;
		}
	}

	/*
	 * Execute the frame in physics steps, and decide the movement again on
	 * every one of them.
	 *
	 * The angle that keeps PM_Accelerate paying depends on the current
	 * velocity, and the velocity is exactly what the previous step just
	 * changed: a command computed at 300 is the wrong command by the time the
	 * bot is doing 500. The landing jump wants the very step the bot touches
	 * down, and a bot that only gets one chance per tenth of a second spends
	 * far longer on the floor than a player whose client sends a dozen
	 * commands in the same window. A real client does this continuously; this
	 * is the bot catching up to that, not overtaking it.
	 *
	 * The clock is not touched. msec is the frame's real time, and the steps
	 * are that integer split with the remainder spread over the first few, so
	 * they sum to exactly what passed -- eight steps of 13ms for a 100ms frame
	 * would be 104ms of simulation for 100ms of play, which is free speed
	 * rather than finer movement. Finer decisions, never a longer clock.
	 */

	/*
	 * THE BREATHER (sg_breather, wave 388 trial). The movement-texture
	 * judge, 4/4 blind on mactf06: "constant run plateau with
	 * instantaneous needle spikes, zero acceleration ramping, no
	 * burst/rest cadence, texture statistically identical from t=0 to
	 * t=850." A human's throttle is not a switch: they ease off checking
	 * corners, after fights, waiting on timers. This eases off ONLY on
	 * safe legs -- never carrying, never on the rope, never engaged, feet
	 * on the floor -- for 0.5-1.8s at a time, on average once per
	 * cvar-seconds of safe travel. Danger cancels it instantly; the cap
	 * on cost is a fraction of a second of arrival time per leg.
	 */
	{
		float dose = sg_cv.breather->value;

		if (dose > 0.0f && role != SG_ROLE_CARRY &&
		    bot->hook_phase == 0 && !bot->engaged_last &&
		    e->groundentity != NULL)
		{
			if (level.time >= bot->breather_next)
			{
				bot->breather_next = level.time +
				    dose * (0.5f + (float)(rand() % 100) / 100.0f);
				bot->breather_until = level.time +
				    0.5f + (float)(rand() % 130) / 100.0f;
			}
			if (level.time < bot->breather_until)
			{
				cmd.forwardmove = (short)(cmd.forwardmove * 0.35f);
				cmd.sidemove = (short)(cmd.sidemove * 0.35f);
			}
		}
		else
			bot->breather_until = 0.0f;     /* danger ends the stroll */
	}

	{
		int		total = cmd.msec;
		int		sub = (int)sg_cv.subframes->value;
		int		base, rem, step;
		short	plain_forward = cmd.forwardmove;
		short	nav_jump = cmd.upmove;
		/* combat's own answer to "is there a fight on RIGHT NOW", as opposed
		 * to the up-to-two-seconds-old belief the surface terms were priced
		 * from. The weave below needs the live one. */
		qboolean engaged = false;
		/* sg_airstrafe, decided once for the frame and spent per sub-step */
		qboolean as_ok = false;         /* the chain is live this frame */
		qboolean as_chain = false;      /* dose 2: hop chaining as well */
		float    as_lean = 0.0f;        /* the sinusoid, -1..1 */

		if (sub < 1)
			sub = 1;
		if (sub > total)
			sub = total;            /* a step cannot be shorter than 1ms */
		base = total / sub;
		rem = total % sub;
		sub_steps = sub;

		/*
		 * Combat rides the frame's command: view and trigger only, movement
		 * untouched. It writes the base cmd once and every subframe inherits
		 * it. Ordering rule from sg_combat.h: at hook_phase 1 the cmd angles
		 * ARE the anchor bearing (the rope fires along v_angle), so combat
		 * must not steal the view that frame.
		 *
		 * Phase 2 -- rope out, being pulled -- used to be gated out too, on the
		 * assumption that shooting and grappling could not share the attack
		 * button. That is true only when the grapple is pers.weapon
		 * (g_cmds.c:1405-1412), which SG_CombatFrame now guarantees never
		 * happens (WEAPONS.md rule S3). An OFFHAND rope is sustained by
		 * ClientEndServerFrame with no button and no view input
		 * (p_view.c:988-990), and while it pulls it SETS velocity outright
		 * (p_weapon.c:2071-2102) -- so the view costs the movement nothing on
		 * those frames and the trigger is free. That is WEAPONS.md 2.4-D2's
		 * flee doctrine: a carrier that grapples and shoots at the same time.
		 *
		 * Phase 3 stays gated out for the opposite reason: the rope is gone,
		 * the bot is flying its own landing on forwardmove down the chosen yaw
		 * (above), and a view stolen there is a landing missed.
		 */
		if (bot->hook_phase != 1 && bot->hook_phase != 3 &&
		    bot->rj_phase == 0 && bot->nade_phase == 0)
			SG_CombatFrame(e, &cmd, &engaged);

		/*
		 * The bomb sequence owns weapon, view, and trigger while it
		 * runs: switch (0.5s), cook with the button held (1.3s, view
		 * arced 25 degrees over the target's bearing), release -- the
		 * grenade code throws on release. Combat resumes next frame
		 * and the ladder takes the weapon back.
		 */
		if (bot->nade_phase == 1)
		{
			/* cook only once the grenade is VERIFIABLY in hand -- the
			 * switch runs through down/up animations, and holding the
			 * trigger early fires whatever is still equipped (wave 127:
			 * zero grenades thrown, the cook squeezed the rail) */
			if (e->client->pers.weapon &&
			    e->client->pers.weapon->pickup_name &&
			    !Q_stricmp(e->client->pers.weapon->pickup_name,
			               "Grenades"))
			{
				bot->nade_phase = 2;
				/* the engine's in-hand deadline: cook_start + TIMER +
				 * 0.2. The release moment is computed against FLIGHT
				 * TIME each aim frame below -- the fixed 2.2s cook
				 * left ~1s of fuse to spend BOUNCING off the impact
				 * point at 667 u/s, which is exactly the one-room-off
				 * miss NADEPOP measured (medians 434 and 717, radius
				 * 165, waves 140/142) */
				bot->nade_until = level.time + 3.2f;
				if (sg_cv.debug->value)
					gi.dprintf("NADE %s cooking\n",
					           e->client->pers.netname);
			}
			else if (level.time >= bot->nade_until + 1.2f)
			{
				bot->nade_phase = 0;    /* switch never landed */
				bot->nade_next = level.time + 4.0f;
			}
		}
		if (bot->nade_phase == 2)
		{
			/*
			 * THE BOMB LEADS THE LANDING (sg_nadelead, wave 309+). The
			 * owner's doctrine already won this argument for rockets
			 * (landlead, adopted 249): an airborne enemy is on a
			 * committed arc. The cooked grenade is the same shot with
			 * a better fuse -- while cooking, if the live enemy is in
			 * the air, walk its parabola to the touchdown and put the
			 * bomb THERE instead of at the danger book's historic
			 * post. Ground targets keep the book (a runner outlives
			 * the fuse; the eight nulled mechanisms all chased them).
			 */
			if (sg_cv.nadelead->value)
			{
				edict_t *len9 = SG_CombatLiveEnemy(e);

				if (len9 && !len9->groundentity)
				{
					vec3_t lp0, lp1;
					trace_t lltr;
					float ltt, lgrav = 800.0f;
					int lseg;

					VectorCopy(len9->s.origin, lp0);
					for (lseg = 1; lseg <= 30; lseg++)
					{
						ltt = 0.05f * (float)lseg;
						lp1[0] = len9->s.origin[0] + len9->velocity[0] * ltt;
						lp1[1] = len9->s.origin[1] + len9->velocity[1] * ltt;
						lp1[2] = len9->s.origin[2] + len9->velocity[2] * ltt
						       - 0.5f * lgrav * ltt * ltt;
						lltr = gi.trace(lp0, len9->mins, len9->maxs, lp1,
						                len9, MASK_PLAYERSOLID);
						if (lltr.fraction < 1.0f)
						{
							VectorCopy(lltr.endpos, bot->nade_at);
							bot->nade_at[2] += 24.0f;
							break;
						}
						VectorCopy(lp1, lp0);
					}
				}
			}

			/*
			 * The bomb aims like the rope: solve the projectile. Throw
			 * speed scales with cook (400 to 800 across the 3s timer;
			 * our 1.3s cook gives ~575), gravity is the server's, and
			 * the launch pitch to land ON the post is closed-form -- the
			 * same equation the ballistic anchor uses. The flat -25
			 * guess sailed 24 of 24 throws over the room (wave 128,
			 * zero kills, duel ratio unmoved).
			 */
			vec3_t na;
			float nyaw, npitch, nh;
			/* the engine's clock: timer remaining if released this frame
			 * is nade_until - now (p_weapon.c: grenade_time = cook_start
			 * + TIMER + 0.2, and nade_until holds exactly that sum) */
			float ntmr = bot->nade_until - level.time;
			float nheld = 3.0f - (ntmr < 0.0f ? 0.0f
			                     : (ntmr > 3.0f ? 3.0f : ntmr));
			float nsp = 400.0f + nheld * ((800.0f - 400.0f) / 3.0f);
			float ng = e->client->ps.pmove.gravity
			           ? (float)e->client->ps.pmove.gravity : 800.0f;
			float ns2 = nsp * nsp, ndisc, nfly = -1.0f;

			VectorSubtract(bot->nade_at, e->s.origin, na);
			nh = sqrtf(na[0] * na[0] + na[1] * na[1]);
			nyaw = atan2f(na[1], na[0]) * 180.0f / (float)M_PI;
			ndisc = ns2 * ns2 - ng * (ng * nh * nh + 2.0f * na[2] * ns2);
			if (ndisc > 0.0f && nh > 32.0f)
			{
				float ntan = (ns2 - sqrtf(ndisc)) / (ng * nh);

				npitch = -atanf(ntan) * 180.0f / (float)M_PI;
				/* flight time from the same closed form: horizontal
				 * range over horizontal speed */
				nfly = nh * sqrtf(1.0f + ntan * ntan) / nsp;

				/*
				 * THE ARC CHECK (wave 237). Three timing doses moved
				 * nothing because most throws never get their
				 * parabola -- from the band, the arc to the stand
				 * runs through corridor walls, and the grenade
				 * bounces to a random floor pop (air%% pinned at 30,
				 * median 556). Sample the intended arc; a blocked
				 * flight is an attempt abandoned at zero cost, per
				 * the owner's economy. Volume falls to what is real.
				 */
				{
					vec3_t ap, lp;
					trace_t atr;
					float tstep = nfly / 6.0f, tt2;
					int seg;
					float cy = cosf(nyaw * (float)M_PI / 180.0f);
					float sy = sinf(nyaw * (float)M_PI / 180.0f);
					float hv2 = nsp / sqrtf(1.0f + ntan * ntan);
					float vv2 = hv2 * ntan;

					VectorCopy(e->s.origin, lp);
					lp[2] += e->viewheight;
					for (seg = 1; seg <= 6; seg++)
					{
						tt2 = tstep * (float)seg;
						ap[0] = e->s.origin[0] + cy * hv2 * tt2;
						ap[1] = e->s.origin[1] + sy * hv2 * tt2;
						ap[2] = e->s.origin[2] + e->viewheight
						      + vv2 * tt2 - 0.5f * ng * tt2 * tt2;
						atr = gi.trace(lp, NULL, NULL, ap, e,
						               MASK_SOLID);
						if (atr.fraction < 1.0f)
						{
							nfly = -2.0f;   /* blocked: no throw */
							break;
						}
						VectorCopy(ap, lp);
					}
				}
				if (nfly < -1.5f)
				{
					bot->nade_phase = 0;    /* abandon, cost-free */
					bot->nade_next = level.time + 4.0f;
					cmd.buttons &= ~BUTTON_ATTACK;
				}
			}
			else
				npitch = -atan2f(na[2], nh) * 180.0f / (float)M_PI
				         - 30.0f;
			/*
			 * THE SILENT COOK (wave 233). The zero-cost audit caught
			 * the veer: this block owned the view for the whole cook
			 * and the legs followed it off the route -- a 33 percent
			 * steal tax on the trial arm. The owner's cook touches
			 * nothing: the view belongs to navigation until the
			 * release frame, when the aim exists for exactly one
			 * command -- the flick. Attack stays held throughout;
			 * cooking needs the trigger, not the eyes.
			 */
			if (!(sg_cv.flycook->value) ||
			    (nfly >= 0.0f && ntmr - 0.2f <= nfly + 0.15f) ||
			    ntmr <= 0.75f)
			{
				cmd.angles[YAW] = ANGLE2SHORT(nyaw)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd.angles[PITCH] = ANGLE2SHORT(npitch)
				                  - e->client->ps.pmove.delta_angles[PITCH];
			}
			/*
			 * THE TRUE AIRBURST. Release when the fuse remaining equals
			 * the flight ahead, so the pop happens ON ARRIVAL -- the
			 * fixed cook landed the bomb with a second to spend
			 * bouncing away from the aim point, and NADEPOP measured
			 * the result: medians 434 and 717 against a 165 radius.
			 * A degenerate solve means the throw is TOO SLOW SO FAR --
			 * speed builds with the cook (wave 143: releasing on
			 * degenerate threw every bomb at 400 u/s on frame one,
			 * fuse=3.0, two pops all wave) -- so keep cooking until
			 * the closed form comes back. The 0.6s floor keeps the POP
			 * outside our own splash radius (0.4s of fuse is ~250
			 * units of clearance) and throws whatever the solve says.
			 */
			/* wave 236: pop EARLY -- hold the cook until remaining
			 * fuse undercuts the flight by 0.15s, so the grenade
			 * expires airborne short of the floor it would have
			 * bounced off (235: aim height alone left 69 percent of
			 * pops grounded -- bounces and clipped arcs land before
			 * the fuse, and a landed grenade is an announcement) */
			if (ntmr > 0.6f &&
			    (nfly < 0.0f || ntmr - 0.2f > nfly - 0.15f))
				cmd.buttons |= BUTTON_ATTACK;
			else
			{
				cmd.buttons &= ~BUTTON_ATTACK;   /* the release throws */
				bot->nade_phase = 0;
				bot->nade_next = level.time + 8.0f;
				if (sg_cv.debug->value)
					gi.dprintf("NADE %s thrown fly=%.2f fuse=%.2f\n",
					           e->client->pers.netname,
					           nfly, ntmr - 0.2f);
			}
		}
		/*
		 * SOUND-DIRECTED FIRE (sg_soundfire, wave 244). The owner:
		 * "I can shoot rockets in the direction of sounds while
		 * travelling a route." The ear places heard_only beliefs that
		 * combat refuses to aim at -- this aims at them on purpose,
		 * once per cadence, launcher already in hand, one flick
		 * frame, zero route seconds. Free speculation; the splash
		 * does the rest or nothing does.
		 */
		if (sg_cv.soundfire->value &&
		    !duel && !engaged && role != SG_ROLE_CARRY &&
		    bot->nade_phase == 0 && bot->hook_phase == 0 &&
		    level.time >= bot->soundfire_next &&
		    e->client->pers.weapon &&
		    e->client->pers.weapon->pickup_name &&
		    !Q_stricmp(e->client->pers.weapon->pickup_name,
		               "Rocket Launcher"))
		{
			int s15;

			for (s15 = 0; s15 < SG_MAX_ENEMY_TRACK; s15++)
			{
				sg_belief_enemy_t *en15 =
				    &sg_caco_enemies[team - 1][s15];
				vec3_t sd15;
				float sl15;

				if (en15->client < 0 || en15->seed < 0 ||
				    !en15->heard_only ||
				    level.time - en15->seen_time >= 2.0f)
					continue;
				VectorSubtract(sg_rune->seeds[en15->seed].origin,
				               e->s.origin, sd15);
				sl15 = VectorLength(sd15);
				if (sl15 < 600.0f || sl15 > 1500.0f)
					continue;   /* too close = own splash; too
					             * far = pure noise */
				/* wave 245: never rocket a ghost a teammate is
				 * standing on -- the one premium this free trick
				 * could quietly charge is friendly splash */
				{
					int bi15, mate15 = 0;

					for (bi15 = 0; bi15 < SG_MAXBOTS; bi15++)
					{
						sg_bot_t *mb15 = &sg_bots[bi15];
						vec3_t md15;

						if (!mb15->active || mb15 == bot ||
						    !mb15->ent || !mb15->ent->inuse)
							continue;
						if (mb15->ent->client->ctf.teamnum != team)
							continue;
						VectorSubtract(mb15->ent->s.origin,
						    sg_rune->seeds[en15->seed].origin, md15);
						if (VectorLength(md15) < 250.0f)
						{
							mate15 = 1;
							break;
						}
					}
					if (mate15)
						continue;
				}
				{
					float sy15 = atan2f(sd15[1], sd15[0])
					             * 180.0f / (float)M_PI;
					float sp15 = -atan2f(sd15[2],
					    sqrtf(sd15[0]*sd15[0] + sd15[1]*sd15[1]))
					             * 180.0f / (float)M_PI;

					cmd.angles[YAW] = ANGLE2SHORT(sy15)
					    - e->client->ps.pmove.delta_angles[YAW];
					cmd.angles[PITCH] = ANGLE2SHORT(sp15)
					    - e->client->ps.pmove.delta_angles[PITCH];
					cmd.buttons |= BUTTON_ATTACK;
					bot->soundfire_next = level.time + 8.0f;
					if (sg_cv.debug->value)
						gi.dprintf("SNDFIRE %s rng=%.0f\n",
						           e->client->pers.netname, sl15);
				}
				break;
			}
		}

		bot->engaged_last = engaged;

		/*
		 * Combat re-aimed: rebuild the movement basis from the view pmove
		 * will ACTUALLY use. Solving the strafe against the navigation
		 * basis while flying the combat view made the engine reconstruct
		 * a different direction than the one asked for -- the bot ran
		 * down its AIM instead of its route on every engaged frame (the
		 * duel implementation's flagged coupling, now closed).
		 */
		if (engaged)
		{
			vec3_t basis;

			basis[YAW] = SHORT2ANGLE((short)(cmd.angles[YAW] +
			             e->client->ps.pmove.delta_angles[YAW]));
			basis[PITCH] = SHORT2ANGLE((short)(cmd.angles[PITCH] +
			               e->client->ps.pmove.delta_angles[PITCH]));
			if (basis[PITCH] > 180.0f)
				basis[PITCH] -= 360.0f;
			basis[PITCH] /= 3.0f;
			basis[ROLL] = 0.0f;
			AngleVectors(basis, basis_fwd, basis_right, NULL);
		}

		/*
		 * THE SPAWN BEAT'S EYES. The last writer before the slew, so it
		 * is looking around rather than arguing with navigation about
		 * where to look; the slew below then carries the sweep at
		 * sg_turnrate like any other ask, which is why this asks for an
		 * ANGLE and not a rate.
		 *
		 * The sweep is centred on the heading the frame already wanted --
		 * the route's, this early -- so the beat ends looking down the
		 * road it is about to run instead of snapping back onto it. One
		 * full sine over the window is centre, one shoulder, through
		 * centre, the other shoulder, centre: what a player's mouse does
		 * in the half second after the screen comes back.
		 *
		 * Danger ends it on the frame danger arrives. `engaged` is
		 * combat's live answer, not last frame's, and the damage ring
		 * catches the shot that came from somewhere the eye had not got
		 * to yet -- which, spawning, is most of the map.
		 */
		if (bot->beat_until > level.time)
		{
			/* bot->engaged_last is this same value by here -- it was
			 * assigned from `engaged` a few lines up, so the live read
			 * is the one worth making */
			if (engaged || Beat_HurtSince(e, bot->beat_from))
				bot->beat_until = 0.0f;
			else
			{
				float span = bot->beat_until - bot->beat_from;
				float t = (span > 0.001f)
				          ? (level.time - bot->beat_from) / span : 1.0f;
				float yaw = SHORT2ANGLE((short)(cmd.angles[YAW] +
				            e->client->ps.pmove.delta_angles[YAW]));

				yaw += (float)bot->beat_sign * bot->beat_arc *
				       sinf(t * 2.0f * (float)M_PI);
				cmd.angles[YAW] = ANGLE2SHORT(yaw)
				                - e->client->ps.pmove.delta_angles[YAW];
			}
		}

		/*
		 * THE AIR-STRAFE CHAIN, armed once a frame and spent per sub-step
		 * (sg_airstrafe: 0 off, 1 the lean, 2 the lean and the hops).
		 *
		 * The angle is the engine's and is derived at SG_AirStrafeCmd; what
		 * is decided here is whether the body is allowed to fly it, and
		 * which way the swing is leaning at this instant.
		 *
		 * THE SWING. sin() over SG_AS_PERIOD, plus the standing heading
		 * error over SG_AS_CORR. The bias is the whole of the anti-drift:
		 * the lean is measured off the direction of TRAVEL, so a swing held
		 * one way walks the body off its road, and adding the error to the
		 * sinusoid gives the correcting shoulder the longer half of every
		 * cycle. Past SG_AS_ABORT the error is not a lean to be biased, it
		 * is a turn to be made, and the chain ends.
		 *
		 * THE VETOES, in the order they are worth stating: never on a rope
		 * (the hook SETS velocity and an off-axis input accumulates into
		 * nothing), never while combat is live (`engaged`, this frame's
		 * answer, not the belief), never with the terminal brake down
		 * (bot->term_brake is the carrier's cornering throttle -- a chain
		 * there is a flag missed), never inside SG_AS_FLAGKEEP of a stand,
		 * never on the final approach, in water, ducked, mid-rocket-jump,
		 * mid-bomb, or through the spawn beat.
		 *
		 * THE ROAD. A hop chain commits the body for the length of a
		 * flight, so it wants a road to spend that on: run_link and
		 * open_ahead say the next step is ground running with room in it,
		 * and SG_RunRoom walks the same seeds the lookahead trusts, holds
		 * the point a stride down them, and asks whether the chord to it is
		 * straight, on-heading, and clear of the player box.
		 *
		 * THE APPETITE. The bar on that road is divided by the persona's
		 * hook enthusiasm -- the trait that already means "appetite for the
		 * optional piece of movement tech" -- so the keen bots commit on
		 * roads the cautious ones walk. Same +/-15% band every other
		 * consumer gets, and sixteen bots stop doing it identically.
		 */
		{
			float dose = sg_cv.airstrafe->value;
			float sp = sqrtf(e->velocity[0] * e->velocity[0] +
			                 e->velocity[1] * e->velocity[1]);

			if (dose > 0.0f && sp >= SG_AS_FLOOR && have_move &&
			    run_link && open_ahead && bestlink >= 0 && sg_rune &&
			    !precision && !engaged &&
			    bot->hook_phase == 0 && bot->rj_phase == 0 &&
			    bot->nade_phase == 0 && bot->term_brake >= 1.0f &&
			    e->waterlevel <= 1 && bot->beat_until <= level.time &&
			    !(e->client->ps.pmove.pm_flags & PMF_DUCKED) &&
			    !SG_NearAFlag(e, SG_AS_FLAGKEEP))
			{
				vec3_t	vdir;
				float	cross, dot, err;
				/* the bar on the road, and the lower bar a chain ALREADY
				 * running is held to. A player who has committed to a
				 * chain does not re-audit the corridor every tenth of a
				 * second and stop dead when it narrows; he finishes the
				 * hop he is in. Without the hysteresis the road test
				 * chatters on and off across the bar and no chain lives
				 * long enough to reach the speeds the technique is for. */
				float	aswant = SG_AS_RUN / SG_PersonaHookScale(e);

				if (bot->as_since != 0.0f)
					aswant *= SG_AS_HOLD;

				vdir[0] = e->velocity[0] / sp;
				vdir[1] = e->velocity[1] / sp;
				vdir[2] = 0.0f;
				/* signed error from travel to route: positive is the route
				 * lying counter-clockwise, which is the sign a positive
				 * lean rotates the wish toward */
				cross = vdir[0] * move_dir[1] - vdir[1] * move_dir[0];
				dot = vdir[0] * move_dir[0] + vdir[1] * move_dir[1];
				err = atan2f(cross, dot) * 180.0f / (float)M_PI;

				if (err > -SG_AS_ABORT && err < SG_AS_ABORT &&
				    SG_RunRoom(e, sg_rune->links[bestlink].to,
				               route_field, move_dir, aswant))
				{
					float dt = (float)total / 1000.0f;

					as_ok = true;
					as_chain = (dose >= 2.0f);

					bot->as_phase += 2.0f * (float)M_PI * dt / SG_AS_PERIOD;
					while (bot->as_phase > 2.0f * (float)M_PI)
						bot->as_phase -= 2.0f * (float)M_PI;

					as_lean = sinf(bot->as_phase) + err / SG_AS_CORR;
					if (as_lean > 1.0f)
						as_lean = 1.0f;
					if (as_lean < -1.0f)
						as_lean = -1.0f;
				}
			}

			if (as_ok)
			{
				/* the same closed form the command uses, at the frame's
				 * speed and one sub-step of frametime, turned into the
				 * VIEW's share of the swing and asked for through the slew
				 * below like any other heading */
				float	ft = (float)base / 1000.0f;
				float	accelspeed = SG_AIR_ACCEL * ft * 300.0f;
				float	c = (300.0f - accelspeed) / sp;
				float	lv, yaw;

				if (bot->as_since == 0.0f)
				{
					bot->as_since = level.time;
					bot->as_entry = sp;
					bot->as_peak = sp;
				}
				else if (sp > bot->as_peak)
					bot->as_peak = sp;

				if (c > 1.0f)
					c = 1.0f;
				if (c < -1.0f)
					c = -1.0f;
				lv = acosf(c) * 180.0f / (float)M_PI *
				     as_lean * SG_AS_VIEWSHARE;
				if (lv > SG_AS_VIEWMAX)
					lv = SG_AS_VIEWMAX;
				if (lv < -SG_AS_VIEWMAX)
					lv = -SG_AS_VIEWMAX;

				yaw = SHORT2ANGLE((short)(cmd.angles[YAW] +
				      e->client->ps.pmove.delta_angles[YAW]));
				yaw += lv;
				cmd.angles[YAW] = ANGLE2SHORT(yaw)
				                - e->client->ps.pmove.delta_angles[YAW];
			}
			else
			{
				if (bot->as_since != 0.0f)
				{
					float dur = level.time - bot->as_since;

					/* one sustained chain in eight: a fleet chaining hops
					 * would otherwise write a line a second per bot, and
					 * the log is for reading */
					if (dur >= SG_AS_MINCHAIN &&
					    sg_cv.debug->value &&
					    !(bot->as_said++ & 7))
						gi.dprintf("AIRCHAIN %s %.2fs entry=%.0f "
						           "peak=%.0f\n",
						           e->client->pers.netname, dur,
						           bot->as_entry, bot->as_peak);
					bot->as_since = 0.0f;
				}
				bot->as_phase = 0.0f;
			}
		}

		/*
		 * THE VIEW SLEWS; IT NO LONGER TELEPORTS. Every writer above --
		 * navigation, combat, the rope aim, the swim pitch -- asks for a
		 * heading, and until now the ask was granted instantly: a 180
		 * happened in one server frame, superhuman to play against and
		 * unwatchable in chase-cam (reported live). The desired heading
		 * is decoded once here, and each subframe advances the actual
		 * view toward it along the shortest arc at sg_turnrate degrees a
		 * second (default 600: a competitive flick, half a second for a
		 * full 360). Zero restores the teleport.
		 */
		{
			float want_y = SHORT2ANGLE((short)(cmd.angles[YAW] +
			               e->client->ps.pmove.delta_angles[YAW]));
			float want_p = SHORT2ANGLE((short)(cmd.angles[PITCH] +
			               e->client->ps.pmove.delta_angles[PITCH]));
			float rate = sg_cv.turnrate->value;

			if (!bot->view_on || rate <= 0.0f)
			{
				bot->vy_cur = want_y;
				bot->vp_cur = want_p;
				bot->view_on = true;
			}
			slew_want_y = want_y;
			slew_want_p = want_p;
			slew_rate = rate;
		}

		for (step = 0; step < sub; step++)
		{
			cmd.msec = (byte)(base + (step < rem ? 1 : 0));
			if (!cmd.msec)
				continue;
			sub_msec = cmd.msec;

			if (slew_rate > 0.0f)
			{
				float dt = (float)cmd.msec / 1000.0f;
				float dy = slew_want_y - bot->vy_cur;
				float dp = slew_want_p - bot->vp_cur;
				float stepmax = slew_rate * dt;

				while (dy > 180.0f) dy -= 360.0f;
				while (dy < -180.0f) dy += 360.0f;
				while (dp > 180.0f) dp -= 360.0f;
				while (dp < -180.0f) dp += 360.0f;
				if (dy > stepmax) dy = stepmax;
				if (dy < -stepmax) dy = -stepmax;
				if (dp > stepmax) dp = stepmax;
				if (dp < -stepmax) dp = -stepmax;
				bot->vy_cur += dy;
				bot->vp_cur += dp;
				cmd.angles[YAW] = ANGLE2SHORT(bot->vy_cur)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd.angles[PITCH] = ANGLE2SHORT(bot->vp_cur)
				                  - e->client->ps.pmove.delta_angles[PITCH];
			}

			/*
			 * THE LEGS ARE FREE OF THE EYES. Movement was welded to the
			 * view -- forwardmove always ran down the gaze -- which is
			 * why the look-ahead experiment steered bodies into walls,
			 * why hook aim needed a standing frame, and why the fleet
			 * read as a mob of people walking wherever they happened to
			 * be staring (called out in exactly those words). A Quake
			 * body translates in four directions relative to ANY view:
			 * the course is a world-space fact (move_dir, the fan's
			 * answer), the view is whatever combat or the rope or the
			 * route wants, and forward/side are just the course
			 * decomposed into the ACTUAL slewed view frame, every
			 * subframe, so even mid-turn the body holds its line.
			 * Underwater stays welded -- pmove swims along the full
			 * view vector by design, and the swim pitch already aims
			 * the view down the course.
			 */
			if (plain_forward > 0 && have_move && e->waterlevel <= 1)
			{
				vec3_t vb, vf, vr;
				float fl;

				vb[YAW] = bot->vy_cur;
				vb[PITCH] = bot->vp_cur / 3.0f;
				vb[ROLL] = 0.0f;
				AngleVectors(vb, vf, vr, NULL);
				fl = sqrtf(vf[0] * vf[0] + vf[1] * vf[1]);
				if (fl > 0.01f)
				{
					cmd.forwardmove = (short)(400.0f *
					    (move_dir[0] * vf[0] + move_dir[1] * vf[1]) / fl);
					cmd.sidemove = (short)(400.0f *
					    (move_dir[0] * vr[0] + move_dir[1] * vr[1]));
				}
				else
				{
					cmd.forwardmove = plain_forward;
					cmd.sidemove = 0;
				}
			}
			else
			{
				cmd.forwardmove = plain_forward;
				cmd.sidemove = 0;
			}
			if (step == 0)
				cmd.upmove = nav_jump;

			/*
			 * No tricks while a rope is out -- the hook SETS velocity, so
			 * there is nothing for an off-axis input to accumulate -- and
			 * none on the final approach, where being able to stop on the
			 * flag is worth more than the speed.
			 */
			/*
			 * The weave replaces the step rather than adding to it: the
			 * strafe work above leans off the direction of TRAVEL to harvest
			 * acceleration down a route, and there is no route left to run
			 * here. `engaged` and not `duel` is the test -- a target that
			 * walked behind a wall two seconds ago is worth holding a range
			 * against, and is not worth dodging.
			 */
			if (duel_hold && engaged)
			{
				cmd.forwardmove = 0;
				cmd.sidemove = weave_side;
			}
			else if (have_move && !precision && bot->hook_phase == 0)
			{
				sg_air_t		airs;
				const sg_air_t	*airp = NULL;
				vec3_t			mf, mr;

				VectorCopy(basis_fwd, mf);
				VectorCopy(basis_right, mr);
				if (as_ok)
				{
					vec3_t vb;

					airs.lean = as_lean;
					airs.chain = as_chain;
					airs.vy_cur = bot->vy_cur;
					airs.vp_cur = bot->vp_cur;
					airp = &airs;

					/*
					 * A chain swings the VIEW, so the basis the policy
					 * decomposes against has to be the swung one -- the
					 * frame basis was built from the heading that was
					 * ASKED for, and solving a lean against a view the
					 * engine is not holding points the wish somewhere
					 * else. Rebuilt per sub-step, the same way the course
					 * decomposition above is.
					 */
					vb[YAW] = bot->vy_cur;
					vb[PITCH] = bot->vp_cur;
					if (vb[PITCH] > 180.0f)
						vb[PITCH] -= 360.0f;
					vb[PITCH] /= 3.0f;
					vb[ROLL] = 0.0f;
					AngleVectors(vb, mf, mr, NULL);
				}

				/*
				 * THE LAST TEN METERS, THE MOVEMENT HALF (406 canary
				 * forensics): the brake slowed the orbit but the strafe
				 * LEAN kept re-making it -- realign, accelerate past
				 * 200, lean off-axis, miss, brake, repeat, 80 seconds
				 * at goal-cost 200. Wave 96 ruled the walk straight;
				 * the aim obeyed, the legs never did. In terminal mode
				 * there is no lean and no hop: plain forward down the
				 * exact aim, throttle on the brake, touch.
				 */
				if (!bot->terminal)
				SG_MovePolicy(e, &cmd, mf, mr, move_dir,
				              open_ahead, run_link,
				              (float)cmd.msec / 1000.0f, airp);

				/*
				 * THE CARRIER'S JINK. The parity killer census (waves
				 * 141-146, 5v5/7v7): 30 of 64 carrier deaths to the
				 * rail, escorts standing right there for 54 of the 65
				 * carries -- a screen does not bend a hitscan line, only
				 * the target's own motion does. This is NOT the weave
				 * (2.4-D2 stands: a carrier never trades its route for a
				 * fight): forward motion is kept whole and a half-rate
				 * side component serpentines the line of travel, ~26
				 * degrees of wander for ~11%% of the pace, only while a
				 * fresh contact (3s) is believed and the legs are on the
				 * ground doing route work.
				 */
				/* open ground only: wave 147's first jink census traded
				 * rails (47%%->25%%) for WEDGES -- carrier suicides went
				 * from one every other wave to four in one, the
				 * serpentine drifting bodies into corners the feelers
				 * had already vetoed. No open room ahead, no jink: a
				 * wall stops more rails than it starts. */
				if (role == SG_ROLE_CARRY && cmd.forwardmove != 0 &&
				    open_ahead &&
				    !sg_cv.noweave->value)
				{
					int s9;

					for (s9 = 0; s9 < SG_MAX_ENEMY_TRACK; s9++)
					{
						sg_belief_enemy_t *en9 =
						    &sg_caco_enemies[team - 1][s9];

						if (en9->client >= 0 &&
						    level.time - en9->seen_time < 3.0f)
						{
							float jp = SG_WEAVE_BASE + SG_WEAVE_STEP *
							    (float)((int)(e->client - game.clients) % 10);
							short js = (fmodf(level.time, jp) < jp * 0.5f)
							           ? 1 : -1;

							cmd.sidemove = (short)(cmd.sidemove / 2
							               + js * (cmd.forwardmove > 0
							                       ? cmd.forwardmove
							                       : -cmd.forwardmove) / 2);
							break;
						}
					}
				}
			}

			/*
			 * THE SPAWN BEAT'S LEGS, applied last so nothing downstream
			 * can hand the throttle back. The breather scales the
			 * command where it is first built and SG_MovePolicy rebuilds
			 * it afterwards from the course; a beat that wants the body
			 * genuinely idling has to be the final word, and this is the
			 * only place that exists.
			 */
			if (bot->beat_until > level.time)
			{
				cmd.forwardmove = (short)(cmd.forwardmove * 0.30f);
				cmd.sidemove = (short)(cmd.sidemove * 0.30f);
			}

			/*
			 * DE-PACE (sg_depace, rung-4 cut #4). The post-zone split
			 * killed the defender hypothesis: 86-89% of the escort
			 * gap is mid-field co-travel, and the anti-linger
			 * surcharge nulled because corridors have no lateral room
			 * -- every candidate link sits inside the carrier's 400u
			 * bubble, so a spatial price has no gradient. Humans
			 * separate TEMPORALLY: varied speeds, item stops,
			 * staggered starts. A lingering non-escort eases off the
			 * throttle (cvar = the scale) until it falls out of the
			 * bubble; the carrier never slows, the convoy de-phases,
			 * and in film it reads as pacing variation, not flight.
			 */
			if (bot->linger_hot &&
			    sg_cv.depace->value > 0.0f)
			{
				float dp = sg_cv.depace->value;

				cmd.forwardmove = (short)(cmd.forwardmove * dp);
				cmd.sidemove = (short)(cmd.sidemove * dp);
			}

			/* the terminal brake: cornering throttle at the stands,
			 * same final-word slot for the same reason */
			if (bot->term_brake < 1.0f)
			{
				cmd.forwardmove = (short)(cmd.forwardmove * bot->term_brake);
				cmd.sidemove = (short)(cmd.sidemove * bot->term_brake);
			}

			ClientThink(e, &cmd);

			/*
			 * Let go of the jump. PM_CheckJump clears PMF_JUMP_HELD only when
			 * a command arrives with upmove under 10 and refuses to jump at
			 * all while it is set, so holding the key buys nothing and costs
			 * the next hop. The release lands inside the same tenth of a
			 * second as the press.
			 */
			if (cmd.upmove >= 10)
				cmd.upmove = 0;
		}
		cmd.msec = (byte)sub_msec;
	}

	/*
	 * Hook lifecycle, after the think so v_angle reflects this frame's
	 * aim. Fire with the game's own Cmd_Hook_f -- the same entry the
	 * console command uses -- and release before the rope enters the
	 * p_weapon.c brake band (ladder starts at 120; 200 leaves a frame of
	 * margin at pull speed). A rope that never attached by its deadline
	 * is cut loose.
	 */
	{
		/*
		 * A rope this bot does not think it owns is a rope it cannot ever
		 * release: g_cmds.c's Cmd_Unhook_f, when the grapple happens to be
		 * pers.weapon, only forces -attack and NEVER aborts -- the live
		 * hook's short-rope dead-stop then overwrites velocity with ~0
		 * every frame (p_weapon.c:2099-2104) and p_client.c:2834 zeroes
		 * gravity, freezing the bot in place for good (Trace, 96 seconds,
		 * 4v4 match). The bot releases through ctf_hook_abort directly --
		 * the same unconditional abort p_weapon.c itself calls -- and this
		 * guard clears any rope left over from a path we did not arm.
		 */
		if (bot->hook_phase == 0 && e->client->hookstate != 0)
			ctf_hook_abort(e);

		/* rocket-jump phase 1: ask for the launcher through the same use
		 * path a player's "use" command runs, at a polite rate */
		if (bot->rj_phase == 1 && level.time >= bot->rj_use_next)
		{
			static gitem_t *rj_rl3;

			if (!rj_rl3)
				rj_rl3 = FindItem("Rocket Launcher");
			if (rj_rl3 && rj_rl3->use)
				rj_rl3->use(e, rj_rl3);
			bot->rj_use_next = level.time + 0.5f;
		}

		if (bot->hook_phase == 1 && level.time > bot->hook_deadline)
		{
			/* the aim never arrived (blocked slew, moving anchor line,
			 * whatever): stand down clean. Without this, a wedged phase
			 * 1 gates out combat FOREVER -- wave 92's three silent
			 * games. */
			bot->hook_phase = 0;
			bot->speedhook = false;
		}
		else if (bot->hook_phase == 1)
		{
			/* the rope fires along the ACTUAL view, which now slews:
			 * hold phase 1 (a standing frame anyway) until the view has
			 * arrived within 3 degrees of the anchor bearing */
			vec3_t ad2;
			float ay, ap, ddy, ddp;

			VectorSubtract(bot->hook_anchor, e->s.origin, ad2);
			ad2[2] -= e->viewheight;
			ay = atan2f(ad2[1], ad2[0]) * 180.0f / (float)M_PI;
			ap = -atan2f(ad2[2], sqrtf(ad2[0] * ad2[0] + ad2[1] * ad2[1]))
			     * 180.0f / (float)M_PI;
			ddy = ay - bot->vy_cur;
			ddp = ap - bot->vp_cur;
			while (ddy > 180.0f) ddy -= 360.0f;
			while (ddy < -180.0f) ddy += 360.0f;
			/* (quickrope's 10-degree carrier fire read NEGATIVE
			 * pooled 216-217 -- sloppy ropes ride worse than the
			 * ritual they save. The sniper's 3 stands for all.) */
			/* THE HOP-FIRE, corrected (wave 223): the owner hops
			 * BEFORE the hook, not with it -- wave 222 put jump and
			 * fire on one command frame and the drag still started
			 * grounded. Now the hop goes in as the aim closes (8
			 * degrees); by the 3-degree fire gate the body is
			 * already airborne and the pull never meets friction. */
			if (sg_cv.hopfire->value &&
			    e->groundentity &&
			    fabsf(ddy) < 8.0f && fabsf(ddp) < 8.0f)
				cmd.upmove = 400;
			if (slew_rate > 0.0f &&
			    (fabsf(ddy) > 3.0f || fabsf(ddp) > 3.0f))
				goto hook_wait;

			/* (The anchor-distance veto lived here for one wave --
			 * 24,728 vetoes, land rate unmoved. Wrong test: bites
			 * were never the failure.) The HOOKABORT census named
			 * the real one: 2,403 bolts a wave die on the SKYBOX --
			 * a lip anchor plus three degrees of slew slack at four
			 * hundred units sails the bolt over the edge. This trace
			 * tests the ACTUAL fire line for the ACTUAL failure: if
			 * the view's own ray ends in sky, wait a frame for the
			 * slew to settle instead of donating the rope to the
			 * void. Cannot strobe a live rope; fires still happen the
			 * moment the line lands on architecture. */
			{
				vec3_t sdir, seye, send;
				trace_t str;

				sdir[0] = cosf(bot->vp_cur * (float)M_PI / 180.0f)
				        * cosf(bot->vy_cur * (float)M_PI / 180.0f);
				sdir[1] = cosf(bot->vp_cur * (float)M_PI / 180.0f)
				        * sinf(bot->vy_cur * (float)M_PI / 180.0f);
				sdir[2] = -sinf(bot->vp_cur * (float)M_PI / 180.0f);
				VectorCopy(e->s.origin, seye);
				seye[2] += e->viewheight;
				VectorMA(seye, 4096.0f, sdir, send);
				str = gi.trace(seye, NULL, NULL, send, e, MASK_SOLID);
				if (str.surface &&
				    (str.surface->flags & SURF_SKY))
				{
					if (sg_cv.debug->value)
						gi.dprintf("HOOKSKYHOLD %s\n",
						           e->client->pers.netname);
					goto hook_wait;
				}
				/* same hold for a teammate in the fire line: the bolt
				 * dies on their body (hook_touch rejects teammates) --
				 * ~57 wasted fires a wave. MASK_SHOT sees bodies where
				 * the sky trace's MASK_SOLID cannot. */
				str = gi.trace(seye, NULL, NULL, send, e, MASK_SHOT);
				if (str.ent && str.ent->client &&
				    str.ent->client->ctf.teamnum ==
				    e->client->ctf.teamnum)
				{
					if (sg_cv.debug->value)
						gi.dprintf("HOOKMATEHOLD %s\n",
						           e->client->pers.netname);
					goto hook_wait;
				}
			}
			Cmd_Hook_f(e);
			bot->hook_phase = 2;
			if (sg_cv.debug->value)
				gi.dprintf("HOOKFIRE %s at (%.0f %.0f %.0f)\n",
				           e->client->pers.netname, bot->hook_anchor[0],
				           bot->hook_anchor[1], bot->hook_anchor[2]);
		}
		else if (bot->hook_phase == 2)
		{
			vec3_t rd, td;
			float rope;
			qboolean arrived, was_pulling;

			/*
			 * NO attach-point verification, on evidence. Release
			 * conditions track the DESTINATION, so a rope biting 50-150
			 * off its proven anchor still flies a working ride -- lmctf03
			 * converted 95% that way all along. Policing the anchor
			 * (tried at 48, then 96) turned every imperfect rope into a
			 * 10Hz fire-abort strobe: 2704 aborts, 9 landings, zero
			 * kills. Rides that genuinely fail are caught where failure
			 * is real -- the field-served check at landing, which
			 * shelves the link (HOOKFAIL).
			 *
			 * But MEASURE the bite: 78-87 percent of rides fail the
			 * field test across four maps, and the two suspects for
			 * wrong bites at scale are teammates' bodies (the bolt
			 * clips MASK_SHOT, which includes players) and doors closed
			 * at runtime that generation held open for the rope-line
			 * proof. The attach entity's classname names the culprit.
			 */
			if (sg_cv.debug->value &&
			    e->client->hook && e->client->hook->hook_target &&
			    !bot->hook_bite_logged)
			{
				vec3_t ba;
				edict_t *ht = e->client->hook->hook_target;

				VectorSubtract(e->client->hook->s.origin,
				               bot->hook_anchor, ba);
				if (VectorLength(ba) > 96.0f)
					gi.dprintf("HOOKBITE %s off=%.0f into=%s org=(%.0f %.0f %.0f) want=(%.0f %.0f %.0f) got=(%.0f %.0f %.0f)\n",
					           e->client->pers.netname, VectorLength(ba),
					           ht->classname ? ht->classname :
					           (ht == g_edicts ? "world" : "?"),
					           e->s.origin[0], e->s.origin[1], e->s.origin[2],
					           bot->hook_anchor[0], bot->hook_anchor[1],
					           bot->hook_anchor[2],
					           e->client->hook->s.origin[0],
					           e->client->hook->s.origin[1],
					           e->client->hook->s.origin[2]);
				bot->hook_bite_logged = true;
			}
			{

			VectorSubtract(bot->hook_anchor, e->s.origin, rd);
			rope = VectorLength(rd);
			/*
			 * Release the way the prover released (sg_rune.c:494-502):
			 * horizontally near the DESTINATION with the height nearly
			 * made, or rope inside the brake band. The old rope<200 cut
			 * every climb loose below its lip -- the bot slid back down
			 * and re-fired the same anchor forever.
			 */
			VectorSubtract(bot->hook_dest, e->s.origin, td);
			arrived = (td[0] * td[0] + td[1] * td[1] < 80.0f * 80.0f &&
			           td[2] > -96.0f && td[2] < 96.0f);

			/*
			 * THE EARLY RELEASE -- ride the rope only as long as the
			 * rope is doing something momentum cannot. The old release
			 * conditions rode every rope to its terminus (observed
			 * live: a bot riding all the way onto the platform where a
			 * human cuts loose under the ceiling and lets the throw
			 * fling them through the door into a bhop chain). Each ride
			 * frame projects the ballistic throw: velocity toward the
			 * destination within ~35 degrees, arrival inside 1.2s, and
			 * the parabola landing within a jumpable window of the
			 * destination height -- cut NOW, fly the rest, keep every
			 * unit of speed. flow_release skips the landing brake: the
			 * whole point of the cut is what happens after it.
			 */
			if (!bot->speedhook && e->client->hookstate != 0)
			{
				float hd2 = sqrtf(td[0] * td[0] + td[1] * td[1]);
				float hv2 = sqrtf(e->velocity[0] * e->velocity[0] +
				                  e->velocity[1] * e->velocity[1]);

				if (hv2 > 300.0f && hd2 > 40.0f)
				{
					float tt = hd2 / hv2;
					float toward = (e->velocity[0] * td[0] +
					                e->velocity[1] * td[1]) / (hv2 * hd2);

					/*
					 * td[2] < 160: the cut is for HORIZONTAL finishes.
					 * Cutting a vertical climb throws the body up BESIDE
					 * the ledge lip to fall straight back down -- one
					 * shaft room turned the whole fleet into confused
					 * circlers within a wave of the feature shipping
					 * (live report, lowest port). Climbs ride to the
					 * top; that is what riding is FOR. And the parabola
					 * must clear ABOVE the destination, never scrape
					 * under it.
					 */
					if (tt < 1.2f && toward > 0.82f)
					{
						float grav = e->client->ps.pmove.gravity
						             ? (float)e->client->ps.pmove.gravity
						             : 800.0f;
						float zp = e->velocity[2] * tt
						         - 0.5f * grav * tt * tt;

						if (zp - td[2] > 24.0f && zp - td[2] < 260.0f)
						{
							/*
							 * THE BODY HAS A BOX (owner's order, wave 372):
							 * the fling arc is flown by a player-sized box,
							 * not a point. Walk the parabola in six segments
							 * with the real mins/maxs; an arc that clips
							 * architecture is not released -- keep riding the
							 * rope, which is the safe fallback and also what
							 * a human does when the fling line is not there.
							 */
							{
								vec3_t ap0, ap1;
								trace_t atr;
								int aseg;
								qboolean arc_clear = true;

								VectorCopy(e->s.origin, ap0);
								for (aseg = 1; aseg <= 6; aseg++)
								{
									float at = tt * (float)aseg / 6.0f;

									ap1[0] = e->s.origin[0] + e->velocity[0] * at;
									ap1[1] = e->s.origin[1] + e->velocity[1] * at;
									ap1[2] = e->s.origin[2] + e->velocity[2] * at
									       - 0.5f * grav * at * at;
									atr = gi.trace(ap0, e->mins, e->maxs, ap1,
									               e, MASK_PLAYERSOLID);
									/*
									 * aseg 1 exempt: the box leaves from beside the
									 * wall the rope hangs on. And a CONTACT is not a
									 * CRASH (wave 378: ~300 vetoes/game survived the
									 * seg-1 exemption -- corridor flings graze walls
									 * constantly, and pmove clip-slide carries them
									 * through). Only a head-on hit -- the segment
									 * direction driving into the plane -- kills the
									 * fling; grazes fly on. Seg 6 is landing contact.
									 */
									if (atr.fraction < 1.0f && aseg > 1 && aseg < 6)
									{
										vec3_t sd;
										float sl;
										VectorSubtract(ap1, ap0, sd);
										sl = VectorLength(sd);
										if (sl > 1.0f)
										{
											VectorScale(sd, 1.0f / sl, sd);
											/* floors don't veto: descending onto a lip
											 * near the target is a landing, not a crash
											 * (wave 380: graze fix halved vetoes but
											 * 100-300/game remained -- late-arc floor
											 * touchdowns reading as head-on). Walls and
											 * ceilings only. */
											if (atr.plane.normal[2] < 0.7f &&
											    DotProduct(sd, atr.plane.normal) < -0.7f)
											{
												arc_clear = false;
												break;
											}
										}
									}
									VectorCopy(ap1, ap0);
								}
								if (!arc_clear)
								{
									if (sg_cv.debug->value)
										gi.dprintf("HOOKARCVETO %s\n",
										           e->client->pers.netname);
									goto hook_wait;
								}
							}
							ctf_hook_abort(e);
							bot->hook_phase = 3;
							bot->flow_release = true;
							bot->hook_deadline = level.time + 1.4f;
							bot->hookfail_streak = 0;
							/*
							 * THE FALL-THROUGH (found wave 285): without
							 * this exit, control ran on into the release
							 * chain below, which saw the rope this cut
							 * just killed (hookstate 0), printed a false
							 * "noattach", and reset hook_phase to 0 --
							 * destroying the flow ride armed two lines
							 * up. 91% of the noattach mass, ~3,200 rides
							 * a wave, were the bots' BEST hooks being
							 * reported as their failures and stripped of
							 * the landing steer/pre-turn/brake.
							 */
							goto hook_wait;
						}
					}
				}
			}

			if (bot->speedhook)
			{
				/* the burst release: cut at speed and KEEP it -- no
				 * landing brake, no shelf accounting, phase straight to
				 * zero and the legs inherit 600+ */
				float bs2 = e->velocity[0] * e->velocity[0]
				          + e->velocity[1] * e->velocity[1]
				          + e->velocity[2] * e->velocity[2];

				if (bs2 > 600.0f * 600.0f ||
				    level.time > bot->hook_deadline ||
				    e->client->hookstate == 0)
				{
					if (e->client->hookstate != 0)
						ctf_hook_abort(e);
					/* the burst's three ends, named: reached speed
					 * (the SILENT SUCCESS that made the land-rate
					 * denominator a lie), stalled to deadline, or
					 * the rope never held */
					if (sg_cv.debug->value)
						gi.dprintf("HOOKEND %s %s\n",
						           e->client->pers.netname,
						           bs2 > 600.0f * 600.0f ? "burst"
						           : (e->client->hookstate == 0
						              ? "noattach" : "burststall"));
					bot->hook_phase = 0;
					bot->speedhook = false;
					bot->commit_link = -1;
				}
			}
			else if (arrived || rope < 130.0f ||
			    level.time > bot->hook_deadline || e->client->hookstate == 0)
			{
				was_pulling = (e->client->hookstate != 0);
				if (was_pulling)
					ctf_hook_abort(e);
				/* a cut live rope hands off to the landing steer; a rope
				 * that never attached does not */
				if (!was_pulling && sg_cv.debug->value)
					gi.dprintf("HOOKEND %s noattach\n",
					           e->client->pers.netname);
				bot->hook_phase = was_pulling ? 3 : 0;
				/* this phase-3 entry is a plain rope cut, never a flow
				 * release -- the flag used to stay latched from an
				 * earlier wiped release and made ~1,000 rides a wave
				 * take the apex cut and skip the landing brake */
				bot->flow_release = false;
				bot->hook_deadline = level.time + 1.0f;
			}
			}
		}
	}

hook_wait:;
	/* the literal emission record: what this frame's usercmd contained */
	if (sg_cv.debug->value >= 2 || 
	    (sg_cv.debug->value && level.time >= bot->next_cmdlog))
	{
		bot->next_cmdlog = level.time + 1.0f;
		/* the last step of the frame: fwd/side/up are that step's command,
		 * and msec x steps is how the frame's real time was spent */
		gi.dprintf("CMD %s: fwd=%d side=%d up=%d btn=%d yaw=%d pitch=%d msec=%d x%d\n",
		           e->client->pers.netname, cmd.forwardmove, cmd.sidemove,
		           cmd.upmove, cmd.buttons, cmd.angles[YAW],
		           cmd.angles[PITCH], cmd.msec, sub_steps);
	}

	/* once a second, the full body state: enough to reconstruct any stall
	 * offline without another instrumented rerun */
	if (sg_cv.debug->value && level.time >= bot->next_report)
	{
		float sp = sqrtf(e->velocity[0] * e->velocity[0] +
		                 e->velocity[1] * e->velocity[1]);
		/* sgoal: the bot's cost on the STATIC field for its role's true
		 * destination (attacker -> enemy stand, everyone else -> own).
		 * The composed goal= number rebuilds under a standing bot --
		 * item beliefs, danger, re-floods -- and wave 169's traces
		 * caught a stationary attacker "receding" 800ms/s in it. Route
		 * progress gets measured against a number that only moves when
		 * the body does. */
		int sgoal = -1;
		const int *sfld = (role == SG_ROLE_ATTACK)
		    ? ((team == CTF_TEAM_RED) ? sg_fields.to_blue_flag
		                              : sg_fields.to_red_flag)
		    : ((team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                              : sg_fields.to_blue_flag);

		if (bot->seed >= 0 && sfld && sfld[bot->seed] < SG_FIELD_INF)
			sgoal = sfld[bot->seed];
		bot->next_report = level.time + 1.0f;
		gi.dprintf("SG %s: role=%d seed=%d goal=%d sgoal=%d spd=%.0f org=(%.0f %.0f %.0f) link=%d "
		           "act=%d hp=%d dh=%d dl=%d st=%.1f gnd=%d eng=%d\n",
		           e->client->pers.netname, role, bot->seed,
		           (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
		               ? goal_field[bot->seed] : -1,
		           sgoal,
		           sp, e->s.origin[0], e->s.origin[1], e->s.origin[2],
		           bestlink,
		           (bestlink >= 0) ? sg_rune->links[bestlink].action : -1,
		           bot->hook_phase, door_hold, (int)drop_yaw_locked,
		           bot->stuck_time, e->groundentity != NULL,
		           (int)bot->engaged_last);
	}

	/*
	 * The same once-a-second cadence, for the eye instead of the log
	 * (sg_drawplan). Its own clock, because the debug report above is
	 * gated on sg_debug and the two are useful separately.
	 */
	if (level.time >= bot->plan_next)
	{
		bot->plan_next = level.time + 1.0f;
		SG_DrawPlan(bot, team, bestlink, route_field);
	}
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


