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
 * claim is directly comparable with the compatibility bots in the same harness.
 */

#pragma once

#include "sg_rune.h"
#include "sg_door_approach.h"

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
	pmove_state_t	old_pms;       /* production snapinitial comparison state */
	vec3_t			origin;
	vec3_t			velocity;
	vec3_t			mins;          /* exact player hull published by Pmove */
	vec3_t			maxs;
	qboolean		groundentity;
	edict_t			*groundentity_entity; /* exact Pmove support identity for
		                                      * mechanism approach admission */
	int				watertype;
	int				waterlevel;
	/* Doors are made nonsolid while a rune is generated. A proof may enter a
	 * door's swept volume only after this exact phantom has touched a validated,
	 * repeatable player activator. Keeping that evidence in the phantom makes it
	 * survive the many one-command SG_OracleRunWorld calls in a rollout without
	 * leaking between candidate links. */
#define SG_PHANTOM_ARMED_DOORS 16
	short			armed_door[SG_PHANTOM_ARMED_DOORS];
	byte			armed_door_count;
	qboolean		door_arm_overflow;
	qboolean		door_passed;
	int				door_wait_ms;
	int				door_open_ms;
} sg_phantom_t;

/* One side-effect-free prediction for the next real DIRECT_TRIGGER_DOOR
 * ClientThink.  The live adapter separately authenticates the support
 * incarnation before arming callback authority. */
typedef struct sg_door_approach_prediction_s
{
	sg_door_approach_state_t state;
	pmove_state_t pms;
	vec3_t mins;
	vec3_t maxs;
	edict_t *groundentity;
	int watertype;
	int waterlevel;
	qboolean expected_touch;
} sg_door_approach_prediction_t;

typedef struct sg_hook_proof_s
{
	int		pull_ms;
	int		release_ms;       /* first 25 ms boundary satisfying release */
	int		settle_arrival_ms;/* first 25 ms boundary inside destination */
	int		settle_ms;
	byte	exit_speed;
	pmove_state_t attach_pms; /* state after quantized outbound zero commands */
	qboolean attach_groundentity;
	int		attach_watertype;
	int		attach_waterlevel;
} sg_hook_proof_t;

typedef struct sg_swim_proof_s
{
	int		arrival_ms;      /* shared 100 ms completion boundary */
	byte	exit_speed;
} sg_swim_proof_t;

/* Dormant PREOPEN D_SWIM witness.  Touch and its containing frame end are
 * source-relative; TOP is mover-schedule-relative; arrival and sweep clearance
 * are suffix-relative.  The final TOP mover pass and first suffix frame are
 * the same 100 ms, so total cost uses suffix_start_ms rather than mover_top_ms. */
typedef struct sg_compound_swim_proof_s
{
	int		touch_ms;
	int		touch_frame_end_ms; /* ceil(touch_ms / 100) * 100 */
	int		mover_top_ms;    /* raw Stage1 TOP tag (80@200 is 500) */
	int		suffix_start_ms; /* TOP-frame start (raw tag minus 100) */
	int		arrival_ms;
	int		sweep_clear_ms;
	int		total_cost_ms;   /* frame end + suffix start + arrival */
	byte	exit_speed;
	pmove_state_t suffix_pms;
	pmove_state_t suffix_old_pms;
	vec3_t		suffix_origin;
	vec3_t		suffix_velocity;
	qboolean	suffix_groundentity;
	int		suffix_watertype;
	int		suffix_waterlevel;
	float		suffix_old_frame_z;
} sg_compound_swim_proof_t;

/* Live-only escape witness from an authoritative body state already inside a
 * PREOPEN member's complete sweep while that exact member is held at TOP.
 * Both times are relative to the first recovery command. */
typedef struct sg_compound_swim_recovery_proof_s
{
	int		arrival_ms;
	int		sweep_clear_ms;
	byte		exit_speed;
} sg_compound_swim_recovery_proof_t;

