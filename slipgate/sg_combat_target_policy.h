#ifndef SG_COMBAT_TARGET_POLICY_H
#define SG_COMBAT_TARGET_POLICY_H

#include <math.h>
#include "g_ctffunc.h"

#define SG_COMBAT_TARGET_STICK_UNITS 128.0f
#define SG_COMBAT_CARRIER_PRIORITY_UNITS 256.0f

/* Belief nominates the client, but carrier priority and the intercept weapon
 * ladder require the currently visible target to still possess a flag.  The
 * self-team row is selected by the production caller before this law runs. */
static inline qboolean SG_CombatEnemyCarrierAllowed(int self_team,
	int maxclients, int believed_client, int target_client,
	qboolean target_carrying_flag)
{
	if (target_carrying_flag != false && target_carrying_flag != true)
		return false;
	return (self_team == CTF_TEAM_RED || self_team == CTF_TEAM_BLUE) &&
	       maxclients > 0 && believed_client >= 0 &&
	       believed_client < maxclients && target_client == believed_client &&
	       target_carrying_flag;
}

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
