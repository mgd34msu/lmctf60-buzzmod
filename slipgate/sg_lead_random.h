/* Per-bot item-timing jitter with no process-global RNG side effects. */
#ifndef SG_LEAD_RANDOM_H
#define SG_LEAD_RANDOM_H

#include <stdint.h>

static inline uint32_t SG_LeadRandomNext(uint32_t state)
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

static inline uint32_t SG_LeadRandomInitial(uint64_t instance_token,
	unsigned client_slot)
{
	uint32_t state = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    (client_slot + 1u) * UINT32_C(0x85ebca6b);

	return SG_LeadRandomNext(state);
}

static inline float SG_LeadRandomUnit(uint32_t draw)
{
	return (float)(draw & UINT32_C(0x00ffffff)) / 16777216.0f;
}

#endif /* SG_LEAD_RANDOM_H */
