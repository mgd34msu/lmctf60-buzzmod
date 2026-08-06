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
void SG_TeachFutility(int seed);
void SG_TeachLinkFutility(int link);
void SG_NoteDeath(edict_t *victim);
extern vec3_t sg_caco_death_org[2];
extern float sg_caco_death_time[2];
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

/*
 * General enemy sightings, per team: any enemy a teammate has SEEN, not
 * only carriers. A rune carrier glows (RF_GLOW, p_view.c:792-794) so a
 * sighting also knows THAT a rune is in enemy hands -- though never which
 * one, because the glow is generic.
 */
#define SG_MAX_ENEMY_TRACK	8
typedef struct
{
	int			client;         /* -1 empty */
	int			seed;
	float		seen_time;
	qboolean	runed;          /* glowed when last seen */
	qboolean	heard_only;     /* placed by ear, not eye: good enough to
	                             * warn a post, never good enough to aim */
} sg_belief_enemy_t;

extern sg_belief_enemy_t sg_caco_enemies[2][SG_MAX_ENEMY_TRACK];
extern float sg_caco_quadheard[2];  /* last enemy-quad sound heard, per team */

/* the D4 inference: Damage rune off its pad, not in our hands, and a
 * glowing enemy on record -- the glow never names the rune, the pad does */
qboolean	Caco_EnemyHasDamageRune(int team);

extern sg_team_belief_t sg_caco_team_belief;

void Caco_See(rune_t *r, edict_t *viewer);      /* one bot's eyes, per frame */
void Caco_HumanEyes(rune_t *r, int team);       /* what human teammates see */
void Caco_Frame(rune_t *r);                     /* shared HUD scan + aging */
void Caco_Reset(void);

/*
 * The damage ring: the third sense, after the eye and the ear.
 *
 * A bot railed from behind gets nothing from either of the other two. The
 * eye needs a sight line it does not have. The ear (Caco_ScanEnemies, the
 * PHS test) needs the shooter to be SAMPLED mid-WEAPON_FIRING, and a
 * hitscan weapon is back to WEAPON_READY long before the next scan frame
 * comes round -- the shot is over before anyone looks. The hit itself is
 * the only evidence that ever arrives, so T_Damage hands it here, where
 * the rest of what a bot BELIEVES lives, rather than to the combat file
 * that only deals in what a bot can see.
 *
 * Four entries is the whole ring. This exists to answer one question --
 * "am I under fire, and roughly from where" -- and a fifth simultaneous
 * attacker does not change that answer. The table is per client and sized
 * to a fixed ceiling, bounds-checked, exactly the way sg_combat.c sizes
 * its own per-client state and for the same reason: this file does not own
 * the bot body and cannot add a field to it.
 */
#define SG_DMG_RING			4
#define SG_DMG_CLIENTS		256

typedef struct
{
	int			attacker;       /* client number, -1 empty */
	int			mod;            /* means of death, MOD_FRIENDLY_FIRE masked off */
	int			damage;
	vec3_t		from;           /* unit vector: the victim's eye toward
	                             * whatever the harm arrived from */
	float		time;
	qboolean	unseen;         /* no sight line to the attacker when it landed */
} sg_damage_hit_t;

extern sg_damage_hit_t sg_caco_damage[SG_DMG_CLIENTS][SG_DMG_RING];

/* the T_Damage hand-off (g_combat.c, beside SG_CombatHit): every landed hit
 * on a bot, shooter seen or not */
void SG_NoteDamage(edict_t *victim, edict_t *attacker, int damage, int mod,
                   vec3_t dir);

/* the newest hit from a shooter this bot could NOT see, if one landed within
 * `window` seconds. Fills a unit vector pointing back down the incoming line.
 * False leaves out_from untouched. */
qboolean SG_RecentUnseenHit(edict_t *self, float window, vec3_t out_from);

/* ------------------------------------------------------------------ fields */

enum
{
	SG_FC_WEAPON = 0, SG_FC_ARMOR, SG_FC_AMMO,
	SG_FC_HEALTH, SG_FC_RUNE, SG_FC_POWERUP,
	SG_FIELD_CLASSES
};

/*
 * Per-item fields: a class field gives cost to the NEAREST item of the class,
 * which is all the detour arithmetic needs when the items are interchangeable.
 * For the classes where identity decides the worth -- powerups and runes, a
 * handful of entities each -- one field per item is kept as well, so the
 * detour triangle can be evaluated exactly against THAT item's position.
 */
