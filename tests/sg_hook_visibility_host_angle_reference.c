#include "sg_hook_visibility_feasibility_fixture.h"

#include <stdarg.h>
#include <string.h>

#include "../q_shared.h"

void Com_Printf(char *format, ...)
{
	(void)format;
}

void HookVisibilityProductionDirection(int16_t pitch, int16_t yaw,
	float forward[3], float right[3])
{
	vec3_t angles = {0.0f, 0.0f, 0.0f};

	angles[PITCH] = (float)SHORT2ANGLE((uint16_t)pitch);
	angles[YAW] = (float)SHORT2ANGLE((uint16_t)yaw);
	AngleVectors(angles, forward, right, NULL);
}

void HookVisibilityHostReferenceAngleBits(uint16_t code,
	uint32_t *sine_bits_out, uint32_t *cosine_bits_out)
{
	float forward[3], right[3];

	HookVisibilityProductionDirection(0, (int16_t)code, forward, right);
	memcpy(sine_bits_out, &forward[1], sizeof(*sine_bits_out));
	memcpy(cosine_bits_out, &forward[0], sizeof(*cosine_bits_out));
}
