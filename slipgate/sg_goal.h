#ifndef SG_GOAL_H
#define SG_GOAL_H

extern float sg_grab_time[2];      /* per team, when that team's carrier last
                                     * grabbed -- the escort/attack post-grab
                                     * hold window reads its age */
extern float sg_push_until[2];     /* the conductor's window (sg_wavepush) */

/* human escape priors (sg_escapeprior): buckets of the corpus draw an
 * attacker's carry decision from. Populated once at level load. */
#define SG_ESC_BUCKETS	8
extern int sg_escape_count[2][SG_ESC_BUCKETS];  /* [0]=red flag stolen, [1]=blue */
extern int sg_escape_total[2];                  /* 0 = no prior for that flag */

void Think_LiveWeights(sg_bot_t *bot, edict_t *e, sg_role_t role,
                              int team, sg_weights_t *live);

void Think_CarryBookends(sg_bot_t *bot, edict_t *e,
                                sg_role_t role, int team,
                                qboolean carrying);

/* resolves the frame's objective fields; context in, context out */
void Think_Objective(sg_bot_t *bot, sg_think_t *tc);

void Think_InterceptField(sg_role_t role, int team,
                                 const int **support_out,
                                 const int **intercept_out);

qboolean Think_ApproachBand(sg_bot_t *bot, edict_t *e,
                                   sg_role_t role, int team,
                                   const int *goal_field);

#endif
