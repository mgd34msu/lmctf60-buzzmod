

#pragma once

#include "sg_client_ownership.h"


#define SG_MAX_SEEDS	32768
#define SG_FIELD_INF	0x3fffffff


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
	/* [believing team][flag colour]. Home/astray is HUD knowledge and is
	 * mirrored into both rows; where_seed/seen_time are earned by that team's
	 * own eyes. A red sighting must never seed blue's recovery field. */
	sg_belief_flag_t	flag[2][2];
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

struct sg_belief_runtime_provider_s;
struct sg_belief_runtime_view_s;

/* Compact belief registration is a level-owner boundary. The caller supplies
 * an accepted compact model plus its compact-cell locator; CACO never derives
 * a compact cell from a legacy seed. */
int SG_CacoCompactBeliefProviderSet(
	const struct sg_belief_runtime_provider_s *provider);
int SG_CacoCompactBeliefActive(void);
const struct sg_belief_runtime_view_s *SG_CacoCompactBeliefViewForClient(
	uint8_t audience_team, uint32_t client_id);

void Caco_Reset(void);
void Caco_ResetClient(edict_t *client);


#define SG_DMG_RING			4
#define SG_DMG_CLIENTS		256

typedef struct
{
	qboolean	landed;
	int			attacker;       /* actionable client; -1 empty or retired */
	int			mod;            /* means of death, MOD_FRIENDLY_FIRE masked off */
	int			damage;
	vec3_t		from;           /* unit vector: the victim's eye toward
	                             * whatever the harm arrived from */
	float		time;
	qboolean	unseen;         /* no sight line to the attacker when it landed */
} sg_damage_hit_t;

extern sg_damage_hit_t sg_caco_damage[SG_DMG_CLIENTS][SG_DMG_RING];

/* T_Damage records the hit; attacker_ctfid gates shooter identity. */
void SG_NoteDamage(edict_t *victim, edict_t *attacker,
	uint64_t attacker_ctfid, int damage, int mod, vec3_t dir);

/* the newest hit from a shooter this bot could NOT see, if one landed within
 * `window` seconds. Fills a unit vector pointing back down the incoming line.
 * False leaves out_from untouched. */
qboolean SG_RecentUnseenHit(edict_t *self, float window, vec3_t out_from);

/* has anything landed on this body since `since`? Reads the same damage
 * ring: the spawn beat and the early-return errand both need "did the
 * world just object" and neither needs a sense of its own. */
qboolean SG_HurtSince(edict_t *e, float since);


#define SG_RAIL_RELOAD	1.6f

/*
 * How much of the reload a crossing is allowed to spend. Half a second of
 * the 1.6 is left on the table on purpose: the belief this is measured
 * against is a position the bot was told about, not a position it can see,
 * and the crossing takes real time to finish. A window that ran to the last
 * tenth would be arithmetic, not caution.
 */
#define SG_RAIL_WINDOW	1.1f

/* How long a man stays "a railer" after his last slug. Weapons are picked up
 * and put down; twenty seconds is about how long a player's reputation for
 * holding a lane survives without evidence. */
#define SG_RAIL_MEMORY	20.0f

/*
 * The refractory between waits, and the whole reason the cap is a cap. A
 * wait ends and the body has not moved, so the geometry that armed it is
 * identical and it would arm again on the very next frame -- a bot pinned
 * in a doorway for as long as the sighting lasts. Four seconds is a little
 * over two reloads: long enough that any lane gets crossed, short enough
 * that a second genuine lane later in the leg still gets timed.
 */
#define SG_RAIL_HOLD_GAP	4.0f

/*
 * When each enemy client was last heard or felt firing a rail, per team.
 * Per team for the same reason sg_caco_enemies is (red hearing a shot must
 * not tell blue anything), and sized to the damage ring's ceiling because
 * this is the same kind of table for the same reason: neither file owns the
 * bot body and neither can add a field to it. 0 means "never, as far as this
 * side knows".
 */
extern float sg_caco_railshot[2][SG_DMG_CLIENTS];
extern float sg_caco_hastefire[2][SG_DMG_CLIENTS];

/* the one reader of the cvar: default 0 leaves every path below dead and the
 * build byte-identical */
qboolean	SG_RailRhythm(void);

/* the tap, called from weapon_railgun_fire (p_weapon.c) once the slug is away
 * and the flash and trail are on the wire. Every test about who could have
 * perceived it is on this side. */
