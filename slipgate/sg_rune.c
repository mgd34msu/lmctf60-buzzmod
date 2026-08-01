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

/* prover autopsy: where drop attempts actually die */
static int dg_pairs, dg_seek, dg_noedge, dg_fell, dg_arrived, dg_nocontact;

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
			/*
			 * The parent must be able to WALK here, not merely know a floor
			 * exists. The lattice hop with its lift can clear thin walls,
			 * and every standable pocket behind one became a seed -- a full
			 * row of them along lmctf03's southern boundary, which then
			 * collected one-way "arrivals" from phantoms pressed against
			 * the far side of the wall. A box trace at chest height from
			 * parent to candidate, clearing normal steps, keeps the seed
			 * set on the playable side of the world.
			 */
			{
				vec3_t pmins = { -16, -16, -24 }, pmaxs = { 16, 16, 32 };
				vec3_t from, to;
				trace_t wtr;

				VectorCopy(gen_seeds[frontier].origin, from);
				VectorCopy(ground, to);
				from[2] += 26.0f;
				to[2] += 26.0f;
				wtr = gi.trace(from, pmins, pmaxs, to, NULL, MASK_PLAYERSOLID);
				if (wtr.fraction < 0.9f)
					continue;
			}
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
		/* things players stand at: spawns, items, flags -- and teleporter
		 * pads and destinations, which SIT where the lattice never grows
		 * (a pit at z=-444, a perch at +276 on lmctf03): without a
		 * germinated seed there, the declared-link pass finds nothing
		 * within reach and the map's one-way shortcuts never enter the
		 * graph. The generator said so in every log; nobody listened. */
		if (strncmp(e->classname, "info_player", 11) != 0 &&
		    strncmp(e->classname, "item_", 5) != 0 &&
		    strncmp(e->classname, "weapon_", 7) != 0 &&
		    strncmp(e->classname, "ammo_", 5) != 0 &&
		    strncmp(e->classname, "info_flag", 9) != 0 &&
		    strncmp(e->classname, "info_position", 13) != 0 &&
		    strncmp(e->classname, "misc_teleporter", 15) != 0)
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
/*
 * Arrival means TOUCHING distance, not radio distance. Forty horizontal
 * units can span a wall, and did: phantoms pressed against one side of the
 * south boundary "arrived" at seeds behind it, writing links no mover can
 * follow. A clear line settles which side of the world the phantom is on.
 */
static qboolean Prove_Contact(vec3_t at, vec3_t target)
{
	vec3_t a2, t2;
	trace_t tr;

	VectorCopy(at, a2);
	VectorCopy(target, t2);
	a2[2] += 16.0f;
	t2[2] += 16.0f;
	tr = gi.trace(a2, NULL, NULL, t2, NULL, MASK_PLAYERSOLID);
	return tr.fraction >= 1.0f;
}

static int gen_momentum_links, gen_momentum_tries;

/*
 * Entry speed for the NEXT Prove roll, consumed by Prove at placement. Zero
 * means the from-rest proof every link has had since the first cut. Nonzero
 * is the momentum experiment: a gap too wide for the runway inside one
 * proof's approach can still be crossed by a body that ARRIVES at speed --
 * which is the case min_speed on the envelope was designed to record and
 * never had a writer for.
 */
static float gen_entry_speed = 0.0f;

