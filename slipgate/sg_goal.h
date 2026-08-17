#ifndef SG_GOAL_H
#define SG_GOAL_H

#include "slipgate/sg_defense_supply.h"

extern float sg_grab_time[2];      /* per team, when that team's carrier last
                                     * grabbed -- the escort/attack post-grab
                                     * hold window reads its age */
extern float sg_push_until[2];     /* the conductor's window (sg_wavepush) */

/* human escape priors (sg_escapeprior): buckets of the corpus draw an
 * attacker's carry decision from. Populated once at level load. */
#define SG_ESC_BUCKETS	8
extern int sg_escape_count[2][SG_ESC_BUCKETS];  /* [0]=red flag stolen, [1]=blue */
extern int sg_escape_total[2];                  /* 0 = no prior for that flag */

/* A watchman supply sortie is deliberately bounded: 2400 ms admits the
 * measured smap05 rocket leg (~2200 ms), while the hard five-second wall
 * clock ends OUTBOUND weapon pursuit and hands authority to the home route. */
#define SG_DEF_SUPPLY_MAX_ROUTE_MS 2400
#define SG_DEF_SUPPLY_DEADLINE     SG_DEFENSE_SUPPLY_DEADLINE_SECONDS
#define SG_DEF_SUPPLY_BACKOFF      2.0f
#define SG_DEF_SUPPLY_NONE         0
#define SG_DEF_SUPPLY_OUTBOUND     1
#define SG_DEF_SUPPLY_RETURN       2

void SG_DefenseSupplyReset(sg_bot_t *bot);
void SG_DefenseSupplyCancel(sg_bot_t *bot, qboolean backoff);
void SG_DefenseSupplyBeginReturn(sg_bot_t *bot);
void SG_DefenseSupplyFinish(sg_bot_t *bot);
qboolean SG_DefenseSupplyActive(const sg_bot_t *bot);
qboolean SG_DefenseSupplyHome(int team);
qboolean SG_DefenseSupplyThreat(int team);

/* fills the frame's live weight row in the context */
void Think_LiveWeights(sg_bot_t *bot, sg_think_t *tc);

void Think_CarryBookends(sg_bot_t *bot, edict_t *e,
                                sg_role_t role, int team,
                                qboolean carrying);

/* resolves the frame's objective fields; context in, context out */
void Think_Objective(sg_bot_t *bot, sg_think_t *tc);

void Think_InterceptField(sg_role_t role, int team,
                                 const int **support_out,
                                 const int **intercept_out);

/* decides whether the attacker waits for a partner; context in */
qboolean Think_ApproachBand(sg_bot_t *bot, sg_think_t *tc);

#endif /* SG_GOAL_H */
