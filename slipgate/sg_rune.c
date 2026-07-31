/*
 * sg_rune.c -- generating what the map affords.
 *
 * Seeding: players verifiably stand where the map put things -- spawn
 * points, items, flags. Those are the germs. From each, candidates spread
 * outward on a lattice; each candidate is kept only if a player-sized box
 * can stand there (trace down finds a floor, the box does not start solid).
 * The spread repeats from every kept seed until nothing new survives, so
 * the seed set grows to cover exactly the ground a player could reach by
 * existing there -- not the void, not the unreachable dark.
 *
 * Proving: for every pair of seeds within reach of each other, the oracle
 * rolls the real physics: stand a phantom on the source, aim it at the
 * target, feed it honest usercmds (run first; run-and-jump if plain running
 * failed), and watch. Arrival within tolerance writes a link with the real
 * elapsed time as its cost and the arrival speed as its exit state. No
 * arrival, no link -- there is no third outcome and no guessing.
 *
 * This first cut proves RL_RUN and RL_JUMP only. Hooks, drops and swims
 * follow once the visual dump has validated the foundation by eye, per the
 * build order in SLIPGATE.md.
 */

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_rune.h"

#define SEED_SPACING	64.0f
#define SEED_MAX		32768
#define LINK_MAX		262144
#define LINK_REACH		192.0f		/* prove pairs within this many units */
#define ARRIVE_RADIUS	40.0f
#define STEP_MSEC		25			/* honest client-rate steps, 4 per frame */
#define TRY_LIMIT_MS	3000		/* a link longer than this is not local */

static rune_seed_t	*gen_seeds;
static int			gen_num_seeds;
static rune_link_t	*gen_links;
static int			gen_num_links;

/* spatial hash so the lattice dedupes at SEED_SPACING */
#define HASH_SIZE 4096
static int hash_head[HASH_SIZE];
static int hash_next[SEED_MAX];

static int Seed_HashKey(vec3_t p)
{
	int x = (int)floorf(p[0] / SEED_SPACING);
	int y = (int)floorf(p[1] / SEED_SPACING);
	int z = (int)floorf(p[2] / (SEED_SPACING * 2.0f));
	return ((x * 73856093) ^ (y * 19349663) ^ (z * 83492791)) & (HASH_SIZE - 1);
}

static qboolean Seed_Nearby(vec3_t p)
{
	int key = Seed_HashKey(p);
	int i;
	vec3_t d;

	for (i = hash_head[key]; i >= 0; i = hash_next[i])
	{
		VectorSubtract(gen_seeds[i].origin, p, d);
		if (d[2] > -48.0f && d[2] < 48.0f &&
		    d[0] * d[0] + d[1] * d[1] < SEED_SPACING * SEED_SPACING * 0.81f)
			return true;
	}
	return false;
}

/*
 * Can a player stand here? Trace a player box down; keep the floor point.
 * The box must not start in solid and the floor must be walkable (normal
 * steeper than 0.7 is a slide, same threshold pmove uses).
 */
static qboolean Seed_Ground(vec3_t candidate, vec3_t out)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, down;
	trace_t tr;
	static const float lifts[] = { 8, 24, 40, 56 };
	int L;

	/*
	 * Entities rest near the floor -- an item after droptofloor, a spawn point a
	 * mapper placed flush -- so a player box centred on their origin starts
	 * inside the ground and the trace reports startsolid. Lift the candidate
	 * until the box is free, then trace down to find where the feet go. Four
	 * lifts cover everything from flush-with-floor to sitting on a step.
	 */
	for (L = 0; L < 4; L++)
	{
		VectorCopy(candidate, start);
		start[2] += lifts[L];
		VectorCopy(start, down);
		down[2] -= 128.0f + lifts[L];

		tr = gi.trace(start, mins, maxs, down, NULL, MASK_PLAYERSOLID);
		if (!tr.startsolid && !tr.allsolid)
			break;
	}
	if (L == 4)
		return false;
	if (tr.fraction == 1.0f)
		return false;                       /* no floor within reach */
	if (tr.plane.normal[2] < 0.7f)
		return false;                       /* too steep to stand on */
	if (gi.pointcontents(tr.endpos) & MASK_WATER)
		return false;                       /* swim seeds come later */

	VectorCopy(tr.endpos, out);
	return true;
}

