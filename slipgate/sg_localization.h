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
	    (r->linked_seed && !r->linked_seed[seed]))
		return -1.0f;
	return dx * dx + dy * dy + dz * dz * 0.25f;
}

#endif /* SG_LOCALIZATION_H */
