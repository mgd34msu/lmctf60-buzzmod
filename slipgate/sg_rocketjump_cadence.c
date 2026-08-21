#include "slipgate/sg_rocketjump_cadence.h"

#include <limits.h>
#include <math.h>
#include <string.h>

int SG_RocketJumpCadenceBegin(sg_rocketjump_cadence_t *cadence,
	float flight_ms, int server_frame_ms)
{
	double frames;

	if (!cadence)
		return 0;
	memset(cadence, 0, sizeof(*cadence));
	if (!isfinite(flight_ms) || flight_ms <= 0.0f || server_frame_ms <= 0)
		return 0;
	frames = ceil((double)flight_ms / (double)server_frame_ms);
	if (frames < 1.0 || frames > (double)INT_MAX)
		return 0;
	cadence->flight_frames = (int)frames;
	cadence->post_launch_steps = SG_ROCKETJUMP_CADENCE_BODY_STEPS - 1;
	return 1;
}

sg_rocketjump_cadence_event_t SG_RocketJumpCadenceNext(
	sg_rocketjump_cadence_t *cadence)
{
	if (!cadence || cadence->flight_frames <= 0)
		return SG_ROCKETJUMP_CADENCE_DONE;
	if (cadence->post_launch_steps > 0)
	{
		cadence->post_launch_steps--;
		return SG_ROCKETJUMP_CADENCE_BODY_STEP;
	}
	if (cadence->body_steps_pending > 0)
	{
		cadence->body_steps_pending--;
		return SG_ROCKETJUMP_CADENCE_BODY_STEP;
	}
	if (cadence->projectile_frame >= cadence->flight_frames)
		return SG_ROCKETJUMP_CADENCE_DONE;
	cadence->projectile_frame++;
	cadence->body_steps_pending = SG_ROCKETJUMP_CADENCE_BODY_STEPS;
	if (cadence->projectile_frame == cadence->flight_frames)
		return SG_ROCKETJUMP_CADENCE_IMPACT;
	return SG_ROCKETJUMP_CADENCE_PROJECTILE_FRAME;
}
