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

/* A stolen-flag exit is sampled once from the human prior.  Its draw belongs
 * to one immutable SG owner and one carry edge; a recyclable edict slot is not
 * enough identity, and life/mission epochs are not interchangeable counters. */
static inline uint32_t SG_EscapePriorDraw(uint64_t instance_token,
	unsigned client_slot, unsigned life_epoch, unsigned leg_epoch,
	unsigned level_tick)
{
	uint32_t state = SG_EscapeRandomInitial(instance_token, client_slot);

	state ^= (life_epoch + 1u) * UINT32_C(0x85ebca6b);
	state ^= (leg_epoch + 1u) * UINT32_C(0xc2b2ae35);
	state ^= (level_tick + 1u) * UINT32_C(0x9e3779b9);
	return SG_EscapeRandomNext(state);
}

#endif /* SG_ESCAPE_RANDOM_H */
