

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_identity.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_compound.h"
#include "slipgate/sg_compound_action_gen.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_rune_install.h"
#include "slipgate/sg_rune_stream.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_rune_mechanism_plan.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_rune_door_scope.h"
#include "slipgate/sg_compound_gen_game.h"
#include "slipgate/sg_rocketjump_cadence.h"
#include "slipgate/sg_push_live.h"
#include "slipgate/sg_train_gate_live.h"
#include "slipgate/sg_water_forest.h"
#include "slipgate/sg_util.h"

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define SEED_SPACING	64.0f
#define PLAYER_HALF_WIDTH 16.0f
#define SEED_MAX		RUNE_MAX_SEEDS
#define LINK_MAX		RUNE_MAX_LINKS
_Static_assert(SEED_MAX == RUNE_MAX_SEEDS,
	"generator seed capacity drift");
_Static_assert(LINK_MAX == RUNE_MAX_LINKS,
	"generator link capacity drift");
#define LINK_REACH		192.0f		/* run/jump pairs within this reach */
#define HOOK_REACH		448.0f
#define HOOK_PAIR_REACH	768.0f
#define ARRIVE_RADIUS	40.0f
#define STEP_MSEC		25			/* honest client-rate steps, 4 per frame */
#define TRY_LIMIT_MS	3000		/* a link longer than this is not local */
#define DROP_HANDOFF_RADIUS    8.0f	/* runtime's lip-to-walkoff handoff */

_Static_assert(STEP_MSEC == SG_RUNE_PROOF_PMOVE_SUBSTEP_MS,
	"generator pmove cadence drift");

/* prover autopsy: where drop attempts actually die */
static int dg_pairs, dg_seek, dg_noedge, dg_fell, dg_arrived, dg_nocontact;

static rune_seed_t	*gen_seeds;
static int			gen_num_seeds;
static qboolean		gen_seed_overflow;
static rune_link_t	*gen_links;
static int			gen_num_links;
static qboolean		gen_link_overflow;
static qboolean		gen_water_overflow;
static sg_water_forest_t gen_water_forest;
static int *gen_water_parents;
static uint8_t *gen_water_ranks;
static sg_water_edge_t *gen_water_edges;
static int *gen_water_edge_slots;

static sg_mech_catalog_view_t gen_mechanism_catalog;
static sg_mechanism_plan_binding_t *gen_mechanism_bindings;
static uint32_t gen_num_mechanism_bindings;
static qboolean gen_mechanism_failed;

/* RUNE generation can be terminated by an external timeout while a prover is
 * rolling.  Keep a small, saturating progress snapshot and emit it only at
 * phase boundaries; the phase-start record is intentionally durable evidence
 * of where a terminated generation was spending its time. */
typedef struct rune_telemetry_s
{
	uint32_t seed_scans;
	uint32_t water_scans;
	uint32_t link_scans;
	uint32_t pair_scans;
	uint32_t qualified;
	uint32_t prover_calls;
	uint32_t prover_steps;
	uint32_t door_replays;
} rune_telemetry_t;

typedef struct rune_phase_telemetry_s
{
	const char *name;
	clock_t cpu_start;
	time_t wall_start;
	qboolean running;
} rune_phase_telemetry_t;

static rune_telemetry_t gen_telemetry;
static rune_phase_telemetry_t gen_phase_telemetry;

static void Rune_TelemetryAdd(uint32_t *value, uint32_t amount)
{
	if (!value)
		return;
	if (UINT32_MAX - *value < amount)
		*value = UINT32_MAX;
	else
		*value += amount;
}

static unsigned long Rune_TelemetryCpuMs(clock_t value)
{
	if (value <= (clock_t)0 || CLOCKS_PER_SEC <= 0)
		return 0UL;
	return (unsigned long)(((uintmax_t)value * 1000U) / CLOCKS_PER_SEC);
}

static unsigned long Rune_TelemetryWallMs(time_t value)
{
	if (value <= (time_t)0)
		return 0UL;
	return (unsigned long)((uintmax_t)value * 1000U);
}

static void Rune_LogFlush(void)
{
	/* The RUNE host contract makes a complete diagnostic record visible before
	 * this returns.  Every phase record calls this after dprint, never per item. */
	sg_host.flush();
}

static void Rune_TelemetryLine(const char *event, const char *phase,
	clock_t cpu_start, time_t wall_start)
{
	clock_t cpu_now = clock();
	time_t wall_now = time(NULL);
	unsigned long cpu_ms = Rune_TelemetryCpuMs(cpu_now);
	unsigned long wall_ms = Rune_TelemetryWallMs(wall_now);
	unsigned long cpu_elapsed = 0UL;
	unsigned long wall_elapsed = 0UL;

	if (cpu_start > (clock_t)0 && cpu_now >= cpu_start)
		cpu_elapsed = Rune_TelemetryCpuMs(cpu_now - cpu_start);
	if (wall_start > (time_t)0 && wall_now >= wall_start)
		wall_elapsed = Rune_TelemetryWallMs(wall_now - wall_start);
	sg_host.dprint("rune: telemetry event=%s phase=%s cpu_ms=%lu wall_ms=%lu "
	               "cpu_elapsed_ms=%lu wall_elapsed_ms=%lu seed_scans=%u "
	               "water_scans=%u link_scans=%u pair_scans=%u qualified=%u "
	               "prover_calls=%u prover_steps=%u door_replays=%u "
	               "catalog_nodes=%u catalog_edges=%u\n",
	               event, phase, cpu_ms, wall_ms, cpu_elapsed, wall_elapsed,
	               (unsigned int)gen_telemetry.seed_scans,
	               (unsigned int)gen_telemetry.water_scans,
	               (unsigned int)gen_telemetry.link_scans,
	               (unsigned int)gen_telemetry.pair_scans,
	               (unsigned int)gen_telemetry.qualified,
	               (unsigned int)gen_telemetry.prover_calls,
	               (unsigned int)gen_telemetry.prover_steps,
	               (unsigned int)gen_telemetry.door_replays,
	               (unsigned int)gen_mechanism_catalog.num_nodes,
	               (unsigned int)gen_mechanism_catalog.num_edges);
	Rune_LogFlush();
}

static void Rune_TelemetryPhaseStart(const char *phase)
{
	gen_phase_telemetry.name = phase;
	gen_phase_telemetry.cpu_start = clock();
	gen_phase_telemetry.wall_start = time(NULL);
	gen_phase_telemetry.running = true;
	Rune_TelemetryLine("phase-start", phase, (clock_t)0, (time_t)0);
}

static void Rune_TelemetryPhaseEnd(void)
{
	if (!gen_phase_telemetry.running)
		return;
	Rune_TelemetryLine("phase-end", gen_phase_telemetry.name,
	                  gen_phase_telemetry.cpu_start,
	                  gen_phase_telemetry.wall_start);
	gen_phase_telemetry.name = NULL;
	gen_phase_telemetry.running = false;
}

/* spatial hash so the lattice dedupes at SEED_SPACING */
#define HASH_SIZE 4096
static int hash_head[HASH_SIZE];
static int hash_next[SEED_MAX];
static byte gen_source_stable[SEED_MAX];
static byte gen_source_waterlevel[SEED_MAX];
static byte gen_source_watertype[SEED_MAX];

static qboolean Seed_Representable(const vec3_t origin)
{
	return isfinite(origin[0]) && isfinite(origin[1]) && isfinite(origin[2]) &&
	       origin[0] >= -4096.0f && origin[0] <= 4095.875f &&
	       origin[1] >= -4096.0f && origin[1] <= 4095.875f &&
	       origin[2] >= -4096.0f && origin[2] <= 4095.875f;
}

static qboolean Seed_OnPmoveGrid(const vec3_t origin)
{
	int axis;

	if (!Seed_Representable(origin))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = origin[axis] * 8.0f;

		if (scaled != (float)(int)scaled)
			return false;
	}
	return true;
}

typedef enum rune_recheck_failure_e
{
	RUNE_RECHECK_NONE = 0,
	RUNE_RECHECK_IDENTITY,
	RUNE_RECHECK_PROOF_LAW
} rune_recheck_failure_t;

typedef struct rune_recheck_s
{
	const char *mapname;
	const sg_rune_authority_t *captured;
	rune_recheck_failure_t failure;
	sg_identity_status_t identity_status;
} rune_recheck_t;

static uint32_t Rune_FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static qboolean Rune_PhysicsCapture(const sg_level_identity_t *level_id,
	rune_identity_t *identity)
{
	cvar_t *airaccelerate;
	float gravity;

	if (!level_id || !identity || !sg_host.cvar)
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
	    (want_funky_gravity && want_funky_gravity->value != 0.0f) ||
	    FRAMETIME !=
	        (float)SG_RUNE_PROOF_SERVER_FRAME_MS / 1000.0f ||
	    level_id->host_physics_id != SG_HOST_PHYSICS_EPOCH)
		return false;

	memset(identity, 0, sizeof(*identity));
	memcpy(identity->map_name, level_id->mapname, RUNE_MAP_NAME_BYTES);
	identity->bsp_checksum = level_id->bsp_checksum;
	identity->entity_crc32 = level_id->entity_crc32;
	identity->physics_flags = SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED;
	identity->gravity = gravity;
	identity->airaccelerate = airaccelerate->value;
	identity->maxvelocity = sv_maxvelocity->value;
	identity->pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	identity->server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	identity->host_physics_id = level_id->host_physics_id;
	return true;
}

qboolean SG_RuneAuthorityCapture(const char *mapname,
	sg_rune_authority_t *authority)
{
	if (!authority)
		return false;
	memset(authority, 0, sizeof(*authority));
	authority->identity_status = SG_LevelIdentitySnapshot(mapname,
		&authority->level);
	if (authority->identity_status != SG_IDENTITY_OK)
		return false;
	return Rune_PhysicsCapture(&authority->level, &authority->identity);
}

static qboolean Rune_IdentityEqual(const rune_identity_t *first,
	const rune_identity_t *second)
{
	return first && second &&
	       first->bsp_checksum == second->bsp_checksum &&
	       first->entity_crc32 == second->entity_crc32 &&
	       first->physics_flags == second->physics_flags &&
	       Rune_FloatBits(first->gravity) == Rune_FloatBits(second->gravity) &&
	       Rune_FloatBits(first->airaccelerate) ==
	           Rune_FloatBits(second->airaccelerate) &&
	       Rune_FloatBits(first->maxvelocity) ==
	           Rune_FloatBits(second->maxvelocity) &&
	       first->pmove_substep_ms == second->pmove_substep_ms &&
	       first->server_frame_ms == second->server_frame_ms &&
	       first->host_physics_id == second->host_physics_id &&
	       memcmp(first->map_name, second->map_name,
	           RUNE_MAP_NAME_BYTES) == 0;
}

qboolean SG_RuneAuthorityMatchesArtifact(
	const sg_rune_authority_t *authority, const rune_artifact_t *artifact)
{
	return authority && authority->identity_status == SG_IDENTITY_OK &&
	       artifact && artifact->magic == RUNE_ARTIFACT_MAGIC &&
	       artifact->action_contract_crc32 ==
	           SG_RUNE_ACTION_CONTRACT_CRC32 &&
	       artifact->mechanism_contract_crc32 ==
	           SG_RUNE_MECHANISM_CONTRACT_CRC32 &&
	       Rune_IdentityEqual(&authority->identity, &artifact->identity);
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	sg_rune_authority_t current;

	if (!SG_RunePublishedShapeValid(rune) ||
	    !SG_RuneAuthorityCapture(rune->artifact.identity.map_name, &current))
		return false;
	return SG_RuneAuthorityMatchesArtifact(&current, &rune->artifact);
}

static qboolean Rune_LevelIdentityEqual(const sg_level_identity_t *first,
	const sg_level_identity_t *second)
{
	return first && second &&
	       first->bsp_checksum == second->bsp_checksum &&
	       first->entity_crc32 == second->entity_crc32 &&
	       first->host_physics_id == second->host_physics_id &&
	       memcmp(first->mapname, second->mapname,
	           SG_LEVEL_IDENTITY_MAPNAME_BYTES) == 0;
}

static qboolean Rune_ProofLawEqual(const rune_identity_t *first,
	const rune_identity_t *second)
{
	return first && second &&
	       first->physics_flags == second->physics_flags &&
	       Rune_FloatBits(first->gravity) == Rune_FloatBits(second->gravity) &&
	       Rune_FloatBits(first->airaccelerate) ==
	           Rune_FloatBits(second->airaccelerate) &&
	       Rune_FloatBits(first->maxvelocity) ==
	           Rune_FloatBits(second->maxvelocity) &&
	       first->pmove_substep_ms == second->pmove_substep_ms &&
	       first->server_frame_ms == second->server_frame_ms;
}

static int Rune_Revalidate(void *context)
{
	rune_recheck_t *recheck = context;
	sg_rune_authority_t current;

	if (!recheck || !recheck->captured)
		return 0;
	recheck->failure = RUNE_RECHECK_NONE;
	if (!SG_RuneProofScopeActive() ||
	    (float)SG_RuneProofGravity() != recheck->captured->identity.gravity)
	{
		recheck->failure = RUNE_RECHECK_PROOF_LAW;
		return 0;
	}
	if (!SG_RuneAuthorityCapture(recheck->mapname, &current))
	{
		recheck->identity_status = current.identity_status;
		recheck->failure = current.identity_status == SG_IDENTITY_OK
			? RUNE_RECHECK_PROOF_LAW : RUNE_RECHECK_IDENTITY;
		return 0;
	}
	if (!Rune_LevelIdentityEqual(&recheck->captured->level,
	    &current.level))
	{
		recheck->failure = RUNE_RECHECK_IDENTITY;
		recheck->identity_status = SG_IDENTITY_UNAVAILABLE;
		return 0;
	}
	if (!Rune_ProofLawEqual(&recheck->captured->identity, &current.identity))
	{
		recheck->failure = RUNE_RECHECK_PROOF_LAW;
		return 0;
	}
	return 1;
}

static void *Rune_StreamAllocate(void *context, size_t bytes)
{
	(void)context;
	return bytes != 0U && bytes <= (size_t)INT_MAX
		? sg_host.game_alloc((int)bytes) : NULL;
}

static void Rune_StreamRelease(void *context, void *allocation)
{
	(void)context;
	if (allocation)
		sg_host.game_free(allocation);
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

static int Seed_NearbyIndex(vec3_t p)
{
	int best = -1, dx, dy, dz, i;
	float best_distance = 0.0f;
	vec3_t d;

	if (!Seed_Representable(p))
		return -2;
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
					if (d[2] > -48.0f && d[2] < 48.0f)
					{
						float horizontal = d[0] * d[0] + d[1] * d[1];
						float distance = horizontal + d[2] * d[2];

						if (horizontal <
						        SEED_SPACING * SEED_SPACING * 0.81f &&
						    (best < 0 || distance < best_distance ||
						     (distance == best_distance && i < best)))
						{
							best = i;
							best_distance = distance;
						}
					}
				}
			}
	return best;
}

static qboolean Seed_Nearby(vec3_t p)
{
	return Seed_NearbyIndex(p) != -1;
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
		/* A hit on the rotator itself has already been clipped back by the
		 * engine's collision epsilon, so it is never a standing floor.  For a
		 * normal floor, however, test only the authoritative reached segment:
		 * a rotator behind that opaque floor cannot make this seed disappear. */
		if (!tr.startsolid && !tr.allsolid &&
		    !(tr.ent && tr.ent->solid == SOLID_BSP && tr.ent->classname &&
		      !strcmp(tr.ent->classname, "func_rotating")) &&
		    !SG_OracleRotatorSweepBlocks(start, mins, maxs, tr.endpos,
		                                 MASK_PLAYERSOLID))
			break;
	}
	if (L == 4)
		return false;
	if (tr.fraction == 1.0f)
		return false;                       /* no floor within reach */
	if (tr.plane.normal[2] < 0.7f)
		return false;                       /* too steep to stand on */
	/* CM backs the trace endpoint off by 1/32 unit.  That float can truncate
	 * onto the floor when converted to pmove's signed-q8 origin.  Admit only
	 * the exact legal grounded rest selected and stabilized by Pmove itself. */
	return SG_OracleCanonicalGroundSource(tr.endpos, out);
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
	if (!Seed_OnPmoveGrid(origin))
		return;
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
	/* Only a distinct, admissible seed beyond the shared capacity overflows. */
	if (gen_num_seeds >= SEED_MAX)
	{
		gen_seed_overflow = true;
		return;
	}
	VectorCopy(origin, gen_seeds[gen_num_seeds].origin);
	gen_seeds[gen_num_seeds].area_hint = 0;
	gen_seeds[gen_num_seeds].flags = submerged ? RSF_WATER : 0;
	gen_source_stable[gen_num_seeds] =
	    Seed_SourceUnstable(origin) ? 0 : 1;
	gen_source_waterlevel[gen_num_seeds] = (byte)waterlevel;
	gen_source_watertype[gen_num_seeds] = (byte)watertype;

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

			Rune_TelemetryAdd(&gen_telemetry.seed_scans, 1U);

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
				if (SG_OracleRotatorSweepBlocks(from, pmins, pmaxs, to,
				                                MASK_PLAYERSOLID))
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

			Rune_TelemetryAdd(&gen_telemetry.seed_scans, 1U);
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

				Rune_TelemetryAdd(&gen_telemetry.seed_scans, 1U);
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

					Rune_TelemetryAdd(&gen_telemetry.seed_scans, 1U);
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

		Rune_TelemetryAdd(&gen_telemetry.seed_scans, 1U);
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
 * from the straight line goes there whenever the proof deviated more
 * than 48 units, and the body steers via it. Zero anchor = straight proof.
 */
static vec3_t gen_prove_wp;
static qboolean gen_prove_has_wp;
static qboolean gen_prove_last_edge_seek;
static qboolean gen_prove_last_airborne;

/*
 * Entry speed for the NEXT Prove roll, consumed by Prove at placement. Zero
 * means a from-rest proof. Nonzero is the momentum experiment: a gap too
 * wide for the runway inside one
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

	Rune_TelemetryAdd(&gen_telemetry.prover_calls, 1U);
	gen_prove_last_edge_seek = false;
	gen_prove_last_airborne = false;
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

		Rune_TelemetryAdd(&gen_telemetry.prover_steps, 1U);

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
			gen_prove_last_edge_seek = true;
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
		if (!ph.groundentity && ph.waterlevel < 2)
			gen_prove_last_airborne = true;
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
	if (VectorLength(want) > RUNE_HOOK_BITE_TOLERANCE)
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

	Rune_TelemetryAdd(&gen_telemetry.prover_calls, 1U);
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
			if (VectorLength(want) > RUNE_HOOK_BITE_TOLERANCE)
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
		 * Keep it inside one compact lattice neighbourhood and one vertical
		 * seed tier.  lmctf54's symmetric flag shelves rise 112--124 units
		 * from their lower approach samples; the old 96-unit ceiling rejected
		 * those hookable entrances before the exact traversal could run. */
		if (!SG_RuneProofHookLateralWindow(horiz, d[2]))
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
	int		sweep_clear_ms;
	sg_replay_reason_t reason;
	sg_phantom_t	end;
} sg_drop_trial_t;

#define DROP_PREFIX_MAX_FRAMES \
	(SG_REPLAY_DROP_TOTAL_MS / SG_REPLAY_STEP_MS)
#define DROP_PREFIX_CHUNK_FRAMES 8
#define DROP_PREFIX_MAX_CHUNKS \
	((DROP_PREFIX_MAX_FRAMES + DROP_PREFIX_CHUNK_FRAMES - 1) / \
	 DROP_PREFIX_CHUNK_FRAMES)

typedef struct drop_prefix_frame_s
{
	usercmd_t command;
	sg_phantom_t phantom;
	qboolean clean;
} drop_prefix_frame_t;

typedef struct drop_prefix_chunk_s
{
	drop_prefix_frame_t frame[DROP_PREFIX_CHUNK_FRAMES];
} drop_prefix_chunk_t;

typedef struct drop_prefix_cache_s
{
	vec3_t source;
	vec3_t lip;
	byte heading;
	int count;
	drop_prefix_chunk_t *chunk[DROP_PREFIX_MAX_CHUNKS];
	struct drop_prefix_cache_s *next;
} drop_prefix_cache_t;

static drop_prefix_cache_t *drop_prefix_cache;
static qboolean drop_prefix_cache_enabled;

static void Drop_PrefixCacheClear(void)
{
	drop_prefix_cache_t *entry = drop_prefix_cache;

	while (entry)
	{
		drop_prefix_cache_t *next = entry->next;
		int i;

		for (i = 0; i < DROP_PREFIX_MAX_CHUNKS; i++)
			if (entry->chunk[i])
				sg_host.game_free(entry->chunk[i]);
		sg_host.game_free(entry);
		entry = next;
	}
	drop_prefix_cache = NULL;
}

static drop_prefix_cache_t *Drop_PrefixCacheGet(vec3_t source, vec3_t lip,
	byte heading)
{
	drop_prefix_cache_t *entry;

	/* Base-link sources are contiguous.  Retaining only the active source
	 * bounds storage without limiting how many exact prefixes it may prove. */
	if (drop_prefix_cache &&
	    !VectorCompare(drop_prefix_cache->source, source))
		Drop_PrefixCacheClear();
	for (entry = drop_prefix_cache; entry; entry = entry->next)
		if (entry->heading == heading &&
		    VectorCompare(entry->source, source) &&
		    VectorCompare(entry->lip, lip))
			return entry;
	entry = sg_host.game_alloc(sizeof(*entry));
	if (!entry)
		return NULL;
	memset(entry, 0, sizeof(*entry));
	VectorCopy(source, entry->source);
	VectorCopy(lip, entry->lip);
	entry->heading = heading;
	entry->next = drop_prefix_cache;
	drop_prefix_cache = entry;
	return entry;
}

static qboolean Drop_PrefixReplay(drop_prefix_cache_t *prefix, int *index,
	const usercmd_t *command, qboolean recovery, sg_phantom_t *phantom,
	qboolean *clean)
{
	drop_prefix_chunk_t *chunk;
	int frame;

	if (!prefix || !index || !command || recovery || !phantom || !clean ||
	    *index < 0 || *index >= prefix->count)
		return false;
	chunk = prefix->chunk[*index / DROP_PREFIX_CHUNK_FRAMES];
	frame = *index % DROP_PREFIX_CHUNK_FRAMES;
	if (!chunk ||
	    memcmp(command, &chunk->frame[frame].command,
	           sizeof(*command)) != 0)
		return false;
	*phantom = chunk->frame[frame].phantom;
	*clean = chunk->frame[frame].clean;
	(*index)++;
	return true;
}

static qboolean Drop_PrefixRecord(drop_prefix_cache_t *prefix, int *index,
	const usercmd_t *command, qboolean recovery,
	const sg_phantom_t *phantom, qboolean clean)
{
	drop_prefix_chunk_t *chunk;
	int slot, frame;

	if (!prefix || !index || !command || recovery || !phantom ||
	    *index != prefix->count || prefix->count >= DROP_PREFIX_MAX_FRAMES)
		return false;
	slot = *index / DROP_PREFIX_CHUNK_FRAMES;
	frame = *index % DROP_PREFIX_CHUNK_FRAMES;
	chunk = prefix->chunk[slot];
	if (!chunk)
	{
		chunk = sg_host.game_alloc(sizeof(*chunk));
		if (!chunk)
			return false;
		prefix->chunk[slot] = chunk;
	}
	chunk->frame[frame].command = *command;
	chunk->frame[frame].phantom = *phantom;
	chunk->frame[frame].clean = clean;
	prefix->count++;
	(*index)++;
	return true;
}

/* The point probe only proposes a lip. The player-sized rollout below is the
 * authority on whether that proposal is executable. */
static qboolean Drop_FindLip(vec3_t src, vec3_t dir, float limit, vec3_t lip)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t probe, down, last = { 0.0f, 0.0f, 0.0f };
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
static void Drop_ReplayContacts(const sg_drop_replay_state_t *state,
	const sg_phantom_t *ph, vec3_t destination,
	qboolean *arrival_contact, qboolean *recovery_contact)
{
	vec3_t delta;
	float horizontal2;
	int next_ms;
	qboolean airborne_after, arrival_gate, recovery_gate;

	*arrival_contact = false;
	*recovery_contact = false;

	next_ms = state->progress.elapsed_ms + SG_REPLAY_STEP_MS;
	airborne_after = state->airborne ||
		(state->walkoff && !ph->groundentity);
	if (ph->door_passed ||
	    (state->spec.destination_water && airborne_after &&
	     (next_ms % SG_REPLAY_FRAME_MS) == 0 &&
	     ph->waterlevel > 0 && ph->waterlevel < 3) ||
	    ph->origin[2] < destination[2] - SG_REPLAY_DROP_BELOW_Z ||
	    next_ms >= SG_REPLAY_DROP_TOTAL_MS ||
	    (next_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    Drop_ReplayHarmfulLiquid(ph))
		return;

	VectorSubtract(destination, ph->origin, delta);
	horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
	arrival_gate = state->walkoff && airborne_after &&
		horizontal2 < SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
		delta[2] > -SG_REPLAY_ARRIVE_Z &&
		delta[2] < SG_REPLAY_ARRIVE_Z &&
		(state->spec.destination_water ? ph->waterlevel == 3 :
		 (ph->groundentity || ph->waterlevel >= 2));
	if (arrival_gate)
	{
		*arrival_contact = Prove_Contact(ph->origin, destination);
		if (*arrival_contact)
			return;
	}
	recovery_gate = state->walkoff && airborne_after &&
		!state->spec.destination_water && ph->groundentity &&
		ph->waterlevel == 0 &&
		horizontal2 < SG_RUNE_PROOF_DROP_RECOVERY_RADIUS *
		                  SG_RUNE_PROOF_DROP_RECOVERY_RADIUS &&
		delta[2] > -SG_RUNE_PROOF_DROP_RECOVERY_Z &&
		delta[2] < SG_RUNE_PROOF_DROP_RECOVERY_Z;
	if (recovery_gate)
		*recovery_contact = Prove_Contact(ph->origin, destination);
}

static void Drop_ReplayObservation(const sg_phantom_t *ph,
	qboolean destination_water, qboolean arrival_contact,
	qboolean recovery_contact,
	sg_replay_observation_t *observation)
{
	memset(observation, 0, sizeof(*observation));
	observation->contact_clear = arrival_contact;
	/* SG_OracleRunWorld already rejects non-world support. */
	observation->ground_support_valid = true;
	observation->drop_arrival_contact_clear = arrival_contact;
	observation->drop_recovery_contact_clear = recovery_contact;
	observation->drop_recovery_admitted = !destination_water;
	observation->drop_landing_observed =
		ph->groundentity || ph->waterlevel >= 2;
	observation->door_passed = ph->door_passed;
}

/* One exact source-to-lip-to-destination attempt. All bookkeeping is local:
 * ProveDrop commits only the candidate that wins, or one final failure record
 * when none wins, so an earlier compass miss cannot leak into a later link. */
static qboolean Drop_Rollout(vec3_t src, vec3_t dst, vec3_t lip, byte heading,
                             qboolean require_deep_water,
                             short *cost_ms, byte *exit_speed,
	                         sg_drop_trial_t *trial,
	                         edict_t *compound_trigger,
	                         edict_t *compound_member)
{
	sg_phantom_t ph;
	sg_drop_replay_spec_t spec;
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	usercmd_t cmd;
	qboolean arrival_contact, recovery_contact;
	qboolean outside_before = true;
	drop_prefix_cache_t *prefix;
	int prefix_index = 0;
	int last_sweep_contact_ms = 0;

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
	Drop_ReplayObservation(&ph, require_deep_water, false, false,
	                       &observation);
	status = SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f);
	prefix = drop_prefix_cache_enabled && !compound_trigger
	    ? Drop_PrefixCacheGet(src, lip, heading) : NULL;
	if (compound_trigger)
		outside_before = SG_DeclaredDoorOutsideSweep(compound_trigger,
		    ph.origin);
	while (status == SG_REPLAY_RUNNING)
	{
		vec3_t before;
		qboolean clean;

		status = SG_DropReplayPreStep(&state, &pose, &cmd);
		if (state.walkoff)
			trial->crossed = true;
		if (status != SG_REPLAY_RUNNING)
			break;
		VectorCopy(ph.origin, before);
		/* Before recovery, source, lip, and heading completely determine each
		 * command and Pmove result.  The synchronous generator does not advance
		 * the world between pairs.  Re-run destination contact and the reducer;
		 * a command mismatch or destination-directed recovery returns to native
		 * Pmove at the exact cached pose. */
		if (!Drop_PrefixReplay(prefix, &prefix_index, &cmd, state.recovery,
		                       &ph, &clean))
		{
			if (prefix && !state.recovery && prefix_index < prefix->count)
				prefix = NULL;
			clean = compound_trigger
			    ? SG_OracleRunCompoundWorld(&ph, &cmd, 1,
			          compound_trigger, compound_member)
			    : SG_OracleRunWorld(&ph, &cmd, 1);
			if (prefix && !state.recovery &&
			    !Drop_PrefixRecord(prefix, &prefix_index, &cmd, false,
			                       &ph, clean))
				prefix = NULL;
		}
		if (!clean)
			break;
		Drop_ReplayPose(&ph, &pose);
		Drop_ReplayContacts(&state, &ph, dst, &arrival_contact,
		                    &recovery_contact);
		Drop_ReplayObservation(&ph, require_deep_water, arrival_contact,
		                       recovery_contact, &observation);
		status = SG_DropReplayPostStep(&state, &pose, &observation);
		if (compound_trigger)
		{
			qboolean crossed_sweep = SG_DeclaredDoorCrossesSweep(
			    compound_trigger, before, ph.origin);
			qboolean outside = SG_DeclaredDoorOutsideSweep(
			    compound_trigger, ph.origin);

			if (trial->sweep_clear_ms)
			{
				if (crossed_sweep || !outside)
				{
					trial->reason = SG_REPLAY_REASON_CONTAMINATED;
					return false;
				}
			}
			else
			{
				if (crossed_sweep || !outside_before || !outside)
					last_sweep_contact_ms = state.progress.elapsed_ms;
				if ((state.progress.elapsed_ms % SG_REPLAY_FRAME_MS) == 0 &&
				    last_sweep_contact_ms > 0 && outside)
					trial->sweep_clear_ms = state.progress.elapsed_ms;
			}
			outside_before = outside;
		}
		if (ph.groundentity &&
		    state.progress.reason != SG_REPLAY_REASON_DOOR_PASSED &&
		    state.progress.reason != SG_REPLAY_REASON_RECOVERY_LOST &&
		    state.progress.reason != SG_REPLAY_REASON_SHALLOW_WATER_CONTACT &&
		    state.progress.reason != SG_REPLAY_REASON_BELOW_DESTINATION)
			trial->landed++;
	}
	if (state.progress.reason == SG_REPLAY_REASON_APPROACH_TIMEOUT)
		trial->fenced = true;
	trial->reason = state.progress.reason;
	trial->end = ph;
	if (status != SG_REPLAY_ARRIVED ||
	    (compound_trigger && !trial->sweep_clear_ms))
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

static qboolean ProveDropPoints(const vec3_t src, const vec3_t dst,
	qboolean destination_water, float lip_limit, vec3_t lip_out,
	byte *heading_out,
	short *cost_ms, byte *exit_speed)
{
	vec3_t source, destination, dir, lip;
	float horiz, limit;
	int e8, tries, candidates = 0;
	short trial_cost = 0;
	byte trial_exit = 0, trial_heading = 0;
	qboolean direct;
	sg_drop_trial_t trial, last_trial;

	if (!src || !dst || !isfinite(lip_limit) || lip_limit < 4.0f ||
	    lip_limit > SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX || !lip_out ||
	    !heading_out || !cost_ms || !exit_speed)
		return false;
	VectorCopy(src, source);
	VectorCopy(dst, destination);
	if (destination_water &&
	    (sg_host.pointcontents(destination) &
	     (CONTENTS_SLIME | CONTENTS_LAVA)))
		return false;       /* liquid movement is shared; survival is not */
	dir[0] = destination[0] - source[0];
	dir[1] = destination[1] - source[1];
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
			limit = lip_limit < 192.0f ? lip_limit : 192.0f;
		}
		else
		{
			limit = horiz + 64.0f;
			if (limit > lip_limit)
				limit = lip_limit;
		}
		if (!Drop_FindLip(source, dir, limit, lip))
			continue;

		candidates++;
		trial_heading = Heading_Quantize(dir[0], dir[1]);
		if (Drop_Rollout(source, destination, lip, trial_heading,
		                 destination_water,
		                 &trial_cost, &trial_exit, &trial, NULL, NULL))
		{
			*heading_out = trial_heading;
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

static qboolean ProveDrop(int from, int to, vec3_t lip_out,
                          short *cost_ms, byte *exit_speed)
{
	byte heading;

	Rune_TelemetryAdd(&gen_telemetry.prover_calls, 1U);
	if ((gen_seeds[from].flags & RSF_WATER) || !gen_source_stable[from])
		return false;
	if (!ProveDropPoints(gen_seeds[from].origin, gen_seeds[to].origin,
	        (gen_seeds[to].flags & RSF_WATER) != 0, 256.0f, lip_out, &heading,
	        cost_ms, exit_speed))
		return false;
	dd_last_heading = heading;
	return true;
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
	l->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	return true;
}

static const rune_mechanism_node_t *Mechanism_Node(uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high = gen_mechanism_catalog.num_nodes;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		uint32_t candidate = gen_mechanism_catalog.nodes[middle].key;

		if (candidate < key)
			low = middle + 1U;
		else
			high = middle;
	}
	return low < gen_mechanism_catalog.num_nodes &&
	       gen_mechanism_catalog.nodes[low].key == key
		? &gen_mechanism_catalog.nodes[low] : NULL;
}

static uint32_t Mechanism_EntityKey(const edict_t *entity)
{
	ptrdiff_t key;

	if (!entity || !g_edicts)
		return SG_MECH_NO_KEY;
	key = entity - g_edicts;
	return key > 0 && key < globals.num_edicts &&
	       Mechanism_Node((uint32_t)key)
		? (uint32_t)key : SG_MECH_NO_KEY;
}

static uint32_t Mechanism_OwnedEntry(uint32_t mover_key, uint16_t kind)
{
	uint32_t i;
	uint32_t match = SG_MECH_NO_KEY;

	for (i = 0U; i < gen_mechanism_catalog.num_nodes; i++)
	{
		const rune_mechanism_node_t *node =
			&gen_mechanism_catalog.nodes[i];

		if (node->kind != kind || node->owner_key != mover_key)
			continue;
		if (match != SG_MECH_NO_KEY)
			return SG_MECH_NO_KEY;
		match = node->key;
	}
	return match;
}

static const rune_mechanism_edge_t *Mechanism_InventoryEdge(uint32_t from_key,
	uint16_t kind, uint16_t ordinal)
{
	uint32_t i;

	for (i = 0U; i < gen_mechanism_catalog.num_edges; i++)
	{
		const rune_mechanism_edge_t *edge =
			&gen_mechanism_catalog.edges[i];

		if (edge->from_key == from_key && edge->kind == kind &&
		    edge->ordinal == ordinal)
			return edge;
	}
	return NULL;
}

static uint32_t Mechanism_TriggerCount(void)
{
	uint32_t count = 0U;
	uint32_t i;

	for (i = 0U; i < gen_mechanism_catalog.num_nodes; i++)
		switch (gen_mechanism_catalog.nodes[i].kind)
		{
		case SG_MECH_NODE_TRIGGER:
		case SG_MECH_NODE_BUTTON:
		case SG_MECH_NODE_RELAY:
		case SG_MECH_NODE_AUTO_DOOR_TRIGGER:
		case SG_MECH_NODE_PLATFORM_TRIGGER:
		case SG_MECH_NODE_ELEVATOR:
		case SG_MECH_NODE_PUSH:
		case SG_MECH_NODE_TELEPORT_TRIGGER:
		case SG_MECH_NODE_OTHER_TRIGGER:
			count++;
			break;
		default:
			break;
		}
	return count;
}

static qboolean Mechanism_Bind(rune_link_t *link, uint32_t entry_key,
	uint32_t mover_key, uint32_t destination_key, uint32_t egress_key,
	uint16_t controller_kind, uint16_t expected_members, uint32_t cooldown_ms)
{
	sg_mechanism_plan_binding_t *binding;
	qboolean push = controller_kind == SG_MECHANISM_CONTROLLER_PUSH;

	if (!link || !gen_mechanism_bindings ||
	    gen_num_mechanism_bindings >= (uint32_t)LINK_MAX ||
	    !Mechanism_Node(entry_key) ||
	    (!push && !Mechanism_Node(mover_key)) ||
	    (push && (mover_key != SG_MECH_NO_KEY ||
	              destination_key != SG_MECH_NO_KEY ||
	              expected_members != 1U || cooldown_ms != 0U)) ||
	    (destination_key != SG_MECH_NO_KEY &&
	     !Mechanism_Node(destination_key)) ||
	    (egress_key != SG_MECH_NO_KEY && !Mechanism_Node(egress_key)) ||
	    expected_members == 0U ||
	    expected_members > RUNE_MAX_MECHANISM_MEMBERS)
	{
		gen_mechanism_failed = true;
		return false;
	}
	binding = &gen_mechanism_bindings[gen_num_mechanism_bindings];
	memset(binding, 0, sizeof(*binding));
	binding->entry_key = entry_key;
	binding->mover_key = mover_key;
	binding->destination_key = destination_key;
	binding->egress_key = egress_key;
	binding->controller_kind = controller_kind;
	binding->expected_members = expected_members;
	binding->cooldown_ms = cooldown_ms;
	link->mechanism_plan = gen_num_mechanism_bindings++;
	return true;
}

static qboolean Mechanism_BindPush(rune_link_t *link, edict_t *trigger)
{
	uint32_t entry_key = Mechanism_EntityKey(trigger);

	return entry_key != SG_MECH_NO_KEY &&
	       Mechanism_Bind(link, entry_key, SG_MECH_NO_KEY, SG_MECH_NO_KEY,
	           SG_MECH_NO_KEY, SG_MECHANISM_CONTROLLER_PUSH, 1U, 0U);
}

static qboolean Mechanism_TrainRoute(uint32_t train_key,
	uint32_t *closed_key_out, uint32_t *open_key_out)
{
	const rune_mechanism_edge_t *train_open;
	const rune_mechanism_edge_t *open_closed;
	const rune_mechanism_edge_t *closed_open;
	uint32_t open_key;
	uint32_t closed_key;

	if (closed_key_out) *closed_key_out = SG_MECH_NO_KEY;
	if (open_key_out) *open_key_out = SG_MECH_NO_KEY;
	if (!closed_key_out || !open_key_out ||
	    !(train_open = Mechanism_InventoryEdge(train_key,
	        SG_MECH_EDGE_ROUTE_TARGET, 0U)) ||
	    Mechanism_InventoryEdge(train_key, SG_MECH_EDGE_ROUTE_TARGET, 1U))
		return false;
	open_key = train_open->to_key;
	if (!(open_closed = Mechanism_InventoryEdge(open_key,
	        SG_MECH_EDGE_ROUTE_TARGET, 0U)) ||
	    Mechanism_InventoryEdge(open_key, SG_MECH_EDGE_ROUTE_TARGET, 1U))
		return false;
	closed_key = open_closed->to_key;
	if (!(closed_open = Mechanism_InventoryEdge(closed_key,
	        SG_MECH_EDGE_ROUTE_TARGET, 0U)) ||
	    Mechanism_InventoryEdge(closed_key, SG_MECH_EDGE_ROUTE_TARGET, 1U) ||
	    closed_open->to_key != open_key)
		return false;
	*closed_key_out = closed_key;
	*open_key_out = open_key;
	return true;
}

static qboolean Mechanism_BindTrain(rune_link_t *link, edict_t *button,
	edict_t *train, edict_t *closed, edict_t *open, uint32_t opening_bound_ms,
	uint16_t controller_kind)
{
	uint32_t button_key = Mechanism_EntityKey(button);
	uint32_t train_key = Mechanism_EntityKey(train);
	uint32_t closed_key = Mechanism_EntityKey(closed);
	uint32_t open_key = Mechanism_EntityKey(open);
	const rune_mechanism_edge_t *target;
	uint32_t route_closed;
	uint32_t route_open;

	if ((controller_kind != SG_MECHANISM_CONTROLLER_TRAIN &&
	     controller_kind != SG_MECHANISM_CONTROLLER_TRAIN_SHOOT) ||
	    opening_bound_ms == 0U || opening_bound_ms > RUNE_MAX_COST_MS ||
	    button_key == SG_MECH_NO_KEY || train_key == SG_MECH_NO_KEY ||
	    closed_key == SG_MECH_NO_KEY || open_key == SG_MECH_NO_KEY ||
	    !(target = Mechanism_InventoryEdge(button_key, SG_MECH_EDGE_TARGET,
	        0U)) ||
	    Mechanism_InventoryEdge(button_key, SG_MECH_EDGE_TARGET, 1U) ||
	    target->to_key != train_key ||
	    !Mechanism_TrainRoute(train_key, &route_closed, &route_open) ||
	    route_closed != closed_key || route_open != open_key)
		goto fail;
	return Mechanism_Bind(link, button_key, train_key, closed_key, open_key,
	    controller_kind, 1U, opening_bound_ms);

fail:
	gen_mechanism_failed = true;
	return false;
}

static qboolean Mechanism_BindShootDoor(rune_link_t *link, edict_t *master,
	uint32_t opening_bound_ms)
{
	edict_t *member;
	uint32_t master_key = Mechanism_EntityKey(master);
	uint16_t members = 0U;

	if (!link || !master || master_key == SG_MECH_NO_KEY ||
	    opening_bound_ms == 0U || opening_bound_ms > RUNE_MAX_COST_MS)
		goto fail;
	for (member = master; member; member = member->teamchain)
	{
		const rune_mechanism_node_t *node =
			Mechanism_Node(Mechanism_EntityKey(member));

		if (!node || (node->kind != SG_MECH_NODE_DOOR_MASTER &&
		        node->kind != SG_MECH_NODE_DOOR_MEMBER) ||
		    (node->flags & (SG_MECH_NODEF_MOVER |
		         SG_MECH_NODEF_SHOOTABLE)) !=
		        (SG_MECH_NODEF_MOVER | SG_MECH_NODEF_SHOOTABLE) ||
		    node->team_master_key != master_key ||
		    !SG_MechCatalogEntityExecutionMatches(node->key, node,
		        SG_MECHANISM_CONTROLLER_TRAIN_SHOOT) || members == UINT16_MAX)
			goto fail;
		members++;
	}
	if (members == 0U)
		goto fail;
	return Mechanism_Bind(link, master_key, master_key, SG_MECH_NO_KEY,
		SG_MECH_NO_KEY, SG_MECHANISM_CONTROLLER_TRAIN_SHOOT, members,
		opening_bound_ms);

fail:
	gen_mechanism_failed = true;
	return false;
}

static qboolean Mechanism_BindPlatform(rune_link_t *link, edict_t *platform,
	edict_t *approach_door, edict_t *egress_door,
	uint16_t expected_members)
{
	uint32_t mover_key = Mechanism_EntityKey(platform);
	uint32_t entry_key = Mechanism_OwnedEntry(mover_key,
		SG_MECH_NODE_PLATFORM_TRIGGER);
	const rune_mechanism_node_t *entry_node = Mechanism_Node(entry_key);
	uint32_t cooldown = 0U;

	if (entry_node &&
	    (entry_node->touch_callback == SG_MECH_CALLBACK_TOUCH_MULTI ||
	     entry_node->touch_callback == SG_MECH_CALLBACK_BUTTON_TOUCH))
		cooldown = entry_node->wait_ms > RUNE_MAX_COST_MS
		    ? RUNE_MAX_COST_MS : entry_node->wait_ms;

	return mover_key != SG_MECH_NO_KEY && entry_key != SG_MECH_NO_KEY &&
	       entry_node &&
	       Mechanism_Bind(link, entry_key, mover_key,
	           approach_door ? Mechanism_EntityKey(approach_door) :
	               SG_MECH_NO_KEY,
	           egress_door ? Mechanism_EntityKey(egress_door) :
	               SG_MECH_NO_KEY,
		SG_MECHANISM_CONTROLLER_PLATFORM, expected_members, cooldown);
}

static qboolean Mechanism_BindTeleport(rune_link_t *link, edict_t *pad,
	edict_t *destination)
{
	uint32_t mover_key = Mechanism_EntityKey(pad);
	uint32_t destination_key = Mechanism_EntityKey(destination);
	uint32_t entry_key = Mechanism_OwnedEntry(mover_key,
		SG_MECH_NODE_TELEPORT_TRIGGER);

	return mover_key != SG_MECH_NO_KEY && entry_key != SG_MECH_NO_KEY &&
	       destination_key != SG_MECH_NO_KEY &&
	       Mechanism_Bind(link, entry_key, mover_key, destination_key,
		SG_MECH_NO_KEY,
		SG_MECHANISM_CONTROLLER_TELEPORT, 1U, 0U);
}

static qboolean Mechanism_BindDoor(rune_link_t *link, edict_t *trigger)
{
	edict_t *members[RUNE_MAX_MECHANISM_MEMBERS];
	uint32_t primary = SG_MECH_NO_KEY;
	uint32_t entry_key;
	uint32_t cooldown;
	uint16_t controller;
	const rune_mechanism_node_t *entry_node;
	int count;
	int i;

	if (!link || !trigger)
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
		RUNE_MAX_MECHANISM_MEMBERS);
	entry_key = Mechanism_EntityKey(trigger);
	if (count <= 0 || entry_key == SG_MECH_NO_KEY)
		goto fail;
	entry_node = Mechanism_Node(entry_key);
	if (!entry_node)
		goto fail;
	for (i = 0; i < count; i++)
	{
		edict_t *master = members[i]->teammaster
			? members[i]->teammaster : members[i];
		uint32_t key = Mechanism_EntityKey(master);

		if (key == SG_MECH_NO_KEY)
			goto fail;
		if (primary == SG_MECH_NO_KEY || key < primary)
			primary = key;
	}
	if (entry_node->touch_callback == SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER)
	{
		controller = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
		cooldown = 1000U;
	}
	else if (entry_node->touch_callback == SG_MECH_CALLBACK_TOUCH_MULTI)
	{
		int wait_ms = entry_node->wait_ms;

		if (wait_ms <= 0)
			goto fail;
		controller = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
		cooldown = wait_ms > RUNE_MAX_COST_MS
		    ? (uint32_t)RUNE_MAX_COST_MS : (uint32_t)wait_ms;
	}
	else
		goto fail;
	return Mechanism_Bind(link, entry_key, primary, SG_MECH_NO_KEY,
		SG_MECH_NO_KEY,
		controller, (uint16_t)count, cooldown);

fail:
	gen_mechanism_failed = true;
	return false;
}

