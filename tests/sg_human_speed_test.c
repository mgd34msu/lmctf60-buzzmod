/* Real-Pmove regression for ordinary bot strafe-jump cadence. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_human_speed.h"

void Pmove(pmove_t *pmove);

static int failures;
static float floor_z;
static edict_t pmove_world;
static csurface_t floor_surface;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

static trace_t FlatFloorTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	float start_bottom = start[2] + mins[2];
	float end_bottom = end[2] + mins[2];

	(void)maxs;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (start_bottom < floor_z)
	{
		trace.startsolid = true;
		trace.allsolid = end_bottom < floor_z;
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
	}
	else if (end_bottom < floor_z)
	{
		float distance = start_bottom - end_bottom;
		float fraction = (start_bottom - floor_z - 0.03125f) / distance;

		if (fraction < 0.0f)
			fraction = 0.0f;
		if (fraction > 1.0f)
			fraction = 1.0f;
		trace.fraction = fraction;
		trace.endpos[0] = start[0] + fraction * (end[0] - start[0]);
		trace.endpos[1] = start[1] + fraction * (end[1] - start[1]);
		trace.endpos[2] = start[2] + fraction * (end[2] - start[2]);
	}
	if (trace.fraction < 1.0f)
	{
		trace.ent = &pmove_world;
		trace.contents = CONTENTS_SOLID;
		trace.surface = &floor_surface;
		trace.plane.normal[2] = 1.0f;
		trace.plane.dist = floor_z;
	}
	return trace;
}

static int DryPointContents(vec3_t point)
{
	(void)point;
	return 0;
}

static void EngineTimerTick(pmove_state_t *pmove, unsigned msec)
{
	unsigned ticks;

	if (!pmove->pm_time)
		return;
	ticks = msec >> 3;
	if (!ticks)
		ticks = 1;
	if (ticks >= pmove->pm_time)
	{
		pmove->pm_time = 0;
		pmove->pm_flags &= (byte)~(PMF_TIME_WATERJUMP |
		    PMF_TIME_LAND | PMF_TIME_TELEPORT);
	}
	else
		pmove->pm_time = (byte)(pmove->pm_time - ticks);
}

static void TestLandingTimerCadence(void)
{
	static const unsigned msec[8] = { 13, 13, 13, 13, 12, 12, 12, 12 };
	sg_human_speed_timer_t timer;
	pmove_state_t corrected;
	pmove_state_t stock;
	unsigned i;

	memset(&timer, 0, sizeof(timer));
	memset(&corrected, 0, sizeof(corrected));
	corrected.pm_flags = PMF_TIME_LAND;
	corrected.pm_time = 18;
	stock = corrected;
	for (i = 0; i < 8; i++)
	{
		(void)SG_HumanSpeedLandingPrepare(&timer, &corrected, msec[i], true);
		EngineTimerTick(&corrected, msec[i]);
		(void)SG_HumanSpeedLandingObserve(&timer, PMF_TIME_LAND,
		    &corrected, msec[i], true);
		EngineTimerTick(&stock, msec[i]);
	}
	CHECK(corrected.pm_time == 6);
	CHECK(stock.pm_time == 10);
	CHECK(timer.tracking);
	CHECK(timer.remainder_ms == 4);
}

static void TestLandingTransitionAndIsolation(void)
{
	sg_human_speed_timer_t timer;
	pmove_state_t pmove;
	sg_human_speed_step_t result;

	memset(&timer, 0, sizeof(timer));
	memset(&pmove, 0, sizeof(pmove));
	pmove.pm_flags = PMF_TIME_LAND;
	pmove.pm_time = 17;
	result = SG_HumanSpeedLandingObserve(&timer, 0, &pmove, 13, true);
	CHECK(result.began && timer.tracking && timer.remainder_ms == 5);
	result = SG_HumanSpeedLandingPrepare(&timer, &pmove, 13, true);
	CHECK(result.extra_ticks == 1 && pmove.pm_time == 16);

	pmove.pm_flags = PMF_TIME_LAND | PMF_TIME_TELEPORT;
	pmove.pm_time = 18;
	result = SG_HumanSpeedLandingPrepare(&timer, &pmove, 13, true);
	CHECK(result.extra_ticks == 0 && pmove.pm_time == 18);
	CHECK(!timer.tracking);
	pmove.pm_flags = PMF_TIME_WATERJUMP;
	result = SG_HumanSpeedLandingPrepare(&timer, &pmove, 13, true);
	CHECK(result.extra_ticks == 0 && pmove.pm_time == 18);
	result = SG_HumanSpeedLandingPrepare(&timer, &pmove, 13, false);
	CHECK(result.extra_ticks == 0 && pmove.pm_time == 18);
}

static void TestCommandBoundaryAndExactCadence(void)
{
	sg_human_speed_timer_t timer;
	pmove_state_t pmove;
	sg_human_speed_step_t result;

	memset(&timer, 0, sizeof(timer));
	memset(&pmove, 0, sizeof(pmove));
	pmove.pm_flags = PMF_TIME_LAND;
	pmove.pm_time = 17;
	result = SG_HumanSpeedLandingObserve(&timer, 0, &pmove, 13, true);
	CHECK(result.began && timer.remainder_ms == 5);

	/* A seedless/proved/early-return command breaks ownership.  Resuming the
	 * chain starts a fresh cadence and cannot spend the old five milliseconds. */
	SG_HumanSpeedCommandBoundary(&timer, false);
	CHECK(!timer.tracking && timer.remainder_ms == 0);
	result = SG_HumanSpeedLandingPrepare(&timer, &pmove, 13, true);
	CHECK(result.extra_ticks == 0 && pmove.pm_time == 17);
	CHECK(timer.tracking && timer.remainder_ms == 5);

	/* The production contract is the default eight-way 12/13 ms cadence,
	 * never arbitrary subframe counts such as five 20 ms commands. */
	result = SG_HumanSpeedLandingPrepare(&timer, &pmove, 20, true);
	CHECK(result.extra_ticks == 0 && pmove.pm_time == 17);
	CHECK(!timer.tracking && timer.remainder_ms == 0);
}

