/*
 * sg_move.h -- the movement module's face: the two think stages the
 * orchestrator calls.  Everything else in sg_move.c is internal.
 */
#ifndef SG_MOVE_H
#define SG_MOVE_H

extern int *sg_airnext;   /* per-seed way to air (Air_Build) */

/* builds the frame's movement from the context; cmd stays a parameter
 * until the whole frame speaks context */
void Think_Move(sg_bot_t *bot, sg_think_t *tc, usercmd_t *cmd);

/* turns the frame's decisions into the usercmd; context in */
void Think_Emit(sg_bot_t *bot, sg_think_t *tc, usercmd_t *cmd);

#endif
