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

#define RUNE_MAGIC      0x454E5552      /* "RUNE" */
#define RUNE_VERSION    1

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
	 * a rune written before these existed still reads correctly and
	 * RUNE_VERSION stays 1. These are actions the mover cannot prove by
	 * rolling physics -- the world moves it, not the other way round.
	 */
	RL_LIFT,            /* ride a func_plat from its bottom to its top */
	RL_TELEPORT,        /* step on a misc_teleporter; the game does the rest */
	/*
	 * Appended under the same rule again -- RL_LIFT is still 5 and
	 * RL_TELEPORT is still 6, so every rune ever written still reads and
	 * RUNE_VERSION stays 1.
	 *
	 * A rocket jump is proven like a hook and priced like nothing else in the
	 * graph: it is the only action whose traversal costs the mover HEALTH.
	 * The prover rolls it with the real physics -- pmove's own jump, then the
	 * splash of the mover's own rocket applied exactly as T_RadiusDamage and
	 * T_Damage apply it -- so the link is PROVEN like any other; what makes it
	 * special is that the runtime must weigh the health in the anchor field
	 * before it spends the link. See the anchor commentary below.
	 */
	RL_ROCKETJUMP,      /* rocket at the feet: the splash lifts the jumper */
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
	 * reads correctly and RUNE_VERSION stays 1. RL_DECLARED marks a link that
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
	 *   RL_HOOK       a world point: where the rope bites.
	 *   RL_DROP       a world point: the lip the mover steps off, found by
	 *                 ProveDrop and walked to before the fall was rolled.
	 *   RL_ROCKETJUMP NOT a point. The link format has no field for the price
	 *                 of a traversal in health and gets no new one (the file
	 *                 stays version 1), so the rocket jump keeps its two
	 *                 numbers here:
	 *
	 *                   anchor[0], anchor[1]  the horizontal part of the UNIT
	 *                       aim vector the proof fired on. It points AWAY from
	 *                       the destination -- the shot goes down and BEHIND,
	 *                       which is what throws the mover forward. The
	 *                       vertical part is not stored because it is not
	 *                       free: the vector is a unit vector and it points
	 *                       down, so aim[2] = -sqrt(1 - x*x - y*y). Straight
	 *                       down is (0,0), and the body may then use any yaw.
	 *                   anchor[2]  the health the jumper pays, as the proof
	 *                       measured it: (int)points from T_RadiusDamage with
	 *                       NO armour, no power armour and no Resist rune.
	 *                       Every one of those only reduces it, so this is the
	 *                       worst case and the runtime may treat it as a
	 *                       ceiling on the price, never a floor.
	 *
	 *   everything else  unused, written as zero.
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

/* in-memory form */
typedef struct rune_s
{
	rune_header_t	hdr;
	rune_seed_t		*seeds;
	rune_link_t		*links;
	/* first-link index per seed, built at load; -1 = none */
	int				*first_link;
	int				*next_link;
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
