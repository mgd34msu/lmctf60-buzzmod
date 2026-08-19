/* Small fail-closed route policies shared by the live descent and host tests. */
#ifndef SG_ROUTE_POLICY_H
#define SG_ROUTE_POLICY_H

#include <math.h>

/* The adopted immediate-return surcharge is a choice among routes, not a
 * permission to stop. Never tax the only finite neighbor: in a one-exit
 * corridor the reverse edge is the route forward. */
static inline int SG_RouteReturnPenaltyAllowed(int previous_seed,
	int candidate_seed, int previous_recent, int finite_neighbor_count,
	float configured_percent)
{
	if (previous_seed < 0 || candidate_seed < 0 ||
	    (previous_recent != 0 && previous_recent != 1) ||
	    finite_neighbor_count < 2 || !isfinite(configured_percent) ||
	    !(configured_percent > 0.0f))
		return 0;
	return previous_recent && candidate_seed == previous_seed;
}

/* An attacker outside the final room must not make standing still the free
 * alternative to a proved ordinary descent.  This fallback is deliberately
 * RUN-only: mechanisms, jumps, drops, swims, and hooks retain their exact
 * controller admission and may never be forced by a pricing fallback. */
static inline int SG_AttackDescentFallbackAllowed(int attack_role, int run_link,
	int current_goal_ms, int candidate_goal_ms, int field_infinite)
{
	if ((attack_role != 0 && attack_role != 1) ||
	    (run_link != 0 && run_link != 1) || !attack_role || !run_link ||
	    field_infinite <= 600 || current_goal_ms <= 600 ||
	    current_goal_ms >= field_infinite ||
	    candidate_goal_ms < 0 || candidate_goal_ms >= current_goal_ms)
		return 0;
	return 1;
}

#endif /* SG_ROUTE_POLICY_H */