static qboolean Prove(int from, int to, qboolean jump,
                      short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	usercmd_t cmd;
	int elapsed;
	vec3_t want, d;
	float yaw;
	float edge_yaw = 0.0f;
	int edge_hold_steps = 0;

	SG_OraclePlace(&ph, gen_seeds[from].origin);

	if (gen_entry_speed > 0.0f)
	{
		vec3_t ev;

		VectorSubtract(gen_seeds[to].origin, gen_seeds[from].origin, ev);
		ev[2] = 0.0f;
		VectorNormalize(ev);
		ph.velocity[0] = ev[0] * gen_entry_speed;
		ph.velocity[1] = ev[1] * gen_entry_speed;
		ph.pms.velocity[0] = (short)(ph.velocity[0] * 8.0f);
		ph.pms.velocity[1] = (short)(ph.velocity[1] * 8.0f);
	}

	for (elapsed = 0; elapsed < TRY_LIMIT_MS; elapsed += STEP_MSEC)
	{
		VectorSubtract(gen_seeds[to].origin, ph.origin, want);
		d[0] = want[0]; d[1] = want[1]; d[2] = 0.0f;

		if (d[0] * d[0] + d[1] * d[1] < ARRIVE_RADIUS * ARRIVE_RADIUS &&
		    want[2] > -72.0f && want[2] < 72.0f &&
		    (ph.groundentity || ph.waterlevel >= 2) &&
		    (Prove_Contact(ph.origin, gen_seeds[to].origin)
		         ? (dg_arrived++, true) : (dg_nocontact++, false)))
		{
			float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
			                 ph.velocity[1] * ph.velocity[1]);
			*cost_ms = (short)elapsed;
			*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
			return true;
		}

		yaw = atan2f(want[1], want[0]);

		/*
		 * Above the target with nowhere to fall: the floor underfoot
		 * extends past us, and walking "toward" a target that is straight
		 * below jitters in place until the budget dies -- which is why
		 * eight drop links existed on a map full of balconies. Seek the
		 * edge: probe the compass for the nearest place the floor stops,
		 * and walk there; gravity does the rest, and the arrival test
		 * still judges the landing.
		 */
		if (want[2] < -100.0f &&
		    d[0] * d[0] + d[1] * d[1] < 160.0f * 160.0f && ph.groundentity)
		{
			/*
			 * The 48-unit probe found an edge from almost nowhere: lattice
			 * seeds sit 64 or more from any lip, so eight drop links existed
			 * on a map of balconies and 53 frontier plateaus stayed cut off
			 * for want of one run-off each. Probe out to three ranges, and
			 * once an edge heading is chosen HOLD it between steps -- 
			 * re-deciding every step turned the walk to the lip into a
			 * dither at the lip.
			 */
			if (edge_hold_steps == 0) dg_seek++;
			if (edge_hold_steps > 0)
			{
				yaw = edge_yaw;
				edge_hold_steps--;
				if (!ph.groundentity) dg_fell++;
			}
			else
			{
				int e8, rr;
				float bestd8 = 1e30f;
				qboolean found_edge = false;
				static const float ranges[3] = { 48.0f, 96.0f, 144.0f };

				for (rr = 0; rr < 3 && !found_edge; rr++)
				for (e8 = 0; e8 < 8; e8++)
				{
					float ey = e8 * (float)(M_PI / 4.0);
					vec3_t ep, edown;
					trace_t etr;

					ep[0] = ph.origin[0] + cosf(ey) * ranges[rr];
					ep[1] = ph.origin[1] + sinf(ey) * ranges[rr];
					ep[2] = ph.origin[2] + 8.0f;
					VectorCopy(ep, edown);
					edown[2] -= 80.0f;
					etr = gi.trace(ep, NULL, NULL, edown, NULL, MASK_PLAYERSOLID);
					if (etr.fraction >= 1.0f)
					{
						vec3_t dd2;

						dd2[0] = ep[0] - gen_seeds[to].origin[0];
						dd2[1] = ep[1] - gen_seeds[to].origin[1];
						if (dd2[0] * dd2[0] + dd2[1] * dd2[1] < bestd8)
						{
							bestd8 = dd2[0] * dd2[0] + dd2[1] * dd2[1];
							edge_yaw = ey;
							found_edge = true;
						}
					}
				}
				if (!found_edge)
				{
					dg_noedge++;
					return false;
				}
				yaw = edge_yaw;
				edge_hold_steps = 8;    /* 200ms of committed walking */
			}
		}

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

	/*
	 * Anchor candidates. Straight above the target fails the commonest
	 * climb in the game: from below a plateau, the rope line to a point
	 * over the plateau clips the plateau's own lip. A player hooks the
	 * ceiling over the APPROACH -- above the lip, above the gap between
	 * here and there -- so candidates walk back from the target toward
	 * the source. First one with both a surface overhead and a clear
	 * rope line from the source's eyes wins.
	 */
	VectorCopy(gen_seeds[from].origin, eye);
	eye[2] += 22.0f;
	{
		static const float backs[4] = { 0.0f, 0.35f, 0.6f, 0.85f };
		int bi;
		qboolean got = false;

		for (bi = 0; bi < 4 && !got; bi++)
		{
			VectorCopy(gen_seeds[to].origin, up);
			up[0] += (gen_seeds[from].origin[0] - up[0]) * backs[bi];
			up[1] += (gen_seeds[from].origin[1] - up[1]) * backs[bi];
			up[2] = (backs[bi] > 0.0f &&
			         gen_seeds[from].origin[2] < gen_seeds[to].origin[2])
			            ? gen_seeds[to].origin[2] : up[2];
			up[2] += 24.0f;
			VectorCopy(up, anchor);
			anchor[2] += 512.0f;
			tr = gi.trace(up, NULL, NULL, anchor, NULL, MASK_PLAYERSOLID);
			if (tr.fraction >= 1.0f || tr.startsolid)
				continue;
			VectorCopy(tr.endpos, anchor);
			anchor[2] -= 4.0f;

			/*
			 * The rope line is proved from the MUZZLE, not the eye: the
			 * real bolt spawns at P_ProjectSource's {8, 8, viewheight-8}
			 * offset (p_weapon.c:2021-2023), eight units below and beside
			 * the eye line. An eye-proved line that cleared a lip by five
			 * units put 1618 real ropes into worldspawn short of their
			 * anchors (iteration 21's bite census). Fire direction first,
			 * then the offset along it, then the trace the bolt will fly.
			 */
			{
				vec3_t md, mfwd, mright, muzzle;
				float mlen;

				VectorSubtract(anchor, eye, md);
				mlen = VectorLength(md);
				if (mlen < 1.0f)
					continue;
				VectorScale(md, 1.0f / mlen, mfwd);
				/* horizontal right of the fire direction, roll zero */
				mright[0] = mfwd[1]; mright[1] = -mfwd[0]; mright[2] = 0.0f;
				VectorNormalize(mright);
				VectorCopy(gen_seeds[from].origin, muzzle);
				muzzle[2] += 22.0f - 8.0f;      /* viewheight - 8 */
				VectorMA(muzzle, 8.0f, mfwd, muzzle);
				VectorMA(muzzle, 8.0f, mright, muzzle);

				tr = gi.trace(muzzle, NULL, NULL, anchor, NULL,
				              Q2_MASK_SHOT_GEN);
				if (tr.fraction >= 0.98f)
					got = true;
			}
		}
		if (!got)
			return false;
	}
	VectorSubtract(anchor, eye, d);
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

/*
 * ENTRY ENVELOPE HELPERS (additive).
 *
 * A link's heading field is the compass direction the traversal was entered
 * on, one byte wrapping the circle: 256 steps of 1.40625 degrees. atan2f
 * returns -pi..pi, so the angle is folded into 0..2pi before it is scaled --
 * casting a negative float to byte is not a wrap, it is undefined, and the
 * fold costs nothing.
 *
 * Only a prover that actually knows the entry direction fills these in. Run
 * and jump links keep the slack of 255 Link_Add gives them, which says
 * plainly "this was entered from wherever the phantom happened to be facing
 * and the record cannot claim better". Recording a made-up cone would be
 * worse than recording none.
 */
static byte Heading_Quantize(float dx, float dy)
{
	float a = atan2f(dy, dx);

	if (a < 0.0f)
		a += (float)(2.0 * M_PI);
	return (byte)(((int)(a / (float)(2.0 * M_PI) * 256.0f)) & 255);
}

/*
 * The drop prover's entry state, published for the call site that writes the
 * link. ProveDrop walks off a lip in a definite direction at a definite
 * speed; those two numbers are the honest envelope for the link it proves,
 * and they are known here and nowhere else.
 */
#define SG_DROP_WALKOFF_SPEED	180.0f	/* conservative floor: the roll used 220 */
#define SG_DROP_SLACK			32		/* +/- 45 degrees */
#define SG_HOOK_SLACK			24		/* +/- ~34 degrees toward the anchor */

static byte dd_last_heading;            /* lip direction of the last proven drop */

/*
 * Prove a drop without asking a phantom to find the edge on foot -- four
 * designs of edge-walking died between the seek timer and the budget. The
 * lip is geometry: sample down-probes along the source-to-target line until
 * the floor ends. Stand the phantom a step past the lip at a conservative
 * walk-off speed, and let the real pmove integrate the fall; the landing
 * is judged by the same arrival-and-contact test as every other link. The
 * lip is stored in the link's anchor so the body knows exactly where to
 * step off.
 */
static int dd_nolip, dd_fenced, dd_flew, dd_landed, dd_won;

static qboolean ProveDrop(int from, int to, vec3_t lip_out,
                          short *cost_ms, byte *exit_speed)
{
	vec3_t src, dst, dir, lip, probe, down;
	trace_t tr;
	sg_phantom_t ph;
	usercmd_t cmd;
	float horiz, walked;
	int elapsed;
	qboolean found_lip = false;

	VectorCopy(gen_seeds[from].origin, src);
	VectorCopy(gen_seeds[to].origin, dst);
	dir[0] = dst[0] - src[0];
	dir[1] = dst[1] - src[1];
	dir[2] = 0.0f;
	horiz = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
	if (horiz < 1.0f)
	{
		/* directly below: any compass direction can hold the lip; walk
		 * the eight and take the first that ends */
		int e8;

		for (e8 = 0; e8 < 8 && !found_lip; e8++)
		{
			dir[0] = cosf(e8 * (float)(M_PI / 4.0));
			dir[1] = sinf(e8 * (float)(M_PI / 4.0));
			for (walked = 16.0f; walked <= 192.0f; walked += 16.0f)
			{
				probe[0] = src[0] + dir[0] * walked;
				probe[1] = src[1] + dir[1] * walked;
				probe[2] = src[2] + 8.0f;
				VectorCopy(probe, down);
				down[2] -= 80.0f;
				tr = gi.trace(probe, NULL, NULL, down, NULL, MASK_PLAYERSOLID);
				if (tr.fraction >= 1.0f)
				{
					VectorCopy(probe, lip);
					found_lip = true;
					break;
				}
			}
		}
	}
	else
	{
		dir[0] /= horiz; dir[1] /= horiz;
		for (walked = 16.0f; walked <= horiz + 64.0f && walked <= 256.0f;
		     walked += 16.0f)
		{
			probe[0] = src[0] + dir[0] * walked;
			probe[1] = src[1] + dir[1] * walked;
			probe[2] = src[2] + 8.0f;
			VectorCopy(probe, down);
			down[2] -= 80.0f;
			tr = gi.trace(probe, NULL, NULL, down, NULL, MASK_PLAYERSOLID);
			if (tr.fraction >= 1.0f)
			{
				VectorCopy(probe, lip);
				found_lip = true;
				break;
			}
		}
	}
	if (!found_lip)
	{
		dd_nolip++;
		return false;
	}

	/*
	 * The lip was found by geometry; the APPROACH must be walked. The
	 * down-probe is a POINT trace -- a point slides past a railing that a
	 * player box cannot (lmctf03 link 11580: lip behind a rail; the body
	 * orbited it for a full match, cited in PLAN.md). Roll the phantom
	 * from the seed straight at the lip with the real physics: if it
	 * cannot get within 24 horizontal units, the lip is scenery seen
	 * through a fence, not a walk-off, and recording it would lie about
	 * what a player can do here.
	 */
	{
		float d2 = 1e30f;
		int step;

		SG_OraclePlace(&ph, src);
		for (step = 0; step < 2500; step += STEP_MSEC)
		{
			vec3_t w;

			w[0] = lip[0] - ph.origin[0];
			w[1] = lip[1] - ph.origin[1];
			d2 = w[0] * w[0] + w[1] * w[1];
			if (d2 < 24.0f * 24.0f)
				break;
			memset(&cmd, 0, sizeof(cmd));
			cmd.msec = STEP_MSEC;
			cmd.angles[YAW] = ANGLE2SHORT(atan2f(w[1], w[0]) * 180.0f / M_PI);
			cmd.forwardmove = 400;
			SG_OracleRun(&ph, &cmd, 1);
		}
		if (d2 >= 24.0f * 24.0f)
		{
			dd_fenced++;
			return false;
		}
	}
	dd_flew++;

	/*
	 * The lip is where a POINT stops finding floor -- but the player box is
	 * 32 wide, and placed at that point its near half still catches the rim:
	 * pmove grounds it on the edge and it hovers over the target forever
	 * (sampled: every failure ended grounded at source height, want z -300,
	 * horizontal want near zero). Place the box fully past the rim, moving.
	 */
	{
		vec3_t start;

		VectorCopy(lip, start);
		start[0] += dir[0] * 24.0f;
		start[1] += dir[1] * 24.0f;
		start[2] += 4.0f;
		SG_OraclePlace(&ph, start);
	}
	ph.pms.velocity[0] = (short)(dir[0] * 220.0f * 8.0f);
	ph.pms.velocity[1] = (short)(dir[1] * 220.0f * 8.0f);

	for (elapsed = 0; elapsed < 2000; elapsed += STEP_MSEC)
	{
		vec3_t want;

		VectorSubtract(dst, ph.origin, want);
		if (want[0] * want[0] + want[1] * want[1] <
		        ARRIVE_RADIUS * ARRIVE_RADIUS &&
		    want[2] > -72.0f && want[2] < 72.0f &&
		    (ph.groundentity || ph.waterlevel >= 2) &&
		    Prove_Contact(ph.origin, dst))
		{
			float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
			                 ph.velocity[1] * ph.velocity[1]);
			dd_won++;
			*cost_ms = (short)(elapsed + (int)(walked / 200.0f * 1000.0f));
			*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
			VectorCopy(lip, lip_out);
			/* the direction the phantom walked off the lip: this proof holds
			 * for a body arriving on that heading, not for any heading */
			dd_last_heading = Heading_Quantize(dir[0], dir[1]);
			return true;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = STEP_MSEC;
		/* converged above the target but still high: aiming at a point
		 * under your feet is a jitter, not a walk -- keep going the lip
		 * direction until gravity has actually taken us down */
		if (want[2] < -100.0f &&
		    want[0] * want[0] + want[1] * want[1] < 64.0f * 64.0f &&
		    ph.groundentity)
			cmd.angles[YAW] = ANGLE2SHORT(atan2f(dir[1], dir[0]) * 180.0f / M_PI);
		else
			cmd.angles[YAW] = ANGLE2SHORT(atan2f(want[1], want[0]) * 180.0f / M_PI);
		cmd.forwardmove = 400;
		SG_OracleRun(&ph, &cmd, 1);

		if (ph.origin[2] < dst[2] - 512.0f)
			return false;
		if (ph.groundentity)
			dd_landed++;
	}
	/* sample the corpse: where did the first few timeouts actually end? */
	if (dd_nolip + dd_flew < 40)
	{
		vec3_t fin;

		VectorSubtract(dst, ph.origin, fin);
		gi.dprintf("geodrop FAIL %d->%d end=(%.0f %.0f %.0f) want=(%.0f %.0f %.0f) grounded=%d\n",
		           from, to, ph.origin[0], ph.origin[1], ph.origin[2],
		           fin[0], fin[1], fin[2], (int)ph.groundentity);
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

/* ===================================================================
 * ADDITIVE BLOCK: water volumes, lifts, teleporters.
 *
 * Everything between this banner and the one that closes it is new. It
 * adds functions and reads the state the existing generator built; it
 * changes nothing above. Two call sites are appended elsewhere, each
 * marked with the same banner word: one line in Rune_Generate (the water
 * seeding pass, after Seed_Flood) and three lines at the very end of
 * Prove_All (the three link passes).
 *
 * Why these three cannot come out of the pair loop above:
 *
 *   water   Seed_Ground only ever returns FLOOR points, and only floors
 *           within its down-trace. A pool deeper than that seeds nothing,
 *           and the volume between surface and floor -- where a swimmer
 *           actually is -- was never a candidate at all. lmctf03's pools
 *           are therefore holes that cut the graph in two.
 *   lift    a func_plat does not move during generation (no think runs),
 *           so no phantom can ride one. The traversal is real but
 *           unprovable by simulation; it is recorded from the plat's own
 *           spawn data instead, with the cost read out of g_func.c.
 *   teleport the transport is not movement at all -- teleporter_touch
 *           assigns the destination origin. There is nothing to prove
 *           beyond both ends existing.
 * =================================================================== */

#define SG_WATER_SPACING	64.0f		/* the water lattice, 3D */
#define SG_WATER_MAX		8192		/* cap: a big ocean must not eat SEED_MAX */
#define SG_SWIM_REACH		192.0f		/* swim pairs proven within this, 3D */
#define SG_PAD_REACH		128.0f		/* teleporter pad/dest to seed */

static int gen_first_water = -1;        /* index of the first water seed, -1 none */
static int gen_num_water;
static int gen_lift_links, gen_tele_links, gen_swim_links, gen_swim_retag;
static int gen_env_drop, gen_env_hook, gen_declared_links;
static int gen_lift_down_drop, gen_lift_down_none;

/*
 * ENTRY ENVELOPES, written where the prover knows them (PLAN item 8).
 *
 * Each of these is called immediately after the Link_Add that created the
 * link, on that link, and records what the proof it just came from actually
 * used. Nothing is inferred: a drop link's cone is the direction the phantom
 * walked off the lip, a hook link's is the direction the rope was fired.
 */
static void Link_Env_Drop(rune_link_t *l, byte heading)
{
	l->heading = heading;
	l->heading_slack = SG_DROP_SLACK;
	/*
	 * The walk-off was rolled at 220 units/second (ProveDrop seeds the
	 * phantom's velocity there). 180 is recorded instead -- a floor the proof
	 * comfortably covers rather than the exact number it happened to use, so
	 * a body arriving a little slower is not turned away from a link it can
	 * make. Stored as speed/4, the field's unit.
	 */
	l->min_speed = (byte)(SG_DROP_WALKOFF_SPEED / 4.0f);
	gen_env_drop++;
}

static void Link_Env_Hook(rune_link_t *l, vec3_t from_origin, vec3_t anchor)
{
	l->heading = Heading_Quantize(anchor[0] - from_origin[0],
	                              anchor[1] - from_origin[1]);
	l->heading_slack = SG_HOOK_SLACK;
	/*
	 * ProveHook stands the phantom still and fires; the rope SETS velocity
	 * (p_weapon.c), it does not add to it, so no entry speed was required and
	 * none is claimed.
	 */
	l->min_speed = 0;
	gen_env_hook++;
}

/*
 * PROVENANCE for the links that were never simulated. Link_Add stamps every
 * link RL_PROVEN because every link it was written for had been rolled by the
 * oracle. The lift and teleport passes are the exception -- they read the
 * map's spawn data -- so the tail of the link array those passes appended is
 * walked backwards afterwards and re-stamped. Backwards from the end, down to
 * the mark taken before the passes ran: nothing earlier is touched, and
 * Link_Add itself is left exactly as it was.
 */
static void Link_Declare_Tail(int mark)
{
	int i;

	for (i = gen_num_links - 1; i >= mark; i--)
	{
		if (gen_links[i].action != RL_LIFT &&
		    gen_links[i].action != RL_TELEPORT)
			continue;               /* a drop proven by the lift pass stays PROVEN */
		gen_links[i].provenance = RL_DECLARED;
		gen_declared_links++;
	}
}

/*
 * Add a seed that lives inside water. Seed_Add is left exactly as it was;
 * the flag is set on the seed it just appended, and the "did it append"
 * test covers Seed_Add's silent refusal at SEED_MAX.
 */
static int Seed_AddWater(vec3_t origin)
{
	int before = gen_num_seeds;

	Seed_Add(origin);
	if (gen_num_seeds == before)
		return -1;
	gen_seeds[gen_num_seeds - 1].flags |= RSF_WATER;
	return gen_num_seeds - 1;
}

/*
 * Can a swimmer be at this point? Two questions, both answered by the
 * engine: is the point in a water volume (pointcontents against MASK_WATER,
 * q_shared.h:358 -- water, slime and lava all move a body the same way), and
 * does a player-sized box placed there start inside solid. A one-unit trace
 * is enough: startsolid is decided at the start point.
 */
static qboolean Seed_WaterFree(vec3_t p)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, end;
	trace_t tr;

	if (!(gi.pointcontents(p) & MASK_WATER))
		return false;

	VectorCopy(p, start);
	VectorCopy(p, end);
	end[2] -= 1.0f;
	tr = gi.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
	if (tr.startsolid || tr.allsolid)
		return false;
	return true;
}

/*
 * The six lattice neighbours of a point, in three dimensions -- a water
 * volume has an inside, so up and down are directions like any other.
 * Returns how many became seeds.
 */
static int Seed_WaterNeighbours(vec3_t from)
{
	static const float dirs6[6][3] = {
		{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
		{ 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
	};
	int k, added = 0;

	for (k = 0; k < 6; k++)
	{
		vec3_t cand;

		cand[0] = from[0] + dirs6[k][0] * SG_WATER_SPACING;
		cand[1] = from[1] + dirs6[k][1] * SG_WATER_SPACING;
		cand[2] = from[2] + dirs6[k][2] * SG_WATER_SPACING;

		if (Seed_Nearby(cand))
			continue;
		if (!Seed_WaterFree(cand))
			continue;
		if (Seed_AddWater(cand) < 0)
			break;              /* seed table full */
		added++;
	}
	return added;
}

/*
 * Seed the water. Two stages, the same shape as Seed_Germinate + Seed_Flood:
 *
 *   entry  every seed the dry passes produced looks one lattice step around
 *          its own body height for water. A shore seed beside a pool finds
 *          it horizontally; a seed standing in the shallows finds it below.
 *   flood  from every water seed already found, breadth-first through the
 *          volume in 3D until the water runs out.
 *
 * There is deliberately no walk-path check here -- the box trace Seed_Flood
 * runs between parent and child asks "could a walker get there", which is
 * the wrong question about a body that is swimming. The water flood is its
 * own pass precisely so that test does not apply to it; whether the two
 * ends are actually connected is settled later, by Prove(), which rolls the
 * real physics through the water like any other link.
 */
static void Seed_Water(void)
{
	static const float around[4][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
	};
	/*
	 * A pool's surface is as often below the floor you are standing on as
	 * level with it -- a sunken pit, a channel, lmctf03's lower water. One
	 * probe at body height would miss every one of those, so each direction
	 * is probed at body height and two lattice steps down.
	 */
	static const float drops[3] = { 0.0f, -64.0f, -128.0f };
	int dry = gen_num_seeds;
	int i, k, z, entries = 0;

	for (i = 0; i < dry; i++)
	{
		if (gen_num_seeds - dry >= SG_WATER_MAX)
			break;
		for (k = 0; k < 4; k++)
		{
			for (z = 0; z < 3; z++)
			{
				vec3_t cand;

				cand[0] = gen_seeds[i].origin[0] + around[k][0] * SG_WATER_SPACING;
				cand[1] = gen_seeds[i].origin[1] + around[k][1] * SG_WATER_SPACING;
				cand[2] = gen_seeds[i].origin[2] + 24.0f + drops[z];

				if (Seed_Nearby(cand))
					continue;
				if (!Seed_WaterFree(cand))
					continue;
				if (Seed_AddWater(cand) < 0)
					break;
				entries++;
				break;          /* one entry per direction is enough */
			}
		}
	}

	if (gen_num_seeds == dry)
	{
		gi.dprintf("rune: no water adjacent to any seed\n");
		return;
	}

	gen_first_water = dry;
	for (i = dry; i < gen_num_seeds; i++)
	{
		vec3_t here;

		if (gen_num_seeds - dry >= SG_WATER_MAX)
			break;
		VectorCopy(gen_seeds[i].origin, here);
		Seed_WaterNeighbours(here);
	}

	gen_num_water = gen_num_seeds - dry;
	gi.dprintf("rune: %d water seeds (%d entered from dry land)\n",
	           gen_num_water, entries);
}

/*
 * A from-seed index over the links the pair loop already wrote, so the swim
 * pass can ask "is this pair linked already" without walking every link. It
 * is a snapshot: links the swim pass itself adds are not in it, which is
 * safe because that pass visits each ordered pair exactly once.
 */
static int *sw_first, *sw_next;

static void Link_Index_Build(void)
{
	int i;

	sw_first = gi.TagMalloc(sizeof(int) * (gen_num_seeds > 0 ? gen_num_seeds : 1),
	                        TAG_GAME);
	sw_next = gi.TagMalloc(sizeof(int) * (gen_num_links > 0 ? gen_num_links : 1),
	                       TAG_GAME);
	for (i = 0; i < gen_num_seeds; i++)
		sw_first[i] = -1;
	for (i = 0; i < gen_num_links; i++)
	{
		sw_next[i] = sw_first[gen_links[i].from];
		sw_first[gen_links[i].from] = i;
	}
}

static int Link_Index_Find(int from, int to)
{
	int i;

	if (!sw_first || from < 0 || from >= gen_num_seeds)
		return -1;
	for (i = sw_first[from]; i >= 0; i = sw_next[i])
		if (gen_links[i].to == to)
			return i;
	return -1;
}

/*
 * One direction of one water pair. Either the pair loop already linked it --
 * in which case the only thing wrong is the label, because a body with water
 * over its head does not run, jump or fall between two points inside that
 * water, it swims -- or it was never linked, and the oracle gets one attempt
 * at it now. Only a link with BOTH ends submerged is relabelled: wading in
 * from the shore really is a run, and jumping off a ledge into a pool really
 * is a drop.
 *
 * Prove() needs no change to do this: it already reads ph.waterlevel and
 * drives upmove directly when the phantom is submerged (no jump semantics
 * under water), and its arrival test already accepts a swimming arrival
 * (ph.groundentity || ph.waterlevel >= 2). What it did not have was
 * anywhere in the water to arrive AT.
 */
static void Prove_Swim_Pair(int from, int to)
{
	short cost;
	byte espeed;
	int have = Link_Index_Find(from, to);

	if (have >= 0)
	{
		if ((gen_seeds[from].flags & RSF_WATER) &&
		    (gen_seeds[to].flags & RSF_WATER) &&
		    (gen_links[have].action == RL_RUN ||
		     gen_links[have].action == RL_JUMP ||
		     gen_links[have].action == RL_DROP))
		{
			gen_links[have].action = RL_SWIM;
			gen_swim_retag++;
		}
		return;
	}
	if (Prove(from, to, false, &cost, &espeed))
	{
		Link_Add(from, to, RL_SWIM, cost, espeed);
		gen_swim_links++;
	}
}

/*
 * Swim links: water to water, and water to and from the shore. Only water
 * seeds drive the outer loop, so this costs time proportional to the water
 * in the map, not to the map. Every ORDERED pair is visited exactly once:
 * two water seeds each take their own turn as i and each does its own
 * outgoing direction, while a dry j never takes a turn at all, so its
 * direction back into the water is done here.
 */
static void Prove_Swims(void)
{
	int i, j;

	if (gen_first_water < 0)
		return;

	Link_Index_Build();

	for (i = gen_first_water; i < gen_num_seeds; i++)
	{
		if (!(gen_seeds[i].flags & RSF_WATER))
			continue;
		for (j = 0; j < gen_num_seeds; j++)
		{
			vec3_t d;

			if (i == j)
				continue;
			VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, d);
			if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >
			        SG_SWIM_REACH * SG_SWIM_REACH)
				continue;

			Prove_Swim_Pair(i, j);
			/* the way back, unless j is water and will come round as i */
			if (!(gen_seeds[j].flags & RSF_WATER))
				Prove_Swim_Pair(j, i);
		}
	}

	gi.TagFree(sw_first);
	gi.TagFree(sw_next);
	sw_first = NULL;
	sw_next = NULL;

	gi.dprintf("rune: %d swim links proven, %d submerged links relabelled swim\n",
	           gen_swim_links, gen_swim_retag);
}

/*
 * Is this ordered pair already linked? A plain scan: the swim pass builds a
 * from-index but frees it again, and the callers here run a handful of times
 * (once per plat), not once per pair, so the scan is bought and paid for by
 * not writing a duplicate link over one the pair loop already proved.
 */
static qboolean Link_Exists(int from, int to)
{
	int i;

	for (i = 0; i < gen_num_links; i++)
		if (gen_links[i].from == from && gen_links[i].to == to)
			return true;
	return false;
}

/* nearest seed to a point, inside a horizontal and a vertical tolerance */
static int Gen_SeedNear(vec3_t p, float horiz, float vert)
{
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < gen_num_seeds; i++)
	{
		vec3_t d;
		float h2, d3;

		VectorSubtract(gen_seeds[i].origin, p, d);
		if (d[2] > vert || d[2] < -vert)
			continue;
		h2 = d[0] * d[0] + d[1] * d[1];
		if (h2 > horiz * horiz)
			continue;
		d3 = h2 + d[2] * d[2];
		if (d3 < bestd)
		{
			bestd = d3;
			best = i;
		}
	}
	return best;
}

/*
 * What a plat costs to ride, read out of g_func.c rather than guessed.
 *
 * SP_func_plat (g_func.c:494) defaults speed to 20 and otherwise scales the
 * mapper's value by 0.1 (g_func.c:504-507); accel and decel default to 5 the
 * same way (g_func.c:509-517), and all three are copied into moveinfo
 * (g_func.c:548-550). Move_Calc then picks one of two integrators
 * (g_func.c:101):
 *
 *   speed == accel == decel -- Move_Begin sets velocity = dir * speed and
 *       waits remaining_distance / speed SECONDS (g_func.c:86-89). speed is
 *       units per second in this branch.
 *
 *   otherwise -- the accelerative branch, Think_AccelMove, which subtracts
 *       current_speed from the remaining distance once per 0.1s frame and
 *       pushes at current_speed * 10 (g_func.c:317, 328). speed is units per
 *       FRAME here, so the default plat really travels 200 units a second,
 *       which is why SP_func_plat divides the mapper's number by ten. This
 *       is the branch a default plat takes: 20 != 5.
 *
 * The ramp comes from plat_CalcAcceleratedMove: AccelerationDistance is
 * target * ((target / rate) + 1) / 2 (g_func.c:212), and when the move is
 * too short to reach full speed the peak comes out of its own quadratic
 * (g_func.c:230-237). Frames become seconds at FRAMETIME.
 *
 * Only the travel is charged. plat_hit_top parks the plat for three seconds
 * before it returns (g_func.c:344) and a rider may have to wait for it to
 * arrive; that is a queueing cost the runtime can see and the graph cannot.
 */
static short Plat_TravelMs(edict_t *e)
{
	float speed = e->moveinfo.speed;
	float accel = e->moveinfo.accel;
	float decel = e->moveinfo.decel;
	float dist = e->pos1[2] - e->pos2[2];
	float secs;

	if (dist < 1.0f)
		return 0;
	if (speed <= 0.0f)
		speed = 20.0f;                      /* g_func.c:505 */

	if (speed == accel && speed == decel)
	{
		secs = dist / speed;                /* g_func.c:86-89, units/second */
	}
	else
	{
		float accel_dist, decel_dist, frames;

		if (accel <= 0.0f)
			accel = 5.0f;                   /* g_func.c:510 */
		if (decel <= 0.0f)
			decel = 5.0f;                   /* g_func.c:513 */

		accel_dist = speed * ((speed / accel) + 1.0f) * 0.5f;
		decel_dist = speed * ((speed / decel) + 1.0f) * 0.5f;

		if (dist - accel_dist - decel_dist >= 0.0f)
			frames = (speed / accel) + (speed / decel) +
			         (dist - accel_dist - decel_dist) / speed;
		else
		{
			float f = (accel + decel) / (accel * decel);
			float peak = (-2.0f + sqrtf(4.0f + 8.0f * f * dist)) / (2.0f * f);

			if (peak < 1.0f)
				peak = 1.0f;
			frames = (peak / accel) + (peak / decel);
		}
		secs = frames * FRAMETIME;          /* units per frame, g_func.c:317 */
	}

	if (secs < 0.05f)
		secs = 0.05f;
	if (secs > 30.0f)
		secs = 30.0f;
	return (short)(secs * 1000.0f);
}

/*
 * Lift links. A plat's two positions are fixed at spawn: pos1 is the top and
 * is the entity's spawn origin, pos2 is the top minus the height (st.height,
 * or the model's own thickness less st.lip) -- SP_func_plat, g_func.c:525-531,
 * comment included. A plat with no targetname is then moved to pos2 and
 * starts at the bottom (g_func.c:541-546), which is where it stands while the
 * rune is generated: the seeds on top of it are at its bottom height.
 *
 * A brush model's mins/maxs come from gi.setmodel and are relative to the
 * entity origin, so the standable face at either position is
 * (position + maxs[2]) with the model's own centre in x and y.
 *
 * The two ends are the nearest seed to the bottom face (the seed sitting on
 * the plat itself) and the nearest seed to the top face (the ledge the plat
 * delivers to -- there is no seed hanging in the air where the plat is not).
 * The search reaches the plat's own half-diagonal plus a lattice step out.
 * The ride point is stored in the link's anchor, the way drop and hook links
 * store theirs: it is where the body has to stand for the plat's trigger.
 */
static void Link_Plats(void)
{
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts; i++)
	{
		vec3_t bottom, top;
		float halfx, halfy, horiz;
		int sb, st_top, before;
		short cost;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (strcmp(e->classname, "func_plat") != 0)
			continue;
		if (e->pos1[2] - e->pos2[2] < 8.0f)
			continue;                       /* travels nowhere worth a link */

		bottom[0] = e->pos2[0] + (e->mins[0] + e->maxs[0]) * 0.5f;
		bottom[1] = e->pos2[1] + (e->mins[1] + e->maxs[1]) * 0.5f;
		bottom[2] = e->pos2[2] + e->maxs[2];
		top[0] = e->pos1[0] + (e->mins[0] + e->maxs[0]) * 0.5f;
		top[1] = e->pos1[1] + (e->mins[1] + e->maxs[1]) * 0.5f;
		top[2] = e->pos1[2] + e->maxs[2];

		halfx = (e->maxs[0] - e->mins[0]) * 0.5f;
		halfy = (e->maxs[1] - e->mins[1]) * 0.5f;
		horiz = sqrtf(halfx * halfx + halfy * halfy) + SEED_SPACING;

		sb = Gen_SeedNear(bottom, horiz, 48.0f);
		st_top = Gen_SeedNear(top, horiz, 64.0f);
		if (sb < 0 || st_top < 0 || sb == st_top)
		{
			/* worth saying out loud: a plat whose landing never seeded is a
			 * hole the visual dump should be looked at for */
			gi.dprintf("rune: plat at (%.0f %.0f %.0f) unlinked, no seed at %s\n",
			           bottom[0], bottom[1], bottom[2],
			           sb < 0 ? "the bottom" : "the top");
			continue;
		}
		/* both ends can only be honest if the pair actually spans the
		 * travel -- otherwise two seeds on the same level got picked */
		if (gen_seeds[st_top].origin[2] - gen_seeds[sb].origin[2] <
		        (e->pos1[2] - e->pos2[2]) * 0.5f)
			continue;

		cost = Plat_TravelMs(e);
		before = gen_num_links;
		Link_Add(sb, st_top, RL_LIFT, cost, 0);
		if (gen_num_links > before)
		{
			VectorCopy(bottom, gen_links[gen_num_links - 1].anchor);
			gen_lift_links++;
		}

		/*
		 * The way back down. The lift link is one-way by construction, and a
		 * plat cannot carry a body down on demand:
		 *
		 *   Touch_Plat_Center (g_func.c:417-430) sends the plat UP when it is
		 *   at STATE_BOTTOM, but when it is at STATE_TOP the touch does the
		 *   opposite of a ride -- "ent->nextthink = level.time + 1; // the
		 *   player is still on the plat, so delay going down" (g_func.c:429).
		 *   Standing on a top-parked plat therefore postpones its descent for
		 *   as long as the body keeps standing there. The descent that does
		 *   happen is the timer plat_hit_top arms (think = plat_go_down,
		 *   nextthink = level.time + 3, g_func.c:349-350), and it fires only
		 *   once the rider has stopped touching the trigger -- i.e. once they
		 *   have already left.
		 *
		 * So there is no reverse RL_LIFT to write, and none is written. What
		 * covers the downward direction is ordinary physics: step off the
		 * ledge beside the plat. ProveDrop is asked for exactly that, top seed
		 * to bottom seed, and if it proves, the link is a real proven drop
		 * with a real lip in its anchor. If it does not prove, the hole is
		 * named out loud rather than papered over with a link no body could
		 * follow.
		 */
		if (Link_Exists(st_top, sb))
			continue;               /* the pair loop already got down there */
		{
			vec3_t lip;
			short dcost;
			byte despeed;

			if (ProveDrop(st_top, sb, lip, &dcost, &despeed))
			{
				int dbefore = gen_num_links;

				Link_Add(st_top, sb, RL_DROP, dcost, despeed);
				if (gen_num_links > dbefore)
				{
					rune_link_t *dl = &gen_links[gen_num_links - 1];

					VectorCopy(lip, dl->anchor);
					Link_Env_Drop(dl, dd_last_heading);
					gen_lift_down_drop++;
				}
			}
			else
			{
				gen_lift_down_none++;
				gi.dprintf("rune: plat at (%.0f %.0f %.0f) has no proven way "
				           "down (no drop, and a top-parked plat does not "
				           "descend on touch -- g_func.c:429)\n",
				           bottom[0], bottom[1], bottom[2]);
			}
		}
	}
	if (gen_lift_links)
		gi.dprintf("rune: %d lift links (%d matching drops down, %d with no way down)\n",
		           gen_lift_links, gen_lift_down_drop, gen_lift_down_none);
}

