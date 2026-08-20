/* Complete-route selection for dynamic field projections. */
#ifndef SG_FIELD_PROJECTION_H
#define SG_FIELD_PROJECTION_H

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_route_policy.h"

static inline int SG_FieldProjectionLinkCostMs(const rune_t *r,
	const int *home, int seed, int link_index)
{
	const rune_link_t *link;
	int candidate;

	if (!r || !r->links || !home || seed < 0 ||
	    seed >= r->hdr.num_seeds || link_index < 0 ||
	    link_index >= r->hdr.num_links || home[seed] < 0 ||
	    home[seed] >= SG_FIELD_INF)
		return SG_FIELD_INF;
	link = &r->links[link_index];
	if (link->to < 0 || link->to >= r->hdr.num_seeds ||
	    !Fields_ActionAdmitted(link->action) || home[link->to] < 0 ||
	    home[link->to] >= home[seed])
		return SG_FIELD_INF;
	candidate = SG_RouteCandidateGoalMs(home[link->to],
	    Fields_LinkTraversalCostMs(link), SG_FIELD_INF);
	return candidate <= home[seed] ? candidate : SG_FIELD_INF;
}

static inline int SG_FieldProjectionStep(const rune_t *r,
	const int *home, int seed)
{
	int best = -1;
	int best_ms = SG_FIELD_INF;
	int li;

	if (!r || !r->first_link || !r->next_link || seed < 0 ||
	    seed >= r->hdr.num_seeds)
		return -1;

	for (li = r->first_link[seed]; li >= 0; li = r->next_link[li])
	{
		int candidate_ms = SG_FieldProjectionLinkCostMs(r, home, seed, li);

		if (candidate_ms < best_ms)
		{
			best_ms = candidate_ms;
			best = r->links[li].to;
		}
	}
	return best;
}

static inline int SG_FieldProjectionSteps(const rune_t *r,
	const int *home, int seed, int *out)
{
	int cost[SG_PROJ_BRANCH];
	int count = 0;
	int li;

	if (!r || !r->first_link || !r->next_link || !out || seed < 0 ||
	    seed >= r->hdr.num_seeds)
		return 0;
	for (li = r->first_link[seed]; li >= 0; li = r->next_link[li])
	{
		int candidate = SG_FieldProjectionLinkCostMs(r, home, seed, li);
		int destination = r->links[li].to;
		int i;

		if (candidate >= SG_FIELD_INF)
			continue;
		for (i = 0; i < count && out[i] != destination; i++)
			;
		if (i < count)
		{
			int j;

			if (candidate >= cost[i])
				continue;
			for (j = i; j + 1 < count; j++)
			{
				out[j] = out[j + 1];
				cost[j] = cost[j + 1];
			}
			count--;
		}
		for (i = 0; i < count && cost[i] <= candidate; i++)
			;
		if (i < SG_PROJ_BRANCH)
		{
			int j;
			int end = count < SG_PROJ_BRANCH ? count : SG_PROJ_BRANCH - 1;

			for (j = end; j > i; j--)
			{
				out[j] = out[j - 1];
				cost[j] = cost[j - 1];
			}
			out[i] = destination;
			cost[i] = candidate;
			if (count < SG_PROJ_BRANCH)
				count++;
		}
	}
	return count;
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
		seed = SG_FieldProjectionStep(r, home, seed);
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
