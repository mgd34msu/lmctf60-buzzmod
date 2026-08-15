/*
 * sg_util.h -- the patterns every SLIPGATE file kept re-typing.
 *
 * Born in the 2026-08-11 standards pass: flag-entity resolution, the
 * stand-marker lookup, XY distance, and the eye-to-point sight trace
 * each existed as five to eleven hand-rolled copies.  One copy each,
 * here, and a call site reads as intent instead of plumbing.
 */
#ifndef SG_UTIL_H
#define SG_UTIL_H

/* Team/index conversions -- CTF_TEAM_RED and CTF_TEAM_BLUE (1 and 2,
 * g_ctffunc.h) are the wire values, but most per-team state is a
 * 2-element array (row 0 red, row 1 blue), and finding the OTHER
 * team came up just as often. Both directions were hand-derived at
 * well over a hundred sites -- `team - CTF_TEAM_RED` as an array
 * index, `(team == CTF_TEAM_RED) ? 0 : 1` written the other way
 * around, an enemy team spelled out as a ternary -- each a fresh
 * chance for the two forms to drift.
 *
 *   SG_TeamIdx(team)     team - CTF_TEAM_RED       (0 red, 1 blue)
 *   SG_TeamFromIdx(idx)  CTF_TEAM_RED + idx         (inverse of above)
 *   SG_EnemyTeam(team)   the other of RED/BLUE
 *
 * All three assume team is CTF_TEAM_RED or CTF_TEAM_BLUE; a caller
 * that has not already ruled out CTF_TEAM_UNDEFINED (or a spectator)
 * rules it out itself, the same discipline the sites they replace
 * already required of themselves. */
int	SG_TeamIdx(int team);
int	SG_TeamFromIdx(int idx);
int	SG_EnemyTeam(int team);

/* Deterministic non-world supports admitted by offline movement proofs. */
qboolean SG_ImmutableSupport(edict_t *ent);

/* The live flag ITEMS (droptofloor-settled, the thing a touch scores
 * on), by the engine's own pointers.  NULL when absent or carried. */
edict_t	*SG_OwnFlag(int team);      /* the flag this team defends */
edict_t	*SG_EnemyFlag(int team);    /* the flag this team steals */

/* The info_flag_* spawn MARKER -- the stand's advertised position,
 * common knowledge under Rule 19 even when the item is elsewhere. */
edict_t	*SG_FlagStand(int team, qboolean own);

/* Horizontal distance -- the pattern behind most range gates. */
float	SG_DistXY(const vec3_t a, const vec3_t b);

/* Eye-to-point sight: can this entity's viewpoint see pt (lifted by
 * lift_z) through MASK_OPAQUE?  The standard perception trace. */
qboolean SG_CanSee(edict_t *e, const vec3_t pt, float lift_z);

/* Hook controls are exact quantized pitch/yaw plus handed muzzle-ray distance.
 * Decode the nominal proof source into the view, muzzle, and static-world bite;
 * live execution re-proves that same control from its actual fixed-point source. */
qboolean SG_HookAimAngles(const vec3_t origin, float viewheight,
	const vec3_t aim, vec3_t view_angles);
qboolean SG_HookControlDecode(const vec3_t origin, float viewheight, int hand,
	const vec3_t control, vec3_t view_angles, vec3_t muzzle, vec3_t bite);

/* A proved swim is a literal feedback controller, not a RUN link with a wet
 * label.  Generation and execution both submit this same 25 ms command and
 * retire it through the same 100 ms arrival predicate.  SG_SwimCommand keeps
 * cmd->msec, but refuses any boundary other than 25 ms; everything else in the
 * command is owned and replaced. */
#define SG_SWIM_STEP_MSEC 25
qboolean SG_SwimCommand(const vec3_t origin, const vec3_t destination,
	const pmove_state_t *pms, usercmd_t *cmd);
