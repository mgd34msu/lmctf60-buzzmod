#ifndef SG_PICKUP_TARGET_H
#define SG_PICKUP_TARGET_H

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"

qboolean SG_LocalPickupTarget(edict_t *self, edict_t *item, vec3_t target);
qboolean SG_WeaponPickupRouteEligible(const edict_t *item,
	const edict_t *taker);
qboolean SG_DefenseSupplyTargetValid(const sg_bot_t *bot);
qboolean SG_StrikeWeaponTargetValid(const sg_bot_t *bot);
qboolean SG_WeaponPickupTarget(const sg_bot_t *bot, qboolean strike_pursuit,
	vec3_t target);
qboolean SG_MegaPickupTarget(const sg_think_t *tc, vec3_t target);

#endif /* SG_PICKUP_TARGET_H */
