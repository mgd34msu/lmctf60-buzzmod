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
#define LINK_REACH		192.0f		/* run/jump pairs within this reach */
#define HOOK_REACH		448.0f		/* hook pairs may span further */
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
		    want[2] > -72.0f && want[2] < 72.0f &&
		    (ph.groundentity || ph.waterlevel >= 2))
		{
			float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
			                 ph.velocity[1] * ph.velocity[1]);
			*cost_ms = (short)elapsed;
			*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
			return true;
		}

		yaw = atan2f(want[1], want[0]);

		/*
		 * Steer like a mover, not a moth. Aiming dead at the target walks
		 * the phantom into doorframes and pillar corners, where it grinds
		 * until the budget dies -- and because the offending corner differs
		 * with direction, the same pair proves one way and fails the other.
		 * That asymmetry, 118 flat one-way cuts on lmctf03, is what severed
		 * the map. Feelers: take the openest heading nearest the target
		 * line, the same fan the live bot walks with.
		 */
		{
			static const float fan[5] = { 0, -35, 35, -75, 75 };
			vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
			float best_score = -1.0f, chosen = yaw;
			int k;

			for (k = 0; k < 5; k++)
			{
				vec3_t fdir, probe;
				trace_t ftr;
				float ty = yaw + fan[k] * (float)(M_PI / 180.0);
				float score;

				fdir[0] = cosf(ty); fdir[1] = sinf(ty); fdir[2] = 0;
				VectorMA(ph.origin, 80.0f, fdir, probe);
				probe[2] += 8.0f;
				ftr = gi.trace(ph.origin, mins, maxs, probe, NULL,
				               MASK_PLAYERSOLID);
				score = ftr.fraction - 0.06f * k;
				if (score > best_score)
				{
					best_score = score;
					chosen = ty;
				}
				if (k == 0 && ftr.fraction >= 1.0f)
					break;
			}
			yaw = chosen;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = STEP_MSEC;
		cmd.angles[YAW] = ANGLE2SHORT(yaw * 180.0f / M_PI);
		cmd.forwardmove = 400;
		/* jump from the ground when the traversal calls for it; tapped,
		 * not held -- PM_CheckJump refuses a held key */
		if (jump && ph.groundentity && (elapsed / STEP_MSEC) % 2 == 0)
			cmd.upmove = 400;
		/* submerged: swim toward the target height -- PM_WaterMove reads
		 * upmove directly, no jump semantics under water */
		if (ph.waterlevel >= 2)
			cmd.upmove = (want[2] > 24.0f) ? 300
			           : (want[2] < -24.0f ? -300 : 0);

		SG_OracleRun(&ph, &cmd, 1);

		/* fell out of the world or into somewhere unrecoverable */
		if (ph.origin[2] < gen_seeds[from].origin[2] - 900.0f &&
		    ph.origin[2] < gen_seeds[to].origin[2] - 900.0f)
			return false;
	}
	return false;
}

/*
 * Prove a hook traversal: the way LMCTF players climb and cross.
 *
 * Anchor: the ceiling or high wall above the target -- trace up from the
 * target seed, take the surface the trace strikes. The rope must have a
 * clear line from the source's eyes to that anchor (MASK_SHOT, the bolt's
 * own clipmask, p_weapon.c fire_hook). Then the pull is rolled exactly as
 * the game applies it: SG_OracleHookStep overwrites velocity per
 * p_weapon.c's ladder each step, SG_OracleRun integrates it, release when
 * the phantom is near the target or the rope is short, and the landing has
 * to arrive like any other link. Flight time is charged at the bolt's 800
 * (GRAPPLE_FIRE_HOOK_SPEED) on top of the pull.
 */
#define Q2_MASK_SHOT_GEN 0x6000003

