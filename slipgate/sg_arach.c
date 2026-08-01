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
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"       /* human orders replace the role quota */

/*
 * The client lifecycle and the glue's edict helpers are declared where the
 * legacy bot spawner declares them -- locally, the way this tree does it
 * (see bl_spawn.c:26-28). Same functions, same signatures.
 */
void		ClientThink(edict_t *ent, usercmd_t *ucmd);
qboolean	ClientConnect(edict_t *ent, char *userinfo);
void		ClientBegin(edict_t *ent);
void		ClientUserinfoChanged(edict_t *ent, char *userinfo);
edict_t		*G_SpawnClient(void);
void		G_FreeClientEdict(edict_t *ent);

#define SG_MAXBOTS      16
#define FIELD_INF       0x3fffffff

typedef struct sg_bot_s
{
	edict_t		*ent;
	qboolean	active;
	int			seed;           /* seed we believe we are at/near */
	float		stuck_time;     /* accumulated time without progress */
	float		next_report;
	float		next_cmdlog;
	vec3_t		last_origin;

	/* hook execution, two-phase: aim this frame (ClientThink turns the cmd
	 * angles into v_angle), fire immediately after, since Weapon_Hook_Fire
	 * launches along v_angle; then release before the p_weapon.c brake band */
	int			hook_phase;     /* 0 none, 1 aimed+firing, 2 rope out,
	                             * 3 released mid-air, steering to land */
	int			hook_link;      /* which link this ride is executing */
	qboolean	hook_bite_logged;   /* one HOOKBITE line per ride */
	float		hook_landbrake; /* stand the landing like the proof did:
	                             * the phantom ARRIVED at a stop; a body
	                             * at 343 skids off the narrow step and
	                             * falls back into the basin it climbed
	                             * out of (iter-19 lmctf03, Gate) */
	vec3_t		hook_anchor;
	vec3_t		hook_dest;      /* the link's destination seed origin */
	float		hook_deadline;

	/* a link chosen for seconds while the bot goes nowhere is a link the
	 * body cannot execute, whatever the rune thinks -- shelve it awhile.
	 * 32 slots: one doorway feeds 25 links from a single seed (662), and
	 * an 8-slot shelf recycled them faster than they expired. */
#define SG_BL_MAX 32
	int			bl_link[SG_BL_MAX];
	float		bl_until[SG_BL_MAX];

	/* a door that would not yield from this side: a wall, for a while.
	 * bd2's triggers are all SOUTH of it -- approached from the north it
	 * simply does not open, and links proven with doors held open are
	 * runtime lies from that side (Trace: 396 of 416 seconds pinned). */
#define SG_DEAD_DOORS 4
	edict_t		*dead_door[SG_DEAD_DOORS];
	float		dead_door_until[SG_DEAD_DOORS];
	edict_t		*door_hold_ent;     /* the door currently being waited on */
	float		door_hold_since;
	qboolean	deaddoor_ahead;     /* last frame's goal line hit a door
	                                 * already known dead: shelve fast */
	vec3_t		deaddoor_spot;      /* where that dead door was struck */
	qboolean	door_held_last;     /* stood still for a door last frame:
	                                 * commanded stillness, not link failure */
	qboolean	mate_block_last;    /* a TEAMMATE was the obstruction: not
	                                 * the link's failure either */
	qboolean	def_stand;          /* this defender is the stand statue;
	                                 * false = the patrol, which never pins */
	qboolean	was_carrying;       /* for the carry-duration bookend */
	float		carry_start;
	int			last_role;          /* role-transition observability */
	qboolean	death_taught;       /* one danger lesson per death */

	/* loop detection wider than the watch's 96-unit ball: recent seeds
	 * visited with the goal value each visit held. Coming back no better
	 * is an orbit whatever its diameter (a carrier hook-cycled a 250-unit
	 * triangle for minutes; the ball never saw it) */
#define SG_VISIT_RING 8
	int			visit_seed[SG_VISIT_RING];
	int			visit_goal[SG_VISIT_RING];  /* the seed's value at visit */
	int			visit_min[SG_VISIT_RING];   /* best goal reached SINCE */
	float		visit_time[SG_VISIT_RING];
	int			visit_head;

	/* rocket-jump execution: the proof stored the aim (anchor[0/1], z
	 * recoverable) and the worst-case health price (anchor[2]); the body
	 * pays it only with the launcher up and the margin in hand */
	int			rj_phase;           /* 0 none, 1 raising RL, 2 aim+fire,
	                                 * 3 flying the arc */
	vec3_t		rj_aim;             /* unit vector the proof fired on */
	vec3_t		rj_dest;
	float		rj_deadline;
	float		rj_fire_until;      /* how long phase 2 holds the trigger */
	float		rj_use_next;        /* weapon-switch request rate limit */
	int			watch_link;     /* the link under progress-watch */
	float		watch_since;
	vec3_t		watch_org;
	int			commit_link;    /* the gradient step being held */
	float		commit_until;
	vec3_t		stag_org;       /* stagnation ball on the BODY, not the
	                             * link: the identity watch above resets
	                             * whenever the argmin flaps, and two
	                             * near-equal links flapping at the commit
	                             * period parked Fiend on one drop lip for
	                             * a full lmctf01 match (iter 41) */
	float		stag_since;
	float		stag_next;      /* escalation: one shelve per 2s while parked */
	qboolean	nav_drove;      /* last frame, navigation drove the legs */
	qboolean	engaged_last;   /* last frame, combat owned the fight */
	int			fan_side;       /* latched detour side: -1 left, +1 right */
	float		fan_side_until;
	float		escape_until;   /* backing out of a concave pocket */
	float		escape_yaw;
	int			rail_link;      /* RUN link being retried the proof's way */
	int			rail_stage;     /* 0 off, 1 walk to from-seed, 2 drive line */
	float		rail_until;
} sg_bot_t;