void		SG_NoteRailShot(edict_t *shooter);

/* the freshest belief about an enemy this team has heard fire a rail inside
 * SG_RAIL_MEMORY, provided the sighting itself is younger than `fresh`.
 * False leaves the outputs untouched. */
qboolean	SG_RailThreat(int team, float fresh, int *out_client,
		             int *out_seed);

/* is that enemy believed EMPTY right now -- his last heard shot inside the
 * window? False covers both "he fired too long ago" and "we have never heard
 * him fire", which is the cautious reading of an unknown gun. */
qboolean	SG_RailCold(int team, int client);


/* ------------------------------------------------------------- bots */

typedef enum
{
	SG_ROLE_ATTACK = 0, SG_ROLE_DEFEND, SG_ROLE_CARRY,
	SG_ROLE_RECOVER, SG_ROLE_ESCORT,
	SG_ROLES
} sg_role_t;
enum
{
	SG_FC_WEAPON = 0, SG_FC_ARMOR, SG_FC_AMMO,
	SG_FC_HEALTH, SG_FC_RUNE, SG_FC_POWERUP,
	SG_FIELD_CLASSES
};
typedef struct
{
	float	objective;
	float	item[SG_FIELD_CLASSES];
	float	carrier_support;
	float	intercept;
} sg_weights_t;
void	SG_HumanSpeedClientThinkBegin(edict_t *ent);
void	SG_HumanSpeedPmoveBegin(edict_t *ent, pmove_state_t *pmove,
	unsigned command_msec);
void	SG_HumanSpeedPmoveEnd(edict_t *ent, const pmove_state_t *pmove,
	unsigned command_msec);
void		SG_RosterStorageReset(void);
void		Botfill_Frame(void);
qboolean	SG_RetireBotForClient(edict_t *ent);
void		SG_DisownBot(edict_t *ent);
qboolean	SG_AddBot(void);
qboolean	SG_AddBotTeam(int teamnum);
int			SG_RemoveBots(void);
void		SG_ListBots(void);                  /* slot/name/team/score/skill/role/seed */
qboolean	SG_RemoveBotNamed(const char *who); /* netname ([SG] optional) or slot */
qboolean	SG_KickWorst(void);                 /* lowest score, either team */
void		SG_WeightsPrint(void);              /* live table and each entry's source */
void		SG_WeightsReload(void);             /* re-read the global weights file and
                                             * this map's playbook, in that order */
void		SG_RunFrame(void);      /* drive all SLIPGATE bots, once per frame */
qboolean	SG_LevelSetup(void);    /* publish this level's staged RUNE */
void		SG_LevelSetupAfterRuneWrite(void); /* load a written RUNE when none is active */
void		Botfill_Reset(void);    /* clear level-time cadence and hysteresis */
void		SG_LevelChange(void);   /* forget level-tagged rune and fields */
void		SG_RuneLevelStorageWillFree(void); /* the level storage is about to go */

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

	/* Per-team lease for one early-return claimant. The holder refreshes it
	 * while active; expiry releases abandoned errands. */
	float		claimed_until;
	int			claimed_by;
} sg_belief_item_t;

/*
 * Each team has an independent dynamic item belief. Static pad geometry is
 * shared map knowledge; availability and respawn timing are not.
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
/* A rejected physical touch retires only the matching taker's exact item
 * commitment. It carries no belief or communication authority. */
void		SG_NoteItemRejected(edict_t *taker, edict_t *item);

/* the calls sg_fields.c needs to stop reading item entities directly */
qboolean	Caco_ItemBelievedUp(edict_t *e);
qboolean	Caco_ItemBelievedUpFor(int team, edict_t *e);
qboolean	Caco_ItemBelievedRouteableFor(int team, edict_t *e);
qboolean	Caco_ItemBelievedRouteable(edict_t *e);
unsigned	Caco_ItemBeliefSig(void);   /* mix into the class rebuild test */

/* the mega entity cache (sg_caco.c): entity numbers for every
 * item_health_mega on the map, found once at level setup instead of on
 * every call. Returns the count and points *out_ents at the array; the
 * caller still applies its own inuse/belief filtering per entity, exactly
 * as it did when it found them by walking globals.num_edicts itself. */
int			SG_MegaEntities(const int **out_ents);

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
