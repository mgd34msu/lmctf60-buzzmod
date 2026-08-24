/* Local graph admission shared by body and objective-field localization. */
#ifndef SG_LOCALIZATION_H
#define SG_LOCALIZATION_H

#include "g_local.h"
#include "slipgate/sg_local.h"

static inline float SG_LocalSeedScore(const rune_t *r, const int *field,
	int seed, float dx, float dy, float dz)
{
	if (!r || !r->seeds || seed < 0 || seed >= r->hdr.num_seeds ||
	    dz > 96.0f || dz < -96.0f ||
	    dx * dx + dy * dy > 128.0f * 128.0f ||
	    (field && (field[seed] < 0 || field[seed] >= SG_FIELD_INF)) ||
	    (r->seeds[seed].flags & RSF_TOMBSTONE) ||
	    (r->linked_seed && !r->linked_seed[seed] &&
	     !(r->artifact.route_contract == RUNE_ROUTE_CONTRACT_LOCAL_ONLY &&
	       (r->seeds[seed].flags & RSF_OBJECTIVE))))
		return -1.0f;
	return dx * dx + dy * dy + dz * dz * 0.25f;
}

/* LOCAL_ONLY objective roots may be terminal graph sinks.  Resolve a stand
 * against the authenticated objective markers directly, without the world
 * trace used for ordinary body localization.  More than one marker inside the
 * local admission window is ambiguous and therefore rejected. */
static inline int SG_LocalObjectiveSeed(const rune_t *r, const vec3_t point)
{
	int candidate = -1;
	int seed;

	if (!r || !point ||
	    r->artifact.route_contract != RUNE_ROUTE_CONTRACT_LOCAL_ONLY)
		return -1;
	for (seed = 0; seed < r->hdr.num_seeds; seed++)
	{
		vec3_t delta;

		if (!(r->seeds[seed].flags & RSF_OBJECTIVE))
			continue;
		VectorSubtract(r->seeds[seed].origin, point, delta);
		if (SG_LocalSeedScore(r, NULL, seed, delta[0], delta[1],
		    delta[2]) < 0.0f)
			continue;
		if (candidate >= 0)
			return -1;
		candidate = seed;
	}
	return candidate;
}

#endif /* SG_LOCALIZATION_H */