static sg_bot_t	sg_bots[SG_MAXBOTS];
static rune_t	*sg_rune;

rune_t *SG_Rune(void)
{
	return sg_rune;
}

/* the danger dimension, defined with its kin further down */
static void Danger_Load(void);
static void Danger_Save(void);
static void Danger_Decay(void);
static char		sg_rune_map[64];

/* one cost field per flag: cost_ms from every seed TO the flag seed */
static int		*sg_field_red;
static int		*sg_field_blue;

/* ------------------------------------------------------------------ rune */

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

static qboolean SG_LevelSetup(void)
{
	if (sg_rune && Q_stricmp(sg_rune_map, level.mapname) == 0)
		return true;

	sg_rune = Rune_Load(level.mapname);
	if (!sg_rune)
	{
		gi.dprintf("slipgate: no rune for %s -- run 'sv rune' first\n",
		           level.mapname);
		return false;
	}
	Danger_Load();      /* what past matches taught about this map */
	/* Com_sprintf is the tree's own bounded copy (q_shared.c) and always
	 * terminates; strncpy at sizeof-1 does not, which is what -Wall's
	 * stringop-truncation was reporting here. Same call Rune_Load uses. */
	Com_sprintf(sg_rune_map, sizeof(sg_rune_map), "%s", level.mapname);

	if (!Fields_Setup(sg_rune))
	{
		gi.dprintf("slipgate: field setup failed (no flags?)\n");
		sg_rune = NULL;
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
 * The weight tables: the one fitted component. Rows are roles; every other
 * number in the system is a measured fact. Starting values encode the
 * owner's specification directly -- attackers live for the enemy flag and
 * take what is on the way, defenders deny armour near home, carriers value
 * health and armour and the way home, everyone values interception when our
 * flag is out and believed seen.
 */
static const sg_weights_t sg_weight_table[SG_ROLES] = {
	/* objective  weap  armr  ammo  hlth  rune  powr   support intercept */
	{ 1.00f, { 0.35f, 0.30f, 0.20f, 0.15f, 0.20f, 0.40f }, 0.10f, 0.60f },  /* attack */
	{ 1.00f, { 0.30f, 0.50f, 0.25f, 0.20f, 0.15f, 0.10f }, 0.40f, 0.80f },  /* defend */
	{ 1.00f, { 0.10f, 0.45f, 0.20f, 0.50f, 0.10f, 0.05f }, 0.30f, 0.00f },  /* carry */
	/*
	 * recover: our flag is out there. The objective is the flag where belief
	 * puts it, the shopping list is the defender's (armour near home, not
	 * powerups across the map), and the thief's believed position outweighs
	 * everything else a non-carrier can be doing -- intercept above 1.
	 */
	{ 1.00f, { 0.30f, 0.50f, 0.25f, 0.20f, 0.15f, 0.10f }, 0.00f, 1.20f },  /* recover */
	/*
	 * escort: the objective IS the carrier's field, so carrier_support is 0
	 * (it would price the same field twice). Items stay modest so the escort
	 * does not wander off the carrier's road for them; intercept keeps the
	 * escort between the carrier and whoever is believed to be hunting.
	 */
	{ 1.00f, { 0.10f, 0.25f, 0.10f, 0.25f, 0.05f, 0.10f }, 0.00f, 0.80f },  /* escort */
};

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
			return tab[i].w[role];
	return 1.0f;
}

/* the role whose surface is being evaluated this frame -- SLIPGATE runs
 * its bots strictly serially, so a file-static carries it into the
 * detour arithmetic without widening every signature on the path */
static int sg_cur_role;

/*
 * The DANGER dimension: deaths teach the map. Team-indexed -- a corridor
 * lethal for red is safe for blue -- fed by each bot registering its own
 * death at its own seed (self-knowledge, the most honest sighting there
 * is), decayed a little every second, priced into the same descent as
 * every other dimension, and persisted beside the rune so the next match
 * on this map starts educated. The rune is a higher-order surface; this
 * is one more dimension of it.
 */
static int		sg_danger[2][SG_MAX_SEEDS];
static const int *sg_cur_danger;
static float	sg_danger_decay_next;

static void Danger_Learn(int team, int seed)
{
	if (team < 1 || team > 2 || seed < 0 || seed >= SG_MAX_SEEDS)
		return;
	sg_danger[team - 1][seed] += 1200;      /* fitted: ~a detour's worth */
	if (sg_danger[team - 1][seed] > 8000)
		sg_danger[team - 1][seed] = 8000;
}

static void Danger_Decay(void)
{
	int t, i;

	if (level.time < sg_danger_decay_next)
		return;
	sg_danger_decay_next = level.time + 1.0f;
	for (t = 0; t < 2; t++)
		for (i = 0; i < SG_MAX_SEEDS; i++)
			if (sg_danger[t][i])
				sg_danger[t][i] -= (sg_danger[t][i] >> 6) + 1;
}

static void Danger_Path(char *buf, int size)
{
	Com_sprintf(buf, size, "%s/maps/%s.rune.danger",
	            "lmctf-hooktest", sg_rune_map);
}

static void Danger_Save(void)
{
	char path[MAX_OSPATH];
	FILE *f;

	if (!sg_rune)
		return;
	Danger_Path(path, sizeof(path));
	f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(sg_danger, sizeof(sg_danger), 1, f);
	fclose(f);
}

static void Danger_Load(void)
{
	char path[MAX_OSPATH];
	FILE *f;

	memset(sg_danger, 0, sizeof(sg_danger));
	Danger_Path(path, sizeof(path));
	f = fopen(path, "rb");
	if (!f)
		return;
	if (fread(sg_danger, sizeof(sg_danger), 1, f) != 1)
		memset(sg_danger, 0, sizeof(sg_danger));
	fclose(f);
}

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
	int carrier_rank = -1;
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
		if (own->client >= 0 &&
		    (int)(sg_bots[i].ent - g_edicts) - 1 == own->client)
			carrier_rank = size;
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
		int escort_rank;

		defenders_wanted = ours_astray ? 1 : 2;
		if (size <= 1)
			defenders_wanted = 0;
		else if (size == 2)
			defenders_wanted = 1;
		/* a live carrier on our side counts toward the defensive share */
		if (own->client >= 0 && !ours_astray)
			defenders_wanted--;
		if (defenders_wanted < 0)
			defenders_wanted = 0;

		/* role-flap diagnostic: two bots alternated DEFEND/ATTACK every
		 * frame of it18 (600 flips/600 samples) -- print the decision
		 * inputs on each change so the oscillating input names itself */
		if (gi.cvar("sg_debug", "0", 0)->value)
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

		/* one escort whenever we have a live carrier that is not me */
		if (have_carrier && own->client != my_client)
		{
			escort_rank = defenders_wanted;
			if (escort_rank == carrier_rank)
				escort_rank++;
			if (my_rank == escort_rank)
				return SG_ROLE_ESCORT;
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

	v = w->objective * (float)goal_field[seed];

	/* the danger dimension: learned, decayed, team-indexed (set by the
	 * caller alongside sg_cur_role); zero where nothing has died */
	if (sg_cur_danger && seed < SG_MAX_SEEDS)
		v += (float)sg_cur_danger[seed];

	for (c = 0; c < SG_FIELD_CLASSES; c++)
		if (w->item[c] > 0.0f)
			v -= 1500.0f * Detour_Value(seed, c, goal_field, w->item[c]);

	if (support && w->carrier_support > 0.0f && support[seed] < SG_FIELD_INF)
		v += w->carrier_support * (float)support[seed];

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

	accelspeed = accel * frametime * wishspeed;

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
static void SG_MovePolicy(edict_t *e, usercmd_t *cmd, vec3_t fwd,
                          vec3_t right, vec3_t dir,
                          qboolean open_ahead, qboolean run_link,
                          float frametime)
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
		if (run_link && open_ahead && sp > 320.0f && !(pmf & PMF_TIME_LAND))
			cmd->upmove = 400;

		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 10.0f);
	}
	else
		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 1.0f);
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