/*
 * Teleporter links. SP_misc_teleporter (g_misc.c:1925) frees any pad without
 * a target and spawns a small trigger over the pad origin carrying the pad's
 * target (g_misc.c:1946-1955). On touch, teleporter_touch resolves the
 * destination with G_Find (NULL, FOFS(targetname), self->target)
 * (g_misc.c:1883) -- the same one call made here -- copies the destination
 * origin onto the player and lifts it ten units (g_misc.c:1893-1895), clears
 * velocity and holds the body for 160ms (g_misc.c:1898-1900).
 *
 * Nothing here is proven, because nothing here is movement: the game assigns
 * the arrival. The only claim the link makes is that both ends are places a
 * body can be, which is what having a seed within SG_PAD_REACH of each means.
 * The 500ms cost covers the walk onto the pad and the engine's hold.
 */
static void Link_Teleporters(void)
{
	edict_t *e, *dest;
	int i;

	for (i = 0; i < globals.num_edicts; i++)
	{
		vec3_t pad, arrive;
		int sp, sd, before;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (strcmp(e->classname, "misc_teleporter") != 0)
			continue;
		if (!e->target)
			continue;

		dest = G_Find(NULL, FOFS(targetname), e->target);
		if (!dest)
			continue;

		VectorCopy(e->s.origin, pad);
		VectorCopy(dest->s.origin, arrive);
		arrive[2] += 10.0f;                 /* g_misc.c:1895 */

		sp = Gen_SeedNear(pad, SG_PAD_REACH, SG_PAD_REACH);
		sd = Gen_SeedNear(arrive, SG_PAD_REACH, SG_PAD_REACH);
		if (sp < 0 || sd < 0 || sp == sd)
		{
			gi.dprintf("rune: teleporter at (%.0f %.0f %.0f) has no seed at %s\n",
			           pad[0], pad[1], pad[2],
			           sp < 0 ? "the pad" : "the destination");
			continue;
		}

		before = gen_num_links;
		Link_Add(sp, sd, RL_TELEPORT, 500, 0);
		if (gen_num_links > before)
		{
			VectorCopy(pad, gen_links[gen_num_links - 1].anchor);
			gen_tele_links++;
		}
	}
	if (gen_tele_links)
		gi.dprintf("rune: %d teleport links\n", gen_tele_links);
}

