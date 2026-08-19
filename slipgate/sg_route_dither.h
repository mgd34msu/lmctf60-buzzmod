/* Deterministic per-visit tie salt with no process-global RNG side effects. */
#ifndef SG_ROUTE_DITHER_H
#define SG_ROUTE_DITHER_H

#include <stdint.h>

static inline unsigned SG_RouteDitherMix(unsigned value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value & 0x7fffffffu;
}

static inline unsigned SG_RouteDitherInitial(uint64_t instance_token,
	unsigned client_slot)
{
	unsigned value = (unsigned)instance_token ^
	    (unsigned)(instance_token >> 32) ^ (client_slot + 1u) * 0x9e3779b9u;

	return SG_RouteDitherMix(value ? value : 0x6d2b79f5u);
}

static inline unsigned SG_RouteDitherNext(unsigned prior, int from_seed,
	int to_seed)
{
	unsigned value = prior ^ ((unsigned)from_seed + 1u) * 0x85ebca6bu ^
	    ((unsigned)to_seed + 1u) * 0xc2b2ae35u;

	return SG_RouteDitherMix(value ? value : 0x27d4eb2du);
}

#endif /* SG_ROUTE_DITHER_H */