/* Canonical source state owned by the compound oracle.  Keep the complete
 * phantom, and in particular pms and old_pms as distinct values: snapinitial
 * is part of the next real Pmove even when a freshly prepared offline source
 * happens to make those states equal. */
typedef struct sg_compound_swim_source_s
{
	sg_phantom_t	phantom;
	float		old_frame_z;
} sg_compound_swim_source_t;

struct sg_compound_world_preopen_s;

void SG_OraclePlace(sg_phantom_t *ph, vec3_t origin);
/* Convert a collision-trace floor endpoint into the same signed-eighth-unit,
 * clear, grounded state that live Pmove accepts.  Ongoing rest stability is
 * deliberately a separate action-source property: slick/current points remain
 * valid ordinary-navigation graph points. */
qboolean SG_OracleCanonicalGroundSource(const vec3_t floor_endpoint,
	vec3_t canonical_origin);
void SG_TeachFutility(int seed);
void SG_TeachLinkFutility(int link);
void SG_NoteDeath(edict_t *victim);
qboolean SG_EnemyRoomDeathKnown(int team, const vec3_t stand_origin,
	float max_age, float max_distance);
void SG_OracleRun(sg_phantom_t *ph, usercmd_t *cmd, int steps);
qboolean SG_OracleRunWorld(sg_phantom_t *ph, usercmd_t *cmd, int steps);
/* Phase-independent exclusion for topology and exposure traces.  It models
 * the full swept volume of canonical func_rotating brushes, not their
 * instantaneous phase.  It deliberately does not make Pmove/action proofs
 * phase-independent. */
qboolean SG_OracleRotatorSweepBlocks(const vec3_t start,
	const vec3_t hull_mins, const vec3_t hull_maxs, const vec3_t end,
	int contentmask);
int SG_OracleHookStep(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t view_angles, int hand);
qboolean SG_OracleHookTraverse(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only);
qboolean SG_OracleHookFlightClear(const vec3_t muzzle, const vec3_t bite);
qboolean SG_OracleSwimTraverse(sg_phantom_t *ph, const vec3_t destination,
	qboolean destination_water, float old_frame_z, sg_swim_proof_t *proof,
	edict_t *passent, qboolean world_only);
rune_reject_reason_t SG_OracleCompoundSwimPrepareSource(
	const vec3_t source,
	const struct sg_compound_world_preopen_s *resolved,
	float old_frame_z, sg_compound_swim_source_t *prepared,
	edict_t *passent, qboolean world_only, qboolean loader_replay);
rune_reject_reason_t SG_OracleCompoundSwimDiscoverContact(
	const sg_compound_swim_source_t *prepared,
	const struct sg_compound_world_preopen_s *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	edict_t *passent, qboolean world_only, qboolean loader_replay);
rune_reject_reason_t SG_OracleCompoundSwimPreopen(sg_phantom_t *ph,
	const struct sg_compound_world_preopen_s *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, edict_t *passent,
	qboolean world_only, qboolean loader_replay);
/* Re-prove a bounded SWIM from the exact state the live client's next Pmove
 * would consume.  The caller owns rune/physics authority and must renew the
 * TOP lease before calling; this observation-only oracle never touches the
 * member timer.  Recovery starts inside the sweep.  A caller already outside
 * is safe to release and is deliberately rejected here. */
rune_reject_reason_t SG_OracleCompoundSwimRecover(sg_phantom_t *ph,
	const struct sg_compound_world_preopen_s *resolved,
	const vec3_t destination, qboolean destination_water,
	float old_frame_z, sg_compound_swim_recovery_proof_t *proof,
	edict_t *passent);
/* Re-prove the nominal suffix from the exact live TOP checkpoint.  Unlike
 * Recover, Continue requires the subject to begin outside the complete mover
 * sweep; both paths keep the member at canonical TOP throughout replay. */