/* ============================== end of the ADDITIVE BLOCK ============ */

/* ===================================================================
 * ADDITIVE BLOCK: rocket jumps.
 *
 * Everything between this banner and the one that closes it is new. It adds
 * functions, reads the seeds and the links the earlier passes produced, and
 * changes nothing above; one call site is appended at the very end of
 * Prove_All.
 *
 * A rocket jump is the only traversal in the graph that costs the mover
 * HEALTH, and that single fact shapes the whole pass:
 *
 *   - it runs LAST, after every other prover, because a rocket jump is only
 *     worth recording where nothing cheaper already gets there;
 *   - a pair is only offered to the prover when it needs the lift (the
 *     destination is well above the source) and could plausibly get it (below
 *     the ceiling the physics itself implies, SG_OracleRocketJumpCeiling);
 *   - a proven traversal is still THROWN AWAY if the graph already reaches
 *     the destination for less than three times what the jump cost, because
 *     a link that adds no reach is a link that only ever costs blood;
 *   - the price is recorded in the link (anchor[2], see sg_rune.h) so the
 *     runtime spends it deliberately.
 *
 * The proof itself is the oracle's, not this file's: pmove supplies the jump,
 * SG_OracleRocketJumpAim supplies the detonation point and the rocket's own
 * travel time, SG_OracleRocketJumpStep supplies the push exactly as
 * T_RadiusDamage and T_Damage would, and SG_OracleRun integrates the flight.
 * The prover only chooses the aim and judges the landing.
 * =================================================================== */

