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
 * target, feed it honest usercmds (run first; one direct jump if plain running
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
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_identity.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_rune_install.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_util.h"

#define SEED_SPACING	64.0f
#define SEED_MAX		RUNE_MAX_SEEDS
#define LINK_MAX		RUNE_MAX_LINKS
_Static_assert(SEED_MAX == SG_RUNE_V3_MAX_SEEDS,
	"generator/v3 seed capacity drift");
_Static_assert(LINK_MAX == SG_RUNE_V3_MAX_LINKS,
	"generator/v3 link capacity drift");
#define LINK_REACH		192.0f		/* run/jump pairs within this reach */
#define HOOK_REACH		448.0f		/* hook pairs may span further */
#define ARRIVE_RADIUS	40.0f
#define STEP_MSEC		25			/* honest client-rate steps, 4 per frame */
#define TRY_LIMIT_MS	3000		/* a link longer than this is not local */
#define DROP_APPROACH_LIMIT_MS 2500	/* reach the serialized lip on foot */
#define DROP_TRAVEL_LIMIT_MS   2000	/* then complete the fall/landing */
#define DROP_HANDOFF_RADIUS    8.0f	/* runtime's lip-to-walkoff handoff */

_Static_assert(STEP_MSEC == SG_RUNE_PROOF_PMOVE_SUBSTEP_MS,
	"generator/v3 pmove cadence drift");

/* prover autopsy: where drop attempts actually die */
static int dg_pairs, dg_seek, dg_noedge, dg_fell, dg_arrived, dg_nocontact;

static rune_seed_t	*gen_seeds;
static int			gen_num_seeds;
static qboolean		gen_seed_overflow;
static rune_link_t	*gen_links;
static int			gen_num_links;
static qboolean		gen_link_overflow;
static qboolean		gen_water_overflow;

/* spatial hash so the lattice dedupes at SEED_SPACING */
#define HASH_SIZE 4096
static int hash_head[HASH_SIZE];
static int hash_next[SEED_MAX];
static byte gen_source_stable[SEED_MAX];
static byte gen_source_waterlevel[SEED_MAX];

static qboolean Seed_Representable(const vec3_t origin)
{
	return isfinite(origin[0]) && isfinite(origin[1]) && isfinite(origin[2]) &&
	       origin[0] >= -4096.0f && origin[0] <= 4095.875f &&
	       origin[1] >= -4096.0f && origin[1] <= 4095.875f &&
	       origin[2] >= -4096.0f && origin[2] <= 4095.875f;
}

typedef enum rune_v3_recheck_failure_e
{
	RUNE_V3_RECHECK_NONE = 0,
	RUNE_V3_RECHECK_IDENTITY,
	RUNE_V3_RECHECK_PROOF_LAW
} rune_v3_recheck_failure_t;

typedef struct rune_v3_recheck_s
{
	const char *mapname;
	const sg_rune_v3_authority_t *captured;
	rune_v3_recheck_failure_t failure;
	sg_identity_status_t identity_status;
} rune_v3_recheck_t;

typedef struct rune_v3_stream_s
{
	const sg_rune_v3_identity_t *identity;
	const rune_seed_t *seeds;
	uint32_t num_seeds;
	const rune_link_t *links;
	uint32_t num_links;
	sg_rune_v3_workspace_t *workspace;
} rune_v3_stream_t;

static uint32_t Rune_V3FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static qboolean Rune_V3PhysicsCapture(const sg_level_identity_t *level_id,
	sg_rune_v3_identity_t *wire_id)
{
	cvar_t *airaccelerate;
	float gravity;

	if (!level_id || !wire_id || !sg_host.cvar)
		return false;
	airaccelerate = sg_host.cvar("sv_airaccelerate", "0", 0);
	gravity = sv_gravity ? sv_gravity->value : 0.0f;
	if (!sv_gravity || !isfinite(gravity) ||
	    gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
	    gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
	    (SG_RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED &&
	     gravity != (float)(short)gravity) ||
	    !airaccelerate || !isfinite(airaccelerate->value) ||
	    (SG_RUNE_PROOF_AIRACCELERATE_ZERO_REQUIRED &&
	     airaccelerate->value != 0.0f) ||
	    !sv_maxvelocity || !isfinite(sv_maxvelocity->value) ||
	    sv_maxvelocity->value < (float)SG_RUNE_PROOF_MAXVELOCITY_MIN ||
	    !SG_RuneV3FunkyGravityCompatible(want_funky_gravity
	        ? &want_funky_gravity->value : NULL) ||
	    FRAMETIME !=
	        (float)SG_RUNE_PROOF_SERVER_FRAME_MS / 1000.0f ||
	    level_id->host_physics_id != SG_HOST_PHYSICS_EPOCH)
		return false;

	memset(wire_id, 0, sizeof(*wire_id));
	memcpy(wire_id->map_name, level_id->mapname,
		SG_RUNE_V3_MAP_NAME_BYTES);
	wire_id->bsp_checksum = level_id->bsp_checksum;
	wire_id->entity_crc32 = level_id->entity_crc32;
	wire_id->physics_flags = SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED;
	wire_id->gravity = gravity;
	wire_id->airaccelerate = airaccelerate->value;
	wire_id->maxvelocity = sv_maxvelocity->value;
	wire_id->pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	wire_id->server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	wire_id->host_physics_id = level_id->host_physics_id;
	return true;
}

qboolean SG_RuneV3AuthorityCapture(const char *mapname,
	sg_rune_v3_authority_t *authority)
{
	if (!authority)
		return false;
	memset(authority, 0, sizeof(*authority));
	authority->identity_status = SG_LevelIdentitySnapshot(mapname,
		&authority->level);
	if (authority->identity_status != SG_IDENTITY_OK)
		return false;
	return Rune_V3PhysicsCapture(&authority->level, &authority->wire);
}

qboolean SG_RuneV3AuthorityMatchesHeader(
	const sg_rune_v3_authority_t *authority,
	const sg_rune_v3_header_t *header)
{
	return authority && authority->identity_status == SG_IDENTITY_OK &&
	       header &&
	       SG_RuneV3MatchIdentity(header, &authority->wire) == RLW_OK;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	sg_rune_v3_authority_t active;

	if (!rune ||
	    !SG_RuneV3AuthorityCapture(rune->v3_header.map_name, &active))
		return false;
	return SG_RuneV3AuthorityMatchesHeader(&active, &rune->v3_header);
}

static qboolean Rune_V3LevelIdentityEqual(const sg_level_identity_t *first,
	const sg_level_identity_t *second)
{
	return first && second &&
	       first->bsp_checksum == second->bsp_checksum &&
	       first->entity_crc32 == second->entity_crc32 &&
	       first->host_physics_id == second->host_physics_id &&
	       memcmp(first->mapname, second->mapname,
	           SG_LEVEL_IDENTITY_MAPNAME_BYTES) == 0;
}

static qboolean Rune_V3ProofLawEqual(const sg_rune_v3_identity_t *first,
	const sg_rune_v3_identity_t *second)
{
	return first && second &&
	       first->physics_flags == second->physics_flags &&
	       Rune_V3FloatBits(first->gravity) ==
	           Rune_V3FloatBits(second->gravity) &&
	       Rune_V3FloatBits(first->airaccelerate) ==
	           Rune_V3FloatBits(second->airaccelerate) &&
	       Rune_V3FloatBits(first->maxvelocity) ==
	           Rune_V3FloatBits(second->maxvelocity) &&
	       first->pmove_substep_ms == second->pmove_substep_ms &&
	       first->server_frame_ms == second->server_frame_ms;
}

static int Rune_V3Revalidate(void *context)
{
	rune_v3_recheck_t *recheck = context;
	sg_rune_v3_authority_t active;

	if (!recheck || !recheck->captured)
		return 0;
	recheck->failure = RUNE_V3_RECHECK_NONE;
	if (!SG_RuneProofScopeActive() ||
	    (float)SG_RuneProofGravity() != recheck->captured->wire.gravity)
	{
		recheck->failure = RUNE_V3_RECHECK_PROOF_LAW;
		return 0;
	}
	if (!SG_RuneV3AuthorityCapture(recheck->mapname, &active))
	{
		recheck->identity_status = active.identity_status;
		recheck->failure = active.identity_status == SG_IDENTITY_OK
			? RUNE_V3_RECHECK_PROOF_LAW : RUNE_V3_RECHECK_IDENTITY;
		return 0;
	}
	if (!Rune_V3LevelIdentityEqual(&recheck->captured->level,
	    &active.level))
	{
		recheck->failure = RUNE_V3_RECHECK_IDENTITY;
		recheck->identity_status = SG_IDENTITY_UNAVAILABLE;
		return 0;
	}
	if (!Rune_V3ProofLawEqual(&recheck->captured->wire, &active.wire))
	{
		recheck->failure = RUNE_V3_RECHECK_PROOF_LAW;
		return 0;
	}
	return 1;
}

static sg_rune_write_result_t Rune_V3Stream(void *context,
	sg_rune_write_sink_fn sink, void *sink_context)
{
	rune_v3_stream_t *stream = context;

	return SG_RuneV3Write(stream->identity, stream->seeds,
		stream->num_seeds, stream->links, stream->num_links,
		stream->workspace, sink, sink_context);
}

static int Seed_HashKey(vec3_t p)
{
	int x = (int)floorf(p[0] / SEED_SPACING);
	int y = (int)floorf(p[1] / SEED_SPACING);
	int z = (int)floorf(p[2] / (SEED_SPACING * 2.0f));
	unsigned int h = (unsigned int)x * 73856093u ^
	                 (unsigned int)y * 19349663u ^
	                 (unsigned int)z * 83492791u;

	return (int)(h & (HASH_SIZE - 1));
}

static qboolean Seed_Nearby(vec3_t p)
{
	int dx, dy, dz, i;
	vec3_t d;

	if (!Seed_Representable(p))
		return true; /* invalid candidates are never insertion opportunities */
	/* The acceptance radius crosses hash-cell boundaries in all three axes.
	 * Searching only p's own cell admitted duplicate/near-duplicate seeds on
	 * opposite sides of a boundary, inflating the O(n^2) proof and making
	 * localization ambiguous. Probe every neighboring logical cell; hash
	 * collisions merely rescan a chain and cannot create a false match. */
	for (dz = -1; dz <= 1; dz++)
		for (dy = -1; dy <= 1; dy++)
			for (dx = -1; dx <= 1; dx++)
			{
				vec3_t probe;
				int key;

				probe[0] = p[0] + dx * SEED_SPACING;
				probe[1] = p[1] + dy * SEED_SPACING;
				probe[2] = p[2] + dz * SEED_SPACING * 2.0f;
				key = Seed_HashKey(probe);
				for (i = hash_head[key]; i >= 0; i = hash_next[i])
				{
					VectorSubtract(gen_seeds[i].origin, p, d);
					if (d[2] > -48.0f && d[2] < 48.0f &&
					    d[0] * d[0] + d[1] * d[1] <
					        SEED_SPACING * SEED_SPACING * 0.81f)
						return true;
				}
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

		tr = sg_host.trace(start, mins, maxs, down, NULL, MASK_PLAYERSOLID);
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

/* Flags describe executable source semantics, not which seeding pass happened
 * to discover the point. A floor seed with water over the player's waist is
 * a swim source even when the ordinary ground flood found it first. Classify
 * the placed player box through one zero command so generic JUMP/DROP never
 * serializes an action whose live source gate must reject. */
static int Seed_SourceWaterlevel(vec3_t origin, int *watertype)
{
	sg_phantom_t ph;
	usercmd_t cmd;

	SG_OraclePlace(&ph, origin);
	memset(&cmd, 0, sizeof(cmd));
	/* Zero elapsed time asks Pmove to categorize the exact fixed-point body
	 * without first drifting it across a surface boundary. */
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	if (watertype)
		*watertype = ph.watertype;
	return ph.waterlevel;
}

/* Injected rest is not a realizable live staging state on slick ground:
 * zero-input Pmove deliberately applies no ground friction there. Keep the
 * seed for ordinary navigation, but never prove a standstill ballistic
 * action from it. */
static qboolean Seed_SourceUnstable(vec3_t origin)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, end;
	sg_phantom_t ph;
	usercmd_t cmd;
	short fixed[3];
	trace_t tr;
	int i, step;

	VectorCopy(origin, start);
	VectorCopy(origin, end);
	start[2] += 1.0f;
	end[2] -= 4.0f;
	/* Use the same standing hull that accepted the source. A centre point ray
	 * misses slick support at ledges and seams even while the player's feet are
	 * resting on that face. */
	tr = sg_host.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
	if (tr.fraction >= 1.0f || !tr.surface ||
	    (tr.surface->flags & SURF_SLICK) || (tr.contents & MASK_CURRENT))
		return true;

	/* The stronger invariant is realizability, not a texture name. Roll the
	 * exact zero-input 4x25 ms state that staging must hold. Conveyors and
	 * current-bearing shallows accelerate a body even when their floor is not
	 * SURF_SLICK; a source that moves under this command can never become the
	 * injected-rest state used by JUMP/DROP/HOOK proofs. */
	SG_OraclePlace(&ph, origin);
	for (i = 0; i < 3; i++)
		fixed[i] = ph.pms.origin[i];
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = STEP_MSEC;
	for (step = 0; step < 4; step++)
		if (!SG_OracleRunWorld(&ph, &cmd, 1))
			return true;
	if (!ph.groundentity || ph.waterlevel >= 2 ||
	    (ph.watertype & (MASK_CURRENT | CONTENTS_LAVA | CONTENTS_SLIME)))
		return true;
	for (i = 0; i < 3; i++)
		if (ph.pms.origin[i] != fixed[i] || ph.pms.velocity[i] != 0)
			return true;
	return false;
}

static void Seed_Add(vec3_t origin)
{
	int key, watertype = 0, waterlevel;
	qboolean submerged;

	/* Pmove stores origin as signed eighth units. Reject before hashing or
	 * SG_OraclePlace casts, so an oversized/malformed map cannot invoke an
	 * undefined float-to-short conversion and later write a self-rejected rune. */
	if (!Seed_Representable(origin))
		return;
	if (gen_num_seeds >= SEED_MAX)
	{
		gen_seed_overflow = true;
		return;
	}
	/* The entity germ pass tests mapper origin before grounding, and every
	 * caller can converge on the same floor point. The final grounded point is
	 * the identity that matters, so dedupe again at the only insertion gate. */
	if (Seed_Nearby(origin))
		return;
	waterlevel = Seed_SourceWaterlevel(origin, &watertype);
	submerged = waterlevel >= 2;
	/* Lava and slime use water movement, but are not navigation volume: a
	 * generated route cannot promise the inventory/health needed to survive
	 * them. Do not spend either the ground or water graph budget on such a
	 * source. */
	if (submerged && (watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return;
	VectorCopy(origin, gen_seeds[gen_num_seeds].origin);
	gen_seeds[gen_num_seeds].area_hint = 0;
	gen_seeds[gen_num_seeds].flags = submerged ? RSF_WATER : 0;
	gen_source_stable[gen_num_seeds] =
	    Seed_SourceUnstable(origin) ? 0 : 1;
	gen_source_waterlevel[gen_num_seeds] = (byte)waterlevel;

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
				wtr = sg_host.trace(from, pmins, pmaxs, to, NULL, MASK_PLAYERSOLID);
				/* A fraction is not a walk.  At 64-unit lattice spacing the old
				 * 0.9 tolerance admitted a body whose hull stopped as much as 6.4
				 * units before the candidate; repeated flood steps then populated
				 * sealed rail/wall pockets as large, internally connected islands.
				 * The endpoint must be wholly reachable by this exact hull sweep. */
				if (wtr.startsolid || wtr.allsolid || wtr.fraction < 1.0f)
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

	/* Declared mechanisms need STATIC approach/egress seeds.  Their exact
	 * centres are the wrong graph nodes: a teleporter pad is inside the trigger
	 * whose side effect ordinary Pmove deliberately refuses to prove, and a
	 * plat centre is supported by a moving BSP.  Germinate just outside those
	 * footprints instead.  The declared controller owns the final touch/ride.
	 *
	 * A teleporter destination is different.  It is immutable solid geometry,
	 * the engine authoritatively places the body on it, and the oracle admits
	 * that pedestal just like a flag stand.  Preserve its exact arrival germ so
	 * the declared endpoint and the ordinary egress graph share one state. */
	for (i = 0; i < globals.num_edicts; i++)
	{
		static const float dirs[4][2] = {
			{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
		};
		vec3_t center;
		int d;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (!strcmp(e->classname, "misc_teleporter_dest"))
		{
			vec3_t ground;

			VectorCopy(e->s.origin, center);
			center[2] += 10.0f; /* teleporter_touch's authoritative arrival */
			if (Seed_Ground(center, ground))
				Seed_Add(ground);
			continue;
		}
		if (!strcmp(e->classname, "misc_teleporter"))
		{
			VectorCopy(e->s.origin, center);
			for (d = 0; d < 4; d++)
			{
				vec3_t candidate, ground;

				VectorCopy(center, candidate);
				/* The pad model is 32 units from centre and the player hull is
				 * another 16; linkentity's clip fringe makes 48 still overlap.
				 * Seventy-two leaves a real static staging body. */
				candidate[0] += dirs[d][0] * 72.0f;
				candidate[1] += dirs[d][1] * 72.0f;
				candidate[2] += 40.0f;
				if (Seed_Ground(candidate, ground))
					Seed_Add(ground);
			}
			continue;
		}
		if (strcmp(e->classname, "func_plat") != 0 || e->targetname)
			continue;
		{
			float halfx = (e->maxs[0] - e->mins[0]) * 0.5f;
			float halfy = (e->maxs[1] - e->mins[1]) * 0.5f;
			int end;

			/* Seed both the bottom queue and the top landing perimeter. */
			for (end = 0; end < 2; end++)
			{
				vec3_t position;

				if (end)
					VectorCopy(e->pos1, position);
				else
					VectorCopy(e->pos2, position);
				center[0] = position[0] +
				    (e->mins[0] + e->maxs[0]) * 0.5f;
				center[1] = position[1] +
				    (e->mins[1] + e->maxs[1]) * 0.5f;
				center[2] = position[2] + e->maxs[2] + 40.0f;
				for (d = 0; d < 4; d++)
				{
					vec3_t candidate, ground;
					float outside =
					    ((d < 2) ? halfx : halfy) + 24.0f;

					VectorCopy(center, candidate);
					candidate[0] += dirs[d][0] * outside;
					candidate[1] += dirs[d][1] * outside;
					if (Seed_Ground(candidate, ground))
						Seed_Add(ground);
				}
			}
		}
	}

	for (i = 0; i < globals.num_edicts; i++)
	{
		vec3_t ground;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		/* Things players stand at: spawns, items, flags, and position hints.
		 * Mechanism germs were handled above with action-specific geometry;
		 * adding their raw entity origins here would recreate the impossible
		 * pad/plat-centre graph islands this pass is designed to avoid. */
		if (strncmp(e->classname, "info_player", 11) != 0 &&
		    strncmp(e->classname, "item_", 5) != 0 &&
		    strncmp(e->classname, "weapon_", 7) != 0 &&
		    strncmp(e->classname, "ammo_", 5) != 0 &&
		    strncmp(e->classname, "info_flag", 9) != 0 &&
		    strncmp(e->classname, "info_position", 13) != 0)
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
static qboolean Prove_Contact(const vec3_t at, const vec3_t target)
{
	vec3_t a2, t2;
	trace_t tr;

	VectorCopy(at, a2);
	VectorCopy(target, t2);
	a2[2] += 16.0f;
	t2[2] += 16.0f;
	tr = sg_host.trace(a2, NULL, NULL, t2, NULL, MASK_PLAYERSOLID);
	return tr.fraction >= 1.0f;
}

static int gen_momentum_links;
static int gen_waypoint_links;

/*
 * The proof's detour apex, when it had one. The oracle steers greedily but
 * persistently and ROUNDS obstacles the runtime feeler fan cannot solve --
 * seed 327's pillar was walked around by every proof and ground against by
 * every body (iters 44-50, the lmctf01 valley). A RUN link's anchor field
 * has sat empty since the format was born; the point of maximum deviation
 * from the straight line now goes there whenever the proof deviated more
 * than 48 units, and the body steers via it. Zero anchor = straight proof.
 */
static vec3_t gen_prove_wp;
static qboolean gen_prove_has_wp;

/*
 * Entry speed for the NEXT Prove roll, consumed by Prove at placement. Zero
 * means the from-rest proof every link has had since the first cut. Nonzero
 * is the momentum experiment: a gap too wide for the runway inside one
 * proof's approach can still be crossed by a body that ARRIVES at speed --
 * which is the case min_speed on the envelope was designed to record and
 * never had a writer for.
 */
static qboolean Prove(int from, int to, qboolean jump,
                      short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	usercmd_t cmd;
	int elapsed;
	vec3_t want, d;
	float yaw;
	float old_frame_z = 0.0f;
	float edge_yaw = 0.0f;
	int edge_hold_steps = 0;
	qboolean jump_tapped = false;
	qboolean jump_airborne = false;

	vec3_t wp_path[128];
	int wp_n = 0;

	SG_OraclePlace(&ph, gen_seeds[from].origin);
	/* Seed_Ground established this as a standing source, and the live bot's
	 * launch gate likewise knows groundentity before command zero. Pmove will
	 * replace the sentinel on the first step; setting it here prevents the
	 * prover from taking an unmodelled 25 ms acceleration step before its one
	 * jump tap merely because SG_OraclePlace has not run Pmove yet. */
	ph.groundentity = true;
	/* Water-source motion is generated by the dedicated swim pass. Generic
	 * JUMP's dry standing/tap contract is not executable by a submerged body. */
	if (jump && ((gen_seeds[from].flags & RSF_WATER) ||
	             !gen_source_stable[from]))
		return false;
	if (jump && (gen_seeds[to].flags & RSF_WATER) &&
	    (sg_host.pointcontents(gen_seeds[to].origin) &
	     (CONTENTS_SLIME | CONTENTS_LAVA)))
		return false;

	for (elapsed = 0; elapsed < TRY_LIMIT_MS; elapsed += STEP_MSEC)
	{
		qboolean arrived = false;

		if (wp_n < 128 && (elapsed / STEP_MSEC) % 2 == 0)
		{
			VectorCopy(ph.origin, wp_path[wp_n]);
			wp_n++;
		}
		VectorSubtract(gen_seeds[to].origin, ph.origin, want);
		d[0] = want[0]; d[1] = want[1]; d[2] = 0.0f;
		/* P_WorldEffects runs once per server frame after the four 25 ms
		 * commands. A geometry proof cannot promise survival through lava or
		 * slime without encoding health/enviro state, so reject that boundary. */
		if ((elapsed % 100) == 0)
		{
			if (ph.waterlevel > 0 &&
			    (ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
				return false;
			if (d[0] * d[0] + d[1] * d[1] <
			        ARRIVE_RADIUS * ARRIVE_RADIUS &&
			    want[2] > -72.0f && want[2] < 72.0f &&
			    ((jump && (gen_seeds[to].flags & RSF_WATER)) ?
			         ph.waterlevel == 3 :
			         (ph.groundentity || ph.waterlevel >= 2)) &&
			    (!jump || jump_airborne))
			{
				if (Prove_Contact(ph.origin, gen_seeds[to].origin))
				{
					dg_arrived++;
					arrived = true;
				}
				else
					dg_nocontact++;
			}
			/* P_FallingDamage runs once per production frame. RUN has no
			 * health contract, and a JUMP may spend damage only on its aligned
			 * terminal landing where SG_BallisticSurvivable prices it. */
			if (P_FallDelta(old_frame_z, ph.velocity[2], ph.groundentity,
			                ph.waterlevel) > 30.0f && (!jump || !arrived))
				return false;
			old_frame_z = ph.velocity[2];
		}

		/* Arrival is judged before the landing guard below: the one legitimate
		 * return to ground is the jump landing at its destination. A dry JUMP
		 * cannot succeed before its one tap actually made the body airborne;
		 * submerged movement keeps Prove's existing swim semantics. */
		/* Runtime owns four 25 ms commands per server frame and can retire a
		 * link only at the next 100 ms think boundary. Judge success/failure at
		 * those same boundaries; accepting a transient 25 ms landing would prove
		 * a state the live controller necessarily runs past. */
		if (arrived)
		{
			float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
			                 ph.velocity[1] * ph.velocity[1]);

			/* the detour apex: max 2D deviation from the endpoint line */
			{
				vec3_t ab;
				float ablen, bestdev = 0.0f;
				int pi;

				VectorSubtract(gen_seeds[to].origin,
				               gen_seeds[from].origin, ab);
				ab[2] = 0.0f;
				ablen = sqrtf(ab[0] * ab[0] + ab[1] * ab[1]);
				gen_prove_has_wp = false;
				if (ablen > 1.0f)
					for (pi = 0; pi < wp_n; pi++)
					{
						float cx = (wp_path[pi][0] - gen_seeds[from].origin[0]),
						      cy = (wp_path[pi][1] - gen_seeds[from].origin[1]);
						float dev = fabsf(cx * ab[1] - cy * ab[0]) / ablen;

						if (dev > bestdev)
						{
							bestdev = dev;
							VectorCopy(wp_path[pi], gen_prove_wp);
						}
					}
				if (bestdev > 48.0f)
					gen_prove_has_wp = true;
			}
			/* Runtime detects the same closed brush and waits before resuming this
			 * RUN. Charge a conservative open budget here so fields do not price an
			 * asynchronous door as if it vanished on trigger contact. */
			if (ph.door_passed && ph.door_open_ms < elapsed + 200)
				return false;
			if (elapsed + (ph.door_passed ? ph.door_wait_ms : 0) > 32767)
				return false;
			*cost_ms = (short)(elapsed +
			    (ph.door_passed ? ph.door_wait_ms : 0));
			/* A zero-millisecond edge is free in every field flood and fails the
			 * on-disk contract. Arrival at command zero means no traversal was
			 * demonstrated; do not serialize the pair as a movement edge. */
			if (*cost_ms <= 0)
				return false;
			*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
			return true;
		}
		if (jump && jump_airborne && ph.groundentity && (elapsed % 100) == 0)
			return false;       /* landed somewhere else: no second hop */
		if (jump && jump_airborne && ph.waterlevel >= 2 &&
		    (elapsed % 100) == 0)
			return false;       /* splashed short: runtime retires at this boundary */

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
		if (!jump && want[2] < -100.0f &&
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
					etr = sg_host.trace(ep, NULL, NULL, edown, NULL, MASK_PLAYERSOLID);
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
		 *
		 * This is the RUN controller only. A JUMP is a replayable direct arc:
		 * every 25 ms command aims at the destination itself, with no fan-selected
		 * detour that would need additional serialized state at runtime.
		 */
		if (!jump)
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
				ftr = sg_host.trace(ph.origin, mins, maxs, probe, NULL,
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
		/* submerged: swim toward the target height -- PM_WaterMove reads
		 * upmove directly, no jump semantics under water. A water-origin
		 * jump fallback therefore remains a swim proof, as before. */
		if (!jump && ph.waterlevel >= 2)
		{
			cmd.upmove = (want[2] > 24.0f) ? 300
			           : (want[2] < -24.0f ? -300 : 0);
		}
		/* A dry seed is generated from a player-box ground trace, so its initial
		 * state is eligible even though SG_OraclePlace has not yet copied a
		 * pmove groundentity result back into the phantom. This makes the tap the
		 * first 25 ms command, matching runtime launch from the centered source.
		 * Later eligibility uses pmove's actual ground result. PM_CheckJump
		 * refuses a held key; never issuing a second tap also prevents a failed
		 * arc from proving via bunny hops. */
		else if (jump && !jump_tapped &&
		         (ph.groundentity ||
		          (elapsed == 0 && !(gen_seeds[from].flags & RSF_WATER))))
		{
			cmd.upmove = 400;
			jump_tapped = true;
		}

		if (!SG_OracleRunWorld(&ph, &cmd, 1))
			return false;
		/* Only ordinary RUN has a runtime wait/resume policy for a validated
		 * door precondition. A jump cannot pause after its tap without changing
		 * the proved launch state. */
		if (jump && ph.door_passed)
			return false;
		if (jump && jump_tapped && !ph.groundentity)
			jump_airborne = true;

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
 * The serialized anchor is a control tuple: exact usercmd-quantized pitch/yaw
 * plus distance along the normalized handed muzzle ray. Generation proves the
 * reconstructed static-world bite. Runtime repeats the view and re-proves the
 * actual source/bite snapshot immediately before firing.
 */
#define Q2_MASK_SHOT_GEN 0x6000003

/* Open-sky lips have no usable overhead trace. Try a deliberately small,
 * symmetric ray fan around the source-eye -> destination-upper-body line.
 * This helper preserves the ordinary hook contract: exact quantized view,
 * handed muzzle clearance, first static-world bite, idempotent reconstructed
 * ray, clear bolt flight, and the complete production-cadence traversal. */
static qboolean ProveHookLateralCandidate(int from, int to,
                                          float pitch, float yaw,
                                          vec3_t control_out,
                                          short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	vec3_t source, view_angles, forward, right, muzzle, shot_end, bite, want;
	sg_hook_proof_t proof;
	trace_t tr;
	int flight_ms;

	source[0] = (short)(gen_seeds[from].origin[0] * 8.0f) * 0.125f;
	source[1] = (short)(gen_seeds[from].origin[1] * 8.0f) * 0.125f;
	source[2] = (short)(gen_seeds[from].origin[2] * 8.0f) * 0.125f;
	view_angles[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(pitch));
	view_angles[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(yaw));
	view_angles[ROLL] = 0.0f;
	if (view_angles[PITCH] < -89.0f || view_angles[PITCH] > 89.0f)
		return false;

	SG_OraclePlace(&ph, gen_seeds[from].origin);
	AngleVectors(view_angles, forward, right, NULL);
	CTF_HookMuzzle(source, 22.0f, RIGHT_HANDED, forward, right, muzzle);
	VectorNormalize(forward);
	tr = sg_host.trace(source, NULL, NULL, muzzle, NULL, Q2_MASK_SHOT_GEN);
	if (tr.fraction < 1.0f || tr.startsolid)
		return false;
	VectorMA(muzzle, HOOK_REACH, forward, shot_end);
	tr = sg_host.trace(muzzle, NULL, NULL, shot_end, NULL,
	                   Q2_MASK_SHOT_GEN);
	if (tr.startsolid || tr.fraction >= 1.0f || tr.ent != g_edicts ||
	    (tr.surface && (tr.surface->flags & SURF_SKY)))
		return false;

	VectorCopy(tr.endpos, bite);
	VectorSubtract(bite, muzzle, want);
	control_out[PITCH] = view_angles[PITCH];
	control_out[YAW] = view_angles[YAW];
	control_out[ROLL] = DotProduct(want, forward);
	if (control_out[ROLL] < 1.0f ||
	    control_out[ROLL] > RUNE_HOOK_MAX_RAY)
		return false;
	VectorMA(muzzle, control_out[ROLL], forward, shot_end);
	VectorSubtract(bite, shot_end, want);
	if (VectorLength(want) > 0.25f)
		return false;
	VectorCopy(shot_end, bite);
	if (CTF_HookPullVelocity(muzzle, bite, want) < 150 ||
	    !SG_OracleHookFlightClear(muzzle, bite))
		return false;

	flight_ms = (int)ceilf(control_out[ROLL] /
	                          RUNE_HOOK_FRAME_DISTANCE) * 100;
	if (!SG_OracleHookTraverse(&ph, bite, gen_seeds[to].origin,
	        view_angles, RIGHT_HANDED, flight_ms, RUNE_HOOK_DRY_SETTLE_MS,
	        0.0f, &proof, NULL, true))
		return false;
	if (flight_ms + proof.pull_ms + proof.settle_ms > 32767)
		return false;
	*cost_ms = (short)(flight_ms + proof.pull_ms + proof.settle_ms);
	*exit_speed = proof.exit_speed;
	return true;
}

static qboolean ProveHook(int from, int to, vec3_t control_out,
                          short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	usercmd_t source_cmd;
	vec3_t source, fire_source, up, aim, bite, view_angles, forward, right,
	       muzzle, want;
	sg_hook_proof_t proof;
	trace_t tr;
	int flight_ms, source_step, open_overhead = 0;
	qboolean source_water = (gen_seeds[from].flags & RSF_WATER) != 0;

	/* Dry hooks launch from the exact maintainable rest state. A submerged
	 * source has no such fixed point: zero input sinks and currents may move it.
	 * Its offline rollout is therefore only a nominal route witness; runtime
	 * re-proves the actual fixed-point body immediately before firing. Keep this
	 * special case to upward water-to-dry exits, where swimming alone cannot
	 * restore the objective route. */
	if ((!source_water && (gen_source_waterlevel[from] != 0 ||
	                       !gen_source_stable[from])) ||
	    (source_water && (gen_source_waterlevel[from] < 2 ||
	                      (gen_seeds[to].flags & RSF_WATER))))
		return false;

	/*
	 * Anchor candidates. Straight above the target fails the commonest
	 * climb in the game: from below a plateau, the rope line to a point
	 * over the plateau clips the plateau's own lip. A player hooks the
	 * ceiling over the APPROACH -- above the lip, above the gap between
	 * here and there -- so candidates walk back from the target toward
	 * the source. First one with both a surface overhead and a clear
	 * rope line from the source's eyes wins.
	 */
	source[0] = (short)(gen_seeds[from].origin[0] * 8.0f) * 0.125f;
	source[1] = (short)(gen_seeds[from].origin[1] * 8.0f) * 0.125f;
	source[2] = (short)(gen_seeds[from].origin[2] * 8.0f) * 0.125f;
	{
		static const float backs[4] = { 0.0f, 0.35f, 0.6f, 0.85f };
		int bi;

		/* A clear ceiling ray is only a candidate, not a traversal proof. The
		 * former loop stopped at the first clear bite and returned failure if
		 * that rope missed the destination, never trying the other three valid
		 * approach ceilings. That turned whole balconies into one-way graph
		 * islands. Run the complete shared traversal for every candidate and
		 * commit the first one that actually arrives. */
		for (bi = 0; bi < 4; bi++)
		{
			vec3_t shot_end, miss, to_aim;
			trace_t muzzle_tr;
			float shot_len;

			VectorCopy(gen_seeds[to].origin, up);
			up[0] += (gen_seeds[from].origin[0] - up[0]) * backs[bi];
			up[1] += (gen_seeds[from].origin[1] - up[1]) * backs[bi];
			up[2] = (backs[bi] > 0.0f &&
			         gen_seeds[from].origin[2] < gen_seeds[to].origin[2])
			            ? gen_seeds[to].origin[2] : up[2];
			up[2] += 24.0f;
			VectorCopy(up, aim);
			aim[2] += 512.0f;
			tr = sg_host.trace(up, NULL, NULL, aim, NULL, MASK_PLAYERSOLID);
			if (tr.fraction >= 1.0f || tr.startsolid)
			{
				if (tr.fraction >= 1.0f && !tr.startsolid)
					open_overhead++;
				continue;
			}
			if (tr.surface && (tr.surface->flags & SURF_SKY))
			{
				open_overhead++;
				continue;
			}
			VectorCopy(tr.endpos, aim);
			aim[2] -= 4.0f;
			if (!SG_HookAimAngles(source, 22.0f, aim, view_angles))
				continue;
			/* Pmove clamps the view before Weapon_Hook_Fire observes it. A
			 * serialized control must already be in that reachable domain. */
			if (view_angles[PITCH] < -89.0f || view_angles[PITCH] > 89.0f)
				continue;
			SG_OraclePlace(&ph, gen_seeds[from].origin);
			if (source_water)
			{
				/* Runtime arms the view, then spends one complete zero-input aim
				 * frame before its exact online proof and fire. Water is not a rest
				 * state, so the nominal witness must consume that same drift first. */
				memset(&source_cmd, 0, sizeof(source_cmd));
				source_cmd.msec = 0;
				if (!SG_OracleRunWorld(&ph, &source_cmd, 1))
					continue;
				for (source_step = 0; source_step < 4; source_step++)
				{
					memset(&source_cmd, 0, sizeof(source_cmd));
					source_cmd.msec = 25;
					source_cmd.angles[PITCH] = ANGLE2SHORT(view_angles[PITCH]) -
					                                 ph.pms.delta_angles[PITCH];
					source_cmd.angles[YAW] = ANGLE2SHORT(view_angles[YAW]) -
					                               ph.pms.delta_angles[YAW];
					source_cmd.angles[ROLL] = -ph.pms.delta_angles[ROLL];
					if (!SG_OracleRunWorld(&ph, &source_cmd, 1))
						break;
				}
				if (source_step != 4 || ph.waterlevel < 2 ||
				    !(ph.watertype & CONTENTS_WATER) ||
				    (ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
					continue;
				/* Runtime re-proves only while the post-aim body still belongs to
				 * this source cell. Apply that same ownership gate to the nominal
				 * witness so objective connectivity cannot depend on a current that
				 * drifts outside the executable launch envelope in the aim frame. */
				VectorSubtract(ph.origin, gen_seeds[from].origin, miss);
				if (miss[0] * miss[0] + miss[1] * miss[1] > 20.0f * 20.0f ||
				    fabsf(miss[2]) > 16.0f)
					continue;
				VectorCopy(ph.origin, fire_source);
			}
			else
				VectorCopy(source, fire_source);
			AngleVectors(view_angles, forward, right, NULL);
			CTF_HookMuzzle(fire_source, 22.0f, RIGHT_HANDED,
			               forward, right, muzzle);
			VectorNormalize(forward); /* fire_hook's actual bolt direction */
			muzzle_tr = sg_host.trace(fire_source, NULL, NULL, muzzle, NULL,
			                             Q2_MASK_SHOT_GEN);
			if (muzzle_tr.fraction < 1.0f || muzzle_tr.startsolid)
				continue;
			VectorSubtract(aim, muzzle, to_aim);
			shot_len = VectorLength(to_aim) + 96.0f;
			VectorMA(muzzle, shot_len, forward, shot_end);
			tr = sg_host.trace(muzzle, NULL, NULL, shot_end, NULL,
			                   Q2_MASK_SHOT_GEN);
			VectorSubtract(tr.endpos, aim, miss);
			if (tr.startsolid || tr.fraction >= 1.0f || tr.ent != g_edicts ||
			    (tr.surface && (tr.surface->flags & SURF_SKY)) ||
			    VectorLength(miss) > 48.0f)
				continue;
			VectorCopy(tr.endpos, bite);
			VectorSubtract(bite, muzzle, want);
			control_out[PITCH] = view_angles[PITCH];
			control_out[YAW] = view_angles[YAW];
			/* Store the signed ray parameter, then prove the point the file can
			 * reproduce rather than an unencoded perpendicular residue. */
			control_out[ROLL] = DotProduct(want, forward);
			if (control_out[ROLL] < 1.0f ||
			    control_out[ROLL] > RUNE_HOOK_MAX_RAY)
				continue;
			VectorMA(muzzle, control_out[ROLL], forward, aim);
			VectorSubtract(bite, aim, want);
			if (VectorLength(want) > 0.25f)
				continue;
			VectorCopy(aim, bite);
			if (CTF_HookPullVelocity(muzzle, bite, want) < 150 ||
			    !SG_OracleHookFlightClear(muzzle, bite))
				continue;
			/* Bolt movement is quantized in later 80-unit server frames. */
			flight_ms = (int)ceilf(control_out[ROLL] /
			                          RUNE_HOOK_FRAME_DISTANCE) * 100;
			if (!SG_OracleHookTraverse(&ph, bite, gen_seeds[to].origin,
			        view_angles, RIGHT_HANDED, flight_ms,
			        source_water ? RUNE_HOOK_WATER_SETTLE_MS
			                     : RUNE_HOOK_DRY_SETTLE_MS,
			        0.0f, &proof, NULL, true))
				continue;
			if (flight_ms + proof.pull_ms + proof.settle_ms > 32767)
				continue;
			*cost_ms = (short)(flight_ms + proof.pull_ms + proof.settle_ms);
			*exit_speed = proof.exit_speed;
			return true;
		}
	}

	/* Only the all-open-sky, upward dry case gets the lateral fan. Its aim
	 * baseline is geometrical: source eye to 24 units over the destination
	 * seed. Ten degrees shallower exposes a vertical lip; the two yaw
	 * magnitudes are paired +/- so map orientation and handed side cannot
	 * select a privileged direction. */
	if (!source_water && open_overhead == 4 &&
	    gen_seeds[to].origin[2] > gen_seeds[from].origin[2])
	{
		static const float yaw_offsets[4] = {
			15.0f, -15.0f, 60.0f, -60.0f
		};
		vec3_t d;
		float horiz, base_yaw, base_pitch;
		int yi;

		VectorSubtract(gen_seeds[to].origin, gen_seeds[from].origin, d);
		horiz = sqrtf(d[0] * d[0] + d[1] * d[1]);
		/* This is a lip-climb fallback, not a second general hook prover.
		 * Keep it inside one compact lattice neighbourhood and below one
		 * ordinary player-height rise. */
		if (horiz > 2.0f * SEED_SPACING || d[2] < 32.0f || d[2] > 96.0f)
			return false;
		base_yaw = atan2f(d[1], d[0]) * 180.0f / (float)M_PI;
		base_pitch = -atan2f(d[2] + 2.0f, horiz) *
		             180.0f / (float)M_PI;
		for (yi = 0; yi < 4; yi++)
			if (ProveHookLateralCandidate(from, to,
			        base_pitch + 10.0f, base_yaw + yaw_offsets[yi],
			        control_out, cost_ms, exit_speed))
				return true;
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
 * The drop prover publishes the walkoff heading for the runtime controller.
 * That heading is not an entry condition: ProveDrop starts at rest on the
 * source seed and builds all of its own approach speed. Momentum jumps still
 * carry a real source-entry cone.
 */
static byte dd_last_heading;            /* lip direction of the last proven drop */

typedef struct sg_drop_trial_s
{
	qboolean	ran;
	qboolean	fenced;
	qboolean	crossed;
	int		landed;
	sg_phantom_t	end;
} sg_drop_trial_t;

/* The point probe only proposes a lip. The player-sized rollout below is the
 * authority on whether that proposal is executable. */
static qboolean Drop_FindLip(vec3_t src, vec3_t dir, float limit, vec3_t lip)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t probe, down, last;
	trace_t tr;
	float walked;
	qboolean have_last = false;

	/* Walk player-centre positions in small fixed increments. The former point
	 * ray could nominate a lip beyond a railing/edge that a 32-unit hull never
	 * reached, then the exact rollout stalled forever a hull radius short. */
	/* Do not serialize a lip exactly on the loader's 2-unit lower bound.
	 * The normalized heading and world coordinates are floats, so a nominal
	 * two-unit probe can round to 1.99998 after subtraction and make the
	 * generator write a rune its own loader rejects. Four units still samples
	 * well inside one 25 ms approach step and leaves a real format margin. */
	for (walked = 4.0f; walked <= limit; walked += 2.0f)
	{
		probe[0] = src[0] + dir[0] * walked;
		probe[1] = src[1] + dir[1] * walked;
		probe[2] = src[2] + 1.0f;
		tr = sg_host.trace(src, mins, maxs, probe, NULL, MASK_PLAYERSOLID);
		if (tr.startsolid || tr.allsolid || tr.fraction < 1.0f)
			break;
		VectorCopy(probe, down);
		down[2] -= 80.0f;
		tr = sg_host.trace(probe, mins, maxs, down, NULL, MASK_PLAYERSOLID);
		if (tr.fraction >= 1.0f)
		{
			if (!have_last)
				return false;
			VectorCopy(last, lip);
			lip[2] = src[2] + 8.0f;
			return true;
		}
		VectorCopy(probe, last);
		have_last = true;
	}
	return false;
}

static void Drop_ReplayPose(const sg_phantom_t *ph, sg_replay_pose_t *pose)
{
	memset(pose, 0, sizeof(*pose));
	pose->pms = ph->pms;
	VectorCopy(ph->origin, pose->origin);
	VectorCopy(ph->velocity, pose->velocity);
	pose->grounded = ph->groundentity;
	pose->watertype = ph->watertype;
	pose->waterlevel = ph->waterlevel;
}

static qboolean Drop_ReplayHarmfulLiquid(const sg_phantom_t *ph)
{
	return ph->waterlevel > 0 &&
	       (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
}

/* Resolve only the contact traces that legacy Drop_Rollout would reach at
 * this production boundary.  Arrival and recovery intentionally may issue
 * two identical traces when the first contact result is false; preserving
 * that short-circuit cadence is part of a behavior-neutral adapter. */
static qboolean Drop_ReplayContact(const sg_drop_replay_state_t *state,
	const sg_phantom_t *ph, vec3_t destination)
{
	vec3_t delta;
	float horizontal2;
	int next_ms;
	qboolean airborne_after, arrival_gate, recovery_gate;
	qboolean contact = true;

	next_ms = state->progress.elapsed_ms + SG_REPLAY_STEP_MS;
	airborne_after = state->airborne ||
		(state->walkoff && !ph->groundentity);
	if (ph->door_passed ||
	    (state->recovery && !ph->groundentity) ||
	    (state->spec.destination_water && airborne_after &&
	     (next_ms % SG_REPLAY_FRAME_MS) == 0 &&
	     ph->waterlevel > 0 && ph->waterlevel < 3) ||
	    ph->origin[2] < destination[2] - SG_REPLAY_DROP_BELOW_Z ||
	    next_ms >= SG_REPLAY_DROP_TOTAL_MS ||
	    (next_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    Drop_ReplayHarmfulLiquid(ph))
		return true;

	VectorSubtract(destination, ph->origin, delta);
	horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
	arrival_gate = state->walkoff &&
		horizontal2 < SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
		delta[2] > -SG_REPLAY_ARRIVE_Z &&
		delta[2] < SG_REPLAY_ARRIVE_Z &&
		(state->spec.destination_water ? ph->waterlevel == 3 :
		 (ph->groundentity || ph->waterlevel >= 2));
	if (arrival_gate)
	{
		contact = Prove_Contact(ph->origin, destination);
		if (contact)
			return true;
	}
	recovery_gate = !state->recovery && state->walkoff && airborne_after &&
		!state->spec.destination_water && ph->groundentity &&
		ph->waterlevel == 0 &&
		horizontal2 < SG_RUNE_PROOF_DROP_RECOVERY_RADIUS *
		                  SG_RUNE_PROOF_DROP_RECOVERY_RADIUS &&
		delta[2] > -SG_RUNE_PROOF_DROP_RECOVERY_Z &&
		delta[2] < SG_RUNE_PROOF_DROP_RECOVERY_Z;
	if (recovery_gate)
		contact = Prove_Contact(ph->origin, destination);
	return contact;
}

static void Drop_ReplayObservation(const sg_phantom_t *ph,
	qboolean destination_water, qboolean contact_clear,
	sg_replay_observation_t *observation)
{
	memset(observation, 0, sizeof(*observation));
	observation->contact_clear = contact_clear;
	/* SG_OracleRunWorld already rejects non-world support. */
	observation->ground_support_valid = true;
	observation->drop_recovery_admitted = !destination_water;
	observation->drop_landing_observed =
		!destination_water && ph->groundentity;
	observation->door_passed = ph->door_passed;
}

/* One exact source-to-lip-to-destination attempt. All bookkeeping is local:
 * ProveDrop commits only the candidate that wins, or one final failure record
 * when none wins, so an earlier compass miss cannot leak into a later link. */
static qboolean Drop_Rollout(vec3_t src, vec3_t dst, vec3_t lip, byte heading,
                             qboolean require_deep_water,
                             short *cost_ms, byte *exit_speed,
                             sg_drop_trial_t *trial)
{
	sg_phantom_t ph;
	sg_drop_replay_spec_t spec;
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	usercmd_t cmd;

	memset(trial, 0, sizeof(*trial));
	trial->ran = true;
	SG_OraclePlace(&ph, src);
	memset(&spec, 0, sizeof(spec));
	VectorCopy(dst, spec.destination);
	VectorCopy(lip, spec.lip);
	spec.heading = heading;
	spec.destination_water = require_deep_water;
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	Drop_ReplayPose(&ph, &pose);
	Drop_ReplayObservation(&ph, require_deep_water, true, &observation);
	status = SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f);
	while (status == SG_REPLAY_RUNNING)
	{
		status = SG_DropReplayPreStep(&state, &pose, &cmd);
		if (state.walkoff)
			trial->crossed = true;
		if (status != SG_REPLAY_RUNNING)
			break;
		if (!SG_OracleRunWorld(&ph, &cmd, 1))
			break;
		Drop_ReplayPose(&ph, &pose);
		Drop_ReplayObservation(&ph, require_deep_water,
			Drop_ReplayContact(&state, &ph, dst), &observation);
		status = SG_DropReplayPostStep(&state, &pose, &observation);
		if (ph.groundentity &&
		    state.progress.reason != SG_REPLAY_REASON_DOOR_PASSED &&
		    state.progress.reason != SG_REPLAY_REASON_RECOVERY_LOST &&
		    state.progress.reason != SG_REPLAY_REASON_SHALLOW_WATER_CONTACT &&
		    state.progress.reason != SG_REPLAY_REASON_BELOW_DESTINATION)
			trial->landed++;
	}
	if (state.progress.reason == SG_REPLAY_REASON_APPROACH_TIMEOUT)
		trial->fenced = true;
	trial->end = ph;
	if (status != SG_REPLAY_ARRIVED)
		return false;
	*cost_ms = (short)state.progress.arrival_ms;
	*exit_speed = state.progress.exit_speed;
	return true;
}

/*
 * Prove the whole drop, not just the ballistic suffix. The lip is discovered
 * geometrically, but one phantom then owns the complete state history: it is
 * placed once at rest on the source seed, walks toward the lip until the
 * runtime's eight-unit handoff, and holds the serialized walkoff heading until
 * it lands. No teleport and no injected velocity may bridge those phases.
 * The landing is judged by the same arrival-and-contact test as every other
 * link, and the elapsed Pmove time is the link's cost.
 */
static int dd_nolip, dd_fenced, dd_flew, dd_landed, dd_won;

static qboolean ProveDrop(int from, int to, vec3_t lip_out,
                          short *cost_ms, byte *exit_speed)
{
	vec3_t src, dst, dir, lip;
	float horiz, limit;
	int e8, tries, candidates = 0;
	short trial_cost = 0;
	byte trial_exit = 0, trial_heading = 0;
	qboolean direct;
	sg_drop_trial_t trial, last_trial;

	VectorCopy(gen_seeds[from].origin, src);
	VectorCopy(gen_seeds[to].origin, dst);
	if ((gen_seeds[from].flags & RSF_WATER) || !gen_source_stable[from])
		return false;       /* water exits belong to the dedicated swim pass */
	if ((gen_seeds[to].flags & RSF_WATER) &&
	    (sg_host.pointcontents(dst) & (CONTENTS_SLIME | CONTENTS_LAVA)))
		return false;       /* liquid movement is shared; survival is not */
	dir[0] = dst[0] - src[0];
	dir[1] = dst[1] - src[1];
	dir[2] = 0.0f;
	horiz = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
	direct = (horiz < 1.0f);
	tries = direct ? 8 : 1;
	if (!direct)
	{
		dir[0] /= horiz;
		dir[1] /= horiz;
	}
	memset(&last_trial, 0, sizeof(last_trial));

	for (e8 = 0; e8 < tries; e8++)
	{
		/* A directly-below destination has no preferred edge. Every compass
		 * direction with a geometrically visible lip earns its own complete
		 * rollout; the first proof, not the first point trace, wins. */
		if (direct)
		{
			dir[0] = cosf(e8 * (float)(M_PI / 4.0));
			dir[1] = sinf(e8 * (float)(M_PI / 4.0));
			limit = 192.0f;
		}
		else
		{
			limit = horiz + 64.0f;
			if (limit > 256.0f)
				limit = 256.0f;
		}
		if (!Drop_FindLip(src, dir, limit, lip))
			continue;

		candidates++;
		trial_heading = Heading_Quantize(dir[0], dir[1]);
		if (Drop_Rollout(src, dst, lip, trial_heading,
		                 (gen_seeds[to].flags & RSF_WATER) ? true : false,
		                 &trial_cost, &trial_exit, &trial))
		{
			dd_last_heading = trial_heading;
			dd_flew += trial.crossed ? 1 : 0;
			dd_landed += trial.landed;
			dd_won++;
			*cost_ms = trial_cost;
			*exit_speed = trial_exit;
			VectorCopy(lip, lip_out);
			return true;
		}
		last_trial = trial;
	}

	if (candidates == 0)
	{
		dd_nolip++;
		return false;
	}

	/* No candidate won. Preserve one pair-level failure record, just as the
	 * old single-candidate prover did; earlier compass misses remain private. */
	dd_fenced += last_trial.fenced ? 1 : 0;
	dd_flew += last_trial.crossed ? 1 : 0;
	dd_landed += last_trial.landed;
	return false;
}

static qboolean Link_Add(int from, int to, rune_action_t act,
                         short cost_ms, byte exit_speed)
{
	rune_link_t *l;

	if (cost_ms <= 0)
		return false;
	if (gen_num_links >= LINK_MAX)
	{
		gen_link_overflow = true;
		return false;
	}
	l = &gen_links[gen_num_links++];
	memset(l, 0, sizeof(*l));
	l->from = from;
	l->to = to;
	l->action = (byte)act;
	l->provenance = RL_PROVEN;
	l->cost_ms = cost_ms;
	l->exit_speed = exit_speed;
	l->heading_slack = 255;     /* run links: any approach heading works */
	return true;
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
#define SG_PAD_REACH		RUNE_TELEPORT_SEED_REACH

static int gen_first_water = -1;        /* index of the first water seed, -1 none */
static int gen_num_water;
static int gen_lift_links, gen_tele_links, gen_door_links, gen_swim_links;
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
	/*
	 * The action heading is the controller's serialized walkoff direction, not
	 * a condition on entry to the source seed. The continuous proof begins at
	 * rest and builds its own speed while approaching the lip, so it requires
	 * neither incoming speed nor a particular incoming heading.
	 */
	l->heading_slack = RUNE_DROP_CONTROL_MARKER;
	l->min_speed = 0;
	gen_env_drop++;
}

static void Link_Env_Hook(rune_link_t *l, const vec3_t control)
{
	l->heading = Heading_Quantize(cosf(control[YAW] * (float)M_PI / 180.0f),
	                              sinf(control[YAW] * (float)M_PI / 180.0f));
	l->heading_slack = (gen_seeds[l->from].flags & RSF_WATER)
	                   ? RUNE_WATER_HOOK_CONTROL_MARKER
	                   : RUNE_HOOK_CONTROL_SLACK;
	/*
	 * A dry proof stands still. A water proof includes nominal zero-input drift
	 * and is re-proved from the actual live entry state before firing. The rope
	 * SETS velocity (p_weapon.c), so neither record claims a minimum entry speed.
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
		    gen_links[i].action != RL_TELEPORT &&
		    gen_links[i].action != RL_DOOR)
			continue;               /* a drop proven by the lift pass stays PROVEN */
		gen_links[i].provenance = RL_DECLARED;
		gen_links[i].heading_slack = RUNE_DECLARED_CONTROL_MARKER;
		gen_declared_links++;
	}
}

/* Add a seed that lives inside water. RSF_WATER is an authoritative player-
 * body classification made by Seed_Add, never a label this caller may force. */
static int Seed_AddWater(vec3_t origin)
{
	int before = gen_num_seeds;

	Seed_Add(origin);
	if (gen_num_seeds == before)
		return -1;
	if (!(gen_seeds[before].flags & RSF_WATER))
	{
		int key = Seed_HashKey(origin);

		/* Seed_Add inserts at the head. Roll back the whole append if the
		 * exact body categorization disagrees with the water-volume proposal. */
		if (hash_head[key] == before)
			hash_head[key] = hash_next[before];
		gen_num_seeds = before;
		return -1;
	}
	gen_num_water++;
	return before;
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
	sg_phantom_t ph;
	usercmd_t cmd;
	trace_t tr;
	int contents;

	if (!Seed_Representable(p))
		return false;
	contents = sg_host.pointcontents(p);

	if (!(contents & CONTENTS_WATER) ||
	    (contents & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return false;

	VectorCopy(p, start);
	VectorCopy(p, end);
	end[2] -= 1.0f;
	tr = sg_host.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
	if (tr.startsolid || tr.allsolid)
		return false;

	/* A center point being wet is not enough: just below the surface a
	 * standing player can still be waterlevel 1 and Pmove will use dry motion.
	 * Categorize the exact player body and admit only a real swimming state in
	 * ordinary water. */
	SG_OraclePlace(&ph, p);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	if (!SG_OracleRunWorld(&ph, &cmd, 1))
		return false;
	return ph.waterlevel >= 2 && (ph.watertype & CONTENTS_WATER) &&
	       !(ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
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
	int i, k, z, entries = 0, existing = 0;

	for (i = 0; i < dry; i++)
		if (gen_seeds[i].flags & RSF_WATER)
			existing++;
	gen_num_water = existing;

	for (i = 0; i < dry; i++)
	{
		if (gen_num_water >= SG_WATER_MAX)
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

	if (gen_num_seeds == dry && existing == 0)
	{
		sg_host.dprint("rune: no water adjacent to any seed\n");
		return;
	}

	/* Some ordinary ground seeds may already be physically submerged. They
	 * are water-flood frontiers too, not merely labels: otherwise a narrow or
	 * deep pool can retain one incoming-only floor seed and no swim volume. */
	gen_first_water = 0;
	for (i = 0; i < gen_num_seeds && gen_num_water < SG_WATER_MAX; i++)
	{
		vec3_t here;

		if (!(gen_seeds[i].flags & RSF_WATER))
			continue;
		VectorCopy(gen_seeds[i].origin, here);
		Seed_WaterNeighbours(here);
	}
	if (gen_num_water >= SG_WATER_MAX)
	{
		gen_water_overflow = true;
		sg_host.dprint("rune: water seed cap %d reached; graph will not be written\n",
		               SG_WATER_MAX);
	}

	sg_host.dprint("rune: %d water seeds (%d entered from dry land)\n",
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

	sw_first = sg_host.game_alloc(sizeof(int) * (gen_num_seeds > 0 ? gen_num_seeds : 1));
	sw_next = sg_host.game_alloc(sizeof(int) * (gen_num_links > 0 ? gen_num_links : 1));
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

/* One controller owns every ordinary shore/water traversal.  It feeds the
 * same exact, quantized 25 ms feedback command used by Think_Emit and judges
 * arrival only at the 100 ms boundary where runtime can retire a commitment.
 * This is deliberately separate from Prove(): a RUN feeler, a vertical
 * upmove heuristic, or an action-byte relabel is not evidence for RL_SWIM. */
static qboolean ProveSwim(int from, int to, short *cost_ms, byte *exit_speed)
{
	sg_phantom_t ph;
	sg_swim_proof_t proof;
	qboolean target_water = (gen_seeds[to].flags & RSF_WATER) != 0;

	if (!((gen_seeds[from].flags | gen_seeds[to].flags) & RSF_WATER))
		return false;
	SG_OraclePlace(&ph, gen_seeds[from].origin);
	/* Categorize the exact placed body without advancing it. */
	{
		usercmd_t cmd;

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 0;
		SG_OracleRun(&ph, &cmd, 1);
	}
	if ((gen_seeds[from].flags & RSF_WATER) ? ph.waterlevel < 2
	                                      : ph.waterlevel >= 2)
		return false;
	if (!SG_OracleSwimTraverse(&ph, gen_seeds[to].origin, target_water,
	                           0.0f, &proof, NULL, true))
		return false;
	*cost_ms = (short)proof.arrival_ms;
	*exit_speed = proof.exit_speed;
	return true;
}

static void Prove_Swim_Pair(int from, int to)
{
	short cost;
	byte espeed;
	int have = Link_Index_Find(from, to);

	/* DROP and HOOK have their own complete proofs and controllers.  Ordinary
	 * direct pairs are absent from the dry pass and must earn a SWIM record
	 * here; no existing record is ever converted by changing its action byte. */
	if (have >= 0)
		return;
	if (ProveSwim(from, to, &cost, &espeed))
	{
		if (Link_Add(from, to, RL_SWIM, cost, espeed))
		{
			/* Zero is the v2 exact-controller marker in this otherwise unused
			 * field. Old RUN/JUMP records relabelled SWIM retain 255 and are
			 * rejected by both Rune_Load and runelint. */
			gen_links[gen_num_links - 1].heading_slack = 0;
			gen_swim_links++;
		}
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

	for (i = 0; i < gen_num_seeds; i++)
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

	sg_host.game_free(sw_first);
	sg_host.game_free(sw_next);
	sw_first = NULL;
	sw_next = NULL;

	sg_host.dprint("rune: %d exact swim links proven\n", gen_swim_links);
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

static qboolean Gen_SeedHasIncoming(int seed)
{
	int i;

	for (i = 0; i < gen_num_links; i++)
		if (gen_links[i].cost_ms > 0 && gen_links[i].to == seed)
			return true;
	return false;
}

static qboolean Gen_SeedHasOutgoing(int seed)
{
	int i;

	for (i = 0; i < gen_num_links; i++)
		if (gen_links[i].cost_ms > 0 && gen_links[i].from == seed)
			return true;
	return false;
}

/* Select a graph-connected static endpoint for a declared mechanism.  A
 * Euclidean nearest seed is insufficient: it can be on the far side of a
 * wall, on the mover itself, or an isolated germ.  Trace the complete player
 * hull from the candidate to the authoritative body point and accept an early
 * hit only when it is the expected pad/platform -- the declared controller
 * owns that final contact.  Connectivity requirements are evaluated against
 * the already-proven ordinary/swim graph. */
static int Gen_MechanismSeedNear(vec3_t body, float horiz, float vert,
	edict_t *expected, qboolean require_stable, qboolean require_dry,
	qboolean require_incoming, qboolean require_outgoing, int action,
	int *approach_ms)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < gen_num_seeds; i++)
	{
		vec3_t d;
		vec3_t start, end;
		float h2, d3;
		trace_t tr;

		int trial_ms = 0;

		if ((require_stable && !gen_source_stable[i]) ||
		    (require_dry && gen_source_waterlevel[i] != 0) ||
		    (require_incoming && !Gen_SeedHasIncoming(i)) ||
		    (require_outgoing && !Gen_SeedHasOutgoing(i)))
			continue;
		VectorSubtract(gen_seeds[i].origin, body, d);
		/* The declared runtime controller is intentionally planar: it walks into
		 * a pad/plat or off the top, but it neither jumps nor swims vertically.
		 * Restrict endpoints to one Pmove step-height band so the swept hull is not
		 * mistaken for proof that a body 64--128 units above/below can reach it. */
		if (fabsf(d[2]) > 16.0f)
			continue;
		if (fabsf(d[2]) > vert)
			continue;
		h2 = d[0] * d[0] + d[1] * d[1];
		if (h2 > horiz * horiz)
			continue;
		VectorCopy(gen_seeds[i].origin, start);
		VectorCopy(body, end);
		/* Clear resting-plane epsilon without changing the XY route. */
		start[2] += 1.0f;
		end[2] += 1.0f;
		tr = sg_host.trace(start, mins, maxs, end,
		                   NULL, MASK_PLAYERSOLID);
		if (tr.startsolid || tr.allsolid ||
		    (tr.fraction < 1.0f && tr.ent != expected))
			continue;
		if (approach_ms && !SG_OracleDeclaredApproach(
		        gen_seeds[i].origin, body, expected, action, &trial_ms))
			continue;
		d3 = h2 + d[2] * d[2];
		if (d3 < bestd)
		{
			bestd = d3;
			best = i;
			if (approach_ms)
				*approach_ms = trial_ms;
		}
	}
	return best;
}

/* A water source cannot be staged at rest. Prove the nominal affordance with
 * the same swim controller runtime will re-prove from its actual state, and
 * choose only a graph-reachable submerged seed. */
static int Gen_TeleportWaterSeed(vec3_t pad_body, edict_t *pad,
	int *approach_ms)
{
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < gen_num_seeds; i++)
	{
		sg_phantom_t ph;
		sg_swim_proof_t proof;
		vec3_t d;
		vec3_t anchor_delta;
		float d2;

		if (!(gen_seeds[i].flags & RSF_WATER) ||
		    !Gen_SeedHasIncoming(i))
			continue;
		VectorSubtract(gen_seeds[i].origin, pad_body, d);
		d2 = DotProduct(d, d);
		if (d2 > RUNE_TELEPORT_SEED_REACH * RUNE_TELEPORT_SEED_REACH)
			continue;
		/* The record identifies the raw pad origin, so remain inside the
		 * loader's serialized-anchor envelope as well as the full-3D approach
		 * sphere. The top-body offset can otherwise admit a source that this
		 * generator writes successfully and its own runtime rejects. */
		VectorSubtract(gen_seeds[i].origin, pad->s.origin, anchor_delta);
		if (sqrtf(anchor_delta[0] * anchor_delta[0] +
		          anchor_delta[1] * anchor_delta[1]) >
		        RUNE_TELEPORT_SEED_REACH ||
		    fabsf(anchor_delta[2]) > RUNE_TELEPORT_SEED_REACH)
			continue;
		SG_OraclePlace(&ph, gen_seeds[i].origin);
		if (!SG_OracleTeleportSwimApproach(&ph, pad_body, pad, 0.0f,
		                                     &proof, NULL, true))
			continue;
		if (d2 < bestd)
		{
			bestd = d2;
			best = i;
			*approach_ms = proof.arrival_ms;
		}
	}
	return best;
}

/* Select a static top egress by replaying the same planar controller from the
 * actual raised platform. The caller has synchronously linked `plat` at pos1;
 * every candidate must already own an ordinary outgoing continuation. */
static int Gen_LiftEgressSeed(vec3_t top_body, float horiz, edict_t *plat,
	int *egress_ms)
{
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < gen_num_seeds; i++)
	{
		vec3_t d;
		float h2, d3;
		int trial_ms;

		if (!gen_source_stable[i] || gen_source_waterlevel[i] != 0 ||
		    !Gen_SeedHasOutgoing(i))
			continue;
		VectorSubtract(gen_seeds[i].origin, top_body, d);
		if (fabsf(d[2]) > 16.0f)
			continue;
		h2 = d[0] * d[0] + d[1] * d[1];
		if (h2 > horiz * horiz)
			continue;
		if (!SG_OracleDeclaredEgress(top_body, gen_seeds[i].origin,
		                              plat, &trial_ms))
			continue;
		d3 = h2 + d[2] * d[2];
		if (d3 < bestd)
		{
			bestd = d3;
			best = i;
			*egress_ms = trial_ms;
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
 * A brush model's mins/maxs come from sg_host.setmodel and are relative to the
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
		vec3_t bottom, bottom_body, top_body;
		vec3_t saved_origin, saved_old_origin, saved_velocity;
		float halfx, halfy, horiz;
		int st_top, before, approach = -1;
		int approach_ms = 0, egress_ms = 0, saved_state, saved_linkcount;
		int total_cost;
		short cost;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (strcmp(e->classname, "func_plat") != 0)
			continue;
		/* A targetnamed plat starts disabled at the top. Its center trigger
		 * cannot activate STATE_UP; an unrelated button/relay must call use.
		 * That external prerequisite is not serialized, so no executable
		 * declared link may claim this lift. */
		if (e->targetname)
			continue;
		if (e->pos1[2] - e->pos2[2] < 8.0f)
			continue;                       /* travels nowhere worth a link */

		bottom[0] = e->pos2[0] + (e->mins[0] + e->maxs[0]) * 0.5f;
		bottom[1] = e->pos2[1] + (e->mins[1] + e->maxs[1]) * 0.5f;
		bottom[2] = e->pos2[2] + e->maxs[2];
		halfx = (e->maxs[0] - e->mins[0]) * 0.5f;
		halfy = (e->maxs[1] - e->mins[1]) * 0.5f;
		horiz = sqrtf(halfx * halfx + halfy * halfy) + SEED_SPACING;

		if (!Seed_Ground(bottom, bottom_body))
			continue;
		approach = Gen_MechanismSeedNear(bottom_body, horiz, 64.0f, e,
		    true, true, true, false, RL_LIFT, &approach_ms);
		if (approach < 0)
		{
			sg_host.dprint("rune: plat at (%.0f %.0f %.0f) unlinked, "
			               "no static-world staging seed\n",
			               bottom[0], bottom[1], bottom[2]);
			continue;
		}

		/* Egress is part of the declaration. Raise only this platform for the
		 * synchronous oracle scope, then restore every authoritative field before
		 * generation continues; no server frame runs inside this command. */
		VectorCopy(e->s.origin, saved_origin);
		VectorCopy(e->s.old_origin, saved_old_origin);
		VectorCopy(e->velocity, saved_velocity);
		saved_state = e->moveinfo.state;
		saved_linkcount = e->linkcount;
		VectorCopy(e->pos1, e->s.origin);
		VectorCopy(e->pos1, e->s.old_origin);
		VectorClear(e->velocity);
		e->moveinfo.state = SG_PLAT_STATE_TOP;
		sg_host.linkentity(e);
		st_top = SG_LiftTopRest(e, NULL, top_body)
		    ? Gen_LiftEgressSeed(top_body, horiz, e, &egress_ms) : -1;
		VectorCopy(saved_origin, e->s.origin);
		VectorCopy(saved_old_origin, e->s.old_origin);
		VectorCopy(saved_velocity, e->velocity);
		e->moveinfo.state = saved_state;
		sg_host.linkentity(e);
		/* Linkentity necessarily increments this generation counter. No server
		 * frame ran during the synchronous relocation, so restore the original
		 * value too; otherwise live riders are spuriously detached after `sv rune`. */
		e->linkcount = saved_linkcount;
		if (st_top < 0 || approach == st_top)
		{
			sg_host.dprint("rune: plat at (%.0f %.0f %.0f) unlinked, "
			               "no proved static-world top egress\n",
			               bottom[0], bottom[1], bottom[2]);
			continue;
		}
		/* both ends can only be honest if the pair actually spans the
		 * travel -- otherwise two seeds on the same level got picked */
		if (gen_seeds[st_top].origin[2] - bottom_body[2] <
		        (e->pos1[2] - e->pos2[2]) * 0.5f)
			continue;

		cost = Plat_TravelMs(e);
		total_cost = (int)cost + approach_ms + egress_ms;
		if (total_cost <= 0 || total_cost > 30000)
			continue;
		before = gen_num_links;
		Link_Add(approach, st_top, RL_LIFT, (short)total_cost, 0);
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
		if (Link_Exists(st_top, approach))
			continue;               /* the pair loop already got down there */
		{
			vec3_t lip;
			short dcost;
			byte despeed;

			if (ProveDrop(st_top, approach, lip, &dcost, &despeed))
			{
				int dbefore = gen_num_links;

				Link_Add(st_top, approach, RL_DROP, dcost, despeed);
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
				sg_host.dprint("rune: plat at (%.0f %.0f %.0f) has no proven way "
				           "down (no drop, and a top-parked plat does not "
				           "descend on touch -- g_func.c:429)\n",
				           bottom[0], bottom[1], bottom[2]);
			}
		}
	}
	if (gen_lift_links)
		sg_host.dprint("rune: %d lift links (%d matching drops down, %d with no way down)\n",
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
		vec3_t pad, pad_body, arrive, arrive_body;
		int sd, before, approach = -1, approach_ms = 0;

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

		if (!SG_TeleportApproachPoint(e, pad_body) ||
		    !Seed_Ground(arrive, arrive_body))
			continue;
		/* The exact destination state was inserted before ordinary germs.  The
		 * pad itself intentionally was not: it is trigger-owned, so only the
		 * declared approach may enter it. */
		sd = Gen_SeedNear(arrive_body, 2.0f, 2.0f);
		if (sd < 0)
		{
			sg_host.dprint("rune: teleporter at (%.0f %.0f %.0f) has no "
			               "destination seed\n", pad[0], pad[1], pad[2]);
			continue;
		}
		/* The source must already be reachable by the ordinary/swim graph, and
		 * the destination must itself own an outgoing continuation. Otherwise a
		 * declared shortcut merely joins two graph islands. Underwater pads are
		 * valid; Seed_Add already rejects damaging liquid. */
		if (!Gen_SeedHasOutgoing(sd))
			continue;
		approach = Gen_MechanismSeedNear(pad_body,
		    RUNE_TELEPORT_SEED_REACH, RUNE_TELEPORT_SEED_REACH, e,
		    true, true, true, false, RL_TELEPORT, &approach_ms);
		if (approach < 0)
			approach = Gen_TeleportWaterSeed(pad_body, e, &approach_ms);
		if (approach == sd)
			approach = -1;
		if (approach < 0)
		{
			sg_host.dprint("rune: teleporter at (%.0f %.0f %.0f) has no "
			               "static-world staging seed\n",
			               pad[0], pad[1], pad[2]);
			continue;
		}

		before = gen_num_links;
		/* Walk proof plus the engine's 160 ms PMF_TIME_TELEPORT hold and one
		 * outer-frame observation margin. */
		Link_Add(approach, sd, RL_TELEPORT,
		         (short)(approach_ms + 300), 0);
		if (gen_num_links > before)
		{
			VectorCopy(pad, gen_links[gen_num_links - 1].anchor);
			gen_tele_links++;
		}
	}
	if (gen_tele_links)
		sg_host.dprint("rune: %d teleport links\n", gen_tele_links);
}

typedef struct
{
	edict_t *ent;
	vec3_t origin, old_origin, angles, velocity, avelocity;
	int state, linkcount;
	solid_t solid;
} door_pose_t;

/* Link one canonical door team at its exact STATE_TOP pose for a synchronous
 * egress proof. Rune generation already made the team SOLID_NOT; restoring
 * every authoritative field (including linkcount) makes `sv rune` invisible
 * to a live server once this scope ends. */
static int DoorTrigger_Targets(edict_t *trigger, edict_t **doors, int capacity)
{
	return SG_DeclaredDoorMembers(trigger, doors, capacity);
}

static int DoorTrigger_Open(edict_t *trigger, door_pose_t *saved, int capacity)
{
	edict_t *doors[16];
	int i, n;

	n = DoorTrigger_Targets(trigger, doors, 16);
	if (n <= 0 || n > capacity)
		return -1;
	for (i = 0; i < n; i++)
	{
		edict_t *member = doors[i];

		saved[i].ent = member;
		VectorCopy(member->s.origin, saved[i].origin);
		VectorCopy(member->s.old_origin, saved[i].old_origin);
		VectorCopy(member->s.angles, saved[i].angles);
		VectorCopy(member->velocity, saved[i].velocity);
		VectorCopy(member->avelocity, saved[i].avelocity);
		saved[i].state = member->moveinfo.state;
		saved[i].solid = member->solid;
		saved[i].linkcount = member->linkcount;
		if (!strcmp(member->classname, "func_door_rotating"))
		{
			/* Rotators keep their immutable world pivot and change only angles.
			 * moveinfo.start_origin may be zero on valid maps and is not a pivot. */
			VectorCopy(member->moveinfo.end_angles, member->s.angles);
		}
		else
		{
			VectorCopy(member->moveinfo.end_origin, member->s.origin);
			VectorCopy(member->moveinfo.end_origin, member->s.old_origin);
			VectorCopy(member->moveinfo.end_angles, member->s.angles);
		}
		VectorClear(member->velocity);
		VectorClear(member->avelocity);
		member->moveinfo.state = SG_PLAT_STATE_TOP;
		member->solid = SOLID_BSP;
		sg_host.linkentity(member);
	}
	return n;
}

static void DoorPose_Restore(door_pose_t *saved, int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
		edict_t *member = saved[i].ent;

		VectorCopy(saved[i].origin, member->s.origin);
		VectorCopy(saved[i].old_origin, member->s.old_origin);
		VectorCopy(saved[i].angles, member->s.angles);
		VectorCopy(saved[i].velocity, member->velocity);
		VectorCopy(saved[i].avelocity, member->avelocity);
		member->moveinfo.state = saved[i].state;
		member->solid = saved[i].solid;
		sg_host.linkentity(member);
		member->linkcount = saved[i].linkcount;
	}
}

static int Door_TravelMs(edict_t *trigger)
{
	edict_t *doors[16];
	int i, count;
	int longest = 0;

	if (!trigger)
		return 0;
	count = DoorTrigger_Targets(trigger, doors, 16);
	if (count <= 0)
		return 0;
	for (i = 0; i < count; i++)
	{
		edict_t *member = doors[i];
		int ms;

		if (!isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) || member->moveinfo.speed <= 0.0f)
			return 0;
		ms = (int)ceilf(fabsf(member->moveinfo.distance) /
		                member->moveinfo.speed * 1000.0f) + 200;
		if (ms > longest)
			longest = ms;
	}
	return longest;
}

/* Touch_Multi's cooldown starts with the opening request. Runtime can consume
 * an already-TOP set without owning that request, so the only extra dead time
 * is the interval after the slowest returning member reaches BOTTOM and before
 * the trigger can fire again. Charge that gap; charging the full trigger wait
 * would reject legitimate long-cooldown maps even though their doors remain
 * open for the entire proved egress window. */
static int Door_CooldownGapMs(edict_t *trigger)
{
	edict_t *doors[16];
	int count, i, longest_cycle = 0, cyclic = 0;
	int trigger_ms;

	if (!trigger)
		return -1;
	count = DoorTrigger_Targets(trigger, doors, 16);
	if (count <= 0)
		return -1;
	trigger_ms = SG_DeclaredDoorTriggerWaitMs(trigger);
	if (trigger_ms <= 0)
		return -1;
	for (i = 0; i < count; i++)
	{
		edict_t *member = doors[i];
		int travel, hold, cycle;

		if (!isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) || member->moveinfo.speed <= 0.0f ||
		    !isfinite(member->moveinfo.wait))
			return -1;
		if (member->moveinfo.wait < 0.0f)
			continue;
		cyclic++;
		travel = (int)ceilf(fabsf(member->moveinfo.distance) /
		                       member->moveinfo.speed * 1000.0f) + 200;
		hold = (int)ceilf(member->moveinfo.wait * 1000.0f);
		cycle = 2 * travel + hold;
		if (cycle > longest_cycle)
			longest_cycle = cycle;
	}
	if (!cyclic)
		return 0;
	return trigger_ms > longest_cycle ? trigger_ms - longest_cycle : 0;
}

/* Keep a bounded nearest-first candidate fan. Declared door proof is much
 * more expensive than a point trace (it rolls up to five seconds of Pmove),
 * and an unrestricted wait x source x destination cube made lmctf03 spend
 * minutes re-proving the same open-pose egress for every approach source. */
static void Door_CandidateInsert(int seed, float score, int *seeds,
	float *scores, int capacity)
{
	int i, j;

	for (i = 0; i < capacity; i++)
		if (seed == seeds[i])
			return;
	for (i = 0; i < capacity; i++)
		if (score < scores[i])
		{
			for (j = capacity - 1; j > i; j--)
			{
				seeds[j] = seeds[j - 1];
				scores[j] = scores[j - 1];
			}
			seeds[i] = seed;
			scores[i] = score;
			return;
	}
}

/* Several exact wait points for one mechanism can prove the same graph edge.
 * The runtime needs only one controller for a (from,to,action) triple and the
 * deployment linter deliberately rejects ambiguous duplicates.  Keep the
 * cheapest proved declaration; replacing its cost and anchor is safe because
 * both candidates have independently passed the complete approach, TOP-pose
 * egress, sweep, and open-window contract. */
static qboolean Door_LinkInsert(int from, int to, short cost_ms,
	const vec3_t wait_point)
{
	int i;

	/* A wait point can legitimately select its source seed as the locally
	 * cheapest open-pose destination.  That proves a controller, but it does
	 * not prove a traversal and the v2 loader/linter reject self-links. */
	if (from == to)
		return false;

	for (i = 0; i < gen_num_links; i++)
	{
		rune_link_t *link = &gen_links[i];

		if (link->from != from || link->to != to || link->action != RL_DOOR)
			continue;
		if (cost_ms < link->cost_ms)
		{
			link->cost_ms = cost_ms;
			VectorCopy(wait_point, link->anchor);
		}
		return false;
	}
	if (!Link_Add(from, to, RL_DOOR, cost_ms, 0))
		return false;
	VectorCopy(wait_point, gen_links[gen_num_links - 1].anchor);
	return true;
}

/* Link_Doors runs before objective pruning, when the ordinary, swim, lift,
 * and teleport graph is complete but contains no declared door edges.  Keep a
 * frozen view of that topology so a wait point does not collapse every source
 * onto one locally cheap, same-component egress while discarding an already
 * proved route into a flag component. */
typedef struct
{
	int *component;
	byte *objective_mask;
} door_topology_t;

static int Graph_ObjectiveRoot(const vec3_t objective, const byte *has_out);

static void Door_TopologyFree(door_topology_t *topology)
{
	if (!topology)
		return;
	if (topology->component)
		sg_host.level_free(topology->component);
	if (topology->objective_mask)
		sg_host.level_free(topology->objective_mask);
	topology->component = NULL;
	topology->objective_mask = NULL;
}

static qboolean Door_TopologyBuild(door_topology_t *topology)
{
	int *first_out = NULL, *next_out = NULL;
	int *first_in = NULL, *next_in = NULL;
	int *order = NULL, *stack = NULL, *stack_edge = NULL, *queue = NULL;
	byte *seen = NULL, *has_out = NULL;
	int order_count = 0, component_count = 0;
	int i, start;
	qboolean ok = false;

	if (!topology || gen_num_seeds <= 0)
		return false;
	memset(topology, 0, sizeof(*topology));
	topology->component = sg_host.level_alloc(sizeof(int) *
	    (size_t)gen_num_seeds);
	topology->objective_mask = sg_host.level_alloc((size_t)gen_num_seeds);
	first_out = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	first_in = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	next_out = sg_host.level_alloc(sizeof(int) *
	    (size_t)(gen_num_links ? gen_num_links : 1));
	next_in = sg_host.level_alloc(sizeof(int) *
	    (size_t)(gen_num_links ? gen_num_links : 1));
	order = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	stack = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	stack_edge = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	queue = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	seen = sg_host.level_alloc((size_t)gen_num_seeds);
	has_out = sg_host.level_alloc((size_t)gen_num_seeds);
	if (!topology->component || !topology->objective_mask || !first_out ||
	    !first_in || !next_out || !next_in || !order || !stack ||
	    !stack_edge || !queue || !seen || !has_out)
		goto done;

	memset(topology->objective_mask, 0, (size_t)gen_num_seeds);
	memset(seen, 0, (size_t)gen_num_seeds);
	memset(has_out, 0, (size_t)gen_num_seeds);
	for (i = 0; i < gen_num_seeds; i++)
	{
		topology->component[i] = -1;
		first_out[i] = -1;
		first_in[i] = -1;
	}
	for (i = 0; i < gen_num_links; i++)
	{
		int from = gen_links[i].from;
		int to = gen_links[i].to;

		has_out[from] = 1;
		next_out[i] = first_out[from];
		first_out[from] = i;
		next_in[i] = first_in[to];
		first_in[to] = i;
	}

	/* Iterative Kosaraju first pass.  The format permits 32768 seeds, so avoid
	 * a recursive DFS whose stack depth would depend on map topology. */
	for (start = 0; start < gen_num_seeds; start++)
	{
		int depth;

		if (seen[start])
			continue;
		depth = 1;
		stack[0] = start;
		stack_edge[0] = first_out[start];
		seen[start] = 1;
		while (depth > 0)
		{
			int edge = stack_edge[depth - 1];

			while (edge >= 0 && seen[gen_links[edge].to])
				edge = next_out[edge];
			if (edge < 0)
			{
				order[order_count++] = stack[--depth];
				continue;
			}
			stack_edge[depth - 1] = next_out[edge];
			stack[depth] = gen_links[edge].to;
			stack_edge[depth] = first_out[stack[depth]];
			seen[stack[depth]] = 1;
			depth++;
		}
	}

	for (i = order_count - 1; i >= 0; i--)
	{
		int head = 0, tail = 0;

		start = order[i];
		if (topology->component[start] >= 0)
			continue;
		topology->component[start] = component_count;
		queue[tail++] = start;
		while (head < tail)
		{
			int at = queue[head++];
			int edge;

			for (edge = first_in[at]; edge >= 0; edge = next_in[edge])
			{
				int from = gen_links[edge].from;

				if (topology->component[from] >= 0)
					continue;
				topology->component[from] = component_count;
				queue[tail++] = from;
			}
		}
		component_count++;
	}

	/* A bit means this pre-door seed already has a directed path to that
	 * objective.  Per-source destination selection can then add only missing
	 * reachability, while the final greatest-fixed-point prune remains the
	 * authority on whether the combined declarations actually close the map. */
	if (redflag && blueflag && redflag->inuse && blueflag->inuse)
	{
		int roots[2];
		int which;

		roots[0] = Graph_ObjectiveRoot(redflag->homeposition, has_out);
		roots[1] = Graph_ObjectiveRoot(blueflag->homeposition, has_out);
		for (which = 0; which < 2; which++)
		{
			int head = 0, tail = 0;

			if (roots[which] < 0)
				continue;
			memset(seen, 0, (size_t)gen_num_seeds);
			seen[roots[which]] = 1;
			queue[tail++] = roots[which];
			while (head < tail)
			{
				int at = queue[head++];
				int edge;

				topology->objective_mask[at] |= (byte)(1 << which);
				for (edge = first_in[at]; edge >= 0;
				     edge = next_in[edge])
				{
					int from = gen_links[edge].from;

					if (seen[from])
						continue;
					seen[from] = 1;
					queue[tail++] = from;
				}
			}
		}
	}
	ok = true;

done:
	if (first_out) sg_host.level_free(first_out);
	if (next_out) sg_host.level_free(next_out);
	if (first_in) sg_host.level_free(first_in);
	if (next_in) sg_host.level_free(next_in);
	if (order) sg_host.level_free(order);
	if (stack) sg_host.level_free(stack);
	if (stack_edge) sg_host.level_free(stack_edge);
	if (queue) sg_host.level_free(queue);
	if (seen) sg_host.level_free(seen);
	if (has_out) sg_host.level_free(has_out);
	if (!ok)
		Door_TopologyFree(topology);
	return ok;
}

#define DOOR_WAIT_MAX 64

static void Door_WaitInsert(edict_t *trigger, const vec3_t point,
	vec3_t *points, int *count)
{
	edict_t *resolved;
	vec3_t fixed;
	int i, axis;

	if (!trigger || !point || !points || !count || *count >= DOOR_WAIT_MAX ||
	    !Seed_Representable((vec_t *)point))
		return;
	for (axis = 0; axis < 3; axis++)
		fixed[axis] = (float)(short)(point[axis] * 8.0f) * 0.125f;
	/* Equivalent triggers identify one mover set, but their volumes can sit on
	 * opposite sides of the door.  A wait sampled for this trigger must touch
	 * this exact brush; otherwise the thin-side pass can reuse a broad-side
	 * anchor, emit the already-known direction, and never prove the reverse
	 * crossing. */
	if (!SG_DeclaredDoorTouchMatches(trigger, fixed))
		return;
	resolved = SG_DeclaredDoorForLink(fixed, fixed);
	if (!SG_DeclaredDoorSameSet(resolved, trigger))
		return;
	for (i = 0; i < *count; i++)
	{
		vec3_t delta;

		VectorSubtract(points[i], fixed, delta);
		if (fabsf(delta[2]) <= 2.0f &&
		    delta[0] * delta[0] + delta[1] * delta[1] <= 4.0f)
			return;
	}
	VectorCopy(fixed, points[*count]);
	(*count)++;
}

/* Flood seeds deliberately coalesce points within almost one lattice cell.
 * A thin door trigger can be touched only from a precise body centre flush
 * with the closed leaf, so it may contain no seed even though a real player
 * can stand there. These points are controller anchors, not localization
 * seeds: sample the trigger's player-overlap rectangle, ground each candidate
 * on static geometry, and retain only exact, unambiguous, full-sweep-clear
 * trigger contacts. The approach oracle must still connect an ordinary seed
 * to the point before any link is emitted. */
static int Door_WaitPoints(edict_t *trigger, vec3_t *points)
{
	static const float fractions[5] = {
		0.0f, 0.25f, 0.5f, 0.75f, 1.0f
	};
	float xlo, xhi, ylo, yhi;
	float zprobe[3];
	int count = 0, i, xi, yi, zi;

	if (!trigger || !points)
		return 0;
	for (i = 0; i < gen_num_seeds; i++)
		if (gen_source_waterlevel[i] == 0)
			Door_WaitInsert(trigger, gen_seeds[i].origin, points, &count);

	/* SG_OracleDeclaredTriggerContains uses the linked-player +/-1 fringe.
	 * Remain one fixed-point unit inside that open overlap interval. */
	xlo = trigger->absmin[0] - 17.0f + 0.125f;
	xhi = trigger->absmax[0] + 17.0f - 0.125f;
	ylo = trigger->absmin[1] - 17.0f + 0.125f;
	yhi = trigger->absmax[1] + 17.0f - 0.125f;
	zprobe[0] = trigger->absmin[2] + 25.0f;
	zprobe[1] = 0.5f * (trigger->absmin[2] + trigger->absmax[2]);
	zprobe[2] = trigger->absmax[2] - 33.0f;
	for (zi = 0; zi < 3 && count < DOOR_WAIT_MAX; zi++)
		for (xi = 0; xi < 5 && count < DOOR_WAIT_MAX; xi++)
			for (yi = 0; yi < 5 && count < DOOR_WAIT_MAX; yi++)
			{
				vec3_t candidate, ground;

				candidate[0] = xlo + fractions[xi] * (xhi - xlo);
				candidate[1] = ylo + fractions[yi] * (yhi - ylo);
				candidate[2] = zprobe[zi];
				if (Seed_Ground(candidate, ground))
					Door_WaitInsert(trigger, ground, points, &count);
			}
	return count;
}

/* A door link is deliberately longer than an ordinary local graph edge. Its
 * exact source already overlaps one validated repeatable player trigger and
 * lies outside every pose the whole team can occupy. Runtime touches from
 * rest, waits at that safe point, then runs this same direct controller while
 * the real team is STATE_TOP. The destination must be dry, graph-connected,
 * and outside the complete sweep so ordinary navigation can safely resume. */
static void Link_Doors(void)
{
	door_topology_t topology = { NULL, NULL };
	qboolean have_topology;
	int di;
	int wait_points = 0, approach_trials = 0, egress_trials = 0;

	have_topology = Door_TopologyBuild(&topology);
	if (!have_topology)
		sg_host.dprint("rune: door topology snapshot unavailable; "
		               "using nearest-only egress selection\n");

	for (di = 1; di < globals.num_edicts; di++)
	{
		edict_t *door = &g_edicts[di];
		door_pose_t saved[16];
		vec3_t door_wait[DOOR_WAIT_MAX];
		int wi, num_wait, pose_count, travel_ms, cooldown_gap_ms;

		if (!SG_DeclaredDoorActivatorSafe(door))
			continue;
		travel_ms = Door_TravelMs(door);
		if (travel_ms <= 0 || travel_ms > 12500)
			continue;
		cooldown_gap_ms = Door_CooldownGapMs(door);
		if (cooldown_gap_ms < 0)
			continue;
		pose_count = DoorTrigger_Open(door, saved, 16);
		if (pose_count <= 0)
			continue;
		num_wait = Door_WaitPoints(door, door_wait);

		/* Wait points are geometry samples, not graph sources: ordinary proof
		 * correctly rejects their scripted touch. Prove a continuous approach
		 * from a connected pre-trigger seed and an open-pose egress to a connected
		 * post-door seed. */
		for (wi = 0; wi < num_wait; wi++)
		{
			#define DOOR_SOURCE_FAN 24
			#define DOOR_DEST_FAN 48
			int source, dest, ci;
			int sources[DOOR_SOURCE_FAN], dests[DOOR_DEST_FAN];
			int egress_ms[DOOR_DEST_FAN];
			float source_scores[DOOR_SOURCE_FAN], dest_scores[DOOR_DEST_FAN];
			float egress_scores[DOOR_DEST_FAN];
			byte egress_proved[DOOR_DEST_FAN];
			int best = -1, best_slot = -1;
			float best_score = 1.0e30f;
			vec_t *wait_point = door_wait[wi];

			if (!SG_DeclaredDoorTouchMatches(door, wait_point) ||
			    !SG_DeclaredDoorSameSet(
			        SG_DeclaredDoorForLink(wait_point, wait_point), door))
				continue;
			wait_points++;
			for (ci = 0; ci < DOOR_SOURCE_FAN; ci++)
			{
				sources[ci] = -1;
				source_scores[ci] = 1.0e30f;
			}
			for (ci = 0; ci < DOOR_DEST_FAN; ci++)
			{
				dests[ci] = -1;
				dest_scores[ci] = 1.0e30f;
				egress_ms[ci] = 0;
				egress_scores[ci] = 1.0e30f;
				egress_proved[ci] = 0;
			}
			for (dest = 0; dest < gen_num_seeds; dest++)
			{
				vec3_t delta;
				float h2, score;

				if (!gen_source_stable[dest] ||
				    gen_source_waterlevel[dest] != 0 ||
				    !Gen_SeedHasOutgoing(dest) ||
				    !SG_DeclaredDoorOutsideSweep(door,
				        gen_seeds[dest].origin))
					continue;
				VectorSubtract(gen_seeds[dest].origin, wait_point, delta);
				h2 = delta[0] * delta[0] + delta[1] * delta[1];
				if (h2 > 768.0f * 768.0f || fabsf(delta[2]) > 96.0f ||
				    !SG_DeclaredDoorCrossesSweep(door, wait_point,
				        gen_seeds[dest].origin))
					continue;
				score = h2 + delta[2] * delta[2];
				Door_CandidateInsert(dest, score, dests, dest_scores,
				                     DOOR_DEST_FAN);
			}
			for (ci = 0; ci < DOOR_DEST_FAN && dests[ci] >= 0; ci++)
			{
				vec3_t delta;
				float h2, score;
				int trial_ms;

				dest = dests[ci];
				egress_trials++;
				if (!SG_OracleDeclaredDoorEgress(wait_point,
				        gen_seeds[dest].origin, door, NULL, &trial_ms))
					continue;
				VectorSubtract(gen_seeds[dest].origin, wait_point, delta);
				h2 = delta[0] * delta[0] + delta[1] * delta[1];
				score = (float)trial_ms + sqrtf(h2) + fabsf(delta[2]);
				egress_ms[ci] = trial_ms;
				egress_scores[ci] = score;
				egress_proved[ci] = 1;
				if (score < best_score)
				{
					best_score = score;
					best = dest;
					best_slot = ci;
				}
			}
			if (best < 0)
				continue;
			for (source = 0; source < gen_num_seeds; source++)
			{
				vec3_t approach_delta;
				float approach_h2, score;

				if (!gen_source_stable[source] ||
				    gen_source_waterlevel[source] != 0 ||
				    !Gen_SeedHasIncoming(source) ||
				    !SG_DeclaredDoorApproachSourceClear(door,
				        gen_seeds[source].origin))
					continue;
				VectorSubtract(wait_point, gen_seeds[source].origin,
				               approach_delta);
				approach_h2 = approach_delta[0] * approach_delta[0] +
				              approach_delta[1] * approach_delta[1];
				if (approach_h2 > 320.0f * 320.0f ||
				    fabsf(approach_delta[2]) > 48.0f)
					continue;
				score = approach_h2 + approach_delta[2] * approach_delta[2];
				Door_CandidateInsert(source, score, sources, source_scores,
				                     DOOR_SOURCE_FAN);
			}
			for (ci = 0; ci < DOOR_SOURCE_FAN && sources[ci] >= 0; ci++)
			{
				int approach_ms, touch_ms;
				int picked[4], picked_count = 0, pi;

				source = sources[ci];
				approach_trials++;
				if (!SG_OracleDeclaredDoorApproach(gen_seeds[source].origin,
				        wait_point, door, &approach_ms, &touch_ms))
					continue;

				/* Preserve the locally cheapest proved controller as a movement
				 * shortcut, then add only bounded topology-improving witnesses. */
				picked[picked_count++] = best_slot;
				if (have_topology)
				{
					int missing = 3 & ~topology.objective_mask[source];
					int bit;

					for (bit = 1; bit <= 2; bit <<= 1)
					{
						int choice = -1;
						float choice_score = 1.0e30f;

						if (!(missing & bit))
							continue;
						for (pi = 0; pi < DOOR_DEST_FAN; pi++)
							if (egress_proved[pi] &&
							    (topology.objective_mask[dests[pi]] & bit) &&
							    egress_scores[pi] < choice_score)
							{
								choice = pi;
								choice_score = egress_scores[pi];
							}
						if (choice >= 0)
						{
							for (pi = 0; pi < picked_count; pi++)
								if (picked[pi] == choice)
									break;
							if (pi == picked_count)
								picked[picked_count++] = choice;
						}
					}

					/* If objective masks offer no new bit, still retain one proved
					 * cross-component mechanism. This is what preserves a base-to-main
					 * half whose source already reaches its own flag. */
					for (pi = 0; pi < picked_count; pi++)
						if (topology.component[dests[picked[pi]]] !=
						    topology.component[source])
							break;
					if (pi == picked_count)
					{
						int choice = -1;
						float choice_score = 1.0e30f;

						for (pi = 0; pi < DOOR_DEST_FAN; pi++)
							if (egress_proved[pi] &&
							    topology.component[dests[pi]] !=
							        topology.component[source] &&
							    egress_scores[pi] < choice_score)
							{
								choice = pi;
								choice_score = egress_scores[pi];
							}
						if (choice >= 0 && picked_count < 4)
							picked[picked_count++] = choice;
					}
				}
				/* If selected during DOWN, the trigger cooldown and the close
				 * motion expire together on supported maps. Budget that remaining
				 * close, the subsequent open, and one second of observation margin;
				 * never serialize the mapper's minutes-long TOP hold as travel. */
				for (pi = 0; pi < picked_count; pi++)
				{
					int slot = picked[pi];
					int contract_cost = SG_DeclaredDoorContractCost(door,
					    approach_ms, touch_ms, egress_ms[slot]);

					if (contract_cost > 0 && Door_LinkInsert(source,
					        dests[slot], (short)contract_cost, wait_point))
						gen_door_links++;
				}
			}
			#undef DOOR_SOURCE_FAN
			#undef DOOR_DEST_FAN
		}
		DoorPose_Restore(saved, pose_count);
	}
	Door_TopologyFree(&topology);
	if (gen_door_links || wait_points)
		sg_host.dprint("rune: %d declared door links (%d wait points, "
		               "%d approach/%d egress trials)\n",
		               gen_door_links, wait_points, approach_trials,
		               egress_trials);
}

#undef DOOR_WAIT_MAX

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

	if (gen_source_waterlevel[from] != 0 || !gen_source_stable[from])
		return false;

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

	/* No supported wire contract has a launch-state controller.  The old proof
	 * injected an exact rest state and simultaneous rocket+jump, while live
	 * execution could arm elsewhere in the seed cell and advance without
	 * confirming a shot.
	 * Keep the implementation available for a future versioned contract, but
	 * write no RL_ROCKETJUMP records until generator and executor share one. */
	sg_host.dprint("rune: rocket jumps disabled (unserialized launch state)\n");
	return;

	if (gen_num_seeds <= 0)
		return;

	Link_Index_Build();
	rj_dist = sg_host.game_alloc(sizeof(int) * gen_num_seeds);
	rj_stamp = sg_host.game_alloc(sizeof(int) * gen_num_seeds);
	memset(rj_stamp, 0, sizeof(int) * gen_num_seeds);
	rj_query = 0;

	sg_host.dprint("rune: rocket jumps -- window %.0f to %.0f units of rise\n",
	           SG_RJ_MIN_RISE, ceiling);

	for (i = 0; i < gen_num_seeds && rj_tries < SG_RJ_MAX_TRIES; i++)
	{
		if (gen_source_waterlevel[i] != 0 || !gen_source_stable[i])
			continue;                   /* exact dry rest is the launch state */

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

	sg_host.game_free(sw_first);
	sg_host.game_free(sw_next);
	sw_first = NULL;
	sw_next = NULL;
	sg_host.game_free(rj_dist);
	sg_host.game_free(rj_stamp);
	rj_dist = NULL;
	rj_stamp = NULL;

	sg_host.dprint("rune: rocketjump pairs=%d rolls=%d noboom=%d nolift=%d "
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
			qboolean water_pair;

			if (i == j)
				continue;
			water_pair = ((gen_seeds[i].flags | gen_seeds[j].flags) &
			              RSF_WATER) != 0;
			VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, d);
			if (d[0] * d[0] + d[1] * d[1] > HOOK_REACH * HOOK_REACH)
				continue;
			/* beyond running reach only the hook applies */
			if (!(gen_seeds[i].flags & RSF_WATER) &&
			    d[0] * d[0] + d[1] * d[1] > LINK_REACH * LINK_REACH &&
			    d[2] <= 128.0f && d[2] >= -256.0f)
			{
				vec3_t anchor;

				if (!(gen_seeds[i].flags & RSF_WATER) &&
				    ProveHook(i, j, anchor, &cost, &espeed))
				{
					rune_link_t *l;

					if (!Link_Add(i, j, RL_HOOK, cost, espeed))
						continue;
					l = &gen_links[gen_num_links - 1];
					VectorCopy(anchor, l->anchor);
					Link_Env_Hook(l, anchor);
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

					if (!Link_Add(i, j, RL_DROP, cost, espeed))
						continue;
					dg_arrived++;
					l = &gen_links[gen_num_links - 1];
					VectorCopy(lip, l->anchor);
					Link_Env_Drop(l, dd_last_heading);
					continue;
				}
			}
			/* Deep traversals have exactly one walking prover: ProveDrop.
			 * Generic Prove can fall and arrive, but it does not publish the lip
			 * or the controller that produced that fall; labelling its result
			 * RL_DROP creates an anchorless edge the runtime cannot execute. */
			/* Ordinary direct motion touching a water seed belongs exclusively
			 * to ProveSwim below. DROP and HOOK above/below remain separate only
			 * because each has its own complete proof and runtime controller. */
			if (!water_pair && d[2] <= 128.0f && d[2] >= -160.0f &&
			    Prove(i, j, false, &cost, &espeed))
			{
				int before_wp = gen_num_links;

				Link_Add(i, j, RL_RUN, cost, espeed);
				if (gen_num_links != before_wp && gen_prove_has_wp &&
				    gen_links[gen_num_links - 1].action == RL_RUN)
				{
					VectorCopy(gen_prove_wp,
					           gen_links[gen_num_links - 1].anchor);
					gen_waypoint_links++;
				}
			}
			else if (!water_pair && d[2] <= 128.0f && d[2] >= -160.0f &&
			         Prove(i, j, true, &cost, &espeed))
				Link_Add(i, j, RL_JUMP, cost, espeed);
			else
			{
				vec3_t anchor;

				/* A short ledge may be too low for the deep-drop partition but
				 * still defeat both ordinary controllers: RUN brakes at the edge
				 * and JUMP lands back on the upper shelf. Only after both exact
				 * proofs fail, give the serialized lip controller the downward
				 * pair. ProveDrop remains the authority on whether a real walkoff,
				 * landing, and bounded ground recovery reaches the destination. */
				if (!water_pair && d[2] < 0.0f && d[2] >= -160.0f)
				{
					vec3_t lip;

					dg_pairs++;
					if (ProveDrop(i, j, lip, &cost, &espeed))
					{
						rune_link_t *l;

						if (!Link_Add(i, j, RL_DROP, cost, espeed))
							continue;
						dg_arrived++;
						l = &gen_links[gen_num_links - 1];
						VectorCopy(lip, l->anchor);
						Link_Env_Drop(l, dd_last_heading);
						continue;
					}
				}

				if ((!(gen_seeds[i].flags & RSF_WATER) ||
				     (!(gen_seeds[j].flags & RSF_WATER) && d[2] > 128.0f)) &&
				    ProveHook(i, j, anchor, &cost, &espeed))
				{
					rune_link_t *l;

					if (!Link_Add(i, j, RL_HOOK, cost, espeed))
						continue;
					l = &gen_links[gen_num_links - 1];
					VectorCopy(anchor, l->anchor);
						Link_Env_Hook(l, anchor);
				}
				/* Momentum-entry jumps used to be proved from one exact 320-u/s
				 * vector but serialized as a broad minimum-speed/cone envelope. No
				 * finite gate could make that claim true for every faster or angled
				 * live entry. Until the format carries a complete entry state (or the
				 * prover covers the envelope), fail closed and keep only from-rest
				 * one-jump proofs. */
			}
		}
		if ((i & 255) == 0)
			sg_host.dprint("rune: proving %d/%d seeds, %d links\n",
			           i, gen_num_seeds, gen_num_links);
	}

	/*
	 * ADDITIVE BLOCK call sites -- appended after the pair loop. Each pass
	 * reads the seeds and specialized links the loop produced and only adds
	 * to them; ordinary pairs touching water were reserved for Prove_Swims.
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
		Link_Doors();           /* repeatable trigger: wait, open, full egress */
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

/* A field is useful only on the greatest part of the directed graph from
 * which BOTH flag objectives remain reachable.  Keeping one-way sink seeds
 * lets an otherwise sound route deliberately strand a bot; keeping unlinked
 * germs bloats localization with places the runtime already refuses to use.
 *
 * Compute the greatest fixed point, not merely the intersection of two
 * reverse floods: a node can initially reach red through blue-dead nodes and
 * blue through red-dead nodes.  Removing those intermediates must trigger a
 * second pass.  Once stable, retain rejected seed coordinates as geometry
 * tombstones but remove every incident link. Localization can then identify
 * the nearest visible sample as non-routable instead of searching past it to
 * a farther live seed on the wrong side of a one-way boundary. */
static int Graph_ObjectiveRoot(const vec3_t objective, const byte *has_out)
{
	const float max_horiz2 = 128.0f * 128.0f;
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < gen_num_seeds; i++)
	{
		vec3_t d, from, to;
		float dd;
		trace_t tr;

		VectorSubtract(gen_seeds[i].origin, objective, d);
		if (d[2] > 96.0f || d[2] < -96.0f ||
		    d[0] * d[0] + d[1] * d[1] > max_horiz2)
			continue;
		dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] * 0.25f;
		if (dd >= bestd)
			continue;
		VectorCopy(objective, from);
		VectorCopy(gen_seeds[i].origin, to);
		from[2] += 16.0f;
		to[2] += 16.0f;
		tr = sg_host.trace(from, NULL, NULL, to, NULL, MASK_DEADSOLID);
		if (tr.startsolid || tr.fraction < 1.0f)
			continue;
		bestd = dd;
		best = i;
	}
	return (best >= 0 && has_out[best]) ? best : -1;
}

static void Graph_ReverseReach(int root, const byte *allowed,
	const int *first_in, const int *next_in, int *queue, byte *reached)
{
	int head = 0, tail = 0;

	memset(reached, 0, (size_t)gen_num_seeds);
	if (root < 0 || root >= gen_num_seeds || !allowed[root])
		return;
	reached[root] = 1;
	queue[tail++] = root;
	while (head < tail)
	{
		int at = queue[head++];
		int li;

		for (li = first_in[at]; li >= 0; li = next_in[li])
		{
			int from = gen_links[li].from;

			if (!allowed[from] || reached[from])
				continue;
			reached[from] = 1;
			queue[tail++] = from;
		}
	}
}

static qboolean Graph_PruneObjectiveCore(void)
{
	int *first_in = NULL, *next_in = NULL, *queue = NULL;
	byte *has_out = NULL, *keep = NULL, *red_reach = NULL, *blue_reach = NULL;
	int red_root, blue_root, i, old_links, changed;
	int kept_seeds = 0, new_links = 0;

	if (!redflag || !blueflag || !redflag->inuse || !blueflag->inuse)
	{
		sg_host.dprint("rune: FAILED: objective flags are unavailable\n");
		return false;
	}
	first_in = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	next_in = sg_host.level_alloc(sizeof(int) *
	    (size_t)(gen_num_links ? gen_num_links : 1));
	queue = sg_host.level_alloc(sizeof(int) * (size_t)gen_num_seeds);
	has_out = sg_host.level_alloc((size_t)gen_num_seeds);
	keep = sg_host.level_alloc((size_t)gen_num_seeds);
	red_reach = sg_host.level_alloc((size_t)gen_num_seeds);
	blue_reach = sg_host.level_alloc((size_t)gen_num_seeds);
	if (!first_in || !next_in || !queue || !has_out || !keep ||
	    !red_reach || !blue_reach)
	{
		sg_host.dprint("rune: FAILED: objective-core allocation\n");
		goto fail;
	}

	memset(has_out, 0, (size_t)gen_num_seeds);
	memset(keep, 1, (size_t)gen_num_seeds);
	for (i = 0; i < gen_num_seeds; i++)
		first_in[i] = -1;
	for (i = 0; i < gen_num_links; i++)
	{
		has_out[gen_links[i].from] = 1;
		next_in[i] = first_in[gen_links[i].to];
		first_in[gen_links[i].to] = i;
	}
	red_root = Graph_ObjectiveRoot(redflag->homeposition, has_out);
	blue_root = Graph_ObjectiveRoot(blueflag->homeposition, has_out);
	if (red_root < 0 || blue_root < 0)
	{
		sg_host.dprint("rune: FAILED: cannot bind both flag objectives to graph\n");
		goto fail;
	}

	do
	{
		changed = 0;
		Graph_ReverseReach(red_root, keep, first_in, next_in, queue, red_reach);
		Graph_ReverseReach(blue_root, keep, first_in, next_in, queue, blue_reach);
		for (i = 0; i < gen_num_seeds; i++)
			if (keep[i] && (!red_reach[i] || !blue_reach[i]))
			{
				keep[i] = 0;
				changed = 1;
			}
	} while (changed);
	if (!keep[red_root] || !keep[blue_root])
	{
		sg_host.dprint("rune: FAILED: flag objectives share no closed route core\n");
		goto fail;
	}
	/* The generating server is the only authority that has the final spawned
	 * flag entities, including engine entity overrides and put-on-floor
	 * settling. Publish those exact seed identities for runegen's deployment
	 * lint; an offline BSP/ENT parser cannot reproduce that world state. */
	sg_host.dprint("rune: objective roots red=%d blue=%d\n",
	               red_root, blue_root);

	old_links = gen_num_links;
	for (i = 0; i < gen_num_seeds; i++)
	{
		if (keep[i])
			kept_seeds++;
		else
			gen_seeds[i].flags |= RSF_TOMBSTONE;
	}
	for (i = 0; i < old_links; i++)
	{
		rune_link_t link = gen_links[i];

		if (!keep[link.from] || !keep[link.to])
			continue;
		gen_links[new_links++] = link;
	}
	gen_num_links = new_links;
	sg_host.dprint("rune: objective core retained %d/%d routable seeds, "
	               "%d/%d links; %d geometry tombstones\n",
	               kept_seeds, gen_num_seeds, new_links, old_links,
	               gen_num_seeds - kept_seeds);

	sg_host.level_free(first_in);
	sg_host.level_free(next_in);
	sg_host.level_free(queue);
	sg_host.level_free(has_out);
	sg_host.level_free(keep);
	sg_host.level_free(red_reach);
	sg_host.level_free(blue_reach);
	return true;

fail:
	if (first_in) sg_host.level_free(first_in);
	if (next_in) sg_host.level_free(next_in);
	if (queue) sg_host.level_free(queue);
	if (has_out) sg_host.level_free(has_out);
	if (keep) sg_host.level_free(keep);
	if (red_reach) sg_host.level_free(red_reach);
	if (blue_reach) sg_host.level_free(blue_reach);
	return false;
}

/* ------------------------------------------------------------------- IO */

/*
 * Doors are closed while the world idles, and a closed door is solid to the
 * phantom's traces -- which proved out as every room becoming an island: 90
 * of 1562 seeds could reach the red flag on lmctf03, the flag room and
 * nothing beyond it. For the duration of generation the doors are unsolid,
 * the same assumption bspc made, and every one is restored before the
 * command returns. The oracle still treats each door's complete swept volume
 * as blocked until that exact phantom touches a validated repeatable player
 * activator, so making the brush nonsolid is not permission to prove through
 * buttons, one-shot scripts, disabled triggers, or the wrong side of a door.
 */
typedef struct { edict_t *e; solid_t solid; } heldopen_t;

static int Doors_Open(heldopen_t *held, int max)
{
	edict_t *e;
	int i, n = 0;

	for (i = 0; i < globals.num_edicts; i++)
	{
		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (strncmp(e->classname, "func_door", 9) != 0)
			continue;
		if (n >= max)
			return -n;
		held[n].e = e;
		held[n].solid = e->solid;
		e->solid = SOLID_NOT;
		sg_host.linkentity(e);
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
		sg_host.linkentity(held[i].e);
	}
}

qboolean Rune_Generate(const char *mapname)
{
	sg_rune_v3_authority_t authority;
	rune_v3_recheck_t recheck;
	rune_v3_stream_t stream;
	sg_rune_v3_workspace_t workspace;
	sg_rune_install_result_t install_result;
	const sg_rune_install_ops_t *install_ops;
	uint64_t *link_keys = NULL;
	uint8_t *source_marks = NULL;
	char game_directory[MAX_OSPATH];
	char path[MAX_OSPATH], tmp_path[MAX_OSPATH];
	heldopen_t held[128];
	int ndoors = 0;
	int directory_written;
	qboolean scope_active = false;
	qboolean generated = false;
	cvar_t *game_directory_cvar;
	const char *game_directory_source;
	const char *canonical_mapname;

	SG_HooksInit();
	if (!SG_RuneV3AuthorityCapture(mapname, &authority))
	{
		if (authority.identity_status != SG_IDENTITY_OK)
			sg_host.dprint("rune: v3 generation refused stage=identity "
			               "status=%d reason=%s\n",
			               (int)authority.identity_status,
			               SG_LevelIdentityReason(authority.identity_status));
		else
			sg_host.dprint("rune: v3 generation refused stage=proof-law "
			               "reason=unsupported-active-law\n");
		return false;
	}
	canonical_mapname = authority.level.mapname;
	game_directory_cvar = sg_host.cvar("gamedir", "", 0);
	game_directory_source = game_directory_cvar &&
		game_directory_cvar->string && game_directory_cvar->string[0]
		? game_directory_cvar->string : ".";
	directory_written = snprintf(game_directory, sizeof(game_directory),
		"%s", game_directory_source);
	install_ops = SG_RuneInstallDefaultOps();
	if (directory_written < 0 ||
	    (size_t)directory_written >= sizeof(game_directory) ||
	    !SG_RuneInstallDestinationPath(path, sizeof(path), game_directory,
	        canonical_mapname) ||
	    !SG_RuneInstallTemporaryPath(tmp_path, sizeof(tmp_path),
	        game_directory, install_ops->process_id(install_ops->context), 0))
	{
		sg_host.dprint("rune: v3 generation refused stage=path "
		               "reason=MAX_OSPATH\n");
		return false;
	}
	if (!SG_RuneProofScopeBegin(authority.wire.gravity))
	{
		sg_host.dprint("rune: v3 generation refused stage=proof-scope "
		               "reason=busy-or-invalid\n");
		return false;
	}
	scope_active = true;

	gen_seeds = NULL;
	gen_links = NULL;
	gen_seeds = sg_host.game_alloc(sizeof(rune_seed_t) * SEED_MAX);
	gen_links = sg_host.game_alloc(sizeof(rune_link_t) * LINK_MAX);
	if (!gen_seeds || !gen_links)
	{
		sg_host.dprint("rune: FAILED: generator allocation; graph was not written\n");
		goto cleanup;
	}
	gen_num_seeds = 0;
	gen_num_links = 0;
	gen_seed_overflow = false;
	gen_link_overflow = false;
	gen_water_overflow = false;
	memset(hash_head, 0xff, sizeof(hash_head));
	/* Every generator budget and diagnostic belongs to this invocation.  These
	 * used to be process statics without a reset, so a second `sv rune` inherited
	 * exhausted momentum/RJ budgets and silently generated a weaker graph. */
	dg_pairs = dg_seek = dg_noedge = dg_fell = dg_arrived = dg_nocontact = 0;
	dd_last_heading = 0;
	dd_nolip = dd_fenced = dd_flew = dd_landed = dd_won = 0;
	gen_momentum_links = gen_waypoint_links = 0;
	gen_prove_has_wp = false;
	VectorClear(gen_prove_wp);
	gen_first_water = -1;
	gen_num_water = 0;
	gen_lift_links = gen_tele_links = gen_door_links = 0;
	gen_swim_links = 0;
	gen_env_drop = gen_env_hook = gen_declared_links = 0;
	gen_lift_down_drop = gen_lift_down_none = 0;
	rj_pairs = rj_tries = rj_noboom = rj_nolift = rj_arrived = 0;
	rj_redundant = rj_links = rj_budget_out = 0;
	rj_query = 0;

	ndoors = Doors_Open(held, 128);
	if (ndoors < 0)
	{
		Doors_Restore(held, -ndoors);
		ndoors = 0;
		sg_host.dprint("rune: FAILED: more than 128 doors; graph was not written\n");
		goto cleanup;
	}
	sg_host.dprint("rune: %d doors held open for proving\n", ndoors);

	sg_host.dprint("rune: germinating from entities...\n");
	Seed_Germinate();
	sg_host.dprint("rune: %d germs; flooding...\n", gen_num_seeds);
	Seed_Flood();
	/* ADDITIVE BLOCK call site: the water volumes the dry passes cannot
	 * reach into, seeded before anything is proven so the pair loop sees
	 * them like any other seed */
	Seed_Water();
	if (gen_num_seeds <= 0)
	{
		Doors_Restore(held, ndoors);
		ndoors = 0;
		sg_host.dprint("rune: FAILED: map produced no executable seeds\n");
		goto cleanup;
	}
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

		sg_host.dprint("rune: measuring exposure...\n");
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
				etr = sg_host.trace(a, NULL, NULL, b, NULL, MASK_OPAQUE);
				if (etr.fraction >= 1.0f)
					vis++;
			}
			/* scale to the byte-ish range; sampled can be < 24 in sparse
			 * regions, so store the RATE, not the raw count */
			gen_seeds[i].area_hint = (short)(sampled ?
			    (vis * 255) / sampled : 0);
		}
	}
	sg_host.dprint("rune: %d seeds; proving links...\n", gen_num_seeds);
	Prove_All();
	sg_host.dprint("rune: %d links proven\n", gen_num_links);
	sg_host.dprint("rune: dropstats pairs=%d seek=%d noedge=%d fellsteps=%d arrived=%d nocontact=%d\n",
	           dg_pairs, dg_seek, dg_noedge, dg_fell, dg_arrived, dg_nocontact);
	sg_host.dprint("rune: geodrop nolip=%d fenced=%d flew=%d landedsteps=%d won=%d\n",
	           dd_nolip, dd_fenced, dd_flew, dd_landed, dd_won);
	sg_host.dprint("rune: envelopes drop=%d hook=%d; declared=%d (lift=%d tele=%d door=%d); "
	           "plat-down drop=%d unlinked=%d; momentum=%d waypoints=%d\n",
	           gen_env_drop, gen_env_hook, gen_declared_links,
	           gen_lift_links, gen_tele_links, gen_door_links,
	           gen_lift_down_drop, gen_lift_down_none, gen_momentum_links,
	           gen_waypoint_links);
	Doors_Restore(held, ndoors);
	ndoors = 0;
	/* Objective ownership is resolved against the real, restored world.  Mark
	 * every non-core geometry sample before writing so runtime localization and
	 * the deployment linter share the same fail-closed topology contract. */
	if (!Graph_PruneObjectiveCore())
		goto cleanup;
	/* SEED_MAX/LINK_MAX are inclusive v3 count limits.  Filling the final
	 * allocated slot is legal; only an attempted insertion beyond it sets the
	 * corresponding overflow flag and invalidates the graph. */
	if (gen_seed_overflow || gen_link_overflow || gen_water_overflow)
	{
		sg_host.dprint("rune: FAILED: %s capacity exhausted; graph was not written\n",
		               gen_seed_overflow ? "seed" :
		               gen_water_overflow ? "water seed" : "link");
		goto cleanup;
	}
	if (gen_num_links <= 0)
	{
		sg_host.dprint("rune: FAILED: no executable links were proven; graph was not written\n");
		goto cleanup;
	}

	if ((size_t)gen_num_links > (size_t)INT_MAX / sizeof(*link_keys) ||
	    (size_t)gen_num_seeds > (size_t)INT_MAX / sizeof(*source_marks))
	{
		sg_host.dprint("rune: FAILED: v3 writer workspace size overflow\n");
		goto cleanup;
	}
	link_keys = sg_host.game_alloc((int)(sizeof(*link_keys) *
		(size_t)gen_num_links));
	source_marks = sg_host.game_alloc((int)(sizeof(*source_marks) *
		(size_t)gen_num_seeds));
	if (!link_keys || !source_marks)
	{
		sg_host.dprint("rune: FAILED: v3 writer workspace allocation\n");
		goto cleanup;
	}
	memset(&workspace, 0, sizeof(workspace));
	workspace.link_keys = link_keys;
	workspace.link_key_capacity = (size_t)gen_num_links;
	workspace.source_marks = source_marks;
	workspace.source_mark_capacity = (size_t)gen_num_seeds;
	memset(&stream, 0, sizeof(stream));
	stream.identity = &authority.wire;
	stream.seeds = gen_seeds;
	stream.num_seeds = (uint32_t)gen_num_seeds;
	stream.links = gen_links;
	stream.num_links = (uint32_t)gen_num_links;
	stream.workspace = &workspace;
	memset(&recheck, 0, sizeof(recheck));
	recheck.mapname = canonical_mapname;
	recheck.captured = &authority;
	recheck.identity_status = SG_IDENTITY_OK;
	install_result = SG_RuneInstallV3(game_directory, canonical_mapname,
		path, sizeof(path), tmp_path, sizeof(tmp_path), Rune_V3Stream,
		&stream, Rune_V3Revalidate, &recheck, install_ops);
	if (install_result.status != SG_RUNE_INSTALL_OK)
	{
		if (install_result.status == SG_RUNE_INSTALL_REVALIDATE_FAILED)
		{
			if (recheck.failure == RUNE_V3_RECHECK_IDENTITY)
				sg_host.dprint("rune: v3 revalidation failed "
				               "kind=identity status=%d reason=%s\n",
				               (int)recheck.identity_status,
				               SG_LevelIdentityReason(recheck.identity_status));
			else
				sg_host.dprint("rune: v3 revalidation failed "
				               "kind=proof-law\n");
		}
		sg_host.dprint("rune: v3 install failed status=%d reason=%s "
		               "os_error=%d cleanup_error=%d diagnostic=%d "
		               "reject_reason=%d writer_stage=%d writer_index=%u "
		               "bytes=%lu; kept existing %s\n",
		               (int)install_result.status,
		               SG_RuneInstallReason(install_result.status),
		               install_result.os_error,
		               install_result.cleanup_error,
		               (int)install_result.writer.diagnostic,
		               (int)install_result.writer.reason,
		               (int)install_result.writer.stage,
		               (unsigned int)install_result.writer.index,
		               (unsigned long)install_result.writer.bytes_written,
		               path);
		goto cleanup;
	}

	sg_host.dprint("rune: wrote %s (%d seeds, %d links)\n",
	           path, gen_num_seeds, gen_num_links);
	generated = true;

cleanup:
	if (ndoors > 0)
		Doors_Restore(held, ndoors);
	if (link_keys)
		sg_host.game_free(link_keys);
	if (source_marks)
		sg_host.game_free(source_marks);
	if (gen_seeds)
		sg_host.game_free(gen_seeds);
	if (gen_links)
		sg_host.game_free(gen_links);
	gen_seeds = NULL;
	gen_links = NULL;
	gen_num_seeds = 0;
	gen_num_links = 0;
	if (scope_active)
		SG_RuneProofScopeEnd();
	return generated;
}