rune_reject_reason_t SG_OracleCompoundSwimContinue(sg_phantom_t *ph,
	const struct sg_compound_world_preopen_s *resolved,
	const vec3_t destination, qboolean destination_water,
	float old_frame_z, sg_compound_swim_recovery_proof_t *proof,
	edict_t *passent);
qboolean SG_OracleTeleportSwimApproach(sg_phantom_t *ph,
	const vec3_t anchor, edict_t *pad, float old_frame_z,
	sg_swim_proof_t *proof, edict_t *passent, qboolean world_only);
qboolean SG_OracleDeclaredApproach(const vec3_t source, const vec3_t target,
	edict_t *expected, int action, int *arrival_ms);
qboolean SG_OracleDeclaredEgress(const vec3_t source, const vec3_t target,
	edict_t *support, int *arrival_ms);
/* Exact repeatable-door declaration. The resolver accepts only a unique,
 * safe trigger whose anchor/source still match the generated contract. One
 * trigger may own several independent door teams; generation temporarily
 * links the whole set at STATE_TOP before replaying the live direct egress. */
edict_t *SG_DeclaredDoorForLink(const vec3_t anchor, const vec3_t source);
qboolean SG_DeclaredDoorActivatorSafe(edict_t *trigger);
qboolean SG_DeclaredDoorDirectActivatorSafe(edict_t *trigger);
qboolean SG_DeclaredButtonDoorSafe(edict_t *button);
qboolean SG_OracleStablePopulationTrace(const vec3_t start,
	const vec3_t mins, const vec3_t maxs, const vec3_t end,
	edict_t *passent, qboolean population_independent, trace_t *trace_out);
qboolean SG_OracleButtonCarryClear(edict_t *button, const vec3_t from,
	const vec3_t to, qboolean population_independent);
typedef enum sg_button_support_mode_e
{
	SG_BUTTON_SUPPORT_NONE = 0,
	SG_BUTTON_SUPPORT_STATIC,
	SG_BUTTON_SUPPORT_RIDER
} sg_button_support_mode_t;
typedef enum sg_button_contact_status_e
{
	SG_BUTTON_CONTACT_OK = 0,
	SG_BUTTON_CONTACT_UNSAFE,
	SG_BUTTON_CONTACT_SWEEP_OCCUPIED,
	SG_BUTTON_CONTACT_BAD_ORIGIN,
	SG_BUTTON_CONTACT_DEGENERATE,
	SG_BUTTON_CONTACT_STARTSOLID,
	SG_BUTTON_CONTACT_ALLSOLID,
	SG_BUTTON_CONTACT_NO_HIT,
	SG_BUTTON_CONTACT_WRONG_HIT,
	SG_BUTTON_CONTACT_STATUS_COUNT
} sg_button_contact_status_t;
sg_button_contact_status_t SG_DeclaredButtonDoorContactStatus(
	edict_t *button, const vec3_t origin);
qboolean SG_DeclaredButtonDoorContactMatches(edict_t *button,
	const vec3_t origin);
qboolean SG_DeclaredButtonDoorApproachSourceClear(edict_t *button,
	const vec3_t origin);
qboolean SG_OracleReplayTriggerEvents(edict_t *trigger,
	qboolean *contaminated, qboolean *door_passed);
qboolean SG_OracleReplayDoorPassage(const vec3_t from, const vec3_t to);
qboolean SG_OracleReplaySourceEvents(edict_t *ent,
	qboolean *contaminated, qboolean *door_passed);
int SG_DeclaredDoorMembers(edict_t *trigger, edict_t **members,
	int capacity);
int SG_DeclaredDoorTriggerWaitMs(edict_t *trigger);
struct sg_rune_mechanism_binding_s;
/* Game-side mover ownership must not retire while any retained player,
 * corpse, or grapple bolt still intersects a physical member's complete
 * sweep.  Both arguments must be exact, currently linked g_edicts entries;
 * malformed, stale, non-solid, or unsupported identities fail closed. */
