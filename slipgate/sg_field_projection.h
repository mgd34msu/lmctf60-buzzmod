/* Complete-route selection for dynamic field projections. */
#ifndef SG_FIELD_PROJECTION_H
#define SG_FIELD_PROJECTION_H

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_route_policy.h"

#define SG_CARRIER_SCREEN_LEAD_MIN_MS 900
#define SG_CARRIER_SCREEN_LEAD_MAX_MS 2200

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
	int seed;
	int hop;

	if (!r || !home || carrier_seed < 0 ||
	    carrier_seed >= r->hdr.num_seeds || home[carrier_seed] < 0 ||
	    home[carrier_seed] >= SG_FIELD_INF || cost_lo < 0 || cost_lo > cost_hi)
		return -1;
	seed = SG_FieldProjectionStep(r, home, carrier_seed);
	for (hop = 0; hop < r->hdr.num_seeds; hop++)
	{
		if (seed < 0)
			break;
		if (home[seed] >= cost_lo && home[seed] <= cost_hi)
			return seed;
		seed = SG_FieldProjectionStep(r, home, seed);
	}
	return -1;
}

static inline int SG_FieldCarrierScreenStation(const rune_t *r,
	const int *home, int carrier_seed)
{
	int carrier_goal;
	int previous = -1;
	int seed;
	int hop;

	if (!r || !home || carrier_seed < 0 ||
	    carrier_seed >= r->hdr.num_seeds || home[carrier_seed] < 0 ||
	    home[carrier_seed] >= SG_FIELD_INF)
		return -1;
	carrier_goal = home[carrier_seed];
	seed = SG_FieldProjectionStep(r, home, carrier_seed);
	for (hop = 0; hop < r->hdr.num_seeds && seed >= 0; hop++)
	{
		int lead_ms = carrier_goal - home[seed];

		if (lead_ms >= SG_CARRIER_SCREEN_LEAD_MIN_MS)
			return lead_ms <= SG_CARRIER_SCREEN_LEAD_MAX_MS || previous < 0
			    ? seed : previous;
		previous = seed;
		seed = SG_FieldProjectionStep(r, home, seed);
	}
	return previous;
}

static inline int SG_FieldCarrierSupportRoot(const rune_t *r,
	const int *home, int carrier_known, int carrier_seed)
{
	int station;

	if (!r || !home || (carrier_known != 0 && carrier_known != 1) ||
	    !carrier_known || carrier_seed < 0 ||
	    carrier_seed >= r->hdr.num_seeds)
		return -1;
	station = SG_FieldCarrierScreenStation(r, home, carrier_seed);
	return station >= 0 ? station : carrier_seed;
}

static inline int SG_FieldIncomingRunStep(const rune_t *r,
	const int *home, int seed)
{
	int best = -1;
	int best_ms = SG_FIELD_INF;
	int link_index;

	if (!r || !r->links || !home || seed < 0 ||
	    seed >= r->hdr.num_seeds || home[seed] < 0 ||
	    home[seed] >= SG_FIELD_INF)
		return -1;
	for (link_index = 0; link_index < r->hdr.num_links; link_index++)
	{
		const rune_link_t *link = &r->links[link_index];
		int candidate;

		if (link->action != RL_RUN || link->to != seed || link->from < 0 ||
		    link->from >= r->hdr.num_seeds || home[link->from] <= home[seed])
			continue;
		candidate = SG_FieldProjectionLinkCostMs(r, home, link->from,
		    link_index);
		if (candidate != home[link->from])
			continue;
		if (candidate < best_ms ||
		    (candidate == best_ms && link->from < best))
		{
			best = link->from;
			best_ms = candidate;
		}
	}
	return best;
}

static inline int SG_FieldCarrierTrailStation(const rune_t *r,
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
		if (home[seed] > cost_hi)
			break;
		seed = SG_FieldIncomingRunStep(r, home, seed);
		if (seed < 0)
			break;
	}
	return -1;
}

#endif /* SG_FIELD_PROJECTION_H */
