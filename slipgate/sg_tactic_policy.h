#ifndef SG_TACTIC_POLICY_H
#define SG_TACTIC_POLICY_H

#include <stdint.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_field_key.h"

typedef struct
{
	qboolean topology_current;
	int tactic_seed;
	uint64_t cached_strategy_activation;
	uint64_t current_strategy_activation;
	sg_field_key_t cached_goal;
	sg_field_key_t current_goal;
	float committed_at;
	float now;
	int route_cost;
} sg_tactic_cache_t;

static inline qboolean SG_TacticCacheNeedsRefresh(
	const sg_tactic_cache_t *cache)
{
	return !cache->topology_current || cache->tactic_seed < 0 ||
	    cache->cached_strategy_activation !=
	        cache->current_strategy_activation ||
	    !SG_FieldKeyMatches(cache->cached_goal, cache->current_goal) ||
	    cache->committed_at > cache->now ||
	    cache->now - cache->committed_at > 10.0f ||
	    cache->route_cost >= SG_FIELD_INF || cache->route_cost < 300;
}

#endif
