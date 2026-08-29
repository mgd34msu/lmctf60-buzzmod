#include "sg_host_engine_runtime.h"
#include "sg_host_engine_runtime_private.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern game_import_t gi;
extern game_export_t globals;

#define SG_HOST_ENGINE_RUNTIME_STATE UINT32_C(0x45525433)
#define SG_HOST_ENGINE_RUNTIME_STATE_INVERSE UINT32_C(0xb8adbccc)

typedef trace_t (*sg_host_engine_trace_function_t)(vec3_t start,
	vec3_t mins, vec3_t maxs, vec3_t end, edict_t *passent, int contentmask);
typedef int (*sg_host_engine_contents_function_t)(vec3_t point);

struct sg_host_engine_runtime_s
{
	uint32_t state;
	uint32_t state_inverse;
	const void *owner;
	sg_host_engine_trace_function_t trace;
	sg_host_engine_contents_function_t pointcontents;
	sg_host_pmove_function_t pmove;
	sg_level_identity_t level;
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
	int accepted;
	const sg_rune_runtime_snapshot_t *accepted_snapshot;
	sg_rune_model_identity_t identity;
	sg_bsp_content_identity_t content_identity;
	uint64_t generation;
	uint64_t topology_revision;
	edict_t *subject;
	uint32_t subject_index;
	const gclient_t *subject_client;
	int subject_number;
};

#define SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_STATE UINT32_C(0x45524133)

struct sg_host_engine_runtime_acceptance_s
{
	uint32_t state;
	uint32_t state_inverse;
	const void *owner;
	const sg_rune_runtime_snapshot_t *snapshot;
	sg_bsp_content_identity_t content_identity;
};

typedef struct sg_host_engine_runtime_scope_s
{
	const sg_host_engine_runtime_t *runtime;
	int collision_failed;
} sg_host_engine_runtime_scope_t;

static sg_host_engine_runtime_scope_t *sg_host_engine_runtime_scope;
static const unsigned char sg_host_engine_runtime_owner_token;

const void *SG_HostEngineRuntimeOwnerToken(void)
{
	return &sg_host_engine_runtime_owner_token;
}

static int FiniteVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	uint32_t axis;

	if (!identity || identity->bsp_content_id == 0U ||
		identity->bsp_content_id == UINT64_MAX ||
		identity->entity_semantics_id == 0U ||
		identity->entity_semantics_id == UINT64_MAX ||
		identity->physics_abi_id == 0U ||
		identity->physics_abi_id == UINT64_MAX ||
		identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX ||
		identity->schema_id == 0U || identity->schema_id == UINT64_MAX ||
		identity->producer_identity == 0U ||
		identity->producer_identity == UINT64_MAX)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(identity->standing_hull.mins.value[axis]) ||
			!isfinite(identity->standing_hull.maxs.value[axis]) ||
			!isfinite(identity->crouching_hull.mins.value[axis]) ||
			!isfinite(identity->crouching_hull.maxs.value[axis]) ||
			identity->standing_hull.mins.value[axis] >=
				identity->standing_hull.maxs.value[axis] ||
			identity->crouching_hull.mins.value[axis] >=
				identity->crouching_hull.maxs.value[axis])
			return 0;
	}
	return isfinite(identity->physics.gravity) &&
		isfinite(identity->physics.ground_acceleration) &&
		isfinite(identity->physics.air_acceleration) &&
		isfinite(identity->physics.water_acceleration) &&
		isfinite(identity->physics.hook_acceleration) &&
		isfinite(identity->physics.external_acceleration) &&
		isfinite(identity->physics.water_drag) &&
		isfinite(identity->physics.max_velocity) &&
		identity->physics.gravity >= 0.0f &&
		identity->physics.ground_acceleration >= 0.0f &&
		identity->physics.air_acceleration >= 0.0f &&
		identity->physics.water_acceleration >= 0.0f &&
		identity->physics.hook_acceleration >= 0.0f &&
		identity->physics.external_acceleration >= 0.0f &&
		identity->physics.water_drag >= 0.0f &&
		identity->physics.max_velocity > 0.0f &&
		identity->physics.frame_ms != 0U &&
		identity->physics.substep_ms != 0U;
}

