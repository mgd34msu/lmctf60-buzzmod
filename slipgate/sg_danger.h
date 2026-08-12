/*
 * sg_danger.h -- the danger dimension's public face.
 */
#ifndef SG_DANGER_H
#define SG_DANGER_H

void		Danger_Learn(int team, int seed);
void		Danger_Decay(void);
void		Danger_Save(void);
void		Danger_Load(void);
const int	*Danger_Field(int team);

#endif