qboolean SG_MoverSubjectOutsideSweep(edict_t *member, edict_t *subject);
/* Population-independent validation for one immediate pusher dispatch. */
qboolean SG_MoverProspectivePusherValid(edict_t *member);
/* Immediate engine-pusher fence: prove a retained physical subject outside
 * the brush volume swept by the member's next quantized 100 ms push.  This is
 * intentionally narrower than the complete route sweep above. */
qboolean SG_MoverSubjectOutsideProspectivePush(edict_t *member,
	edict_t *subject);
qboolean SG_DeclaredDoorOutsideSweep(edict_t *trigger, const vec3_t origin);
qboolean SG_DeclaredDoorCrossesSweep(edict_t *trigger, const vec3_t from,
	const vec3_t to);
qboolean SG_DeclaredDoorAtTop(edict_t *trigger);
qboolean SG_DeclaredDoorAtTopFor(edict_t *trigger, int window_ms);
qboolean SG_BoundDoorOutsideSweep(
	const struct sg_rune_mechanism_binding_s *binding, const vec3_t origin);
qboolean SG_BoundDoorCrossesSweep(
	const struct sg_rune_mechanism_binding_s *binding, const vec3_t from,
	const vec3_t to);
qboolean SG_BoundDoorAtTop(
	const struct sg_rune_mechanism_binding_s *binding);
qboolean SG_BoundDoorAtTopFor(
	const struct sg_rune_mechanism_binding_s *binding, int window_ms);
qboolean SG_BoundDoorTouchMatches(
	const struct sg_rune_mechanism_binding_s *binding,
	const vec3_t activator_origin);
qboolean SG_BoundDoorEntryContactMatches(
	const struct sg_rune_mechanism_binding_s *binding,
	const vec3_t activator_origin);
/* Protective maintenance for an exact already-authorized physical member
 * set.  This exists for the lifecycle edge where the activator disappears
 * after acquisition; it validates the entire set before changing any timer. */
qboolean SG_DeclaredDoorHoldMembers(edict_t *const *members, int count,
	int lease_ms);
qboolean SG_DeclaredDoorMembersTerminal(edict_t *const *members, int count);
qboolean SG_DeclaredDoorHoldOpen(edict_t *trigger, int lease_ms);
int SG_DeclaredDoorContractCost(edict_t *trigger, int approach_ms,
	int touch_ms, int egress_ms);
qboolean SG_DeclaredDoorTouchMatches(edict_t *trigger,
	const vec3_t activator_origin);
qboolean SG_DeclaredDoorActivationMatches(edict_t *trigger,
	edict_t *door_master, const vec3_t activator_origin);
qboolean SG_DeclaredDoorEquivalentTouch(edict_t *expected,
	edict_t *actual, const vec3_t activator_origin);
qboolean SG_DeclaredDoorEquivalentActivation(edict_t *expected,
	edict_t *actual, edict_t *door_master,
	const vec3_t activator_origin);
qboolean SG_DeclaredDoorSameSet(edict_t *first, edict_t *second);
qboolean SG_DeclaredDoorApproachSourceClear(edict_t *trigger,
	const vec3_t origin);
qboolean SG_OracleDeclaredDoorStepSafe(edict_t *ent, edict_t *trigger,
	const usercmd_t *cmd);
qboolean SG_OracleBoundDoorStepSafe(edict_t *ent,
	const struct sg_rune_mechanism_binding_s *binding,
	const usercmd_t *cmd);
qboolean SG_OracleBoundDoorApproachStep(edict_t *ent,
	const struct sg_rune_mechanism_binding_s *binding,
	const usercmd_t *cmd, const sg_door_approach_state_t *state,
	sg_door_approach_prediction_t *prediction,
	sg_door_approach_reason_t *reason_out);
qboolean SG_OracleDeclaredDoorContinue(edict_t *ent, const vec3_t target,
	edict_t *trigger, int *arrival_ms);