static void Seed_Add(vec3_t origin)
{
	int key;

	if (gen_num_seeds >= SEED_MAX)
		return;
	VectorCopy(origin, gen_seeds[gen_num_seeds].origin);
	gen_seeds[gen_num_seeds].area_hint = 0;
	gen_seeds[gen_num_seeds].flags = 0;

	key = Seed_HashKey(origin);
	hash_next[gen_num_seeds] = hash_head[key];
	hash_head[key] = gen_num_seeds;
	gen_num_seeds++;
}

/*
 * Germinate from every entity with an origin, then flood the lattice.
 * A simple work-queue breadth-first spread: for each seed, try the eight
 * lattice neighbours (and a step up, so stairs and ledges within step
 * height propagate); every candidate that can be stood on and is not
 * already covered becomes a new seed and a new frontier.
 */
static void Seed_Flood(void)
{
	int frontier = 0;
	static const float dirs[8][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
	};

	while (frontier < gen_num_seeds)
	{
		int i;

		for (i = 0; i < 8; i++)
		{
			vec3_t cand, ground;

			VectorCopy(gen_seeds[frontier].origin, cand);
			cand[0] += dirs[i][0] * SEED_SPACING;
			cand[1] += dirs[i][1] * SEED_SPACING;
			cand[2] += 40.0f;   /* reach over steps; trace-down finds the floor */

			if (Seed_Nearby(cand))
				continue;
			if (!Seed_Ground(cand, ground))
				continue;
			if (Seed_Nearby(ground))
				continue;
			Seed_Add(ground);
		}
		frontier++;
	}
}

static void Seed_Germinate(void)
{
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts; i++)
	{
		vec3_t ground;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		/* things players stand at: spawns, items, flags */
		if (strncmp(e->classname, "info_player", 11) != 0 &&
		    strncmp(e->classname, "item_", 5) != 0 &&
		    strncmp(e->classname, "weapon_", 7) != 0 &&
		    strncmp(e->classname, "ammo_", 5) != 0 &&
		    strncmp(e->classname, "info_flag", 9) != 0 &&
		    strncmp(e->classname, "info_position", 13) != 0)
			continue;

		if (Seed_Nearby(e->s.origin))
			continue;
		if (Seed_Ground(e->s.origin, ground))
			Seed_Add(ground);
	}
}

/*
 * Prove one candidate traversal with the oracle. The phantom stands on the
 * source seed, faces the target, and runs -- with a jump on the landing
 * step permitted when 'jump' is set. Success is standing within
 * ARRIVE_RADIUS of the target inside the time budget.
 *
 * Steering: re-aimed at the target every step from the phantom's live
 * position, exactly the information a real mover has. This proves the
 * link is traversable by a competent mover, not by a clairvoyant one.
 */
static qboolean Prove(int from, int to, qboolean jump,
                      short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	usercmd_t cmd;
	int elapsed;
	vec3_t want, d;
	float yaw;

	SG_OraclePlace(&ph, gen_seeds[from].origin);

	for (elapsed = 0; elapsed < TRY_LIMIT_MS; elapsed += STEP_MSEC)
	{
		VectorSubtract(gen_seeds[to].origin, ph.origin, want);
		d[0] = want[0]; d[1] = want[1]; d[2] = 0.0f;

		if (d[0] * d[0] + d[1] * d[1] < ARRIVE_RADIUS * ARRIVE_RADIUS &&
		    want[2] > -72.0f && want[2] < 72.0f && ph.groundentity)
		{
			float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
			                 ph.velocity[1] * ph.velocity[1]);
			*cost_ms = (short)elapsed;
			*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
			return true;
		}

		yaw = atan2f(want[1], want[0]);

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = STEP_MSEC;
		cmd.angles[YAW] = ANGLE2SHORT(yaw * 180.0f / M_PI);
		cmd.forwardmove = 400;
		/* jump from the ground when the traversal calls for it; tapped,
		 * not held -- PM_CheckJump refuses a held key */
		if (jump && ph.groundentity && (elapsed / STEP_MSEC) % 2 == 0)
			cmd.upmove = 400;

		SG_OracleRun(&ph, &cmd, 1);

		/* fell out of the world or into somewhere unrecoverable */
		if (ph.origin[2] < gen_seeds[from].origin[2] - 512.0f &&
		    ph.origin[2] < gen_seeds[to].origin[2] - 512.0f)
			return false;
	}
	return false;
}