qboolean SG_SwimArrived(const vec3_t origin, const vec3_t destination,
	qboolean destination_water, qboolean grounded, int watertype, int waterlevel,
	edict_t *passent);
qboolean SG_SupportedArrived(const vec3_t origin, const vec3_t destination,
	qboolean grounded, int watertype, int waterlevel, edict_t *passent);

/* A runtime-supported outer action with the direct OWNS_CONTROL trait owns
 * the command stream from staging through its completion predicate. Tactical
 * weapon cooks must not arm beside an existing commitment and deadlock both
 * owners. Effective suffixes never authorize ownership. */
qboolean SG_ActionOwnsControl(int action);
qboolean SG_DeclaredCommand(const vec3_t origin, const vec3_t target,
	const pmove_state_t *pms, usercmd_t *cmd);
qboolean SG_EscortTerminal(edict_t *bot, edict_t *target);

/* g_func.c keeps these values file-local, but declared lift execution must
 * read the same moveinfo state machine.  Names stay SG-prefixed so the game
 * implementation remains the owner of its private STATE_* macros. */
#define SG_PLAT_STATE_TOP    0
#define SG_PLAT_STATE_BOTTOM 1
#define SG_PLAT_STATE_UP     2
#define SG_PLAT_STATE_DOWN   3
edict_t *SG_LiftForAnchor(const vec3_t anchor);
qboolean SG_LiftWaitPoint(edict_t *plat, const vec3_t origin, vec3_t wait);
qboolean SG_LiftRider(edict_t *plat, edict_t *body);
qboolean SG_LiftTopRest(edict_t *plat, edict_t *passent, vec3_t rest);
edict_t *SG_TeleportForAnchor(const vec3_t anchor);
qboolean SG_TeleportApproachPoint(edict_t *pad, vec3_t approach);
qboolean SG_TeleportDestinationForAnchor(const vec3_t anchor, vec3_t destination);

/* Timer/cooldown primitives -- the two hand-rolled patterns against
 * level.time (a float, seconds, restarts at zero on changelevel) that
 * this tree kept re-typing at well over a hundred sites, each a fresh
 * read of the same idiom with occasional > vs >= drift between them.
 *
 * A DEADLINE field holds a future level.time value ("ready at X"):
 *   SG_TimerArm(&field, delay)    field = level.time + delay
 *   SG_TimerReady(field)          level.time >= field
 *   SG_TimerReadyStrict(field)    level.time >  field
 *   SG_TimerPending(field)        level.time <  field
 *   SG_TimerRemaining(field)      field - level.time
 *
 * A SINCE field holds a past level.time value ("started at X"):
 *   SG_Mark(&field)               field = level.time
 *   SG_Age(field)                 level.time - field
 *   SG_AgeOver(field, span)       (level.time - field) >  span
 *   SG_AgeAtLeast(field, span)    (level.time - field) >= span
 *   SG_AgeUnder(field, span)      (level.time - field) <  span
 *
 * A comparison written with the operands reversed at the call site
 * (e.g. "field > level.time" instead of "level.time < field") still
 * picks whichever of these matches the ORIGINAL operator -- reversing
 * operand order on a comparison changes nothing arithmetically, but
 * swapping > for >= does, and that edge is exactly what these keep:
 * SG_TimerReady and SG_TimerReadyStrict are not interchangeable, and
 * neither are SG_AgeOver, SG_AgeAtLeast, and SG_AgeUnder. */
void     SG_TimerArm(float *stamp, float delay);
qboolean SG_TimerReady(float stamp);
qboolean SG_TimerReadyStrict(float stamp);
qboolean SG_TimerPending(float stamp);
float    SG_TimerRemaining(float stamp);

void     SG_Mark(float *stamp);
float    SG_Age(float since);
qboolean SG_AgeOver(float since, float span);
qboolean SG_AgeAtLeast(float since, float span);
qboolean SG_AgeUnder(float since, float span);

#endif