static qboolean Mechanism_BindButtonDoor(rune_link_t *link, edict_t *button)
{
	const rune_mechanism_node_t *entry;
	uint32_t entry_key = Mechanism_EntityKey(button);
	uint32_t mover_key = SG_MECH_NO_KEY;
	uint32_t team_members = 0U;
	uint32_t target_count = 0U;
	uint32_t i;

	if (!link || entry_key == SG_MECH_NO_KEY)
		goto fail;
	entry = Mechanism_Node(entry_key);
	if (!entry || entry->kind != SG_MECH_NODE_BUTTON ||
	    entry->touch_callback != SG_MECH_CALLBACK_BUTTON_TOUCH ||
	    entry->use_callback != SG_MECH_CALLBACK_BUTTON_USE ||
	    entry->wait_ms <= 0)
		goto fail;
	/* A mapper commonly gives every brush in a canonical door team the same
	 * targetname.  Stock G_UseTargets calls the master first, opening the full
	 * team, then later same-team slave calls are no-ops.  Bind that exact
	 * ordered one-team fanout rather than narrowing BUTTON_DOOR to an unteamed
	 * single brush. */
	for (;; target_count++)
	{
		const rune_mechanism_edge_t *target = Mechanism_InventoryEdge(
			entry_key, SG_MECH_EDGE_TARGET, target_count);
		const rune_mechanism_node_t *destination;

		if (!target)
			break;
		destination = Mechanism_Node(target->to_key);
		if (!destination)
			goto fail;
		if (destination->kind == SG_MECH_NODE_DOOR_MASTER)
		{
			if (mover_key != SG_MECH_NO_KEY)
				goto fail;
			mover_key = destination->key;
		}
		else if (destination->kind != SG_MECH_NODE_DOOR_MEMBER ||
		         mover_key == SG_MECH_NO_KEY ||
		         destination->team_master_key != mover_key)
			goto fail;
	}
	if (target_count == 0U || mover_key == SG_MECH_NO_KEY)
		goto fail;
	for (i = 0U; i < gen_mechanism_catalog.num_edges; i++)
	{
		const rune_mechanism_edge_t *edge =
			&gen_mechanism_catalog.edges[i];

		if (edge->from_key == mover_key && edge->kind == SG_MECH_EDGE_TEAM)
			team_members++;
	}
	if (team_members + 1U > RUNE_MAX_MECHANISM_MEMBERS)
		goto fail;
	return Mechanism_Bind(link, entry_key, mover_key, SG_MECH_NO_KEY,
		SG_MECH_NO_KEY,
		SG_MECHANISM_CONTROLLER_BUTTON_DOOR,
		(uint16_t)(team_members + 1U), (uint32_t)entry->wait_ms);

fail:
	gen_mechanism_failed = true;
	return false;
}



#define SG_WATER_SPACING	64.0f		/* the water lattice, 3D */
#define SG_SWIM_REACH		192.0f		/* swim pairs proven within this, 3D */
#define SG_PAD_REACH		RUNE_TELEPORT_SEED_REACH

static int gen_first_water = -1;        /* index of the first water seed, -1 none */
static int gen_num_water;
static int gen_lift_links, gen_tele_links, gen_door_links, gen_push_links;
static int gen_train_links;
static int gen_button_door_links, gen_swim_links;
static int gen_door_drop_trials, gen_door_drop_proofs;
static int gen_door_drop_compound_trials, gen_door_drop_compound_proofs;
static int gen_env_drop, gen_env_hook, gen_declared_links;
static int gen_lift_down_drop, gen_lift_down_none;

/* Record the control envelope supplied by the link proof. */
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

/* Mark map-declared controller links appended after a recorded array offset. */
static void Link_Declare_Tail(int mark)
{
	int i;

	for (i = gen_num_links - 1; i >= mark; i--)
	{
		if (gen_links[i].action != RL_LIFT &&
		    gen_links[i].action != RL_TELEPORT &&
		    gen_links[i].action != RL_DOOR &&
		    gen_links[i].action != RL_BUTTON_DOOR &&
		    gen_links[i].action != RL_PUSH &&
		    gen_links[i].action != RL_TRAIN)
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
		int key = Seed_HashKey(gen_seeds[before].origin);

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

	if (!Seed_OnPmoveGrid(p))
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

static qboolean ProveSwim(int from, int to, short *cost_ms,
	byte *exit_speed);

static int Water_ProveEdge(void *context, int from, int to,
	sg_water_proof_t *proof)
{
	short cost;
	byte exit_speed;

	(void)context;
	Rune_TelemetryAdd(&gen_telemetry.prover_calls, 1U);
	if (!ProveSwim(from, to, &cost, &exit_speed))
		return 0;
	proof->cost_ms = cost;
	proof->exit_speed = exit_speed;
	return 1;
}

static sg_water_connect_result_t Water_Connect(int from, int to)
{
	sg_water_connect_result_t result = SG_WaterForestConnect(
		&gen_water_forest, from, to, Water_ProveEdge, NULL);

	if (result == SG_WATER_CONNECT_OVERFLOW)
		gen_water_overflow = true;
	return result;
}

static void Seed_RemoveWater(int seed)
{
	int key;

	if (seed != gen_num_seeds - 1)
		return;
	key = Seed_HashKey(gen_seeds[seed].origin);
	if (hash_head[key] == seed)
		hash_head[key] = hash_next[seed];
	gen_num_seeds--;
	gen_num_water--;
}

typedef enum water_discover_result_e
{
	WATER_DISCOVER_NONE,
	WATER_DISCOVER_CONNECTED,
	WATER_DISCOVER_OVERFLOW
} water_discover_result_t;

static water_discover_result_t Seed_DiscoverWater(int from, vec3_t candidate)
{
	int nearby, destination, added = false;
	sg_water_connect_result_t result;

	if (!Seed_WaterFree(candidate))
		return WATER_DISCOVER_NONE;
	nearby = Seed_NearbyIndex(candidate);
	if (nearby == -2 ||
	    (nearby >= 0 && !(gen_seeds[nearby].flags & RSF_WATER)))
		return WATER_DISCOVER_NONE;
	if (nearby >= 0)
		destination = nearby;
	else
	{
		destination = Seed_AddWater(candidate);
		if (destination < 0)
			return gen_seed_overflow ? WATER_DISCOVER_OVERFLOW
			                         : WATER_DISCOVER_NONE;
		added = true;
	}
	result = Water_Connect(from, destination);
	if (result == SG_WATER_CONNECT_OVERFLOW)
	{
		if (added)
			Seed_RemoveWater(destination);
		return WATER_DISCOVER_OVERFLOW;
	}
	if (result == SG_WATER_CONNECT_RECORDED ||
	    result == SG_WATER_CONNECT_ALREADY)
		return WATER_DISCOVER_CONNECTED;
	if (added)
		Seed_RemoveWater(destination);
	return WATER_DISCOVER_NONE;
}

/*
 * The six lattice neighbours of a point, in three dimensions -- a water
 * volume has an inside, so up and down are directions like any other.
 * Returns false only on shared seed-capacity overflow.
 */
static qboolean Seed_WaterNeighbours(int from)
{
	static const float dirs6[6][3] = {
		{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
		{ 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
	};
	int k;

	for (k = 0; k < 6; k++)
	{
		vec3_t cand;

		Rune_TelemetryAdd(&gen_telemetry.water_scans, 1U);

		cand[0] = gen_seeds[from].origin[0] + dirs6[k][0] * SG_WATER_SPACING;
		cand[1] = gen_seeds[from].origin[1] + dirs6[k][1] * SG_WATER_SPACING;
		cand[2] = gen_seeds[from].origin[2] + dirs6[k][2] * SG_WATER_SPACING;

		if (Seed_DiscoverWater(from, cand) == WATER_DISCOVER_OVERFLOW)
			return false;
	}
	return true;
}

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
	qboolean capacity_exhausted = false;

	for (i = 0; i < dry; i++)
		if (gen_seeds[i].flags & RSF_WATER)
			existing++;
	gen_num_water = existing;

	for (i = 0; i < dry && !capacity_exhausted; i++)
	{
		if (gen_seeds[i].flags & RSF_WATER)
			continue;
		for (k = 0; k < 4 && !capacity_exhausted; k++)
		{
			for (z = 0; z < 3; z++)
			{
				vec3_t cand;
				water_discover_result_t result;

				Rune_TelemetryAdd(&gen_telemetry.water_scans, 1U);

				cand[0] = gen_seeds[i].origin[0] + around[k][0] * SG_WATER_SPACING;
				cand[1] = gen_seeds[i].origin[1] + around[k][1] * SG_WATER_SPACING;
				cand[2] = gen_seeds[i].origin[2] + 24.0f + drops[z];

				result = Seed_DiscoverWater(i, cand);
				if (result == WATER_DISCOVER_OVERFLOW)
				{
					capacity_exhausted = true;
					break;
				}
				if (result == WATER_DISCOVER_CONNECTED)
				{
					entries++;
					break;
				}
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
	for (i = 0; i < gen_num_seeds && !capacity_exhausted; i++)
	{
		if (!(gen_seeds[i].flags & RSF_WATER))
			continue;
		capacity_exhausted = !Seed_WaterNeighbours(i);
	}
	if (capacity_exhausted)
	{
		gen_water_overflow = true;
		sg_host.dprint("rune: water discovery capacity exhausted; "
		               "graph will not be written\n");
	}

	sg_host.dprint("rune: %d water seeds (%d entered from dry land)\n",
	           gen_num_water, entries);
}

/* A snapshot of links written before the sparse swim forest is published. */
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

static void Prove_Swims(void)
{
	size_t i, publishable = 0;

	if (gen_first_water < 0 || gen_water_forest.edge_count == 0)
		return;

	Link_Index_Build();
	for (i = 0; i < gen_water_forest.edge_count; i++)
	{
		sg_water_edge_t *edge = &gen_water_forest.edges[i];

		if (Link_Index_Find(edge->from, edge->to) < 0)
			publishable++;
	}
	if (publishable > (size_t)(LINK_MAX - gen_num_links))
	{
		gen_link_overflow = true;
		goto done;
	}
	for (i = 0; i < gen_water_forest.edge_count; i++)
	{
		sg_water_edge_t *edge = &gen_water_forest.edges[i];

		if (Link_Index_Find(edge->from, edge->to) >= 0)
			continue;
		if (!Link_Add(edge->from, edge->to, RL_SWIM,
		    (short)edge->proof.cost_ms, edge->proof.exit_speed))
			break;
		gen_links[gen_num_links - 1].heading_slack = 0;
		gen_swim_links++;
	}

done:
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

/* Candidate discovery is only a budget around the authoritative approach
 * replay below.  The historical 48-unit vertical budget remains exact for
 * AUTO/BUTTON and for ordinary dry DIRECT egress.  A DIRECT trigger whose
 * already-proved best egress ends in supported safe shallow water may use the
 * same 96-unit vertical discovery budget as that egress: lmctf58's four lower
 * cellar triggers have a realizable 72-unit descent from their connected dry
 * source to the only wait point that also crosses into the shallow basin.
 * Unsafe/deep water cannot enlarge this budget because it must pass the same
 * shared controller-aware liquid gate used by both egress replay call sites.
 * Discovery may inspect the best destination, but every selected destination
 * must pass this gate again immediately before its link is serialized. */
static qboolean Door_ApproachEnvelopeEligible(int controller_kind,
	int egress_destination, const vec3_t delta)
{
	float max_vertical = 48.0f;
	float horizontal2;

	if (!delta)
		return false;
	if (controller_kind == SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
	    egress_destination >= 0 && egress_destination < gen_num_seeds &&
	    gen_source_waterlevel[egress_destination] == 1 &&
	    SG_OracleDoorEgressWaterSafe(controller_kind,
	        gen_source_waterlevel[egress_destination],
	        gen_source_watertype[egress_destination]))
		max_vertical = 96.0f;
	horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
	return horizontal2 <= 320.0f * 320.0f &&
	       fabsf(delta[2]) <= max_vertical;
}

/* Select a graph-connected static endpoint for a declared mechanism.  A
 * Euclidean nearest seed is insufficient: it can be on the far side of a
 * wall, on the mover itself, or an isolated germ.  Trace the complete player
 * hull from the candidate to the authoritative body point and accept an early
 * hit only when it is the expected pad/platform -- the declared controller
 * owns that final contact.  Connectivity requirements are evaluated against
 * the already-proven ordinary/swim graph. */
static int Gen_MechanismSeedNear(vec3_t body, float horiz, float vert,
	edict_t *entry, edict_t *support, qboolean require_stable,
	qboolean require_dry,
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

		VectorSubtract(gen_seeds[i].origin, body, d);
		h2 = d[0] * d[0] + d[1] * d[1];
		if ((require_stable && !gen_source_stable[i]) ||
		    (require_dry && gen_source_waterlevel[i] != 0) ||
		    (require_incoming && !Gen_SeedHasIncoming(i)) ||
		    (require_outgoing && !Gen_SeedHasOutgoing(i)))
			continue;
		/* The declared runtime controller is intentionally planar: it walks into
		 * a pad/plat or off the top, but it neither jumps nor swims vertically.
		 * Restrict endpoints to one Pmove step-height band so the swept hull is not
		 * mistaken for proof that a body 64--128 units above/below can reach it. */
		if (fabsf(d[2]) > 16.0f)
			continue;
		if (fabsf(d[2]) > vert)
			continue;
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
		    (tr.fraction < 1.0f && tr.ent != support && tr.ent != entry))
			continue;
		if (approach_ms)
		{
			const rune_mechanism_node_t *entry_node =
			    Mechanism_Node(Mechanism_EntityKey(entry));
			qboolean proved = action == RL_LIFT && entry_node &&
			    entry_node->touch_callback == SG_MECH_CALLBACK_BUTTON_TOUCH
				? SG_OracleButtonLiftApproach(gen_seeds[i].origin, body,
				      entry, support, &trial_ms)
				: SG_OracleDeclaredApproach(gen_seeds[i].origin, body,
				      entry, support, action, &trial_ms);

			if (!proved)
				continue;
		}
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

static int Gen_LiftWaterSeed(const vec3_t bottom_body, edict_t *entry,
	edict_t *platform, int *approach_ms)
{
	int seed;
	int best = -1;
	int best_ms = INT_MAX;
	float best_distance = 1.0e30f;

	if (!bottom_body || !entry || !platform || !approach_ms)
		return -1;
	SG_OracleDoorBoundsCacheBegin();
	for (seed = 0; seed < gen_num_seeds; seed++)
	{
		sg_phantom_t phantom;
		sg_swim_proof_t proof;
		vec3_t delta;
		float distance;

		if (!(gen_seeds[seed].flags & RSF_WATER) ||
		    gen_source_waterlevel[seed] < 2 ||
		    !(gen_source_watertype[seed] & CONTENTS_WATER) ||
		    (gen_source_watertype[seed] &
		        (CONTENTS_LAVA | CONTENTS_SLIME)) ||
		    !Gen_SeedHasIncoming(seed))
			continue;
		VectorSubtract(gen_seeds[seed].origin, bottom_body, delta);
		distance = DotProduct(delta, delta);
		if (distance > SG_SWIM_REACH * SG_SWIM_REACH)
			continue;
		SG_OraclePlace(&phantom, gen_seeds[seed].origin);
		phantom.waterlevel = gen_source_waterlevel[seed];
		phantom.watertype = gen_source_watertype[seed];
		if (!SG_OracleLiftSwimApproach(&phantom, bottom_body, entry,
		        platform, 0.0f, &proof, NULL, true))
			continue;
		if (proof.arrival_ms < best_ms ||
		    (proof.arrival_ms == best_ms && distance < best_distance))
		{
			best = seed;
			best_ms = proof.arrival_ms;
			best_distance = distance;
		}
	}
	SG_OracleDoorBoundsCacheEnd();
	if (best >= 0)
		*approach_ms = best_ms;
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

static float Lift_EgressSearchRadius(float halfx, float halfy)
{
	return sqrtf(halfx * halfx + halfy * halfy) + SEED_SPACING +
	       PLAYER_HALF_WIDTH;
}


static short Plat_TravelMs(edict_t *e, const vec3_t source,
	const vec3_t destination)
{
	float speed = e->moveinfo.speed;
	float accel = e->moveinfo.accel;
	float decel = e->moveinfo.decel;
	float dist = fabsf(destination[2] - source[2]);
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

static qboolean Lift_EgressSpans(const vec3_t source,
	const vec3_t destination, const vec3_t source_body,
	const vec3_t egress_body)
{
	float travel_z;

	if (!source || !destination || !source_body || !egress_body)
		return false;
	travel_z = destination[2] - source[2];
	return (egress_body[2] - source_body[2]) * travel_z >=
	       0.5f * travel_z * travel_z;
}


static qboolean Lift_Endpoints(edict_t *mover, edict_t **entry_out,
	vec3_t source, vec3_t destination, qboolean *stock_out)
{
	uint32_t mover_key = Mechanism_EntityKey(mover);
	uint32_t entry_key;
	const rune_mechanism_node_t *mover_node;
	const rune_mechanism_node_t *entry_node;
	edict_t *entry;

	if (entry_out) *entry_out = NULL;
	if (stock_out) *stock_out = false;
	if (!mover || !entry_out || !source || !destination || !stock_out ||
	    mover_key == SG_MECH_NO_KEY ||
	    !(mover_node = Mechanism_Node(mover_key)) ||
	    mover_node->kind != SG_MECH_NODE_PLATFORM ||
	    (entry_key = Mechanism_OwnedEntry(mover_key,
	        SG_MECH_NODE_PLATFORM_TRIGGER)) == SG_MECH_NO_KEY ||
	    entry_key >= (uint32_t)globals.num_edicts ||
	    !(entry_node = Mechanism_Node(entry_key)))
		return false;
	entry = &g_edicts[entry_key];
	if (!strcmp(mover->classname, "func_plat") && !mover->targetname &&
	    entry_node->touch_callback == SG_MECH_CALLBACK_TOUCH_PLAT_CENTER)
	{
		VectorCopy(mover->pos2, source);
		VectorCopy(mover->pos1, destination);
		*stock_out = true;
	}
	else if (!strcmp(mover->classname, "func_door") &&
	         entry_node->touch_callback == SG_MECH_CALLBACK_TOUCH_MULTI &&
	         entry_node->use_callback == SG_MECH_CALLBACK_USE_MULTI &&
	         mover_node->use_callback == SG_MECH_CALLBACK_USE_DOOR &&
	         mover_node->blocked_callback == SG_MECH_CALLBACK_BLOCKED_DOOR)
	{
		VectorCopy(mover->moveinfo.start_origin, source);
		VectorCopy(mover->moveinfo.end_origin, destination);
	}
	else if (!strcmp(mover->classname, "func_door") &&
	         entry_node->touch_callback == SG_MECH_CALLBACK_BUTTON_TOUCH &&
	         entry_node->use_callback == SG_MECH_CALLBACK_BUTTON_USE &&
	         mover_node->use_callback == SG_MECH_CALLBACK_USE_DOOR &&
	         mover_node->blocked_callback == SG_MECH_CALLBACK_BLOCKED_DOOR)
	{
		VectorCopy(mover->moveinfo.start_origin, source);
		VectorCopy(mover->moveinfo.end_origin, destination);
	}
	else
		return false;
	if (fabsf(destination[2] - source[2]) < 8.0f)
		return false;
	*entry_out = entry;
	return true;
}

typedef struct
{
	edict_t *ent;
	vec3_t origin, old_origin, angles, velocity, avelocity;
	int state, linkcount;
	solid_t solid;
} door_pose_t;

static int DoorTrigger_Targets(edict_t *trigger, edict_t **doors,
	int capacity);
static int DoorTrigger_Open(edict_t *trigger, door_pose_t *saved,
	int capacity);
static void DoorPose_Restore(door_pose_t *saved, int count);
static int Door_TravelMs(edict_t *trigger);
static int Door_WaitPoints(edict_t *trigger, vec3_t *points,
	qboolean automatic);
static int Gen_CompoundLiftEgressSeed(const vec3_t top_body, float horiz,
	edict_t *plat, edict_t **trigger_out, uint16_t *member_count_out,
	int *egress_ms_out);
static int Gen_CompoundLiftDoorExit(const vec3_t body, edict_t *plat,
	edict_t **trigger_out, uint16_t *member_count_out, int *egress_ms_out);

static qboolean Lift_DoorStageDelay(edict_t *trigger,
	uint32_t *delay_ms_out, qboolean automatic)
{
	if (delay_ms_out)
		*delay_ms_out = 0U;
	if (!trigger || !delay_ms_out)
		return false;
	if (SG_DeclaredDoorDirectActivatorSafe(trigger) || (automatic &&
	    SG_DeclaredDoorActivatorSafe(trigger)))
		return true;
	return SG_DeclaredDoorDelayedActivatorSafe(trigger, delay_ms_out);
}

static qboolean Lift_DoorStageTouchMatches(edict_t *trigger,
	const vec3_t origin, qboolean automatic)
{
	uint32_t delay_ms;

	if (SG_DeclaredDoorDirectActivatorSafe(trigger) || (automatic &&
	    SG_DeclaredDoorActivatorSafe(trigger)))
		return SG_DeclaredDoorTouchMatches(trigger, origin);
	return SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms) &&
	       SG_DeclaredDelayedDoorTouchMatches(trigger, origin);
}

static qboolean Lift_DoorStageSameSet(edict_t *first, edict_t *second)
{
	uint32_t first_delay;
	uint32_t second_delay;

	if (!Lift_DoorStageDelay(first, &first_delay, false) ||
	    !Lift_DoorStageDelay(second, &second_delay, false) ||
	    first_delay != second_delay)
		return false;
	return first_delay == 0U ? SG_DeclaredDoorSameSet(first, second) :
	       SG_DeclaredDelayedDoorSameSet(first, second);
}

static qboolean Lift_DoorStageCrossesSweep(edict_t *trigger,
	const vec3_t from, const vec3_t to, qboolean automatic)
{
	uint32_t delay_ms;

	if (!Lift_DoorStageDelay(trigger, &delay_ms, automatic))
		return false;
	return delay_ms == 0U ? SG_DeclaredDoorCrossesSweep(trigger, from, to) :
	       SG_DeclaredDelayedDoorCrossesSweep(trigger, from, to);
}

static int Lift_CompoundApproach(edict_t *entry, edict_t *support,
	const vec3_t bottom_body, edict_t **door_out,
	uint16_t *expected_members_out, vec3_t wait_out,
	int *approach_ms_out, qboolean stock)
{
	#define LIFT_DOOR_WAIT_MAX 64
	edict_t *best_door = NULL;
	uint16_t best_members = 0U;
	int best_seed = -1;
	int best_ms = INT_MAX;
	vec3_t best_wait = { 0.0f, 0.0f, 0.0f };
	int trigger_index;

	if (door_out) *door_out = NULL;
	if (expected_members_out) *expected_members_out = 0U;
	if (approach_ms_out) *approach_ms_out = 0;
	if (wait_out) VectorClear(wait_out);
	if (!entry || !support || !bottom_body || !door_out ||
	    !expected_members_out || !wait_out || !approach_ms_out)
		return -1;
	for (trigger_index = 1; trigger_index < globals.num_edicts;
	     trigger_index++)
	{
		edict_t *trigger = &g_edicts[trigger_index];
		const rune_mechanism_node_t *trigger_node;
		edict_t *members[RUNE_MAX_MECHANISM_MEMBERS];
		vec3_t wait_points[LIFT_DOOR_WAIT_MAX];
		int member_count;
		int wait_count;
		int wait_index;
		int travel_ms;
		uint32_t delay_ms;

		trigger_node = Mechanism_Node(Mechanism_EntityKey(trigger));
		if (!trigger_node ||
		    (trigger_node->kind != SG_MECH_NODE_TRIGGER && (!stock ||
		     trigger_node->kind != SG_MECH_NODE_AUTO_DOOR_TRIGGER)) ||
		    !Lift_DoorStageDelay(trigger, &delay_ms, stock) || delay_ms > INT_MAX)
			continue;
		member_count = DoorTrigger_Targets(trigger, members,
			RUNE_MAX_MECHANISM_MEMBERS);
		travel_ms = Door_TravelMs(trigger);
		if (member_count <= 0 || member_count >= RUNE_MAX_MECHANISM_MEMBERS ||
		    travel_ms <= 0)
			continue;
		wait_count = Door_WaitPoints(trigger, wait_points, stock);
		for (wait_index = 0; wait_index < wait_count; wait_index++)
		{
			door_pose_t saved[RUNE_MAX_MECHANISM_MEMBERS];
			int seed;

			if (!Lift_DoorStageCrossesSweep(trigger,
			        wait_points[wait_index], bottom_body, stock))
				continue;
			for (seed = 0; seed < gen_num_seeds; seed++)
			{
				vec3_t delta;
				int door_ms;
				int carrier_ms;
				int pose_count;
				int total_ms;

				if (!gen_source_stable[seed] ||
				    gen_source_waterlevel[seed] != 0 ||
				    !Gen_SeedHasIncoming(seed))
					continue;
				VectorSubtract(gen_seeds[seed].origin,
				    wait_points[wait_index], delta);
				if (!Door_ApproachEnvelopeEligible(
				        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR,
				        -1, delta) ||
				    !SG_OracleDeclaredDoorApproach(gen_seeds[seed].origin,
				        wait_points[wait_index], trigger, &door_ms,
				        &carrier_ms))
					continue;
				pose_count = DoorTrigger_Open(trigger, saved,
					RUNE_MAX_MECHANISM_MEMBERS);
				if (pose_count <= 0)
					continue;
				carrier_ms = 0;
				if (!SG_OracleDeclaredCompoundLiftApproach(
				        wait_points[wait_index], bottom_body, entry, support,
				        trigger, &carrier_ms))
					carrier_ms = -1;
				DoorPose_Restore(saved, pose_count);
				if (carrier_ms < 0)
					continue;
				total_ms = door_ms + (int)delay_ms + travel_ms + carrier_ms;
				if (total_ms < best_ms)
				{
					best_ms = total_ms;
					best_seed = seed;
					best_door = trigger;
					best_members = (uint16_t)(member_count + 1);
					VectorCopy(wait_points[wait_index], best_wait);
				}
			}
		}
	}
	if (best_seed >= 0)
	{
		*door_out = best_door;
		*expected_members_out = best_members;
		VectorCopy(best_wait, wait_out);
		*approach_ms_out = best_ms;
	}
	return best_seed;
	#undef LIFT_DOOR_WAIT_MAX
}


static void Link_Plats(void)
{
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts; i++)
	{
		edict_t *entry;
		vec3_t source, destination, anchor, bottom_body, top_body;
		vec3_t saved_origin, saved_old_origin, saved_velocity;
		float halfx, halfy, horiz;
		int st_top = -1, before, approach = -1;
		int approach_ms = 0, egress_ms = 0, saved_state, saved_linkcount;
		int dispatch_ms = 0, lower_ms = 0, lower_seed = -1;
		edict_t *lower_door = NULL;
		uint16_t lower_members = 0U;
		int saved_solid;
		int total_cost;
		qboolean stock;
		edict_t *approach_door = NULL;
		edict_t *egress_door = NULL;
		uint16_t expected_members = 1U;
		vec3_t approach_wait;
		short cost;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (!Lift_Endpoints(e, &entry, source, destination, &stock))
			continue;
		halfx = (e->maxs[0] - e->mins[0]) * 0.5f;
		halfy = (e->maxs[1] - e->mins[1]) * 0.5f;
		horiz = Lift_EgressSearchRadius(halfx, halfy);

		VectorCopy(e->s.origin, saved_origin);
		VectorCopy(e->s.old_origin, saved_old_origin);
		VectorCopy(e->velocity, saved_velocity);
		saved_state = e->moveinfo.state;
		saved_solid = e->solid;
		saved_linkcount = e->linkcount;
		e->solid = SOLID_BSP;
		VectorCopy(source, e->s.origin);
		VectorCopy(source, e->s.old_origin);
		VectorClear(e->velocity);
		e->moveinfo.state = SG_PLAT_STATE_BOTTOM;
		sg_host.linkentity(e);
		if (SG_LiftRest(entry, e, NULL, bottom_body))
			approach = Gen_MechanismSeedNear(bottom_body, horiz, 64.0f,
			    entry, e, true, true, true, false, RL_LIFT, &approach_ms);
		if (approach < 0)
			approach = Lift_CompoundApproach(entry, e, bottom_body,
				&approach_door, &expected_members, approach_wait,
				&approach_ms, stock);
		if (approach < 0 && stock)
			approach = Gen_LiftWaterSeed(bottom_body, entry, e,
			    &approach_ms);
		if (approach_door)
			VectorCopy(approach_wait, anchor);
		else if (stock)
		{
			anchor[0] = source[0] + (e->mins[0] + e->maxs[0]) * 0.5f;
			anchor[1] = source[1] + (e->mins[1] + e->maxs[1]) * 0.5f;
			anchor[2] = source[2] + e->maxs[2];
		}
		else
			VectorCopy(bottom_body, anchor);
		VectorCopy(destination, e->s.origin);
		VectorCopy(destination, e->s.old_origin);
		VectorClear(e->velocity);
		e->moveinfo.state = SG_PLAT_STATE_TOP;
		sg_host.linkentity(e);
		if (approach >= 0 && SG_LiftRest(entry, e, NULL, top_body))
			st_top = Gen_LiftEgressSeed(top_body, horiz, e, &egress_ms);
		if (st_top < 0 && approach >= 0 && approach_door)
		{
			uint16_t egress_members = 0U;

			st_top = Gen_CompoundLiftEgressSeed(top_body, horiz, e,
				&egress_door, &egress_members, &egress_ms);
			if (st_top >= 0)
			{
				edict_t *bottom_members[RUNE_MAX_MECHANISM_MEMBERS];
				edict_t *top_members[RUNE_MAX_MECHANISM_MEMBERS];
				int bottom_count = DoorTrigger_Targets(approach_door,
					bottom_members, RUNE_MAX_MECHANISM_MEMBERS);
				int top_count = DoorTrigger_Targets(egress_door,
					top_members, RUNE_MAX_MECHANISM_MEMBERS);
				int bottom_index;
				int top_index;

				if (bottom_count <= 0 || top_count <= 0 ||
				    top_count != (int)egress_members ||
				    1 + bottom_count + top_count >
				        RUNE_MAX_MECHANISM_MEMBERS)
					st_top = -1;
				for (bottom_index = 0; st_top >= 0 &&
				     bottom_index < bottom_count; bottom_index++)
					for (top_index = 0; top_index < top_count; top_index++)
						if (bottom_members[bottom_index] ==
						    top_members[top_index])
							st_top = -1;
				if (st_top >= 0)
				{
					expected_members = (uint16_t)(1 + bottom_count +
						top_count);
				}
			}
		}
		if (stock && st_top >= 0)
		{
			VectorCopy(source, e->s.origin);
			VectorCopy(source, e->s.old_origin);
			VectorClear(e->velocity);
			e->moveinfo.state = SG_PLAT_STATE_BOTTOM;
			sg_host.linkentity(e);
			if (SG_OracleDeclaredApproach(gen_seeds[st_top].origin,
			        top_body, entry, e, RL_LIFT, &dispatch_ms))
				lower_seed = Gen_CompoundLiftDoorExit(bottom_body, e,
				    &lower_door, &lower_members, &lower_ms);
		}
		VectorCopy(saved_origin, e->s.origin);
		VectorCopy(saved_old_origin, e->s.old_origin);
		VectorCopy(saved_velocity, e->velocity);
		e->moveinfo.state = saved_state;
		e->solid = saved_solid;
		sg_host.linkentity(e);
		/* Linkentity necessarily increments this generation counter. No server
		 * frame ran during the synchronous relocation, so restore the original
		 * value too; otherwise live riders are spuriously detached after `sv rune`. */
		e->linkcount = saved_linkcount;
		if (approach < 0)
		{
			sg_host.dprint("rune: lift at (%.0f %.0f %.0f) unlinked, "
			               "no static-world staging seed\n",
			               source[0], source[1], source[2]);
			continue;
		}
		if (st_top < 0 || approach == st_top)
		{
			sg_host.dprint("rune: lift at (%.0f %.0f %.0f) unlinked, "
			               "no proved static-world top egress\n",
			               source[0], source[1], source[2]);
			continue;
		}
		/* both ends can only be honest if the pair actually spans the
		 * travel -- otherwise two seeds on the same level got picked */
		if (!Lift_EgressSpans(source, destination, bottom_body,
		        gen_seeds[st_top].origin))
			continue;

		cost = Plat_TravelMs(e, source, destination);
		total_cost = (int)cost + approach_ms + egress_ms;
		if (total_cost <= 0 || total_cost > 30000)
			continue;
		before = gen_num_links;
		Link_Add(approach, st_top, RL_LIFT, (short)total_cost, 0);
		if (gen_num_links > before)
		{
			rune_link_t *link = &gen_links[gen_num_links - 1];

			VectorCopy(anchor, link->anchor);
			if (!Mechanism_BindPlatform(link, e, approach_door, egress_door,
			        expected_members))
				gen_num_links--;
			else
				gen_lift_links++;
		}
		if (stock && st_top >= 0 && lower_seed >= 0)
		{
			int reverse_cost = dispatch_ms + 1000 + (int)cost + lower_ms;

			if (reverse_cost > 0 && reverse_cost <= RUNE_MAX_COST_MS)
			{
				before = gen_num_links;
				Link_Add(st_top, lower_seed, RL_LIFT,
				    (short)reverse_cost, 0);
				if (gen_num_links > before)
				{
					rune_link_t *link = &gen_links[gen_num_links - 1];

					link->mode = RLCM_RIDE;
					VectorCopy(top_body, link->anchor);
					if (!Mechanism_BindPlatform(link, e, NULL, lower_door,
					        (uint16_t)(1U + lower_members)))
						gen_num_links--;
					else
						gen_lift_links++;
				}
			}
		}


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
				sg_host.dprint("rune: lift at (%.0f %.0f %.0f) has no proven way "
				           "down (no drop, and a top-parked plat does not "
				           "descend on touch -- g_func.c:429)\n",
				           source[0], source[1], source[2]);
			}
		}
	}
	if (gen_lift_links)
		sg_host.dprint("rune: %d lift links (%d matching drops down, %d with no way down)\n",
		           gen_lift_links, gen_lift_down_drop, gen_lift_down_none);
}


static void Link_Teleporters(void)
{
	edict_t *e, *dest, *entry;
	int i;

	for (i = 0; i < globals.num_edicts; i++)
	{
		vec3_t pad, pad_body, arrive, arrive_body;
		uint32_t entry_key;
		int sd, before, approach = -1, approach_ms = 0;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (strcmp(e->classname, "misc_teleporter") != 0)
			continue;
		if (!e->target)
			continue;
		entry_key = Mechanism_OwnedEntry(Mechanism_EntityKey(e),
		    SG_MECH_NODE_TELEPORT_TRIGGER);
		if (entry_key == SG_MECH_NO_KEY ||
		    entry_key >= (uint32_t)globals.num_edicts)
			continue;
		entry = &g_edicts[entry_key];

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
		    RUNE_TELEPORT_SEED_REACH, RUNE_TELEPORT_SEED_REACH, entry, e,
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
			rune_link_t *link = &gen_links[gen_num_links - 1];

			VectorCopy(pad, link->anchor);
			if (!Mechanism_BindTeleport(link, e, dest))
				gen_num_links--;
			else
				gen_tele_links++;
		}
	}
	if (gen_tele_links)
		sg_host.dprint("rune: %d teleport links\n", gen_tele_links);
}

static qboolean Push_NodeShape(const rune_mechanism_node_t *node)
{
	return node && node->kind == SG_MECH_NODE_PUSH &&
	       node->flags == (SG_MECH_NODEF_REPEATABLE |
	           SG_MECH_NODEF_TOUCHABLE) &&
	       node->target_offset == 0U && node->targetname_offset == 0U &&
	       node->killtarget_offset == 0U && node->path_target_offset == 0U &&
	       node->owner_key == SG_MECH_NO_KEY &&
	       node->team_master_key == SG_MECH_NO_KEY &&
	       node->spawnflags == 0U &&
	       node->touch_callback == SG_MECH_CALLBACK_TRIGGER_PUSH_TOUCH &&
	       node->use_callback == SG_MECH_CALLBACK_NONE &&
	       node->think_callback == SG_MECH_CALLBACK_NONE &&
	       node->blocked_callback == SG_MECH_CALLBACK_NONE &&
	       node->delay_ms == 0 && node->wait_ms == 0 &&
	       node->speed_q8 == 680U && node->accel_q8 == 0U &&
	       node->decel_q8 == 0U &&
	       isfinite(node->push_velocity[0]) &&
	       isfinite(node->push_velocity[1]) &&
	       isfinite(node->push_velocity[2]) &&
	       (node->push_velocity[0] != 0.0f ||
	        node->push_velocity[1] != 0.0f ||
	        node->push_velocity[2] != 0.0f);
}

static int Push_Destination(const vec3_t landing)
{
	short landing_q8[3];
	int best = -1;
	int64_t best_distance = INT64_MAX;
	int i, axis;

	for (axis = 0; axis < 3; axis++)
	{
		float fixed = landing[axis] * 8.0f;

		if (!isfinite(fixed) || fixed < (float)SHRT_MIN ||
		    fixed > (float)SHRT_MAX || fixed != (float)(short)fixed)
			return -1;
		landing_q8[axis] = (short)fixed;
	}
	for (i = 0; i < gen_num_seeds; i++)
	{
		short seed_q8[3];
		int64_t distance = 0;

		if (!Gen_SeedHasOutgoing(i) || gen_source_waterlevel[i] != 0)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			int64_t delta;

			seed_q8[axis] = (short)(gen_seeds[i].origin[axis] * 8.0f);
			delta = (int64_t)seed_q8[axis] - landing_q8[axis];
			distance += delta * delta;
		}
		if (!SG_PushArrivalEnvelope(landing_q8, seed_q8) ||
		    distance >= best_distance)
			continue;
		best = i;
		best_distance = distance;
	}
	return best;
}

static void Link_Pushes(void)
{
	uint32_t node_index;

	for (node_index = 0U; node_index < gen_mechanism_catalog.num_nodes;
	     node_index++)
	{
		const rune_mechanism_node_t *node =
			&gen_mechanism_catalog.nodes[node_index];
		edict_t *trigger;
		int best_source = -1, best_destination = -1, best_cost = 0;
		int64_t best_source_distance = INT64_MAX;
		int source;

		if (!Push_NodeShape(node) || node->key == 0U ||
		    node->key >= (uint32_t)globals.num_edicts)
			continue;
		trigger = &g_edicts[node->key];
		for (source = 0; source < gen_num_seeds; source++)
		{
			vec3_t landing;
			int destination, cost_ms;
			int64_t source_distance = 0;
			int axis;
			float dx, dy, dz;

			if (!gen_source_stable[source] ||
			    gen_source_waterlevel[source] != 0 ||
			    !Gen_SeedHasIncoming(source))
				continue;
			dx = gen_seeds[source].origin[0] < trigger->absmin[0]
			    ? trigger->absmin[0] - gen_seeds[source].origin[0]
			    : gen_seeds[source].origin[0] > trigger->absmax[0]
			        ? gen_seeds[source].origin[0] - trigger->absmax[0] : 0.0f;
			dy = gen_seeds[source].origin[1] < trigger->absmin[1]
			    ? trigger->absmin[1] - gen_seeds[source].origin[1]
			    : gen_seeds[source].origin[1] > trigger->absmax[1]
			        ? gen_seeds[source].origin[1] - trigger->absmax[1] : 0.0f;
			dz = gen_seeds[source].origin[2] < trigger->absmin[2]
			    ? trigger->absmin[2] - gen_seeds[source].origin[2]
			    : gen_seeds[source].origin[2] > trigger->absmax[2]
			        ? gen_seeds[source].origin[2] - trigger->absmax[2] : 0.0f;
			if (dx * dx + dy * dy > 320.0f * 320.0f || fabsf(dz) > 128.0f)
				continue;
			Rune_TelemetryAdd(&gen_telemetry.prover_calls, 1U);
			if (!SG_OraclePushFlight(gen_seeds[source].origin, trigger,
			        node->push_velocity, landing, &cost_ms))
				continue;
			destination = Push_Destination(landing);
			if (destination < 0 || destination == source || cost_ms <= 0 ||
			    cost_ms > 30000)
				continue;
			for (axis = 0; axis < 3; axis++)
			{
				float center =
					(trigger->absmin[axis] + trigger->absmax[axis]) * 0.5f;
				int64_t delta = (int64_t)(
					(gen_seeds[source].origin[axis] - center) * 8.0f);

				source_distance += delta * delta;
			}
			if (source_distance >= best_source_distance)
				continue;
			best_source = source;
			best_destination = destination;
			best_cost = cost_ms;
			best_source_distance = source_distance;
		}
		if (best_source >= 0)
		{
			int before = gen_num_links;

			Link_Add(best_source, best_destination, RL_PUSH,
			    (short)best_cost, 0);
			if (gen_num_links > before)
			{
				rune_link_t *link = &gen_links[gen_num_links - 1];

				if (!Mechanism_BindPush(link, trigger))
					gen_num_links--;
				else
					gen_push_links++;
			}
		}
	}
	if (gen_push_links)
		sg_host.dprint("rune: %d fixed push links\n", gen_push_links);
}

static qboolean Train_NodeShape(const rune_mechanism_node_t *node)
{
	return node && node->kind == SG_MECH_NODE_TRAIN &&
	       node->spawnflags == 2U &&
	       node->use_callback == SG_MECH_CALLBACK_TRAIN_USE &&
	       node->blocked_callback == SG_MECH_CALLBACK_BLOCKED_TRAIN &&
	       node->speed_q8 > 0U && node->speed_q8 != UINT32_MAX;
}

static qboolean Train_ButtonShape(const rune_mechanism_node_t *node)
{
	return node && node->kind == SG_MECH_NODE_BUTTON &&
	       (node->flags & (SG_MECH_NODEF_TOUCHABLE | SG_MECH_NODEF_USABLE |
	           SG_MECH_NODEF_SHOOTABLE)) ==
	           (SG_MECH_NODEF_TOUCHABLE | SG_MECH_NODEF_USABLE) &&
	       node->touch_callback == SG_MECH_CALLBACK_BUTTON_TOUCH &&
	       node->use_callback == SG_MECH_CALLBACK_BUTTON_USE &&
	       node->speed_q8 > 0U && node->speed_q8 != UINT32_MAX;
}

static qboolean Train_ShootButtonShape(const rune_mechanism_node_t *node)
{
	return node && node->kind == SG_MECH_NODE_BUTTON &&
	       node->flags == (SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
	           SG_MECH_NODEF_MOVER | SG_MECH_NODEF_SHOOTABLE) &&
	       node->touch_callback == SG_MECH_CALLBACK_NONE &&
	       node->use_callback == SG_MECH_CALLBACK_BUTTON_USE &&
	       node->think_callback == SG_MECH_CALLBACK_NONE &&
	       node->blocked_callback == SG_MECH_CALLBACK_NONE &&
	       node->spawnflags == 0U && node->delay_ms == 0 &&
	       node->wait_ms > 0 && node->target_offset != 0U &&
	       node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U && node->speed_q8 > 0U &&
	       node->speed_q8 != UINT32_MAX;
}

static qboolean Train_ReverseTouchEndpoints(uint32_t train_key,
	int *source_out, int *destination_out)
{
	sg_train_gate_reverse_touch_t selection;
	int link_index;

	if (!source_out || !destination_out || !gen_mechanism_bindings)
		return false;
	SG_TrainGateReverseTouchBegin(&selection);
	for (link_index = 0; link_index < gen_num_links; link_index++)
	{
		const rune_link_t *link = &gen_links[link_index];
		const sg_mechanism_plan_binding_t *binding;

		if (link->action != RL_TRAIN ||
		    link->mechanism_plan >= gen_num_mechanism_bindings)
			continue;
		binding = &gen_mechanism_bindings[link->mechanism_plan];
		if (binding->controller_kind != SG_MECHANISM_CONTROLLER_TRAIN ||
		    binding->mover_key != train_key)
			continue;
		SG_TrainGateReverseTouchConsider(&selection,
		    link->mode == RLCM_PREOPEN, link->to, link->from);
	}
	return SG_TrainGateReverseTouchResult(&selection,
	    source_out, destination_out) ? true : false;
}

static uint32_t Train_OpeningBound(edict_t *button, edict_t *train,
	edict_t *closed, edict_t *open)
{
	vec3_t delta;
	float button_distance;
	float train_distance;
	float total_ms;
	uint32_t rounded;

	if (!button || !train || !closed || !open ||
	    button->moveinfo.speed <= 0.0f || train->moveinfo.speed <= 0.0f)
		return 0U;
	VectorSubtract(button->moveinfo.end_origin,
	    button->moveinfo.start_origin, delta);
	button_distance = VectorLength(delta);
	VectorSubtract(open->s.origin, closed->s.origin, delta);
	train_distance = VectorLength(delta);
	total_ms = button_distance * 1000.0f / button->moveinfo.speed +
	    train_distance * 1000.0f / train->moveinfo.speed + 500.0f;
	if (!isfinite(total_ms) || total_ms <= 0.0f ||
	    total_ms > (float)RUNE_MAX_COST_MS)
		return 0U;
	rounded = (uint32_t)ceilf(total_ms / 100.0f) * 100U;
	return rounded <= RUNE_MAX_COST_MS ? rounded : 0U;
}

static void Train_Sweep(edict_t *train, edict_t *closed, edict_t *open,
	vec3_t mins_out, vec3_t maxs_out)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float extent = train->maxs[axis] - train->mins[axis];

		mins_out[axis] = closed->s.origin[axis] < open->s.origin[axis]
		    ? closed->s.origin[axis] : open->s.origin[axis];
		maxs_out[axis] = (closed->s.origin[axis] > open->s.origin[axis]
		    ? closed->s.origin[axis] : open->s.origin[axis]) + extent;
	}
}

static void Train_SetPose(edict_t *train, edict_t *corner)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		train->s.origin[axis] = corner->s.origin[axis] - train->mins[axis];
	VectorCopy(train->s.origin, train->s.old_origin);
	VectorClear(train->velocity);
	VectorClear(train->avelocity);
	train->solid = SOLID_BSP;
	sg_host.linkentity(train);
}