qboolean SG_OracleBoundDoorContinue(edict_t *ent, const vec3_t target,
	const struct sg_rune_mechanism_binding_s *binding, int *arrival_ms);
qboolean SG_OracleDoorApproachContactObserved(qboolean button_controller,
	qboolean physical_touch, qboolean bound_contact);
qboolean SG_OracleDoorShallowWadeSafe(int waterlevel, int watertype);
qboolean SG_OracleDoorEgressWaterSafe(int controller_kind, int waterlevel,
	int watertype);
qboolean SG_OracleDeclaredDoorApproach(const vec3_t source,
	const vec3_t wait_point, edict_t *trigger, int *arrival_ms,
	int *touch_ms);
qboolean SG_OracleDeclaredButtonDoorApproach(const vec3_t source,
	const vec3_t wait_point, edict_t *button, int *arrival_ms,
	int *touch_ms, sg_button_support_mode_t *support_mode);
qboolean SG_OracleBoundDoorApproach(const vec3_t source,
	const vec3_t wait_point,
	const struct sg_rune_mechanism_binding_s *binding, int *arrival_ms,
	int *touch_ms);
qboolean SG_OracleBoundButtonDoorApproach(const vec3_t source,
	const vec3_t wait_point,
	const struct sg_rune_mechanism_binding_s *binding, int *arrival_ms,
	int *touch_ms, sg_button_support_mode_t *support_mode);
qboolean SG_OracleDeclaredDoorEgress(const vec3_t source,
	const vec3_t target, edict_t *trigger, edict_t *passent,
	int *arrival_ms);
qboolean SG_OracleDeclaredButtonDoorTopEgress(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *passent,
	int *arrival_ms, sg_button_support_mode_t support_mode);
qboolean SG_OracleBoundDoorEgress(const vec3_t source,
	const vec3_t target,
	const struct sg_rune_mechanism_binding_s *binding, edict_t *passent,
	int *arrival_ms);
qboolean SG_OracleBoundButtonDoorEgress(const vec3_t source,
	const vec3_t target,
	const struct sg_rune_mechanism_binding_s *binding, edict_t *passent,
	int *arrival_ms, sg_button_support_mode_t support_mode);
qboolean SG_OracleValidateDeclaredDoorLink(const vec3_t source,
	const vec3_t anchor, const vec3_t target, edict_t *trigger,
	int stored_cost_ms);
qboolean SG_OracleValidateBoundDoorLink(const vec3_t source,
	const vec3_t anchor, const vec3_t target,
	const struct sg_rune_mechanism_binding_s *binding, int stored_cost_ms);

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

void Caco_See(rune_t *r, edict_t *viewer);      /* one bot's eyes, per frame */
void Caco_HumanEyes(rune_t *r, int team);       /* what human teammates see */
void Caco_Frame(rune_t *r);                     /* shared HUD scan + aging */
void Caco_Reset(void);
void Caco_ResetClient(edict_t *client);

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

/* has anything landed on this body since `since`? Reads the same damage
 * ring: the spawn beat and the early-return errand both need "did the
 * world just object" and neither needs a sense of its own. */
qboolean Beat_HurtSince(edict_t *e, float since);

