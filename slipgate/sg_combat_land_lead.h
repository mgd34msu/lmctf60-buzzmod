#ifndef SG_COMBAT_LAND_LEAD_H
#define SG_COMBAT_LAND_LEAD_H

#include "g_local.h"

typedef struct
{
	float touch_time;
	float gravity;
} sg_combat_landing_t;

float SG_CombatRocketSpeed(void);
float SG_CombatRocketRadius(void);
qboolean SG_CombatLandingAim(edict_t *enemy, vec3_t point,
                             sg_combat_landing_t *landing);
qboolean SG_CombatLandingSplashClear(edict_t *self, edict_t *enemy,
                                     vec3_t muzzle, vec3_t shotdir,
                                     sg_combat_landing_t landing,
                                     float *projectile_time, vec3_t impact);

#ifdef SG_COMBAT_AIM_TEST
int SG_CombatLandLeadTestSplashReaches(float distance);
#endif

#endif
