/* Stable per-client-life route pricing without process-global RNG state. */
#ifndef SG_ROUTE_JITTER_H
#define SG_ROUTE_JITTER_H

#include <stdint.h>

static inline uint32_t SG_RouteJitterMix(uint32_t value)
{
	if (value == 0)
		value = UINT32_C(0x6d2b79f5);
	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	value *= UINT32_C(0x846ca68b);
	value ^= value >> 16;
	return value ? value : UINT32_C(0x27d4eb2d);
}

/* Instance, client slot, life epoch and objective-leg epoch are independent
 * pieces of identity.  In particular, life=1/leg=0 must not collapse onto
 * life=0/leg=1, and a reused SG slot must not replay its former owner's map
 * opinion after BotSlot_Reset clears the two counters. */
static inline uint32_t SG_RouteJitterIdentity(uint64_t instance_token,
	unsigned client_slot, unsigned life_epoch, unsigned leg_epoch)
{
	uint32_t value = (uint32_t)instance_token ^
	    SG_RouteJitterMix((uint32_t)(instance_token >> 32)) ^
	    (client_slot + 1u) * UINT32_C(0x9e3779b9) ^
	    (life_epoch + 1u) * UINT32_C(0x85ebca6b) ^
	    (leg_epoch + 1u) * UINT32_C(0xc2b2ae35);

	return SG_RouteJitterMix(value);
}

static inline unsigned SG_RouteJitterDraw(uint64_t instance_token,
	unsigned client_slot, unsigned life_epoch, unsigned leg_epoch,
	unsigned link_index)
{
	uint32_t value = SG_RouteJitterIdentity(instance_token, client_slot,
	    life_epoch, leg_epoch) ^
	    (link_index + 1u) * UINT32_C(0x27d4eb2d);

	return (SG_RouteJitterMix(value) >> 4) & 1023u;
}

#endif /* SG_ROUTE_JITTER_H */
