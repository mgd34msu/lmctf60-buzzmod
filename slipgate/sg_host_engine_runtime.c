#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world

#include "sg_host_engine_runtime.h"
#include "sg_host_engine_runtime_private.h"
#include "sg_host_engine_pmove.h"
#include "sg_client_ownership.h"

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
	sg_host_static_identity_t static_identity;
};

typedef struct sg_host_engine_subject_s
{
	edict_t *entity;
	uint32_t index;
	const gclient_t *client;
	int number;
	uint64_t spawn_generation;
} sg_host_engine_subject_t;

typedef struct sg_host_engine_runtime_scope_s
{
	const sg_host_engine_runtime_t *runtime;
	sg_host_engine_subject_t subject;
	int collision_failed;
	int trace_mask;
	const pmove_t *pmove;
	sg_host_pmove_trace_t *traces;
	size_t trace_capacity;
	uint32_t substep;
	uint64_t trace_count;
	uint64_t collision_trace_count;
	int trace_capacity_failed;
} sg_host_engine_runtime_scope_t;

static sg_host_engine_runtime_scope_t *sg_host_engine_runtime_scope;

static int FiniteVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}


static int RuntimeShapeValid(const sg_host_engine_runtime_t *runtime)
{
	return runtime && runtime->state == SG_HOST_ENGINE_RUNTIME_STATE &&
		runtime->state_inverse == SG_HOST_ENGINE_RUNTIME_STATE_INVERSE &&
		runtime->owner == (const void *)&gi && runtime->trace &&
		runtime->pointcontents && runtime->pmove && runtime->mapname[0] != '\0';
}

/* Live physics belongs to the production runtime, not the portable Pmove ABI
 * adapter.  This keeps construction-only users linkable without inventing
 * fallback cvars while the production owner still captures exact engine law. */
int SG_HostEnginePhysicsLaw(sg_rune_physics_parameters_t *law_out)
{
	cvar_t *airaccelerate;
	float frame_ms;

	if (!law_out || !gi.cvar || !sv_gravity || !sv_maxvelocity ||
		!want_funky_gravity)
		return 0;
	airaccelerate = gi.cvar("sv_airaccelerate", "0", 0);
	frame_ms = FRAMETIME * 1000.0f;
	if (!airaccelerate || !isfinite(airaccelerate->value) ||
		airaccelerate->value != 0.0f ||
		!isfinite(sv_gravity->value) || sv_gravity->value < 1.0f ||
		sv_gravity->value > (float)SHRT_MAX ||
		truncf(sv_gravity->value) != sv_gravity->value ||
		!isfinite(sv_maxvelocity->value) || sv_maxvelocity->value <= 0.0f ||
		!isfinite(want_funky_gravity->value) ||
		want_funky_gravity->value != 0.0f || !isfinite(frame_ms) ||
		frame_ms <= 0.0f || truncf(frame_ms) != frame_ms ||
		frame_ms > (float)UINT32_MAX)
		return 0;
	memset(law_out, 0, sizeof(*law_out));
	law_out->gravity = sv_gravity->value;
	law_out->ground_acceleration = SG_HOST_ENGINE_GROUND_ACCELERATION;
	law_out->air_acceleration = SG_HOST_ENGINE_AIR_ACCELERATION;
	law_out->water_acceleration = SG_HOST_ENGINE_WATER_ACCELERATION;
	law_out->hook_acceleration = SG_HOST_ENGINE_HOOK_ACCELERATION;
	law_out->external_acceleration = SG_HOST_ENGINE_EXTERNAL_ACCELERATION;
	law_out->water_drag = SG_HOST_ENGINE_WATER_DRAG;
	law_out->max_velocity = sv_maxvelocity->value;
	law_out->frame_ms = (uint32_t)frame_ms;
	law_out->substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return 1;
}

static int LevelEqual(const sg_level_identity_t *left,
	const sg_level_identity_t *right)
{
	return left && right && left->bsp_checksum == right->bsp_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_id == right->host_physics_id &&
		left->bsp_bytes == right->bsp_bytes &&
		memcmp(left->bsp_sha256, right->bsp_sha256,
			SG_LEVEL_BSP_SHA256_BYTES) == 0 &&
		memcmp(left->mapname, right->mapname,
			SG_LEVEL_IDENTITY_MAPNAME_BYTES) == 0;
}

static int SameFloat(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int StaticIdentityEqual(const sg_host_static_identity_t *left,
	const sg_host_static_identity_t *right)
{
	const float *left_physics;
	const float *right_physics;
	uint32_t axis;
	uint32_t index;

	if (!left || !right ||
		memcmp(&left->bsp_identity, &right->bsp_identity,
			sizeof(left->bsp_identity)) != 0 ||
		left->bsp_bytes != right->bsp_bytes ||
		left->engine_checksum != right->engine_checksum ||
		left->entity_crc32 != right->entity_crc32 ||
		left->host_physics_epoch != right->host_physics_epoch ||
		left->reserved != right->reserved ||
		left->physics_abi_id != right->physics_abi_id ||
		left->physics.frame_ms != right->physics.frame_ms ||
		left->physics.substep_ms != right->physics.substep_ms)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SameFloat(left->standing_hull.mins.value[axis],
				right->standing_hull.mins.value[axis]) ||
			!SameFloat(left->standing_hull.maxs.value[axis],
				right->standing_hull.maxs.value[axis]) ||
			!SameFloat(left->crouching_hull.mins.value[axis],
				right->crouching_hull.mins.value[axis]) ||
			!SameFloat(left->crouching_hull.maxs.value[axis],
				right->crouching_hull.maxs.value[axis]))
			return 0;
	left_physics = &left->physics.gravity;
	right_physics = &right->physics.gravity;
	for (index = 0U; index < 8U; index++)
		if (!SameFloat(left_physics[index], right_physics[index]))
			return 0;
	return 1;
}

