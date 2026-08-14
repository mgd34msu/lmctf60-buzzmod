/*
 * sg_rune.h -- the RUNE: what a map affords, proven and written down.
 *
 * A rune is a link graph over the map's movement phase space. Every link in
 * it was demonstrated by the oracle -- the real physics rolled forward from
 * a concrete entry state -- before it was recorded. Nothing is inferred from
 * geometry heuristics; a link exists because a phantom traversed it.
 *
 * File: maps/<mapname>.rune, flat binary, little-endian, versioned. The
 * generator writes it once per map; the runtime maps it read-only; learning
 * appends OBSERVED links and adjusts costs in place.
 */

#pragma once

#include <limits.h>

/* The flat v2 structs and sidecar CRC are canonical little-endian bytes.
 * This game module currently serializes those structs directly, so fail at
 * compile time instead of silently producing/accepting incompatible assets on
 * a big-endian target. A future portable writer may replace this guard with
 * explicit field encoding. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "SLIPGATE rune v2 requires a little-endian target"
#endif

#define RUNE_MAGIC      0x454E5552      /* "RUNE" */
/* Version 2 changes proof semantics without changing the flat structs:
 * grapple links use the production 100 ms pull cadence and serialize the
 * exact quantized shot control plus its static-world ray distance. Version 1
 * hook links used a private 25 ms pull model and must never be treated as
 * equivalent. Every v2 physics proof is also bound to the movement constants below;
 * generation and execution fail closed when the server uses another law. */
#define RUNE_VERSION    2
#define RUNE_PROOF_GRAVITY 800
#define RUNE_PROOF_AIRACCELERATE 0.0f
#define RUNE_HOOK_BOLT_SPEED 800.0f
#define RUNE_HOOK_FRAME_DISTANCE 80.0f
#define RUNE_MAX_SEEDS  32768
#define RUNE_MAX_LINKS  262144
#define RUNE_HOOK_MAX_RAY 8192.0f
/* Dry graph hooks retain their proved entry-heading slack. Water-origin hooks
 * use an otherwise-invalid marker because their controller is a different
 * contract: actual-state online reproof, zero-input outbound drift, exact
 * attachment state, air reserve, and a dry destination. */
#define RUNE_HOOK_CONTROL_SLACK 24
#define RUNE_WATER_HOOK_CONTROL_MARKER 253
#define RUNE_HOOK_DRY_SETTLE_MS 1000
#define RUNE_HOOK_WATER_SETTLE_MS 1250
#define RUNE_TELEPORT_SEED_REACH 128.0f
#define RUNE_DROP_RECOVERY_RADIUS 96.0f
#define RUNE_DROP_RECOVERY_Z 72.0f

/* how a link is traversed */
typedef enum
{
	RL_RUN = 0,         /* ground movement, no jump required */
	RL_JUMP,            /* requires a jump during the traversal */
	RL_DROP,            /* a fall the mover survives */
	RL_HOOK,            /* grapple at the anchor stored in the link */
	RL_SWIM,            /* through water */
	/*
	 * Appended, never inserted: every value above keeps the number it had, so
	 * a rune written before these existed still reads correctly. These are
	 * actions the mover cannot prove by
	 * rolling physics -- the world moves it, not the other way round.
	 */
	RL_LIFT,            /* ride a func_plat from its bottom to its top */
	RL_TELEPORT,        /* step on a misc_teleporter; the game does the rest */
	/* Reserved numeric value. V2 cannot serialize the exact live launch state,
	 * so generation emits none and loading rejects it fail-closed. A future
	 * format may give the action a complete controller contract. */
	RL_ROCKETJUMP,
	/* A repeatable player trigger opens one validated func_door target set. The
	 * body starts at a connected dry rest seed, follows the exact sweep-clear
	 * controller to anchor, waits there for every member to reach STATE_TOP,
	 * then follows a proved open-pose egress to a dry static destination outside
	 * the sweep. Appended after the reserved RJ value so every earlier on-disk
	 * action number remains unchanged. */
	RL_DOOR,
} rune_action_t;

/* how the link came to be believed */
typedef enum
{
	RL_PROVEN = 0,      /* the oracle rolled it */
	RL_OBSERVED,        /* a player demonstrated it in play */
	RL_ADJUSTED,        /* proven, but cost corrected by experience */
	/*
	 * Appended, never inserted -- the same rule the action enum above keeps,
	 * for the same reason: RL_PROVEN/OBSERVED/ADJUSTED hold the numbers they
	 * have always held, so a rune written before this value existed still
	 * reads correctly. RL_DECLARED marks a link that
	 * was READ OFF the map rather than simulated: the lift and teleport links
	 * come from a func_plat's spawn positions and a misc_teleporter's target,
	 * with the cost computed from g_func.c's own move maths. Calling those
	 * PROVEN would be a lie about how they were established, and the runtime
	 * has a real interest in the difference -- a declared link's cost has
	 * never been measured against a clock.
	 */
	RL_DECLARED,        /* read off the map's spawn data, not simulated */
} rune_provenance_t;

/* seed flags: bits in rune_seed_t.flags, a field that has always been there
 * and has always been written as 0 -- so setting a bit needs no new version */
#define RSF_WATER	1       /* the seed is a point INSIDE a water volume, not
                             * a floor point: reached and left by swimming */
#define RSF_TOMBSTONE	2   /* retained geometry owner outside the closed
                             * two-objective route core; has no links and
                             * makes localization fail closed instead of
                             * snapping through it to a farther live seed */

