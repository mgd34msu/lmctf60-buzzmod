/* Small fail-closed route policies shared by the live descent and host tests. */
#ifndef SG_ROUTE_POLICY_H
#define SG_ROUTE_POLICY_H

#include <math.h>

#define SG_ATTACK_FINAL_APPROACH_MS 2500

/* An outgoing choice owns the edge it is about to traverse. The destination
 * surface alone is only the suffix and can make a slow edge look free. */
static inline float SG_RouteCandidatePrice(float destination_price,
	int traversal_ms, float objective_weight)
{
	float price;

	if (!isfinite(destination_price) || traversal_ms < 0 ||
	    !isfinite(objective_weight) || objective_weight < 0.0f)
		return INFINITY;
	price = destination_price + (float)traversal_ms * objective_weight;
	return isfinite(price) ? price : INFINITY;
}

static inline int SG_RouteCandidateGoalMs(int destination_ms,
	int traversal_ms, int infinity)
{
	if (infinity <= 0 || destination_ms < 0 || destination_ms >= infinity ||
	    traversal_ms < 0 || traversal_ms >= infinity - destination_ms)
		return infinity;
	return destination_ms + traversal_ms;
}

static inline int SG_RouteCandidateDescends(int current_ms,
	int destination_ms, int traversal_ms, int infinity)
{
	return current_ms >= 0 && current_ms < infinity &&
	    SG_RouteCandidateGoalMs(destination_ms, traversal_ms, infinity) <
	        current_ms;
}

/* Carriers and flag-touch runners own an immediate contact objective. Their
 * route may bend for proved traversal, but not for cosmetic lateral texture. */
static inline int SG_RouteRibbonAllowed(int carrying, int flag_touch_mission)
{
	if ((carrying != 0 && carrying != 1) ||
	    (flag_touch_mission != 0 && flag_touch_mission != 1))
		return 0;
	return !carrying && !flag_touch_mission;
}

/* The immediate-return surcharge chooses among equally useful organic routes;
 * it cannot spend progress or override a pure route.  A merely finite
 * alternative can point uphill, so require two non-worsening neighbors.  A
 * reverse edge that alone holds or reduces field cost is the route forward,
 * even when some worse finite edge also exists. */
static inline int SG_RouteReturnPenaltyAllowed(int previous_seed,
	int candidate_seed, int previous_recent,
	int nonworsening_neighbor_count, int route_pure,
	float configured_percent)
{
	if (previous_seed < 0 || candidate_seed < 0 ||
	    (previous_recent != 0 && previous_recent != 1) ||
	    (route_pure != 0 && route_pure != 1) ||
	    nonworsening_neighbor_count < 2 ||
	    !isfinite(configured_percent) ||
	    !(configured_percent > 0.0f))
		return 0;
	return !route_pure && previous_recent && candidate_seed == previous_seed;
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

static inline int SG_AttackDescentOverrideNeeded(int attack_role,
	int current_goal_ms, int selected_goal_ms, int field_infinite)
{
	if ((attack_role != 0 && attack_role != 1) || !attack_role ||
	    field_infinite <= SG_ATTACK_FINAL_APPROACH_MS ||
	    current_goal_ms <= SG_ATTACK_FINAL_APPROACH_MS ||
	    current_goal_ms >= field_infinite || selected_goal_ms < 0 ||
	    selected_goal_ms > field_infinite)
		return 0;
	return selected_goal_ms >= current_goal_ms;
}

/* Near-goal hook suppression is an optimization, not route authority. */
static inline int SG_HookNearGoalSkipAllowed(int hook_policy, int carrying,
	int descending_run_available, int current_goal_ms, int field_infinite)
{
	if ((hook_policy != 0 && hook_policy != 1) ||
	    (carrying != 0 && carrying != 1) ||
	    (descending_run_available != 0 && descending_run_available != 1) ||
	    field_infinite <= 0 || current_goal_ms < 0 ||
	    current_goal_ms >= field_infinite)
		return 0;
	return hook_policy && !carrying && descending_run_available &&
	       current_goal_ms < 600;
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