static qboolean ProveHook(int from, int to, vec3_t anchor_out,
                          short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	usercmd_t cmd;
	vec3_t up, anchor, eye, d, want;
	trace_t tr;
	int elapsed;
	float rope, flight_ms;

	/* anchor: first surface straight above the target seed */
	VectorCopy(gen_seeds[to].origin, up);
	up[2] += 24.0f;
	VectorCopy(up, anchor);
	anchor[2] += 512.0f;
	tr = gi.trace(up, NULL, NULL, anchor, NULL, MASK_PLAYERSOLID);
	if (tr.fraction >= 1.0f || tr.startsolid)
		return false;
	VectorCopy(tr.endpos, anchor);
	anchor[2] -= 4.0f;                      /* just off the surface */

	/* rope line from the source's eyes, with the bolt's own mask */
	VectorCopy(gen_seeds[from].origin, eye);
	eye[2] += 22.0f;
	tr = gi.trace(eye, NULL, NULL, anchor, NULL, Q2_MASK_SHOT_GEN);
	VectorSubtract(anchor, eye, d);
	if (tr.fraction < 0.98f)
		return false;
	rope = VectorLength(d);
	if (rope < 150.0f)
		return false;                       /* p_weapon: short rope is a brake */
	flight_ms = rope / 800.0f * 1000.0f;

	/* roll the pull: alternate the game's velocity overwrite with pmove */
	SG_OraclePlace(&ph, gen_seeds[from].origin);
	for (elapsed = 0; elapsed < TRY_LIMIT_MS; elapsed += STEP_MSEC)
	{
		VectorSubtract(gen_seeds[to].origin, ph.origin, want);
		if (want[0] * want[0] + want[1] * want[1] <
		        ARRIVE_RADIUS * ARRIVE_RADIUS * 4.0f &&
		    want[2] > -96.0f && want[2] < 96.0f)
			break;                          /* close enough: release */

		VectorSubtract(anchor, ph.origin, d);
		if (VectorLength(d) < 130.0f)
			break;                          /* rope short: the brake band */

		SG_OracleHookStep(&ph, anchor);
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = STEP_MSEC;
		SG_OracleRun(&ph, &cmd, 1);
	}

	/* released: fall/settle up to a second, then the arrival test */
	{
		int settle;

		for (settle = 0; settle < 1000; settle += STEP_MSEC)
		{
			VectorSubtract(gen_seeds[to].origin, ph.origin, want);
			if (want[0] * want[0] + want[1] * want[1] <
			        ARRIVE_RADIUS * ARRIVE_RADIUS &&
			    want[2] > -72.0f && want[2] < 72.0f &&
			    (ph.groundentity || ph.waterlevel >= 2))
			{
				float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
				                 ph.velocity[1] * ph.velocity[1]);
				*cost_ms = (short)(flight_ms + elapsed + settle);
				*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
				VectorCopy(anchor, anchor_out);
				return true;
			}
			memset(&cmd, 0, sizeof(cmd));
			cmd.msec = STEP_MSEC;
			VectorSubtract(gen_seeds[to].origin, ph.origin, want);
			cmd.angles[YAW] = ANGLE2SHORT(atan2f(want[1], want[0]) * 180.0f / M_PI);
			cmd.forwardmove = 400;
			SG_OracleRun(&ph, &cmd, 1);
		}
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
			if (d[0] * d[0] + d[1] * d[1] > HOOK_REACH * HOOK_REACH)
				continue;
			/* beyond running reach only the hook applies */
			if (d[0] * d[0] + d[1] * d[1] > LINK_REACH * LINK_REACH &&
			    d[2] <= 128.0f && d[2] >= -256.0f)
			{
				vec3_t anchor;

				if (ProveHook(i, j, anchor, &cost, &espeed))
				{
					rune_link_t *l;

					Link_Add(i, j, RL_HOOK, cost, espeed);
					l = &gen_links[gen_num_links - 1];
					VectorCopy(anchor, l->anchor);
				}
				continue;
			}
			if (d[2] > 512.0f || d[2] < -512.0f)
				continue;

			/*
			 * Dropping off an edge is the most ordinary move in the game
			 * and was in no prover's domain: run and jump stopped at -256
			 * and hooks aim up. The balconies over both flag rooms -- 496
			 * units up, forty links each, no way down -- were the visible
			 * result. Pmove cannot feel fall damage, so a deep drop proves
			 * as traversable and carries the RL_DROP tag; what a drop
			 * COSTS in health is the surface's business, not the graph's.
			 */
			if (d[2] <= 128.0f && d[2] >= -600.0f &&
			    Prove(i, j, false, &cost, &espeed))
				Link_Add(i, j, (d[2] < -160.0f) ? RL_DROP : RL_RUN,
				         cost, espeed);
			else if (d[2] <= 128.0f && d[2] >= -600.0f &&
			         Prove(i, j, true, &cost, &espeed))
				Link_Add(i, j, (d[2] < -160.0f) ? RL_DROP : RL_JUMP,
				         cost, espeed);
			else
			{
				vec3_t anchor;

				if (ProveHook(i, j, anchor, &cost, &espeed))
				{
					rune_link_t *l;

					Link_Add(i, j, RL_HOOK, cost, espeed);
					l = &gen_links[gen_num_links - 1];
					VectorCopy(anchor, l->anchor);
				}
			}
		}
		if ((i & 255) == 0)
			gi.dprintf("rune: proving %d/%d seeds, %d links\n",
			           i, gen_num_seeds, gen_num_links);
	}
}