#define SG_RJ_MIN_RISE		80.0f	/* below this a plain jump (or a jump link
                                     * the pair loop already proved) does the
                                     * job, and paying ~50 health for it is
                                     * simply a bad trade */
#define SG_RJ_REACH			320.0f	/* horizontal, derived: the flattest aim
                                     * the prover tries (30 degrees off
                                     * vertical) converts about 275 u/s of the
                                     * kick into horizontal speed, and a body
                                     * that has to still be ABOVE its launch
                                     * height on arrival has spent under a
                                     * second in the air by then */
#define SG_RJ_SLACK			32		/* +/- 45 degrees around the launch */
#define SG_RJ_MAX_TRIES		2048	/* pass budget, in rolls -- see below */
#define SG_RJ_OPEN_MAX		256		/* redundancy search: open list cap */
#define SG_RJ_NODE_MAX		512		/* redundancy search: expansion cap */

static int rj_pairs, rj_tries, rj_noboom, rj_nolift, rj_arrived,
           rj_redundant, rj_links, rj_budget_out;

/*
 * Is the destination already reachable from the source for less than cap_ms,
 * using only the links the OTHER passes proved?
 *
 * A bounded Dijkstra, not a "does a direct link exist" test and not an
 * unbounded search. The direct-link test was rejected because it answers the
 * wrong question: two seeds with no direct link between them are very often
 * one stair-flight apart, and buying that with 50 health would be absurd.
 * Dijkstra answers the question actually being asked -- is there a route home
 * for less than three rocket jumps' worth of time -- and the cost cap keeps
 * it cheap on its own, because the frontier can only grow as far as cap_ms of
 * travel reaches. The expansion and open-list caps are belt and braces: if
 * either is hit the answer returned is TRUE, i.e. assume the map already
 * reaches it and do not write the expensive link. An unproven link costs
 * nothing; a health-priced link that adds no reach costs blood every time the
 * runtime is talked into it.
 *
 * The snapshot deliberately does not contain the rocket-jump links this pass
 * is itself adding. That is not an approximation: the question is whether the
 * map reaches the destination WITHOUT buying another rocket jump, so a route
 * that pays for one is not an answer to it.
 */
