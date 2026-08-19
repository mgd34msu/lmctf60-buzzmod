/* Human-looking scoreboard ping without consuming gameplay randomness. */
#ifndef SG_BOT_PING_H
#define SG_BOT_PING_H

#include <stdint.h>

static inline uint32_t SG_BotPingMix(uint32_t value)
{
	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	value *= UINT32_C(0x846ca68b);
	value ^= value >> 16;
	return value;
}

static inline int SG_BotPingBase(uint64_t instance_token,
	unsigned client_slot)
{
	uint32_t value = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    (client_slot + 1u) * UINT32_C(0x9e3779b9);

	return 5 + (int)(SG_BotPingMix(value) % 11u);
}

static inline int SG_BotPingValue(int base, uint64_t instance_token,
	int frame_number)
{
	uint32_t value = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    ((uint32_t)frame_number + 1u) * UINT32_C(0x9e3779b9);
	int ping;

	value = SG_BotPingMix(value);
	ping = base + (int)(value % 3u) - 1;
	if (ping < 5)
		return 5;
	if (ping > 15)
		return 15;
	return ping;
}

#endif /* SG_BOT_PING_H */
