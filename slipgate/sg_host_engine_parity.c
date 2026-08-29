#include "sg_host_engine_parity.h"
#include "sg_host_engine_pmove.h"

#include <math.h>
#include <string.h>

typedef struct sg_host_engine_parity_context_s
{
	uint32_t mode;
	uint32_t trace_calls;
	uint32_t contents_calls;
	csurface_t surface;
	int marker;
} sg_host_engine_parity_context_t;

static sg_host_engine_parity_context_t *sg_parity_context;

static int FiniteVector(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static void HashBytes(uint64_t *hash, const void *data, size_t size)
{
	const unsigned char *bytes = data;

	while (size-- != 0U)
	{
		*hash ^= (uint64_t)*bytes++;
		*hash *= UINT64_C(1099511628211);
	}
}

static void HashPmove(uint64_t *hash, uint32_t tag, const pmove_t *pmove)
{
	uint32_t groundentity_present = pmove->groundentity != NULL ? 1U : 0U;

	HashBytes(hash, &tag, sizeof(tag));
	/* Hash the observable state fields individually.  Hashing the structure
	 * would include ABI padding and make a valid engine binding appear to
	 * drift between parity runs. */
	HashBytes(hash, &pmove->s.pm_type, sizeof(pmove->s.pm_type));
	HashBytes(hash, pmove->s.origin, sizeof(pmove->s.origin));
	HashBytes(hash, pmove->s.velocity, sizeof(pmove->s.velocity));
	HashBytes(hash, &pmove->s.pm_flags, sizeof(pmove->s.pm_flags));
	HashBytes(hash, &pmove->s.pm_time, sizeof(pmove->s.pm_time));
	HashBytes(hash, &pmove->s.gravity, sizeof(pmove->s.gravity));
	HashBytes(hash, pmove->s.delta_angles, sizeof(pmove->s.delta_angles));
	HashBytes(hash, pmove->mins, sizeof(pmove->mins));
	HashBytes(hash, pmove->maxs, sizeof(pmove->maxs));
	HashBytes(hash, &pmove->viewheight, sizeof(pmove->viewheight));
	HashBytes(hash, pmove->viewangles, sizeof(pmove->viewangles));
	HashBytes(hash, &groundentity_present, sizeof(groundentity_present));
	HashBytes(hash, &pmove->numtouch, sizeof(pmove->numtouch));
	HashBytes(hash, &pmove->watertype, sizeof(pmove->watertype));
	HashBytes(hash, &pmove->waterlevel, sizeof(pmove->waterlevel));
}

static trace_t ParityTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	float fraction;

	(void)mins;
	(void)maxs;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.plane.normal[2] = 1.0f;
	trace.surface = &sg_parity_context->surface;
	sg_parity_context->trace_calls++;
	if (sg_parity_context->mode == SG_HOST_ENGINE_PARITY_ACCELERATION &&
		end[2] < start[2])
	{
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.ent = (struct edict_s *)(void *)&sg_parity_context->marker;
		return trace;
	}
	if (sg_parity_context->mode == SG_HOST_ENGINE_PARITY_COLLISION &&
		start[0] < 0.5f && end[0] > 0.5f)
	{
		fraction = (0.5f - start[0]) / (end[0] - start[0]);
		trace.fraction = fraction < 0.0f ? 0.0f : fraction;
		trace.endpos[0] = 0.5f;
		trace.plane.normal[0] = -1.0f;
		trace.plane.normal[2] = 0.0f;
		trace.contents = CONTENTS_SOLID;
		trace.ent = (struct edict_s *)(void *)&sg_parity_context->marker;
	}
	return trace;
}

static int ParityContents(vec3_t point)
{
	sg_parity_context->contents_calls++;
	if (sg_parity_context->mode == SG_HOST_ENGINE_PARITY_WATER)
		return point[2] < 1000.0f ? CONTENTS_WATER : 0;
	return 0;
}

static int RunCase(uint32_t mode, pmove_state_t state, usercmd_t command,
	pmove_t *pmove_out)
{
	pmove_t pmove;

	memset(&pmove, 0, sizeof(pmove));
	pmove.s = state;
	pmove.cmd = command;
	pmove.trace = ParityTrace;
	pmove.pointcontents = ParityContents;
	pmove_out[0] = pmove;
	sg_parity_context->mode = mode;
	if (!SG_HostEnginePmove(pmove_out))
		return 0;
	return FiniteVector(pmove_out->mins) && FiniteVector(pmove_out->maxs) &&
		isfinite(pmove_out->viewheight);
}

