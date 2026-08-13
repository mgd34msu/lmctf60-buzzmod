/*
 * sg_descend.h -- the descent module's face: the two stages the
 * orchestrator calls.
 */
#ifndef SG_DESCEND_H
#define SG_DESCEND_H

/* per-link human traffic tiers, loaded alongside the rune (0-255) --
 * the descent's own link-cost pricing terms */
extern unsigned char *sg_human_use;
extern unsigned char *sg_human_live;    /* cut from the 20s windows */
extern unsigned char *sg_human_escape;  /* the ESCAPEE's cut: only the flag */

/* per-seed human defensive dwell / steal-response END, per team */
extern unsigned char *sg_def_post[2];
extern unsigned char *sg_def_icept[2];

/* prices the link fan and picks the leg; reads its inputs from the think
 * context and writes bestval/incumbent/rail_* results back into it */
int Think_PickLink(sg_bot_t *bot, sg_think_t *tc);

int Think_CommitLink(sg_bot_t *bot, edict_t *e, sg_role_t role,
                            int team, qboolean carrying,
                            const sg_weights_t *live,
                            const sg_weights_t *w,
                            const int *goal_field, qboolean precision,
                            qboolean duel, vec3_t duel_org,
                            float duel_want, float duel_expo,
                            float bestval, float incumbent_v,
                            int rail_seed, int rail_client,
                            float rail_dose, int bestlink_in, usercmd_t *cmd,
                            qboolean *rally_hold_io,
                            qboolean *rail_hold_io,
                            qboolean *think_over,
                            qboolean *hold_post_out,
                            float *post_yaw_io, float *post_sight_io);

#endif
