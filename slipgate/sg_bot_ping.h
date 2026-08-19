/* Human-looking scoreboard ping without consuming gameplay randomness. */
#ifndef SG_BOT_PING_H
#define SG_BOT_PING_H

#include <stdint.h>

static inline int SG_BotPingValue(int base, uint64_t instance_token,
	int frame_number)
{
	uint32_t value = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    ((uint32_t)frame_number + 1u) * UINT32_C(0x9e3779b9);
	int ping;

	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	ping = base + (int)(value % 3u) - 1;
	if (ping < 5)
		return 5;
	if (ping > 15)
		return 15;
	return ping;
}

#endif /* SG_BOT_PING_H */