static int ContentIdentityValid(const sg_bsp_content_identity_t *identity)
{
	uint32_t index;
	int any = 0;

	if (!identity)
		return 0;
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
	{
		if (identity->bytes[index] != 0U)
			any = 1;
	}
	return any;
}

static int ModelIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	uint32_t axis;

	if (!left || !right || left->bsp_content_id != right->bsp_content_id ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->source_set_identity != right->source_set_identity ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity ||
		left->physics.frame_ms != right->physics.frame_ms ||
		left->physics.substep_ms != right->physics.substep_ms)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (left->standing_hull.mins.value[axis] !=
				right->standing_hull.mins.value[axis] ||
			left->standing_hull.maxs.value[axis] !=
				right->standing_hull.maxs.value[axis] ||
			left->crouching_hull.mins.value[axis] !=
				right->crouching_hull.mins.value[axis] ||
			left->crouching_hull.maxs.value[axis] !=
				right->crouching_hull.maxs.value[axis])
			return 0;
	}
	return left->physics.gravity == right->physics.gravity &&
		left->physics.ground_acceleration ==
			right->physics.ground_acceleration &&
		left->physics.air_acceleration == right->physics.air_acceleration &&
		left->physics.water_acceleration ==
			right->physics.water_acceleration &&
		left->physics.hook_acceleration == right->physics.hook_acceleration &&
		left->physics.external_acceleration ==
			right->physics.external_acceleration &&
		left->physics.water_drag == right->physics.water_drag &&
		left->physics.max_velocity == right->physics.max_velocity;
}

static int RuntimeShapeValid(const sg_host_engine_runtime_t *runtime)
{
	return runtime && runtime->state == SG_HOST_ENGINE_RUNTIME_STATE &&
		runtime->state_inverse == SG_HOST_ENGINE_RUNTIME_STATE_INVERSE &&
		runtime->owner == (const void *)&gi && runtime->trace &&
		runtime->pointcontents && runtime->pmove && runtime->mapname[0] != '\0';
}

static int LevelEqual(const sg_level_identity_t *left,
	const sg_level_identity_t *right)
{
	return left && right && left->bsp_checksum == right->bsp_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_id == right->host_physics_id &&
		memcmp(left->mapname, right->mapname,
			SG_LEVEL_IDENTITY_MAPNAME_BYTES) == 0;
}

static int RuntimeCallbacksCurrent(const sg_host_engine_runtime_t *runtime)
{
	return RuntimeShapeValid(runtime) &&
		runtime->trace == (sg_host_engine_trace_function_t)gi.trace &&
		runtime->pointcontents ==
			(sg_host_engine_contents_function_t)gi.pointcontents &&
		runtime->pmove == (sg_host_pmove_function_t)gi.Pmove;
}

static int RuntimeSubjectCurrent(const sg_host_engine_runtime_t *runtime)
{
	const edict_t *subject;

	if (!RuntimeShapeValid(runtime) || !runtime->subject ||
		!globals.edicts || globals.num_edicts <= 0 ||
		runtime->subject_index >= (uint32_t)globals.num_edicts)
		return 0;
	subject = &globals.edicts[runtime->subject_index];
	return subject == runtime->subject && subject->inuse && subject->client &&
		subject->s.number == runtime->subject_number &&
		subject->client == runtime->subject_client;
}

