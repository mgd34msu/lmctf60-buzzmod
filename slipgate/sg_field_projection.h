/* Complete-route selection for dynamic field projections. */
#ifndef SG_FIELD_PROJECTION_H
#define SG_FIELD_PROJECTION_H

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_route_policy.h"

static inline int SG_FieldCarrierProjectionStep(const rune_t *r,
	const int *home, int seed)
{
	int best = -1;
	int best_ms = SG_FIELD_INF;
	int li;

	for (li = r->first_link[seed]; li >= 0; li = r->next_link[li])
	{
		const rune_link_t *link = &r->links[li];
		int candidate_ms;

		if (!Fields_ActionAdmitted(link->action) ||
		    home[link->to] >= home[seed])
			continue;
		candidate_ms = SG_RouteCandidateGoalMs(home[link->to],
		    Fields_LinkTraversalCostMs(link), SG_FIELD_INF);
		if (candidate_ms < best_ms)
		{
			best_ms = candidate_ms;
			best = link->to;
		}
	}
	return best;
}

#endif /* SG_FIELD_PROJECTION_H */
