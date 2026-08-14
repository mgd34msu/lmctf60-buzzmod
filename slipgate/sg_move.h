/*
 * sg_move.h -- the movement module's face: the two think stages the
 * orchestrator calls.  Everything else in sg_move.c is internal.
 */
#ifndef SG_MOVE_H
#define SG_MOVE_H

extern int *sg_airnext;   /* per-seed way to air (Air_Build) */

/* builds the frame's movement from the context; cmd stays a parameter
 * until the whole frame speaks context */
void Think_Move(sg_bot_t *bot, sg_think_t *tc);

/* turns the frame's decisions into the usercmd; context in */
void Think_Emit(sg_bot_t *bot, sg_think_t *tc);

/* Active proved hook phases own their command independently of field
 * localization. Returns true when it consumed this server frame. */
qboolean SG_HookActiveFrame(sg_bot_t *bot, edict_t *e);

/* Capability contract shared by link selection and execution. */
qboolean SG_HookOffhandReady(edict_t *e);

/* Candidate selection and the exact-source launch gate use one conservative
 * P_FallingDamage contract. Combat may change health while staging, so both
 * call sites are required. */
qboolean SG_BallisticSurvivable(edict_t *e, const rune_link_t *link);

#endif
