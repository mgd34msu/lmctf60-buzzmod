#include <limits.h>
#include <math.h>
#include <string.h>

#include "sg_host_pmove.h"
#include "sg_host_engine_pmove.h"

typedef struct sg_host_pmove_scope_s
{
	const sg_host_collision_authority_t *authority;
	const sg_host_collision_scene_t *scene;
	csurface_t surface;
	int collision_failed;
} sg_host_pmove_scope_t;

static sg_host_pmove_scope_t *sg_host_pmove_scope;

static byte PlaneSignBits(const float normal[3])
{
	byte bits = 0;

	if (normal[0] < 0.0f)
		bits |= 1;
	if (normal[1] < 0.0f)
		bits |= 2;
	if (normal[2] < 0.0f)
		bits |= 4;
	return bits;
}

static trace_t PmoveTrace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t host_trace;
	sg_host_collision_trace_t trace;
	sg_host_pmove_scope_t *scope = sg_host_pmove_scope;

	memset(&host_trace, 0, sizeof(host_trace));
	host_trace.fraction = 1.0f;
	VectorCopy(end, host_trace.endpos);
	if (scope)
	{
		memset(&scope->surface, 0, sizeof(scope->surface));
		host_trace.surface = &scope->surface;
	}
	if (!scope || !SG_HostCollisionTrace(scope->authority, scope->scene,
		start, mins, maxs, end, SG_HOST_MASK_PLAYER_SOLID, &trace))
	{
		if (scope)
			scope->collision_failed = 1;
		host_trace.allsolid = true;
		host_trace.startsolid = true;
		host_trace.fraction = 0.0f;
		VectorCopy(start, host_trace.endpos);
		host_trace.contents = CONTENTS_SOLID;
		host_trace.ent = (struct edict_s *)(void *)scope;
		return host_trace;
	}
	host_trace.allsolid = trace.allsolid ? true : false;
	host_trace.startsolid = trace.startsolid ? true : false;
	host_trace.fraction = trace.fraction;
	VectorCopy(trace.end, host_trace.endpos);
	VectorCopy(trace.plane.normal, host_trace.plane.normal);
	host_trace.plane.dist = trace.plane.distance;
	host_trace.plane.type = (byte)trace.plane.type;
	host_trace.plane.signbits = PlaneSignBits(trace.plane.normal);
	host_trace.contents = (int)trace.contents;
	if (trace.fraction < 1.0f || trace.startsolid || trace.allsolid)
	{
		scope->surface.flags = trace.surface_flags;
		/* Pmove treats entity pointers as opaque collision identities. */
		host_trace.ent = (struct edict_s *)(void *)scope;
	}
	return host_trace;
}

static int PmovePointContents(vec3_t point)
{
	if (!sg_host_pmove_scope)
		return CONTENTS_SOLID;
	return (int)SG_HostCollisionPointContents(sg_host_pmove_scope->authority,
		sg_host_pmove_scope->scene, point);
}

static int ParametersValid(const sg_host_collision_authority_t *authority,
	short *gravity_out, uint32_t *steps_out)
{
	const sg_rune_physics_parameters_t *physics;
	short gravity;

	if (!authority || !authority->world)
		return 0;
	physics = &authority->identity.physics;
	if (physics->substep_ms == 0 || physics->substep_ms > UCHAR_MAX ||
		physics->frame_ms == 0 ||
		physics->frame_ms % physics->substep_ms != 0)
		return 0;
	if (!isfinite(physics->gravity) || physics->gravity < 0.0f ||
		physics->gravity > (float)SHRT_MAX ||
		truncf(physics->gravity) != physics->gravity)
		return -1;
	gravity = (short)physics->gravity;
	*gravity_out = gravity;
	*steps_out = physics->frame_ms / physics->substep_ms;
	return 1;
}

static int HookGravityActive(const sg_host_pmove_request_t *request)
{
	return request && request->hook_law_id == SG_HOST_PMOVE_HOOK_LAW_ID &&
		request->hook_attached == 1U &&
		request->hook_length < SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO;
}

static int RequestGravity(const sg_host_pmove_request_t *request,
	short map_gravity, short *gravity_out)
{
	if (!request || !gravity_out || request->hook_attached > 1U)
		return 0;
	if (request->hook_law_id != 0U &&
		request->hook_law_id != SG_HOST_PMOVE_HOOK_LAW_ID)
		return 0;
	if (request->hook_law_id == 0U && request->hook_attached != 0U)
		return 0;
	*gravity_out = HookGravityActive(request) ? 0 : map_gravity;
	return 1;
}

static int HullMatchesIdentity(const sg_host_collision_authority_t *authority,
	const pmove_t *pmove, short expected_gravity)
{
	const sg_rune_hull_profile_t *hull =
		(pmove->s.pm_flags & PMF_DUCKED) ?
		&authority->identity.crouching_hull :
		&authority->identity.standing_hull;
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		if (pmove->mins[axis] != hull->mins.value[axis] ||
			pmove->maxs[axis] != hull->maxs.value[axis])
			return 0;
	return pmove->s.gravity == expected_gravity;
}

