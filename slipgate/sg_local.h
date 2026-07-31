/*
 * sg_local.h -- SLIPGATE internals.
 *
 *     SLIPGATE = RUNE + ARACHNOTRON + CACO
 *
 * RUNE        what the map affords: a link graph over phase space, every
 *             link proven by rolling the real physics before it was written
 * ARACHNOTRON the brain with legs: fields over the rune, a value surface
 *             per bot per moment, a body descending it at physics rate
 * CACO        the eye: belief instead of omniscience, sightings aging into
 *             uncertainty, learning into the rune and the weights
 *
 * See slipgate/SLIPGATE.md for the constitution. Principles that bind every
 * file here: physics is read or simulated, never assumed; simulated time
 * sums to real time; facts are measured, only preferences are fitted; every
 * claim is A/B-able against the legacy bots in the same harness.
 */

#pragma once

#include "sg_rune.h"

#define SG_MAX_SEEDS	32768
#define SG_FIELD_INF	0x3fffffff

/* ------------------------------------------------------------------ oracle */

/*
 * A phantom: player-shaped movement state that belongs to no client. The
 * oracle rolls these through gi.Pmove against the live world. pms is
 * authoritative (it is what Pmove reads and writes); origin/velocity are the
 * float decode of it, refreshed after every step.
 */
typedef struct sg_phantom_s
{
	pmove_state_t	pms;
	vec3_t			origin;
	vec3_t			velocity;
	qboolean		groundentity;
	int				waterlevel;
} sg_phantom_t;

void SG_OraclePlace(sg_phantom_t *ph, vec3_t origin);
void SG_OracleRun(sg_phantom_t *ph, usercmd_t *cmd, int steps);
void SG_OracleHookStep(sg_phantom_t *ph, vec3_t anchor);

/* -------------------------------------------------------------------- caco */

#define SG_BELIEF_STALE		8.0f    /* seconds before a sighting stops counting */

typedef enum { SG_FLAG_HOME = 0, SG_FLAG_ASTRAY } sg_flagstate_t;

typedef struct
{
	sg_flagstate_t	state;          /* HUD-level: home or not */
	int				where_seed;     /* last SEEN position, -1 unknown */
	float			seen_time;
} sg_belief_flag_t;

typedef struct
{
	int		client;                 /* -1 none */
	int		seed;                   /* last seen position, -1 unknown */
	float	seen_time;
} sg_belief_carrier_t;

typedef struct
{
	sg_belief_flag_t	flag[2];            /* [0] red flag, [1] blue flag */
	sg_belief_carrier_t	carrier[2];         /* our carrier, per team-1 */
	sg_belief_carrier_t	enemy_carrier[2];   /* who has team N+1's flag */
} sg_team_belief_t;

extern sg_team_belief_t sg_caco_team_belief;

void Caco_See(rune_t *r, edict_t *viewer);      /* one bot's eyes, per frame */
void Caco_Frame(rune_t *r);                     /* shared HUD scan + aging */
void Caco_Reset(void);

/* ------------------------------------------------------------------ fields */

enum
{
	SG_FC_WEAPON = 0, SG_FC_ARMOR, SG_FC_AMMO,
	SG_FC_HEALTH, SG_FC_RUNE, SG_FC_POWERUP,
	SG_FIELD_CLASSES
};

typedef struct
{
	int		red_flag_seed, blue_flag_seed;

	int		*to_red_flag, *to_blue_flag;        /* stands (capture points) */
	int		*to_red_flag_now, *to_blue_flag_now;/* where the flag IS, per belief */
	int		*item[SG_FIELD_CLASSES];
	unsigned item_sig[SG_FIELD_CLASSES];
	int		*our_carrier[2];                    /* support field, per team-1 */

	float	next_refresh;
} sg_fields_t;

extern sg_fields_t sg_fields;

qboolean	Fields_Setup(rune_t *r);
void		Fields_Refresh(rune_t *r);
void		Field_Flood(rune_t *r, int *dist,
		            const int *sources, const int *source_cost, int n);

/* ------------------------------------------------------------- arachnotron */

/* roles, per the owner's specification: 2-in-5 defend, rest attack,
 * carrier is a role of its own that also counts toward defence */
typedef enum { SG_ROLE_ATTACK = 0, SG_ROLE_DEFEND, SG_ROLE_CARRY, SG_ROLES } sg_role_t;

/*
 * The composition weights: how much each concern matters to each role.
 * These are THE fitted component of SLIPGATE -- everything else here is
 * measured fact. Objective scales the role's principal field; item weights
 * are the worth used by the detour arithmetic; support and intercept scale
 * the dynamic fields when belief supplies them.
 */
typedef struct
{
	float	objective;
	float	item[SG_FIELD_CLASSES];
	float	carrier_support;
	float	intercept;
} sg_weights_t;

qboolean	SG_OwnsBot(edict_t *ent);
qboolean	SG_AddBot(void);
int			SG_RemoveBots(void);
void		SG_RunFrame(void);      /* drive all SLIPGATE bots, once per frame */
void		SG_LevelChange(void);   /* forget level-tagged rune and fields */

rune_t		*Rune_Load(const char *mapname);
int			Rune_NearestSeed(rune_t *r, vec3_t p);