static void Train_PoseAt(edict_t *train, edict_t *corner,
	door_pose_t *saved)
{

	memset(saved, 0, sizeof(*saved));
	saved->ent = train;
	VectorCopy(train->s.origin, saved->origin);
	VectorCopy(train->s.old_origin, saved->old_origin);
	VectorCopy(train->s.angles, saved->angles);
	VectorCopy(train->velocity, saved->velocity);
	VectorCopy(train->avelocity, saved->avelocity);
	saved->state = train->moveinfo.state;
	saved->solid = train->solid;
	saved->linkcount = train->linkcount;
	Train_SetPose(train, corner);
}

static qboolean Train_RideEndpoints(edict_t *closed, edict_t *open,
	edict_t **lower_out, edict_t **upper_out, vec3_t displacement_out)
{
	vec3_t delta;

	if (lower_out) *lower_out = NULL;
	if (upper_out) *upper_out = NULL;
	if (displacement_out) VectorClear(displacement_out);
	if (!closed || !open || !lower_out || !upper_out || !displacement_out)
		return false;
	VectorSubtract(open->s.origin, closed->s.origin, delta);
	if (fabsf(delta[0]) > 0.125f || fabsf(delta[1]) > 0.125f ||
	    fabsf(delta[2]) < 8.0f)
		return false;
	if (delta[2] > 0.0f)
	{
		*lower_out = closed;
		*upper_out = open;
	}
	else
	{
		*lower_out = open;
		*upper_out = closed;
	}
	VectorSubtract((*upper_out)->s.origin, (*lower_out)->s.origin,
	    displacement_out);
	return displacement_out[2] >= 8.0f;
}

static qboolean Train_OppositeSide(const vec3_t contact,
	const vec3_t destination, const vec3_t sweep_mins,
	const vec3_t sweep_maxs)
{
	float gap[4];
	int side = -1;
	int index;

	gap[0] = sweep_mins[0] - (contact[0] + 16.0f);
	gap[1] = (contact[0] - 16.0f) - sweep_maxs[0];
	gap[2] = sweep_mins[1] - (contact[1] + 16.0f);
	gap[3] = (contact[1] - 16.0f) - sweep_maxs[1];
	for (index = 0; index < 4; index++)
		if (gap[index] >= 0.0f &&
		    (side < 0 || gap[index] < gap[side]))
			side = index;
	switch (side)
	{
	case 0: return destination[0] - 16.0f >= sweep_maxs[0];
	case 1: return destination[0] + 16.0f <= sweep_mins[0];
	case 2: return destination[1] - 16.0f >= sweep_maxs[1];
	case 3: return destination[1] + 16.0f <= sweep_mins[1];
	default: return false;
	}
}

static int Train_TravelAxis(edict_t *closed, edict_t *open)
{
	int axis;
	int travel_axis = -1;

	if (!closed || !open)
		return -1;
	for (axis = 0; axis < 3; axis++)
	{
		if (fabsf(open->s.origin[axis] - closed->s.origin[axis]) <= 0.125f)
			continue;
		if (travel_axis >= 0)
			return -1;
		travel_axis = axis;
	}
	return travel_axis;
}

static sg_train_gate_side_t Train_SeedSweepAxisSide(const vec3_t origin,
	const vec3_t sweep_mins, const vec3_t sweep_maxs, unsigned int axis)
{
	vec3_t bounds_mins;
	vec3_t bounds_maxs;

	bounds_mins[0] = origin[0] - 16.0f;
	bounds_mins[1] = origin[1] - 16.0f;
	bounds_mins[2] = origin[2] - 24.0f;
	bounds_maxs[0] = origin[0] + 16.0f;
	bounds_maxs[1] = origin[1] + 16.0f;
	bounds_maxs[2] = origin[2] + 32.0f;
	return SG_TrainGateSweepAxisSide(bounds_mins, bounds_maxs,
	    sweep_mins, sweep_maxs, axis);
}

static float Train_SeedSweepAxisGap(const vec3_t origin,
	const vec3_t sweep_mins, const vec3_t sweep_maxs, unsigned int axis,
	sg_train_gate_side_t side)
{
	static const sg_train_gate_side_t lower[3] = {
		SG_TRAIN_GATE_SIDE_X_MIN,
		SG_TRAIN_GATE_SIDE_Y_MIN,
		SG_TRAIN_GATE_SIDE_Z_MIN
	};
	static const sg_train_gate_side_t upper[3] = {
		SG_TRAIN_GATE_SIDE_X_MAX,
		SG_TRAIN_GATE_SIDE_Y_MAX,
		SG_TRAIN_GATE_SIDE_Z_MAX
	};
	static const float hull_mins[3] = { -16.0f, -16.0f, -24.0f };
	static const float hull_maxs[3] = { 16.0f, 16.0f, 32.0f };

	if (axis >= 3U)
		return HUGE_VALF;
	if (side == lower[axis])
		return sweep_mins[axis] - (origin[axis] + hull_maxs[axis]);
	if (side == upper[axis])
		return (origin[axis] + hull_mins[axis]) - sweep_maxs[axis];
	return HUGE_VALF;
}

static float Train_SeedSweepGapSquared(const vec3_t origin,
	const vec3_t sweep_mins, const vec3_t sweep_maxs)
{
	static const float hull_mins[3] = { -16.0f, -16.0f, -24.0f };
	static const float hull_maxs[3] = { 16.0f, 16.0f, 32.0f };
	float distance = 0.0f;

	for (int axis = 0; axis < 3; axis++)
	{
		float body_min = origin[axis] + hull_mins[axis];
		float body_max = origin[axis] + hull_maxs[axis];
		float gap = body_max < sweep_mins[axis]
			? sweep_mins[axis] - body_max
			: (body_min > sweep_maxs[axis]
				? body_min - sweep_maxs[axis] : 0.0f);

		distance += gap * gap;
	}
	return distance;
}

static void Link_Trains(void)
{
	uint32_t button_index;

	for (button_index = 0U;
	     button_index < gen_mechanism_catalog.num_nodes; button_index++)
	{
		const rune_mechanism_node_t *button_node =
			&gen_mechanism_catalog.nodes[button_index];
		const rune_mechanism_edge_t *target;
		const rune_mechanism_node_t *train_node;
		const rune_mechanism_node_t *closed_node;
		const rune_mechanism_node_t *open_node;
		edict_t *button;
		edict_t *train;
		edict_t *closed;
		edict_t *open;
		uint32_t closed_key;
		uint32_t open_key;
		uint32_t opening_bound;
		vec3_t button_center;
		vec3_t sweep_mins;
		vec3_t sweep_maxs;
		int best_source = -1;
		int best_destination = -1;
		int best_egress_ms = 0;
		int best_cost = INT_MAX;
		vec3_t best_contact = { 0.0f, 0.0f, 0.0f };
		int source;

		if (!Train_ButtonShape(button_node) || button_node->key == 0U ||
		    button_node->key >= (uint32_t)globals.num_edicts ||
		    !(target = Mechanism_InventoryEdge(button_node->key,
		        SG_MECH_EDGE_TARGET, 0U)) ||
		    Mechanism_InventoryEdge(button_node->key, SG_MECH_EDGE_TARGET, 1U) ||
		    !(train_node = Mechanism_Node(target->to_key)) ||
		    !Train_NodeShape(train_node) ||
		    !Mechanism_TrainRoute(train_node->key, &closed_key, &open_key) ||
		    !(closed_node = Mechanism_Node(closed_key)) ||
		    !(open_node = Mechanism_Node(open_key)) ||
		    closed_node->kind != SG_MECH_NODE_PATH_CORNER ||
		    open_node->kind != SG_MECH_NODE_PATH_CORNER ||
		    train_node->key >= (uint32_t)globals.num_edicts ||
		    closed_key >= (uint32_t)globals.num_edicts ||
		    open_key >= (uint32_t)globals.num_edicts)
			continue;
		button = &g_edicts[button_node->key];
		train = &g_edicts[train_node->key];
		closed = &g_edicts[closed_key];
		open = &g_edicts[open_key];
		opening_bound = Train_OpeningBound(button, train, closed, open);
		if (opening_bound == 0U)
			continue;
		button_center[0] = (button->absmin[0] + button->absmax[0]) * 0.5f;
		button_center[1] = (button->absmin[1] + button->absmax[1]) * 0.5f;
		button_center[2] = (button->absmin[2] + button->absmax[2]) * 0.5f;
		Train_Sweep(train, closed, open, sweep_mins, sweep_maxs);
		for (source = 0; source < gen_num_seeds; source++)
		{
			vec3_t delta;
			vec3_t contact;
			int approach_ms;
			int destination;
			door_pose_t saved;

			if (!gen_source_stable[source] ||
			    gen_source_waterlevel[source] != 0 ||
			    !Gen_SeedHasIncoming(source))
				continue;
			VectorSubtract(gen_seeds[source].origin, button_center, delta);
			if (delta[0] * delta[0] + delta[1] * delta[1] >
			        320.0f * 320.0f || fabsf(delta[2]) > 128.0f ||
			    !SG_OracleTrainGateApproach(gen_seeds[source].origin,
			        button_center, button, &approach_ms, contact))
				continue;
			Train_PoseAt(train, open, &saved);
			for (destination = 0; destination < gen_num_seeds; destination++)
			{
				vec3_t egress_delta;
				int egress_ms;
				int cost;

				if (destination == source ||
				    !gen_source_stable[destination] ||
				    gen_source_waterlevel[destination] != 0 ||
				    !Gen_SeedHasOutgoing(destination))
					continue;
				VectorSubtract(gen_seeds[destination].origin, contact,
				    egress_delta);
				if (egress_delta[0] * egress_delta[0] +
				        egress_delta[1] * egress_delta[1] > 1600.0f * 1600.0f ||
				    fabsf(egress_delta[2]) > 256.0f ||
				    !Train_OppositeSide(contact,
				        gen_seeds[destination].origin, sweep_mins, sweep_maxs) ||
				    !SG_OracleTrainGateCross(contact,
				        gen_seeds[destination].origin, button, train,
				        sweep_mins, sweep_maxs, 3U, &egress_ms))
					continue;
				cost = approach_ms + (int)opening_bound + egress_ms;
				if (cost <= 0 || cost > RUNE_MAX_COST_MS ||
				    cost >= best_cost)
					continue;
				best_source = source;
				best_destination = destination;
				best_egress_ms = egress_ms;
				best_cost = cost;
				VectorCopy(contact, best_contact);
			}
			DoorPose_Restore(&saved, 1);
		}
		if (best_source >= 0)
		{
			int before = gen_num_links;

			Link_Add(best_source, best_destination, RL_TRAIN,
			    (short)best_cost, 0);
			if (gen_num_links > before)
			{
				rune_link_t *link = &gen_links[gen_num_links - 1];

				VectorCopy(best_contact, link->anchor);
				VectorCopy(gen_seeds[best_destination].origin,
				    link->mechanism_anchor);
				link->sweep_clear_ms = (uint16_t)best_egress_ms;
				link->mode = RLCM_PREOPEN;
				if (!Mechanism_BindTrain(link, button, train, closed, open,
				        opening_bound, SG_MECHANISM_CONTROLLER_TRAIN))
					gen_num_links--;
				else
					gen_train_links++;
			}
		}
	}
	if (gen_train_links)
		sg_host.dprint("rune: %d sealed train links\n", gen_train_links);
}

static void Link_TrainRides(void)
{
	uint32_t train_index;
	int added = 0;

	for (train_index = 0U;
	     train_index < gen_mechanism_catalog.num_nodes; train_index++)
	{
		const rune_mechanism_node_t *train_node =
			&gen_mechanism_catalog.nodes[train_index];
		const rune_mechanism_node_t *closed_node;
		const rune_mechanism_node_t *open_node;
		edict_t *best_button = NULL;
		edict_t *train;
		edict_t *closed;
		edict_t *open;
		edict_t *lower;
		edict_t *upper;
		uint32_t closed_key;
		uint32_t open_key;
		uint32_t best_opening_bound = 0U;
		vec3_t displacement;
		vec3_t best_board = { 0.0f, 0.0f, 0.0f };
		vec3_t best_top = { 0.0f, 0.0f, 0.0f };
		float halfx;
		float halfy;
		float egress_radius;
		int best_source = -1;
		int best_destination = -1;
		int best_egress_ms = 0;
		int best_cost = INT_MAX;
		uint32_t button_index;
		door_pose_t saved;

		if (!Train_NodeShape(train_node) || train_node->key == 0U ||
		    train_node->key >= (uint32_t)globals.num_edicts ||
		    !Mechanism_TrainRoute(train_node->key, &closed_key, &open_key) ||
		    !(closed_node = Mechanism_Node(closed_key)) ||
		    !(open_node = Mechanism_Node(open_key)) ||
		    closed_node->kind != SG_MECH_NODE_PATH_CORNER ||
		    open_node->kind != SG_MECH_NODE_PATH_CORNER ||
		    closed_key >= (uint32_t)globals.num_edicts ||
		    open_key >= (uint32_t)globals.num_edicts)
			continue;
		train = &g_edicts[train_node->key];
		closed = &g_edicts[closed_key];
		open = &g_edicts[open_key];
		if (!Train_RideEndpoints(closed, open, &lower, &upper, displacement))
			continue;
		halfx = (train->maxs[0] - train->mins[0]) * 0.5f;
		halfy = (train->maxs[1] - train->mins[1]) * 0.5f;
		egress_radius = Lift_EgressSearchRadius(halfx, halfy);
		Train_PoseAt(train, lower, &saved);
		for (button_index = 0U;
		     button_index < gen_mechanism_catalog.num_nodes; button_index++)
		{
			const rune_mechanism_node_t *button_node =
				&gen_mechanism_catalog.nodes[button_index];
			const rune_mechanism_edge_t *target;
			edict_t *button;
			uint32_t opening_bound;
			vec3_t button_center;
			int source;

			if (!Train_ButtonShape(button_node) || button_node->key == 0U ||
			    button_node->key >= (uint32_t)globals.num_edicts ||
			    !(target = Mechanism_InventoryEdge(button_node->key,
			        SG_MECH_EDGE_TARGET, 0U)) ||
			    Mechanism_InventoryEdge(button_node->key,
			        SG_MECH_EDGE_TARGET, 1U) || target->to_key != train_node->key)
				continue;
			button = &g_edicts[button_node->key];
			opening_bound = Train_OpeningBound(button, train, closed, open);
			if (opening_bound == 0U)
				continue;
			button_center[0] =
				(button->absmin[0] + button->absmax[0]) * 0.5f;
			button_center[1] =
				(button->absmin[1] + button->absmax[1]) * 0.5f;
			button_center[2] =
				(button->absmin[2] + button->absmax[2]) * 0.5f;
			for (source = 0; source < gen_num_seeds; source++)
			{
				vec3_t delta;
				vec3_t board;
				vec3_t top;
				int approach_ms;
				int destination;

				if (!gen_source_stable[source] ||
				    gen_source_waterlevel[source] != 0 ||
				    !Gen_SeedHasIncoming(source))
					continue;
				VectorSubtract(gen_seeds[source].origin, button_center, delta);
				if (delta[0] * delta[0] + delta[1] * delta[1] >
				        320.0f * 320.0f || fabsf(delta[2]) > 128.0f ||
				    !SG_OracleTrainRideBoard(gen_seeds[source].origin,
				        button_center, button, train, &approach_ms, board) ||
				    !SG_OracleTrainRideCarry(board, displacement, train, top))
					continue;
				Train_SetPose(train, upper);
				for (destination = 0; destination < gen_num_seeds;
				     destination++)
				{
					vec3_t egress_delta;
					float horizontal2;
					int egress_ms;
					int cost;

					if (destination == source ||
					    !gen_source_stable[destination] ||
					    gen_source_waterlevel[destination] != 0 ||
					    !Gen_SeedHasOutgoing(destination))
						continue;
					VectorSubtract(gen_seeds[destination].origin, top,
					    egress_delta);
					horizontal2 = egress_delta[0] * egress_delta[0] +
					    egress_delta[1] * egress_delta[1];
					if (fabsf(egress_delta[2]) > 16.0f ||
					    horizontal2 > egress_radius * egress_radius ||
					    !SG_OracleTrainRideEgress(top,
					        gen_seeds[destination].origin, train, &egress_ms))
						continue;
					cost = approach_ms + (int)opening_bound + egress_ms;
					if (cost <= 0 || cost > RUNE_MAX_COST_MS ||
					    cost >= best_cost)
						continue;
					best_button = button;
					best_source = source;
					best_destination = destination;
					best_opening_bound = opening_bound;
					best_egress_ms = egress_ms;
					best_cost = cost;
					VectorCopy(board, best_board);
					VectorCopy(top, best_top);
				}
				Train_SetPose(train, lower);
			}
		}
		DoorPose_Restore(&saved, 1);
		if (best_button && best_source >= 0 && best_destination >= 0)
		{
			int before = gen_num_links;

			Link_Add(best_source, best_destination, RL_TRAIN,
			    (short)best_cost, 0);
			if (gen_num_links > before)
			{
				rune_link_t *link = &gen_links[gen_num_links - 1];

				VectorCopy(best_board, link->anchor);
				VectorCopy(best_top, link->mechanism_anchor);
				link->sweep_clear_ms = (uint16_t)best_egress_ms;
				link->mode = RLCM_RIDE;
				if (!Mechanism_BindTrain(link, best_button, train, closed,
				        open, best_opening_bound,
				        SG_MECHANISM_CONTROLLER_TRAIN))
					gen_num_links--;
				else
				{
					gen_train_links++;
					added++;
				}
			}
		}
	}
	if (added)
		sg_host.dprint("rune: %d sealed carried train links\n", added);
}

