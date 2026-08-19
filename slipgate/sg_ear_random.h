/* Team-local tactical hearing randomness and stable listener selection. */
#ifndef SG_EAR_RANDOM_H
#define SG_EAR_RANDOM_H

#include <math.h>
#include <stdint.h>

static inline uint32_t SG_EarRandomNext(uint32_t state)
{
	if (state == 0)
		state = UINT32_C(0xa511e9b3);
	state ^= state >> 16;
	state *= UINT32_C(0x7feb352d);
	state ^= state >> 15;
	state *= UINT32_C(0x846ca68b);
	state ^= state >> 16;
	return state ? state : UINT32_C(0x1b56c4e9);
}

static inline uint32_t SG_EarRandomInitial(unsigned team_index)
{
	return SG_EarRandomNext((team_index + 1u) * UINT32_C(0x9e3779b9) ^
	    UINT32_C(0x85ebca6b));
}

static inline float SG_EarRandomSigned(uint32_t draw)
{
	float unit = (float)(draw & UINT32_C(0x00ffffff)) / 16777216.0f;

	return unit * 2.0f - 1.0f;
}

/* A shared team belief uses the most accurate audible observation.  Client
 * number is only a deterministic tie-breaker; it never outranks fidelity. */
static inline int SG_EarCandidateBetter(float candidate_fraction,
	int candidate_client, int have_best, float best_fraction,
	int best_client)
{
	if (!isfinite(candidate_fraction) || candidate_fraction < 0.0f ||
	    candidate_fraction > 1.0f || candidate_client < 0)
		return 0;
	if (!have_best)
		return 1;
	if (!isfinite(best_fraction) || best_fraction < 0.0f ||
	    best_fraction > 1.0f || best_client < 0)
		return 1;
	if (candidate_fraction < best_fraction)
		return 1;
	return candidate_fraction == best_fraction &&
	    candidate_client < best_client;
}

#endif /* SG_EAR_RANDOM_H */