#define SG_MAX_PER_ITEM		8

typedef struct
{
	int		red_flag_seed, blue_flag_seed;

	int		*to_red_flag, *to_blue_flag;        /* stands (capture points) */
	int		*to_red_flag_now, *to_blue_flag_now;/* where the flag IS, per belief */
	int		*item[SG_FIELD_CLASSES];
	unsigned item_sig[SG_FIELD_CLASSES];
	int		*our_carrier[2];                    /* support field, per team-1 */
	int		*to_post[2];                        /* learned defensive post (.dpo) */
	int		*to_lane[2];                        /* rail-lane post: sees the most
	                                             * approach corridor (computed) */
	int		*to_icept[2];                       /* learned steal-intercept (.dpo) */

	float	next_refresh;

	/* appended: per-item detour fields, only for the per-item classes */
	int		*per_item[SG_FIELD_CLASSES][SG_MAX_PER_ITEM];
	int		per_item_seed[SG_FIELD_CLASSES][SG_MAX_PER_ITEM];
	int		per_item_ent[SG_FIELD_CLASSES][SG_MAX_PER_ITEM];
	int		per_item_count[SG_FIELD_CLASSES];

	/* appended: has the carrier support field ever been flooded this level? */
	qboolean our_carrier_valid[2];
} sg_fields_t;

extern sg_fields_t sg_fields;

qboolean	Fields_Setup(rune_t *r);
void		Fields_Refresh(rune_t *r);
void		Field_Flood(rune_t *r, int *dist,
		            const int *sources, const int *source_cost, int n);

/* ------------------------------------------------------------- arachnotron */

/*
 * Roles, per the owner's specification: 2-in-5 defend, rest attack, carrier
 * is a role of its own that also counts toward defence. RECOVER and ESCORT
 * are appended (the values of the first three are load-bearing for the weight
 * table's row order): attackers become recoverers while our own flag is
 * astray, and one attacker escorts a live carrier of ours.
 */
typedef enum
{
	SG_ROLE_ATTACK = 0, SG_ROLE_DEFEND, SG_ROLE_CARRY,
	SG_ROLE_RECOVER, SG_ROLE_ESCORT,
	SG_ROLES
} sg_role_t;

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
qboolean	SG_AddBotTeam(int teamnum);
int			SG_RemoveBots(void);
/* the admin surface behind `sv sg` and the referee's Manage Bots menu */
void		SG_ListBots(void);                  /* slot/name/team/score/skill/role/seed */
qboolean	SG_RemoveBotNamed(const char *who); /* netname ([SG] optional) or slot */
qboolean	SG_KickWorst(void);                 /* lowest score, either team */
void		SG_WeightsPrint(void);              /* live table and each entry's source */
void		SG_WeightsReload(void);             /* re-read the global weights file and
                                             * this map's playbook, in that order */
/*
 * Post-death caution (sg_tilt): a factor on how far out this bot is willing
 * to START a fight, applied on top of whatever the persona already decided.
 * 1.0 for anyone who is not a SLIPGATE bot inside its own window -- humans,
 * legacy bots, and every bot that has not died recently. Route and
 * willingness only: tilt never touches aim, reaction or the trigger.
 */
float		SG_TiltCaution(edict_t *ent);
void		SG_RunFrame(void);      /* drive all SLIPGATE bots, once per frame */
void		SG_LevelChange(void);   /* forget level-tagged rune and fields */

rune_t		*Rune_Load(const char *mapname);
int			Rune_NearestSeed(rune_t *r, vec3_t p);
rune_t		*SG_Rune(void);         /* the loaded rune, NULL before setup --
                                     * combat reads reachability from it */

/* ------------------------------------------------ caco: powerup and rune
 *
 * WHERE a powerup spawns is map knowledge -- everybody who has played the
 * map knows the quad pad. Whether it is standing there RIGHT NOW is not:
 * that is known if somebody has looked at the pad since it last respawned,
 * or worked out from the clock by somebody who watched it get taken.
 *
 * Runes are a different animal in LMCTF and get a different model; see the
 * commentary over the belief code in sg_caco.c.
 */

#define SG_MAX_BELIEF_ITEMS	32

enum
{
	SG_BI_POWERUP = 0,      /* item_quad, item_invulnerability */
	SG_BI_RUNE              /* damage/haste/resist/regen/vampire_rune */
};

