/*
 * sg_move.h -- the movement module's face: the two think stages the
 * orchestrator calls.  Everything else in sg_move.c is internal.
 */
#ifndef SG_MOVE_H
#define SG_MOVE_H

void Think_Move(sg_bot_t *bot, edict_t *e, sg_role_t role,
                int team, qboolean carrying,
                const sg_weights_t *live, const sg_weights_t *w,
                const int *goal_field, const int *route_field,
                qboolean route_pure, int bestlink,
                qboolean precision, qboolean hold_post,
                qboolean rally_hold, qboolean rail_hold,
                float post_yaw, float post_sight, qboolean duel,
                vec3_t duel_org, float duel_want, float duel_expo,
                usercmd_t *cmd,
                vec3_t move_dir_out, float *view_yaw_io,
                float *view_pitch_io, qboolean *have_move_out,
                qboolean *open_ahead_out, qboolean *run_link_out,
                int *door_hold_out, edict_t **door_ent_out,
                qboolean *drop_yaw_locked_out, float *drop_yaw_out,
                qboolean *hook_brake_out);

void Think_Emit(sg_bot_t *bot, edict_t *e, sg_role_t role,
                int team, qboolean carrying,
                const sg_weights_t *live, const sg_weights_t *w,
                const int *goal_field, const int *route_field,
                qboolean route_pure, int bestlink,
                qboolean precision, qboolean hold_post,
                qboolean rally_hold, qboolean rail_hold,
                int rail_seed, int rail_client, float rail_dose,
                float post_yaw, float post_sight,
                qboolean duel, vec3_t duel_org, float duel_want,
                float duel_expo, vec3_t move_dir, float view_yaw,
                float view_pitch, qboolean have_move,
                qboolean open_ahead, qboolean run_link,
                int door_hold, edict_t *door_ent,
                qboolean drop_yaw_locked, float drop_yaw,
                qboolean hook_brake, usercmd_t *cmd);

#endif