typedef struct run_result_s
{
	float peak_speed;
	float final_speed;
	unsigned above_330_ms;
	unsigned land_timer_ms;
	unsigned jumps;
} run_result_t;

typedef struct landing_result_s
{
	unsigned release_ms;
	float release_speed;
	unsigned initial_timer;
} landing_result_t;

static landing_result_t RunLandingRelease(qboolean corrected)
{
	static const unsigned step_msec[8] = { 13, 13, 13, 13, 12, 12, 12, 12 };
	sg_human_speed_timer_t timer;
	pmove_state_t state;
	pmove_state_t old_state;
	landing_result_t result;
	unsigned elapsed = 0;
	unsigned step = 0;

	memset(&timer, 0, sizeof(timer));
	memset(&state, 0, sizeof(state));
	memset(&old_state, 0, sizeof(old_state));
	memset(&result, 0, sizeof(result));
	state.pm_type = PM_NORMAL;
	state.origin[2] = 24 * 8;
	state.velocity[0] = 360 * 8;
	state.velocity[2] = -300 * 8;
	state.gravity = 800;
	old_state = state;

	while (elapsed < 500)
	{
		pmove_t pmove;
		usercmd_t command;
		byte flags_before;
		unsigned msec = step_msec[step & 7u];

		memset(&command, 0, sizeof(command));
		command.msec = (byte)msec;
		command.forwardmove = 400;
		command.upmove = 400;
		flags_before = state.pm_flags;
		(void)SG_HumanSpeedLandingPrepare(&timer, &state, msec, corrected);
		memset(&pmove, 0, sizeof(pmove));
		pmove.s = state;
		pmove.snapinitial = memcmp(&old_state, &state, sizeof(state)) != 0;
		pmove.cmd = command;
		pmove.trace = FlatFloorTrace;
		pmove.pointcontents = DryPointContents;
		Pmove(&pmove);
		old_state = pmove.s;
		state = pmove.s;
		(void)SG_HumanSpeedLandingObserve(&timer, flags_before, &state,
		    msec, corrected);
		elapsed += msec;
		if (step == 0)
		{
			CHECK(state.pm_flags & PMF_TIME_LAND);
			result.initial_timer = state.pm_time;
		}
		if (!(state.pm_flags & PMF_ON_GROUND))
		{
			float vx = state.velocity[0] * 0.125f;
			float vy = state.velocity[1] * 0.125f;

			result.release_ms = elapsed;
			result.release_speed = sqrtf(vx * vx + vy * vy);
			break;
		}
		step++;
	}
	return result;
}