/* ------------------------------------------------------------ the rail rhythm
 *
 * The owner, describing his own play: "rails guard sight lines on many maps
 * ... the hard counter to my sight-lines coverage was raw speed". A rail is
 * not a stream of harm, it is one slug and then a long wait, and a human who
 * has to cross a guarded lane counts that wait. He hears the shot, steps out
 * while the shooter is empty, and ends the burst behind something before the
 * next one is loaded. Nothing here reads the enemy's weaponstate or his
 * ammunition; the only clock is the one every player in earshot shares.
 *
 * THE RELOAD, read off p_weapon.c rather than remembered.
 *
 * Weapon_Railgun calls Weapon_Generic(ent, 3, 18, 56, 61, ..., fire_frames
 * {4}), so FRAME_ACTIVATE_LAST is 3, FRAME_FIRE_FIRST is 4, FRAME_FIRE_LAST
 * is 18 and FRAME_IDLE_FIRST is 19. On the frame the trigger is accepted the
 * state goes READY -> FIRING with gunframe 4; that same call falls straight
 * on into the FIRING block, matches fire_frames[0], and runs
 * weapon_railgun_fire, which advances gunframe to 5 itself. Every later frame
 * advances it by one, and the state returns to READY the frame gunframe
 * reaches FRAME_IDLE_FIRST + 1 = 20 -- fifteen frames after the shot, by
 * which time the READY branch of that call has already been passed. The
 * earliest the next slug can leave is therefore the frame after that:
 * sixteen server frames at FRAMETIME 0.1, or 1.6 seconds shot to shot.
 */
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

/*
 * THE MEGA (sg_megaworth). item_health_mega matches the "item_health" prefix,
 * so it is already inside SG_FC_HEALTH's class flood -- and that is exactly
 * the problem the census found. A class field answers "cost to the NEAREST
 * health box", and a 10-point stimpack two rooms nearer hides the mega behind
 * it; the class worth then collapses to 0.05 at full health because the class
 * is priced as HEALTH, which a healthy bot does not want. The mega is not
 * health: it is +100 OVER max (HEALTH_IGNORE_MAX | HEALTH_TIMED,
 * g_items.c:598-640), which is why a human takes it at 100/100.
 *
 * Identity worth needs identity fields, the same argument that gave the
 * powerups and runes theirs -- so the megas get one field each, flooded FROM
 * the pad, and the detour triangle can be evaluated exactly against THAT pad.
 * Four is generous: no shipped map carries more than two.
 */
#define SG_MAX_MEGA			4

typedef struct
{
	int		red_flag_seed, blue_flag_seed;

	int		*to_red_flag, *to_blue_flag;        /* stands (capture points) */
	int		*to_flag_now[2][2];                 /* [believing team][flag colour] */
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

	/* appended: one field per believed-up mega, flooded FROM the pad, so
	 * to_mega[k][x] is the cost to get from x TO that pad (sg_megaworth).
	 * mega_count is 0 whenever the cvar is off -- nothing is flooded and
	 * nothing reads these. */
	int		*to_mega[SG_MAX_MEGA];
	int		mega_seed[SG_MAX_MEGA];
	int		mega_ent[SG_MAX_MEGA];
	int		mega_count;
	/* Per-team cliff penalty for seeds under the enemy stand. This prices the
	 * measured static-cost asymmetry that RL_DROP's flat surcharge misses.
	 * Zero on flat-stand maps. [0]=red attackers, [1]=blue attackers. */
	int		*shelf_cliff[2];

	/* The action topology used by every cached field.  The offhand-hook bit
	 * may change during a level; Fields_Refresh rebuilds all cached fields on
	 * that edge before publishing the new value.  DPO roots are retained as
	 * seed identities because their source planes are candidate-only setup
	 * storage and cannot be consulted again during a refresh. */
	qboolean hook_admitted;
	qboolean action_topology_pending;
	unsigned action_topology_epoch;
	int		post_seed[2];
	int		icept_seed[2];
	int		lane_seed[2];
} sg_fields_t;

extern sg_fields_t sg_fields;

/*
 * Candidate-only inputs consumed while the graph fields are constructed.
	 * The DPO order is fixed and deliberately named here so sidecar loading
	 * cannot silently transpose teams or post/intercept semantics:
 * post red, post blue, intercept red, intercept blue.
 *
 * A NULL setup object or NULL plane is neutral.  The pointed-to storage only
 * has to remain alive for Fields_Setup; this structure does not publish it.
 */
