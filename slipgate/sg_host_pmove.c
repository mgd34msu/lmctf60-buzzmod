#include <limits.h>
#include <math.h>
#include <string.h>

#include "sg_host_pmove.h"

typedef struct sg_host_pmove_scope_s
{
	const sg_host_collision_authority_t *authority;
	const sg_host_collision_scene_t *scene;
	const pmove_t *pmove;
	csurface_t surface;
	sg_host_pmove_trace_t *traces;
	size_t trace_capacity;
	uint32_t substep;
	uint64_t trace_count;
	uint64_t collision_trace_count;
	int trace_capacity_failed;
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
	sg_host_pmove_trace_t *record = NULL;

	memset(&host_trace, 0, sizeof(host_trace));
	host_trace.fraction = 1.0f;
	VectorCopy(end, host_trace.endpos);
	if (scope)
	{
		if (scope->traces)
		{
			if (scope->trace_count >= scope->trace_capacity)
			{
				scope->trace_capacity_failed = 1;
				host_trace.allsolid = true;
				host_trace.startsolid = true;
				host_trace.fraction = 0.0f;
				VectorCopy(start, host_trace.endpos);
				host_trace.contents = CONTENTS_SOLID;
				host_trace.ent = (struct edict_s *)(void *)scope;
				return host_trace;
			}
			record = &scope->traces[scope->trace_count];
			memset(record, 0, sizeof(*record));
			record->ordinal = scope->trace_count + 1U;
			record->substep = scope->substep;
			if (scope->pmove)
				record->state = scope->pmove->s;
			VectorCopy(start, record->start);
			VectorCopy(mins, record->mins);
			VectorCopy(maxs, record->maxs);
			VectorCopy(end, record->end);
		}
		scope->trace_count++;
		memset(&scope->surface, 0, sizeof(scope->surface));
		host_trace.surface = &scope->surface;
	}
	if (!scope || !SG_HostCollisionTrace(scope->authority, scope->scene,
		start, mins, maxs, end, SG_HOST_MASK_PLAYER_SOLID, &trace))
	{
		if (scope)
		{
			scope->collision_failed = 1;
			scope->collision_trace_count++;
		}
		host_trace.allsolid = true;
		host_trace.startsolid = true;
		host_trace.fraction = 0.0f;
		VectorCopy(start, host_trace.endpos);
		host_trace.contents = CONTENTS_SOLID;
		host_trace.ent = (struct edict_s *)(void *)scope;
		return host_trace;
	}
	if (record)
		record->result = trace;
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
		scope->collision_trace_count++;
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

static int HullMatchesIdentity(const sg_host_collision_authority_t *authority,
	const pmove_t *pmove)
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
	return pmove->s.gravity == (short)authority->identity.physics.gravity;
}