static void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	usercmd_t cmd;
	const int *goal_field, *support = NULL, *intercept = NULL;
	const sg_weights_t *w;
	sg_role_t role;
	int team, li, bestlink = -1;
	float bestval;
	vec3_t want, d;
	qboolean carrying;
	static int intercept_field[SG_MAX_SEEDS];

	/* movement policy state for this frame */
	vec3_t		basis_fwd, basis_right;     /* the basis pmove will build */
	vec3_t		move_dir;                   /* heading the route wants */
	float		view_yaw = 0.0f, view_pitch = 0.0f;
	qboolean	have_move = false;          /* a direction to travel at all */
	qboolean	open_ahead = false;         /* room in front to hop into */
	qboolean	run_link = false;           /* chosen link is ground running */
	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
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

	/* the duel terms, read once per frame and priced per candidate seed */
	qboolean	duel = false;               /* combat has a live or fresh target */
	vec3_t		duel_org;                   /* where it is believed to be */
	float		duel_want = 0.0f;           /* range the weapon in hand wants */
	float		duel_expo = 0.0f;           /* what being seen costs, 0 to ~1 */
	qboolean	duel_hold = false;          /* the chosen step is short: weave */
	short		weave_side = 0;             /* this frame's strafe sign */

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;

	if (e->deadflag)
	{
		/* my own death, at my own seed: the most honest sighting there
		 * is, and the danger dimension's only teacher */
		if (bot->seed >= 0 && !bot->death_taught)
		{
			Danger_Learn(e->client->ctf.teamnum, bot->seed);
			bot->death_taught = true;
		}
		bot->seed = -1;
		cmd.buttons = BUTTON_ATTACK;
		ClientThink(e, &cmd);
		return;
	}
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

	/* carry bookends and role transitions: short carries slip between the
	 * 1Hz samples, so the transitions themselves get lines */
	if (gi.cvar("sg_debug", "0", 0)->value)
	{
		if (carrying && !bot->was_carrying)
		{
			bot->carry_start = level.time;
			gi.dprintf("CARRY %s begins\n", e->client->pers.netname);
		}
		else if (!carrying && bot->was_carrying)
			gi.dprintf("CARRY %s ends after %.1fs\n",
			           e->client->pers.netname,
			           level.time - bot->carry_start);
		if ((int)role != bot->last_role)
		{
			if (role == SG_ROLE_ESCORT)
				gi.dprintf("ESCORT %s begins\n", e->client->pers.netname);
			bot->last_role = (int)role;
		}
	}
	bot->was_carrying = carrying;
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
	SG_CombatWeights(e, &sg_weight_table[role], &live);
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
				live.item[SG_FC_RUNE] *=
				    Caco_EnemyHasDamageRune(team) ? 1.80f : 1.45f;
				if (live.item[SG_FC_RUNE] > 2.0f)
					live.item[SG_FC_RUNE] = 2.0f;
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
		if (live.item[SG_FC_ARMOR] < 1.1f)  live.item[SG_FC_ARMOR] = 1.1f;
		if (live.item[SG_FC_HEALTH] < 1.0f) live.item[SG_FC_HEALTH] = 1.0f;
		if (live.item[SG_FC_AMMO] < 1.0f)   live.item[SG_FC_AMMO] = 1.0f;
	}
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
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                                    : sg_fields.to_blue_flag;
	else if (role == SG_ROLE_RECOVER)
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag_now
		                                    : sg_fields.to_blue_flag_now;
	else if (role == SG_ROLE_ESCORT)
	{
		edict_t *ht = SG_ChatEscortTarget(e);

		goal_field = sg_fields.our_carrier[team - 1];
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

	if (role != SG_ROLE_CARRY)
	{
		sg_belief_carrier_t *ec = &sg_caco_team_belief.enemy_carrier[team - 1];

		support = sg_fields.our_carrier[team - 1];
		if (ec->seed >= 0)
		{
			int cost = 0;
			int hold = Intercept_HoldSeed(team, ec->seed);

			/* the hold ground across the thief's projected motion --
			 * or their believed position when the projection is thin */
			Field_Flood(sg_rune, intercept_field, &hold, &cost, 1);
			intercept = intercept_field;
		}
	}

	/* where am I on the rune? */
	VectorSubtract(e->s.origin, bot->last_origin, d);
	if (bot->seed < 0 || VectorLength(d) > 48.0f)
	{
		bot->seed = Rune_NearestSeed(sg_rune, e->s.origin);
		VectorCopy(e->s.origin, bot->last_origin);
	}
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
	sg_cur_role = role;             /* for the rune identity pricing */
	sg_cur_danger = sg_danger[team - 1];    /* the danger dimension, ours */
	bestval = Surface_At(bot->seed, w, goal_field, support, intercept);
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
	for (li = sg_rune->first_link[bot->seed]; li >= 0; li = sg_rune->next_link[li])
	{
		rune_link_t *l = &sg_rune->links[li];
		float v = Surface_At(l->to, w, goal_field, support, intercept);
		int b;

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
		if (role == SG_ROLE_CARRY)
		{
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
						v += 3.0f * (400.0f - VectorLength(d));
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
		else if (duel)
		{
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

		if (v < bestval)
		{
			bestval = v;
			bestlink = li;
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
			if (gi.cvar("sg_debug", "0", 0)->value)
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
			if (gi.cvar("sg_debug", "0", 0)->value)
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
		if (gi.cvar("sg_debug", "0", 0)->value)
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
					if (gi.cvar("sg_debug", "0", 0)->value)
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
			if (gi.cvar("sg_debug", "0", 0)->value)
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
	 */
	if (role == SG_ROLE_DEFEND && bot->def_stand &&
	    goal_field[bot->seed] < 400)
	{
		int facev = 0x7fffffff, face = -1;
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
		hold_post = true;
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

		if (bot->hook_phase == 3)
		{
			if (e->groundentity || level.time > bot->hook_deadline)
			{
				if (e->groundentity && gi.cvar("sg_debug", "0", 0)->value)
				{
					vec3_t ld;

					VectorSubtract(bot->hook_dest, e->s.origin, ld);
					gi.dprintf("HOOKLAND %s dist=%.0f dz=%.0f\n",
					           e->client->pers.netname,
					           sqrtf(ld[0] * ld[0] + ld[1] * ld[1]), ld[2]);
				}
				bot->hook_phase = 0;
				bot->hook_landbrake = level.time + 0.3f;
				/* a rope ride ENDS its commitment: wherever this landing
				 * is, the next step is argued fresh from here */
				bot->commit_link = -1;

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
						if (gi.cvar("sg_debug", "0", 0)->value)
							gi.dprintf("HOOKFAIL %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
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
			have_aim = true;
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
					VectorCopy(l->anchor, aim);
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
				run_link = true;

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
		else if (bot->seed >= 0)
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

			if (role == SG_ROLE_ATTACK)
			{
				/* enemy stand position is common knowledge */
				gf = G_Find(NULL, FOFS(classname),
				            (team == CTF_TEAM_RED) ? "info_flag_blue"
				                                   : "info_flag_red");
			}
			else
			{
				gf = G_Find(NULL, FOFS(classname),
				            (team == CTF_TEAM_RED) ? "info_flag_red"
				                                   : "info_flag_blue");
			}
			if (gf)
			{
				VectorCopy(gf->s.origin, aim);
				have_aim = true;
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
			for (k = 0; k < 9; k++)
			{
				static const float fan[9] = { 0, -30, 30, -60, 60, -100, 100,
				                              -145, 145 };
				float score;

				try_yaw = (base_yaw + fan[k]) * M_PI / 180.0f;
				fwd[0] = cosf(try_yaw); fwd[1] = sinf(try_yaw); fwd[2] = 0;
				VectorMA(e->s.origin, 96.0f, fwd, probe);
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
			if (chosen_yaw != base_yaw)
			{
				int side = (chosen_yaw > base_yaw) ? 1 : -1;

				if (bot->fan_side != side || level.time >= bot->fan_side_until)
					bot->fan_side_until = level.time + 0.7f;
				bot->fan_side = side;
			}

			/* at a drop lip the proven walk-off heading overrides the fan:
			 * the proof is a line, and the line is the record's */
			if (drop_yaw_locked)
				chosen_yaw = drop_yaw;

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
						if (gi.cvar("sg_debug", "0", 0)->value)
							gi.dprintf("RAILFAIL %s link=%d seed=%d\n",
							           e->client->pers.netname,
							           bestlink, bot->seed);
					}
					else if (gi.cvar("sg_debug", "0", 0)->value)
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
			if (bot->hook_phase == 1)
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
			VectorMA(e->s.origin, 160.0f, move_dir, probe);
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
		 * standing where the phantom stood, then argue the next step */
		if (level.time < bot->hook_landbrake && e->groundentity)
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
				if (gi.cvar("sg_debug", "0", 0)->value)
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
		    e->air_finished - level.time < 4.0f)
		{
			cmd.angles[PITCH] = ANGLE2SHORT(-85.0f)
			                  - e->client->ps.pmove.delta_angles[PITCH];
			cmd.forwardmove = 400;
			cmd.upmove = 400;
			view_pitch = -85.0f;
			bot->nav_drove = false;     /* not the route's fault */
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
	if (duel && role != SG_ROLE_CARRY && !precision && bot->hook_phase == 0)
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
	{
		int		total = cmd.msec;
		int		sub = (int)gi.cvar("sg_subframes", "8", 0)->value;
		int		base, rem, step;
		short	plain_forward = cmd.forwardmove;
		short	nav_jump = cmd.upmove;
		/* combat's own answer to "is there a fight on RIGHT NOW", as opposed
		 * to the up-to-two-seconds-old belief the surface terms were priced
		 * from. The weave below needs the live one. */
		qboolean engaged = false;

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
		    bot->rj_phase == 0)
			SG_CombatFrame(e, &cmd, &engaged);
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

		for (step = 0; step < sub; step++)
		{
			cmd.msec = (byte)(base + (step < rem ? 1 : 0));
			if (!cmd.msec)
				continue;
			sub_msec = cmd.msec;

			/*
			 * Every step starts from the plain command -- forward down the
			 * view, no strafe -- so anything the policy declines to do leaves
			 * ordinary running behind. The navigation jump (a jump LINK, or
			 * the stuck hop) belongs to the frame, not to every step, so it
			 * goes in once and is released like any other.
			 */
			cmd.forwardmove = plain_forward;
			cmd.sidemove = 0;
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
				SG_MovePolicy(e, &cmd, basis_fwd, basis_right, move_dir,
				              open_ahead, run_link,
				              (float)cmd.msec / 1000.0f);

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
		void Cmd_Hook_f(edict_t *ent);

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

		if (bot->hook_phase == 1)
		{
			Cmd_Hook_f(e);
			bot->hook_phase = 2;
			if (gi.cvar("sg_debug", "0", 0)->value)
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
			if (gi.cvar("sg_debug", "0", 0)->value &&
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
			if (arrived || rope < 130.0f ||
			    level.time > bot->hook_deadline || e->client->hookstate == 0)
			{
				was_pulling = (e->client->hookstate != 0);
				if (was_pulling)
					ctf_hook_abort(e);
				/* a cut live rope hands off to the landing steer; a rope
				 * that never attached does not */
				bot->hook_phase = was_pulling ? 3 : 0;
				bot->hook_deadline = level.time + 1.0f;
			}
			}
		}
	}

	/* the literal emission record: what this frame's usercmd contained */
	if (gi.cvar("sg_debug", "0", 0)->value >= 2 || 
	    (gi.cvar("sg_debug", "0", 0)->value && level.time >= bot->next_cmdlog))
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
	if (gi.cvar("sg_debug", "0", 0)->value && level.time >= bot->next_report)
	{
		float sp = sqrtf(e->velocity[0] * e->velocity[0] +
		                 e->velocity[1] * e->velocity[1]);
		bot->next_report = level.time + 1.0f;
		gi.dprintf("SG %s: role=%d seed=%d goal=%d spd=%.0f org=(%.0f %.0f %.0f) link=%d "
		           "act=%d hp=%d dh=%d dl=%d st=%.1f gnd=%d\n",
		           e->client->pers.netname, role, bot->seed,
		           (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
		               ? goal_field[bot->seed] : -1,
		           sp, e->s.origin[0], e->s.origin[1], e->s.origin[2],
		           bestlink,
		           (bestlink >= 0) ? sg_rune->links[bestlink].action : -1,
		           bot->hook_phase, door_hold, (int)drop_yaw_locked,
		           bot->stuck_time, e->groundentity != NULL);
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

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->inuse)
			SG_BotThink(&sg_bots[i]);
}

/* ---------------------------------------------------------------- spawn */

/*
 * Sixteen names, because `slot & 7` on a ten-bot 5v5 fielded TWO Arachs
 * and TWO Cacos in every game since the format began -- and every
 * per-name analysis quietly merged two different bots (the it18 "role
 * flap" was two same-named clients interleaving in the telemetry, and
 * the ghost was hunted with a printf). Names are identity; identity is
 * data.
 */
static const char *sg_names[] = {
	"Arach", "Caco", "Rune", "Slip", "Gate", "Phase", "Field", "Trace",
	"Vore", "Fiend", "Scrag", "Ogre", "Knight", "Wizard", "Spawn", "Shal",
};

/*
 * Ownership, for the legacy glue to ask. Two bot systems share the match --
 * that is the A/B harness working -- and the old code's FL_BOT loops must
 * not assume every bot is theirs.
 */
qboolean SG_OwnsBot(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
			return true;
	return false;
}

qboolean SG_AddBot(void)
{
	edict_t *ent;
	char userinfo[MAX_INFO_STRING];
	int i, slot = -1;

	if (!SG_LevelSetup())
		return false;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (!sg_bots[i].active)
		{
			slot = i;
			break;
		}
	if (slot < 0)
		return false;

	memset(userinfo, 0, sizeof(userinfo));
	Info_SetValueForKey(userinfo, "name", va("%s[SG]", sg_names[slot & 15]));
	Info_SetValueForKey(userinfo, "skin", "male/grunt");
	Info_SetValueForKey(userinfo, "hand", "0");

	ent = G_SpawnClient();
	if (!ent)
		return false;
	ent->flags &= ~FL_BOT;
	ent->inuse = false;
	if (!ClientConnect(ent, userinfo))
	{
		G_FreeClientEdict(ent);
		return false;
	}
	ent->inuse = true;
	ent->flags |= FL_BOT;
	ClientUserinfoChanged(ent, userinfo);
	ClientBegin(ent);

	sg_bots[slot].ent = ent;
	sg_bots[slot].active = true;
	sg_bots[slot].seed = -1;
	sg_bots[slot].stuck_time = 0.0f;

	gi.dprintf("slipgate: %s entered\n",
	           Info_ValueForKey(userinfo, "name"));
	return true;
}

int SG_RemoveBots(void)
{
	int i, n = 0;
	void ClientDisconnect(edict_t *ent);

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active)
			continue;
		if (sg_bots[i].ent && sg_bots[i].ent->inuse)
		{
			ClientDisconnect(sg_bots[i].ent);
			G_FreeClientEdict(sg_bots[i].ent);
		}
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
		n++;
	}
	return n;
}

void SG_LevelChange(void)
{
	int i;

	Danger_Save();      /* the map's lessons outlive the level */

	/* rune and fields were TAG_LEVEL -- the engine freed them */
	sg_rune = NULL;
	sg_field_red = sg_field_blue = NULL;
	sg_rune_map[0] = 0;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
	}
}
