#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_localization.h"

static int Rune_NearestFieldSeedMatching(const rune_t *r, const vec3_t p,
	const int *field, int required_value, qboolean match_value)
{
	/* A seed is a local topology sample, not a global Voronoi label. Beyond two
	 * lattice steps the body may be in an intentionally omitted region. */
	int seed;
	int best = -1;
	float best_distance = 1e30f;

	if (!r || !r->seeds || !sg_host.trace)
		return -1;
	for (seed = 0; seed < r->hdr.num_seeds; seed++)
	{
		vec3_t delta;
		vec3_t from;
		vec3_t to;
		float distance;
		trace_t trace;

		VectorSubtract(r->seeds[seed].origin, p, delta);
		distance = SG_LocalSeedScore(r, field, seed, delta[0], delta[1],
		    delta[2]);
		if (distance < 0.0f || distance >= best_distance ||
		    (match_value && field[seed] != required_value))
			continue;
		VectorCopy(p, from);
		VectorCopy(r->seeds[seed].origin, to);
		from[2] += 16.0f;
		to[2] += 16.0f;
		trace = sg_host.trace(from, NULL, NULL, to, NULL, MASK_DEADSOLID);
		if (trace.startsolid || trace.fraction < 1.0f)
			continue;
		best_distance = distance;
		best = seed;
	}
	return best;
}

int Rune_NearestFieldSeed(rune_t *r, vec3_t p, const int *field)
{
	return Rune_NearestFieldSeedMatching(r, p, field, 0, false);
}

int Rune_NearestFieldMinimumSeed(const rune_t *r, const vec3_t p,
	const int *field)
{
	int minimum = SG_FIELD_INF;
	int seed;

	if (!r || !r->seeds || !field)
		return -1;
	for (seed = 0; seed < r->hdr.num_seeds; seed++)
		if (field[seed] >= 0 && field[seed] < minimum)
			minimum = field[seed];
	if (minimum >= SG_FIELD_INF)
		return -1;
	return Rune_NearestFieldSeedMatching(r, p, field, minimum, true);
}

int Rune_NearestSeed(rune_t *r, vec3_t p)
{
	return Rune_NearestFieldSeed(r, p, NULL);
}
