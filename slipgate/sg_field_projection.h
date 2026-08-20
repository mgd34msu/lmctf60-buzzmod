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

static inline int SG_FieldCarrierLeadStation(const rune_t *r,
	const int *home, int carrier_seed, int cost_lo, int cost_hi)
{
	int seed = carrier_seed;
	int hop;

	if (!r || !home || carrier_seed < 0 ||
	    carrier_seed >= r->hdr.num_seeds || home[carrier_seed] < 0 ||
	    home[carrier_seed] >= SG_FIELD_INF || cost_lo < 0 || cost_lo > cost_hi)
		return -1;
	for (hop = 0; hop < r->hdr.num_seeds; hop++)
	{
		if (home[seed] >= cost_lo && home[seed] <= cost_hi)
			return seed;
		seed = SG_FieldCarrierProjectionStep(r, home, seed);
		if (seed < 0)
			break;
	}
	return -1;
}

static inline int SG_FieldNearestBandSeed(const rune_t *r, const int *field,
	int origin_seed, int cost_lo, int cost_hi)
{
	int best = -1;
	float best_distance = -1.0f;
	int seed;

	if (!r || !r->seeds || !field || origin_seed < 0 ||
	    origin_seed >= r->hdr.num_seeds || field[origin_seed] < 0 ||
	    field[origin_seed] >= SG_FIELD_INF || cost_lo < 0 || cost_lo > cost_hi)
		return -1;
	for (seed = 0; seed < r->hdr.num_seeds && seed < SG_MAX_SEEDS; seed++)
	{
		vec3_t delta;
		float distance;

		if (field[seed] < cost_lo || field[seed] > cost_hi ||
		    field[seed] >= SG_FIELD_INF)
			continue;
		VectorSubtract(r->seeds[seed].origin,
		    r->seeds[origin_seed].origin, delta);
		distance = VectorLength(delta);
		if (best_distance < 0.0f || distance < best_distance)
		{
			best_distance = distance;
			best = seed;
		}
	}
	return best;
}

#endif /* SG_FIELD_PROJECTION_H */
