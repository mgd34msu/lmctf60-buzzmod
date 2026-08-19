/* Per-bot lateral-lane variation with no process-global RNG side effects. */
#ifndef SG_RIBBON_RANDOM_H
#define SG_RIBBON_RANDOM_H

#include <stdint.h>

static inline uint32_t SG_RibbonRandomNext(uint32_t state)
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

static inline uint32_t SG_RibbonRandomInitial(uint64_t instance_token,
	unsigned client_slot)
{
	uint32_t state = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    (client_slot + 1u) * UINT32_C(0xc2b2ae35);

	return SG_RibbonRandomNext(state);
}

static inline float SG_RibbonRandomOffset(uint32_t draw, float width)
{
	return ((float)(draw % 2001u) / 1000.0f - 1.0f) * width;
}

static inline float SG_RibbonRandomInterval(uint32_t draw)
{
	return 1.0f + (float)(draw % 100u) / 100.0f;
}

#endif /* SG_RIBBON_RANDOM_H */
