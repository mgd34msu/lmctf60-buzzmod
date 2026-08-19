#ifndef SG_COMBAT_TARGET_POLICY_H
#define SG_COMBAT_TARGET_POLICY_H

#include <math.h>

#define SG_COMBAT_TARGET_STICK_UNITS 128.0f
#define SG_COMBAT_CARRIER_PRIORITY_UNITS 256.0f

/* Preserve a currently visible target across incidental distance crossings.
 * A different enemy still wins as soon as it is materially closer. */
static inline float SG_CombatTargetScore(float distance, int entity_index,
	int incumbent_index, qboolean enemy_carrier)
{
	if (!isfinite(distance) || distance < 0.0f)
		return HUGE_VALF;
	if (entity_index == incumbent_index)
		distance -= SG_COMBAT_TARGET_STICK_UNITS;
	if (enemy_carrier)
		distance -= SG_COMBAT_CARRIER_PRIORITY_UNITS;
	return distance;
}

#endif
