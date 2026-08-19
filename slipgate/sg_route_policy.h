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

#endif /* SG_ROUTE_POLICY_H */