static void Link_TrainShootButtons(
	const sg_compound_gen_game_topology_t *topology)
{
	#define TRAIN_SHOOT_DEST_SLOTS 4
	uint32_t button_index;

	for (button_index = 0U;
	     button_index < gen_mechanism_catalog.num_nodes; button_index++)
	{
		const rune_mechanism_node_t *button_node =
			&gen_mechanism_catalog.nodes[button_index];
		const rune_mechanism_edge_t *target;
		const rune_mechanism_node_t *train_node;
		const rune_mechanism_node_t *closed_node;
		const rune_mechanism_node_t *open_node;
		edict_t *button;
		edict_t *train;
		edict_t *closed;
		edict_t *open;
		uint32_t closed_key;
		uint32_t open_key;
		uint32_t opening_bound;
		vec3_t sweep_mins;
		vec3_t sweep_maxs;
		int source_by_axis_side[3][SG_TRAIN_GATE_SIDE_Z_MAX + 1];
		float gap_by_axis_side[3][SG_TRAIN_GATE_SIDE_Z_MAX + 1];
		vec3_t contact_by_axis_side[3][SG_TRAIN_GATE_SIDE_Z_MAX + 1];
		uint32_t source_side_mask[3] = { 0U, 0U, 0U };
		uint32_t closure_axis_mask = 0U;
		int destination_by_axis[3] = { -1, -1, -1 };
		int cost_by_axis[3] = { INT_MAX, INT_MAX, INT_MAX };
		int cross_ms_by_axis[3] = { 0, 0, 0 };
		uint32_t transaction_bound_by_axis[3] = { 0U, 0U, 0U };
		int selected_destination[TRAIN_SHOOT_DEST_SLOTS] = { -1, -1, -1, -1 };
		int selected_cost[TRAIN_SHOOT_DEST_SLOTS] = {
			INT_MAX, INT_MAX, INT_MAX, INT_MAX
		};
		int selected_cross[TRAIN_SHOOT_DEST_SLOTS] = { -1, -1, -1, -1 };
		int selected_cross_ms[TRAIN_SHOOT_DEST_SLOTS] = { 0, 0, 0, 0 };
		int best_source = -1;
		int best_destination = -1;
		int best_cost = INT_MAX;
		uint32_t best_transaction_bound = 0U;
		vec3_t best_contact = { 0.0f, 0.0f, 0.0f };
		int motion_axis;
		int passage_axis = -1;
		int reverse_source = -1;
		int reverse_destination = -1;
		int source;
		int axis;
		int side_index;

		for (axis = 0; axis < 3; axis++)
			for (side_index = SG_TRAIN_GATE_SIDE_NONE;
			     side_index <= SG_TRAIN_GATE_SIDE_Z_MAX; side_index++)
			{
				source_by_axis_side[axis][side_index] = -1;
				gap_by_axis_side[axis][side_index] = HUGE_VALF;
				VectorClear(contact_by_axis_side[axis][side_index]);
			}

		if (!Train_ShootButtonShape(button_node) || button_node->key == 0U ||
		    button_node->key >= (uint32_t)globals.num_edicts ||
		    !(target = Mechanism_InventoryEdge(button_node->key,
		        SG_MECH_EDGE_TARGET, 0U)) ||
		    Mechanism_InventoryEdge(button_node->key, SG_MECH_EDGE_TARGET, 1U) ||
		    !(train_node = Mechanism_Node(target->to_key)) ||
		    !Train_NodeShape(train_node) ||
		    !Mechanism_TrainRoute(train_node->key, &closed_key, &open_key) ||
		    !(closed_node = Mechanism_Node(closed_key)) ||
		    !(open_node = Mechanism_Node(open_key)) ||
		    closed_node->kind != SG_MECH_NODE_PATH_CORNER ||
		    open_node->kind != SG_MECH_NODE_PATH_CORNER ||
		    train_node->key >= (uint32_t)globals.num_edicts ||
		    closed_key >= (uint32_t)globals.num_edicts ||
		    open_key >= (uint32_t)globals.num_edicts)
			continue;
		button = &g_edicts[button_node->key];
		train = &g_edicts[train_node->key];
		closed = &g_edicts[closed_key];
		open = &g_edicts[open_key];
		if (!SG_MechCatalogEntityExecutionMatches(button_node->key,
		        button_node, SG_MECHANISM_CONTROLLER_TRAIN_SHOOT))
			continue;
		opening_bound = Train_OpeningBound(button, train, closed, open);
		if (opening_bound == 0U)
			continue;
		Train_Sweep(train, closed, open, sweep_mins, sweep_maxs);
		motion_axis = Train_TravelAxis(closed, open);
		if (motion_axis < 0 ||
		    !Train_ReverseTouchEndpoints(train_node->key, &reverse_source,
		        &reverse_destination) || !topology || !topology->component ||
		    topology->component[reverse_source] < 0)
			continue;
		for (source = 0; source < gen_num_seeds; source++)
		{
			vec3_t contact;
			int flight_ms;
			door_pose_t saved;

			if (!gen_source_stable[source] ||
			    gen_source_waterlevel[source] != 0 ||
			    !Gen_SeedHasIncoming(source) || !Gen_SeedHasOutgoing(source) ||
			    topology->component[source] !=
			        topology->component[reverse_source] ||
			    !SG_OracleTrainGateShot(gen_seeds[source].origin, button,
			        contact, &flight_ms))
				continue;
			Train_PoseAt(train, open, &saved);
			for (axis = 0; axis < 3; axis++)
			{
				sg_train_gate_side_t source_side;
				sg_train_gate_side_t destination_side;
				vec3_t entry_delta;
				vec3_t egress_delta;
				uint32_t transaction_bound;
				int entry_ms;
				int cross_ms;
				int egress_ms;
				int cost;
				float gap;

				if (axis == motion_axis)
					continue;
				source_side = Train_SeedSweepAxisSide(
				    gen_seeds[source].origin, sweep_mins, sweep_maxs,
				    (unsigned int)axis);
				if (source_side == SG_TRAIN_GATE_SIDE_NONE)
					continue;
				source_side_mask[axis] |= 1U << source_side;
				destination_side = SG_TrainGateOppositeSide(source_side);
				if (destination_side == SG_TRAIN_GATE_SIDE_NONE ||
				    Train_SeedSweepAxisSide(
				        gen_seeds[reverse_source].origin, sweep_mins,
				        sweep_maxs, (unsigned int)axis) != source_side ||
				    Train_SeedSweepAxisSide(
				        gen_seeds[reverse_destination].origin, sweep_mins,
				        sweep_maxs, (unsigned int)axis) != destination_side ||
				    source == reverse_destination ||
				    !gen_source_stable[reverse_destination] ||
				    gen_source_waterlevel[reverse_destination] != 0 ||
				    !Gen_SeedHasOutgoing(reverse_destination))
					continue;
				transaction_bound = opening_bound + (uint32_t)flight_ms + 1100U;
				if (transaction_bound > RUNE_MAX_COST_MS)
					continue;
				VectorSubtract(gen_seeds[reverse_source].origin,
				    gen_seeds[source].origin, entry_delta);
				VectorSubtract(gen_seeds[reverse_destination].origin,
				    gen_seeds[reverse_source].origin, egress_delta);
				if (entry_delta[0] * entry_delta[0] +
				        entry_delta[1] * entry_delta[1] > 1600.0f * 1600.0f ||
				    fabsf(entry_delta[2]) > 256.0f ||
				    egress_delta[0] * egress_delta[0] +
				        egress_delta[1] * egress_delta[1] > 1600.0f * 1600.0f ||
				    fabsf(egress_delta[2]) > 256.0f ||
				    !SG_OracleTrainGateEntry(gen_seeds[source].origin,
				        gen_seeds[reverse_source].origin, button, train,
				        &entry_ms) ||
				    !SG_OracleTrainGateCross(gen_seeds[reverse_source].origin,
				        gen_seeds[reverse_destination].origin, button, train,
				        sweep_mins, sweep_maxs, (unsigned int)axis, &cross_ms))
					continue;
				if (entry_ms > RUNE_MAX_COST_MS - cross_ms)
					continue;
				egress_ms = entry_ms + cross_ms;
				cost = (int)transaction_bound + egress_ms;
				gap = Train_SeedSweepAxisGap(gen_seeds[source].origin,
				    sweep_mins, sweep_maxs, (unsigned int)axis, source_side);
				if (cost <= 0 || cost > RUNE_MAX_COST_MS ||
				    cost > cost_by_axis[axis] ||
				    (cost == cost_by_axis[axis] &&
				     gap >= gap_by_axis_side[axis][source_side]))
					continue;
				source_by_axis_side[axis][source_side] = source;
				gap_by_axis_side[axis][source_side] = gap;
				VectorCopy(gen_seeds[reverse_source].origin,
				    contact_by_axis_side[axis][source_side]);
				destination_by_axis[axis] = reverse_destination;
				cost_by_axis[axis] = cost;
				cross_ms_by_axis[axis] = cross_ms;
				transaction_bound_by_axis[axis] = transaction_bound;
			}
			DoorPose_Restore(&saved, 1);
		}
		for (axis = 0; axis < 3; axis++)
		{
			sg_train_gate_side_t source_side =
			    SG_TrainGateUniqueSourceSide(source_side_mask[axis]);

			if (axis != motion_axis && source_side != SG_TRAIN_GATE_SIDE_NONE &&
			    source_by_axis_side[axis][source_side] >= 0 &&
			    destination_by_axis[axis] >= 0)
				closure_axis_mask |= 1U << axis;
		}
		passage_axis = SG_TrainGateUniquePassageAxis(closure_axis_mask,
		    (unsigned int)motion_axis);
		if (passage_axis >= 0)
		{
			sg_train_gate_side_t source_side =
			    SG_TrainGateUniqueSourceSide(source_side_mask[passage_axis]);

			best_source = source_by_axis_side[passage_axis][source_side];
			best_destination = destination_by_axis[passage_axis];
			best_cost = cost_by_axis[passage_axis];
			best_transaction_bound = transaction_bound_by_axis[passage_axis];
			VectorCopy(contact_by_axis_side[passage_axis][source_side],
			    best_contact);

			selected_destination[0] = best_destination;
			selected_cost[0] = best_cost;
			selected_cross[0] = best_destination;
			selected_cross_ms[0] = cross_ms_by_axis[passage_axis];
			if (cross_ms_by_axis[passage_axis] > 0 &&
			    best_cost > cross_ms_by_axis[passage_axis])
			{
				sg_train_gate_side_t destination_side =
				    SG_TrainGateOppositeSide(source_side);
				int pre_cross_cost =
				    best_cost - cross_ms_by_axis[passage_axis];
				int missing = 3 & ~topology->objective_mask[best_source];
				int cross_destination;
				door_pose_t saved;

				Train_PoseAt(train, open, &saved);
				for (cross_destination = 0;
				     cross_destination < gen_num_seeds; cross_destination++)
				{
					vec3_t cross_delta;
					int cross_ms;
					int destination;

					if (!gen_source_stable[cross_destination] ||
					    gen_source_waterlevel[cross_destination] != 0 ||
					    Train_SeedSweepAxisSide(
					        gen_seeds[cross_destination].origin,
					        sweep_mins, sweep_maxs,
					        (unsigned int)passage_axis) != destination_side)
						continue;
					VectorSubtract(gen_seeds[cross_destination].origin,
					    best_contact, cross_delta);
					if (cross_delta[0] * cross_delta[0] +
					        cross_delta[1] * cross_delta[1] >
					            1600.0f * 1600.0f ||
					    fabsf(cross_delta[2]) > 256.0f ||
					    !SG_OracleTrainGateCross(best_contact,
					        gen_seeds[cross_destination].origin, button, train,
					        sweep_mins, sweep_maxs,
					        (unsigned int)passage_axis, &cross_ms))
						continue;
					for (destination = 0; destination < gen_num_seeds;
					     destination++)
					{
						vec3_t exit_delta;
						int new_bits;
						int crosses;
						int exit_ms;
						int cost;
						int slot;

						if (destination == cross_destination ||
						    !gen_source_stable[destination] ||
						    gen_source_waterlevel[destination] != 0 ||
						    !Gen_SeedHasOutgoing(destination) ||
						    Train_SeedSweepAxisSide(
						        gen_seeds[destination].origin, sweep_mins,
						        sweep_maxs, (unsigned int)passage_axis) !=
						        destination_side)
							continue;
						new_bits =
						    topology->objective_mask[destination] & missing;
						crosses = topology->component[destination] >= 0 &&
						    topology->component[destination] !=
						        topology->component[best_source];
						if (!new_bits && (missing != 0 || !crosses))
							continue;
						VectorSubtract(
						    gen_seeds[destination].origin,
						    gen_seeds[cross_destination].origin,
						    exit_delta);
						if (exit_delta[0] * exit_delta[0] +
						        exit_delta[1] * exit_delta[1] >
						        1600.0f * 1600.0f ||
						    fabsf(exit_delta[2]) > 256.0f ||
						    !SG_OracleTrainGateExit(
						        gen_seeds[cross_destination].origin,
						        gen_seeds[destination].origin,
						        button, train,
						        &exit_ms) ||
						    pre_cross_cost >
						        RUNE_MAX_COST_MS - cross_ms - exit_ms)
							continue;
						cost = pre_cross_cost + cross_ms + exit_ms;
						for (slot = 1; slot <= 2; slot++)
							if ((new_bits & (1 << (slot - 1))) &&
							    cost < selected_cost[slot])
							{
								selected_destination[slot] =
								    destination;
								selected_cost[slot] = cost;
								selected_cross[slot] =
								    cross_destination;
								selected_cross_ms[slot] = cross_ms;
							}
						if (missing == 0 && crosses &&
						    cost < selected_cost[3])
						{
							selected_destination[3] = destination;
							selected_cost[3] = cost;
							selected_cross[3] = cross_destination;
							selected_cross_ms[3] = cross_ms;
						}
					}
				}
				DoorPose_Restore(&saved, 1);
			}
		}
		if (best_source >= 0 && best_destination >= 0)
		{
			int slot;

			for (slot = 0; slot < TRAIN_SHOOT_DEST_SLOTS; slot++)
			{
				int before = gen_num_links;
				int earlier;

				if (selected_destination[slot] < 0 || selected_cross[slot] < 0 ||
				    selected_cost[slot] <= 0 ||
				    selected_cost[slot] > RUNE_MAX_COST_MS)
					continue;
				for (earlier = 0; earlier < slot; earlier++)
					if (selected_destination[earlier] ==
					        selected_destination[slot])
						break;
				if (earlier < slot)
					continue;
				Link_Add(best_source, selected_destination[slot], RL_TRAIN,
				    (short)selected_cost[slot], 0);
				if (gen_num_links > before)
				{
					rune_link_t *link = &gen_links[gen_num_links - 1];

					VectorCopy(best_contact, link->anchor);
					VectorCopy(gen_seeds[selected_cross[slot]].origin,
					    link->mechanism_anchor);
					link->sweep_clear_ms = (uint16_t)selected_cross_ms[slot];
					link->mode = RLCM_PREOPEN;
					if (!Mechanism_BindTrain(link, button, train, closed, open,
					        best_transaction_bound,
					        SG_MECHANISM_CONTROLLER_TRAIN_SHOOT))
						gen_num_links--;
					else
						gen_train_links++;
				}
			}
		}
	}
	#undef TRAIN_SHOOT_DEST_SLOTS
}

/* Link one canonical door team at its exact STATE_TOP pose for a synchronous
 * egress proof. Rune generation already made the team SOLID_NOT; restoring
 * every authoritative field (including linkcount) makes `sv rune` invisible
 * to a live server once this scope ends. */
static int DoorTrigger_Targets(edict_t *trigger, edict_t **doors, int capacity)
{
	uint32_t delay_ms = 0U;

	if (SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms))
		return SG_DeclaredDelayedDoorMembers(trigger, doors, capacity);
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

static edict_t *Lift_EgressDoorStage(const vec3_t top_body,
	uint16_t *member_count_out)
{
	edict_t *match = NULL;
	uint32_t match_delay = 0U;
	int index;

	if (member_count_out)
		*member_count_out = 0U;
	if (!top_body || !member_count_out)
		return NULL;
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *trigger = &g_edicts[index];
		edict_t *members[RUNE_MAX_MECHANISM_MEMBERS];
		uint32_t delay_ms;
		int count;

		if (!Lift_DoorStageDelay(trigger, &delay_ms, false) ||
		    !Lift_DoorStageTouchMatches(trigger, top_body, false))
			continue;
		count = DoorTrigger_Targets(trigger, members,
			RUNE_MAX_MECHANISM_MEMBERS);
		if (count <= 0 || count >= RUNE_MAX_MECHANISM_MEMBERS)
			return NULL;
		if (match &&
		    (delay_ms != match_delay ||
		     !Lift_DoorStageSameSet(match, trigger)))
			return NULL;
		if (!match)
		{
			match = trigger;
			match_delay = delay_ms;
			*member_count_out = (uint16_t)count;
		}
	}
	return match;
}

static int Gen_CompoundLiftEgressSeed(const vec3_t top_body, float horiz,
	edict_t *plat, edict_t **trigger_out, uint16_t *member_count_out,
	int *egress_ms_out)
{
	door_pose_t saved[RUNE_MAX_MECHANISM_MEMBERS];
	edict_t *trigger;
	uint32_t delay_ms = 0U;
	uint16_t member_count;
	float best_distance = 1e30f;
	int best = -1;
	int travel_ms = 0;
	int pose_count;
	int seed;

	if (trigger_out)
		*trigger_out = NULL;
	if (member_count_out)
		*member_count_out = 0U;
	if (egress_ms_out)
		*egress_ms_out = 0;
	if (!top_body || !plat || !trigger_out || !member_count_out ||
	    !egress_ms_out)
		return -1;
	trigger = Lift_EgressDoorStage(top_body, &member_count);
	if (!trigger || !Lift_DoorStageDelay(trigger, &delay_ms, false) ||
	    delay_ms > INT_MAX ||
	    (travel_ms = Door_TravelMs(trigger)) <= 0)
		return -1;
	pose_count = DoorTrigger_Open(trigger, saved,
		RUNE_MAX_MECHANISM_MEMBERS);
	if (pose_count != (int)member_count)
	{
		if (pose_count > 0)
			DoorPose_Restore(saved, pose_count);
		return -1;
	}
	for (seed = 0; seed < gen_num_seeds; seed++)
	{
		vec3_t delta;
		float horizontal_squared;
		float distance_squared;
		int rollout_ms;

		if (!gen_source_stable[seed] || gen_source_waterlevel[seed] != 0 ||
		    !Gen_SeedHasOutgoing(seed))
			continue;
		VectorSubtract(gen_seeds[seed].origin, top_body, delta);
		if (fabsf(delta[2]) > 16.0f)
			continue;
		horizontal_squared = delta[0] * delta[0] + delta[1] * delta[1];
		if (horizontal_squared > horiz * horiz ||
		    !SG_OracleDeclaredCompoundLiftEgress(top_body,
		        gen_seeds[seed].origin, plat, trigger, &rollout_ms))
			continue;
		distance_squared = horizontal_squared + delta[2] * delta[2];
		if (distance_squared < best_distance)
		{
			best_distance = distance_squared;
			best = seed;
			*egress_ms_out = (int)delay_ms + travel_ms + rollout_ms;
		}
	}
	DoorPose_Restore(saved, pose_count);
	if (best >= 0)
	{
		*trigger_out = trigger;
		*member_count_out = member_count;
	}
	return best;
}

static int Gen_CompoundLiftDoorExit(const vec3_t body, edict_t *plat,
	edict_t **trigger_out, uint16_t *member_count_out, int *egress_ms_out)
{
	int best = -1;
	float best_distance = 1.0e30f;
	int trigger_index;

	if (trigger_out) *trigger_out = NULL;
	if (member_count_out) *member_count_out = 0U;
	if (egress_ms_out) *egress_ms_out = 0;
	if (!body || !plat || !trigger_out || !member_count_out || !egress_ms_out)
		return -1;
	for (trigger_index = 1; trigger_index < globals.num_edicts;
	     trigger_index++)
	{
		edict_t *trigger = &g_edicts[trigger_index];
		edict_t *members[RUNE_MAX_MECHANISM_MEMBERS];
		door_pose_t saved[RUNE_MAX_MECHANISM_MEMBERS];
		uint32_t delay_ms;
		int member_count, travel_ms, pose_count, seed;

		if (!Lift_DoorStageDelay(trigger, &delay_ms, true) || delay_ms > INT_MAX)
			continue;
		member_count = DoorTrigger_Targets(trigger, members,
		    RUNE_MAX_MECHANISM_MEMBERS);
		travel_ms = Door_TravelMs(trigger);
		if (member_count <= 0 || member_count >= RUNE_MAX_MECHANISM_MEMBERS ||
		    travel_ms <= 0)
			continue;
		pose_count = DoorTrigger_Open(trigger, saved,
		    RUNE_MAX_MECHANISM_MEMBERS);
		if (pose_count != member_count)
		{
			if (pose_count > 0) DoorPose_Restore(saved, pose_count);
			continue;
		}
		for (seed = 0; seed < gen_num_seeds; seed++)
		{
			vec3_t delta;
			float distance2;
			int rollout_ms;

			if (!gen_source_stable[seed] ||
			    gen_source_waterlevel[seed] != 0 ||
			    !Gen_SeedHasOutgoing(seed) ||
			    !Lift_DoorStageCrossesSweep(trigger, body,
			        gen_seeds[seed].origin, true))
				continue;
			VectorSubtract(gen_seeds[seed].origin, body, delta);
			if (fabsf(delta[2]) > 16.0f ||
			    delta[0] * delta[0] + delta[1] * delta[1] >
			        HOOK_PAIR_REACH * HOOK_PAIR_REACH ||
			    !SG_OracleDeclaredCompoundLiftEgress(body,
			        gen_seeds[seed].origin, plat, trigger, &rollout_ms))
				continue;
			distance2 = DotProduct(delta, delta);
			if (distance2 < best_distance)
			{
				best_distance = distance2;
				best = seed;
				*trigger_out = trigger;
				*member_count_out = (uint16_t)member_count;
				*egress_ms_out = (int)delay_ms + travel_ms + rollout_ms;
			}
		}
		DoorPose_Restore(saved, pose_count);
	}
	return best;
}

static qboolean Button_Displacement(edict_t *button, vec3_t displacement)
{
	const rune_mechanism_node_t *node;
	sg_mech_button_endpoints_t endpoints;
	uint32_t key = Mechanism_EntityKey(button);
	int axis;

	if (displacement)
		VectorClear(displacement);
	node = key == SG_MECH_NO_KEY ? NULL : Mechanism_Node(key);
	if (!button || !displacement || !node ||
	    !SG_MechCatalogButtonBottomEndpoints(key, node, button, &endpoints))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		int delta = (int)endpoints.end_q8[axis] -
		    (int)endpoints.start_q8[axis];

		if (delta < INT16_MIN || delta > INT16_MAX)
			return false;
		displacement[axis] = (float)delta * 0.125f;
	}
	return !VectorCompare(displacement, vec3_origin);
}

static qboolean Button_TopPoseBegin(edict_t *button, door_pose_t *saved,
	const vec3_t displacement)
{
	const rune_mechanism_node_t *node;
	sg_mech_button_endpoints_t endpoints;
	uint32_t key = Mechanism_EntityKey(button);
	int axis;

	node = key == SG_MECH_NO_KEY ? NULL : Mechanism_Node(key);
	if (!button || !saved || !displacement || !node ||
	    VectorCompare((vec_t *)displacement, vec3_origin) ||
	    !SG_MechCatalogButtonBottomEndpoints(key, node, button, &endpoints))
		return false;
	for (axis = 0; axis < 3; axis++)
		if ((float)((int)endpoints.end_q8[axis] -
		        (int)endpoints.start_q8[axis]) * 0.125f != displacement[axis])
			return false;
	memset(saved, 0, sizeof(*saved));
	saved->ent = button;
	VectorCopy(button->s.origin, saved->origin);
	VectorCopy(button->s.old_origin, saved->old_origin);
	VectorCopy(button->s.angles, saved->angles);
	VectorCopy(button->velocity, saved->velocity);
	VectorCopy(button->avelocity, saved->avelocity);
	saved->state = button->moveinfo.state;
	saved->solid = button->solid;
	saved->linkcount = button->linkcount;
	for (axis = 0; axis < 3; axis++)
	{
		button->s.origin[axis] = (float)endpoints.end_q8[axis] * 0.125f;
		button->s.old_origin[axis] = button->s.origin[axis];
	}
	VectorClear(button->velocity);
	VectorClear(button->avelocity);
	button->moveinfo.state = SG_PLAT_STATE_TOP;
	button->solid = SOLID_BSP;
	sg_host.linkentity(button);
	return true;
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
		double nominal;

		if (!isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) || member->moveinfo.speed <= 0.0f)
			return 0;
		nominal = fabs((double)member->moveinfo.distance) /
		    (double)member->moveinfo.speed * 1000.0;
		if (!isfinite(nominal) || nominal <= 0.0 || nominal > 12500.0)
			return 0;
		ms = (int)ceil(nominal) + 200;
		if (ms > 12500)
			return 0;
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
	int count, i, cyclic = 0;
	int64_t longest_cycle = 0;
	int64_t trigger_ms;

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
		int64_t travel, hold, cycle;
		double hold_ms, nominal;

		if (!isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) || member->moveinfo.speed <= 0.0f ||
		    !isfinite(member->moveinfo.wait))
			return -1;
		if (member->moveinfo.wait < 0.0f)
			continue;
		cyclic++;
		nominal = fabs((double)member->moveinfo.distance) /
		    (double)member->moveinfo.speed * 1000.0;
		if (!isfinite(nominal) || nominal <= 0.0 || nominal > 12500.0)
			return -1;
		travel = (int64_t)ceil(nominal) + 200;
		if (travel > 12500)
			return -1;
		hold_ms = (double)member->moveinfo.wait * 1000.0;
		if (!isfinite(hold_ms) || hold_ms < 0.0 ||
		    hold_ms >= (double)INT64_MAX)
			return -1;
		hold = (int64_t)ceil(hold_ms);
		if (hold > INT64_MAX - 2 * travel)
			return -1;
		cycle = 2 * travel + hold;
		if (cycle > longest_cycle)
			longest_cycle = cycle;
	}
	if (!cyclic)
		return 0;
	return trigger_ms > longest_cycle
	    ? (int)(trigger_ms - longest_cycle) : 0;
}

#ifdef SG_RUNE_TIMING_TEST
static void *Drop_PrefixTestAllocationFailure(int size)
{
	(void)size;
	return NULL;
}

int SG_RuneTestDropPrefixCacheCases(void)
{
	drop_prefix_cache_t local;
	drop_prefix_chunk_t local_chunk;
	drop_prefix_cache_t *entry;
	usercmd_t command, mismatch;
	sg_phantom_t phantom, expected;
	vec3_t source = { 1.0f, 2.0f, 3.0f };
	vec3_t next_source = { 4.0f, 5.0f, 6.0f };
	vec3_t lip = { 7.0f, 8.0f, 9.0f };
	qboolean clean = true;
	void *(*allocate)(int) = sg_host.game_alloc;
	int failures = 0;
	int index;

	memset(&local, 0, sizeof(local));
	memset(&local_chunk, 0, sizeof(local_chunk));
	memset(&command, 0, sizeof(command));
	memset(&phantom, 0, sizeof(phantom));
	command.msec = SG_REPLAY_STEP_MS;
	phantom.pms.origin[0] = 80;
	local.chunk[0] = &local_chunk;
	local_chunk.frame[0].command = command;
	local_chunk.frame[0].phantom = phantom;
	local_chunk.frame[0].clean = false;
	local.count = 1;

	index = 0;
	memset(&expected, 0, sizeof(expected));
	if (!Drop_PrefixReplay(&local, &index, &command, false,
	                       &expected, &clean) || index != 1 || clean ||
	    memcmp(&expected, &phantom, sizeof(expected)) != 0)
		failures |= 1;
	mismatch = command;
	mismatch.forwardmove = 1;
	index = 0;
	if (Drop_PrefixReplay(&local, &index, &mismatch, false,
	                      &expected, &clean) || index != 0)
		failures |= 2;
	index = 0;
	if (Drop_PrefixReplay(&local, &index, &command, true,
	                      &expected, &clean) || index != 0)
		failures |= 4;

	index = local.count;
	phantom.pms.origin[0] = 160;
	if (!Drop_PrefixRecord(&local, &index, &command, false, &phantom, true) ||
	    local.count != 2 || index != 2 ||
	    local_chunk.frame[1].phantom.pms.origin[0] != 160)
		failures |= 8;
	if (Drop_PrefixRecord(&local, &index, &command, true, &phantom, true) ||
	    local.count != 2 || index != 2)
		failures |= 16;
	local.count = DROP_PREFIX_CHUNK_FRAMES;
	index = local.count;
	sg_host.game_alloc = Drop_PrefixTestAllocationFailure;
	if (Drop_PrefixRecord(&local, &index, &command, false, &phantom, true) ||
	    local.count != DROP_PREFIX_CHUNK_FRAMES || index != local.count)
		failures |= 32;
	sg_host.game_alloc = allocate;

	Drop_PrefixCacheClear();
	entry = Drop_PrefixCacheGet(source, lip, 17);
	if (!entry || Drop_PrefixCacheGet(source, lip, 17) != entry ||
	    entry->next)
		failures |= 64;
	entry = Drop_PrefixCacheGet(next_source, lip, 17);
	if (!entry || entry != drop_prefix_cache || entry->next ||
	    !VectorCompare(entry->source, next_source))
		failures |= 128;
	Drop_PrefixCacheClear();
	return failures;
}

int SG_RuneTestDoorCooldownGapMs(edict_t *trigger)
{
	return Door_CooldownGapMs(trigger);
}

float SG_RuneTestLiftEgressSearchRadius(float halfx, float halfy)
{
	return Lift_EgressSearchRadius(halfx, halfy);
}

int SG_RuneTestPlatformTravelMs(edict_t *platform, float source_z,
	float destination_z)
{
	vec3_t source = { 0.0f, 0.0f, 0.0f };
	vec3_t destination = { 0.0f, 0.0f, 0.0f };

	source[2] = source_z;
	destination[2] = destination_z;
	return Plat_TravelMs(platform, source, destination);
}

qboolean SG_RuneTestLiftEgressSpans(float source_z, float destination_z,
	float source_body_z, float egress_body_z)
{
	vec3_t source = { 0.0f, 0.0f, 0.0f };
	vec3_t destination = { 0.0f, 0.0f, 0.0f };
	vec3_t source_body = { 0.0f, 0.0f, 0.0f };
	vec3_t egress_body = { 0.0f, 0.0f, 0.0f };

	source[2] = source_z;
	destination[2] = destination_z;
	source_body[2] = source_body_z;
	egress_body[2] = egress_body_z;
	return Lift_EgressSpans(source, destination, source_body, egress_body);
}

int SG_RuneTestLiftEgressDoorMemberCount(const vec3_t top_body)
{
	uint16_t member_count = 0U;

	return Lift_EgressDoorStage(top_body, &member_count)
		? (int)member_count : -1;
}
#endif

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

typedef struct door_drop_candidate_s
{
	int destination;
	float score;
	vec3_t lip;
	byte heading;
	byte exit_speed;
	int arrival_ms;
	int sweep_clear_ms;
	qboolean proved;
} door_drop_candidate_t;

