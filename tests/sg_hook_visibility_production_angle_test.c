#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_hook_visibility_feasibility_internal.h"
#include "sg_hook_visibility_feasibility_fixture.h"

static uint32_t Bits(float value)
{
	uint32_t result;

	memcpy(&result, &value, sizeof(result));
	return result;
}

static int CompareDirection(int16_t pitch, int16_t yaw, uint32_t code,
	const char *axis)
{
	float host_forward[3], host_right[3];
	float candidate_forward[3], candidate_right[3];

	HookVisibilityProductionDirection(pitch, yaw, host_forward, host_right);
	SG_HookVisibilityFeasibilityDirection(pitch, yaw, candidate_forward,
		candidate_right);
	if (memcmp(host_forward, candidate_forward, sizeof(host_forward)) == 0 &&
		memcmp(host_right, candidate_right, sizeof(host_right)) == 0)
		return 1;
	fprintf(stderr,
		"%s code %u mismatch: host=%08x,%08x,%08x/%08x,%08x,%08x "
		"candidate=%08x,%08x,%08x/%08x,%08x,%08x\n",
		axis, code, Bits(host_forward[0]), Bits(host_forward[1]),
		Bits(host_forward[2]), Bits(host_right[0]), Bits(host_right[1]),
		Bits(host_right[2]), Bits(candidate_forward[0]),
		Bits(candidate_forward[1]), Bits(candidate_forward[2]),
		Bits(candidate_right[0]), Bits(candidate_right[1]),
		Bits(candidate_right[2]));
	return 0;
}

int main(void)
{
	uint32_t code;

	for (code = 0U; code <= UINT16_MAX; code++)
	{
		if (!CompareDirection(0, (int16_t)(uint16_t)code, code, "yaw") ||
			!CompareDirection((int16_t)(uint16_t)code, 0, code, "pitch"))
			return 1;
	}
	puts("production AngleVectors matched all 65536 pitch and yaw codes");
	return 0;
}
