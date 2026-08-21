#ifndef SG_COMBAT_TARGET_POLICY_H
#define SG_COMBAT_TARGET_POLICY_H

#include <math.h>
#include "g_ctffunc.h"

#define SG_COMBAT_TARGET_STICK_UNITS 128.0f
#define SG_COMBAT_CARRIER_PRIORITY_UNITS 256.0f

static inline byte SG_CombatTargetClaimTrigger(byte buttons,
	qboolean target_acquired)
{
	if (target_acquired)
		buttons &= ~BUTTON_ATTACK;
	return buttons;
}

/* A retained combat target is an observation about one client life, not an
 * enduring claim on an edict slot.  Client slots are recycled and team
 * changes respawn through a new ctfid, so every external consumer of the
 * retained target must revalidate both the opposing team and that exact life
 * before using the live entity. */
static inline qboolean SG_CombatLiveEnemyIdentityAllowed(int self_team,
	int target_team, int maxclients, int num_edicts, int target_edict_index,
	unsigned long expected_ctfid, unsigned long current_ctfid,
	qboolean target_inuse, qboolean target_client, qboolean target_live,
	qboolean target_noclip)
{
	if ((target_inuse != false && target_inuse != true) ||
	    (target_client != false && target_client != true) ||
	    (target_live != false && target_live != true) ||
	    (target_noclip != false && target_noclip != true))
		return false;
	if (self_team != CTF_TEAM_RED && self_team != CTF_TEAM_BLUE)
		return false;
	if (target_team != CTF_TEAM_RED && target_team != CTF_TEAM_BLUE)
		return false;
	if (target_team == self_team || maxclients <= 0 || num_edicts <= 1 ||
	    target_edict_index <= 0 || target_edict_index > maxclients ||
	    target_edict_index >= num_edicts)
		return false;
	if (!target_inuse || !target_client || !target_live || target_noclip)
		return false;
	return expected_ctfid != 0 && current_ctfid == expected_ctfid;
}

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