static int EvaluateFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	int use_engine,
	const struct sg_host_engine_pmove_binding_s *binding,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_scope_t scope;
	pmove_state_t previous;
	pmove_state_t state;
	pmove_t pm;
	short gravity;
	short effective_gravity;
	uint32_t steps, step;
	int parameters;
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;
	const sg_rune_hull_profile_t *hull;

	if (!authority || !request || !result_out ||
		request->state.pm_type != PM_NORMAL)
		error = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
	else if (!host_pmove && !use_engine)
		error = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
	else if (use_engine && !binding)
		error = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
	else if (sg_host_pmove_scope)
		error = SG_HOST_PMOVE_ERROR_REENTRANT;
	else if ((parameters = ParametersValid(authority, &gravity, &steps)) == 0)
		error = SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING;
	else if (parameters < 0)
		error = SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY;
	if (error != SG_HOST_PMOVE_ERROR_NONE)
	{
		if (error_out)
			*error_out = error;
		return 0;
	}
	if (!RequestGravity(request, gravity, &effective_gravity))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
		return 0;
	}
	memset(&scope, 0, sizeof(scope));
	scope.authority = authority;
	scope.scene = scene;
	state = request->state;
	previous = request->previous_state;
	state.gravity = effective_gravity;
	memset(result_out, 0, sizeof(*result_out));
	/* Keep a defined terminal value even for an analyzer that cannot derive
	 * the positive step count from ParametersValid().  The production timing
	 * contract below still rejects a zero-step authority before this point. */
	memset(&pm, 0, sizeof(pm));
	pm.s = state;
	sg_host_pmove_scope = &scope;
	for (step = 0; step < steps; step++)
	{
		memset(&pm, 0, sizeof(pm));
		pm.s = state;
		pm.cmd = request->command;
		pm.cmd.msec = (byte)authority->identity.physics.substep_ms;
		pm.snapinitial = memcmp(&previous, &pm.s, sizeof(pm.s)) != 0;
		pm.trace = PmoveTrace;
		pm.pointcontents = PmovePointContents;
		if (use_engine)
		{
			if (!SG_HostEnginePmoveBound(binding, &pm))
				error = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		}
		else
			host_pmove(&pm);
		if (error != SG_HOST_PMOVE_ERROR_NONE)
			break;
		if (scope.collision_failed)
		{
			error = SG_HOST_PMOVE_ERROR_COLLISION;
			break;
		}
		if (!HullMatchesIdentity(authority, &pm, effective_gravity))
		{
			error = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
			break;
		}
		state = pm.s;
		previous = pm.s;
	}
	sg_host_pmove_scope = NULL;
	if (error != SG_HOST_PMOVE_ERROR_NONE)
	{
		if (error_out)
			*error_out = error;
		return 0;
	}
	result_out->state = pm.s;
	/* HullMatchesIdentity above established these exact engine outputs.  Copy
	 * the authoritative values rather than trusting an opaque adapter to
	 * initialize a caller-owned pmove buffer for the analyzer. */
	hull = (pm.s.pm_flags & PMF_DUCKED) ?
		&authority->identity.crouching_hull :
		&authority->identity.standing_hull;
	VectorCopy(hull->mins.value, result_out->mins);
	VectorCopy(hull->maxs.value, result_out->maxs);
	VectorCopy(pm.viewangles, result_out->view_angles);
	result_out->view_height = pm.viewheight;
	result_out->grounded = pm.groundentity != NULL;
	for (step = 0; step < 3; step++)
	{
		result_out->origin[step] = pm.s.origin[step] * 0.125f;
		result_out->velocity[step] = pm.s.velocity[step] * 0.125f;
	}
	{
		sg_host_collision_pose_t pose;
		sg_rune_stance_t stance = (pm.s.pm_flags & PMF_DUCKED) ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;

		if (!SG_HostCollisionClassifyPose(authority, scene,
			result_out->origin, stance, &pose))
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		if (pose.supported)
		{
			result_out->support_model_index = pose.support.model_index;
			result_out->support_instance_id = pose.support.instance_id;
		}
	}
	result_out->water_type = pm.watertype;
	result_out->water_level = pm.waterlevel;
	result_out->evaluated_steps = steps;
	result_out->elapsed_ms = authority->identity.physics.frame_ms;
	result_out->gravity = (float)effective_gravity;
	result_out->physics_abi_id = authority->identity.physics_abi_id;
	result_out->gravity_law_id = HookGravityActive(request) ?
		SG_HOST_PMOVE_HOOK_LAW_ID : UINT64_C(0);
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return 1;
}

int SG_HostPmoveEvaluateFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	return EvaluateFrame(authority, scene, host_pmove, 0, NULL, request,
		result_out, error_out);
}

int SG_HostPmoveEvaluateEngineFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	sg_host_engine_pmove_binding_t binding;

	if (!SG_HostEnginePmoveBindingCapture(&binding))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	return EvaluateFrame(authority, scene, NULL, 1, &binding, request,
		result_out, error_out);
}

int SG_HostPmoveEvaluateBoundEngineFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const struct sg_host_engine_pmove_binding_s *binding,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	return EvaluateFrame(authority, scene, NULL, 1, binding, request,
		result_out, error_out);
}

const char *SG_HostPmoveErrorString(sg_host_pmove_error_t error)
{
	switch (error)
	{
	case SG_HOST_PMOVE_ERROR_NONE: return "none";
	case SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE: return "host Pmove unavailable";
	case SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING: return "unsupported Pmove timing";
	case SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY: return "unsupported Pmove gravity";
	case SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH: return "Pmove identity mismatch";
	case SG_HOST_PMOVE_ERROR_REENTRANT: return "reentrant Pmove evaluation";
	case SG_HOST_PMOVE_ERROR_COLLISION: return "collision callback failure";
	default: return "unknown Pmove error";
	}
}