static void Link_Add(int from, int to, rune_action_t act,
                     short cost_ms, byte exit_speed)
{
	rune_link_t *l;

	if (gen_num_links >= LINK_MAX)
		return;
	l = &gen_links[gen_num_links++];
	memset(l, 0, sizeof(*l));
	l->from = from;
	l->to = to;
	l->action = (byte)act;
	l->provenance = RL_PROVEN;
	l->cost_ms = cost_ms;
	l->exit_speed = exit_speed;
	l->heading_slack = 255;     /* run links: any approach heading works */
}

static void Prove_All(void)
{
	int i, j;

	for (i = 0; i < gen_num_seeds; i++)
	{
		for (j = 0; j < gen_num_seeds; j++)
		{
			vec3_t d;
			short cost;
			byte espeed;

			if (i == j)
				continue;
			VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, d);
			if (d[0] * d[0] + d[1] * d[1] > LINK_REACH * LINK_REACH)
				continue;
			if (d[2] > 128.0f || d[2] < -256.0f)
				continue;       /* beyond a jump up or a modest drop */

			if (Prove(i, j, false, &cost, &espeed))
				Link_Add(i, j, RL_RUN, cost, espeed);
			else if (Prove(i, j, true, &cost, &espeed))
				Link_Add(i, j, RL_JUMP, cost, espeed);
		}
		if ((i & 255) == 0)
			gi.dprintf("rune: proving %d/%d seeds, %d links\n",
			           i, gen_num_seeds, gen_num_links);
	}
}

/* ------------------------------------------------------------------- IO */

qboolean Rune_Generate(const char *mapname)
{
	rune_header_t hdr;
	char path[MAX_OSPATH];
	FILE *f;
	cvar_t *gamedir = gi.cvar("gamedir", "", 0);

	gen_seeds = gi.TagMalloc(sizeof(rune_seed_t) * SEED_MAX, TAG_GAME);
	gen_links = gi.TagMalloc(sizeof(rune_link_t) * LINK_MAX, TAG_GAME);
	gen_num_seeds = 0;
	gen_num_links = 0;
	memset(hash_head, 0xff, sizeof(hash_head));

	gi.dprintf("rune: germinating from entities...\n");
	Seed_Germinate();
	gi.dprintf("rune: %d germs; flooding...\n", gen_num_seeds);
	Seed_Flood();
	gi.dprintf("rune: %d seeds; proving links...\n", gen_num_seeds);
	Prove_All();
	gi.dprintf("rune: %d links proven\n", gen_num_links);

	Com_sprintf(path, sizeof(path), "%s/maps/%s.rune",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "wb");
	if (!f)
	{
		gi.dprintf("rune: cannot write %s\n", path);
		gi.TagFree(gen_seeds);
		gi.TagFree(gen_links);
		return false;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic = RUNE_MAGIC;
	hdr.version = RUNE_VERSION;
	hdr.num_seeds = gen_num_seeds;
	hdr.num_links = gen_num_links;
	strncpy(hdr.mapname, mapname, sizeof(hdr.mapname) - 1);

	fwrite(&hdr, sizeof(hdr), 1, f);
	fwrite(gen_seeds, sizeof(rune_seed_t), gen_num_seeds, f);
	fwrite(gen_links, sizeof(rune_link_t), gen_num_links, f);
	fclose(f);

	gi.dprintf("rune: wrote %s (%d seeds, %d links)\n",
	           path, gen_num_seeds, gen_num_links);

	gi.TagFree(gen_seeds);
	gi.TagFree(gen_links);
	return true;
}
