#ifndef SG_DEFENSE_FACING_H
#define SG_DEFENSE_FACING_H

#include "g_local.h"
#include "slipgate/sg_route_policy.h"
#include "slipgate/sg_rune.h"

/* A held defender watches a corridor that can actually deliver an attacker
 * along this scoring field. Departures from the post and mechanism sources
 * elsewhere in the map are not physical approach bearings. */
static inline int SG_DefenseFacingSeed(const rune_t *rune, int post_seed,
	const int *goal_field, int infinity)
{
	int best_seed = -1;
	int best_goal = infinity;
	int link_index;
	int post_goal;

	if (!rune || !rune->links || !goal_field || infinity <= 0 ||
	    post_seed < 0 || post_seed >= rune->hdr.num_seeds)
		return -1;
	post_goal = goal_field[post_seed];
	if (post_goal < 0 || post_goal >= infinity)
		return -1;

	for (link_index = 0; link_index < rune->hdr.num_links; link_index++)
	{
		const rune_link_t *link = &rune->links[link_index];
		int upstream_goal;

		if (link->action != RL_RUN || link->to != post_seed ||
		    link->from < 0 || link->from >= rune->hdr.num_seeds ||
		    link->from == post_seed)
			continue;
		upstream_goal = goal_field[link->from];
		if (upstream_goal <= post_goal || upstream_goal >= infinity ||
		    SG_RouteCandidateGoalMs(post_goal, link->cost_ms, infinity) !=
		        upstream_goal)
			continue;
		if (upstream_goal < best_goal ||
		    (upstream_goal == best_goal && link->from < best_seed))
		{
			best_goal = upstream_goal;
			best_seed = link->from;
		}
	}
	return best_seed;
}

#endif /* SG_DEFENSE_FACING_H */