/* V2 declared controllers were redesigned after early experimental files
 * had already used the ordinary 255 heading-slack byte. This otherwise-unused
 * byte is the fail-closed contract marker for exact source/approach/egress
 * records; stale declarations are rejected without perturbing the disk layout. */
#define RUNE_DECLARED_CONTROL_MARKER 254
/* DROP gained a proved, ground-only first-impact recovery after early v2
 * experimental graphs existed. Mark the complete controller contract in its
 * otherwise-unbounded entry-heading byte so stale v2 records cannot silently
 * acquire movement that their proof never executed. */
#define RUNE_DROP_CONTROL_MARKER 254

typedef struct rune_seed_s
{
	vec3_t	origin;         /* on the floor, player-standable, proven --
	                         * or inside water when flags & RSF_WATER */
	short	area_hint;      /* reserved: coarse region id, 0 for now */
	short	flags;
} rune_seed_t;

typedef struct rune_link_s
{
	int		from, to;       /* seed indices */
	byte	action;         /* rune_action_t */
	byte	provenance;     /* rune_provenance_t */
	byte	min_speed;      /* entry envelope: speed/4, 0 = from rest */
	byte	heading;        /* entry heading, 0-255 wrapping the circle */
	byte	heading_slack;  /* +/- tolerance, same units */
	byte	exit_speed;     /* speed/4 the traversal ended with */
	short	cost_ms;        /* real traversal time, milliseconds */
	/*
	 * anchor: three floats whose meaning is the ACTION's, claimed in the order
	 * the actions were added and never shared. A reader that does not know an
	 * action must not read its anchor.
	 *
	 *   RL_HOOK       NOT a point. anchor[PITCH] and anchor[YAW] are the exact
	 *                 SHORT-quantized view angles used by the proof;
	 *                 anchor[ROLL] is the distance from the handed muzzle to the
	 *                 static-world bite along that ray. Together they reproduce
	 *                 both the shot control and expected bite without an inverse
	 *                 floating-point solve.
	 *   RL_DROP       a world point: the lip the mover steps off, found by
	 *                 ProveDrop and walked to before the fall was rolled.
	 *   RL_RUN        zero for a direct proof, otherwise the world-space
	 *                 detour apex the proved controller walked through.
	 *   RL_LIFT       the world-space bottom ride point that owns the plat's
	 *                 center trigger.
	 *   RL_TELEPORT   the world-space teleporter pad/trigger point.
	 *   RL_DOOR       the exact dry, sweep-clear wait point inside one unique
	 *                 validated repeatable trigger. That trigger may own several
	 *                 independent door teams; `from` begins the proved approach
	 *                 and `to` ends the proved open-pose egress.
	 *   RL_JUMP/RL_SWIM  unused, written as zero.
	 *   RL_ROCKETJUMP    reserved/unsupported in v2; no record is valid.
	 */
	vec3_t	anchor;
} rune_link_t;

typedef struct rune_header_s
{
	int		magic;
	int		version;
	int		num_seeds;
	int		num_links;
	char	mapname[64];
} rune_header_t;

/* Python tooling and sidecar CRCs consume the canonical flat records below,
 * not a compiler-specific approximation of them.  Little-endian alone is not
 * enough: fail the build on an ABI whose scalar widths or padding differ from
 * the v2 disk contract. */
_Static_assert(CHAR_BIT == 8, "rune v2 requires 8-bit bytes");
_Static_assert(sizeof(int) == 4, "rune v2 requires 32-bit int");
_Static_assert(sizeof(float) == 4, "rune v2 requires 32-bit float");
_Static_assert(sizeof(rune_header_t) == 80, "rune_header_t disk layout changed");
_Static_assert(sizeof(rune_seed_t) == 16, "rune_seed_t disk layout changed");
_Static_assert(sizeof(rune_link_t) == 28, "rune_link_t disk layout changed");

/* in-memory form */
typedef struct rune_s
{
	rune_header_t	hdr;
	rune_seed_t		*seeds;
	rune_link_t		*links;
	/* first-link index per seed, built at load; -1 = none */
	int				*first_link;
	int				*next_link;
	byte			*linked_seed; /* owns at least one outgoing link; incoming-only
	                             * dead ends and true orphans are not routes */
} rune_t;

/*
 * sg_oracle.c -- the rocket-jump force and the ceiling it implies.
 *
 * These three live here rather than beside the other oracle calls in
 * sg_local.h for a mechanical reason: sg_local.h includes THIS header first
 * and only then defines sg_phantom_t, so a declaration there cannot be seen
 * by a reader of the rune format who needs to understand RL_ROCKETJUMP. The
 * struct tag is all a pointer declaration needs.
 */
struct sg_phantom_s;
qboolean	SG_OracleRocketJumpAim(vec3_t origin, vec3_t aim,
                                   vec3_t boom_out, float *flight_ms);
int			SG_OracleRocketJumpStep(struct sg_phantom_s *ph, vec3_t boom);
float		SG_OracleRocketJumpCeiling(void);

/* sg_rune.c -- generation and IO */
qboolean	Rune_Generate(const char *mapname);     /* seeds + proves + writes */
rune_t		*Rune_Load(const char *mapname);
void		Rune_Free(rune_t *rune);
void		Rune_DumpVisual(const rune_t *rune, const char *path);  /* html */
qboolean	SG_RunePhysicsCompatible(void);