typedef enum sg_dpo_plane_e
{
	SG_DPO_POST_RED = 0,
	SG_DPO_POST_BLUE,
	SG_DPO_INTERCEPT_RED,
	SG_DPO_INTERCEPT_BLUE,
	SG_DPO_PLANE_COUNT
} sg_dpo_plane_t;

typedef struct sg_field_setup_inputs_s
{
	const unsigned char	*dpo[SG_DPO_PLANE_COUNT];
} sg_field_setup_inputs_t;

qboolean	Fields_Setup(rune_t *r, const sg_field_setup_inputs_t *inputs);
void		Fields_Refresh(rune_t *r);
qboolean	Fields_ActionTopologyRefresh(rune_t *r);
qboolean	Fields_ActionAdmitted(int action);
qboolean	Fields_ActionTopologyCurrent(unsigned epoch);
/* the one place the sg_megaworth cvar is read, so pricing, flooding and the
 * debug line can never disagree about whether the feature is on */
qboolean	SG_MegaOn(void);
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
/* Ordinary dose-2 bot commands mark one exact ClientThink.  The game boundary
 * consumes that mark so every other SG Pmove resets fractional landing time. */
void	SG_HumanSpeedClientThinkBegin(edict_t *ent);
void	SG_HumanSpeedPmoveBegin(edict_t *ent, pmove_state_t *pmove,
	unsigned command_msec);
void	SG_HumanSpeedPmoveEnd(edict_t *ent, const pmove_state_t *pmove,
	unsigned command_msec);
/* G_UseTargets marks only transient DelayedUse edicts with this bit.  It
 * preserves SG provenance across the delay without changing mapper entities
 * or treating human activators as guarded bots. */
#define SG_DELAYED_USE_BOT_ACTIVATOR 0x40000000
void		SG_CancelBotDelayedUses(edict_t *activator);
qboolean	SG_AuthorizeDoorTriggerTouch(edict_t *source, edict_t *activator);
qboolean	SG_AuthorizeDoorTriggerUse(edict_t *source, edict_t *activator);
qboolean	SG_AuthorizeButtonTouch(edict_t *source, edict_t *activator);
qboolean	SG_AuthorizeButtonUse(edict_t *source, edict_t *activator);
qboolean	SG_AuthorizeButtonTargets(edict_t *source, edict_t *activator);
void		SG_ButtonExecutionEntityFreed(edict_t *entity);
qboolean	SG_HandleMechanismTargets(edict_t *source,
								      edict_t *activator);
qboolean	SG_AuthorizeLiftTouch(edict_t *source, edict_t *platform,
								  edict_t *activator);
qboolean	SG_AuthorizeLiftUse(edict_t *platform, edict_t *activator);
edict_t		*SG_ResolveTeleportDestination(edict_t *source,
								       edict_t *activator);
qboolean	SG_AuthorizeDoorActivation(edict_t *source, edict_t *door_master,
								   edict_t *activator);
struct sg_bot_s;
/* Last-resort safety terminal: normal player_die lifecycle, deliberately
 * gibbed nonsolid so an unmaintainable mover cannot close onto a corpse. */
void		SG_DeclaredDoorTerminalDeath(struct sg_bot_s *bot);
void		SG_NoteDropTriggerContact(edict_t *source, edict_t *activator);
void		SG_NoteDropSolidContact(edict_t *source, edict_t *activator);
qboolean	SG_RetireBotForClient(edict_t *ent);
void		SG_DisownBot(edict_t *ent);
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
void		Botfill_Reset(void);    /* clear level-time cadence and hysteresis */
void		SG_LevelChange(void);   /* forget level-tagged rune and fields */
void		SG_DangerCheckpoint(const char *event); /* final dirty save */
void		SG_DangerPersistenceReset(void); /* release lease, forget model */

rune_t		*Rune_Load(const char *mapname);
int			Rune_NearestSeed(rune_t *r, vec3_t p);
rune_t		*SG_Rune(void);
const char	*SG_RuneMapName(void);         /* the loaded rune, NULL before setup --
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
