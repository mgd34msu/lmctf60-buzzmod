#include "sg_hook_visibility_feasibility_fixture.h"

#include <math.h>
#include <string.h>

#define HOST_REFERENCE_PI 3.14159265358979323846

void HookVisibilityHostReferenceAngleBits(uint16_t code,
	uint32_t *sine_bits_out, uint32_t *cosine_bits_out)
{
	float degrees = (float)((double)code * (360.0 / 65536.0));
	float angle = (float)((double)degrees *
		(HOST_REFERENCE_PI * 2.0 / 360.0));
	float sine = (float)sin((double)angle);
	float cosine = (float)cos((double)angle);

	memcpy(sine_bits_out, &sine, sizeof(*sine_bits_out));
	memcpy(cosine_bits_out, &cosine, sizeof(*cosine_bits_out));
}
