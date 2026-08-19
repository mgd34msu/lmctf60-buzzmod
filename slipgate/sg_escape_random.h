/* Per-bot pocket escape variation with no process-global RNG side effects. */
#ifndef SG_ESCAPE_RANDOM_H
#define SG_ESCAPE_RANDOM_H

#include <stdint.h>

static inline uint32_t SG_EscapeRandomNext(uint32_t state)
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

static inline uint32_t SG_EscapeRandomInitial(uint64_t instance_token,
	unsigned client_slot)
{
	uint32_t state = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    (client_slot + 1u) * UINT32_C(0x27d4eb2d);

	return SG_EscapeRandomNext(state);
}

static inline int SG_EscapeRandomYaw(uint32_t draw)
{
	return (int)(draw % 81u) - 40;
}

static inline float SG_EscapeRandomDuration(uint32_t draw)
{
	return 1.0f + (float)(draw % 9u) * 0.1f;
}

#endif /* SG_ESCAPE_RANDOM_H */
