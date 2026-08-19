/* sg_role_policy.h -- pure roster laws shared by role assignment and tests. */
#ifndef SG_ROLE_POLICY_H
#define SG_ROLE_POLICY_H

static inline int SG_CoordinationBodyLive(int active, int inuse, int deadflag,
	int health)
{
	return active && inuse && !deadflag && health > 0;
}

/* Rank one slot among the currently live same-team bodies.  A dead/missing
 * self ranks after every live body, so it cannot reserve a live defender post
 * while waiting to respawn. */
static inline int SG_RoleLiveRank(const unsigned char *eligible, int count,
	int self, int *live_count)
{
	int rank = 0;
	int live = 0;
	int index;

	if (live_count)
		*live_count = 0;
	if (!eligible || count <= 0 || self < 0 || self >= count)
		return -1;
	for (index = 0; index < count; index++)
	{
		if (index == self)
			rank = live;
		if (!eligible[index])
			continue;
		live++;
	}
	if (!eligible[self])
		rank = live;
	if (live_count)
		*live_count = live;
	return rank;
}

static inline int SG_RoleOutsideDefenderQuota(const unsigned char *eligible,
	int count, int slot, int defenders_wanted)
{
	int live_count;
	int rank;

	if (!eligible || count <= 0 || slot < 0 || slot >= count ||
	    defenders_wanted < 0 || !eligible[slot])
		return 0;
	rank = SG_RoleLiveRank(eligible, count, slot, &live_count);
	return rank >= defenders_wanted && rank < live_count;
}

/* Near-goal defenders and escorts are intentionally stationed.  A human
 * escort's exact terminal hold remains authoritative even when its fallback
 * graph cost is not near the ordered teammate. */
static inline int SG_RoleMissionHold(int role, int goal_cost,
	int ordered_escort_terminal)
{
	if (role == SG_ROLE_ESCORT && ordered_escort_terminal)
		return 1;
	return (role == SG_ROLE_DEFEND || role == SG_ROLE_ESCORT) &&
	       goal_cost >= 0 && goal_cost < 1500;
}

/* The generic wedge clock measures navigation deadlock, not a stationary
 * firefight.  A current retained duel or combat ownership from the preceding
 * frame is positive activity even when the body has not translated. */
static inline int SG_WedgeClockReset(float displacement, int duel,
	int engaged_last)
{
	return displacement > 96.0f || duel || engaged_last;
}

/* Route-failure clocks may judge only navigation-owned motion.  Mission holds
 * and combat-owned frames cannot prove that the selected graph link failed. */
static inline int SG_RouteFailureWatchSuppressed(int role, int goal_cost,
	int ordered_escort_terminal, int duel, int engaged_last)
{
	return SG_RoleMissionHold(role, goal_cost, ordered_escort_terminal) ||
	       duel || engaged_last;
}

static inline int SG_RoleOwnsDefenseState(int role)
{
	return role == SG_ROLE_DEFEND;
}

#endif /* SG_ROLE_POLICY_H */
