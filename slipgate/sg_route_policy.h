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

/* An attacker without physical touch authority must not make standing still
 * the free alternative to a proved ordinary descent.  The final field band is
 * not itself a touch proof: it can end at a seed still a body-length or a wall
 * corner from the item.  This fallback is deliberately RUN-only: mechanisms,
 * jumps, drops, swims, and hooks retain their exact controller admission and
 * may never be forced by a pricing fallback.  The later terminal-touch stage
 * remains authoritative and clears this ordinary link when contact is real. */
static inline int SG_AttackDescentFallbackAllowed(int attack_role, int run_link,
	int current_goal_ms, int candidate_goal_ms, int field_infinite)
{
	if ((attack_role != 0 && attack_role != 1) ||
	    (run_link != 0 && run_link != 1) || !attack_role || !run_link ||
	    field_infinite <= 0 || current_goal_ms <= 0 ||
	    current_goal_ms >= field_infinite ||
	    candidate_goal_ms < 0 || candidate_goal_ms >= current_goal_ms)
		return 0;
	return 1;
}

/* Incumbent and candidate seeds must be compared on the same combat surface.
 * Forward-pressure policy removes duel range control for an attacker pressing
 * the enemy objective and for a carrier fleeing home.  Applying that removal
 * only to candidates makes the current seed artificially expensive and turns
 * an otherwise stable route into gratuitous link changes. */
static inline int SG_DuelRoutePriceAllowed(int duel, int enemy_pressure,
	int press_enabled, int carrying, int carry_press_enabled)
{
	if ((duel != 0 && duel != 1) ||
	    (enemy_pressure != 0 && enemy_pressure != 1) ||
	    (press_enabled != 0 && press_enabled != 1) ||
	    (carrying != 0 && carrying != 1) ||
	    (carry_press_enabled != 0 && carry_press_enabled != 1))
		return 0;
	return duel && !(enemy_pressure && press_enabled) &&
	       !(carrying && carry_press_enabled);
}

#endif /* SG_ROUTE_POLICY_H */
