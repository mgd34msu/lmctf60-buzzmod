/*
 * sg_danger.h -- the danger dimension's public face.
 */
#ifndef SG_DANGER_H
#define SG_DANGER_H

void		Danger_Learn(int team, int seed);
void		Danger_Decay(void);
/* Legacy native persistence entry points: deliberately uncalled by the v3 B3
 * runtime. B4 will replace their format before restoring cross-match state. */
void		Danger_Save(void);
void		Danger_Load(void);
void		Danger_ResetLevel(void);
const int	*Danger_Field(int team);

#endif