static void Door_DropCandidateInsert(const rune_link_t *suffix, float score,
	door_drop_candidate_t *candidates, int capacity)
{
	int i, j;

	if (!suffix || !candidates || capacity <= 0 || suffix->action != RL_DROP ||
	    !isfinite(score))
		return;
	for (i = 0; i < capacity; i++)
		if (candidates[i].destination == suffix->to)
			return;
	for (i = 0; i < capacity; i++)
		if (score < candidates[i].score)
		{
			for (j = capacity - 1; j > i; j--)
				candidates[j] = candidates[j - 1];
			memset(&candidates[i], 0, sizeof(candidates[i]));
			candidates[i].destination = suffix->to;
			candidates[i].score = score;
			VectorCopy(suffix->anchor, candidates[i].lip);
			candidates[i].heading = suffix->heading;
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
	const vec3_t wait_point, edict_t *trigger, rune_action_t action,
	const vec3_t button_displacement,
	sg_button_support_mode_t button_support, int egress_ms)
{
	int i;
	uint16_t sweep_ms = 0U;

	if (action == RL_BUTTON_DOOR)
	{
		int rounded;

		if (!button_displacement ||
		    (button_support != SG_BUTTON_SUPPORT_STATIC &&
		     button_support != SG_BUTTON_SUPPORT_RIDER) || egress_ms <= 0)
			return false;
		rounded = ((egress_ms + SG_RUNE_PROOF_SERVER_FRAME_MS - 1) /
		    SG_RUNE_PROOF_SERVER_FRAME_MS) *
		    SG_RUNE_PROOF_SERVER_FRAME_MS;
		if (rounded <= 0 || rounded > UINT16_MAX || rounded > cost_ms)
			return false;
		sweep_ms = (uint16_t)rounded;
	}

	/* A wait point can legitimately select its source seed as the locally
	 * cheapest open-pose destination.  That proves a controller, but it does
	 * not prove a traversal and the RUNE loader/linter reject self-links. */
	if (from == to)
		return false;

	for (i = 0; i < gen_num_links; i++)
	{
		rune_link_t *link = &gen_links[i];

		if (link->from != from || link->to != to || link->action != action)
			continue;
		if (cost_ms < link->cost_ms)
		{
			if (!(action == RL_BUTTON_DOOR
			      ? Mechanism_BindButtonDoor(link, trigger)
			      : Mechanism_BindDoor(link, trigger)))
				return false;
			link->cost_ms = cost_ms;
			VectorCopy(wait_point, link->anchor);
			if (action == RL_BUTTON_DOOR)
			{
				VectorCopy(button_displacement, link->mechanism_anchor);
				link->sweep_clear_ms = sweep_ms;
				link->mode = button_support == SG_BUTTON_SUPPORT_RIDER
				    ? RLCM_RIDE : RLCM_PREOPEN;
			}
		}
		return false;
	}
	if (!Link_Add(from, to, action, cost_ms, 0))
		return false;
	VectorCopy(wait_point, gen_links[gen_num_links - 1].anchor);
	if (action == RL_BUTTON_DOOR)
	{
		VectorCopy(button_displacement,
		    gen_links[gen_num_links - 1].mechanism_anchor);
		gen_links[gen_num_links - 1].sweep_clear_ms = sweep_ms;
		gen_links[gen_num_links - 1].mode =
		    button_support == SG_BUTTON_SUPPORT_RIDER
		    ? RLCM_RIDE : RLCM_PREOPEN;
	}
	if (!(action == RL_BUTTON_DOOR
	      ? Mechanism_BindButtonDoor(&gen_links[gen_num_links - 1], trigger)
	      : Mechanism_BindDoor(&gen_links[gen_num_links - 1], trigger)))
	{
		gen_num_links--;
		return false;
	}
	return true;
}

/* Link_Doors runs before objective pruning, when the ordinary, swim, lift,
 * and teleport graph is complete but contains no declared door edges.  Keep a
 * frozen view of that topology so a wait point does not collapse every source
 * onto one locally cheap, same-component egress while discarding an already
 * proved route into a flag component. */
typedef sg_compound_gen_game_topology_t door_topology_t;
#define Door_TopologyFree(topology) \
	SG_CompoundGenGameTopologyFree((topology), sg_host.level_free)

typedef struct graph_objective_diag_s
{
	int nearest;
	vec3_t objective;
	vec3_t origin;
	vec3_t delta;
	float score;
	float distance;
	qboolean has_out;
	uint32_t spatial_rejects;
	uint32_t vertical_rejects;
	uint32_t los_rejects;
	uint32_t rotator_rejects;
	uint32_t no_out_rejects;
} graph_objective_diag_t;

static int Graph_ObjectiveRoot(const vec3_t objective, const byte *has_out,
	graph_objective_diag_t *diag);

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

		roots[0] = Graph_ObjectiveRoot(redflag->homeposition, has_out, NULL);
		roots[1] = Graph_ObjectiveRoot(blueflag->homeposition, has_out, NULL);
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
	vec3_t *points, int *count, qboolean automatic)
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
	if (!Lift_DoorStageTouchMatches(trigger, fixed, automatic))
		return;
	{
		uint32_t delay_ms;

		if (!Lift_DoorStageDelay(trigger, &delay_ms, automatic))
			return;
		if (delay_ms == 0U)
		{
			resolved = SG_DeclaredDoorForLink(fixed, fixed);
			if (!SG_DeclaredDoorSameSet(resolved, trigger))
				return;
		}
		else
		{
			resolved = trigger;
			if (!SG_DeclaredDelayedDoorTouchMatches(resolved, fixed))
				return;
		}
	}
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
static int Door_WaitPoints(edict_t *trigger, vec3_t *points,
	qboolean automatic)
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
			Door_WaitInsert(trigger, gen_seeds[i].origin, points, &count,
			    automatic);

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
					Door_WaitInsert(trigger, ground, points, &count,
					    automatic);
			}
	return count;
}

typedef struct button_wait_stats_s
{
	int proposed;
	int grounded;
	int accepted;
	int unrepresentable;
	int duplicate;
	int contact[SG_BUTTON_CONTACT_STATUS_COUNT];
} button_wait_stats_t;

static void Button_WaitInsert(edict_t *button, const vec3_t point,
	vec3_t *points, int *count, button_wait_stats_t *stats)
{
	vec3_t fixed, base;
	sg_button_contact_status_t status;
	int axis, i, radius, dx, dy, dz;
	qboolean found = false;

	if (!button || !point || !points || !count || *count >= DOOR_WAIT_MAX ||
	    !stats)
		return;
	if (!Seed_Representable((vec_t *)point))
	{
		stats->unrepresentable++;
		return;
	}
	for (axis = 0; axis < 3; axis++)
		base[axis] = roundf(point[axis] * 8.0f) * 0.125f;
	/* Collision traces return contact points just outside a brush, while the
	 * serialized q8 anchor can round back onto its inclusive boundary. Search
	 * only the two nearest q8 shells for the closest clear point whose inward
	 * player-hull trace hits this exact button. The subsequent full Pmove
	 * approach remains the authority that proves the chosen point is reachable
	 * and actually fires the stock callback. */
	for (radius = 0; radius <= 2 && !found; radius++)
		for (dz = -radius; dz <= radius && !found; dz++)
			for (dy = -radius; dy <= radius && !found; dy++)
				for (dx = -radius; dx <= radius; dx++)
				{
					if (abs(dx) != radius && abs(dy) != radius &&
					    abs(dz) != radius)
						continue;
					fixed[0] = base[0] + (float)dx * 0.125f;
					fixed[1] = base[1] + (float)dy * 0.125f;
					fixed[2] = base[2] + (float)dz * 0.125f;
					if (!Seed_Representable(fixed))
					{
						stats->unrepresentable++;
						continue;
					}
					status = SG_DeclaredButtonDoorContactStatus(button, fixed);
					if (status >= SG_BUTTON_CONTACT_STATUS_COUNT)
						continue;
					stats->contact[status]++;
					if (status == SG_BUTTON_CONTACT_OK)
					{
						found = true;
						break;
					}
				}
	if (!found)
		return;
	for (i = 0; i < *count; i++)
	{
		vec3_t delta;

		VectorSubtract(points[i], fixed, delta);
		if (fabsf(delta[2]) <= 2.0f &&
		    delta[0] * delta[0] + delta[1] * delta[1] <= 4.0f)
		{
			stats->duplicate++;
			return;
		}
	}
	VectorCopy(fixed, points[*count]);
	(*count)++;
	stats->accepted++;
}

/* A touchable func_button is a solid BSP, not an AREA_TRIGGER.  Sample its
 * standable top (floor-plate buttons are common) plus the four horizontal
 * player-hull contact faces one fixed-point unit outside the brush.  Ground
 * each proposal through the real collision model and retain only points whose
 * short exact hull trace hits this button first. */
static int Button_WaitPoints(edict_t *button, vec3_t *points,
	button_wait_stats_t *stats)
{
	static const float fractions[5] = {
		0.0f, 0.25f, 0.5f, 0.75f, 1.0f
	};
	vec3_t raw_min, raw_max;
	float zprobe[3];
	int count = 0, i, face, fi, zi;

	if (!button || !points || !stats || !SG_DeclaredButtonDoorSafe(button))
		return 0;
	memset(stats, 0, sizeof(*stats));
	for (i = 0; i < gen_num_seeds; i++)
		if (gen_source_waterlevel[i] == 0)
		{
			stats->proposed++;
			stats->grounded++;
			Button_WaitInsert(button, gen_seeds[i].origin, points, &count,
			    stats);
		}
	for (i = 0; i < 3; i++)
	{
		raw_min[i] = button->absmin[i] + 1.0f;
		raw_max[i] = button->absmax[i] - 1.0f;
	}
	zprobe[0] = raw_min[2] + 24.0f;
	zprobe[1] = 0.5f * (raw_min[2] + raw_max[2]);
	zprobe[2] = raw_max[2] - 32.0f;
	for (fi = 0; fi < 5 && count < DOOR_WAIT_MAX; fi++)
		for (zi = 0; zi < 5 && count < DOOR_WAIT_MAX; zi++)
		{
			vec3_t candidate, ground;

			candidate[0] = raw_min[0] + fractions[fi] *
				(raw_max[0] - raw_min[0]);
			candidate[1] = raw_min[1] + fractions[zi] *
				(raw_max[1] - raw_min[1]);
			candidate[2] = raw_max[2] + 24.125f;
			stats->proposed++;
			if (Seed_Ground(candidate, ground))
			{
				stats->grounded++;
				Button_WaitInsert(button, ground, points, &count, stats);
			}
		}
	for (face = 0; face < 4 && count < DOOR_WAIT_MAX; face++)
		for (fi = 0; fi < 5 && count < DOOR_WAIT_MAX; fi++)
			for (zi = 0; zi < 3 && count < DOOR_WAIT_MAX; zi++)
			{
				vec3_t candidate, ground;

				candidate[2] = zprobe[zi];
				if (face < 2)
				{
					candidate[0] = face == 0
					    ? raw_min[0] - 16.125f
					    : raw_max[0] + 16.125f;
					candidate[1] = raw_min[1] + fractions[fi] *
					    (raw_max[1] - raw_min[1]);
				}
				else
				{
					candidate[0] = raw_min[0] + fractions[fi] *
					    (raw_max[0] - raw_min[0]);
					candidate[1] = face == 2
					    ? raw_min[1] - 16.125f
					    : raw_max[1] + 16.125f;
				}
				stats->proposed++;
				if (Seed_Ground(candidate, ground))
				{
					stats->grounded++;
					Button_WaitInsert(button, ground, points, &count, stats);
				}
			}
	return count;
}

/* A door link is deliberately longer than an ordinary local graph edge. Its
 * exact source already overlaps one validated repeatable player trigger and
 * lies outside every pose the whole team can occupy. Runtime touches from
 * rest, waits at that safe point, then runs this same direct controller while
 * the real team is STATE_TOP. The destination must be dry, graph-connected,
 * and outside the complete sweep so ordinary navigation can safely resume. */
