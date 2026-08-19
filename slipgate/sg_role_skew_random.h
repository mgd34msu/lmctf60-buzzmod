/* Per-team formation drift with no process-global RNG side effects. */
#ifndef SG_ROLE_SKEW_RANDOM_H
#define SG_ROLE_SKEW_RANDOM_H

#include <stdint.h>

static inline uint32_t SG_RoleSkewRandomNext(uint32_t state)
{
	if (state == 0)
		state = UINT32_C(0x6d2b79f5);
	state ^= state >> 16;
	state *= UINT32_C(0x7feb352d);
	state ^= state >> 15;
	state *= UINT32_C(0x846ca68b);
	state ^= state >> 16;
	return state ? state : UINT32_C(0x27d4eb2d);
}

static inline uint32_t SG_RoleSkewRandomInitial(unsigned team_index)
{
	return SG_RoleSkewRandomNext(
	    (team_index + 1u) * UINT32_C(0x9e3779b9) ^ UINT32_C(0xc2b2ae35));
}

static inline int SG_RoleSkewRandomValue(uint32_t draw)
{
	return (int)(draw % 3u) - 1;
}

static inline float SG_RoleSkewRandomInterval(uint32_t draw)
{
	return 150.0f + (float)(draw % 90u);
}

#endif /* SG_ROLE_SKEW_RANDOM_H */
