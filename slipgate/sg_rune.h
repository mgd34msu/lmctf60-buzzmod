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
} rune_action_t;

/* how the link came to be believed */
typedef enum
{
	RL_PROVEN = 0,      /* the oracle rolled it */
	RL_OBSERVED,        /* a player demonstrated it in play */
	RL_ADJUSTED,        /* proven, but cost corrected by experience */
} rune_provenance_t;

typedef struct rune_seed_s
{
	vec3_t	origin;         /* on the floor, player-standable, proven */
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
	vec3_t	anchor;         /* RL_HOOK only: where the rope bites */
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

/* sg_rune.c -- generation and IO */
qboolean	Rune_Generate(const char *mapname);     /* seeds + proves + writes */
rune_t		*Rune_Load(const char *mapname);
void		Rune_Free(rune_t *rune);
void		Rune_DumpVisual(const rune_t *rune, const char *path);  /* html */