static run_result_t RunChain(qboolean corrected)
{
	static const unsigned step_msec[8] = { 13, 13, 13, 13, 12, 12, 12, 12 };
	sg_human_speed_timer_t timer;
	pmove_state_t state;
	pmove_state_t old_state;
	pmove_t pmove;
	run_result_t result;
	unsigned elapsed = 0;
	unsigned step = 0;
	qboolean was_grounded = true;

	memset(&timer, 0, sizeof(timer));
	memset(&state, 0, sizeof(state));
	memset(&old_state, 0, sizeof(old_state));
	memset(&result, 0, sizeof(result));
	state.pm_type = PM_NORMAL;
	state.origin[2] = 24 * 8;
	state.velocity[0] = 300 * 8;
	state.pm_flags = PMF_ON_GROUND;
	state.gravity = 800;
	old_state = state;

	while (elapsed < 8000)
	{
		usercmd_t command;
		sg_human_speed_air_t air;
		byte flags_before;
		float vx, vy, vz, speed;
		qboolean grounded;
		unsigned msec = step_msec[step & 7u];

		memset(&command, 0, sizeof(command));
		command.msec = (byte)msec;
		vx = state.velocity[0] * 0.125f;
		vy = state.velocity[1] * 0.125f;
		vz = state.velocity[2] * 0.125f;
		speed = sqrtf(vx * vx + vy * vy);
		grounded = (state.pm_flags & PMF_ON_GROUND) != 0;
		if (grounded)
		{
			command.forwardmove = 400;
			if (!(state.pm_flags & PMF_TIME_LAND))
				command.upmove = 400;
		}
		else
		{
			vec3_t velocity = { vx, vy, vz };

			memset(&air, 0, sizeof(air));
			air.lean = sinf((float)elapsed *
			    (2.0f * (float)M_PI / 1350.0f));
			air.view_yaw = 0.0f;
			air.view_pitch = 0.0f;
			air.chain = true;
			SG_HumanSpeedAirCommand(&command, &air, velocity, speed,
			    (float)msec / 1000.0f);
			if (vz < 0.0f)
				command.upmove = 400;
		}

		flags_before = state.pm_flags;
		(void)SG_HumanSpeedLandingPrepare(&timer, &state, msec, corrected);
		memset(&pmove, 0, sizeof(pmove));
		pmove.s = state;
		pmove.snapinitial = memcmp(&old_state, &state, sizeof(state)) != 0;
		pmove.cmd = command;
		pmove.trace = FlatFloorTrace;
		pmove.pointcontents = DryPointContents;
		Pmove(&pmove);
		old_state = pmove.s;
		state = pmove.s;
		(void)SG_HumanSpeedLandingObserve(&timer, flags_before, &state,
		    msec, corrected);

		grounded = pmove.groundentity != NULL;
		if (was_grounded && !grounded)
			result.jumps++;
		was_grounded = grounded;
		vx = state.velocity[0] * 0.125f;
		vy = state.velocity[1] * 0.125f;
		speed = sqrtf(vx * vx + vy * vy);
		if (speed > result.peak_speed)
			result.peak_speed = speed;
		if (speed >= 330.0f)
			result.above_330_ms += msec;
		if (state.pm_flags & PMF_TIME_LAND)
			result.land_timer_ms += msec;
		result.final_speed = speed;
		elapsed += msec;
		step++;
	}
	return result;
}

static void TestRealPmoveChain(void)
{
	run_result_t corrected = RunChain(true);
	landing_result_t stock_landing = RunLandingRelease(false);
	landing_result_t corrected_landing = RunLandingRelease(true);

	CHECK(corrected.peak_speed > 330.0f);
	CHECK(corrected.above_330_ms >= 500);
	CHECK(corrected.jumps >= 8);
	CHECK(corrected_landing.initial_timer == 17);
	CHECK(stock_landing.initial_timer == 17);
	CHECK(corrected_landing.release_ms >= 140);
	CHECK(corrected_landing.release_ms <= 160);
	CHECK(stock_landing.release_ms >= 210);
	CHECK(stock_landing.release_ms <= 240);
	CHECK(corrected_landing.release_ms + 50 < stock_landing.release_ms);
	CHECK(corrected_landing.release_speed == 300.0f);
	CHECK(stock_landing.release_speed == 300.0f);
}

int main(void)
{
	memset(&pmove_world, 0, sizeof(pmove_world));
	memset(&floor_surface, 0, sizeof(floor_surface));
	pmove_world.inuse = true;
	floor_z = 0.0f;
	TestLandingTimerCadence();
	TestLandingTransitionAndIsolation();
	TestCommandBoundaryAndExactCadence();
	TestRealPmoveChain();
	if (failures)
	{
		fprintf(stderr, "%d sg_human_speed tests failed\n", failures);
		return 1;
	}
	printf("sg_human_speed_test: ok\n");
	return 0;
}
