/* Team/topic callout timing isolated from cosmetic chat randomness. */
#ifndef SG_CALLOUT_RANDOM_H
#define SG_CALLOUT_RANDOM_H

#include <stdint.h>

static inline uint32_t SG_CalloutRandomNext(uint32_t state)
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

static inline uint32_t SG_CalloutRandomInitial(unsigned team_index,
	unsigned topic)
{
	return SG_CalloutRandomNext((team_index + 1u) * UINT32_C(0x9e3779b9) ^
	    (topic + 1u) * UINT32_C(0xc2b2ae35));
}

static inline float SG_CalloutRandomDelay(uint32_t draw, float minimum,
	float maximum)
{
	float unit = (float)(draw & UINT32_C(0x00ffffff)) / 16777216.0f;

	return minimum + unit * (maximum - minimum);
}

#endif /* SG_CALLOUT_RANDOM_H */