static int RunAcceleration(uint64_t *hash, uint32_t *cases,
	uint32_t *calls)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.gravity = 800;
	memset(&command, 0, sizeof(command));
	command.msec = 25U;
	command.forwardmove = 300;
	if (!RunCase(SG_HOST_ENGINE_PARITY_ACCELERATION, state, command, &pmove) ||
		pmove.s.velocity[0] <= 0.0f)
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_ACCELERATION, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_ACCELERATION;
	return 1;
}

static int RunGravity(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[2] = 1600;
	state.gravity = 800;
	memset(&command, 0, sizeof(command));
	command.msec = 25U;
	if (!RunCase(SG_HOST_ENGINE_PARITY_GRAVITY, state, command, &pmove) ||
		pmove.s.velocity[2] >= 0 || pmove.s.origin[2] >= state.origin[2])
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_GRAVITY, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_GRAVITY;
	return 1;
}

static int RunCollision(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[2] = 1600;
	state.gravity = 0;
	memset(&command, 0, sizeof(command));
	command.msec = 100U;
	command.forwardmove = 300;
	if (!RunCase(SG_HOST_ENGINE_PARITY_COLLISION, state, command, &pmove) ||
		pmove.s.origin[0] > 4 || pmove.s.velocity[0] > 1)
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_COLLISION, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_COLLISION;
	return 1;
}

static int RunWater(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[2] = 0;
	state.gravity = 800;
	memset(&command, 0, sizeof(command));
	command.msec = 25U;
	if (!RunCase(SG_HOST_ENGINE_PARITY_WATER, state, command, &pmove) ||
		pmove.waterlevel == 0 || !(pmove.watertype & CONTENTS_WATER))
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_WATER, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_WATER;
	return 1;
}

static int RunStance(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	pmove_state_t standing;
	pmove_state_t crouching;
	usercmd_t command;
	pmove_t stand_pmove;
	pmove_t crouch_pmove;

	memset(&standing, 0, sizeof(standing));
	standing.pm_type = PM_NORMAL;
	standing.gravity = 0;
	standing.pm_flags = PMF_ON_GROUND;
	crouching = standing;
	crouching.pm_flags = PMF_DUCKED | PMF_ON_GROUND;
	memset(&command, 0, sizeof(command));
	command.msec = 25U;
	{
		usercmd_t crouch_command = command;
		crouch_command.upmove = -1;
		if (!RunCase(SG_HOST_ENGINE_PARITY_STANCE, standing, command,
				&stand_pmove) ||
			!RunCase(SG_HOST_ENGINE_PARITY_STANCE, crouching, crouch_command,
				&crouch_pmove) || stand_pmove.maxs[2] != 32.0f ||
			crouch_pmove.maxs[2] != 4.0f || stand_pmove.viewheight != 22.0f ||
			crouch_pmove.viewheight != -2.0f)
			return 0;
	}
	HashPmove(hash, SG_HOST_ENGINE_PARITY_STANCE, &stand_pmove);
	HashPmove(hash, SG_HOST_ENGINE_PARITY_STANCE + 1U, &crouch_pmove);
	*calls += 2U;
	*cases |= SG_HOST_ENGINE_PARITY_STANCE;
	return 1;
}

static int RunTiming(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[2] = 1600;
	state.gravity = 0;
	memset(&command, 0, sizeof(command));
	command.msec = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	state.velocity[0] = 800;
	if (!RunCase(SG_HOST_ENGINE_PARITY_TIMING, state, command, &pmove) ||
		pmove.s.origin[0] <= state.origin[0])
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_TIMING, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_TIMING;
	return 1;
}

int SG_HostEnginePmoveParity(sg_host_engine_parity_result_t *result_out)
{
	sg_host_engine_parity_context_t context;
	uint64_t hash = UINT64_C(1469598103934665603);
	uint32_t cases = 0U;
	uint32_t calls = 0U;

	if (!result_out || sg_parity_context)
		return 0;
	memset(&context, 0, sizeof(context));
	memset(result_out, 0, sizeof(*result_out));
	sg_parity_context = &context;
	if (!RunAcceleration(&hash, &cases, &calls) ||
		!RunGravity(&hash, &cases, &calls) ||
		!RunCollision(&hash, &cases, &calls) ||
		!RunWater(&hash, &cases, &calls) ||
		!RunStance(&hash, &cases, &calls) ||
		!RunTiming(&hash, &cases, &calls))
	{
		sg_parity_context = NULL;
		result_out->cases = cases;
		result_out->engine_calls = calls;
		result_out->trace_calls = context.trace_calls;
		result_out->contents_calls = context.contents_calls;
		return 0;
	}
	sg_parity_context = NULL;
	result_out->fingerprint = hash;
	result_out->cases = cases;
	result_out->engine_calls = calls;
	result_out->trace_calls = context.trace_calls;
	result_out->contents_calls = context.contents_calls;
	return cases == SG_HOST_ENGINE_PARITY_ALL;
}