static int *rj_dist, *rj_stamp, rj_query;

static qboolean Reach_Within(int from, int to, int cap_ms)
{
	int open[SG_RJ_OPEN_MAX];
	int nopen = 0, expanded = 0;

	if (from == to)
		return true;
	if (!sw_first || !rj_dist || !rj_stamp)
		return true;                    /* no index: cannot claim it adds reach */

	rj_query++;
	rj_dist[from] = 0;
	rj_stamp[from] = rj_query;
	open[nopen++] = from;

	while (nopen > 0)
	{
		int bi = 0, k, cur, curd, li;

		for (k = 1; k < nopen; k++)
			if (rj_dist[open[k]] < rj_dist[open[bi]])
				bi = k;
		cur = open[bi];
		curd = rj_dist[cur];
		open[bi] = open[--nopen];

		if (cur == to)
			return true;
		if (curd > cap_ms)
			continue;
		if (++expanded > SG_RJ_NODE_MAX)
			return true;                /* budget out: assume reachable */

		for (li = sw_first[cur]; li >= 0; li = sw_next[li])
		{
			rune_link_t *l = &gen_links[li];
			int nd;

			/* a zero-cost link (a teleport pass writes short costs, a plat
			 * can compute one) must not make a free cycle */
			nd = curd + (l->cost_ms > 0 ? (int)l->cost_ms : 1);
			if (nd > cap_ms)
				continue;
			if (rj_stamp[l->to] == rj_query && rj_dist[l->to] <= nd)
				continue;
			rj_stamp[l->to] = rj_query;
			rj_dist[l->to] = nd;
			if (nopen >= SG_RJ_OPEN_MAX)
				return true;            /* budget out: assume reachable */
			open[nopen++] = l->to;
		}
	}
	return false;
}

/*
 * Prove one rocket jump, the way a player performs one: stand still, aim down
 * and back over the shoulder, jump, and fire so the rocket goes off under the
 * feet as the body leaves the floor.
 *
 * The classic technique is a family, not a single move, so the aim is
 * parameterised over three pitches -- straight down, and 15 and 30 degrees
 * behind vertical. Straight down buys the most height and no distance; the
 * tilted ones trade height for a horizontal throw toward the destination. The
 * first that arrives wins, which orders them the way a player would: take the
 * cheapest-looking shot that works.
 *
 * The sequencing is the game's, and it matters. The body jumps FIRST and the
 * rocket detonates when it arrives -- SG_OracleRocketJumpAim returns the
 * rocket's own travel time and the roll pays it, step by honest step, before
 * calling SG_OracleRocketJumpStep. Applying the blast and the jump in one
 * instant would have been wrong twice over: the game does not do it (the
 * rocket has to fly the 38-odd units to the floor first), and pmove would
 * have refused the jump anyway, because PM_CatagorizePosition takes a body
 * with a large upward velocity off the ground before PM_CheckJump ever looks
 * at it. Paying the flight time makes the proof weaker than the arithmetic --
 * the body has already risen when the burst goes off, so the burst is further
 * away and pushes less. That is the point: the arithmetic is not the claim,
 * the roll is.
 *
 * Entry state is a body at rest, so min_speed on the link is 0 and no
 * approach speed is claimed. The one thing the mover must get right is the
 * AIM, and that is what goes in the anchor.
 */
