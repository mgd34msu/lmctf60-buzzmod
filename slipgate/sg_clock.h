/*
 * sg_clock.h -- clockplay's public face: posture reads for role logic
 * and cover pricing, the per-frame tick, and the level reset.
 */
#ifndef SG_CLOCK_H
#define SG_CLOCK_H

void	Clock_Frame(void);
int		Clock_DefendShift(int team);
float	Clock_CoverScale(int team);
void	Clock_LevelReset(void);

#endif