static int AcceptanceValid(
	const sg_host_engine_runtime_acceptance_t *acceptance)
{
	return acceptance &&
		acceptance->state == SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_STATE &&
		acceptance->state_inverse == ~SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_STATE &&
		acceptance->owner == SG_HostEngineRuntimeOwnerToken() &&
		acceptance->snapshot &&
		SG_RuneRuntimeSnapshotValid(acceptance->snapshot) &&
		ContentIdentityValid(&acceptance->content_identity);
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeBegin(
	const char *mapname, sg_host_engine_runtime_t **runtime_out)
{
	sg_host_engine_runtime_t *runtime;
	sg_level_identity_t level;
	sg_identity_status_t identity_status;

	if (!runtime_out || *runtime_out || !mapname)
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	*runtime_out = NULL;
	identity_status = SG_LevelIdentitySnapshot(mapname, &level);
	if (identity_status != SG_IDENTITY_OK)
		return identity_status == SG_IDENTITY_INVALID_ARGUMENT ||
			identity_status == SG_IDENTITY_INVALID_MAPNAME ?
			SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT :
			SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	/* The identity provider is an owner boundary too.  Do not let a provider
	 * that returned a different committed map silently bind callbacks to this
	 * level's runtime. */
	if (strcmp(level.mapname, mapname) != 0)
		return SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	if (!gi.trace || !gi.pointcontents || !gi.Pmove)
		return SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE;
	runtime = calloc(1U, sizeof(*runtime));
	if (!runtime)
		return SG_HOST_ENGINE_RUNTIME_ALLOCATION_FAILED;
	runtime->state = SG_HOST_ENGINE_RUNTIME_STATE;
	runtime->state_inverse = SG_HOST_ENGINE_RUNTIME_STATE_INVERSE;
	runtime->owner = (const void *)&gi;
	runtime->trace = (sg_host_engine_trace_function_t)gi.trace;
	runtime->pointcontents =
		(sg_host_engine_contents_function_t)gi.pointcontents;
	runtime->pmove = (sg_host_pmove_function_t)gi.Pmove;
	runtime->level = level;
	memcpy(runtime->mapname, level.mapname, sizeof(runtime->mapname));
	*runtime_out = runtime;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeAcceptanceIssueOwner(
	const void *owner_token, const sg_rune_runtime_snapshot_t *snapshot,
	const sg_bsp_content_identity_t *content_identity,
	sg_host_engine_runtime_acceptance_t **acceptance_out)
{
	sg_host_engine_runtime_acceptance_t *acceptance;

	if (!acceptance_out || *acceptance_out ||
		owner_token != SG_HostEngineRuntimeOwnerToken())
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	*acceptance_out = NULL;
	if (!snapshot || !SG_RuneRuntimeSnapshotValid(snapshot))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	if (!ContentIdentityValid(content_identity))
		return SG_HOST_ENGINE_RUNTIME_INVALID_CONTENT_ID;
	acceptance = calloc(1U, sizeof(*acceptance));
	if (!acceptance)
		return SG_HOST_ENGINE_RUNTIME_ALLOCATION_FAILED;
	acceptance->state = SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_STATE;
	acceptance->state_inverse = ~SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_STATE;
	acceptance->owner = SG_HostEngineRuntimeOwnerToken();
	acceptance->snapshot = snapshot;
	acceptance->content_identity = *content_identity;
	*acceptance_out = acceptance;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

void SG_HostEngineRuntimeAcceptanceDestroyOwner(
	sg_host_engine_runtime_acceptance_t *acceptance)
{
	if (!acceptance)
		return;
	acceptance->state = 0U;
	acceptance->state_inverse = 0U;
	acceptance->owner = NULL;
	acceptance->snapshot = NULL;
	memset(&acceptance->content_identity, 0,
		sizeof(acceptance->content_identity));
	free(acceptance);
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeJoinOwner(
	sg_host_engine_runtime_t *runtime, const void *owner_token,
	const sg_host_engine_runtime_acceptance_t *acceptance)
{
	const sg_rune_model_identity_t *identity;

	if (owner_token != SG_HostEngineRuntimeOwnerToken())
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	if (!RuntimeShapeValid(runtime) || !AcceptanceValid(acceptance))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	identity = &acceptance->snapshot->model->identity;
	if (!IdentityValid(identity))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	if (!RuntimeCallbacksCurrent(runtime))
		return SG_HOST_ENGINE_RUNTIME_DRIFT;
	if (!SG_HostEngineRuntimeCurrent(runtime))
		return SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	runtime->accepted_snapshot = acceptance->snapshot;
	runtime->identity = *identity;
	runtime->content_identity = acceptance->content_identity;
	runtime->generation = acceptance->snapshot->identity;
	runtime->topology_revision = acceptance->snapshot->topology_revision;
	runtime->accepted = 1;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeBindSubjectOwner(
	sg_host_engine_runtime_t *runtime, const void *owner_token,
	uint32_t subject_index)
{
	edict_t *subject;

	if (owner_token != SG_HostEngineRuntimeOwnerToken())
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	if (!RuntimeShapeValid(runtime) || !runtime->accepted ||
		!RuntimeCallbacksCurrent(runtime) || !SG_HostEngineRuntimeCurrent(runtime))
		return SG_HOST_ENGINE_RUNTIME_NOT_ACCEPTED;
	if (!globals.edicts || globals.num_edicts <= 0 ||
		subject_index >= (uint32_t)globals.num_edicts)
		return SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE;
	subject = &globals.edicts[subject_index];
	if (!subject->inuse || !subject->client || subject->s.number <= 0 ||
		(uint32_t)subject->s.number != subject_index)
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	runtime->subject = subject;
	runtime->subject_index = subject_index;
	runtime->subject_client = subject->client;
	runtime->subject_number = subject->s.number;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

int SG_HostEngineRuntimeCurrent(const sg_host_engine_runtime_t *runtime)
{
	sg_level_identity_t current;

	if (!RuntimeCallbacksCurrent(runtime) ||
		SG_LevelIdentitySnapshot(runtime->mapname, &current) != SG_IDENTITY_OK ||
		!LevelEqual(&runtime->level, &current))
		return 0;
	return 1;
}

int SG_HostEngineRuntimeAccepted(const sg_host_engine_runtime_t *runtime)
{
	return RuntimeShapeValid(runtime) && runtime->accepted &&
		runtime->accepted_snapshot &&
		SG_RuneRuntimeSnapshotValid(runtime->accepted_snapshot) &&
		ModelIdentityEqual(&runtime->identity,
			&runtime->accepted_snapshot->model->identity) &&
		runtime->generation == runtime->accepted_snapshot->identity &&
		runtime->topology_revision ==
			runtime->accepted_snapshot->topology_revision &&
		IdentityValid(&runtime->identity) &&
		ContentIdentityValid(&runtime->content_identity) &&
		runtime->generation != 0U && runtime->topology_revision != 0U &&
		SG_HostEngineRuntimeCurrent(runtime);
}

static int TraceArgumentsValid(const float start[3], const float mins[3],
	const float maxs[3], const float end[3])
{
	return FiniteVector(start) && FiniteVector(end) &&
		(!mins || FiniteVector(mins)) && (!maxs || FiniteVector(maxs));
}

static int EntityCurrent(const edict_t *entity, uint32_t *index_out)
{
	int index;

	if (!entity || !index_out || !globals.edicts || globals.num_edicts <= 0)
		return 0;
	/* The engine uses the level world edict (slot zero) as a legitimate trace
	 * and ground identity.  It has no client and is not an ordinary numbered
	 * mover, so authenticate it by the owner array address rather than by the
	 * nonzero entity-number rule used below. */
	if (entity == &globals.edicts[0])
	{
		*index_out = 0U;
		return 1;
	}
	for (index = 0; index < globals.num_edicts; index++)
		if (&globals.edicts[index] == entity)
		{
			if (!entity->inuse || entity->s.number <= 0 ||
				(uint32_t)entity->s.number != (uint32_t)index)
				return 0;
			*index_out = (uint32_t)index;
			return 1;
		}
	return 0;
}

static int TraceConvert(const trace_t *source,
	sg_host_collision_trace_t *destination)
{
	uint32_t entity_index;

	if (!source || !destination || !isfinite(source->fraction) ||
		source->fraction < 0.0f || source->fraction > 1.0f ||
		!FiniteVector(source->endpos) || !FiniteVector(source->plane.normal) ||
		!isfinite(source->plane.dist) || source->contents < 0)
		return 0;
	memset(destination, 0, sizeof(*destination));
	destination->allsolid = source->allsolid ? 1 : 0;
	destination->startsolid = source->startsolid ? 1 : 0;
	destination->fraction = source->fraction;
	memcpy(destination->end, source->endpos, sizeof(destination->end));
	memcpy(destination->plane.normal, source->plane.normal,
		sizeof(destination->plane.normal));
	destination->plane.distance = source->plane.dist;
	destination->plane.type = source->plane.type;
	destination->contents = (sg_host_collision_contents_t)source->contents;
	destination->texinfo = SG_HOST_COLLISION_TEXINFO_NONE;
	destination->surface_flags = source->surface ? source->surface->flags : 0;
	destination->model_index = SG_HOST_COLLISION_MODEL_WORLD;
	destination->instance_id = 0U;
	if (source->ent)
	{
		if (!EntityCurrent(source->ent, &entity_index))
			return 0;
		if (entity_index == 0U)
		{
			/* WORLD is represented by model zero in the normalized API even
			 * though the engine world edict commonly carries model index 1. */
			destination->model_index = SG_HOST_COLLISION_MODEL_WORLD;
			destination->instance_id = 0U;
		}
		else
		{
			if (source->ent->s.modelindex <= 0)
				return 0;
			destination->model_index = (uint32_t)source->ent->s.modelindex;
			destination->instance_id = (uint64_t)entity_index;
		}
	}
	return 1;
}

int SG_HostEngineRuntimeTrace(const sg_host_engine_runtime_t *runtime,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	float start_copy[3];
	float mins_copy[3];
	float maxs_copy[3];
	float end_copy[3];
	trace_t trace;

	if (!SG_HostEngineRuntimeAccepted(runtime) ||
		!RuntimeSubjectCurrent(runtime) ||
		!TraceArgumentsValid(start, mins, maxs, end) || !trace_out)
		return 0;
	memcpy(start_copy, start, sizeof(start_copy));
	memcpy(end_copy, end, sizeof(end_copy));
	if (mins)
		memcpy(mins_copy, mins, sizeof(mins_copy));
	if (maxs)
		memcpy(maxs_copy, maxs, sizeof(maxs_copy));
	trace = runtime->trace(start_copy, mins ? mins_copy : NULL,
		maxs ? maxs_copy : NULL, end_copy, runtime->subject, (int)mask);
	return TraceConvert(&trace, trace_out);
}

int SG_HostEngineRuntimePointContents(
	const sg_host_engine_runtime_t *runtime, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	float point_copy[3];
	int contents;

	if (!SG_HostEngineRuntimeAccepted(runtime) || !FiniteVector(point) ||
		!contents_out)
		return 0;
	memcpy(point_copy, point, sizeof(point_copy));
	contents = runtime->pointcontents(point_copy);
	if (contents < 0)
		return 0;
	*contents_out = (sg_host_collision_contents_t)contents;
	return 1;
}

static trace_t RuntimePmoveTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	sg_host_collision_trace_t host_trace;
	sg_host_engine_runtime_scope_t *scope = sg_host_engine_runtime_scope;

	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (!scope || !RuntimeSubjectCurrent(scope->runtime) ||
		!TraceArgumentsValid(start, mins, maxs, end))
	{
		if (scope)
			scope->collision_failed = 1;
		trace.allsolid = true;
		trace.startsolid = true;
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.contents = CONTENTS_SOLID;
		return trace;
	}
	/* Keep the engine trace object intact inside the private Pmove scope.  In
	 * particular, `ent` is the engine's authenticated ground/mover identity;
	 * reconstructing a normalized trace here would turn every support into
	 * WORLD and make grounded movement observably wrong. */
	trace = scope->runtime->trace(start, mins, maxs, end,
		scope->runtime->subject, (int)SG_HOST_MASK_PLAYER_SOLID);
	if (!TraceConvert(&trace, &host_trace))
	{
		scope->collision_failed = 1;
		memset(&trace, 0, sizeof(trace));
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.allsolid = true;
		trace.startsolid = true;
		trace.contents = CONTENTS_SOLID;
	}
	return trace;
}

static int RuntimePmoveContents(vec3_t point)
{
	sg_host_collision_contents_t contents;

	if (!sg_host_engine_runtime_scope ||
		!SG_HostEngineRuntimePointContents(sg_host_engine_runtime_scope->runtime,
			point, &contents))
	{
		if (sg_host_engine_runtime_scope)
			sg_host_engine_runtime_scope->collision_failed = 1;
		return CONTENTS_SOLID;
	}
	return (int)contents;
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

static int HullMatchesIdentity(const sg_rune_model_identity_t *identity,
	const pmove_t *pmove, short expected_gravity)
{
	const sg_rune_hull_profile_t *hull = (pmove->s.pm_flags & PMF_DUCKED) ?
		&identity->crouching_hull : &identity->standing_hull;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (pmove->mins[axis] != hull->mins.value[axis] ||
			pmove->maxs[axis] != hull->maxs.value[axis])
			return 0;
	return pmove->s.gravity == expected_gravity;
}

int SG_HostEngineRuntimePmove(const sg_host_engine_runtime_t *runtime,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	const sg_rune_model_identity_t *identity;
	sg_host_engine_runtime_scope_t scope;
	pmove_state_t previous;
	pmove_state_t state;
	pmove_t pm;
	short gravity;
	short effective_gravity;
	uint32_t steps;
	uint32_t step;

	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	if (!result_out || !request || request->state.pm_type != PM_NORMAL)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (!SG_HostEngineRuntimeAccepted(runtime))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	if (!RuntimeSubjectCurrent(runtime))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	if (sg_host_engine_runtime_scope)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_REENTRANT;
		return 0;
	}
	identity = &runtime->identity;
	if (identity->physics.substep_ms == 0U ||
		identity->physics.substep_ms > UCHAR_MAX ||
		identity->physics.frame_ms == 0U ||
		identity->physics.frame_ms % identity->physics.substep_ms != 0U)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING;
		return 0;
	}
	if (!isfinite(identity->physics.gravity) || identity->physics.gravity < 0.0f ||
		identity->physics.gravity > (float)SHRT_MAX ||
		truncf(identity->physics.gravity) != identity->physics.gravity)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY;
		return 0;
	}
	if (!RequestGravity(request, (short)identity->physics.gravity,
		&effective_gravity))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
		return 0;
	}
	gravity = effective_gravity;
	steps = identity->physics.frame_ms / identity->physics.substep_ms;
	state = request->state;
	previous = request->previous_state;
	memset(result_out, 0, sizeof(*result_out));
	memset(&scope, 0, sizeof(scope));
	scope.runtime = runtime;
	sg_host_engine_runtime_scope = &scope;
	memset(&pm, 0, sizeof(pm));
	for (step = 0U; step < steps; step++)
	{
		memset(&pm, 0, sizeof(pm));
		pm.s = state;
		pm.s.gravity = gravity;
		pm.cmd = request->command;
		pm.cmd.msec = (byte)identity->physics.substep_ms;
		pm.snapinitial = memcmp(&previous, &pm.s, sizeof(pm.s)) != 0;
		pm.trace = RuntimePmoveTrace;
		pm.pointcontents = RuntimePmoveContents;
		if (!RuntimeCallbacksCurrent(runtime))
			break;
		runtime->pmove(&pm);
		if (scope.collision_failed || !HullMatchesIdentity(identity, &pm,
			effective_gravity))
			break;
		state = pm.s;
		previous = pm.s;
	}
	sg_host_engine_runtime_scope = NULL;
	if (step != steps)
	{
		if (error_out)
			*error_out = scope.collision_failed ? SG_HOST_PMOVE_ERROR_COLLISION :
				SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	result_out->state = pm.s;
	memcpy(result_out->mins, pm.mins, sizeof(result_out->mins));
	memcpy(result_out->maxs, pm.maxs, sizeof(result_out->maxs));
	memcpy(result_out->view_angles, pm.viewangles,
		sizeof(result_out->view_angles));
	result_out->view_height = pm.viewheight;
	result_out->grounded = pm.groundentity != NULL;
	if (pm.groundentity)
	{
		uint32_t ground_index;

		if (!EntityCurrent(pm.groundentity, &ground_index))
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		result_out->support_model_index = ground_index == 0U ?
			SG_HOST_COLLISION_MODEL_WORLD :
			(uint32_t)pm.groundentity->s.modelindex;
		if (ground_index != 0U && pm.groundentity->s.modelindex <= 0)
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		result_out->support_instance_id = (uint64_t)ground_index;
	}
	for (step = 0U; step < 3U; step++)
	{
		result_out->origin[step] = pm.s.origin[step] * 0.125f;
		result_out->velocity[step] = pm.s.velocity[step] * 0.125f;
	}
	result_out->water_type = pm.watertype;
	result_out->water_level = pm.waterlevel;
	result_out->evaluated_steps = steps;
	result_out->elapsed_ms = identity->physics.frame_ms;
	result_out->gravity = (float)effective_gravity;
	result_out->physics_abi_id = identity->physics_abi_id;
	result_out->gravity_law_id = HookGravityActive(request) ?
		SG_HOST_PMOVE_HOOK_LAW_ID : UINT64_C(0);
	return 1;
}

const sg_rune_model_identity_t *SG_HostEngineRuntimeIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? &runtime->identity : NULL;
}

const sg_bsp_content_identity_t *SG_HostEngineRuntimeContentIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? &runtime->content_identity :
		NULL;
}

uint64_t SG_HostEngineRuntimeGeneration(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? runtime->generation : 0U;
}

uint64_t SG_HostEngineRuntimeTopologyRevision(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ?
		runtime->topology_revision : 0U;
}

uint64_t SG_HostEngineRuntimeSubjectInstance(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) &&
		RuntimeSubjectCurrent(runtime) ? (uint64_t)runtime->subject_index : 0U;
}