static qboolean ProveRocketJump(int from, int to, vec3_t anchor_out,
                                short *cost_ms, byte *exit_speed,
                                byte *heading_out)
{
	vec3_t src, dst, tdir;
	float horiz;
	int ai;
	static const float tilts[3] = { 0.0f, 15.0f, 30.0f };

	VectorCopy(gen_seeds[from].origin, src);
	VectorCopy(gen_seeds[to].origin, dst);

	tdir[0] = dst[0] - src[0];
	tdir[1] = dst[1] - src[1];
	tdir[2] = 0.0f;
	horiz = sqrtf(tdir[0] * tdir[0] + tdir[1] * tdir[1]);
	if (horiz < 1.0f)
	{
		/* straight overhead: there is no "behind", and only the vertical
		 * shot can help anyway */
		tdir[0] = 1.0f;
		tdir[1] = 0.0f;
	}
	else
	{
		tdir[0] /= horiz;
		tdir[1] /= horiz;
	}

	for (ai = 0; ai < 3; ai++)
	{
		sg_phantom_t ph;
		usercmd_t cmd;
		vec3_t aim, boom, before, kvel, want;
		float flight_ms, t = tilts[ai] * (float)(M_PI / 180.0);
		int elapsed, fsteps, s, health;
		byte heading;

		/* down, and back over the shoulder: the horizontal part of the aim
		 * points AWAY from the destination, which is what throws the body
		 * toward it */
		aim[0] = -tdir[0] * sinf(t);
		aim[1] = -tdir[1] * sinf(t);
		aim[2] = -cosf(t);

		if (!SG_OracleRocketJumpAim(src, aim, boom, &flight_ms))
		{
			rj_noboom++;
			continue;                   /* nothing under that aim to burst on */
		}

		SG_OraclePlace(&ph, src);
		elapsed = 0;

		/*
		 * The jump. Tapped, never held (PM_CheckJump refuses a held key), and
		 * the height of it is pmove's business -- this only presses the key.
		 */
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = STEP_MSEC;
		cmd.upmove = 400;
		SG_OracleRun(&ph, &cmd, 1);
		elapsed += STEP_MSEC;

		/*
		 * The rocket's own flight, rolled rather than assumed away. The
		 * rocket leaves the muzzle at the same instant the jump key goes
		 * down, so the jump step above IS the first step of the flight --
		 * counting it again would detonate the rocket a step late and quietly
		 * weaken every proof by the height the body gained in that step.
		 */
		fsteps = (int)(flight_ms / (float)STEP_MSEC + 0.5f);
		for (s = 1; s < fsteps; s++)
		{
			VectorSubtract(dst, ph.origin, want);
			memset(&cmd, 0, sizeof(cmd));
			cmd.msec = STEP_MSEC;
			cmd.angles[YAW] = ANGLE2SHORT(atan2f(want[1], want[0])
			                              * 180.0f / M_PI);
			cmd.forwardmove = 400;      /* air control: weak, but real */
			SG_OracleRun(&ph, &cmd, 1);
			elapsed += STEP_MSEC;
		}

		/* the detonation, applied by the oracle exactly as the game applies
		 * it, and paid for in health */
		VectorCopy(ph.velocity, before);
		health = SG_OracleRocketJumpStep(&ph, boom);
		if (health <= 0)
		{
			rj_nolift++;
			continue;                   /* out of range, or behind something */
		}
		VectorSubtract(ph.velocity, before, kvel);
		if (kvel[2] <= 0.0f)
		{
			rj_nolift++;
			continue;                   /* pushed sideways or down: not a jump */
		}

		/*
		 * The heading the link records is the direction the blast actually
		 * threw the body, taken from the velocity the oracle just added --
		 * not from the aim the prover asked for. When the shot went straight
		 * down there is no horizontal throw at all and the destination
		 * bearing is the only honest thing to write.
		 */
		if (kvel[0] * kvel[0] + kvel[1] * kvel[1] > 1.0f)
			heading = Heading_Quantize(kvel[0], kvel[1]);
		else
			heading = Heading_Quantize(tdir[0], tdir[1]);

		for (; elapsed < TRY_LIMIT_MS; elapsed += STEP_MSEC)
		{
			VectorSubtract(dst, ph.origin, want);

			if (want[0] * want[0] + want[1] * want[1] <
			        ARRIVE_RADIUS * ARRIVE_RADIUS &&
			    want[2] > -72.0f && want[2] < 72.0f &&
			    (ph.groundentity || ph.waterlevel >= 2) &&
			    Prove_Contact(ph.origin, dst))
			{
				float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
				                 ph.velocity[1] * ph.velocity[1]);

				rj_arrived++;
				*cost_ms = (short)elapsed;
				*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
				*heading_out = heading;
				/* the anchor's rocket-jump layout, documented in sg_rune.h:
				 * the horizontal aim, then the price */
				anchor_out[0] = aim[0];
				anchor_out[1] = aim[1];
				anchor_out[2] = (float)health;
				return true;
			}

			/* back on the floor below the destination: this shot fell short,
			 * and standing there running out the clock proves nothing */
			if (ph.groundentity && elapsed > 400 &&
			    ph.origin[2] < dst[2] - 72.0f)
				break;
			/* under the source and still falling: gone */
			if (ph.origin[2] < src[2] - 200.0f)
				break;

			memset(&cmd, 0, sizeof(cmd));
			cmd.msec = STEP_MSEC;
			cmd.angles[YAW] = ANGLE2SHORT(atan2f(want[1], want[0])
			                              * 180.0f / M_PI);
			cmd.forwardmove = 400;
			SG_OracleRun(&ph, &cmd, 1);
		}
	}
	return false;
}

/*
 * The pass. Runs after everything else, on pairs that need the lift and might
 * get it, and writes only what survives the redundancy gate.
 *
 * Budget: the same two mechanisms every other prover in this file is bounded
 * by -- the reach filters that decide which pairs are even candidates, and
 * TRY_LIMIT_MS inside the roll -- plus one this pass needs on its own,
 * because unlike run/jump/drop a rocket jump is a rare answer to a rare
 * question: a hard cap on how many rolls the pass may spend. Past the cap the
 * pass stops rather than degrading, and says so.
 */
static void Prove_RocketJumps(void)
{
	int i, j;
	float ceiling = SG_OracleRocketJumpCeiling();

	if (gen_num_seeds <= 0)
		return;

	Link_Index_Build();
	rj_dist = gi.TagMalloc(sizeof(int) * gen_num_seeds, TAG_GAME);
	rj_stamp = gi.TagMalloc(sizeof(int) * gen_num_seeds, TAG_GAME);
	memset(rj_stamp, 0, sizeof(int) * gen_num_seeds);
	rj_query = 0;

	gi.dprintf("rune: rocket jumps -- window %.0f to %.0f units of rise\n",
	           SG_RJ_MIN_RISE, ceiling);

	for (i = 0; i < gen_num_seeds && rj_tries < SG_RJ_MAX_TRIES; i++)
	{
		if (gen_seeds[i].flags & RSF_WATER)
			continue;                   /* no rocket jumps out of a pool */

		for (j = 0; j < gen_num_seeds && rj_tries < SG_RJ_MAX_TRIES; j++)
		{
			vec3_t d, anchor;
			short cost;
			byte espeed, heading;
			int before;
			rune_link_t *l;

			if (i == j || (gen_seeds[j].flags & RSF_WATER))
				continue;

			VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, d);
			if (d[2] < SG_RJ_MIN_RISE || d[2] > ceiling)
				continue;
			if (d[0] * d[0] + d[1] * d[1] > SG_RJ_REACH * SG_RJ_REACH)
				continue;
			rj_pairs++;

			/* already linked directly: whatever that link is, it is cheaper
			 * than blood. Costs one index lookup and saves a whole roll. */
			if (Link_Index_Find(i, j) >= 0)
			{
				rj_redundant++;
				continue;
			}

			rj_tries++;
			if (!ProveRocketJump(i, j, anchor, &cost, &espeed, &heading))
				continue;

			/*
			 * Proven -- and now the expensive question, asked with the real
			 * cost in hand rather than an estimate of it: does the graph
			 * already get there for less than three of these? If it does,
			 * the jump adds nothing but a health bill.
			 */
			if (Reach_Within(i, j, 3 * (int)cost))
			{
				rj_redundant++;
				continue;
			}

			before = gen_num_links;
			Link_Add(i, j, RL_ROCKETJUMP, cost, espeed);
			if (gen_num_links == before)
				continue;               /* Link_Add refused at LINK_MAX */
			l = &gen_links[gen_num_links - 1];
			VectorCopy(anchor, l->anchor);
			l->heading = heading;
			l->heading_slack = SG_RJ_SLACK;
			/*
			 * The proof stood the body still and fired, so no entry speed is
			 * required and none is claimed -- the same honesty Link_Env_Hook
			 * keeps. The heading is not an approach the body must arrive on
			 * either; it is the direction the blast threw the proof, and the
			 * cone around it says how far off that a body may end up and
			 * still be doing what was demonstrated.
			 */
			l->min_speed = 0;
			rj_links++;
		}
	}

	if (rj_tries >= SG_RJ_MAX_TRIES)
		rj_budget_out = 1;

	gi.TagFree(sw_first);
	gi.TagFree(sw_next);
	sw_first = NULL;
	sw_next = NULL;
	gi.TagFree(rj_dist);
	gi.TagFree(rj_stamp);
	rj_dist = NULL;
	rj_stamp = NULL;

	gi.dprintf("rune: rocketjump pairs=%d rolls=%d noboom=%d nolift=%d "
	           "arrived=%d redundant=%d links=%d%s\n",
	           rj_pairs, rj_tries, rj_noboom, rj_nolift, rj_arrived,
	           rj_redundant, rj_links,
	           rj_budget_out ? " (BUDGET EXHAUSTED, pass stopped early)" : "");
}