static int EvaluateFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_scope_t scope;
	pmove_state_t previous;
	pmove_state_t state;
	pmove_t pm;
	short gravity;
	uint32_t steps, step;
	int parameters;
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;

	if (!authority || !request || !result_out ||
		request->state.pm_type != PM_NORMAL)
		error = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
	else if (!host_pmove)
		error = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
	else if (sg_host_pmove_scope)
		error = SG_HOST_PMOVE_ERROR_REENTRANT;
	else if ((parameters = ParametersValid(authority, &gravity, &steps)) == 0)
		error = SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING;
	else if (parameters < 0)
		error = SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY;
	else if (replay_out && (!workspace || !workspace->substeps ||
		workspace->substep_capacity < steps || !workspace->traces ||
		workspace->trace_capacity == 0U))
		error = SG_HOST_PMOVE_ERROR_CAPACITY;
	if (error != SG_HOST_PMOVE_ERROR_NONE)
	{
		if (error_out)
			*error_out = error;
		return 0;
	}
	memset(&scope, 0, sizeof(scope));
	scope.authority = authority;
	scope.scene = scene;
	if (replay_out)
	{
		scope.traces = workspace->traces;
		scope.trace_capacity = workspace->trace_capacity;
	}
	state = request->state;
	previous = request->previous_state;
	state.gravity = gravity;
	memset(result_out, 0, sizeof(*result_out));
	sg_host_pmove_scope = &scope;
	for (step = 0; step < steps; step++)
	{
		pmove_state_t before_state = state;
		uint64_t first_trace_ordinal = scope.trace_count + 1U;
		uint64_t collision_trace_count = scope.collision_trace_count;

		memset(&pm, 0, sizeof(pm));
		pm.s = state;
		pm.cmd = request->command;
		pm.cmd.msec = (byte)authority->identity.physics.substep_ms;
		pm.snapinitial = memcmp(&previous, &pm.s, sizeof(pm.s)) != 0;
		pm.trace = PmoveTrace;
		pm.pointcontents = PmovePointContents;
		scope.substep = step;
		scope.pmove = &pm;
		host_pmove(&pm);
		scope.pmove = NULL;
		if (scope.trace_capacity_failed)
		{
			error = SG_HOST_PMOVE_ERROR_CAPACITY;
			break;
		}
		if (scope.collision_failed)
		{
			error = SG_HOST_PMOVE_ERROR_COLLISION;
			break;
		}
		if (!HullMatchesIdentity(authority, &pm))
		{
			error = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
			break;
		}
		state = pm.s;
		previous = pm.s;
		if (replay_out)
		{
			sg_host_pmove_substep_t *substep = &workspace->substeps[step];
			sg_host_collision_pose_t pose;
			sg_rune_stance_t stance;
			uint32_t axis;

			memset(substep, 0, sizeof(*substep));
			substep->before_state = before_state;
			substep->state = pm.s;
			for (axis = 0U; axis < 3U; axis++)
			{
				substep->before_origin[axis] =
					before_state.origin[axis] * 0.125f;
				substep->before_velocity[axis] =
					before_state.velocity[axis] * 0.125f;
				substep->origin[axis] = pm.s.origin[axis] * 0.125f;
				substep->velocity[axis] = pm.s.velocity[axis] * 0.125f;
			}
			stance = (pm.s.pm_flags & PMF_DUCKED) ?
				SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
			substep->stance = stance;
			if (!SG_HostCollisionClassifyPose(authority, scene,
					substep->origin, stance, &pose))
			{
				error = SG_HOST_PMOVE_ERROR_COLLISION;
				break;
			}
			substep->grounded = pm.groundentity != NULL;
			if (pose.supported)
			{
				substep->support_model_index = pose.support.model_index;
				substep->support_instance_id = pose.support.instance_id;
			}
			substep->water_type = pm.watertype;
			substep->water_level = pm.waterlevel;
			substep->step = step;
			substep->elapsed_ms =
				(step + 1U) * authority->identity.physics.substep_ms;
			substep->first_trace_ordinal = first_trace_ordinal;
			substep->trace_count = scope.trace_count -
				(first_trace_ordinal - 1U);
			substep->collision_trace_count = scope.collision_trace_count -
				collision_trace_count;
		}
	}
	sg_host_pmove_scope = NULL;
	if (error != SG_HOST_PMOVE_ERROR_NONE)
	{
		if (error_out)
			*error_out = error;
		return 0;
	}
	result_out->state = pm.s;
	VectorCopy(pm.mins, result_out->mins);
	VectorCopy(pm.maxs, result_out->maxs);
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
	result_out->trace_count = scope.trace_count;
	result_out->collision_trace_count = scope.collision_trace_count;
	result_out->gravity = authority->identity.physics.gravity;
	result_out->physics_abi_id = authority->identity.physics_abi_id;
	if (replay_out)
	{
		memset(replay_out, 0, sizeof(*replay_out));
		replay_out->request = *request;
		replay_out->result = *result_out;
		replay_out->substeps = workspace->substeps;
		replay_out->substep_count = steps;
		replay_out->traces = workspace->traces;
		replay_out->trace_count = (size_t)scope.trace_count;
		replay_out->bsp_content_id = authority->identity.bsp_content_id;
		replay_out->physics_abi_id = authority->identity.physics_abi_id;
		replay_out->frame_ms = authority->identity.physics.frame_ms;
		replay_out->substep_ms = authority->identity.physics.substep_ms;
	}
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
	return EvaluateFrame(authority, scene, host_pmove, request, result_out,
		NULL, NULL, error_out);
}

int SG_HostPmoveReplayFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_result_t result;

	if (!replay_out)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	memset(replay_out, 0, sizeof(*replay_out));
	return EvaluateFrame(authority, scene, host_pmove, request, &result,
		workspace, replay_out, error_out);
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
	case SG_HOST_PMOVE_ERROR_CAPACITY: return "insufficient replay capacity";
	case SG_HOST_PMOVE_ERROR_COLLISION: return "collision callback failure";
	default: return "unknown Pmove error";
	}
}
