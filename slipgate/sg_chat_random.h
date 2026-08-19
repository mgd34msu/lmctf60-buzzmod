/* Private per-client randomness for chat and radio texture. */
#ifndef SG_CHAT_RANDOM_H
#define SG_CHAT_RANDOM_H

#include <stdint.h>

static inline uint32_t SG_ChatRandomNext(uint32_t state)
{
	if (state == 0)
		state = UINT32_C(0x91e10da5);
	state ^= state >> 16;
	state *= UINT32_C(0x7feb352d);
	state ^= state >> 15;
	state *= UINT32_C(0x846ca68b);
	state ^= state >> 16;
	return state ? state : UINT32_C(0x6d2b79f5);
}

static inline uint32_t SG_ChatRandomInitial(uint64_t client_identity,
	unsigned client_index)
{
	uint64_t mixed = client_identity ^
	    ((uint64_t)(client_index + 1u) * UINT64_C(0x9e3779b97f4a7c15));

	mixed ^= mixed >> 30;
	mixed *= UINT64_C(0xbf58476d1ce4e5b9);
	mixed ^= mixed >> 27;
	mixed *= UINT64_C(0x94d049bb133111eb);
	mixed ^= mixed >> 31;
	return SG_ChatRandomNext((uint32_t)(mixed ^ (mixed >> 32)));
}

static inline float SG_ChatRandomUnit(uint32_t draw)
{
	return (float)(draw & UINT32_C(0x00ffffff)) / 16777216.0f;
}

#endif /* SG_CHAT_RANDOM_H */