static void Link_Doors(door_topology_t *topology)
{
	qboolean have_topology;
	int di;
	int wait_points = 0, approach_trials = 0, egress_trials = 0;
	int drop_suffix_seen = 0, drop_suffix_geometry = 0;
	int drop_suffix_failures_logged = 0;

	have_topology = Door_TopologyBuild(topology);
	if (!have_topology)
		sg_host.dprint("rune: door topology snapshot unavailable; "
		               "using nearest-only egress selection\n");

	for (di = 1; di < globals.num_edicts; di++)
	{
		edict_t *door = &g_edicts[di];
		edict_t *members[16];
		door_pose_t saved[16];
		vec3_t door_wait[DOOR_WAIT_MAX];
		button_wait_stats_t button_stats = { 0 };
		qboolean button_controller;
		qboolean direct_controller;
		int controller_kind;
		rune_action_t door_action;
		int wi, num_wait, member_count, travel_ms, cooldown_gap_ms;

		button_controller = SG_DeclaredButtonDoorSafe(door);
		if (!button_controller && !SG_DeclaredDoorActivatorSafe(door))
			continue;
		direct_controller = !button_controller &&
		    SG_DeclaredDoorDirectActivatorSafe(door);
		controller_kind = button_controller
		    ? SG_MECHANISM_CONTROLLER_BUTTON_DOOR
		    : (direct_controller
		          ? SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
		          : SG_MECHANISM_CONTROLLER_AUTO_DOOR);
		door_action = button_controller ? RL_BUTTON_DOOR : RL_DOOR;
		travel_ms = Door_TravelMs(door);
		if (travel_ms <= 0 || travel_ms > 12500)
			continue;
		cooldown_gap_ms = Door_CooldownGapMs(door);
		if (cooldown_gap_ms < 0)
			continue;
		member_count = DoorTrigger_Targets(door, members, 16);
		if (member_count <= 0)
			continue;
		num_wait = button_controller
		    ? Button_WaitPoints(door, door_wait, &button_stats)
		    : Door_WaitPoints(door, door_wait, false);
		if (button_controller)
			sg_host.dprint("rune: button %d members=%d travel=%d cooldown=%d "
			               "wait=%d proposed=%d grounded=%d accepted=%d "
			               "unrepresentable=%d duplicate=%d contact="
			               "%d/%d/%d/%d/%d/%d/%d/%d/%d "
			               "distance=%.3f speed=%.3f hold=%.3f\n",
			               di, member_count, travel_ms, cooldown_gap_ms,
			               num_wait, button_stats.proposed,
			               button_stats.grounded, button_stats.accepted,
			               button_stats.unrepresentable,
			               button_stats.duplicate,
			               button_stats.contact[SG_BUTTON_CONTACT_OK],
			               button_stats.contact[SG_BUTTON_CONTACT_UNSAFE],
			               button_stats.contact[SG_BUTTON_CONTACT_SWEEP_OCCUPIED],
			               button_stats.contact[SG_BUTTON_CONTACT_BAD_ORIGIN],
			               button_stats.contact[SG_BUTTON_CONTACT_DEGENERATE],
			               button_stats.contact[SG_BUTTON_CONTACT_STARTSOLID],
			               button_stats.contact[SG_BUTTON_CONTACT_ALLSOLID],
			               button_stats.contact[SG_BUTTON_CONTACT_NO_HIT],
			               button_stats.contact[SG_BUTTON_CONTACT_WRONG_HIT],
			               door->moveinfo.distance, door->moveinfo.speed,
			               door->moveinfo.wait);

		/* Wait points are geometry samples, not graph sources: ordinary proof
		 * correctly rejects their scripted touch. Prove a continuous approach
		 * from a connected pre-trigger seed and an open-pose egress to a connected
 * post-door seed.  Button destinations stay dry; a direct trigger may end in
 * supported, non-hazardous waterlevel one because the live suffix oracle uses
 * the same bounded shallow-wade law. */
		for (wi = 0; wi < num_wait; wi++)
		{
			#define DOOR_SOURCE_FAN 24
			#define DOOR_DEST_FAN 48
			#define DOOR_DROP_DEST_FAN 24
			#define DOOR_SUPPORT_MODES 2
			int source, dest, ci, li;
			int sources[DOOR_SOURCE_FAN], dests[DOOR_DEST_FAN];
			int drop_dests[DOOR_SUPPORT_MODES][DOOR_DROP_DEST_FAN];
			door_drop_candidate_t drop_suffixes[DOOR_SUPPORT_MODES]
			    [DOOR_DROP_DEST_FAN];
			int egress_ms[DOOR_SUPPORT_MODES][DOOR_DEST_FAN];
			float source_scores[DOOR_SOURCE_FAN], dest_scores[DOOR_DEST_FAN];
			float drop_dest_scores[DOOR_SUPPORT_MODES][DOOR_DROP_DEST_FAN];
			float egress_scores[DOOR_SUPPORT_MODES][DOOR_DEST_FAN];
			byte egress_proved[DOOR_SUPPORT_MODES][DOOR_DEST_FAN];
			byte support_enabled[DOOR_SUPPORT_MODES] = { 1, 1 };
			int best_slot[DOOR_SUPPORT_MODES] = { -1, -1 };
			float best_score[DOOR_SUPPORT_MODES] = { 1.0e30f, 1.0e30f };
			vec3_t button_displacement, egress_anchor[DOOR_SUPPORT_MODES];
			door_pose_t button_saved = { 0 };
			int pose_count = 0;
			int support_count = button_controller ? 2 : 1;
			vec_t *wait_point = door_wait[wi];

			if (button_controller
			        ? !SG_DeclaredButtonDoorContactMatches(door, wait_point)
			        : (!SG_DeclaredDoorTouchMatches(door, wait_point) ||
			           !SG_DeclaredDoorSameSet(
			               SG_DeclaredDoorForLink(wait_point, wait_point),
			               door)))
				continue;
			wait_points++;
			VectorClear(button_displacement);
			VectorCopy(wait_point, egress_anchor[0]);
			VectorCopy(wait_point, egress_anchor[1]);
			for (ci = 0; ci < DOOR_SOURCE_FAN; ci++)
			{
				sources[ci] = -1;
				source_scores[ci] = 1.0e30f;
			}
			for (ci = 0; ci < DOOR_DEST_FAN; ci++)
			{
				int mode_index;

				dests[ci] = -1;
				dest_scores[ci] = 1.0e30f;
				for (mode_index = 0; mode_index < DOOR_SUPPORT_MODES;
				     mode_index++)
				{
					egress_ms[mode_index][ci] = 0;
					egress_scores[mode_index][ci] = 1.0e30f;
					egress_proved[mode_index][ci] = 0;
				}
			}
			for (ci = 0; ci < DOOR_DROP_DEST_FAN; ci++)
			{
				int mode_index;

				for (mode_index = 0; mode_index < DOOR_SUPPORT_MODES;
				     mode_index++)
				{
					drop_dests[mode_index][ci] = -1;
					drop_dest_scores[mode_index][ci] = 1.0e30f;
					memset(&drop_suffixes[mode_index][ci], 0,
					    sizeof(drop_suffixes[mode_index][ci]));
					drop_suffixes[mode_index][ci].destination = -1;
					drop_suffixes[mode_index][ci].score = 1.0e30f;
				}
			}
			if (button_controller)
			{
				if (!Button_Displacement(door, button_displacement))
					continue;
				VectorAdd(wait_point, button_displacement,
				    egress_anchor[1]);
				/* A carried body moves before the target doors are activated.  Its
				 * complete BOTTOM-to-TOP segment must therefore remain outside the
				 * targeted team sweep, not merely both endpoints. */
				if (SG_DeclaredDoorCrossesSweep(door, wait_point,
				        egress_anchor[1]) ||
				    !SG_OracleButtonCarryClear(door, wait_point,
				        egress_anchor[1], false))
					support_enabled[1] = 0;
			}
			for (dest = 0; dest < gen_num_seeds; dest++)
			{
				float score = 1.0e30f;
				int mode_index;
				qboolean drop_destination_water =
				    (gen_seeds[dest].flags & RSF_WATER) != 0;

				/* Unlike ordinary door egress, a DROP may terminate in safe
				 * deep water and does not require a standable destination seed.
				 * Apply its endpoint law before the ordinary dry/wade gate. */
				if (Gen_SeedHasOutgoing(dest) &&
				    SG_DeclaredDoorOutsideSweep(door,
				        gen_seeds[dest].origin) &&
				    SG_ActionEndpointAllowed(RL_DOOR_DROP, 0,
				        drop_destination_water) &&
				    (!drop_destination_water ||
				     !(sg_host.pointcontents(gen_seeds[dest].origin) &
				       (CONTENTS_SLIME | CONTENTS_LAVA))))
					for (mode_index = 0; mode_index < support_count;
					     mode_index++)
					{
						vec3_t delta;
						float h2, drop_score;

						if (!support_enabled[mode_index])
							continue;
						VectorSubtract(gen_seeds[dest].origin,
						    egress_anchor[mode_index], delta);
						h2 = delta[0] * delta[0] +
						     delta[1] * delta[1];
						if (delta[2] < -48.0f && delta[2] >= -2048.0f &&
						    h2 <= SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX *
						          SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX)
						{
							drop_score = h2 + delta[2] * delta[2];
							Door_CandidateInsert(dest, drop_score,
							    drop_dests[mode_index],
							    drop_dest_scores[mode_index],
							    DOOR_DROP_DEST_FAN);
						}
					}

				if (!gen_source_stable[dest] ||
				    !SG_OracleDoorEgressWaterSafe(controller_kind,
				        gen_source_waterlevel[dest],
				        gen_source_watertype[dest]) ||
				    !Gen_SeedHasOutgoing(dest) ||
				    !SG_DeclaredDoorOutsideSweep(door,
				        gen_seeds[dest].origin))
					continue;
				for (mode_index = 0; mode_index < support_count; mode_index++)
				{
					vec3_t delta;
					float h2, candidate_score;

					if (!support_enabled[mode_index])
						continue;

					VectorSubtract(gen_seeds[dest].origin,
					    egress_anchor[mode_index], delta);
					h2 = delta[0] * delta[0] + delta[1] * delta[1];
					if (h2 > 768.0f * 768.0f ||
					    fabsf(delta[2]) > 96.0f ||
					    !SG_DeclaredDoorCrossesSweep(door,
					        egress_anchor[mode_index],
					        gen_seeds[dest].origin))
						continue;
					candidate_score = h2 + delta[2] * delta[2];
					if (candidate_score < score)
						score = candidate_score;
				}
				if (score < 1.0e30f)
					Door_CandidateInsert(dest, score, dests, dest_scores,
					    DOOR_DEST_FAN);
			}
			/* A separately proven ordinary DROP immediately beyond an eligible
			 * TOP-pose door egress is strong discovery evidence for D_DROP.
			 * Prioritize its destination in the bounded fan, but still require
			 * the later single-phantom replay from the exact door contact. */
			for (li = 0; li < gen_num_links; li++)
			{
				const rune_link_t *suffix = &gen_links[li];
				int mode_index;

				if (suffix->action != RL_DROP || suffix->from < 0 ||
				    suffix->from >= gen_num_seeds || suffix->to < 0 ||
				    suffix->to >= gen_num_seeds)
					continue;
				drop_suffix_seen++;
				for (mode_index = 0; mode_index < support_count;
				     mode_index++)
				{
					vec3_t final_delta;
					float final_h2, suffix_score;

					if (!support_enabled[mode_index])
						continue;
					VectorSubtract(gen_seeds[suffix->to].origin,
					    egress_anchor[mode_index], final_delta);
					final_h2 = final_delta[0] * final_delta[0] +
					    final_delta[1] * final_delta[1];
					if (final_delta[2] >= -48.0f ||
					    final_h2 >
					        SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX *
					        SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX)
						continue;
					drop_suffix_geometry++;
					suffix_score = final_h2 +
					    final_delta[2] * final_delta[2];
					Door_CandidateInsert(suffix->to,
					    suffix_score,
					    drop_dests[mode_index],
					    drop_dest_scores[mode_index],
					    DOOR_DROP_DEST_FAN);
					Door_DropCandidateInsert(suffix, suffix_score,
					    drop_suffixes[mode_index],
					    DOOR_DROP_DEST_FAN);
				}
			}
			/* The approach law is proved against the untouched BOTTOM/closed
			 * world.  Only after candidate discovery do we publish one atomic
			 * TOP proof pose for the door team and, for BUTTON_DOOR, the sealed
			 * entry endpoint. */
			pose_count = DoorTrigger_Open(door, saved, 16);
			if (pose_count <= 0)
				continue;
			if (button_controller &&
			    !Button_TopPoseBegin(door, &button_saved,
			        button_displacement))
			{
				DoorPose_Restore(saved, pose_count);
				continue;
			}
			for (int mode_index = 0; mode_index < support_count; mode_index++)
				for (ci = 0; ci < DOOR_DROP_DEST_FAN &&
				     drop_suffixes[mode_index][ci].destination >= 0; ci++)
				{
					const door_drop_candidate_t *candidate =
					    &drop_suffixes[mode_index][ci];
					short drop_ms;
					byte drop_exit;
					sg_drop_trial_t trial;

					dest = candidate->destination;
					gen_door_drop_trials++;
					if (Drop_Rollout(egress_anchor[mode_index],
					        gen_seeds[dest].origin,
					        (vec_t *)candidate->lip, candidate->heading,
					        (gen_seeds[dest].flags & RSF_WATER) != 0,
					        &drop_ms, &drop_exit, &trial, door, members[0]))
					{
						door_drop_candidate_t *proved_candidate =
						    &drop_suffixes[mode_index][ci];

						gen_door_drop_proofs++;
						proved_candidate->arrival_ms = drop_ms;
						proved_candidate->sweep_clear_ms =
						    trial.sweep_clear_ms;
						proved_candidate->exit_speed = drop_exit;
						proved_candidate->proved = true;
						sg_host.dprint("rune: door-drop suffix probe "
						               "trigger=%d wait=%d mode=%d dest=%d "
						               "suffix=%d lip=(%.3f %.3f %.3f) "
						               "heading=%u exit=%u\n", di, wi,
						               mode_index, dest, (int)drop_ms,
						               candidate->lip[0], candidate->lip[1],
						               candidate->lip[2],
						               (unsigned int)candidate->heading,
						               (unsigned int)drop_exit);
					}
					else if (drop_suffix_failures_logged < 8)
					{
						drop_suffix_failures_logged++;
						sg_host.dprint("rune: door-drop suffix reject "
						               "trigger=%d wait=%d mode=%d dest=%d "
						               "reason=%s lip=(%.3f %.3f %.3f) "
						               "heading=%u end=(%.3f %.3f %.3f)\n",
						               di, wi, mode_index, dest,
						               SG_ReplayReasonName(trial.reason),
						               candidate->lip[0], candidate->lip[1],
						               candidate->lip[2],
						               (unsigned int)candidate->heading,
							               trial.end.origin[0],
							               trial.end.origin[1],
						               trial.end.origin[2]);
					}
				}
			for (int mode_index = 0; mode_index < support_count; mode_index++)
				for (ci = 0; ci < DOOR_DROP_DEST_FAN &&
				     drop_dests[mode_index][ci] >= 0; ci++)
				{
					vec3_t drop_lip;
					short drop_ms;
					byte drop_heading, drop_exit;

					dest = drop_dests[mode_index][ci];
					gen_door_drop_trials++;
					if (ProveDropPoints(egress_anchor[mode_index],
					        gen_seeds[dest].origin,
					        (gen_seeds[dest].flags & RSF_WATER) != 0,
					        SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX,
					        drop_lip, &drop_heading, &drop_ms, &drop_exit))
					{
						gen_door_drop_proofs++;
						sg_host.dprint("rune: door-drop probe "
						               "trigger=%d wait=%d mode=%d dest=%d "
						               "suffix=%d lip=(%.3f %.3f %.3f) "
						               "heading=%u exit=%u\n",
						               di, wi, mode_index, dest,
						               (int)drop_ms, drop_lip[0],
						               drop_lip[1], drop_lip[2],
						               (unsigned int)drop_heading,
						               (unsigned int)drop_exit);
					}
				}
			for (ci = 0; ci < DOOR_DEST_FAN && dests[ci] >= 0; ci++)
			{
				int mode_index;

				dest = dests[ci];
				for (mode_index = 0; mode_index < support_count; mode_index++)
				{
					vec3_t delta;
					float h2, score;
					int trial_ms;

					if (!support_enabled[mode_index])
						continue;
					egress_trials++;
					Rune_TelemetryAdd(&gen_telemetry.door_replays, 1U);
					if (!(button_controller
					          ? SG_OracleDeclaredButtonDoorTopEgress(
					                egress_anchor[mode_index],
					                gen_seeds[dest].origin, door, NULL, &trial_ms,
					                mode_index == 1 ? SG_BUTTON_SUPPORT_RIDER :
					                    SG_BUTTON_SUPPORT_STATIC)
					          : SG_OracleDeclaredDoorEgress(
					                egress_anchor[mode_index],
					                gen_seeds[dest].origin, door, NULL, &trial_ms)))
						continue;
					VectorSubtract(gen_seeds[dest].origin,
					    egress_anchor[mode_index], delta);
					h2 = delta[0] * delta[0] + delta[1] * delta[1];
					score = (float)trial_ms + sqrtf(h2) + fabsf(delta[2]);
					egress_ms[mode_index][ci] = trial_ms;
					egress_scores[mode_index][ci] = score;
					egress_proved[mode_index][ci] = 1;
					if (score < best_score[mode_index])
					{
						best_score[mode_index] = score;
						best_slot[mode_index] = ci;
					}
				}
			}
			if (button_controller)
				DoorPose_Restore(&button_saved, 1);
			DoorPose_Restore(saved, pose_count);
			if (best_slot[0] < 0 && best_slot[1] < 0)
				continue;
			/* DIRECT_TRIGGER_DOOR uses one symmetric safe-wade law for both
			 * the approach source and egress destination.  AUTO_DOOR and
			 * BUTTON_DOOR remain dry-only through the same controller gate. */
			for (source = 0; source < gen_num_seeds; source++)
			{
				vec3_t approach_delta;
				float approach_h2, score;
				int approach_destination = best_slot[0] >= 0
				    ? dests[best_slot[0]] : -1;

				if (!gen_source_stable[source] ||
				    !SG_OracleDoorEgressWaterSafe(controller_kind,
				        gen_source_waterlevel[source],
				        gen_source_watertype[source]) ||
				    !Gen_SeedHasIncoming(source) ||
				    !(button_controller
				          ? SG_DeclaredButtonDoorApproachSourceClear(door,
				                gen_seeds[source].origin)
				          : SG_DeclaredDoorApproachSourceClear(door,
				                gen_seeds[source].origin)))
					continue;
				VectorSubtract(wait_point, gen_seeds[source].origin,
				               approach_delta);
				approach_h2 = approach_delta[0] * approach_delta[0] +
				              approach_delta[1] * approach_delta[1];
				if (!Door_ApproachEnvelopeEligible(controller_kind,
				        approach_destination, approach_delta))
					continue;
				score = approach_h2 + approach_delta[2] * approach_delta[2];
				Door_CandidateInsert(source, score, sources, source_scores,
				                     DOOR_SOURCE_FAN);
			}
			for (ci = 0; ci < DOOR_SOURCE_FAN && sources[ci] >= 0; ci++)
			{
				int approach_ms, touch_ms;
				int picked[4], picked_count = 0, pi;
				int mode_index = 0;
				vec3_t picked_approach_delta;
				sg_button_support_mode_t support_mode =
				    SG_BUTTON_SUPPORT_NONE;

				source = sources[ci];
				approach_trials++;
				Rune_TelemetryAdd(&gen_telemetry.door_replays, 1U);
				if (button_controller)
				{
					if (!SG_OracleDeclaredButtonDoorApproach(
					        gen_seeds[source].origin, wait_point, door,
					        &approach_ms, &touch_ms, &support_mode))
						continue;
					mode_index = support_mode == SG_BUTTON_SUPPORT_RIDER ? 1 :
					    support_mode == SG_BUTTON_SUPPORT_STATIC ? 0 : -1;
				}
				else if (!SG_OracleDeclaredDoorApproach(
				             gen_seeds[source].origin, wait_point, door,
				             &approach_ms, &touch_ms))
					continue;
				if (mode_index < 0 || best_slot[mode_index] < 0)
					continue;
				VectorSubtract(wait_point, gen_seeds[source].origin,
				    picked_approach_delta);

				/* Preserve the locally cheapest proved controller as a movement
				 * shortcut, then add only bounded topology-improving witnesses. */
				picked[picked_count++] = best_slot[mode_index];
				if (have_topology)
				{
					int missing = 3 & ~topology->objective_mask[source];
					int bit;

					for (bit = 1; bit <= 2; bit <<= 1)
					{
						int choice = -1;
						float choice_score = 1.0e30f;

						if (!(missing & bit))
							continue;
						for (pi = 0; pi < DOOR_DEST_FAN; pi++)
							if (egress_proved[mode_index][pi] &&
								    (topology->objective_mask[dests[pi]] & bit) &&
							    egress_scores[mode_index][pi] < choice_score)
							{
								choice = pi;
								choice_score = egress_scores[mode_index][pi];
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
						if (topology->component[dests[picked[pi]]] !=
						    topology->component[source])
							break;
					if (pi == picked_count)
					{
						int choice = -1;
						float choice_score = 1.0e30f;

						for (pi = 0; pi < DOOR_DEST_FAN; pi++)
							if (egress_proved[mode_index][pi] &&
							    topology->component[dests[pi]] !=
							        topology->component[source] &&
							    egress_scores[mode_index][pi] < choice_score)
							{
								choice = pi;
								choice_score = egress_scores[mode_index][pi];
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
					int contract_cost;

					/* A shallow best egress may admit source discovery, but it
					 * cannot authorize a dry or otherwise ineligible alternate. */
					if (!Door_ApproachEnvelopeEligible(controller_kind,
					        dests[slot], picked_approach_delta))
						continue;
					contract_cost = SG_DeclaredDoorContractCost(door,
					    approach_ms, touch_ms,
					    egress_ms[mode_index][slot]);

					if (contract_cost > 0 && Door_LinkInsert(source,
					        dests[slot], (short)contract_cost, wait_point, door,
					        door_action, button_controller
					            ? button_displacement : NULL,
					        support_mode, egress_ms[mode_index][slot]))
					{
						if (button_controller)
							gen_button_door_links++;
						else
							gen_door_links++;
					}
				}
			}
			#undef DOOR_SOURCE_FAN
			#undef DOOR_DEST_FAN
			#undef DOOR_DROP_DEST_FAN
			#undef DOOR_SUPPORT_MODES
		}
	}
	if (gen_door_links || gen_button_door_links || wait_points)
		sg_host.dprint("rune: %d declared door links, %d button-door links "
		               "(%d wait points, "
		               "%d approach/%d egress trials)\n",
		               gen_door_links, gen_button_door_links, wait_points,
		               approach_trials,
		               egress_trials);
	sg_host.dprint("rune: door-drop suffix discovery seen=%d geometry=%d\n",
	               drop_suffix_seen, drop_suffix_geometry);
}

typedef struct compound_drop_plan_context_s
{
	const sg_compound_world_preopen_t *mechanism;
	const vec3_t *contact;
	const door_drop_candidate_t *drops;
	int drop_count;
} compound_drop_plan_context_t;

static qboolean Compound_DropAnchorOnLattice(const vec3_t anchor)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float scaled = anchor[axis] * 8.0f;

		if (!isfinite(scaled) || scaled < -32768.0f || scaled > 32767.0f ||
		    scaled != (float)(short)scaled)
			return false;
	}
	return true;
}

static rune_reject_reason_t Compound_DropPlanProve(void *opaque, int action,
	const sg_compound_action_gen_candidate_t *candidate,
	sg_compound_action_gen_proof_t *proof)
{
	compound_drop_plan_context_t *context = opaque;
	sg_compound_drop_proof_t exact;
	const door_drop_candidate_t *drop;
	rune_reject_reason_t reason;

	if (!context || !candidate || !proof || action != RL_DOOR_DROP ||
	    candidate->local_rank >= (uint32_t)context->drop_count)
		return RLR_BAD_CONTROL_POLICY;
	drop = &context->drops[candidate->local_rank];
	if (drop->destination != candidate->destination)
		return RLR_BAD_CONTROL_POLICY;
	reason = SG_OracleCompoundDropPreopen(
	    gen_seeds[candidate->source].origin, context->mechanism,
	    *context->contact, gen_seeds[candidate->destination].origin,
	    drop->lip, drop->heading,
	    (gen_seeds[candidate->destination].flags & RSF_WATER) != 0,
	    &exact, false);
	if (reason != RLR_OK)
		return reason;
	memset(proof, 0, sizeof(*proof));
	VectorCopy(drop->lip, proof->suffix_anchor);
	proof->touch_ms = exact.touch_ms;
	proof->touch_frame_end_ms = exact.touch_frame_end_ms;
	proof->mover_top_ms = exact.mover_top_ms;
	proof->suffix_start_ms = exact.suffix_start_ms;
	proof->total_cost_ms = exact.total_cost_ms;
	proof->arrival_ms = exact.arrival_ms;
	proof->sweep_clear_ms = exact.sweep_clear_ms;
	proof->heading = exact.heading;
	proof->heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	proof->exit_speed = exact.exit_speed;
	return RLR_OK;
}

static int Compound_DropPublish(const rune_link_t *links, size_t count)
{
	size_t index;
	int published = 0;

	if ((!links && count > 0) || count > (size_t)LINK_MAX)
		return 0;
	for (index = 0; index < count; index++)
	{
		const rune_link_t *candidate = &links[index];
		int existing = -1;
		int link_index;

		if (candidate->action != RL_DOOR_DROP ||
		    SG_CompoundValidateLink(gen_seeds, (uint32_t)gen_num_seeds,
		                            candidate) != RLR_OK)
			continue;
		for (link_index = 0; link_index < gen_num_links; link_index++)
			if (gen_links[link_index].action == RL_DOOR_DROP &&
			    gen_links[link_index].from == candidate->from &&
			    gen_links[link_index].to == candidate->to)
			{
				existing = link_index;
				break;
			}
		if (existing >= 0)
		{
			if (gen_links[existing].cost_ms <= candidate->cost_ms)
				continue;
			gen_links[existing] = *candidate;
			continue;
		}
		if (gen_num_links >= LINK_MAX)
		{
			gen_link_overflow = true;
			break;
		}
		gen_links[gen_num_links++] = *candidate;
		published++;
	}
	return published;
}

static int Compound_HookPublish(const rune_link_t *links, size_t count)
{
	size_t index;
	int published = 0;

	if ((!links && count > 0) || count > (size_t)LINK_MAX)
		return 0;
	for (index = 0; index < count; index++)
	{
		const rune_link_t *candidate = &links[index];
		int existing = -1;
		int link_index;

		if (candidate->action != RL_DOOR_HOOK ||
		    SG_CompoundValidateLink(gen_seeds, (uint32_t)gen_num_seeds,
		                            candidate) != RLR_OK)
			continue;
		for (link_index = 0; link_index < gen_num_links; link_index++)
			if (gen_links[link_index].action == RL_DOOR_HOOK &&
			    gen_links[link_index].from == candidate->from &&
			    gen_links[link_index].to == candidate->to)
			{
				existing = link_index;
				break;
			}
		if (existing >= 0)
		{
			if (gen_links[existing].cost_ms <= candidate->cost_ms)
				continue;
			gen_links[existing] = *candidate;
			continue;
		}
		if (gen_num_links >= LINK_MAX)
		{
			gen_link_overflow = true;
			break;
		}
		gen_links[gen_num_links++] = *candidate;
		published++;
	}
	return published;
}

static void Link_CompoundDrops(void)
{
	#define COMPOUND_WORLD_MAX 64
	#define COMPOUND_SOURCE_FAN 24
	#define COMPOUND_DROP_FAN 24
	sg_compound_world_candidate_t mechanisms[COMPOUND_WORLD_MAX];
	int mechanism_count = 0;
	int base_link_count = gen_num_links;
	int logged = 0;
	int planner_emitted = 0;
	int planner_proofs = 0;
	int planner_published = 0;
	int contact_failures[128] = { 0 };
	int proof_failures[128] = { 0 };
	door_topology_t topology = { NULL, NULL };
	sg_compound_action_gen_seed_t *planner_seeds = NULL;
	qboolean have_topology;
	int mi;
	rune_reject_reason_t enumerate_reason;

	enumerate_reason = SG_CompoundWorldEnumeratePreopen(mechanisms,
	    COMPOUND_WORLD_MAX, &mechanism_count);
	if (enumerate_reason != RLR_OK)
	{
		sg_host.dprint("rune: door-drop compound discovery reason=%d\n",
		               (int)enumerate_reason);
		return;
	}
	have_topology = Door_TopologyBuild(&topology);
	if (have_topology)
	{
		int seed;

		planner_seeds = sg_host.level_alloc(sizeof(*planner_seeds) *
		    (size_t)gen_num_seeds);
		if (!planner_seeds)
			have_topology = false;
		else
			for (seed = 0; seed < gen_num_seeds; seed++)
			{
				planner_seeds[seed].component = topology.component[seed];
				planner_seeds[seed].objective_mask =
				    topology.objective_mask[seed];
				planner_seeds[seed].water =
				    (gen_seeds[seed].flags & RSF_WATER) != 0;
				planner_seeds[seed].has_incoming = Gen_SeedHasIncoming(seed);
				planner_seeds[seed].has_outgoing = Gen_SeedHasOutgoing(seed);
			}
	}
	for (mi = 0; mi < mechanism_count; mi++)
	{
		sg_compound_world_candidate_t *mechanism = &mechanisms[mi];
		int hi;

		for (hi = 0; hi < mechanism->hint_count; hi++)
		{
			int sources[COMPOUND_SOURCE_FAN];
			float source_scores[COMPOUND_SOURCE_FAN];
			int source, si;

			for (si = 0; si < COMPOUND_SOURCE_FAN; si++)
			{
				sources[si] = -1;
				source_scores[si] = 1.0e30f;
			}
			for (source = 0; source < gen_num_seeds; source++)
			{
				vec3_t delta;
				float horizontal2, score;

				if (!gen_source_stable[source] ||
				    (gen_seeds[source].flags & RSF_WATER) ||
				    gen_source_waterlevel[source] != 0 ||
				    !Gen_SeedHasIncoming(source) ||
				    !SG_CompoundWorldOutsideSweep(&mechanism->resolved,
				        gen_seeds[source].origin))
					continue;
				VectorSubtract(mechanism->hints[hi],
				    gen_seeds[source].origin, delta);
				horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
				if (horizontal2 > 768.0f * 768.0f || fabsf(delta[2]) > 96.0f)
					continue;
				score = horizontal2 + delta[2] * delta[2];
				Door_CandidateInsert(source, score, sources, source_scores,
				    COMPOUND_SOURCE_FAN);
			}
			for (si = 0; si < COMPOUND_SOURCE_FAN && sources[si] >= 0; si++)
			{
				door_drop_candidate_t drops[COMPOUND_DROP_FAN];
				vec3_t contact;
				int li, di;
				rune_reject_reason_t contact_reason;

				source = sources[si];
				contact_reason = SG_OracleCompoundDropDiscoverContact(
				    gen_seeds[source].origin, &mechanism->resolved,
				    mechanism->hints[hi], contact, false);
				if (contact_reason != RLR_OK)
				{
					if ((int)contact_reason >= 0 && (int)contact_reason < 128)
						contact_failures[(int)contact_reason]++;
					continue;
				}
				for (di = 0; di < COMPOUND_DROP_FAN; di++)
				{
					memset(&drops[di], 0, sizeof(drops[di]));
					drops[di].destination = -1;
					drops[di].score = 1.0e30f;
				}
				for (li = 0; li < base_link_count; li++)
				{
					const rune_link_t *suffix = &gen_links[li];
					vec3_t delta;
					float horizontal2, score;

					if (suffix->action != RL_DROP || suffix->to < 0 ||
					    suffix->to >= gen_num_seeds ||
					    !Gen_SeedHasOutgoing(suffix->to) ||
					    !SG_ActionEndpointAllowed(RL_DOOR_DROP, 0,
					        (gen_seeds[suffix->to].flags & RSF_WATER) != 0) ||
					    !SG_CompoundWorldOutsideSweep(&mechanism->resolved,
					        gen_seeds[suffix->to].origin))
						continue;
					VectorSubtract(gen_seeds[suffix->to].origin,
					    contact, delta);
					horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
					if (delta[2] >= -48.0f || delta[2] < -2048.0f ||
					    horizontal2 >
					        SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX *
					        SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX)
						continue;
					score = horizontal2 + delta[2] * delta[2];
					Door_DropCandidateInsert(suffix, score, drops,
					    COMPOUND_DROP_FAN);
				}
				if (have_topology)
				{
					sg_compound_action_gen_candidate_t candidates[
					    COMPOUND_DROP_FAN];
					rune_link_t output[4];
					sg_compound_action_gen_request_t request;
					sg_compound_action_gen_result_t result;
					compound_drop_plan_context_t context;
					int candidate_count = 0;

					for (di = 0; di < COMPOUND_DROP_FAN &&
					     drops[di].destination >= 0; di++)
					{
						if (!Compound_DropAnchorOnLattice(drops[di].lip))
							continue;
						memset(&candidates[candidate_count], 0,
						    sizeof(candidates[candidate_count]));
						candidates[candidate_count].source = source;
						candidates[candidate_count].destination =
						    drops[di].destination;
						candidates[candidate_count].trigger_key =
						    mechanism->resolved.trigger_key;
						candidates[candidate_count].mover_key =
						    mechanism->resolved.mover_key;
						VectorCopy(contact,
						    candidates[candidate_count].mechanism_anchor);
						candidates[candidate_count].local_rank =
						    (uint32_t)di;
						candidates[candidate_count].mode = RLCM_PREOPEN;
						candidate_count++;
					}
					if (candidate_count > 0)
					{
						memset(&context, 0, sizeof(context));
						context.mechanism = &mechanism->resolved;
						context.contact = (const vec3_t *)&contact;
						context.drops = drops;
						context.drop_count = COMPOUND_DROP_FAN;
						memset(&request, 0, sizeof(request));
						request.action = RL_DOOR_DROP;
						request.seeds = planner_seeds;
						request.seed_count = (size_t)gen_num_seeds;
						request.candidates = candidates;
						request.candidate_count = (size_t)candidate_count;
						request.output = output;
						request.output_capacity = 4;
						request.prove = Compound_DropPlanProve;
						request.context = &context;
						request.production_enabled = 1;
						result = SG_CompoundActionGenPlan(&request);
						planner_proofs += (int)result.proof_calls;
						if (result.status == SG_COMPOUND_ACTION_GEN_OK)
						{
							planner_emitted += (int)result.emitted;
							planner_published += Compound_DropPublish(
							    output, result.emitted);
						}
					}
				}
				for (di = 0; di < COMPOUND_DROP_FAN &&
				     drops[di].destination >= 0; di++)
				{
					sg_compound_drop_proof_t proof;
					rune_reject_reason_t reason;
					door_drop_candidate_t *drop = &drops[di];

					gen_door_drop_compound_trials++;
					reason = SG_OracleCompoundDropPreopen(
					    gen_seeds[source].origin, &mechanism->resolved,
					    contact, gen_seeds[drop->destination].origin,
					    drop->lip, drop->heading,
					    (gen_seeds[drop->destination].flags & RSF_WATER) != 0,
					    &proof, false);
					if (reason != RLR_OK)
					{
						if ((int)reason >= 0 && (int)reason < 128)
							proof_failures[(int)reason]++;
						continue;
					}
					gen_door_drop_compound_proofs++;
					if (logged++ < 8)
						sg_host.dprint("rune: door-drop compound proof "
						    "trigger=%d source=%d dest=%d touch=%d top=%d "
						    "suffix=%d clear=%d total=%d\n",
						    mechanism->resolved.trigger_key, source,
						    drop->destination, proof.touch_ms,
						    proof.mover_top_ms, proof.arrival_ms,
						    proof.sweep_clear_ms, proof.total_cost_ms);
				}
			}
		}
	}
	sg_host.dprint("rune: door-drop compound mechanisms=%d trials=%d "
	               "proofs=%d\n", mechanism_count,
	               gen_door_drop_compound_trials,
	               gen_door_drop_compound_proofs);
	for (mi = 0; mi < 128; mi++)
		if (contact_failures[mi] || proof_failures[mi])
			sg_host.dprint("rune: door-drop compound reject reason=%d "
			               "contact=%d proof=%d\n", mi,
			               contact_failures[mi], proof_failures[mi]);
	sg_host.dprint("rune: door-drop planner proofs=%d emitted=%d "
	               "published=%d\n", planner_proofs, planner_emitted,
	               planner_published);
	if (planner_seeds)
		sg_host.level_free(planner_seeds);
	Door_TopologyFree(&topology);
	#undef COMPOUND_WORLD_MAX
	#undef COMPOUND_SOURCE_FAN
	#undef COMPOUND_DROP_FAN
}

typedef struct compound_hook_plan_context_s
{
	const sg_compound_action_gen_candidate_t *candidates;
	const sg_compound_action_gen_proof_t *proofs;
	size_t count;
} compound_hook_plan_context_t;

static rune_reject_reason_t Compound_HookPlanProve(void *opaque, int action,
	const sg_compound_action_gen_candidate_t *candidate,
	sg_compound_action_gen_proof_t *proof);

static void Link_CompoundHooks(void)
{
	#define COMPOUND_HOOK_PRODUCTION 1
	#define COMPOUND_WORLD_MAX 64
	#define COMPOUND_SOURCE_FAN 24
	#define COMPOUND_HOOK_FAN 24
	sg_compound_world_candidate_t mechanisms[COMPOUND_WORLD_MAX];
	int mechanism_count = 0;
	int base_link_count = gen_num_links;
	int oracle_trials = 0;
	int oracle_proofs = 0;
	int planner_emitted = 0;
	int planner_proofs = 0;
	int planner_published = 0;
	int contact_failures[128] = { 0 };
	int proof_failures[128] = { 0 };
	door_topology_t topology = { NULL, NULL };
	sg_compound_action_gen_seed_t *planner_seeds = NULL;
	qboolean have_topology;
	int mi;
	rune_reject_reason_t enumerate_reason;

	enumerate_reason = SG_CompoundWorldEnumeratePreopen(mechanisms,
	    COMPOUND_WORLD_MAX, &mechanism_count);
	if (enumerate_reason != RLR_OK)
	{
		sg_host.dprint("rune: door-hook compound discovery reason=%d\n",
		               (int)enumerate_reason);
		return;
	}
	have_topology = Door_TopologyBuild(&topology);
	if (have_topology)
	{
		int seed;

		planner_seeds = sg_host.level_alloc(sizeof(*planner_seeds) *
		    (size_t)gen_num_seeds);
		if (!planner_seeds)
			have_topology = false;
		else
			for (seed = 0; seed < gen_num_seeds; seed++)
			{
				planner_seeds[seed].component = topology.component[seed];
				planner_seeds[seed].objective_mask =
				    topology.objective_mask[seed];
				planner_seeds[seed].water =
				    (gen_seeds[seed].flags & RSF_WATER) != 0;
				planner_seeds[seed].has_incoming = Gen_SeedHasIncoming(seed);
				planner_seeds[seed].has_outgoing = Gen_SeedHasOutgoing(seed);
			}
	}
	for (mi = 0; mi < mechanism_count; mi++)
	{
		sg_compound_world_candidate_t *mechanism = &mechanisms[mi];
		int hi;

		for (hi = 0; hi < mechanism->hint_count; hi++)
		{
			int sources[COMPOUND_SOURCE_FAN];
			float source_scores[COMPOUND_SOURCE_FAN];
			int source, si;

			for (si = 0; si < COMPOUND_SOURCE_FAN; si++)
			{
				sources[si] = -1;
				source_scores[si] = 1.0e30f;
			}
			for (source = 0; source < gen_num_seeds; source++)
			{
				vec3_t delta;
				float horizontal2, score;

				if (!(gen_seeds[source].flags & RSF_WATER) ||
				    gen_source_waterlevel[source] < 2 ||
				    !(gen_source_watertype[source] & CONTENTS_WATER) ||
				    (gen_source_watertype[source] &
				     (CONTENTS_LAVA | CONTENTS_SLIME)) ||
				    !Gen_SeedHasIncoming(source) ||
				    !SG_CompoundWorldOutsideSweep(&mechanism->resolved,
				        gen_seeds[source].origin))
					continue;
				VectorSubtract(mechanism->hints[hi],
				    gen_seeds[source].origin, delta);
				horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
				if (horizontal2 > 768.0f * 768.0f ||
				    fabsf(delta[2]) > 256.0f)
					continue;
				score = horizontal2 + delta[2] * delta[2];
				Door_CandidateInsert(source, score, sources, source_scores,
				    COMPOUND_SOURCE_FAN);
			}
			for (si = 0; si < COMPOUND_SOURCE_FAN && sources[si] >= 0; si++)
			{
				sg_compound_swim_source_t prepared;
				int destinations[COMPOUND_HOOK_FAN];
				float destination_scores[COMPOUND_HOOK_FAN];
				vec3_t contact;
				int li, di;
				rune_reject_reason_t reason;

				source = sources[si];
				memset(&prepared, 0, sizeof(prepared));
				reason = SG_OracleCompoundSwimPrepareSource(
				    gen_seeds[source].origin, &mechanism->resolved, 0.0f,
				    &prepared, NULL, true, false);
				if (reason != RLR_OK)
				{
					if ((int)reason >= 0 && (int)reason < 128)
						contact_failures[(int)reason]++;
					continue;
				}
				reason = SG_OracleCompoundSwimDiscoverContact(&prepared,
				    &mechanism->resolved, mechanism->hints[hi], contact, NULL,
				    true, false);
				if (reason != RLR_OK)
				{
					if ((int)reason >= 0 && (int)reason < 128)
						contact_failures[(int)reason]++;
					continue;
				}
				for (di = 0; di < COMPOUND_HOOK_FAN; di++)
				{
					destinations[di] = -1;
					destination_scores[di] = 1.0e30f;
				}
				for (li = 0; li < base_link_count; li++)
				{
					const rune_link_t *suffix = &gen_links[li];
					vec3_t delta;
					float score;

					if (suffix->action != RL_HOOK || suffix->to < 0 ||
					    suffix->to >= gen_num_seeds || suffix->to == source ||
					    !Gen_SeedHasOutgoing(suffix->to) ||
					    !SG_ActionEndpointAllowed(RL_DOOR_HOOK, true,
					        (gen_seeds[suffix->to].flags & RSF_WATER) != 0) ||
					    !SG_CompoundWorldOutsideSweep(&mechanism->resolved,
					        gen_seeds[suffix->to].origin))
						continue;
					VectorSubtract(gen_seeds[suffix->to].origin, contact,
					               delta);
					score = DotProduct(delta, delta);
					if (!isfinite(score))
						continue;
					Door_CandidateInsert(suffix->to, score, destinations,
					    destination_scores, COMPOUND_HOOK_FAN);
				}
				if (have_topology && destinations[0] >= 0)
				{
					sg_compound_action_gen_candidate_t candidates[
					    COMPOUND_HOOK_FAN];
					sg_compound_action_gen_proof_t proofs[COMPOUND_HOOK_FAN];
					rune_link_t output[4];
					sg_compound_action_gen_request_t request;
					sg_compound_action_gen_result_t result;
					compound_hook_plan_context_t context;
					int successful = 0;

					for (di = 0; di < COMPOUND_HOOK_FAN &&
					     destinations[di] >= 0; di++)
					{
						sg_compound_hook_proof_t exact;
						sg_phantom_t phantom = prepared.phantom;

						oracle_trials++;
						memset(&exact, 0, sizeof(exact));
						reason = SG_OracleCompoundHookPreopen(&phantom,
						    &mechanism->resolved, contact,
						    gen_seeds[destinations[di]].origin, NULL,
						    prepared.old_frame_z, &exact, NULL, true,
						    false);
						if (reason != RLR_OK)
						{
							if ((int)reason >= 0 && (int)reason < 128)
								proof_failures[(int)reason]++;
							continue;
						}
						oracle_proofs++;
						memset(&candidates[successful], 0,
						    sizeof(candidates[successful]));
						candidates[successful].source = source;
						candidates[successful].destination =
						    destinations[di];
						candidates[successful].trigger_key =
						    mechanism->resolved.trigger_key;
						candidates[successful].mover_key =
						    mechanism->resolved.mover_key;
						VectorCopy(contact,
						    candidates[successful].mechanism_anchor);
						candidates[successful].local_rank =
						    (uint32_t)successful;
						candidates[successful].mode = RLCM_PREOPEN;
						memset(&proofs[successful], 0,
						    sizeof(proofs[successful]));
						VectorCopy(exact.control,
						    proofs[successful].suffix_anchor);
						proofs[successful].touch_ms = exact.touch_ms;
						proofs[successful].touch_frame_end_ms =
						    exact.touch_frame_end_ms;
						proofs[successful].mover_top_ms =
						    exact.mover_top_ms;
						proofs[successful].suffix_start_ms =
						    exact.suffix_start_ms;
						proofs[successful].total_cost_ms =
						    exact.total_cost_ms;
						proofs[successful].arrival_ms = exact.arrival_ms;
						proofs[successful].sweep_clear_ms =
						    exact.sweep_clear_ms;
						proofs[successful].heading_slack =
						    SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
						proofs[successful].exit_speed = exact.exit_speed;
						successful++;
					}
					if (!successful)
						continue;
					memset(&context, 0, sizeof(context));
					context.candidates = candidates;
					context.proofs = proofs;
					context.count = (size_t)successful;
					memset(&request, 0, sizeof(request));
					request.action = RL_DOOR_HOOK;
					request.seeds = planner_seeds;
					request.seed_count = (size_t)gen_num_seeds;
					request.candidates = candidates;
					request.candidate_count = (size_t)successful;
					request.output = output;
					request.output_capacity = 4;
					request.prove = Compound_HookPlanProve;
					request.context = &context;
					request.production_enabled =
					    COMPOUND_HOOK_PRODUCTION;
					result = SG_CompoundActionGenPlan(&request);
					planner_proofs += (int)result.proof_calls;
					if (result.status == SG_COMPOUND_ACTION_GEN_OK)
					{
						planner_emitted += (int)result.emitted;
						planner_published += Compound_HookPublish(output,
						    result.emitted);
					}
				}
			}
		}
	}
	sg_host.dprint("rune: door-hook compound mechanisms=%d trials=%d "
	               "proofs=%d planner_proofs=%d emitted=%d published=%d\n",
	               mechanism_count, oracle_trials, oracle_proofs,
	               planner_proofs, planner_emitted, planner_published);
	for (mi = 0; mi < 128; mi++)
		if (contact_failures[mi] || proof_failures[mi])
			sg_host.dprint("rune: door-hook compound reject reason=%d "
			               "contact=%d proof=%d\n", mi,
			               contact_failures[mi], proof_failures[mi]);
	if (planner_seeds)
		sg_host.level_free(planner_seeds);
	Door_TopologyFree(&topology);
	#undef COMPOUND_HOOK_PRODUCTION
	#undef COMPOUND_WORLD_MAX
	#undef COMPOUND_SOURCE_FAN
	#undef COMPOUND_HOOK_FAN
}

static rune_reject_reason_t Compound_HookPlanProve(void *opaque, int action,
	const sg_compound_action_gen_candidate_t *candidate,
	sg_compound_action_gen_proof_t *proof)
{
	compound_hook_plan_context_t *context = opaque;
	const sg_compound_action_gen_candidate_t *stored;

	if (!context || !candidate || !proof || action != RL_DOOR_HOOK ||
	    !context->candidates || !context->proofs ||
	    candidate->local_rank >= context->count)
		return RLR_BAD_CONTROL_POLICY;
	stored = &context->candidates[candidate->local_rank];
	if (stored->source != candidate->source ||
	    stored->destination != candidate->destination)
		return RLR_BAD_CONTROL_POLICY;
	*proof = context->proofs[candidate->local_rank];
	return RLR_OK;
}

#undef DOOR_WAIT_MAX



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
		sg_rocketjump_cadence_t cadence;
		sg_rocketjump_cadence_event_t cadence_event;
		usercmd_t cmd;
		vec3_t aim, shot_angles, boom, before, kvel, want;
		float flight_ms, t = tilts[ai] * (float)(M_PI / 180.0);
		int elapsed, health;
		short pitch_control, yaw_control;
		byte heading;

		/* down, and back over the shoulder: the horizontal part of the aim
		 * points AWAY from the destination, which is what throws the body
		 * toward it */
		aim[0] = -tdir[0] * sinf(t);
		aim[1] = -tdir[1] * sinf(t);
		aim[2] = -cosf(t);
		vectoangles(aim, shot_angles);
		pitch_control = (short)ANGLE2SHORT(shot_angles[PITCH]);
		yaw_control = (short)ANGLE2SHORT(shot_angles[YAW]);
		shot_angles[PITCH] = SHORT2ANGLE(pitch_control);
		shot_angles[YAW] = SHORT2ANGLE(yaw_control);
		shot_angles[ROLL] = 0.0f;
		AngleVectors(shot_angles, aim, NULL, NULL);

		SG_OraclePlace(&ph, src);
		elapsed = 0;

		/*
		 * The jump. Tapped, never held (PM_CheckJump refuses a held key), and
		 * the height of it is pmove's business -- this only presses the key.
		 */
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = STEP_MSEC;
		cmd.angles[PITCH] = pitch_control;
		cmd.angles[YAW] = yaw_control;
		cmd.upmove = 400;
		SG_OracleRun(&ph, &cmd, 1);
		elapsed += STEP_MSEC;

		if (!SG_OracleRocketJumpAim(ph.origin, pitch_control, yaw_control,
		        boom, &flight_ms))
		{
			rj_noboom++;
			continue;
		}

		if (!SG_RocketJumpCadenceBegin(&cadence, flight_ms,
		    SG_RUNE_PROOF_SERVER_FRAME_MS))
		{
			rj_noboom++;
			continue;
		}
		health = 0;
		VectorClear(kvel);
		while ((cadence_event = SG_RocketJumpCadenceNext(&cadence)) !=
		       SG_ROCKETJUMP_CADENCE_DONE)
		{
			/* G_RunFrame advances projectiles before SG_RunFrame emits the
			 * next body quartet.  The launch command itself moves the body
			 * first; fire_rocket then starts at that post-25 ms pose. */
			if (cadence_event == SG_ROCKETJUMP_CADENCE_IMPACT)
			{
				VectorCopy(ph.velocity, before);
				health = SG_OracleRocketJumpStep(&ph, boom);
				VectorSubtract(ph.velocity, before, kvel);
			}
			else if (cadence_event == SG_ROCKETJUMP_CADENCE_BODY_STEP)
			{
				VectorSubtract(dst, ph.origin, want);
				memset(&cmd, 0, sizeof(cmd));
				cmd.msec = STEP_MSEC;
				cmd.angles[YAW] = ANGLE2SHORT(atan2f(want[1], want[0])
				                              * 180.0f / M_PI);
				cmd.forwardmove = 400;
				SG_OracleRun(&ph, &cmd, 1);
				elapsed += STEP_MSEC;
			}
		}

		if (health <= 0)
		{
			rj_nolift++;
			continue;                   /* out of range, or behind something */
		}
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

			if (SG_RocketJumpArrived(ph.origin, dst, ph.groundentity,
			        ph.waterlevel, ph.groundentity_entity, NULL))
			{
				float sp = sqrtf(ph.velocity[0] * ph.velocity[0] +
				                 ph.velocity[1] * ph.velocity[1]);

				rj_arrived++;
				*cost_ms = (short)elapsed;
				*exit_speed = (byte)(sp / 4.0f > 255.0f ? 255 : sp / 4.0f);
				*heading_out = heading;
				/* Exact signed usercmd pitch/yaw plus the worst-case health
				 * price.  These three floats are integral and codec-checked. */
				anchor_out[0] = (float)pitch_control;
				anchor_out[1] = (float)yaw_control;
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
#if 1
	int i, j;
	float ceiling = SG_OracleRocketJumpCeiling();

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
			sg_host.dprint("rune: rocketjump candidate from=%d to=%d "
			    "source=(%.0f %.0f %.0f) destination=(%.0f %.0f %.0f) "
			    "control=(%.0f %.0f) health=%.0f cost=%d\n",
			    i, j, gen_seeds[i].origin[0], gen_seeds[i].origin[1],
			    gen_seeds[i].origin[2], gen_seeds[j].origin[0],
			    gen_seeds[j].origin[1], gen_seeds[j].origin[2],
			    anchor[0], anchor[1], anchor[2], (int)cost);
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
#else
	/* No supported wire contract has a launch-state controller.  The old proof
	 * injected an exact rest state and simultaneous rocket+jump, while live
	 * execution could arm elsewhere in the seed cell and advance without
	 * confirming a shot.  Keep the implementation above available, but write
	 * no RL_ROCKETJUMP records until the serialized contract
	 * and executor share one. */
	(void)Reach_Within;
	(void)ProveRocketJump;
	sg_host.dprint("rune: rocket jumps disabled (unserialized launch state)\n");
#endif
}

static void Prove_HookFrontier(void)
{
	door_topology_t topology = { NULL, NULL };
	sg_rune_proof_hook_seed_t *seeds = NULL;
	sg_rune_proof_hook_candidate_t *candidates = NULL;
	uint16_t *component_trials = NULL;
	uint16_t *source_trials = NULL;
	size_t *source_cursor = NULL;
	size_t *component_source_cursor = NULL;
	sg_rune_proof_hook_frontier_t frontier;
	size_t selected = 0, trial;
	int component_count = 0, proofs = 0;
	int active_components = 0, max_component_trials = 0;
	int active_sources = 0, max_source_trials = 0;
	unsigned int candidate_ranks[15] = { 0 };
	unsigned int proof_ranks[15] = { 0 };
	int i;

	if (!Door_TopologyBuild(&topology))
	{
		sg_host.dprint("rune: hook frontier unavailable reason=topology\n");
		return;
	}
	for (i = 0; i < gen_num_seeds; i++)
		if (topology.component[i] >= component_count)
			component_count = topology.component[i] + 1;
	if (component_count <= 0)
		goto done;
	seeds = sg_host.level_alloc(sizeof(*seeds) * (size_t)gen_num_seeds);
	candidates = sg_host.level_alloc(sizeof(*candidates) *
	    SG_RUNE_PROOF_HOOK_FRONTIER_MAX);
	component_trials = sg_host.level_alloc(sizeof(*component_trials) *
	    (size_t)component_count);
	source_trials = sg_host.level_alloc(sizeof(*source_trials) *
	    (size_t)gen_num_seeds);
	source_cursor = sg_host.level_alloc(sizeof(*source_cursor) *
	    (size_t)gen_num_seeds);
	component_source_cursor = sg_host.level_alloc(
	    sizeof(*component_source_cursor) * (size_t)component_count);
	if (!seeds || !candidates || !component_trials || !source_trials ||
	    !source_cursor || !component_source_cursor)
	{
		sg_host.dprint("rune: hook frontier unavailable reason=allocation\n");
		goto done;
	}
	for (i = 0; i < gen_num_seeds; i++)
	{
		seeds[i].origin_q8[0] = (int32_t)lrintf(gen_seeds[i].origin[0] * 8.0f);
		seeds[i].origin_q8[1] = (int32_t)lrintf(gen_seeds[i].origin[1] * 8.0f);
		seeds[i].origin_q8[2] = (int32_t)lrintf(gen_seeds[i].origin[2] * 8.0f);
		seeds[i].component = topology.component[i];
		seeds[i].objective_mask = topology.objective_mask[i];
		seeds[i].water = (gen_seeds[i].flags & RSF_WATER) != 0;
		seeds[i].stable = gen_source_stable[i] != 0;
		seeds[i].waterlevel = gen_source_waterlevel[i];
	}
	memset(&frontier, 0, sizeof(frontier));
	frontier.seeds = seeds;
	frontier.seed_count = (size_t)gen_num_seeds;
	frontier.component_count = (size_t)component_count;
	frontier.global_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	frontier.component_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	frontier.source_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	frontier.component_trials = component_trials;
	frontier.source_trials = source_trials;
	frontier.source_cursor = source_cursor;
	frontier.component_source_cursor = component_source_cursor;
	frontier.output = candidates;
	frontier.output_capacity = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	selected = SG_RuneProofSelectHookFrontier(&frontier);
	for (i = 0; i < component_count; i++)
	{
		if (component_trials[i] > 0)
			active_components++;
		if ((int)component_trials[i] > max_component_trials)
			max_component_trials = component_trials[i];
	}
	for (i = 0; i < gen_num_seeds; i++)
	{
		if (source_trials[i] > 0)
			active_sources++;
		if ((int)source_trials[i] > max_source_trials)
			max_source_trials = source_trials[i];
	}
	for (trial = 0; trial < selected; trial++)
	{
		vec3_t anchor;
		short cost;
		byte espeed;
		rune_link_t *link;

		if (candidates[trial].rank < 15)
			candidate_ranks[candidates[trial].rank]++;
		if (!ProveHook(candidates[trial].from, candidates[trial].to,
		        anchor, &cost, &espeed))
			continue;
		if (!Link_Add(candidates[trial].from, candidates[trial].to,
		        RL_HOOK, cost, espeed))
			continue;
		link = &gen_links[gen_num_links - 1];
		VectorCopy(anchor, link->anchor);
		Link_Env_Hook(link, anchor);
		if (candidates[trial].rank < 15)
			proof_ranks[candidates[trial].rank]++;
		proofs++;
	}
	sg_host.dprint("rune: hook frontier components=%d candidates=%u "
	               "prover_calls=%u links=%d global_limit=%u "
	               "schedule=rank-component-source-round-robin\n",
	               component_count, (unsigned int)selected,
	               (unsigned int)selected, proofs,
	               (unsigned int)SG_RUNE_PROOF_HOOK_FRONTIER_MAX);
	sg_host.dprint("rune: hook frontier distribution active_components=%d "
	               "max_component_trials=%d active_sources=%d "
	               "max_source_trials=%d "
	               "candidate_ranks=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u "
	               "proof_ranks=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
	               active_components, max_component_trials, active_sources,
	               max_source_trials,
	               candidate_ranks[0], candidate_ranks[1], candidate_ranks[2],
	               candidate_ranks[3], candidate_ranks[4], candidate_ranks[5],
	               candidate_ranks[6], candidate_ranks[7], candidate_ranks[8],
	               candidate_ranks[9], candidate_ranks[10], candidate_ranks[11],
	               candidate_ranks[12], candidate_ranks[13], candidate_ranks[14],
	               proof_ranks[0], proof_ranks[1], proof_ranks[2], proof_ranks[3],
	               proof_ranks[4], proof_ranks[5], proof_ranks[6], proof_ranks[7],
	               proof_ranks[8], proof_ranks[9], proof_ranks[10], proof_ranks[11],
	               proof_ranks[12], proof_ranks[13], proof_ranks[14]);

done:
	if (seeds) sg_host.level_free(seeds);
	if (candidates) sg_host.level_free(candidates);
	if (component_trials) sg_host.level_free(component_trials);
	if (source_trials) sg_host.level_free(source_trials);
	if (source_cursor) sg_host.level_free(source_cursor);
	if (component_source_cursor) sg_host.level_free(component_source_cursor);
	Door_TopologyFree(&topology);
}

static void Prove_BaseLinks(door_topology_t *topology)
{
	int i, j;

	Drop_PrefixCacheClear();
	drop_prefix_cache_enabled = true;
	SG_OracleDoorBoundsCacheBegin();
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
			Rune_TelemetryAdd(&gen_telemetry.pair_scans, 1U);
			water_pair = ((gen_seeds[i].flags | gen_seeds[j].flags) &
			              RSF_WATER) != 0;
			VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, d);
			if (d[0] * d[0] + d[1] * d[1] > HOOK_PAIR_REACH * HOOK_PAIR_REACH)
				continue;
			Rune_TelemetryAdd(&gen_telemetry.qualified, 1U);
			/* beyond running reach only the hook applies */
			if (!(gen_seeds[i].flags & RSF_WATER) &&
			    d[0] * d[0] + d[1] * d[1] > LINK_REACH * LINK_REACH &&
			    d[2] <= 128.0f && d[2] >= -256.0f)
			{
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
				/* Keep only from-rest jumps until entry state is fully serialized. */
			}
		}
		if ((i & 255) == 0)
			sg_host.dprint("rune: proving %d/%d seeds, %d links\n",
			           i, gen_num_seeds, gen_num_links);
	}
	drop_prefix_cache_enabled = false;
	Drop_PrefixCacheClear();

	/* Ordinary pairs touching water are reserved for Prove_Swims.  The
	 * declared-mechanism passes consume the completed base graph so their
	 * specialized links can be stamped with declared provenance. */
	Prove_Swims();          /* swim links: water to water, water to shore */
	SG_OracleDoorBoundsCacheEnd();
	{
		/* Link_Plats may append both a declared ascent and a proved drop. Stamp
		 * only the declared action before the hook topology snapshot. */
		int declared_mark = gen_num_links;

		Link_Plats();           /* declared lift: bottom seed -> top seed */
		Link_Declare_Tail(declared_mark);
	}
	Prove_HookFrontier();
	{
		int declared_mark = gen_num_links;

		/* Teleporter staging may itself require one of the bounded, proved hook
		 * frontiers. Doors consume the completed hook and teleporter topology. */
		Link_Teleporters();     /* misc_teleporter pad seed -> destination seed */
		Link_Doors(topology);   /* repeatable trigger: wait, open, full egress */
		Link_Pushes();
		Link_Trains();
		Link_TrainRides();
		Link_TrainShootButtons(topology);
		Link_Declare_Tail(declared_mark);
	}

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
static int Graph_ObjectiveRoot(const vec3_t objective, const byte *has_out,
	graph_objective_diag_t *diag)
{
	const float max_horiz2 = 128.0f * 128.0f;
	int i, best = -1;
	float bestd = 1e30f;

	if (diag)
	{
		memset(diag, 0, sizeof(*diag));
		diag->nearest = -1;
		VectorCopy(objective, diag->objective);
	}

	for (i = 0; i < gen_num_seeds; i++)
	{
		vec3_t d, from, to;
		float dd;
		trace_t tr;

		VectorSubtract(gen_seeds[i].origin, objective, d);
		if (d[2] > 96.0f || d[2] < -96.0f)
		{
			if (diag) Rune_TelemetryAdd(&diag->vertical_rejects, 1U);
			continue;
		}
		if (d[0] * d[0] + d[1] * d[1] > max_horiz2)
		{
			if (diag) Rune_TelemetryAdd(&diag->spatial_rejects, 1U);
			continue;
		}
		dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] * 0.25f;
		if (dd >= bestd)
			continue;
		VectorCopy(objective, from);
		VectorCopy(gen_seeds[i].origin, to);
		from[2] += 16.0f;
		to[2] += 16.0f;
		tr = sg_host.trace(from, NULL, NULL, to, NULL, MASK_DEADSOLID);
		if (tr.startsolid || tr.fraction < 1.0f)
		{
			if (diag) Rune_TelemetryAdd(&diag->los_rejects, 1U);
			continue;
		}
		if (SG_OracleRotatorSweepBlocks(from, NULL, NULL, to,
		                               MASK_DEADSOLID))
		{
			if (diag) Rune_TelemetryAdd(&diag->rotator_rejects, 1U);
			continue;
		}
		bestd = dd;
		best = i;
	}
	if (diag && best >= 0)
	{
		diag->nearest = best;
		VectorCopy(gen_seeds[best].origin, diag->origin);
		VectorSubtract(gen_seeds[best].origin, objective, diag->delta);
		diag->score = bestd;
		diag->distance = sqrtf(diag->delta[0] * diag->delta[0] +
		    diag->delta[1] * diag->delta[1] +
		    diag->delta[2] * diag->delta[2]);
		diag->has_out = has_out && has_out[best];
		if (!diag->has_out)
			Rune_TelemetryAdd(&diag->no_out_rejects, 1U);
	}
	return (best >= 0 && has_out[best]) ? best : -1;
}