/* ------------------------------------------------------------------- IO */

/*
 * Doors are closed while the world idles, and a closed door is solid to the
 * phantom's traces -- which proved out as every room becoming an island: 90
 * of 1562 seeds could reach the red flag on lmctf03, the flag room and
 * nothing beyond it. For the duration of generation the doors are unsolid,
 * the same assumption bspc made, and every one is restored before the
 * command returns. Links that cross a door's volume are the runtime's
 * business to re-validate against the door's actual state; first the link
 * has to exist.
 */
typedef struct { edict_t *e; solid_t solid; } heldopen_t;

static int Doors_Open(heldopen_t *held, int max)
{
	edict_t *e;
	int i, n = 0;

	for (i = 0; i < globals.num_edicts && n < max; i++)
	{
		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (strncmp(e->classname, "func_door", 9) != 0)
			continue;
		held[n].e = e;
		held[n].solid = e->solid;
		e->solid = SOLID_NOT;
		gi.linkentity(e);
		n++;
	}
	return n;
}

static void Doors_Restore(heldopen_t *held, int n)
{
	int i;

	for (i = 0; i < n; i++)
	{
		held[i].e->solid = held[i].solid;
		gi.linkentity(held[i].e);
	}
}

qboolean Rune_Generate(const char *mapname)
{
	rune_header_t hdr;
	char path[MAX_OSPATH];
	FILE *f;
	heldopen_t held[128];
	int ndoors;
	cvar_t *gamedir = gi.cvar("gamedir", "", 0);

	gen_seeds = gi.TagMalloc(sizeof(rune_seed_t) * SEED_MAX, TAG_GAME);
	gen_links = gi.TagMalloc(sizeof(rune_link_t) * LINK_MAX, TAG_GAME);
	gen_num_seeds = 0;
	gen_num_links = 0;
	memset(hash_head, 0xff, sizeof(hash_head));

	ndoors = Doors_Open(held, 128);
	gi.dprintf("rune: %d doors held open for proving\n", ndoors);

	gi.dprintf("rune: germinating from entities...\n");
	Seed_Germinate();
	gi.dprintf("rune: %d germs; flooding...\n", gen_num_seeds);
	Seed_Flood();
	gi.dprintf("rune: %d seeds; proving links...\n", gen_num_seeds);
	Prove_All();
	gi.dprintf("rune: %d links proven\n", gen_num_links);
	Doors_Restore(held, ndoors);

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
