#ifndef SG_GOAL_H
#define SG_GOAL_H

void Think_LiveWeights(sg_bot_t *bot, edict_t *e, sg_role_t role,
                              int team, sg_weights_t *live);

void Think_CarryBookends(sg_bot_t *bot, edict_t *e,
                                sg_role_t role, int team,
                                qboolean carrying);

void Think_Objective(sg_bot_t *bot, edict_t *e, sg_role_t role,
                            int team, qboolean carrying,
                            const sg_weights_t *w,
                            const int *support, const int *intercept,
                            const int **goal_out, const int **route_out,
                            qboolean *route_pure_out);

void Think_InterceptField(sg_role_t role, int team,
                                 const int **support_out,
                                 const int **intercept_out);

qboolean Think_ApproachBand(sg_bot_t *bot, edict_t *e,
                                   sg_role_t role, int team,
                                   const int *goal_field);

#endif
