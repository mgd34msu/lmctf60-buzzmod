/* Route-bound hold stations for enemy-carrier interception. */
#ifndef SG_INTERCEPT_POLICY_H
#define SG_INTERCEPT_POLICY_H

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_field_projection.h"

static inline int SG_InterceptHoldSeed(const rune_t *r,
	const sg_proj_t *projection, const int *home, int fallback)
{
	vec3_t axis;
	float axis_length;
	float best_score = -1.0f;
	int best = -1;
	int i;

	if (!r || !r->seeds || !r->links || !r->first_link || !r->next_link ||
	    !projection || !home || projection->n < 2 ||
	    projection->n > SG_PROJ_MAX || projection->client < 0 ||
	    projection->seed[0] < 0 ||
	    projection->seed[0] >= r->hdr.num_seeds ||
	    projection->seed[projection->n - 1] < 0 ||
	    projection->seed[projection->n - 1] >= r->hdr.num_seeds)
		return fallback;
	VectorSubtract(r->seeds[projection->seed[0]].origin,
	    r->seeds[projection->seed[projection->n - 1]].origin, axis);
	axis[2] = 0.0f;
	axis_length = VectorLength(axis);
	if (axis_length < 64.0f)
		return fallback;
	axis[0] /= axis_length;
	axis[1] /= axis_length;

	for (i = 0; i < projection->n; i++)
	{
		int seed = projection->seed[i];
		int fan = 0;
		int link;
		float choke;

		if (seed < 0 || seed >= r->hdr.num_seeds)
			continue;
		for (link = r->first_link[seed]; link >= 0;
		     link = r->next_link[link])
			if (SG_FieldProjectionLinkCostMs(r, home, seed, link) <
			    SG_FIELD_INF)
				fan++;
		choke = 600.0f / (4.0f + (float)fan);

		for (link = r->first_link[seed]; link >= 0;
		     link = r->next_link[link])
		{
			vec3_t offset;
			float lateral;
			float rise;
			float score;
			int candidate;

			if (SG_FieldProjectionLinkCostMs(r, home, seed, link) >=
			    SG_FIELD_INF)
				continue;
			candidate = r->links[link].to;
			VectorSubtract(r->seeds[candidate].origin,
			    r->seeds[seed].origin, offset);
			rise = offset[2];
			offset[2] = 0.0f;
			lateral = fabsf(offset[0] * axis[1] -
			    offset[1] * axis[0]);
			if (lateral > 250.0f)
				lateral = 250.0f;
			score = lateral + choke;
			if (rise > 0.0f)
				score += rise > 200.0f ? 200.0f : rise;
			if (score > best_score)
			{
				best_score = score;
				best = candidate;
			}
		}
	}
	return best >= 0 ? best : fallback;
}

#endif /* SG_INTERCEPT_POLICY_H */