static int BuildStaticIdentity(const sg_level_identity_t *level_identity,
	sg_host_static_identity_t *identity_out)
{
	if (!level_identity || !identity_out || level_identity->bsp_bytes == 0U)
		return 0;
	memset(identity_out, 0, sizeof(*identity_out));
	memcpy(identity_out->bsp_identity.bytes, level_identity->bsp_sha256,
		sizeof(identity_out->bsp_identity.bytes));
	identity_out->bsp_bytes = level_identity->bsp_bytes;
	identity_out->engine_checksum = level_identity->bsp_checksum;
	identity_out->entity_crc32 = level_identity->entity_crc32;
	identity_out->host_physics_epoch = level_identity->host_physics_id;
	identity_out->physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	return SG_HostEngineHullProfiles(&identity_out->standing_hull,
		&identity_out->crouching_hull) &&
		SG_HostEnginePhysicsLaw(&identity_out->physics);
}

static int RuntimeCallbacksCurrent(const sg_host_engine_runtime_t *runtime)
{
	return RuntimeShapeValid(runtime) &&
		runtime->trace == (sg_host_engine_trace_function_t)gi.trace &&
		runtime->pointcontents ==
			(sg_host_engine_contents_function_t)gi.pointcontents &&
		runtime->pmove == (sg_host_pmove_function_t)gi.Pmove;
}

static int RuntimeSubjectCurrent(const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_t *subject)
{
	edict_t *current;

	if (!RuntimeShapeValid(runtime) || !runtime->accepted || !subject ||
		!subject->entity ||
		!globals.edicts || globals.num_edicts <= 0 ||
		subject->index >= (uint32_t)globals.num_edicts)
		return 0;
	current = &globals.edicts[subject->index];
	return current == subject->entity && current->inuse && current->client &&
		current->client->pers.connected && current->classname &&
		strcmp(current->classname, "player") == 0 &&
		current->s.number == subject->number && current->client == subject->client &&
		current->client->ctf.ctfid == subject->spawn_generation &&
		subject->spawn_generation != 0U &&
		SG_OwnsBot(current);
}

static int RuntimeBotSubject(const sg_host_engine_runtime_t *runtime,
	uint32_t subject_index, sg_host_engine_subject_t *subject_out)
{
	edict_t *subject;

	if (!subject_out || !SG_HostEngineRuntimeAccepted(runtime) ||
		!globals.edicts || globals.num_edicts <= 0 ||
		subject_index >= (uint32_t)globals.num_edicts)
		return 0;
	memset(subject_out, 0, sizeof(*subject_out));
	subject = &globals.edicts[subject_index];
	if (!subject->inuse || !subject->client ||
		!subject->client->pers.connected || !subject->classname ||
		strcmp(subject->classname, "player") != 0 || subject->s.number <= 0 ||
		(uint32_t)subject->s.number != subject_index ||
		subject->client->ctf.ctfid == 0U || !SG_OwnsBot(subject))
		return 0;
	subject_out->entity = subject;
	subject_out->index = subject_index;
	subject_out->client = subject->client;
	subject_out->number = subject->s.number;
	subject_out->spawn_generation = subject->client->ctf.ctfid;
	return RuntimeSubjectCurrent(runtime, subject_out);
}

static int RuntimeExactBotSubject(const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity,
	sg_host_engine_subject_t *subject_out)
{
	return identity && identity->reserved == 0U &&
		identity->spawn_generation != 0U &&
		RuntimeBotSubject(runtime, identity->client_id, subject_out) &&
		subject_out->spawn_generation == identity->spawn_generation;
}

int SG_HostEngineRuntimeOwnerSubject(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	sg_host_engine_subject_identity_t *subject_out)
{
	sg_host_engine_subject_t subject;

	if (subject_out)
		memset(subject_out, 0, sizeof(*subject_out));
	if (!subject_out || !RuntimeBotSubject(runtime, subject_index, &subject))
		return 0;
	subject_out->client_id = subject.index;
	subject_out->spawn_generation = subject.spawn_generation;
	return RuntimeSubjectCurrent(runtime, &subject);
}

int SG_HostEngineRuntimeOwnerSubjectCurrent(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity)
{
	sg_host_engine_subject_t subject;

	return RuntimeExactBotSubject(runtime, identity, &subject) &&
		RuntimeSubjectCurrent(runtime, &subject);
}

