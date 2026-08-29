#include "sg_host_engine_parity.h"
#include "sg_host_engine_pmove.h"

#include <math.h>
#include <string.h>

typedef struct sg_host_engine_parity_context_s
{
	uint32_t mode;
	uint32_t probe_seed;
	int collision_origin_x;
	int timing_velocity;
	int forwardmove;
	float water_surface_z;
	uint32_t trace_calls;
	uint32_t contents_calls;
	const sg_host_engine_pmove_binding_t *binding;
	sg_host_engine_parity_inputs_t inputs;
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

static void HashInputs(uint64_t *hash,
	const sg_host_engine_parity_inputs_t *inputs)
{
	HashBytes(hash, &inputs->gravity, sizeof(inputs->gravity));
	HashBytes(hash, &inputs->max_velocity, sizeof(inputs->max_velocity));
	HashBytes(hash, &inputs->airaccelerate, sizeof(inputs->airaccelerate));
	HashBytes(hash, &inputs->frame_ms, sizeof(inputs->frame_ms));
	HashBytes(hash, &inputs->substep_ms, sizeof(inputs->substep_ms));
}

/* Derive the probe geometry from the bound map physics.  The suite still
 * checks the same six observable behaviors, but a replacement callback cannot
 * pass by recognizing one public table of fixed origins and commands. */
static uint32_t ProbeSeed(const sg_host_engine_parity_inputs_t *inputs)
{
	uint64_t hash = UINT64_C(1469598103934665603);

	HashInputs(&hash, inputs);
	return (uint32_t)(hash ^ (hash >> 32));
}

static void ConfigureProbes(sg_host_engine_parity_context_t *context)
{
	context->probe_seed = ProbeSeed(&context->inputs);
	context->collision_origin_x = -32 -
		(int)(context->probe_seed % 32U);
	context->timing_velocity = 800 +
		(int)(context->probe_seed % 128U);
	context->forwardmove = 200 +
		(int)(context->probe_seed % 101U);
	context->water_surface_z = 1000.0f +
		(float)(context->probe_seed % 97U);
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
		start[0] < (float)sg_parity_context->collision_origin_x + 0.5f &&
		end[0] > (float)sg_parity_context->collision_origin_x + 0.5f)
	{
		fraction = ((float)sg_parity_context->collision_origin_x + 0.5f -
			start[0]) / (end[0] - start[0]);
		trace.fraction = fraction < 0.0f ? 0.0f : fraction;
		trace.endpos[0] = (float)sg_parity_context->collision_origin_x + 0.5f;
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
		return point[2] < sg_parity_context->water_surface_z ?
			CONTENTS_WATER : 0;
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
	if (!SG_HostEnginePmoveBound(sg_parity_context->binding, pmove_out))
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
	state.gravity = (short)sg_parity_context->inputs.gravity;
	memset(&command, 0, sizeof(command));
	command.msec = (byte)sg_parity_context->inputs.substep_ms;
	command.forwardmove = (short)sg_parity_context->forwardmove;
	if (!RunCase(SG_HOST_ENGINE_PARITY_ACCELERATION, state, command, &pmove) ||
		pmove.s.velocity[0] <= 0.0f)
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_ACCELERATION, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_ACCELERATION;
	return 1;
}

static int RunGravityCase(uint64_t *hash, uint32_t *cases, uint32_t *calls,
	float gravity, uint32_t tag)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[2] = (short)(1600 +
		(short)(sg_parity_context->probe_seed % 16U) * 8);
	state.gravity = (short)gravity;
	memset(&command, 0, sizeof(command));
	command.msec = (byte)sg_parity_context->inputs.substep_ms;
	if (!RunCase(SG_HOST_ENGINE_PARITY_GRAVITY, state, command, &pmove) ||
		pmove.s.velocity[2] >= 0 || pmove.s.origin[2] >= state.origin[2])
		return 0;
	HashPmove(hash, tag, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_GRAVITY;
	return 1;
}

static int RunGravity(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	/* Include both the live map value and the accepted low-gravity edge.  A
	 * fixed 800-only probe cannot authenticate a map whose physics is 100. */
	return RunGravityCase(hash, cases, calls, sg_parity_context->inputs.gravity,
		SG_HOST_ENGINE_PARITY_GRAVITY) &&
		RunGravityCase(hash, cases, calls, 100.0f,
		SG_HOST_ENGINE_PARITY_GRAVITY + 6U);
}

static int RunCollision(uint64_t *hash, uint32_t *cases, uint32_t *calls)
{
	pmove_state_t state;
	usercmd_t command;
	pmove_t pmove;

	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[0] = (short)(sg_parity_context->collision_origin_x * 8);
	state.origin[2] = (short)(1600 +
		(short)(sg_parity_context->probe_seed % 16U) * 8);
	state.gravity = 0;
	memset(&command, 0, sizeof(command));
	command.msec = (byte)sg_parity_context->inputs.frame_ms;
	command.forwardmove = (short)sg_parity_context->forwardmove;
	if (!RunCase(SG_HOST_ENGINE_PARITY_COLLISION, state, command, &pmove) ||
		pmove.s.origin[0] > (short)(sg_parity_context->collision_origin_x *
			8 + 4) || pmove.s.velocity[0] > 1)
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
	state.gravity = (short)sg_parity_context->inputs.gravity;
	memset(&command, 0, sizeof(command));
	command.msec = (byte)sg_parity_context->inputs.substep_ms;
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
	state.origin[2] = (short)(1600 +
		(short)(sg_parity_context->probe_seed % 16U) * 8);
	state.gravity = 0;
	memset(&command, 0, sizeof(command));
	command.msec = (byte)sg_parity_context->inputs.substep_ms;
	state.origin[0] = (short)(sg_parity_context->probe_seed % 32U * 8U);
	state.velocity[0] = (short)sg_parity_context->timing_velocity;
	if (!RunCase(SG_HOST_ENGINE_PARITY_TIMING, state, command, &pmove) ||
		pmove.s.origin[0] <= state.origin[0])
		return 0;
	HashPmove(hash, SG_HOST_ENGINE_PARITY_TIMING, &pmove);
	*calls += 1U;
	*cases |= SG_HOST_ENGINE_PARITY_TIMING;
	return 1;
}

int SG_HostEnginePmoveParityBound(
	const sg_host_engine_pmove_binding_t *binding,
	const sg_host_engine_parity_inputs_t *inputs,
	sg_host_engine_parity_result_t *result_out)
{
	sg_host_engine_parity_context_t context;
	uint64_t hash = UINT64_C(1469598103934665603);
	uint32_t cases = 0U;
	uint32_t calls = 0U;

	if (!result_out || !binding || !inputs || sg_parity_context ||
		!SG_HostEnginePmoveBindingCurrent(binding) ||
		!isfinite(inputs->gravity) || inputs->gravity < 1.0f ||
		inputs->gravity > 32767.0f || truncf(inputs->gravity) != inputs->gravity ||
		!isfinite(inputs->max_velocity) || inputs->max_velocity < 800.0f ||
		!isfinite(inputs->airaccelerate) || inputs->airaccelerate < 0.0f ||
		inputs->frame_ms == 0U || inputs->substep_ms == 0U ||
		inputs->frame_ms > UINT8_MAX || inputs->substep_ms > UINT8_MAX ||
		inputs->frame_ms % inputs->substep_ms != 0U)
		return 0;
	memset(&context, 0, sizeof(context));
	context.binding = binding;
	context.inputs = *inputs;
	ConfigureProbes(&context);
	memset(result_out, 0, sizeof(*result_out));
	sg_parity_context = &context;
	HashInputs(&hash, inputs);
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

int SG_HostEnginePmoveParity(sg_host_engine_parity_result_t *result_out)
{
	sg_host_engine_pmove_binding_t binding;
	sg_host_engine_parity_inputs_t inputs = {
		800.0f, 2000.0f, 0.0f, SG_HOST_ENGINE_FRAME_MS,
		SG_HOST_ENGINE_PMOVE_SUBSTEP_MS
	};

	if (!SG_HostEnginePmoveBindingCapture(&binding))
		return 0;
	return SG_HostEnginePmoveParityBound(&binding, &inputs, result_out);
}