void SG_HostEngineRuntimeDestroy(sg_host_engine_runtime_t *runtime)
{
	if (!runtime)
		return;
	runtime->accepted = 0;
	runtime->state = 0U;
	runtime->state_inverse = 0U;
	runtime->owner = NULL;
	runtime->trace = NULL;
	runtime->pointcontents = NULL;
	runtime->pmove = NULL;
	runtime->accepted_snapshot = NULL;
	runtime->subject = NULL;
	runtime->subject_client = NULL;
	free(runtime);
}

const char *SG_HostEngineRuntimeStatusString(
	sg_host_engine_runtime_status_t status)
{
	switch (status)
	{
	case SG_HOST_ENGINE_RUNTIME_OK: return "ok";
	case SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
	case SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE: return "host unavailable";
	case SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE: return "level unavailable";
	case SG_HOST_ENGINE_RUNTIME_NOT_ACCEPTED: return "not accepted";
	case SG_HOST_ENGINE_RUNTIME_DRIFT: return "runtime drift";
	case SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY: return "invalid identity";
	case SG_HOST_ENGINE_RUNTIME_INVALID_CONTENT_ID:
		return "invalid BSP content identity";
	case SG_HOST_ENGINE_RUNTIME_EVALUATION_FAILED:
		return "evaluation failed";
	case SG_HOST_ENGINE_RUNTIME_ALLOCATION_FAILED:
		return "allocation failed";
	default: return "unknown runtime status";
	}
}
