/*
 * sg_lead.h -- the early-return errand's public face.
 */
#ifndef SG_LEAD_H
#define SG_LEAD_H

#define SG_LEAD_BASE		4.0f    /* seconds of lead before the persona */
#define SG_LEAD_CAMP		8.0f    /* ... and what camp_tendency adds to it */
#define SG_LEAD_JITTER		2.0f    /* rolled per attempt: no synchronised herd */
#define SG_LEAD_STANDOFF	400     /* ms of field: ~120u at 300 u/s, and the
                                     * same number the defender's post pins at */
#define SG_LEAD_GRACE		4.0f    /* how long past T the wait is still a wait */
#define SG_LEAD_RETRY		1.0f    /* attempt cadence while nothing is claimed */
#define SG_LEAD_LEASE		1.0f    /* the claim a live errand re-stamps */
#define SG_LEAD_MAXWAIT		20.0f   /* total stand ceiling: past this the
                                     * clock was a miscall (owner's rule) */
#define SG_LEAD_SPEED		300.0f  /* u/s: pm_maxspeed, the file's own ruler */

void		Lead_Abort(sg_bot_t *bot, const char *why);
const int	*Lead_Field(sg_bot_t *bot, sg_role_t role, qboolean carrying);

#endif