static void Graph_LogObjectiveRoot(const char *team,
	const graph_objective_diag_t *diag, int root)
{
	if (!team || !diag)
		return;
	sg_host.dprint("rune: objective-root team=%s root=%d settled=(%.3f %.3f %.3f) "
	               "nearest=%d origin=(%.3f %.3f %.3f) delta=(%.3f %.3f %.3f) "
	               "score=%.3f distance=%.3f has_out=%d rejects=spatial:%u vertical:%u "
	               "los:%u rotator:%u no_out:%u\n",
	               team, root,
	               diag->objective[0], diag->objective[1], diag->objective[2],
	               diag->nearest, diag->origin[0], diag->origin[1],
	               diag->origin[2], diag->delta[0], diag->delta[1],
	               diag->delta[2], diag->score, diag->distance,
	               diag->has_out ? 1 : 0,
	               (unsigned int)diag->spatial_rejects,
	               (unsigned int)diag->vertical_rejects,
	               (unsigned int)diag->los_rejects,
	               (unsigned int)diag->rotator_rejects,
	               (unsigned int)diag->no_out_rejects);
}

static int Graph_ReachCount(const byte *reached)
{
	int i, count = 0;

	if (!reached)
		return 0;
	for (i = 0; i < gen_num_seeds; i++)
		if (reached[i])
			count++;
	return count;
}

typedef struct
{
	int red_only;
	int blue_only;
	int shared;
	int neither;
} graph_partition_counts_t;

#define OBJECTIVE_DIAG_PAIR_BUDGET 16777216ULL

static unsigned int Graph_ObjectivePartitionMask(int seed,
	const byte *red_reach, const byte *blue_reach)
{
	return (red_reach[seed] ? 1U : 0U) |
	       (blue_reach[seed] ? 2U : 0U);
}

static void Graph_LogClosestPartitionPair(const char *pair,
	unsigned int from_mask, unsigned int to_mask, int from_count, int to_count,
	const byte *red_reach, const byte *blue_reach)
{
	unsigned long long possible =
	    (unsigned long long)from_count * (unsigned long long)to_count;
	unsigned long long comparisons = 0ULL;
	int from_seed = -1, to_seed = -1;
	int i, j;
	float best = 1.0e30f;

	/* Failure evidence must not turn a bounded graph pass into an unbounded
	 * quadratic stall. These products are small on the failing corpus maps;
	 * larger partitions retain their exact counts and explicitly report that
	 * the nearest-pair search was skipped. */
	if (possible > OBJECTIVE_DIAG_PAIR_BUDGET)
	{
		sg_host.dprint("rune: objective-gap pair=%s from=-1 to=-1 "
		               "distance=-1.000 exact=0 pairs=%llu budget=%llu\n",
		               pair, possible,
		               (unsigned long long)OBJECTIVE_DIAG_PAIR_BUDGET);
		return;
	}
	for (i = 0; i < gen_num_seeds; i++)
	{
		if (Graph_ObjectivePartitionMask(i, red_reach, blue_reach) !=
		    from_mask)
			continue;
		for (j = 0; j < gen_num_seeds; j++)
		{
			vec3_t delta;
			float distance2;

			if (Graph_ObjectivePartitionMask(j, red_reach, blue_reach) !=
			    to_mask)
				continue;
			comparisons++;
			VectorSubtract(gen_seeds[j].origin, gen_seeds[i].origin, delta);
			distance2 = DotProduct(delta, delta);
			if (distance2 < best)
			{
				best = distance2;
				from_seed = i;
				to_seed = j;
			}
		}
	}
	if (from_seed >= 0 && to_seed >= 0)
	{
		vec3_t delta;

		VectorSubtract(gen_seeds[to_seed].origin,
		    gen_seeds[from_seed].origin, delta);
		sg_host.dprint("rune: objective-gap pair=%s from=%d "
		               "from_origin=(%.3f %.3f %.3f) to=%d "
		               "to_origin=(%.3f %.3f %.3f) delta=(%.3f %.3f %.3f) "
		               "distance=%.3f exact=1 pairs=%llu budget=%llu\n",
		               pair, from_seed, gen_seeds[from_seed].origin[0],
		               gen_seeds[from_seed].origin[1],
		               gen_seeds[from_seed].origin[2], to_seed,
		               gen_seeds[to_seed].origin[0],
		               gen_seeds[to_seed].origin[1],
		               gen_seeds[to_seed].origin[2], delta[0], delta[1],
		               delta[2], sqrtf(best), comparisons,
		               (unsigned long long)OBJECTIVE_DIAG_PAIR_BUDGET);
	}
}

static void Graph_LogObjectivePartitions(const byte *red_reach,
	const byte *blue_reach)
{
	graph_partition_counts_t counts = { 0, 0, 0, 0 };
	int i;

	for (i = 0; i < gen_num_seeds; i++)
	{
		switch (Graph_ObjectivePartitionMask(i, red_reach, blue_reach))
		{
		case 1U: counts.red_only++; break;
		case 2U: counts.blue_only++; break;
		case 3U: counts.shared++; break;
		default: counts.neither++; break;
		}
	}
	sg_host.dprint("rune: objective-partitions red_only=%d blue_only=%d "
	               "shared=%d neither=%d\n", counts.red_only,
	               counts.blue_only, counts.shared, counts.neither);
	Graph_LogClosestPartitionPair("red-blue", 1U, 2U,
	    counts.red_only, counts.blue_only, red_reach, blue_reach);
	Graph_LogClosestPartitionPair("red-shared", 1U, 3U,
	    counts.red_only, counts.shared, red_reach, blue_reach);
	Graph_LogClosestPartitionPair("blue-shared", 2U, 3U,
	    counts.blue_only, counts.shared, red_reach, blue_reach);
}

static void Graph_LogBoundaryLinks(const char *phase, const byte *red_reach,
	const byte *blue_reach)
{
	int i, reported = 0;

	if (!red_reach || !blue_reach)
		return;
	for (i = 0; i < gen_num_links && reported < 16; i++)
	{
		const rune_link_t *link = &gen_links[i];
		unsigned int from_mask = Graph_ObjectivePartitionMask(link->from,
		    red_reach, blue_reach);
		unsigned int to_mask = Graph_ObjectivePartitionMask(link->to,
		    red_reach, blue_reach);

		if (from_mask == to_mask)
			continue;
		sg_host.dprint("rune: objective-boundary phase=%s ordinal=%d "
		               "link=%d from=%d to=%d "
		               "from_reach=%u to_reach=%u action=%u provenance=%u plan=%u\n",
		               phase, reported, i, link->from, link->to,
		               from_mask, to_mask,
		               (unsigned int)link->action, (unsigned int)link->provenance,
		               (unsigned int)link->mechanism_plan);
		reported++;
	}
	sg_host.dprint("rune: objective-boundary phase=%s reported=%d limit=16\n",
	               phase, reported);
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

#ifndef SG_RUNE_OBJECTIVE_DROP_PROVER
#define SG_RUNE_OBJECTIVE_DROP_PROVER ProveDrop
#endif
#ifndef SG_RUNE_OBJECTIVE_HOOK_PROVER
#define SG_RUNE_OBJECTIVE_HOOK_PROVER ProveHook
#endif
#ifndef SG_RUNE_OBJECTIVE_ROCKET_PROVER
#define SG_RUNE_OBJECTIVE_ROCKET_PROVER ProveRocketJump
#endif
static qboolean Graph_ProveObjectiveReverse(const byte *red_reach,
	const byte *blue_reach, int *link_out, byte *action_out)
{
	int old_links = gen_num_links;
	int action_pass, i;

	if (link_out)
		*link_out = -1;
	/* A proved ascent into a one-sided objective component may have a physical
	 * walkoff back into the shared component that the ordinary pair admission
	 * never offered to the drop controller. More generally, a generated
	 * shared-to-one-sided boundary identifies the exact inverse pair whose
	 * absence prevents objective closure. Re-run the native controllers on
	 * that pair instead of changing their global candidate budgets. Only a
	 * graph that already failed objective closure reaches these proofs. */
	for (action_pass = 0; action_pass < 3; action_pass++)
	{
		for (i = 0; i < old_links; i++)
		{
			rune_link_t boundary = gen_links[i];
			unsigned int from_mask = Graph_ObjectivePartitionMask(boundary.from,
			    red_reach, blue_reach);
			unsigned int to_mask = Graph_ObjectivePartitionMask(boundary.to,
			    red_reach, blue_reach);
			vec3_t anchor;
			short cost;
			byte exit_speed, heading = 0;
			qboolean proved;

			if (from_mask != 3U || (to_mask != 1U && to_mask != 2U) ||
			    Link_Exists(boundary.to, boundary.from))
				continue;
			if (action_pass == 0)
			{
				if (boundary.action != RL_HOOK ||
				    gen_seeds[boundary.to].origin[2] <=
				        gen_seeds[boundary.from].origin[2] + 160.0f)
					continue;
				proved = SG_RUNE_OBJECTIVE_DROP_PROVER(boundary.to,
				    boundary.from, anchor, &cost, &exit_speed);
			}
			else if (action_pass == 1)
				proved = SG_RUNE_OBJECTIVE_HOOK_PROVER(boundary.to,
				    boundary.from, anchor, &cost, &exit_speed);
			else
				proved = SG_RUNE_OBJECTIVE_ROCKET_PROVER(boundary.to,
				    boundary.from, anchor, &cost, &exit_speed, &heading);
			if (!proved)
				continue;
			if (!Link_Add(boundary.to, boundary.from,
			    action_pass == 0 ? RL_DROP :
			    action_pass == 1 ? RL_HOOK : RL_ROCKETJUMP,
			    cost, exit_speed))
				return false;
			VectorCopy(anchor, gen_links[gen_num_links - 1].anchor);
			if (action_pass == 0)
				Link_Env_Drop(&gen_links[gen_num_links - 1], dd_last_heading);
			else if (action_pass == 1)
				Link_Env_Hook(&gen_links[gen_num_links - 1], anchor);
			else
			{
				gen_links[gen_num_links - 1].heading = heading;
				gen_links[gen_num_links - 1].heading_slack = SG_RJ_SLACK;
				gen_links[gen_num_links - 1].min_speed = 0;
			}
			if (link_out)
				*link_out = gen_num_links - 1;
			if (action_out)
				*action_out = gen_links[gen_num_links - 1].action;
			return true;
		}
	}
	return false;
}

static qboolean Graph_ObjectiveReachMasks(int red_root, int blue_root,
	byte *red_reach, byte *blue_reach)
{
	int *first_in = NULL, *next_in = NULL, *queue = NULL;
	byte *allowed = NULL;
	qboolean ok = false;

	if (!red_reach || !blue_reach || red_root < 0 || blue_root < 0 ||
	    red_root >= gen_num_seeds || blue_root >= gen_num_seeds)
		return false;
	first_in = sg_host.level_alloc(sizeof(*first_in) * (size_t)gen_num_seeds);
	next_in = sg_host.level_alloc(sizeof(*next_in) *
	    (size_t)(gen_num_links ? gen_num_links : 1));
	queue = sg_host.level_alloc(sizeof(*queue) * (size_t)gen_num_seeds);
	allowed = sg_host.level_alloc((size_t)gen_num_seeds);
	if (!first_in || !next_in || !queue || !allowed)
		goto done;
	memset(allowed, 1, (size_t)gen_num_seeds);
	for (int i = 0; i < gen_num_seeds; i++)
		first_in[i] = -1;
	for (int i = 0; i < gen_num_links; i++)
	{
		next_in[i] = first_in[gen_links[i].to];
		first_in[gen_links[i].to] = i;
	}
	Graph_ReverseReach(red_root, allowed, first_in, next_in, queue,
	    red_reach);
	Graph_ReverseReach(blue_root, allowed, first_in, next_in, queue,
	    blue_reach);
	ok = true;

done:
	if (first_in) sg_host.level_free(first_in);
	if (next_in) sg_host.level_free(next_in);
	if (queue) sg_host.level_free(queue);
	if (allowed) sg_host.level_free(allowed);
	return ok;
}

#define OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX 16

typedef struct {
	int seed[2];
	float distance2;
} objective_rocket_pair_t;

static qboolean Graph_ProveObjectiveRocketClosure(int red_root, int blue_root,
	const byte *red_reach, const byte *blue_reach, int *calls_out)
{
	objective_rocket_pair_t candidates[OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX];
	byte *candidate_red = NULL, *candidate_blue = NULL;
	int calls = 0;
	qboolean closed = false;

	if (calls_out)
		*calls_out = 0;
	candidate_red = sg_host.level_alloc((size_t)gen_num_seeds);
	candidate_blue = sg_host.level_alloc((size_t)gen_num_seeds);
	if (!candidate_red || !candidate_blue)
		goto done;
	for (int rank = 0; rank < OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX; rank++)
	{
		candidates[rank].seed[0] = -1;
		candidates[rank].seed[1] = -1;
		candidates[rank].distance2 = 1.0e30f;
	}
	for (int red = 0; red < gen_num_seeds; red++)
	{
		if (Graph_ObjectivePartitionMask(red, red_reach, blue_reach) != 1U)
			continue;
		for (int blue = 0; blue < gen_num_seeds; blue++)
		{
			vec3_t delta;
			float distance2;
			int rank;

			if (Graph_ObjectivePartitionMask(blue, red_reach,
			        blue_reach) != 2U)
				continue;
			Rune_TelemetryAdd(&gen_telemetry.pair_scans, 1U);
			VectorSubtract(gen_seeds[red].origin,
			    gen_seeds[blue].origin, delta);
			distance2 = DotProduct(delta, delta);
			for (rank = 0; rank < OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX; rank++)
				if (distance2 < candidates[rank].distance2)
					break;
			if (rank == OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX)
				continue;
			for (int move = OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX - 1;
			     move > rank; move--)
				candidates[move] = candidates[move - 1];
			candidates[rank].seed[0] = red;
			candidates[rank].seed[1] = blue;
			candidates[rank].distance2 = distance2;
		}
	}
	for (int rank = 0; rank < OBJECTIVE_CLOSURE_ROCKET_PAIR_MAX &&
	     candidates[rank].seed[0] >= 0; rank++)
	{
		vec3_t anchor[2];
		short cost[2];
		byte speed[2], heading[2];
		qboolean proved = true;
		int link_mark = gen_num_links;

		for (int direction = 0; direction < 2; direction++)
		{
			calls++;
			if (!SG_RUNE_OBJECTIVE_ROCKET_PROVER(
			        candidates[rank].seed[direction],
			        candidates[rank].seed[1 - direction], anchor[direction],
			        &cost[direction], &speed[direction], &heading[direction]))
				proved = false;
		}
		if (!proved)
			continue;
		for (int direction = 0; direction < 2; direction++)
		{
			rune_link_t *link;

			if (!Link_Add(candidates[rank].seed[direction],
			        candidates[rank].seed[1 - direction], RL_ROCKETJUMP,
			        cost[direction], speed[direction]))
			{
				gen_num_links = link_mark;
				goto done;
			}
			link = &gen_links[gen_num_links - 1];
			VectorCopy(anchor[direction], link->anchor);
			link->heading = heading[direction];
			link->heading_slack = SG_RJ_SLACK;
			link->min_speed = 0;
		}
		if (Graph_ObjectiveReachMasks(red_root, blue_root,
		        candidate_red, candidate_blue) &&
		    candidate_blue[red_root] && candidate_red[blue_root])
		{
			closed = true;
			goto done;
		}
		gen_num_links = link_mark;
	}

done:
	if (candidate_red) sg_host.level_free(candidate_red);
	if (candidate_blue) sg_host.level_free(candidate_blue);
	if (calls_out)
		*calls_out = calls;
	return closed;
}

static qboolean Graph_ObjectiveForwardMasks(int red_root, int blue_root,
	byte *red_forward, byte *blue_forward)
{
	int *first_out = NULL, *next_out = NULL, *queue = NULL;
	qboolean ok = false;
	int roots[2];
	byte *outputs[2];

	if (!red_forward || !blue_forward || red_root < 0 || blue_root < 0 ||
	    red_root >= gen_num_seeds || blue_root >= gen_num_seeds)
		return false;
	first_out = sg_host.level_alloc(sizeof(*first_out) *
	    (size_t)gen_num_seeds);
	next_out = sg_host.level_alloc(sizeof(*next_out) *
	    (size_t)(gen_num_links ? gen_num_links : 1));
	queue = sg_host.level_alloc(sizeof(*queue) * (size_t)gen_num_seeds);
	if (!first_out || !next_out || !queue)
		goto done;
	for (int i = 0; i < gen_num_seeds; i++)
		first_out[i] = -1;
	for (int i = 0; i < gen_num_links; i++)
	{
		next_out[i] = first_out[gen_links[i].from];
		first_out[gen_links[i].from] = i;
	}
	roots[0] = red_root;
	roots[1] = blue_root;
	outputs[0] = red_forward;
	outputs[1] = blue_forward;
	for (int side = 0; side < 2; side++)
	{
		int head = 0, tail = 0;

		memset(outputs[side], 0, (size_t)gen_num_seeds);
		outputs[side][roots[side]] = 1;
		queue[tail++] = roots[side];
		while (head < tail)
		{
			int at = queue[head++];

			for (int edge = first_out[at]; edge >= 0;
			     edge = next_out[edge])
			{
				int to = gen_links[edge].to;

				if (!outputs[side][to])
				{
					outputs[side][to] = 1;
					queue[tail++] = to;
				}
			}
		}
	}
	ok = true;

done:
	if (first_out) sg_host.level_free(first_out);
	if (next_out) sg_host.level_free(next_out);
	if (queue) sg_host.level_free(queue);
	return ok;
}

static qboolean ShootDoor_Shot(const vec3_t source, edict_t *master,
	vec3_t contact_out, int *flight_ms)
{
	edict_t *member;
	vec3_t aim, angles, forward, muzzle, end;
	trace_t trace;

	if (contact_out) VectorClear(contact_out);
	if (flight_ms) *flight_ms = 0;
	if (!source || !master || !contact_out || !flight_ms)
		return false;
	for (member = master; member; member = member->teamchain)
	{
		if (!member->inuse || !member->classname ||
		    strcmp(member->classname, "func_door") || member->health <= 0 ||
		    member->health != member->max_health ||
		    member->takedamage != DAMAGE_YES || !member->die)
			return false;
		for (int axis = 0; axis < 3; axis++)
			aim[axis] = 0.5f * (member->absmin[axis] + member->absmax[axis]);
		if (!SG_BlasterAimAngles(source, 22.0f, RIGHT_HANDED, aim, angles,
		        muzzle))
			continue;
		AngleVectors(angles, forward, NULL, NULL);
		VectorMA(muzzle, 8192.0f, forward, end);
		trace = sg_host.trace(muzzle, NULL, NULL, end, NULL, MASK_SHOT);
		if (trace.ent != member || trace.startsolid || trace.allsolid ||
		    trace.fraction <= 0.0f || trace.fraction >= 1.0f)
			continue;
		VectorCopy(trace.endpos, contact_out);
		VectorSubtract(contact_out, muzzle, end);
		*flight_ms = (int)ceilf(VectorLength(end));
		return *flight_ms > 0 && *flight_ms <= RUNE_MAX_COST_MS;
	}
	return false;
}

static edict_t *ShootDoor_StandingSupport(const vec3_t origin)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t down;
	trace_t trace;

	VectorCopy(origin, down);
	down[2] -= 4.0f;
	trace = sg_host.trace((vec_t *)origin, mins, maxs, down, NULL,
		MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
	    trace.plane.normal[2] < 0.7f)
		return NULL;
	return trace.ent;
}

static uint32_t ShootDoor_OpeningBound(edict_t *master)
{
	edict_t *member;
	float maximum_ms = 0.0f;

	for (member = master; member; member = member->teamchain)
	{
		vec3_t delta;
		float duration_ms;

		if (member->moveinfo.speed <= 0.0f)
			return 0U;
		VectorSubtract(member->moveinfo.end_origin,
			member->moveinfo.start_origin, delta);
		duration_ms = VectorLength(delta) * 1000.0f /
			member->moveinfo.speed + 100.0f;
		if (!isfinite(duration_ms) || duration_ms <= 0.0f)
			return 0U;
		if (duration_ms > maximum_ms)
			maximum_ms = duration_ms;
	}
	if (maximum_ms <= 0.0f || maximum_ms > RUNE_MAX_COST_MS)
		return 0U;
	return (uint32_t)ceilf(maximum_ms / 100.0f) * 100U;
}

static qboolean ShootDoor_PoseOpen(edict_t *master, door_pose_t *saved,
	int capacity, int *count_out, vec3_t sweep_min, vec3_t sweep_max)
{
	edict_t *member;
	int count = 0;

	if (count_out) *count_out = 0;
	if (!master || !saved || capacity <= 0 || !count_out)
		return false;
	VectorSet(sweep_min, HUGE_VALF, HUGE_VALF, HUGE_VALF);
	VectorSet(sweep_max, -HUGE_VALF, -HUGE_VALF, -HUGE_VALF);
	for (member = master; member; member = member->teamchain)
	{
		door_pose_t *pose;

		if (count >= capacity)
			goto fail;
		pose = &saved[count++];
		memset(pose, 0, sizeof(*pose));
		pose->ent = member;
		VectorCopy(member->s.origin, pose->origin);
		VectorCopy(member->s.old_origin, pose->old_origin);
		VectorCopy(member->s.angles, pose->angles);
		VectorCopy(member->velocity, pose->velocity);
		VectorCopy(member->avelocity, pose->avelocity);
		pose->state = member->moveinfo.state;
		pose->solid = member->solid;
		pose->linkcount = member->linkcount;
		for (int axis = 0; axis < 3; axis++)
		{
			float start_min = member->moveinfo.start_origin[axis] +
				member->mins[axis];
			float start_max = member->moveinfo.start_origin[axis] +
				member->maxs[axis];
			float end_min = member->moveinfo.end_origin[axis] +
				member->mins[axis];
			float end_max = member->moveinfo.end_origin[axis] +
				member->maxs[axis];

			if (start_min < sweep_min[axis]) sweep_min[axis] = start_min;
			if (end_min < sweep_min[axis]) sweep_min[axis] = end_min;
			if (start_max > sweep_max[axis]) sweep_max[axis] = start_max;
			if (end_max > sweep_max[axis]) sweep_max[axis] = end_max;
		}
		VectorCopy(member->moveinfo.end_origin, member->s.origin);
		VectorCopy(member->moveinfo.end_origin, member->s.old_origin);
		VectorClear(member->velocity);
		VectorClear(member->avelocity);
		member->moveinfo.state = SG_PLAT_STATE_TOP;
		member->solid = SOLID_BSP;
		sg_host.linkentity(member);
	}
	*count_out = count;
	return count > 0;

fail:
	DoorPose_Restore(saved, count);
	return false;
}

static int ShootDoor_PassageAxis(edict_t *master,
	const vec3_t sweep_min, const vec3_t sweep_max)
{
	int motion_axis = -1;
	int passage_axis = -1;
	float passage_extent = HUGE_VALF;

	for (int axis = 0; axis < 3; axis++)
	{
		if (fabsf(master->moveinfo.end_origin[axis] -
		        master->moveinfo.start_origin[axis]) <= 0.125f)
			continue;
		if (motion_axis >= 0)
			return -1;
		motion_axis = axis;
	}
	if (motion_axis < 0)
		return -1;
	for (int axis = 0; axis < 2; axis++)
		if (axis != motion_axis &&
		    sweep_max[axis] - sweep_min[axis] < passage_extent)
		{
			passage_axis = axis;
			passage_extent = sweep_max[axis] - sweep_min[axis];
		}
	return passage_axis;
}

static qboolean ShootDoor_AddLink(int from, int to, edict_t *master,
	uint32_t opening_bound, int cross_ms, int flight_ms)
{
	int before = gen_num_links;
	int cost = cross_ms + flight_ms + (int)opening_bound;
	rune_link_t *link;

	if (cost <= 0 || cost > RUNE_MAX_COST_MS || cross_ms <= 0 ||
	    cross_ms > cost || (cross_ms % 100) != 0)
		return false;
	if (!Link_Add(from, to, RL_TRAIN, (short)cost, 0))
		return false;
	if (gen_num_links == before)
		return true;
	link = &gen_links[gen_num_links - 1];
	VectorCopy(gen_seeds[from].origin, link->anchor);
	VectorCopy(gen_seeds[to].origin, link->mechanism_anchor);
	link->provenance = RL_DECLARED;
	link->heading_slack = RUNE_DECLARED_CONTROL_MARKER;
	link->sweep_clear_ms = (uint16_t)cross_ms;
	link->mode = RLCM_PREOPEN;
	if (!Mechanism_BindShootDoor(link, master, opening_bound))
	{
		gen_num_links = before;
		return false;
	}
	gen_train_links++;
	gen_declared_links++;
	return true;
}

static qboolean Link_ShootDoors(const door_topology_t *topology,
	int red_root, int blue_root)
{
	int link_mark = gen_num_links;
	int train_mark = gen_train_links;
	int declared_mark = gen_declared_links;
	uint32_t binding_mark = gen_num_mechanism_bindings;
	byte *red_reach = NULL;
	byte *blue_reach = NULL;
	qboolean closed = false;

	if (!topology || !topology->component)
		return false;
	for (uint32_t node_index = 0U;
	     node_index < gen_mechanism_catalog.num_nodes; node_index++)
	{
		const rune_mechanism_node_t *node =
			&gen_mechanism_catalog.nodes[node_index];
		edict_t *master;
		door_pose_t saved[16];
		vec3_t sweep_min, sweep_max;
		int member_count = 0;
		int passage_axis;
		int side_component[2] = { -1, -1 };
		float best_sum = HUGE_VALF;
		uint32_t opening_bound;

		if (node->kind != SG_MECH_NODE_DOOR_MASTER ||
		    (node->flags & SG_MECH_NODEF_SHOOTABLE) == 0U ||
		    node->key == 0U || node->key >= (uint32_t)globals.num_edicts)
			continue;
		if (
		    !(master = &g_edicts[node->key]) ||
		    !SG_MechCatalogEntityExecutionMatches(node->key, node,
		        SG_MECHANISM_CONTROLLER_TRAIN_SHOOT) ||
		    !(opening_bound = ShootDoor_OpeningBound(master)) ||
		    !ShootDoor_PoseOpen(master, saved, 16, &member_count,
		        sweep_min, sweep_max))
			continue;
		passage_axis = ShootDoor_PassageAxis(master, sweep_min, sweep_max);
		if (passage_axis >= 0)
		{
			for (int left = 0; left < gen_num_seeds; left++)
			{
				sg_train_gate_side_t left_side;
				float left_gap;

				if (!gen_source_stable[left] ||
				    gen_source_waterlevel[left] != 0)
					continue;
				left_side = Train_SeedSweepAxisSide(gen_seeds[left].origin,
					sweep_min, sweep_max, (unsigned int)passage_axis);
				if (left_side == SG_TRAIN_GATE_SIDE_NONE)
					continue;
				left_gap = Train_SeedSweepGapSquared(gen_seeds[left].origin,
					sweep_min, sweep_max);
				for (int right = 0; right < gen_num_seeds; right++)
				{
					sg_train_gate_side_t right_side;
					float right_gap;

					if (!gen_source_stable[right] ||
					    gen_source_waterlevel[right] != 0 ||
					    topology->component[left] == topology->component[right])
						continue;
					right_side = Train_SeedSweepAxisSide(
						gen_seeds[right].origin, sweep_min, sweep_max,
						(unsigned int)passage_axis);
					if (right_side != SG_TrainGateOppositeSide(left_side))
						continue;
					right_gap = Train_SeedSweepGapSquared(
						gen_seeds[right].origin, sweep_min, sweep_max);
					if (left_gap + right_gap >= best_sum)
						continue;
					best_sum = left_gap + right_gap;
					side_component[0] = topology->component[left];
					side_component[1] = topology->component[right];
				}
			}
		}
		DoorPose_Restore(saved, member_count);
		if (passage_axis < 0 || side_component[0] < 0 ||
		    side_component[1] < 0)
			continue;
		for (int direction = 0; direction < 2; direction++)
		{
			qboolean proved = false;
			int calls = 0;
			int selected_from = -1;
			int selected_to = -1;
			int selected_cross_ms = 0;
			int selected_flight_ms = 0;

			for (int from = 0; from < gen_num_seeds && !proved && calls < 4096;
			     from++)
			{
				vec3_t contact;
				int flight_ms;
				sg_train_gate_side_t from_side;

				if (topology->component[from] != side_component[direction] ||
				    !gen_source_stable[from] || gen_source_waterlevel[from] != 0 ||
				    !ShootDoor_Shot(gen_seeds[from].origin, master, contact,
				        &flight_ms))
					continue;
				from_side = Train_SeedSweepAxisSide(gen_seeds[from].origin,
					sweep_min, sweep_max, (unsigned int)passage_axis);
				if (!ShootDoor_PoseOpen(master, saved, 16, &member_count,
				        sweep_min, sweep_max))
					continue;
				for (int to = 0; to < gen_num_seeds && !proved && calls < 4096;
				     to++)
				{
					vec3_t delta, landing;
					int destination = to;
					int cross_ms;
					edict_t *landing_support;
					edict_t *seed_support;

					if (topology->component[to] != side_component[1 - direction] ||
					    !gen_source_stable[to] || gen_source_waterlevel[to] != 0 ||
					    Link_Exists(from, to) ||
					    Train_SeedSweepAxisSide(gen_seeds[to].origin, sweep_min,
					        sweep_max, (unsigned int)passage_axis) !=
					        SG_TrainGateOppositeSide(from_side))
						continue;
					VectorSubtract(gen_seeds[to].origin, gen_seeds[from].origin,
						delta);
					if (delta[0] * delta[0] + delta[1] * delta[1] >
					        512.0f * 512.0f || fabsf(delta[2]) > 128.0f)
						continue;
					calls++;
					if (member_count > 1)
					{
						if (!SG_OracleShootDoorCross(gen_seeds[from].origin,
						        gen_seeds[to].origin, master, master, sweep_min,
						        sweep_max, (unsigned int)passage_axis, &cross_ms,
						        landing))
							continue;
						destination = Seed_NearbyIndex(landing);
						landing_support = ShootDoor_StandingSupport(landing);
						seed_support = destination >= 0 ?
							ShootDoor_StandingSupport(
							    gen_seeds[destination].origin) : NULL;
						if (destination < 0 || !gen_source_stable[destination] ||
						    gen_source_waterlevel[destination] != 0 ||
						    !landing_support || landing_support != seed_support ||
						    !SG_SupportedArrived(landing,
						        gen_seeds[destination].origin, true, 0, 0, NULL))
							continue;
					}
					else if (!SG_OracleTrainGateCross(gen_seeds[from].origin,
					        gen_seeds[to].origin, master, master, sweep_min,
					        sweep_max, (unsigned int)passage_axis, &cross_ms))
						continue;
					selected_from = from;
					selected_to = destination;
					selected_cross_ms = cross_ms;
					selected_flight_ms = flight_ms;
					proved = true;
				}
				DoorPose_Restore(saved, member_count);
			}
			if (proved && !ShootDoor_AddLink(selected_from, selected_to, master,
			        opening_bound, selected_cross_ms, selected_flight_ms))
				goto done;
		}
	}
	red_reach = sg_host.level_alloc((size_t)gen_num_seeds);
	blue_reach = sg_host.level_alloc((size_t)gen_num_seeds);
	closed = red_reach && blue_reach &&
		Graph_ObjectiveReachMasks(red_root, blue_root, red_reach, blue_reach) &&
		blue_reach[red_root] && red_reach[blue_root];

done:
	if (!closed)
	{
		gen_num_links = link_mark;
		gen_num_mechanism_bindings = binding_mark;
		gen_train_links = train_mark;
		gen_declared_links = declared_mark;
	}
	if (red_reach) sg_host.level_free(red_reach);
	if (blue_reach) sg_host.level_free(blue_reach);
	return closed;
}

#define OBJECTIVE_CLOSURE_RUN_CALL_MAX 8192

