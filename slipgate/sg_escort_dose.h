/* One escort decision per carry epoch, independent of process-global RNG. */
#ifndef SG_ESCORT_DOSE_H
#define SG_ESCORT_DOSE_H

#include <stdint.h>

static inline uint32_t SG_EscortDoseMix(uint32_t value)
{
	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	value *= UINT32_C(0x846ca68b);
	value ^= value >> 16;
	return value;
}

static inline int SG_EscortDoseEnabled(int team_index, int carrier_client,
	uint32_t carry_epoch, int percent)
{
	uint32_t value;

	if (percent <= 0)
		return 0;
	if (percent >= 100)
		return 1;
	value = ((uint32_t)team_index + 1u) * UINT32_C(0x9e3779b9) ^
	    ((uint32_t)carrier_client + 1u) * UINT32_C(0x85ebca6b) ^
	    (carry_epoch + 1u) * UINT32_C(0xc2b2ae35);
	return (int)(SG_EscortDoseMix(value) % 100u) < percent;
}

#endif /* SG_ESCORT_DOSE_H */
