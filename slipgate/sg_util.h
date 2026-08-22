/* sg_util.h -- shared team, flag, geometry, and visibility helpers. */
#ifndef SG_UTIL_H
#define SG_UTIL_H


int	SG_TeamIdx(int team);
int	SG_TeamFromIdx(int idx);
int	SG_EnemyTeam(int team);

/* Deterministic non-world supports admitted by offline movement proofs. */
qboolean SG_ImmutableSupport(edict_t *ent);

/* A carried flag edict remains at its take origin. During the one-second drop
 * grace, owner names only the former carrier; inventory identifies a carrier. */
edict_t *SG_FlagCarrier(edict_t *flag);
qboolean SG_FlagApproachAvailableTo(edict_t *flag, edict_t *player);

/* The live flag ITEMS (droptofloor-settled, the thing a touch scores on),
 * by the engine's own pointers. NULL when absent or actually carried. */
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
qboolean SG_BlasterAimAngles(const vec3_t origin, float viewheight, int hand,
	const vec3_t aim, vec3_t view_angles, vec3_t muzzle);
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
qboolean SG_RocketJumpArrived(const vec3_t origin, const vec3_t destination,
	qboolean grounded, int waterlevel, edict_t *support, edict_t *passent);

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
qboolean SG_LiftWaitPoint(edict_t *entry, const vec3_t origin, vec3_t wait);
qboolean SG_LiftRider(edict_t *plat, edict_t *body);
qboolean SG_LiftRest(edict_t *entry, edict_t *plat, edict_t *passent,
	vec3_t rest);
qboolean SG_TeleportApproachPoint(edict_t *pad, vec3_t approach);


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