typedef struct
{
	vec3_t		org;                    /* where it is believed to be */
	int			seed;                   /* nearest seed to org, -1 unknown */
	float		seen_up_time;           /* last time it was SEEN standing */
	float		believed_respawn_time;  /* when the clock says it is back */
	qboolean	believed_up;

	/* bookkeeping behind the belief, not part of it */
	int			ent;                    /* edict index of the item entity */
	int			cls;                    /* SG_BI_* */
	float		respawn_delay;          /* seconds; 0 = no clock to infer from */

	/*
	 * THE ERRAND LEASE (sg_itemlead, owner's ruling 2026-08-05: "bots with an
	 * armed team clock should get back early like humans"). Exactly one bot
	 * per team per pad may leave early for it -- five bots standing on one
	 * pedestal is not anticipation, it is a queue -- and the claim lives here
	 * because the row is ALREADY per team, so a red errand cannot reserve the
	 * pad against blue and neither team can read the other's plan.
	 *
	 * It is a LEASE and not a lock: the holder re-stamps claimed_until every
	 * frame it is still on the errand, so a bot that dies, changes role or
	 * simply stops asking releases the pad within a second without anybody
	 * having to remember to unlock it. claimed_by is a CLIENT index so the
	 * holder can re-take its own lease; -1 is nobody.
	 */
	float		claimed_until;
	int			claimed_by;
} sg_belief_item_t;

/*
 * ONE TABLE PER TEAM (sg_itemcomm, owner's ruling 2026-08-05).
 *
 * It used to be one table for the server, which meant red looking at the quad
 * pad taught blue that the quad was gone. sg_caco_enemies has been per team
 * since it was written for exactly that reason; the item table was the last
 * shared belief left, and it was the one carrying a RESPAWN CLOCK -- so the
 * leak was not merely "they know it is empty", it was "they know to the second
 * when it is back".
 *
 * Row [team-1] is what that team believes. The two rows hold the same STATIC
 * geometry -- ent, cls, org and respawn_delay for a powerup pad are map
 * knowledge every player on both sides has -- and differ only in the dynamic
 * part: believed_up, seen_up_time, believed_respawn_time, and a rune's seed.
 * Index i names the same entity in both rows, so a caller that has an index
 * from one row may use it on the other.
 *
 * With sg_itemcomm 0 both rows are written identically at every site, so the
 * split is invisible and the build behaves exactly as it did before it.
 */
extern sg_belief_item_t	sg_caco_items[2][SG_MAX_BELIEF_ITEMS];
extern int				sg_caco_num_items;

/* the one reader of the cvar: item belief is per team AND respawn clocks are
 * earned by a spoken callout rather than by scanning */
qboolean	SG_ItemComm(void);

/*
 * The pickup hand-off. Touch_Item (g_items.c) calls this for every successful
 * pickup by anybody, bot or human, and every decision about whether it matters
 * is made on this side. It is what arms a respawn clock now -- by way of a
 * bot's mouth, never directly; the war story is over its definition in
 * sg_caco.c and over the majors table in sg_chat.c.
 */
void		SG_NoteItemTaken(edict_t *taker, edict_t *item);

/* the calls sg_fields.c needs to stop reading item entities directly */
qboolean	Caco_ItemBelievedUp(edict_t *e);
qboolean	Caco_ItemBelievedUpFor(int team, edict_t *e);
int			Caco_ItemBeliefSeed(rune_t *r, edict_t *e);
unsigned	Caco_ItemBeliefSig(void);   /* mix into the class rebuild test */

/* ---------------------------------------------- caco: carrier projection
 *
 * An aged belief about an enemy carrier is not a point, it is a set: every
 * seed he could plausibly have reached since we last saw him, advanced once
 * a second down his own route-home field. sg_caco_proj[i] is the set for the
 * carrier holding team i+1's flag; sg_caco_team_belief.enemy_carrier[i].seed
 * is its deepest member.
 */

#define SG_PROJ_MAX		32      /* plausible positions kept per carrier */
#define SG_PROJ_BRANCH	3       /* the best step plus two alternatives */

typedef struct
{
	int		seed[SG_PROJ_MAX];  /* ordered: [0] is deepest along their route */
	int		n;
	int		client;             /* who the set is about, -1 = idle */
	float	from_time;          /* the sighting it was last collapsed to */
} sg_proj_t;

extern sg_proj_t sg_caco_proj[2];
unsigned char *SG_DefPlane(int post, int team1);
