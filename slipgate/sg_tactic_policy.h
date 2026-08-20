#ifndef SG_TACTIC_POLICY_H
#define SG_TACTIC_POLICY_H

#include "g_local.h"
#include "slipgate/sg_local.h"

typedef struct
{
	const int *field;
	int minimum_seed;
} sg_tactic_goal_t;

typedef struct
{
	qboolean topology_current;
	int tactic_seed;
	int cached_role;
	int current_role;
	sg_tactic_goal_t cached_goal;
	sg_tactic_goal_t current_goal;
	float committed_at;
	float now;
	int route_cost;
} sg_tactic_cache_t;

static inline qboolean SG_TacticCacheNeedsRefresh(
	const sg_tactic_cache_t *cache)
{
	return !cache->topology_current || cache->tactic_seed < 0 ||
	    cache->cached_role != cache->current_role ||
	    cache->cached_goal.field != cache->current_goal.field ||
	    cache->cached_goal.minimum_seed != cache->current_goal.minimum_seed ||
	    cache->committed_at > cache->now ||
	    cache->now - cache->committed_at > 10.0f ||
	    cache->route_cost >= SG_FIELD_INF || cache->route_cost < 300;
}

#endif
