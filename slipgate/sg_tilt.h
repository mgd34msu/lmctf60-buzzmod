/*
 * sg_tilt.h -- tilt's public face and its tuning constants.
 */
#ifndef SG_TILT_H
#define SG_TILT_H

#define SG_TILT_WINDOW	25.0f   /* "not through there again", in seconds */
#define SG_TILT_REPEAT	60.0f   /* two deaths inside this is a pattern */
#define SG_TILT_PRICE	1.30f   /* what a lane step costs while it lasts */
#define SG_TILT_CAUTION	8.0f    /* post-respawn timidity at skill 0 */
#define SG_TILT_CAUTION4 4.0f   /* ...and at skill 4: the good player
                                 * shakes it off in half the time */
#define SG_TILT_ENGAGE	0.80f   /* engagement willingness while cautious */
#define SG_TILT_COVER	0.5f    /* the approach-cover dose, borrowed: while
                                 * cautious, EVERY role pays for open ground
                                 * the way an attacker on the last approach
                                 * already does */

void		Tilt_Lane(sg_bot_t *bot, int seed);
qboolean	Tilt_InLane(const sg_bot_t *bot, int seed);
void		Tilt_Note(edict_t *e, sg_bot_t *bot);
void		Tilt_LevelReset(void);

#endif