static qboolean Prove_ObjectiveClosure(int red_root, int blue_root)
{
	door_topology_t topology = { NULL, NULL };
	byte *red_reach = NULL, *blue_reach = NULL;
	byte *red_forward = NULL, *blue_forward = NULL;
	int hook_from[2] = { -1, -1 };
	int hook_to[2] = { -1, -1 };
	vec3_t hook_anchor[2];
	short hook_cost[2] = { 0, 0 };
	byte hook_speed[2] = { 0, 0 };
	int calls[2] = { 0, 0 };
	int rejected[2] = { 0, 0 };
	int rocket_calls = 0;
	int link_mark = gen_num_links;
	int envelope_mark = gen_env_hook;
	qboolean closed = false;

	memset(hook_anchor, 0, sizeof(hook_anchor));
	red_reach = sg_host.level_alloc((size_t)gen_num_seeds);
	blue_reach = sg_host.level_alloc((size_t)gen_num_seeds);
	if (!red_reach || !blue_reach ||
	    !Graph_ObjectiveReachMasks(red_root, blue_root,
	        red_reach, blue_reach))
		goto done;
	if (Graph_ProveObjectiveRocketClosure(red_root, blue_root,
	        red_reach, blue_reach, &rocket_calls))
	{
		closed = true;
		goto done;
	}
	red_forward = sg_host.level_alloc((size_t)gen_num_seeds);
	blue_forward = sg_host.level_alloc((size_t)gen_num_seeds);
	if (!red_forward || !blue_forward ||
	    !Graph_ObjectiveForwardMasks(red_root, blue_root,
	        red_forward, blue_forward) ||
	    !Door_TopologyBuild(&topology))
		goto done;
	if (Link_ShootDoors(&topology, red_root, blue_root))
	{
		closed = true;
		goto done;
	}

	for (int i = 0; i < gen_num_links; i++)
	{
		const rune_link_t *link = &gen_links[i];
		unsigned int from_mask = Graph_ObjectivePartitionMask(link->from,
		    red_reach, blue_reach);
		unsigned int to_mask = Graph_ObjectivePartitionMask(link->to,
		    red_reach, blue_reach);
		int side;
		vec3_t anchor;
		short cost;
		byte speed;

		if (from_mask != 3U || (to_mask != 1U && to_mask != 2U) ||
		    topology.component[link->from] == topology.component[link->to])
			continue;
		side = to_mask == 1U ? 0 : 1;
		if (hook_from[side] >= 0 ||
		    !ProveHook(link->to, link->from, anchor, &cost, &speed))
			continue;
		hook_from[side] = link->to;
		hook_to[side] = link->from;
		VectorCopy(anchor, hook_anchor[side]);
		hook_cost[side] = cost;
		hook_speed[side] = speed;
	}
	if (hook_from[0] < 0 || hook_from[1] < 0)
		goto done;

	for (int side = 0; side < 2; side++)
	{
		int target_component = topology.component[hook_from[side]];
		qboolean proved = false;

		for (int radius =
		         SG_RUNE_PROOF_OBJECTIVE_RUN_MIN_HORIZONTAL_Q8 / 8 + 64;
		     radius <= SG_RUNE_PROOF_OBJECTIVE_RUN_MAX_HORIZONTAL_Q8 / 8 &&
		     !proved &&
		     calls[side] < OBJECTIVE_CLOSURE_RUN_CALL_MAX;
		     radius += 64)
		{
			float radius2 = (float)radius * (float)radius;
			float previous2 = (float)(radius - 64) * (float)(radius - 64);

			for (int from = 0; from < gen_num_seeds && !proved &&
			     calls[side] < OBJECTIVE_CLOSURE_RUN_CALL_MAX; from++)
			{
				for (int to = 0; to < gen_num_seeds && !proved &&
				     calls[side] < OBJECTIVE_CLOSURE_RUN_CALL_MAX; to++)
				{
					sg_rune_proof_objective_run_seed_t source = { 0 };
					sg_rune_proof_objective_run_seed_t target = { 0 };
					vec3_t delta;
					float horizontal2;
					short cost;
					byte speed;

					if (topology.component[to] != target_component)
						continue;
					for (int axis = 0; axis < 3; axis++)
					{
						source.origin_q8[axis] = (int32_t)lrintf(
						    gen_seeds[from].origin[axis] * 8.0f);
						target.origin_q8[axis] = (int32_t)lrintf(
						    gen_seeds[to].origin[axis] * 8.0f);
					}
					source.component = topology.component[from];
					target.component = topology.component[to];
					source.forward_mask =
					    (red_forward[from] ? 1U : 0U) |
					    (blue_forward[from] ? 2U : 0U);
					source.stable = gen_source_stable[from] ? 1U : 0U;
					target.stable = gen_source_stable[to] ? 1U : 0U;
					source.waterlevel = gen_source_waterlevel[from];
					target.waterlevel = gen_source_waterlevel[to];
					if (!SG_RuneProofObjectiveRunCandidate(&source, &target,
					        side ? 2U : 1U))
						continue;
					VectorSubtract(gen_seeds[to].origin,
					    gen_seeds[from].origin, delta);
					horizontal2 = delta[0] * delta[0] +
					    delta[1] * delta[1];
					if (horizontal2 <= previous2 || horizontal2 > radius2)
						continue;
					calls[side]++;
					if (!Prove(from, to, false, &cost, &speed))
						continue;
					if (!SG_RuneProofObjectiveRunReplayAccepted(
					        gen_prove_last_edge_seek,
					        gen_prove_last_airborne))
					{
						rejected[side]++;
						continue;
					}
					if (!Link_Add(from, to, RL_RUN, cost, speed))
						goto done;
					if (gen_prove_has_wp)
						VectorCopy(gen_prove_wp,
						    gen_links[gen_num_links - 1].anchor);
					if (!Link_Add(hook_from[side], hook_to[side], RL_HOOK,
					        hook_cost[side], hook_speed[side]))
						goto done;
					VectorCopy(hook_anchor[side],
					    gen_links[gen_num_links - 1].anchor);
					Link_Env_Hook(&gen_links[gen_num_links - 1],
					    hook_anchor[side]);
					proved = true;
				}
			}
		}
		if (!proved)
			goto done;
	}
	closed = gen_num_links == link_mark + 4 &&
	    Graph_ObjectiveReachMasks(red_root, blue_root,
	        red_reach, blue_reach) &&
	    blue_reach[red_root] && red_reach[blue_root];

done:
	sg_host.dprint("rune: objective closure rocket_calls=%d run_calls=%d/%d "
	               "nonruntime=%d/%d links=%d closed=%d limit=%d\n",
	               rocket_calls, calls[0], calls[1], rejected[0], rejected[1],
	               gen_num_links - link_mark, closed ? 1 : 0,
	               OBJECTIVE_CLOSURE_RUN_CALL_MAX);
	if (!closed)
	{
		gen_num_links = link_mark;
		gen_env_hook = envelope_mark;
	}
	if (red_reach) sg_host.level_free(red_reach);
	if (blue_reach) sg_host.level_free(blue_reach);
	if (red_forward) sg_host.level_free(red_forward);
	if (blue_forward) sg_host.level_free(blue_forward);
	Door_TopologyFree(&topology);
	return closed;
}

static qboolean Graph_PruneObjectiveCoreTry(qboolean defer_route_failure,
	int *red_root_out, int *blue_root_out)
{
	int *first_in = NULL, *next_in = NULL, *queue = NULL;
	byte *has_out = NULL, *keep = NULL, *red_reach = NULL, *blue_reach = NULL;
	graph_objective_diag_t red_diag, blue_diag;
	int red_root, blue_root, i, old_links, changed, iteration = 0;
	int initial_red = 0, initial_blue = 0, final_red = 0, final_blue = 0;
	int kept_seeds = 0, new_links = 0;
	int repair_link = -1, repairs = 0;
	byte repair_action = RL_RUN;
	uint32_t new_bindings = 0U;

	if (red_root_out) *red_root_out = -1;
	if (blue_root_out) *blue_root_out = -1;
	if (!redflag || !blueflag || !redflag->inuse || !blueflag->inuse)
	{
		sg_host.dprint("rune: FAILED: objective flags are unavailable\n");
		return false;
	}
	if (gen_num_seeds <= 0)
	{
		sg_host.dprint("rune: objective-core roots red=-1 blue=-1 "
		               "initial_red_reach=0 initial_blue_reach=0 "
		               "final_red_reach=0 final_blue_reach=0 "
		               "red_to_blue=0 blue_to_red=0 reason=no-seeds\n");
		sg_host.dprint("rune: FAILED: objective-core has no seeds\n");
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
		Rune_TelemetryAdd(&gen_telemetry.link_scans, 1U);
		has_out[gen_links[i].from] = 1;
		next_in[i] = first_in[gen_links[i].to];
		first_in[gen_links[i].to] = i;
	}
	red_root = Graph_ObjectiveRoot(redflag->homeposition, has_out, &red_diag);
	blue_root = Graph_ObjectiveRoot(blueflag->homeposition, has_out, &blue_diag);
	if (red_root_out) *red_root_out = red_root;
	if (blue_root_out) *blue_root_out = blue_root;
	if (red_root < 0 || blue_root < 0)
	{
		Graph_LogObjectiveRoot("red", &red_diag, red_root);
		Graph_LogObjectiveRoot("blue", &blue_diag, blue_root);
		sg_host.dprint("rune: objective-core roots red=%d blue=%d "
		               "initial_red_reach=0 initial_blue_reach=0 "
		               "final_red_reach=0 final_blue_reach=0 "
		               "red_to_blue=0 blue_to_red=0 reason=objective-root\n",
		               red_root, blue_root);
		sg_host.dprint("rune: FAILED: cannot bind both flag objectives to graph\n");
		goto fail;
	}

	do
	{
		int removed = 0;

		changed = 0;
		Graph_ReverseReach(red_root, keep, first_in, next_in, queue, red_reach);
		Graph_ReverseReach(blue_root, keep, first_in, next_in, queue, blue_reach);
		if (iteration == 0)
		{
			initial_red = Graph_ReachCount(red_reach);
			initial_blue = Graph_ReachCount(blue_reach);
			sg_host.dprint("rune: objective-core initial red_reach=%d blue_reach=%d\n",
			               initial_red, initial_blue);
			Graph_LogObjectivePartitions(red_reach, blue_reach);
			Graph_LogBoundaryLinks("initial", red_reach, blue_reach);
			if ((!blue_reach[red_root] || !red_reach[blue_root]) &&
			    Graph_ProveObjectiveReverse(red_reach, blue_reach,
			        &repair_link, &repair_action))
			{
				repairs++;
				sg_host.dprint("rune: objective-repair kind=%s link=%d "
				               "repairs=%d\n",
				               repair_action == RL_DROP ? "hook-reverse-drop" :
				               repair_action == RL_HOOK ? "reverse-hook" :
				               "reverse-rocketjump", repair_link, repairs);
				sg_host.level_free(next_in);
				next_in = sg_host.level_alloc(sizeof(*next_in) *
				    (size_t)gen_num_links);
				if (!next_in)
					goto fail;
				for (i = 0; i < gen_num_seeds; i++)
					first_in[i] = -1;
				memset(has_out, 0, (size_t)gen_num_seeds);
				memset(keep, 1, (size_t)gen_num_seeds);
				for (i = 0; i < gen_num_links; i++)
				{
					has_out[gen_links[i].from] = 1;
					next_in[i] = first_in[gen_links[i].to];
					first_in[gen_links[i].to] = i;
				}
				changed = 1;
				continue;
			}
		}
		for (i = 0; i < gen_num_seeds; i++)
			if (keep[i] && (!red_reach[i] || !blue_reach[i]))
			{
				keep[i] = 0;
				changed = 1;
				removed++;
			}
		sg_host.dprint("rune: objective-core iteration=%d removed=%d\n",
		               iteration, removed);
		iteration++;
	} while (changed);
	/* The terminating pass is the fixed point, so these are the final counts
	 * and the two membership bits used for the deterministic boundary report. */
	final_red = Graph_ReachCount(red_reach);
	final_blue = Graph_ReachCount(blue_reach);
	sg_host.dprint("rune: objective-core final red_reach=%d blue_reach=%d "
	               "red_to_blue=%d blue_to_red=%d iterations=%d\n",
	               final_red, final_blue, blue_reach[red_root] ? 1 : 0,
	               red_reach[blue_root] ? 1 : 0, iteration);
	if (!keep[red_root] || !keep[blue_root])
	{
		if (defer_route_failure)
			goto fail;
		Graph_LogObjectiveRoot("red", &red_diag, red_root);
		Graph_LogObjectiveRoot("blue", &blue_diag, blue_root);
		Graph_LogBoundaryLinks("final", red_reach, blue_reach);
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
		if (link.mechanism_plan != RUNE_NO_MECHANISM_PLAN)
		{
			if (!gen_mechanism_bindings ||
			    link.mechanism_plan >= gen_num_mechanism_bindings ||
			    new_bindings >= (uint32_t)LINK_MAX)
			{
				gen_mechanism_failed = true;
				goto fail;
			}
			gen_mechanism_bindings[new_bindings] =
				gen_mechanism_bindings[link.mechanism_plan];
			link.mechanism_plan = new_bindings++;
		}
		gen_links[new_links++] = link;
	}
	gen_num_links = new_links;
	gen_num_mechanism_bindings = new_bindings;
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

static qboolean Graph_PruneObjectiveCore(void)
{
	return Graph_PruneObjectiveCoreTry(false, NULL, NULL);
}

static qboolean Graph_PruneObjectiveCoreWithClosure(void)
{
	int red_root, blue_root;

	if (Graph_PruneObjectiveCoreTry(true, &red_root, &blue_root))
		return true;
	if (red_root < 0 || blue_root < 0)
		return false;
	Prove_ObjectiveClosure(red_root, blue_root);
	return Graph_PruneObjectiveCore();
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
static int Doors_TargetIdentity(void *context, void *opaque, int key)
{
	edict_t *entity = opaque;

	(void)context;
	return entity && g_edicts && key >= 0 && key < globals.num_edicts &&
	       entity == &g_edicts[key] && entity->inuse && entity->classname &&
	       strncmp(entity->classname, "func_door", 9) == 0;
}

static int Doors_GetSolid(void *context, void *opaque)
{
	(void)context;
	return (int)((edict_t *)opaque)->solid;
}

static int Doors_GetLinkcount(void *context, void *opaque)
{
	(void)context;
	return ((edict_t *)opaque)->linkcount;
}

static void Doors_SetSolid(void *context, void *opaque, int solid)
{
	(void)context;
	((edict_t *)opaque)->solid = (solid_t)solid;
}

static void Doors_SetLinkcount(void *context, void *opaque, int linkcount)
{
	(void)context;
	((edict_t *)opaque)->linkcount = linkcount;
}

static void Doors_LinkEntity(void *context, void *opaque)
{
	(void)context;
	sg_host.linkentity((edict_t *)opaque);
}

static const sg_rune_door_scope_ops_t doors_scope_ops = {
	Doors_TargetIdentity,
	Doors_GetSolid,
	Doors_GetLinkcount,
	Doors_SetSolid,
	Doors_SetLinkcount,
	Doors_LinkEntity
};

/* Collect the complete target set before SG_RuneDoorScopeOpen changes the
 * first solid.  The former negative-count convention discovered overflow only
 * after opening 128 doors and made every caller responsible for partial
 * rollback. */
static sg_rune_door_scope_status_t Doors_Open(
	sg_rune_door_scope_t *scope)
{
	sg_rune_door_scope_target_t targets[SG_RUNE_DOOR_SCOPE_MAX];
	size_t count = 0;
	int index;

	if (!scope || !g_edicts || globals.num_edicts < 0)
		return SG_RUNE_DOOR_SCOPE_INVALID_ARGUMENT;
	for (index = 0; index < globals.num_edicts; index++)
	{
		edict_t *entity = &g_edicts[index];

		if (!entity->inuse || !entity->classname ||
		    strncmp(entity->classname, "func_door", 9) != 0)
			continue;
		if (count >= SG_RUNE_DOOR_SCOPE_MAX)
			return SG_RUNE_DOOR_SCOPE_CAPACITY;
		targets[count].entity = entity;
		targets[count].key = index;
		count++;
	}
	return SG_RuneDoorScopeOpen(scope, targets, count, (int)SOLID_NOT,
		&doors_scope_ops, NULL);
}

static sg_rune_door_scope_status_t Doors_Restore(
	sg_rune_door_scope_t *scope)
{
	return SG_RuneDoorScopeRestore(scope, &doors_scope_ops, NULL);
}

qboolean Rune_Generate(const char *mapname)
{
	sg_rune_authority_t authority;
	rune_recheck_t recheck;
	sg_rune_stream_source_t stream_source;
	sg_rune_stream_result_t stream_failure;
	sg_rune_stream_t *stream = NULL;
	sg_mechanism_plan_buffers_t mechanism_buffers;
	sg_mechanism_plan_result_t mechanism_result;
	rune_mechanism_edge_t *mechanism_edges = NULL;
	rune_mechanism_plan_t *mechanism_plans = NULL;
	uint32_t *mechanism_edge_marks = NULL;
	uint32_t *mechanism_node_marks = NULL;
	uint32_t *mechanism_node_queue = NULL;
	sg_rune_install_result_t install_result;
	const sg_rune_install_ops_t *install_ops;
	char game_directory[MAX_OSPATH];
	char path[MAX_OSPATH], tmp_path[MAX_OSPATH];
	sg_rune_door_scope_t doors;
	door_topology_t compound_topology = { NULL, NULL };
	sg_rune_door_scope_status_t door_status;
	int door_count = 0;
	int directory_written;
	qboolean scope_active = false;
	qboolean generated = false;
	qboolean door_restore_failed = false;
	cvar_t *game_directory_cvar;
	const char *game_directory_source;
	const char *canonical_mapname;

	SG_RuneDoorScopeInit(&doors);
	Drop_PrefixCacheClear();
	SG_HooksInit();
	memset(&gen_mechanism_catalog, 0, sizeof(gen_mechanism_catalog));
	if (SG_MechCatalogSnapshot(&gen_mechanism_catalog) !=
	    SG_MECH_CATALOG_READY)
	{
		sg_host.dprint("rune: generation refused stage=mechanism-catalog "
		               "reason=%s\n", SG_MechCatalogReason());
		return false;
	}
	if (!SG_RuneAuthorityCapture(mapname, &authority))
	{
		if (authority.identity_status != SG_IDENTITY_OK)
			sg_host.dprint("rune: generation refused stage=identity "
			               "status=%d reason=%s\n",
			               (int)authority.identity_status,
			               SG_LevelIdentityReason(authority.identity_status));
		else
			sg_host.dprint("rune: generation refused stage=proof-law "
			               "reason=unsupported-law\n");
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
		sg_host.dprint("rune: generation refused stage=path "
		               "reason=MAX_OSPATH\n");
		return false;
	}
	if (!SG_RuneProofScopeBegin(authority.identity.gravity))
	{
		sg_host.dprint("rune: generation refused stage=proof-scope "
		               "reason=busy-or-invalid\n");
		return false;
	}
	scope_active = true;

	gen_seeds = NULL;
	gen_links = NULL;
	gen_mechanism_bindings = NULL;
	gen_water_parents = NULL;
	gen_water_ranks = NULL;
	gen_water_edges = NULL;
	gen_water_edge_slots = NULL;
	gen_seeds = sg_host.game_alloc(sizeof(rune_seed_t) * SEED_MAX);
	gen_links = sg_host.game_alloc(sizeof(rune_link_t) * LINK_MAX);
	gen_mechanism_bindings = sg_host.game_alloc(
		sizeof(*gen_mechanism_bindings) * LINK_MAX);
	gen_water_parents = sg_host.game_alloc(sizeof(*gen_water_parents) * SEED_MAX);
	gen_water_ranks = sg_host.game_alloc(sizeof(*gen_water_ranks) * SEED_MAX);
	gen_water_edges = sg_host.game_alloc(sizeof(*gen_water_edges) * LINK_MAX);
	gen_water_edge_slots = sg_host.game_alloc(
		sizeof(*gen_water_edge_slots) * LINK_MAX * 2U);
	if (!gen_seeds || !gen_links || !gen_mechanism_bindings ||
	    !gen_water_parents || !gen_water_ranks || !gen_water_edges ||
	    !gen_water_edge_slots ||
	    !SG_WaterForestInit(&gen_water_forest, gen_water_parents,
	    gen_water_ranks, SEED_MAX, gen_water_edges, LINK_MAX,
	    gen_water_edge_slots, LINK_MAX * 2U))
	{
		sg_host.dprint("rune: FAILED: generator allocation; graph was not written\n");
		goto cleanup;
	}
	gen_num_seeds = 0;
	gen_num_links = 0;
	gen_num_mechanism_bindings = 0U;
	gen_mechanism_failed = false;
	gen_seed_overflow = false;
	gen_link_overflow = false;
	gen_water_overflow = false;
	memset(&gen_telemetry, 0, sizeof(gen_telemetry));
	memset(&gen_phase_telemetry, 0, sizeof(gen_phase_telemetry));
	memset(hash_head, 0xff, sizeof(hash_head));
	/* Reset every generator budget and diagnostic for this invocation. */
	dg_pairs = dg_seek = dg_noedge = dg_fell = dg_arrived = dg_nocontact = 0;
	dd_last_heading = 0;
	dd_nolip = dd_fenced = dd_flew = dd_landed = dd_won = 0;
	gen_momentum_links = gen_waypoint_links = 0;
	gen_prove_has_wp = false;
	VectorClear(gen_prove_wp);
	gen_first_water = -1;
	gen_num_water = 0;
	gen_lift_links = gen_tele_links = gen_door_links = gen_push_links = 0;
	gen_train_links = 0;
	gen_button_door_links = 0;
	gen_swim_links = 0;
	gen_door_drop_trials = gen_door_drop_proofs = 0;
	gen_door_drop_compound_trials = gen_door_drop_compound_proofs = 0;
	gen_env_drop = gen_env_hook = gen_declared_links = 0;
	gen_lift_down_drop = gen_lift_down_none = 0;
	rj_pairs = rj_tries = rj_noboom = rj_nolift = rj_arrived = 0;
	rj_redundant = rj_links = rj_budget_out = 0;
	rj_query = 0;

	Rune_TelemetryPhaseStart("door-open");
	door_status = Doors_Open(&doors);
	if (door_status != SG_RUNE_DOOR_SCOPE_OK)
	{
		if (door_status == SG_RUNE_DOOR_SCOPE_RESTORE_FAILED)
			door_restore_failed = true;
		if (door_status == SG_RUNE_DOOR_SCOPE_CAPACITY)
			sg_host.dprint("rune: FAILED: more than 128 doors; graph was not written\n");
		else
			sg_host.dprint("rune: FAILED: door-open scope reason=%s; "
			               "graph was not written\n",
			               SG_RuneDoorScopeStatusName(door_status));
		goto cleanup;
	}
	door_count = (int)doors.count;
	sg_host.dprint("rune: %d doors held open for proving\n", door_count);
	Rune_TelemetryPhaseEnd();

	Rune_TelemetryPhaseStart("seed-germinate");
	sg_host.dprint("rune: germinating from entities...\n");
	Seed_Germinate();
	Rune_TelemetryPhaseEnd();
	Rune_TelemetryPhaseStart("seed-flood");
	sg_host.dprint("rune: %d germs; flooding...\n", gen_num_seeds);
	Seed_Flood();
	Rune_TelemetryPhaseEnd();
	Rune_TelemetryPhaseStart("water-seed");
	/* Seed water volumes that the dry passes cannot reach before proving, so
	 * the pair loop sees them like any other seed. */
	Seed_Water();
	Rune_TelemetryPhaseEnd();
	if (gen_water_overflow)
	{
		sg_host.dprint("rune: FAILED: water seed capacity exhausted; "
		               "graph was not written\n");
		goto cleanup;
	}
	if (gen_num_seeds <= 0)
	{
		door_status = Doors_Restore(&doors);
		if (door_status != SG_RUNE_DOOR_SCOPE_OK)
		{
			door_restore_failed = true;
			sg_host.dprint("rune: FAILED: base door restore reason=%s; "
			               "graph was not written\n",
			               SG_RuneDoorScopeStatusName(door_status));
			goto cleanup;
		}
		sg_host.dprint("rune: FAILED: map produced no executable seeds\n");
		goto cleanup;
	}
	Rune_TelemetryPhaseStart("exposure");
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
				if (etr.fraction >= 1.0f &&
				    !SG_OracleRotatorSweepBlocks(a, NULL, NULL, b,
				                                 MASK_OPAQUE))
					vis++;
			}
			/* scale to the byte-ish range; sampled can be < 24 in sparse
			 * regions, so store the RATE, not the raw count */
			gen_seeds[i].area_hint = (short)(sampled ?
			    (vis * 255) / sampled : 0);
		}
	}
	Rune_TelemetryPhaseEnd();
	sg_host.dprint("rune: %d seeds; proving links...\n", gen_num_seeds);
	Rune_TelemetryPhaseStart("base-links");
	Prove_BaseLinks(&compound_topology);
	door_status = Doors_Restore(&doors);
	if (door_status != SG_RUNE_DOOR_SCOPE_OK)
	{
		door_restore_failed = true;
		sg_host.dprint("rune: FAILED: base door restore reason=%s; "
		               "graph was not written\n",
		               SG_RuneDoorScopeStatusName(door_status));
		goto cleanup;
	}
	Rune_TelemetryPhaseEnd();
	Rune_TelemetryPhaseStart("compound-swim");
	SG_OracleDoorBoundsCacheBegin();
	if (!SG_CompoundGenGameGenerate(gen_seeds, (size_t)gen_num_seeds,
	    gen_links, &gen_num_links, LINK_MAX, &compound_topology,
	    sg_host.level_alloc, sg_host.level_free))
	{
		SG_OracleDoorBoundsCacheEnd();
		sg_host.dprint("rune: FAILED: compound-swim generation\n");
		goto cleanup;
	}
	SG_OracleDoorBoundsCacheEnd();
	Rune_TelemetryPhaseEnd();
	Rune_TelemetryPhaseStart("compound-links");
	Link_CompoundDrops();
	Link_CompoundHooks();
	Rune_TelemetryPhaseEnd();
	Rune_TelemetryPhaseStart("rocket-jumps");
	door_status = Doors_Open(&doors);
	if (door_status != SG_RUNE_DOOR_SCOPE_OK)
	{
		if (door_status == SG_RUNE_DOOR_SCOPE_RESTORE_FAILED)
			door_restore_failed = true;
		sg_host.dprint("rune: FAILED: rocket-jump door scope reason=%s; "
		               "graph was not written\n",
		               SG_RuneDoorScopeStatusName(door_status));
		goto cleanup;
	}
	/* Last of all, as before: RJ asks whether every cheaper prover already
	 * reaches the destination. The action is dormant today, but retaining this
	 * call and its open-door world preserves its exact current order. */
	Prove_RocketJumps();
	door_status = Doors_Restore(&doors);
	if (door_status != SG_RUNE_DOOR_SCOPE_OK)
	{
		door_restore_failed = true;
		sg_host.dprint("rune: FAILED: rocket-jump door restore reason=%s; "
		               "graph was not written\n",
		               SG_RuneDoorScopeStatusName(door_status));
		goto cleanup;
	}
	Rune_TelemetryPhaseEnd();
	sg_host.dprint("rune: %d links proven\n", gen_num_links);
	sg_host.dprint("rune: dropstats pairs=%d seek=%d noedge=%d fellsteps=%d arrived=%d nocontact=%d\n",
	           dg_pairs, dg_seek, dg_noedge, dg_fell, dg_arrived, dg_nocontact);
	sg_host.dprint("rune: geodrop nolip=%d fenced=%d flew=%d landedsteps=%d won=%d\n",
	           dd_nolip, dd_fenced, dd_flew, dd_landed, dd_won);
	sg_host.dprint("rune: envelopes drop=%d hook=%d; declared=%d "
	           "(lift=%d tele=%d door=%d button-door=%d push=%d train=%d); "
	           "plat-down drop=%d unlinked=%d; momentum=%d waypoints=%d\n",
	           gen_env_drop, gen_env_hook, gen_declared_links,
	           gen_lift_links, gen_tele_links, gen_door_links,
	           gen_button_door_links, gen_push_links, gen_train_links,
	           gen_lift_down_drop, gen_lift_down_none, gen_momentum_links,
	           gen_waypoint_links);
	sg_host.dprint("rune: door-drop probe trials=%d proofs=%d\n",
	               gen_door_drop_trials, gen_door_drop_proofs);
	sg_host.dprint("rune: door-drop compound trials=%d proofs=%d\n",
	               gen_door_drop_compound_trials,
	               gen_door_drop_compound_proofs);
	/* Objective ownership is resolved against the real, restored world.  Mark
	 * every non-core geometry sample before writing so runtime localization and
	 * the deployment linter share the same fail-closed topology contract. */
	Rune_TelemetryPhaseStart("objective-core");
	if (!Graph_PruneObjectiveCoreWithClosure())
		goto cleanup;
	Rune_TelemetryPhaseEnd();
	/* SEED_MAX/LINK_MAX are inclusive graph count limits.  Filling the final
	 * allocated slot is legal; only an attempted insertion beyond it sets the
	 * corresponding overflow flag and invalidates the graph. */
	if (gen_seed_overflow || gen_link_overflow || gen_water_overflow)
	{
		sg_host.dprint("rune: FAILED: %s capacity exhausted; graph was not written\n",
		               gen_seed_overflow ? "seed" :
		               gen_water_overflow ? "water seed" : "link");
		goto cleanup;
	}
	if (gen_mechanism_failed)
	{
		sg_host.dprint("rune: FAILED: exact mechanism binding failed; "
		               "graph was not written\n");
		goto cleanup;
	}
	if (gen_num_links <= 0)
	{
		sg_host.dprint("rune: FAILED: no executable links were proven; graph was not written\n");
		goto cleanup;
	}

	Rune_TelemetryPhaseStart("mechanism-plan");
	memset(&mechanism_buffers, 0, sizeof(mechanism_buffers));
	memset(&mechanism_result, 0, sizeof(mechanism_result));
	if (gen_mechanism_catalog.num_edges != 0U ||
	    gen_num_mechanism_bindings != 0U)
	{
		uint32_t edge_capacity = gen_num_mechanism_bindings != 0U
			? RUNE_MAX_MECHANISM_EDGES
			: gen_mechanism_catalog.num_edges;

		mechanism_edges = Rune_StreamAllocate(NULL,
			(size_t)edge_capacity * sizeof(*mechanism_edges));
		mechanism_buffers.edges = mechanism_edges;
		mechanism_buffers.edge_capacity = edge_capacity;
	}
	if (gen_num_mechanism_bindings != 0U)
	{
		mechanism_plans = Rune_StreamAllocate(NULL,
			(size_t)gen_num_mechanism_bindings * sizeof(*mechanism_plans));
		mechanism_edge_marks = Rune_StreamAllocate(NULL,
			(size_t)gen_mechanism_catalog.num_edges *
			    sizeof(*mechanism_edge_marks));
		mechanism_node_marks = Rune_StreamAllocate(NULL,
			(size_t)gen_mechanism_catalog.num_nodes *
			    sizeof(*mechanism_node_marks));
		mechanism_node_queue = Rune_StreamAllocate(NULL,
			(size_t)gen_mechanism_catalog.num_nodes *
			    sizeof(*mechanism_node_queue));
		mechanism_buffers.plans = mechanism_plans;
		mechanism_buffers.plan_capacity = gen_num_mechanism_bindings;
		mechanism_buffers.edge_marks = mechanism_edge_marks;
		mechanism_buffers.edge_mark_capacity =
			gen_mechanism_catalog.num_edges;
		mechanism_buffers.node_marks = mechanism_node_marks;
		mechanism_buffers.node_mark_capacity =
			gen_mechanism_catalog.num_nodes;
		mechanism_buffers.node_queue = mechanism_node_queue;
		mechanism_buffers.node_queue_capacity =
			gen_mechanism_catalog.num_nodes;
	}
	if (!SG_MechanismPlansMaterialize(gen_links, (uint32_t)gen_num_links,
	    gen_mechanism_bindings, gen_num_mechanism_bindings,
	    &gen_mechanism_catalog, &mechanism_buffers, &mechanism_result))
	{
		sg_host.dprint("rune: FAILED: mechanism plan materialization "
		               "reason=%s link=%u plan=%u inventory_edges=%u "
		               "edges=%u plans=%u; graph was not written\n",
		               SG_MechanismPlanDiagnosticName(
		                   mechanism_result.diagnostic),
		               (unsigned int)mechanism_result.link_index,
		               (unsigned int)mechanism_result.plan_index,
		               (unsigned int)mechanism_result.num_inventory_edges,
		               (unsigned int)mechanism_result.num_edges,
		               (unsigned int)mechanism_result.num_plans);
		goto cleanup;
	}
	Rune_TelemetryPhaseEnd();
	sg_host.dprint("rune: mechanism plans materialized nodes=%u triggers=%u "
	               "inventory_edges=%u plan_edges=%u plans=%u\n",
	               (unsigned int)gen_mechanism_catalog.num_nodes,
	               (unsigned int)Mechanism_TriggerCount(),
	               (unsigned int)mechanism_result.num_inventory_edges,
	               (unsigned int)(mechanism_result.num_edges -
	                   mechanism_result.num_inventory_edges),
	               (unsigned int)mechanism_result.num_plans);

	memset(&stream_source, 0, sizeof(stream_source));
	stream_source.identity = &authority.identity;
	stream_source.seeds = gen_seeds;
	stream_source.num_seeds = (uint32_t)gen_num_seeds;
	stream_source.links = gen_links;
	stream_source.num_links = (uint32_t)gen_num_links;
	stream_source.nodes = gen_mechanism_catalog.nodes;
	stream_source.num_nodes = gen_mechanism_catalog.num_nodes;
	stream_source.edges = mechanism_edges;
	stream_source.num_edges = mechanism_result.num_edges;
	stream_source.plans = mechanism_plans;
	stream_source.num_plans = mechanism_result.num_plans;
	stream_source.strings = gen_mechanism_catalog.strings;
	stream_source.string_bytes = gen_mechanism_catalog.string_bytes;
	stream = SG_RuneStreamCreate(&stream_source, Rune_StreamAllocate,
		Rune_StreamRelease, NULL, &stream_failure);
	if (!stream)
	{
		sg_host.dprint("rune: FAILED: RUNE stream allocation diagnostic=%d "
		               "stage=%d\n", stream_failure.diagnostic,
		               (int)stream_failure.stage);
		goto cleanup;
	}
	memset(&recheck, 0, sizeof(recheck));
	recheck.mapname = canonical_mapname;
	recheck.captured = &authority;
	recheck.identity_status = SG_IDENTITY_OK;
	install_result = SG_RuneInstall(game_directory, canonical_mapname,
		path, sizeof(path), tmp_path, sizeof(tmp_path), SG_RuneStreamWrite,
		stream, Rune_Revalidate, &recheck, install_ops);
	if (install_result.status != SG_RUNE_INSTALL_OK)
	{
		if (install_result.status == SG_RUNE_INSTALL_REVALIDATE_FAILED)
		{
			if (recheck.failure == RUNE_RECHECK_IDENTITY)
				sg_host.dprint("rune: revalidation failed "
				               "kind=identity status=%d reason=%s\n",
				               (int)recheck.identity_status,
				               SG_LevelIdentityReason(recheck.identity_status));
			else
				sg_host.dprint("rune: revalidation failed "
				               "kind=proof-law\n");
		}
		sg_host.dprint("rune: install failed status=%d reason=%s "
		               "os_error=%d cleanup_error=%d diagnostic=%d "
		               "writer_stage=%d writer_index=%u "
		               "bytes=%lu; kept existing %s\n",
		               (int)install_result.status,
		               SG_RuneInstallReason(install_result.status),
		               install_result.os_error,
		               install_result.cleanup_error,
		               (int)install_result.writer.diagnostic,
		               (int)install_result.writer.stage,
		               (unsigned int)install_result.writer.index,
		               (unsigned long)install_result.writer.bytes_written,
		               path);
		goto cleanup;
	}

	sg_host.dprint("rune: wrote %s (%d seeds, %d links, %u mechanism "
	               "nodes, %u triggers, %u inventory edges, %u activation "
	               "plans)\n", path, gen_num_seeds, gen_num_links,
	               (unsigned int)gen_mechanism_catalog.num_nodes,
	               (unsigned int)Mechanism_TriggerCount(),
	               (unsigned int)mechanism_result.num_inventory_edges,
	               (unsigned int)mechanism_result.num_plans);
	generated = true;

cleanup:
	Rune_TelemetryPhaseEnd();
	Drop_PrefixCacheClear();
	Door_TopologyFree(&compound_topology);
	if (SG_RuneDoorScopeActive(&doors))
	{
		door_status = Doors_Restore(&doors);
		if (door_status != SG_RUNE_DOOR_SCOPE_OK)
		{
			sg_host.dprint("rune: FAILED: cleanup door restore reason=%s; "
			               "graph was not written\n",
			               SG_RuneDoorScopeStatusName(door_status));
			generated = false;
		}
		else if (door_restore_failed)
			sg_host.dprint("rune: cleanup restored pending door scope; "
			               "graph remains unwritten\n");
	}
	if (door_restore_failed)
		generated = false;
	SG_RuneStreamDestroy(stream);
	Rune_StreamRelease(NULL, mechanism_edges);
	Rune_StreamRelease(NULL, mechanism_plans);
	Rune_StreamRelease(NULL, mechanism_edge_marks);
	Rune_StreamRelease(NULL, mechanism_node_marks);
	Rune_StreamRelease(NULL, mechanism_node_queue);
	if (gen_seeds)
		sg_host.game_free(gen_seeds);
	if (gen_links)
		sg_host.game_free(gen_links);
	if (gen_mechanism_bindings)
		sg_host.game_free(gen_mechanism_bindings);
	if (gen_water_parents)
		sg_host.game_free(gen_water_parents);
	if (gen_water_ranks)
		sg_host.game_free(gen_water_ranks);
	if (gen_water_edges)
		sg_host.game_free(gen_water_edges);
	if (gen_water_edge_slots)
		sg_host.game_free(gen_water_edge_slots);
	gen_seeds = NULL;
	gen_links = NULL;
	gen_mechanism_bindings = NULL;
	gen_water_parents = NULL;
	gen_water_ranks = NULL;
	gen_water_edges = NULL;
	gen_water_edge_slots = NULL;
	gen_num_mechanism_bindings = 0U;
	gen_num_seeds = 0;
	gen_num_links = 0;
	if (scope_active)
		SG_RuneProofScopeEnd();
	Rune_LogFlush();
	return generated;
}
