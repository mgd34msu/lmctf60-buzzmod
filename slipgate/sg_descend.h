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

/* holds or releases the committed leg; context in, context out, cmd stays
 * a parameter until the movement stage speaks context */
int Think_CommitLink(sg_bot_t *bot, sg_think_t *tc);

#endif /* SG_DESCEND_H */
