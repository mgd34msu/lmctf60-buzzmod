/* Ordinary strafe-jump command and landing-timer adapter. */
#include "../g_local.h"
#include "sg_human_speed.h"

void SG_HumanSpeedTimerReset(sg_human_speed_timer_t *state)
{
	if (!state)
		return;
	state->remainder_ms = 0;
	state->tracking = false;
}

void SG_HumanSpeedCommandBoundary(sg_human_speed_timer_t *state,
	qboolean owned)
{
	if (!owned)
		SG_HumanSpeedTimerReset(state);
}

static qboolean SG_HumanSpeedExactLandingTimer(const pmove_state_t *pmove)
{
	const int timed = PMF_TIME_LAND | PMF_TIME_WATERJUMP |
	    PMF_TIME_TELEPORT;

	return pmove && pmove->pm_time != 0 &&
	    (pmove->pm_flags & timed) == PMF_TIME_LAND;
}

sg_human_speed_step_t SG_HumanSpeedLandingPrepare(
	sg_human_speed_timer_t *state, pmove_state_t *pmove,
	unsigned command_msec, qboolean enabled)
{
	sg_human_speed_step_t result;
	unsigned elapsed_ticks;
	unsigned stock_ticks;

	memset(&result, 0, sizeof(result));
	if (!state || !pmove || !enabled ||
	    (command_msec != 12 && command_msec != 13) ||
	    !SG_HumanSpeedExactLandingTimer(pmove))
	{
		SG_HumanSpeedTimerReset(state);
		return result;
	}

	if (!state->tracking)
	{
		state->tracking = true;
		state->remainder_ms = 0;
	}

	elapsed_ticks = (state->remainder_ms + command_msec) >> 3;
	state->remainder_ms = (state->remainder_ms + command_msec) & 7u;
	stock_ticks = command_msec >> 3;
	if (stock_ticks == 0)
		stock_ticks = 1;
	if (elapsed_ticks > stock_ticks)
		result.extra_ticks = elapsed_ticks - stock_ticks;

	if (result.extra_ticks >= pmove->pm_time)
	{
		pmove->pm_time = 0;
		pmove->pm_flags &= (byte)~PMF_TIME_LAND;
		result.expired = true;
		SG_HumanSpeedTimerReset(state);
	}
	else
		pmove->pm_time = (byte)(pmove->pm_time - result.extra_ticks);
	result.remainder_ms = state->remainder_ms;
	return result;
}

sg_human_speed_step_t SG_HumanSpeedLandingObserve(
	sg_human_speed_timer_t *state, byte flags_before,
	const pmove_state_t *pmove, unsigned command_msec, qboolean enabled)
{
	sg_human_speed_step_t result;
	const int timed = PMF_TIME_LAND | PMF_TIME_WATERJUMP |
	    PMF_TIME_TELEPORT;

	memset(&result, 0, sizeof(result));
	if (!state || !pmove || !enabled ||
	    (command_msec != 12 && command_msec != 13))
	{
		SG_HumanSpeedTimerReset(state);
		return result;
	}
	if (!SG_HumanSpeedExactLandingTimer(pmove))
	{
		SG_HumanSpeedTimerReset(state);
		return result;
	}

	if ((flags_before & timed) != PMF_TIME_LAND)
	{
		/* This command both established and paid the engine's first whole
		 * timer tick.  Preserve only its fractional 8 ms remainder. */
		state->tracking = true;
		state->remainder_ms = command_msec & 7u;
		result.began = true;
	}
	result.remainder_ms = state->remainder_ms;
	return result;
}

void SG_HumanSpeedAirCommand(usercmd_t *cmd,
	const sg_human_speed_air_t *air, const vec3_t velocity,
	float speed2d, float frametime)
{
	vec3_t basis, vf, vr, vdir, direction;
	float wishspeed = 300.0f;
	float accelspeed, cosine, theta, sine, flat;

	if (!cmd || !air || speed2d < 1.0f || frametime <= 0.0f)
		return;
	accelspeed = SG_HUMAN_SPEED_AIR_ACCEL * frametime * wishspeed;
	if (speed2d <= wishspeed - accelspeed)
		return;

	cosine = (wishspeed - accelspeed) / speed2d;
	if (cosine > 1.0f)
		cosine = 1.0f;
	if (cosine < -1.0f)
		cosine = -1.0f;
	theta = acosf(cosine) * air->lean;

	vdir[0] = velocity[0] / speed2d;
	vdir[1] = velocity[1] / speed2d;
	vdir[2] = 0.0f;
	sine = sinf(theta);
	cosine = cosf(theta);
	direction[0] = vdir[0] * cosine - vdir[1] * sine;
	direction[1] = vdir[0] * sine + vdir[1] * cosine;
	direction[2] = 0.0f;

	basis[YAW] = air->view_yaw;
	basis[PITCH] = air->view_pitch;
	if (basis[PITCH] > 180.0f)
		basis[PITCH] -= 360.0f;
	basis[PITCH] /= 3.0f;
	basis[ROLL] = 0.0f;
	AngleVectors(basis, vf, vr, NULL);
	flat = sqrtf(vf[0] * vf[0] + vf[1] * vf[1]);
	if (flat < 0.01f)
		return;
	cmd->forwardmove = (short)(400.0f *
	    (direction[0] * vf[0] + direction[1] * vf[1]) / flat);
	cmd->sidemove = (short)(400.0f *
	    (direction[0] * vr[0] + direction[1] * vr[1]));
}
