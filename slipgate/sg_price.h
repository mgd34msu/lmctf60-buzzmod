/*
 * sg_price.h -- the pricing core's face, and the frame state the think
 * loop arms before every descent.
 */
#ifndef SG_PRICE_H
#define SG_PRICE_H

extern const int	*sg_cur_danger;
extern int			sg_cur_team;
extern qboolean		sg_route_pure_now;
extern qboolean		sg_cur_push;
extern int			sg_cur_health;
extern float		sg_cur_mega;

float	Rune_RoleFactor(int role, int entnum);
float	Detour_Value(int seed, int fc, const int *goal_field, float wv);
float	Mega_Detour(int seed, const int *goal_field, int *pad_out);
float	Surface_At(int seed, const sg_weights_t *w, const int *goal,
	           const int *support, const int *intercept);

#endif
