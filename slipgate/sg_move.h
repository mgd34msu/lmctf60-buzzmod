/*
 * sg_move.h -- the movement module's face: the two think stages the
 * orchestrator calls.  Everything else in sg_move.c is internal.
 */
#ifndef SG_MOVE_H
#define SG_MOVE_H

/* Pointer-only public seams must not depend on an incidental include order. */
typedef struct sg_rune_mechanism_binding_s sg_rune_mechanism_binding_t;
typedef struct sg_door_approach_prediction_s sg_door_approach_prediction_t;

extern int *sg_airnext;   /* per-seed way to air (Air_Build) */

/* Invalidates every deferred SG button callback at a level boundary. */
void SG_ButtonExecutionLevelReset(void);
/* Invalidates one exact source incarnation before G_FreeEdict recycles it. */
void SG_ButtonExecutionEntityFreed(edict_t *entity);
/* Clears process-local BUTTON_DOOR support/endpoint authority whenever the
 * surrounding declared action is retired or a bot slot changes identity. */
void SG_ButtonExecutionActionReset(sg_bot_t *bot);
/* The synchronous DIRECT_TRIGGER_DOOR command seam.  Begin freezes the
 * durable action identity; Arm mints authority for exactly one frame/substep;
 * Finish consumes it after ClientThink.  CommandClear is pause-safe and does
 * not erase the persistent reducer. */
qboolean SG_DeclaredDoorApproachExecutionBegin(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding, const short source_q8[3],
	const short anchor_q8[3]);
qboolean SG_DeclaredDoorApproachExecutionArm(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding,
	const sg_door_approach_prediction_t *prediction, int substep);
qboolean SG_DeclaredDoorApproachExecutionFinish(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding, edict_t *entity);
void SG_DeclaredDoorApproachCommandClear(sg_bot_t *bot);
/* Exercises the same guard-retain path production uses after a failed
 * reauthorization: command authority is erased, while the durable reducer
 * and its continuous-air history remain frozen for resume. */
void SG_DeclaredDoorApproachExecutionRetain(sg_bot_t *bot);
/* Shared by the live movement gate and its game-boundary regression. */
qboolean SG_ButtonExecutionSupportValid(
	const sg_rune_mechanism_binding_t *binding, const sg_bot_t *bot,
	const edict_t *subject);

typedef enum sg_button_execution_anchor_state_e
{
	SG_BUTTON_EXECUTION_ANCHOR_INVALID = 0,
	SG_BUTTON_EXECUTION_ANCHOR_BOTTOM,
	SG_BUTTON_EXECUTION_ANCHOR_TOP,
	SG_BUTTON_EXECUTION_ANCHOR_MOVING
} sg_button_execution_anchor_state_t;

/* Resolve the authenticated BOTTOM anchor against the exact current button
 * endpoint.  STATIC mode remains at the serialized anchor; RIDER mode follows
 * the sealed displacement.  Moving endpoints are returned only for activated
 * retreat reproof and never authorize the initial egress handoff. */
sg_button_execution_anchor_state_t SG_ButtonExecutionAnchor(
	const sg_rune_mechanism_binding_t *binding, const sg_bot_t *bot,
	const edict_t *subject, const vec3_t bottom_anchor,
	const vec3_t serialized_displacement, int serialized_mode,
	vec3_t effective_anchor);

/* builds the frame's movement from the context; cmd stays a parameter
 * until the whole frame speaks context */
void Think_Move(sg_bot_t *bot, sg_think_t *tc);

/* turns the frame's decisions into the usercmd; context in */
void Think_Emit(sg_bot_t *bot, sg_think_t *tc);

/* The posted-defense tangent is a transient life-local command preference.
 * Death and slot lifecycle callers clear it before a new body can act. */
void SG_DefenseCombatLeaseReset(sg_bot_t *bot);

/* Active proved hook phases own their command independently of field
 * localization. Returns true when it consumed this server frame. */
qboolean SG_HookActiveFrame(sg_bot_t *bot, edict_t *e);

/* Called immediately after Weapon_Hook_Fire performs the one production
 * end-frame pull.  It only observes an ordinary graph hook that is already
 * reducer-owned; all other clients retain the historical engine path. */
void SG_HookLiveEndFrame(edict_t *e);

/* Capability contract shared by link selection and execution. */
qboolean SG_HookOffhandReady(edict_t *e);

/* Exact direct-touch authorities shared by route commitment and terminal
 * movement. Enemy-flag authority also preempts optional prebreach actions. */
qboolean SG_AttackFlagDirectTouchAuthority(edict_t *e, int team,
	edict_t **flag_out);
qboolean SG_OwnHomeFlagDirectTouchAuthority(edict_t *e, int team,
	edict_t **flag_out);

/* Flag-room flying cooks are one transaction regardless of whether the
 * approach band or the terminal room starts them: bind only a current,
 * visible live enemy inside the strike throw envelope, and erase every
 * identity/deadline field when the transaction is retired. */
void SG_NadeTargetClear(sg_bot_t *bot);
qboolean SG_NadeArmPrebreachLiveEnemy(sg_bot_t *bot, edict_t *e, int team);

/* Candidate selection and the exact-source launch gate use one conservative
 * P_FallingDamage contract. Combat may change health while staging, so both
 * call sites are required. */
qboolean SG_BallisticSurvivable(edict_t *e, const rune_link_t *link);

#ifdef SG_STRIKE_TRANSITION_TEST_API
void SG_StrikeTestDirectTouchClaimMovement(sg_bot_t *bot, const edict_t *e,
	sg_think_t *tc, qboolean terminal);
qboolean SG_StrikeTestDirectTouchDuelWeave(qboolean terminal, usercmd_t *cmd);
qboolean SG_StrikeTestEnemyFlagTouchMissionActive(qboolean strike_pressure,
	qboolean scoop_mission);
qboolean SG_TestGenericRailMoveAllowed(const sg_bot_t *bot,
	const sg_think_t *tc);
qboolean SG_StrikeTestRocketJumpPhase2Command(const sg_bot_t *bot,
	const edict_t *e, usercmd_t *cmd);
#endif

#endif