/* ======================= end of the ROCKET JUMP BLOCK ================ */

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
					Link_Env_Hook(l, gen_seeds[i].origin, anchor);
				}
				continue;
			}
			/*
			 * A plunge into water is the one deep descent the game makes
			 * safe -- the splash cancels the fall -- and the one these
			 * cutoffs made unprovable. lmctf05's halves connect through
			 * water at -1984: the red base was a 367-seed component with
			 * one hook in and NO way out, the red flag inside it, and
			 * every attacker on the map priced at infinity because of
			 * these two lines. Depth stays capped for dry landings.
			 */
			if (d[2] > 512.0f ||
			    (d[2] < -512.0f &&
			     !((gen_seeds[j].flags & RSF_WATER) && d[2] >= -2048.0f)))
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
			if (d[2] < -160.0f) dg_pairs++;
			if (d[2] < -160.0f &&
			    (d[2] >= -600.0f ||
			     ((gen_seeds[j].flags & RSF_WATER) && d[2] >= -2048.0f)))
			{
				vec3_t lip;

				if (ProveDrop(i, j, lip, &cost, &espeed))
				{
					rune_link_t *l;

					dg_arrived++;
					Link_Add(i, j, RL_DROP, cost, espeed);
					l = &gen_links[gen_num_links - 1];
					VectorCopy(lip, l->anchor);
					Link_Env_Drop(l, dd_last_heading);
					continue;
				}
			}
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
					Link_Env_Hook(l, gen_seeds[i].origin, anchor);
				}
				else if (d[2] <= 128.0f && d[2] >= -600.0f)
				{
					/*
					 * Both from-rest rolls and the rope failed. One card
					 * left: arrive at speed. The phantom enters at 320
					 * u/s on the target line and jumps off that momentum;
					 * what it proves, the envelope records -- min_speed
					 * 280 (a floor the 320 proof comfortably covers, the
					 * same honesty Link_Env_Drop keeps), heading the
					 * target line, the drop cone's slack. The flood
					 * charges arrival speed via Env_AccelCost; the body
					 * gates its hop on carrying the speed for real.
					 */
					int before = gen_num_links;

					/*
					 * Budgeted like the rocket-jump pass: every pair that
					 * failed three provers gets one more full pmove roll,
					 * and on a 3000-seed map that unbounded doubled the
					 * generation past its harness timeout. Six thousand
					 * rolls cover the gap-shaped candidates that matter.
					 */
					if (gen_momentum_tries >= 6000)
						continue;
					gen_momentum_tries++;

					gen_entry_speed = 320.0f;
					if (Prove(i, j, true, &cost, &espeed))
					{
						Link_Add(i, j,
						         (d[2] < -160.0f) ? RL_DROP : RL_JUMP,
						         cost, espeed);
						if (gen_num_links != before)
						{
							rune_link_t *l = &gen_links[gen_num_links - 1];

							l->heading = Heading_Quantize(d[0], d[1]);
							l->heading_slack = SG_DROP_SLACK;
							l->min_speed = (byte)(280.0f / 4.0f);
							gen_momentum_links++;
						}
					}
					gen_entry_speed = 0.0f;
				}
			}
		}
		if ((i & 255) == 0)
			gi.dprintf("rune: proving %d/%d seeds, %d links\n",
			           i, gen_num_seeds, gen_num_links);
	}

	/*
	 * ADDITIVE BLOCK call sites -- appended after the pair loop, which is
	 * untouched. Each pass reads the seeds and links the loop produced and
	 * only adds to them; Prove_Swims also relabels a run/jump link whose
	 * two ends are both underwater, because that is what it always was.
	 */
	Prove_Swims();          /* swim links: water to water, water to shore */
	{
		/*
		 * Mark the end of the proven links, run the two declaring passes, then
		 * re-stamp what they appended. The lift and teleport links were read
		 * off spawn data; the drops Link_Plats proves on the way down were
		 * rolled by the oracle like any other, and Link_Declare_Tail leaves
		 * those alone by looking at the action.
		 */
		int declared_mark = gen_num_links;

		Link_Plats();           /* func_plat: bottom seed -> top seed */
		Link_Teleporters();     /* misc_teleporter pad seed -> destination seed */
		Link_Declare_Tail(declared_mark);
	}

	/*
	 * ROCKET JUMP BLOCK call site. Last of all, on purpose: the pass asks
	 * whether the rest of the graph already reaches a place, and it can only
	 * ask that once the rest of the graph exists. It runs after
	 * Link_Declare_Tail as well, so the links it writes keep the RL_PROVEN
	 * stamp Link_Add gives them -- they were rolled, every one of them.
	 */
	Prove_RocketJumps();
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
	/* ADDITIVE BLOCK call site: the water volumes the dry passes cannot
	 * reach into, seeded before anything is proven so the pair loop sees
	 * them like any other seed */
	Seed_Water();
	/*
	 * EXPOSURE, into the reserved field. area_hint has been written zero
	 * since the format was born; it becomes the seed's exposure count --
	 * how many of up to 24 sampled seeds within 1000 units can see it,
	 * eye to eye, MASK_OPAQUE. The duel's cover pricing burns a runtime
	 * trace per candidate today; with exposure in the rune it is a
	 * lookup, and "sneak past" becomes a gradient the whole map wide.
	 * Old runes read as exposure 0 everywhere -- honest "unknown", and
	 * consumers treat 0 as no-opinion.
	 */
	{
		int i, j, step, vis, sampled;

		gi.dprintf("rune: measuring exposure...\n");
		for (i = 0; i < gen_num_seeds; i++)
		{
			vec3_t a;
			trace_t etr;

			VectorCopy(gen_seeds[i].origin, a);
			a[2] += 22.0f;
			vis = sampled = 0;
			/* a stride walk gives a stable pseudo-sample without rand() */
			step = (gen_num_seeds / 24) | 1;
			for (j = i % step; j < gen_num_seeds && sampled < 24; j += step)
			{
				vec3_t b, d2;

				if (j == i)
					continue;
				VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, d2);
				if (VectorLength(d2) > 1000.0f)
					continue;
				sampled++;
				VectorCopy(gen_seeds[j].origin, b);
				b[2] += 22.0f;
				etr = gi.trace(a, NULL, NULL, b, NULL, MASK_OPAQUE);
				if (etr.fraction >= 1.0f)
					vis++;
			}
			/* scale to the byte-ish range; sampled can be < 24 in sparse
			 * regions, so store the RATE, not the raw count */
			gen_seeds[i].area_hint = (short)(sampled ?
			    (vis * 255) / sampled : 0);
		}
	}
	gi.dprintf("rune: %d seeds; proving links...\n", gen_num_seeds);
	Prove_All();
	gi.dprintf("rune: %d links proven\n", gen_num_links);
	gi.dprintf("rune: dropstats pairs=%d seek=%d noedge=%d fellsteps=%d arrived=%d nocontact=%d\n",
	           dg_pairs, dg_seek, dg_noedge, dg_fell, dg_arrived, dg_nocontact);
	gi.dprintf("rune: geodrop nolip=%d fenced=%d flew=%d landedsteps=%d won=%d\n",
	           dd_nolip, dd_fenced, dd_flew, dd_landed, dd_won);
	gi.dprintf("rune: envelopes drop=%d hook=%d; declared=%d (lift=%d tele=%d); "
	           "plat-down drop=%d unlinked=%d; momentum=%d\n",
	           gen_env_drop, gen_env_hook, gen_declared_links,
	           gen_lift_links, gen_tele_links,
	           gen_lift_down_drop, gen_lift_down_none, gen_momentum_links);
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