int SG_HostEngineRuntimeOwnerSubjectState(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity,
	sg_host_pmove_state_observation_t *observation_out)
{
	sg_host_engine_subject_t subject;
	uint32_t axis;

	if (observation_out)
		memset(observation_out, 0, sizeof(*observation_out));
	if (!observation_out ||
		!RuntimeExactBotSubject(runtime, identity, &subject))
		return 0;
	observation_out->state = subject.client->ps.pmove;
	for (axis = 0U; axis < 3U; axis++)
	{
		observation_out->origin[axis] =
			observation_out->state.origin[axis] * 0.125f;
		observation_out->velocity[axis] =
			observation_out->state.velocity[axis] * 0.125f;
		if (!SameFloat(observation_out->origin[axis],
				subject.entity->s.origin[axis]) ||
			!SameFloat(observation_out->velocity[axis],
				subject.entity->velocity[axis]))
		{
			memset(observation_out, 0, sizeof(*observation_out));
			return 0;
		}
	}
	if (!RuntimeSubjectCurrent(runtime, &subject))
	{
		memset(observation_out, 0, sizeof(*observation_out));
		return 0;
	}
	return 1;
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeBegin(
	const char *mapname, sg_host_engine_runtime_t **runtime_out)
{
	sg_host_engine_runtime_t *runtime;
	sg_level_identity_t level_identity;
	sg_identity_status_t identity_status;

	if (!runtime_out || *runtime_out || !mapname)
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	*runtime_out = NULL;
	identity_status = SG_LevelIdentitySnapshot(mapname, &level_identity);
	if (identity_status != SG_IDENTITY_OK)
		return identity_status == SG_IDENTITY_INVALID_ARGUMENT ||
			identity_status == SG_IDENTITY_INVALID_MAPNAME ?
			SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT :
			SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	/* The identity provider is an owner boundary too.  Do not let a provider
	 * that returned a different committed map silently bind callbacks to this
	 * level's runtime. */
	if (strcmp(level_identity.mapname, mapname) != 0)
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
	runtime->level = level_identity;
	if (!BuildStaticIdentity(&level_identity, &runtime->static_identity))
	{
		free(runtime);
		return SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE;
	}
	memcpy(runtime->mapname, level_identity.mapname, sizeof(runtime->mapname));
	*runtime_out = runtime;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

void SG_HostEngineRuntimeOwnerClearAcceptance(
	sg_host_engine_runtime_t *runtime)
{
	if (!RuntimeShapeValid(runtime))
		return;
	runtime->accepted = 0;
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerActivate(
	sg_host_engine_runtime_t *runtime)
{
	sg_host_static_identity_t current;

	if (!RuntimeShapeValid(runtime))
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	SG_HostEngineRuntimeOwnerClearAcceptance(runtime);
	if (!RuntimeCallbacksCurrent(runtime) || !SG_HostEngineRuntimeCurrent(runtime) ||
		!BuildStaticIdentity(&runtime->level, &current) ||
		!StaticIdentityEqual(&runtime->static_identity, &current))
		return SG_HOST_ENGINE_RUNTIME_DRIFT;
	runtime->accepted = 1;
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
	sg_host_static_identity_t current;

	return RuntimeShapeValid(runtime) && runtime->accepted &&
		RuntimeCallbacksCurrent(runtime) && SG_HostEngineRuntimeCurrent(runtime) &&
		BuildStaticIdentity(&runtime->level, &current) &&
		StaticIdentityEqual(&runtime->static_identity, &current);
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
	uint32_t subject_index, const float start[3], const float mins[3],
	const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	sg_host_engine_subject_t subject;
	float start_copy[3];
	float mins_copy[3];
	float maxs_copy[3];
	float end_copy[3];
	trace_t trace;

	if (!RuntimeBotSubject(runtime, subject_index, &subject) ||
		!TraceArgumentsValid(start, mins, maxs, end) || !trace_out)
		return 0;
	memcpy(start_copy, start, sizeof(start_copy));
	memcpy(end_copy, end, sizeof(end_copy));
	if (mins)
		memcpy(mins_copy, mins, sizeof(mins_copy));
	if (maxs)
		memcpy(maxs_copy, maxs, sizeof(maxs_copy));
	trace = runtime->trace(start_copy, mins ? mins_copy : NULL,
		maxs ? maxs_copy : NULL, end_copy, subject.entity, (int)mask);
	return RuntimeSubjectCurrent(runtime, &subject) &&
		TraceConvert(&trace, trace_out);
}

static sg_host_hook_target_kind_t RuntimeTargetKind(edict_t *entity)
{
	if (!entity)
		return SG_HOST_HOOK_TARGET_NONE;
	if (entity->classname && strcmp(entity->classname, "bodyque") == 0)
		return SG_HOST_HOOK_TARGET_BODYQUE;
	/* Use the same validator and ordering as hook_touch.  Its deathmatch and
	 * team rules are the human hook's definition of an attachable player. */
	if (ctf_validateplayer(entity, CTF_TEAM_ANYTEAM))
		return SG_HOST_HOOK_TARGET_PLAYER;
	if (entity == &globals.edicts[0] || (entity->classname &&
		strcmp(entity->classname, "worldspawn") == 0))
		return SG_HOST_HOOK_TARGET_WORLD;
	if (entity->classname && strncmp(entity->classname, "func", 4) == 0)
		return SG_HOST_HOOK_TARGET_FUNC;
	if (entity->classname && strncmp(entity->classname, "info_flag", 9) == 0)
		return SG_HOST_HOOK_TARGET_INFO_FLAG;
	return SG_HOST_HOOK_TARGET_OTHER;
}

static int RuntimeHookCollision(const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_t *subject, edict_t *target,
	uint32_t target_index, int32_t surface_flags,
	sg_host_hook_collision_t *collision_out)
{
	if (!runtime || !target || !collision_out)
		return 0;
	memset(collision_out, 0, sizeof(*collision_out));
	collision_out->hit = 1;
	collision_out->owner_hit = target == subject->entity;
	collision_out->sky =
		(surface_flags & (int32_t)SG_HOST_SURFACE_SKY) != 0;
	if (collision_out->owner_hit)
		return 1;
	collision_out->target_kind = RuntimeTargetKind(target);
	if (target_index == 0U)
	{
		uint32_t index;
		uint64_t identity = UINT64_C(1469598103934665603);

		for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
			identity = (identity ^ runtime->static_identity.bsp_identity.bytes[index]) *
				UINT64_C(1099511628211);
		collision_out->target_identity = identity ? identity : UINT64_C(1);
	}
	else
		collision_out->target_identity = (uint64_t)target_index;
	collision_out->target_dead = target->deadflag != DEAD_NO;
	if (target->client)
		collision_out->same_team = subject->entity->client->ctf.teamnum ==
			target->client->ctf.teamnum;
	return 1;
}

int SG_HostEngineRuntimeOwnerHookCollision(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_observation_t *observation_out)
{
	sg_host_engine_subject_t subject;
	sg_host_hook_collision_t collision;
	edict_t *hook;
	edict_t *target;
	uint32_t current_index;

	if (!observation_out || !RuntimeBotSubject(runtime, subject_index, &subject) ||
		hook_index >= (uint32_t)globals.num_edicts ||
		target_index >= (uint32_t)globals.num_edicts)
		return 0;
	hook = &globals.edicts[hook_index];
	if (!hook->inuse || hook->s.number < 0 ||
		(uint32_t)hook->s.number != hook_index || hook->owner != subject.entity ||
		subject.entity->client->hook != hook)
		return 0;
	target = &globals.edicts[target_index];
	if (!EntityCurrent(target, &current_index) || current_index != target_index)
		return 0;
	if (!RuntimeHookCollision(runtime, &subject, target, target_index,
		surface_flags, &collision) || !RuntimeSubjectCurrent(runtime, &subject))
		return 0;
	memset(observation_out, 0, sizeof(*observation_out));
	observation_out->event = hook->hook_target ? SG_HOST_HOOK_ATTACHED_TICK :
		SG_HOST_HOOK_FLIGHT_HIT;
	observation_out->phase = hook->hook_target ? SG_HOST_HOOK_ATTACHED :
		SG_HOST_HOOK_IN_FLIGHT;
	observation_out->first_hit = hook->hook_target == NULL;
	observation_out->frame = level.framenum < 0 ? 0U :
		(uint32_t)level.framenum;
	observation_out->last_damage_frame = hook->hook_lastframe < 0 ? 0U :
		(uint32_t)hook->hook_lastframe;
	observation_out->target_kind = collision.target_kind;
	observation_out->target_identity = collision.target_identity;
	observation_out->attached_target_identity = hook->hook_target ?
		collision.target_identity : 0U;
	observation_out->sky = collision.sky;
	observation_out->owner_hit = collision.owner_hit;
	observation_out->same_team = collision.same_team;
	observation_out->target_dead = collision.target_dead;
	return 1;
}

int SG_HostEngineRuntimeOwnerHookPullInputs(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, vec3_t start_out, vec3_t bite_out)
{
	sg_host_engine_subject_t subject;
	edict_t *hook;
	vec3_t forward;
	vec3_t right;

	if (!start_out || !bite_out ||
		!RuntimeBotSubject(runtime, subject_index, &subject) ||
		hook_index >= (uint32_t)globals.num_edicts)
		return 0;
	hook = &globals.edicts[hook_index];
	if (!hook->inuse || hook->s.number < 0 ||
		(uint32_t)hook->s.number != hook_index || hook->owner != subject.entity ||
		subject.entity->client->hook != hook)
		return 0;
	AngleVectors(subject.entity->client->v_angle, forward, right, NULL);
	CTF_HookMuzzle(subject.entity->s.origin, (float)subject.entity->viewheight,
		subject.entity->client->pers.hand, forward, right, start_out);
	if (hook->hook_target)
		VectorAdd(hook->hook_target->absmin, hook->hook_offset, bite_out);
	else
		VectorCopy(hook->s.origin, bite_out);
	return RuntimeSubjectCurrent(runtime, &subject) &&
		FiniteVector(start_out) && FiniteVector(bite_out);
}

int SG_HostEngineRuntimeHookTrace(const sg_host_engine_runtime_t *runtime,
	uint32_t subject_index, uint32_t hook_index,
	sg_host_collision_contents_t mask,
	sg_host_hook_collision_t *collision_out,
	sg_host_collision_trace_t *trace_out)
{
	sg_host_engine_subject_t subject;
	edict_t *hook;
	trace_t trace;
	float start_copy[3];
	float end_copy[3];
	sg_host_collision_trace_t normalized;
	uint32_t entity_index;
	edict_t *target;

	if (!collision_out || !trace_out ||
		!RuntimeBotSubject(runtime, subject_index, &subject) ||
		hook_index >= (uint32_t)globals.num_edicts)
		return 0;
	hook = &globals.edicts[hook_index];
	if (!hook->inuse || hook->s.number < 0 ||
		(uint32_t)hook->s.number != hook_index || hook->owner != subject.entity)
		return 0;
	memset(collision_out, 0, sizeof(*collision_out));
	memset(trace_out, 0, sizeof(*trace_out));
	memcpy(start_copy, subject.entity->s.origin, sizeof(start_copy));
	memcpy(end_copy, hook->s.origin, sizeof(end_copy));
	trace = runtime->trace(start_copy, NULL, NULL, end_copy,
		subject.entity, (int)mask);
	if (!RuntimeSubjectCurrent(runtime, &subject) ||
		!TraceConvert(&trace, &normalized))
		return 0;
	*trace_out = normalized;
	if (trace.fraction >= 1.0f && !trace.startsolid && !trace.allsolid)
		return 1;
	/* A hit must retain the engine's exact target pointer.  Treating a
	 * normalized model number as an entity class would allow a caller to turn
	 * any mover into a valid FUNC target. */
	if (!trace.ent || !EntityCurrent(trace.ent, &entity_index))
		return 0;
	target = trace.ent;
	if (!RuntimeHookCollision(runtime, &subject, target, entity_index,
		trace.surface ? trace.surface->flags : 0, collision_out))
		return 0;
	collision_out->trace_epsilon_applied = 1;
	/* T_Damage is deliberately not performed by this query.  The hook state
	 * machine consumes this owner-derived record, and a live weapon owner is
	 * responsible for reporting a post-damage death on its next touch. */
	return 1;
}

static int RuntimePointContents(const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_t *subject, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	float point_copy[3];
	int contents;

	if (!RuntimeSubjectCurrent(runtime, subject) || !FiniteVector(point) ||
		!contents_out)
		return 0;
	memcpy(point_copy, point, sizeof(point_copy));
	contents = runtime->pointcontents(point_copy);
	if (contents < 0)
		return 0;
	*contents_out = (sg_host_collision_contents_t)contents;
	return RuntimeSubjectCurrent(runtime, subject);
}

int SG_HostEngineRuntimeOwnerTrace(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	sg_host_engine_subject_t subject;
	float start_copy[3];
	float mins_copy[3];
	float maxs_copy[3];
	float end_copy[3];
	trace_t trace;

	if (!RuntimeExactBotSubject(runtime, identity, &subject) ||
		!TraceArgumentsValid(start, mins, maxs, end) || !trace_out)
		return 0;
	memcpy(start_copy, start, sizeof(start_copy));
	memcpy(end_copy, end, sizeof(end_copy));
	if (mins)
		memcpy(mins_copy, mins, sizeof(mins_copy));
	if (maxs)
		memcpy(maxs_copy, maxs, sizeof(maxs_copy));
	trace = runtime->trace(start_copy, mins ? mins_copy : NULL,
		maxs ? maxs_copy : NULL, end_copy, subject.entity, (int)mask);
	return RuntimeSubjectCurrent(runtime, &subject) &&
		TraceConvert(&trace, trace_out);
}

int SG_HostEngineRuntimeOwnerPointContents(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	sg_host_engine_subject_t subject;

	return RuntimeExactBotSubject(runtime, identity, &subject) &&
		RuntimePointContents(runtime, &subject, point, contents_out);
}

static const sg_rune_hull_profile_t *RuntimeStanceHull(
	const sg_host_engine_runtime_t *runtime, sg_rune_stance_t stance)
{
	if (!runtime)
		return NULL;
	if (stance == SG_RUNE_STANCE_STANDING)
		return &runtime->static_identity.standing_hull;
	if (stance == SG_RUNE_STANCE_CROUCHING)
		return &runtime->static_identity.crouching_hull;
	return NULL;
}

int SG_HostEngineRuntimeOwnerClassifyPose(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	const sg_rune_hull_profile_t *hull;
	float support_end[3];
	float sample[3];
	float view_height;
	int sample2;
	int sample1;
	sg_host_collision_contents_t contents;

	if (!pose_out || !FiniteVector(origin) ||
		!(hull = RuntimeStanceHull(runtime, stance)) ||
		!SG_HostEngineRuntimeOwnerSubjectCurrent(runtime, identity))
		return 0;
	memset(pose_out, 0, sizeof(*pose_out));
	pose_out->stance = stance;
	pose_out->hull = *hull;
	pose_out->gravity = runtime->static_identity.physics.gravity;
	pose_out->physics_abi_id = runtime->static_identity.physics_abi_id;
	if (!SG_HostEngineRuntimeOwnerTrace(runtime, identity, origin,
		hull->mins.value, hull->maxs.value, origin,
		SG_HOST_MASK_PLAYER_SOLID, &pose_out->occupancy))
		return 0;
	pose_out->valid = !pose_out->occupancy.allsolid;
	memcpy(support_end, origin, sizeof(support_end));
	support_end[2] -= SG_HOST_GROUND_PROBE;
	if (!SG_HostEngineRuntimeOwnerTrace(runtime, identity, origin,
		hull->mins.value, hull->maxs.value, support_end,
		SG_HOST_MASK_PLAYER_SOLID, &pose_out->support))
		return 0;
	pose_out->supported = pose_out->support.startsolid ||
		(pose_out->support.fraction < 1.0f &&
		 pose_out->support.plane.normal[2] >= SG_HOST_GROUND_NORMAL_Z);
	pose_out->support_is_mover = pose_out->supported &&
		pose_out->support.instance_id != 0U;
	view_height = stance == SG_RUNE_STANCE_STANDING ?
		SG_HOST_STANDING_VIEW_HEIGHT : SG_HOST_CROUCHING_VIEW_HEIGHT;
	{
		float sample_height = view_height - hull->mins.value[2];

		if (sample_height < (float)INT_MIN ||
			sample_height >= (float)INT_MAX)
			return 0;
		sample2 = (int)sample_height;
	}
	sample1 = sample2 / 2;
	memcpy(sample, origin, sizeof(sample));
	sample[2] = origin[2] + hull->mins.value[2] + 1.0f;
	if (!SG_HostEngineRuntimeOwnerPointContents(runtime, identity, sample,
		&contents))
		return 0;
	if ((contents & SG_HOST_MASK_WATER) != 0U)
	{
		pose_out->water_type = contents;
		pose_out->water_level = 1U;
		sample[2] = origin[2] + hull->mins.value[2] + (float)sample1;
		if (!SG_HostEngineRuntimeOwnerPointContents(runtime, identity, sample,
			&contents))
			return 0;
		if ((contents & SG_HOST_MASK_WATER) != 0U)
		{
			pose_out->water_level = 2U;
			sample[2] = origin[2] + hull->mins.value[2] + (float)sample2;
			if (!SG_HostEngineRuntimeOwnerPointContents(runtime, identity,
				sample, &contents))
				return 0;
			if ((contents & SG_HOST_MASK_WATER) != 0U)
				pose_out->water_level = 3U;
		}
	}
	return SG_HostEngineRuntimeOwnerSubjectCurrent(runtime, identity);
}

int SG_HostEngineRuntimeOwnerTransition(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out)
{
	const sg_rune_hull_profile_t *hull = RuntimeStanceHull(runtime, stance);
	sg_host_collision_trace_t source;
	sg_host_collision_trace_t destination;

	if (!hull || !transition_out || !FiniteVector(start) ||
		!FiniteVector(end))
		return 0;
	memset(transition_out, 0, sizeof(*transition_out));
	if (!SG_HostEngineRuntimeOwnerTrace(runtime, identity, start,
		hull->mins.value, hull->maxs.value, start,
		SG_HOST_MASK_PLAYER_SOLID, &source) ||
		!SG_HostEngineRuntimeOwnerTrace(runtime, identity, end,
		hull->mins.value, hull->maxs.value, end,
		SG_HOST_MASK_PLAYER_SOLID, &destination) ||
		!SG_HostEngineRuntimeOwnerTrace(runtime, identity, start,
		hull->mins.value, hull->maxs.value, end,
		SG_HOST_MASK_PLAYER_SOLID, &transition_out->sweep))
		return 0;
	transition_out->source_valid = !source.allsolid;
	transition_out->destination_valid = !destination.allsolid;
	transition_out->clear = transition_out->source_valid &&
		transition_out->destination_valid &&
		!transition_out->sweep.startsolid &&
		!transition_out->sweep.allsolid &&
		transition_out->sweep.fraction == 1.0f;
	return SG_HostEngineRuntimeOwnerSubjectCurrent(runtime, identity);
}

static trace_t RuntimePmoveTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	sg_host_collision_trace_t host_trace;
	sg_host_engine_runtime_scope_t *scope = sg_host_engine_runtime_scope;
	sg_host_pmove_trace_t *record = NULL;

	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (!scope || !RuntimeSubjectCurrent(scope->runtime, &scope->subject) ||
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
	if (scope->traces)
	{
		if (scope->trace_count >= scope->trace_capacity)
		{
			scope->trace_capacity_failed = 1;
			trace.allsolid = true;
			trace.startsolid = true;
			trace.fraction = 0.0f;
			VectorCopy(start, trace.endpos);
			trace.contents = CONTENTS_SOLID;
			return trace;
		}
		record = &scope->traces[scope->trace_count];
		memset(record, 0, sizeof(*record));
		record->ordinal = scope->trace_count + 1U;
		record->substep = scope->substep;
		if (scope->pmove)
			record->state = scope->pmove->s;
		memcpy(record->start, start, sizeof(record->start));
		if (mins)
			memcpy(record->mins, mins, sizeof(record->mins));
		if (maxs)
			memcpy(record->maxs, maxs, sizeof(record->maxs));
		memcpy(record->end, end, sizeof(record->end));
	}
	scope->trace_count++;
	/* Keep the engine trace object intact inside the private Pmove scope.  In
	 * particular, `ent` is the engine's authenticated ground/mover identity;
	 * reconstructing a normalized trace here would turn every support into
	 * WORLD and make grounded movement observably wrong. */
	trace = scope->runtime->trace(start, mins, maxs, end,
		scope->subject.entity, scope->trace_mask);
	if (!TraceConvert(&trace, &host_trace))
	{
		scope->collision_failed = 1;
		scope->collision_trace_count++;
		memset(&trace, 0, sizeof(trace));
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.allsolid = true;
		trace.startsolid = true;
		trace.contents = CONTENTS_SOLID;
	}
	else
	{
		if (record)
			record->result = host_trace;
		if (trace.fraction < 1.0f || trace.startsolid || trace.allsolid)
			scope->collision_trace_count++;
	}
	return trace;
}

static int RuntimePmoveContents(vec3_t point)
{
	sg_host_collision_contents_t contents;

	if (!sg_host_engine_runtime_scope ||
		!RuntimePointContents(sg_host_engine_runtime_scope->runtime,
			&sg_host_engine_runtime_scope->subject, point, &contents))
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

static int HullMatchesIdentity(const sg_host_static_identity_t *identity,
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
	uint32_t subject_index, const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	const sg_host_static_identity_t *identity;
	sg_host_engine_subject_t subject;
	sg_host_engine_runtime_scope_t scope;
	pmove_t pm;
	short effective_gravity;
	uint32_t step;

	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	if (!result_out || !request || request->state.pm_type < PM_NORMAL ||
		request->state.pm_type > PM_FREEZE)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (!RuntimeBotSubject(runtime, subject_index, &subject))
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
	identity = &runtime->static_identity;
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
	memset(result_out, 0, sizeof(*result_out));
	memset(&scope, 0, sizeof(scope));
	scope.runtime = runtime;
	scope.subject = subject;
	scope.trace_mask = subject.entity->health > 0 ?
		(int)SG_HOST_MASK_PLAYER_SOLID : MASK_DEADSOLID;
	sg_host_engine_runtime_scope = &scope;
	memset(&pm, 0, sizeof(pm));
	pm.s = request->state;
	pm.s.gravity = effective_gravity;
	pm.cmd = request->command;
	pm.snapinitial = memcmp(&request->previous_state, &pm.s,
		sizeof(pm.s)) != 0;
	pm.trace = RuntimePmoveTrace;
	pm.pointcontents = RuntimePmoveContents;
	if (RuntimeCallbacksCurrent(runtime))
		runtime->pmove(&pm);
	sg_host_engine_runtime_scope = NULL;
	if (!RuntimeCallbacksCurrent(runtime) || scope.collision_failed ||
		!RuntimeSubjectCurrent(runtime, &subject) ||
		(request->state.pm_type == PM_NORMAL &&
			!HullMatchesIdentity(identity, &pm, effective_gravity)))
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
	if (pm.numtouch < 0 || pm.numtouch > MAXTOUCH)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
		return 0;
	}
	result_out->touch_count = (uint32_t)pm.numtouch;
	for (step = 0U; step < result_out->touch_count; step++)
	{
		uint32_t touch_index;

		if (!EntityCurrent(pm.touchents[step], &touch_index))
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		result_out->touch_instance_ids[step] = touch_index;
	}
	for (step = 0U; step < 3U; step++)
	{
		result_out->origin[step] = pm.s.origin[step] * 0.125f;
		result_out->velocity[step] = pm.s.velocity[step] * 0.125f;
	}
	result_out->water_type = pm.watertype;
	result_out->water_level = pm.waterlevel;
	result_out->evaluated_steps = 1U;
	result_out->elapsed_ms = request->command.msec;
	result_out->trace_count = scope.trace_count;
	result_out->collision_trace_count = scope.collision_trace_count;
	result_out->gravity = (float)effective_gravity;
	result_out->physics_abi_id = identity->physics_abi_id;
	result_out->gravity_law_id = HookGravityActive(request) ?
		SG_HOST_PMOVE_HOOK_LAW_ID : UINT64_C(0);
	return 1;
}

int SG_HostEngineRuntimeOwnerReplayFrame(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *identity,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	const sg_host_static_identity_t *static_identity;
	sg_host_engine_subject_t subject;
	sg_host_engine_runtime_scope_t scope;
	pmove_state_t previous;
	pmove_state_t state;
	pmove_t pm;
	short effective_gravity;
	uint32_t steps;
	uint32_t step;
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;

	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	if (replay_out)
		memset(replay_out, 0, sizeof(*replay_out));
	if (!runtime || !request || !workspace || !replay_out ||
		request->state.pm_type != PM_NORMAL)
		error = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
	else if (!RuntimeExactBotSubject(runtime, identity, &subject))
		error = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
	else if (sg_host_engine_runtime_scope)
		error = SG_HOST_PMOVE_ERROR_REENTRANT;
	else if (runtime->static_identity.physics.substep_ms == 0U ||
		runtime->static_identity.physics.substep_ms > UCHAR_MAX ||
		runtime->static_identity.physics.frame_ms == 0U ||
		runtime->static_identity.physics.frame_ms > UCHAR_MAX ||
		runtime->static_identity.physics.frame_ms %
			runtime->static_identity.physics.substep_ms != 0U ||
		request->command.msec !=
			(byte)runtime->static_identity.physics.frame_ms)
		error = SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING;
	else
	{
		steps = runtime->static_identity.physics.frame_ms /
			runtime->static_identity.physics.substep_ms;
		if (!workspace->substeps || workspace->substep_capacity < steps ||
			!workspace->traces || workspace->trace_capacity == 0U)
			error = SG_HOST_PMOVE_ERROR_CAPACITY;
	}
	if (error != SG_HOST_PMOVE_ERROR_NONE)
	{
		if (error_out)
			*error_out = error;
		return 0;
	}
	static_identity = &runtime->static_identity;
	if (!isfinite(static_identity->physics.gravity) ||
		static_identity->physics.gravity < 0.0f ||
		static_identity->physics.gravity > (float)SHRT_MAX ||
		truncf(static_identity->physics.gravity) !=
			static_identity->physics.gravity)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY;
		return 0;
	}
	if (!RequestGravity(request, (short)static_identity->physics.gravity,
		&effective_gravity))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
		return 0;
	}
	memset(&scope, 0, sizeof(scope));
	scope.runtime = runtime;
	scope.subject = subject;
	scope.trace_mask = subject.entity->health > 0 ?
		(int)SG_HOST_MASK_PLAYER_SOLID : MASK_DEADSOLID;
	scope.traces = workspace->traces;
	scope.trace_capacity = workspace->trace_capacity;
	state = request->state;
	previous = request->previous_state;
	state.gravity = effective_gravity;
	memset(&pm, 0, sizeof(pm));
	pm.s = state;
	sg_host_engine_runtime_scope = &scope;
	for (step = 0U; step < steps; step++)
	{
		pmove_state_t before_state = state;
		uint64_t first_trace_ordinal = scope.trace_count + 1U;
		uint64_t collision_trace_count = scope.collision_trace_count;
		sg_host_pmove_substep_t *substep = &workspace->substeps[step];

		memset(&pm, 0, sizeof(pm));
		pm.s = state;
		pm.cmd = request->command;
		pm.cmd.msec = (byte)static_identity->physics.substep_ms;
		pm.snapinitial = memcmp(&previous, &pm.s, sizeof(pm.s)) != 0;
		pm.trace = RuntimePmoveTrace;
		pm.pointcontents = RuntimePmoveContents;
		scope.substep = step;
		scope.pmove = &pm;
		if (RuntimeCallbacksCurrent(runtime))
			runtime->pmove(&pm);
		scope.pmove = NULL;
		if (!RuntimeCallbacksCurrent(runtime) ||
			!RuntimeSubjectCurrent(runtime, &subject))
		{
			error = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
			break;
		}
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
		if (!HullMatchesIdentity(static_identity, &pm, effective_gravity))
		{
			error = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
			break;
		}
		memset(substep, 0, sizeof(*substep));
		substep->before_state = before_state;
		substep->state = pm.s;
		for (uint32_t axis = 0U; axis < 3U; axis++)
		{
			substep->before_origin[axis] =
				before_state.origin[axis] * 0.125f;
			substep->before_velocity[axis] =
				before_state.velocity[axis] * 0.125f;
			substep->origin[axis] = pm.s.origin[axis] * 0.125f;
			substep->velocity[axis] = pm.s.velocity[axis] * 0.125f;
		}
		substep->stance = (pm.s.pm_flags & PMF_DUCKED) != 0 ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
		substep->grounded = pm.groundentity != NULL;
		if (pm.groundentity)
		{
			uint32_t ground_index;

			if (!EntityCurrent(pm.groundentity, &ground_index) ||
				(ground_index != 0U && pm.groundentity->s.modelindex <= 0))
			{
				error = SG_HOST_PMOVE_ERROR_COLLISION;
				break;
			}
			substep->support_model_index = ground_index == 0U ?
				SG_HOST_COLLISION_MODEL_WORLD :
				(uint32_t)pm.groundentity->s.modelindex;
			substep->support_instance_id = (uint64_t)ground_index;
		}
		substep->water_type = pm.watertype;
		substep->water_level = pm.waterlevel;
		substep->step = step;
		substep->elapsed_ms =
			(step + 1U) * static_identity->physics.substep_ms;
		substep->first_trace_ordinal = first_trace_ordinal;
		substep->trace_count = scope.trace_count -
			(first_trace_ordinal - 1U);
		substep->collision_trace_count = scope.collision_trace_count -
			collision_trace_count;
		state = pm.s;
		previous = pm.s;
	}
	sg_host_engine_runtime_scope = NULL;
	if (error != SG_HOST_PMOVE_ERROR_NONE)
	{
		if (error_out)
			*error_out = error;
		return 0;
	}
	memset(&replay_out->result, 0, sizeof(replay_out->result));
	replay_out->result.state = pm.s;
	memcpy(replay_out->result.mins, pm.mins,
		sizeof(replay_out->result.mins));
	memcpy(replay_out->result.maxs, pm.maxs,
		sizeof(replay_out->result.maxs));
	memcpy(replay_out->result.view_angles, pm.viewangles,
		sizeof(replay_out->result.view_angles));
	replay_out->result.view_height = pm.viewheight;
	replay_out->result.grounded = pm.groundentity != NULL;
	if (pm.groundentity)
	{
		uint32_t ground_index;

		if (!EntityCurrent(pm.groundentity, &ground_index) ||
			(ground_index != 0U && pm.groundentity->s.modelindex <= 0))
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		replay_out->result.support_model_index = ground_index == 0U ?
			SG_HOST_COLLISION_MODEL_WORLD :
			(uint32_t)pm.groundentity->s.modelindex;
		replay_out->result.support_instance_id = (uint64_t)ground_index;
	}
	if (pm.numtouch < 0 || pm.numtouch > MAXTOUCH)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
		return 0;
	}
	replay_out->result.touch_count = (uint32_t)pm.numtouch;
	for (step = 0U; step < replay_out->result.touch_count; step++)
	{
		uint32_t touch_index;

		if (!EntityCurrent(pm.touchents[step], &touch_index))
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		replay_out->result.touch_instance_ids[step] = touch_index;
	}
	for (step = 0U; step < 3U; step++)
	{
		replay_out->result.origin[step] = pm.s.origin[step] * 0.125f;
		replay_out->result.velocity[step] = pm.s.velocity[step] * 0.125f;
	}
	replay_out->result.water_type = pm.watertype;
	replay_out->result.water_level = pm.waterlevel;
	replay_out->result.evaluated_steps = steps;
	replay_out->result.elapsed_ms = static_identity->physics.frame_ms;
	replay_out->result.trace_count = scope.trace_count;
	replay_out->result.collision_trace_count = scope.collision_trace_count;
	replay_out->result.gravity = (float)effective_gravity;
	replay_out->result.physics_abi_id = static_identity->physics_abi_id;
	replay_out->result.gravity_law_id = HookGravityActive(request) ?
		SG_HOST_PMOVE_HOOK_LAW_ID : UINT64_C(0);
	replay_out->request = *request;
	replay_out->substeps = workspace->substeps;
	replay_out->substep_count = steps;
	replay_out->traces = workspace->traces;
	replay_out->trace_count = (size_t)scope.trace_count;
	replay_out->bsp_identity = static_identity->bsp_identity;
	replay_out->bsp_content_id = 0U;
	replay_out->physics_abi_id = static_identity->physics_abi_id;
	replay_out->frame_ms = static_identity->physics.frame_ms;
	replay_out->substep_ms = static_identity->physics.substep_ms;
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return RuntimeSubjectCurrent(runtime, &subject);
}

const sg_host_static_identity_t *SG_HostEngineRuntimeStaticIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? &runtime->static_identity :
		NULL;
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
	memset(&runtime->static_identity, 0, sizeof(runtime->static_identity));
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
