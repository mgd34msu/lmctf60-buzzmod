#include "sg_host_law_publication.h"
#include "sg_host_law_publication_private.h"
#include "sg_host_engine_runtime_private.h"
#include "sg_weapon_host_constants.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern game_import_t gi;
extern cvar_t *sv_gravity;
extern cvar_t *sv_maxvelocity;
extern cvar_t *want_funky_gravity;
extern cvar_t *ctfflags;
extern int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);

#define SG_HOST_LAW_STATE UINT32_C(0x484c5033)
#define SG_HOST_COLLISION_ID UINT64_C(0x434f4c4c49534933)
#define SG_HOST_PMOVE_ID UINT64_C(0x504d4f56454c5733)
#define SG_HOST_GRAVITY_ID UINT64_C(0x4752415649545933)

typedef enum sg_host_law_backend_e
{
	SG_HOST_LAW_BACKEND_CONTROLLER = 1,
	SG_HOST_LAW_BACKEND_ENGINE_STATIC,
	SG_HOST_LAW_BACKEND_ENGINE_RUNTIME
} sg_host_law_backend_t;

typedef struct sg_host_law_currentness_s
{
	uint64_t state;
	uint64_t state_inverse;
	uint64_t generation;
	uint32_t references;
	uint32_t active;
} sg_host_law_currentness_t;

#define SG_HOST_LAW_CURRENTNESS_STATE UINT64_C(0x43555252454e5431)
#define SG_HOST_LAW_CONSTRUCTION_STATE UINT64_C(0x434f4e53544c4157)

static uint64_t sg_host_law_next_generation = UINT64_C(1);

struct sg_host_law_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	const sg_host_law_publication_t *self;
	sg_host_collision_authority_t authority;
	sg_host_law_backend_t backend;
	const sg_host_engine_runtime_t *runtime;
	sg_host_static_identity_t static_identity;
	sg_host_engine_pmove_binding_t pmove_binding;
	sg_host_hook_live_capture_function_t hook_live_capture;
	sg_host_mechanism_live_capture_function_t mechanism_live_capture;
	sg_host_law_currentness_t *construction_currentness;
	sg_host_law_view_t view;
};

struct sg_host_law_construction_s
{
	uint64_t state;
	uint64_t state_inverse;
	const struct sg_host_law_construction_s *self;
	sg_bsp_world_t *world;
	sg_host_collision_authority_t authority;
	sg_host_static_identity_t static_identity;
	sg_host_engine_pmove_binding_t pmove_binding;
	sg_host_hook_live_capture_function_t hook_live_capture;
	sg_host_mechanism_live_capture_function_t mechanism_live_capture;
	sg_host_law_currentness_t *currentness;
	sg_host_law_view_t laws;
};

#define SG_HOST_LAW_FNV_OFFSET UINT64_C(1469598103934665603)
#define SG_HOST_LAW_FNV_PRIME UINT64_C(1099511628211)

static int CurrentnessValid(const sg_host_law_currentness_t *currentness)
{
	return currentness &&
		currentness->state == SG_HOST_LAW_CURRENTNESS_STATE &&
		currentness->state_inverse == ~SG_HOST_LAW_CURRENTNESS_STATE &&
		currentness->generation != 0U && currentness->references != 0U;
}

static sg_host_law_currentness_t *CurrentnessCreate(void)
{
	sg_host_law_currentness_t *currentness;

	if (sg_host_law_next_generation == 0U)
		return NULL;
	currentness = calloc(1U, sizeof(*currentness));
	if (!currentness)
		return NULL;
	currentness->state = SG_HOST_LAW_CURRENTNESS_STATE;
	currentness->state_inverse = ~SG_HOST_LAW_CURRENTNESS_STATE;
	currentness->generation = sg_host_law_next_generation++;
	currentness->references = 1U;
	currentness->active = 1U;
	return currentness;
}

static int CurrentnessRetain(sg_host_law_currentness_t *currentness)
{
	if (!CurrentnessValid(currentness) ||
		currentness->references == UINT32_MAX)
		return 0;
	currentness->references++;
	return 1;
}

static void CurrentnessRelease(sg_host_law_currentness_t *currentness)
{
	if (!CurrentnessValid(currentness))
		return;
	currentness->references--;
	if (currentness->references != 0U)
		return;
	currentness->state = 0U;
	currentness->state_inverse = 0U;
	currentness->generation = 0U;
	currentness->active = 0U;
	free(currentness);
}

static void CurrentnessRevoke(sg_host_law_currentness_t *currentness)
{
	if (CurrentnessValid(currentness))
		currentness->active = 0U;
}

static sg_host_law_result_t Result(sg_host_law_status_t status,
	sg_host_law_field_t field, uint32_t element, uint64_t expected,
	uint64_t observed)
{
	sg_host_law_result_t result = { status, field, element, 0U, expected,
		observed };

	return result;
}

static sg_host_law_result_t Ok(void)
{
	return Result(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int SameFloat(float left, float right)
{
	return FloatBits(left) == FloatBits(right);
}

static int FiniteVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int ContentIdentityValid(const sg_bsp_content_identity_t *identity)
{
	uint32_t index;
	int any = 0;

	if (!identity)
		return 0;
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		if (identity->bytes[index] != 0U)
			any = 1;
	return any;
}

static int FiniteHull(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(hull->mins.value[axis]) ||
			!isfinite(hull->maxs.value[axis]) ||
			hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	const sg_rune_physics_parameters_t *physics;

	if (!identity || !identity->bsp_content_id ||
		identity->bsp_content_id == UINT64_MAX || !identity->entity_semantics_id ||
		identity->entity_semantics_id == UINT64_MAX || !identity->physics_abi_id ||
		identity->physics_abi_id == UINT64_MAX ||
		identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX ||
		identity->schema_id == 0U || identity->schema_id == UINT64_MAX ||
		identity->producer_identity == 0U ||
		identity->producer_identity == UINT64_MAX ||
		!FiniteHull(&identity->standing_hull) ||
		!FiniteHull(&identity->crouching_hull))
		return 0;
	physics = &identity->physics;
	return isfinite(physics->gravity) && isfinite(physics->ground_acceleration) &&
		isfinite(physics->air_acceleration) &&
		isfinite(physics->water_acceleration) &&
		isfinite(physics->hook_acceleration) &&
		isfinite(physics->external_acceleration) && isfinite(physics->water_drag) &&
		isfinite(physics->max_velocity) && physics->gravity >= 0.0f &&
		physics->ground_acceleration >= 0.0f && physics->air_acceleration >= 0.0f &&
		physics->water_acceleration >= 0.0f && physics->hook_acceleration >= 0.0f &&
		physics->external_acceleration >= 0.0f && physics->water_drag >= 0.0f &&
		physics->max_velocity > 0.0f && physics->frame_ms &&
		physics->substep_ms;
}

static int StaticIdentityValid(const sg_host_static_identity_t *identity)
{
	const sg_rune_physics_parameters_t *physics;

	if (!identity || !ContentIdentityValid(&identity->bsp_identity) ||
		identity->bsp_bytes == 0U || identity->host_physics_epoch == 0U ||
		identity->reserved != 0U || identity->physics_abi_id == 0U ||
		identity->physics_abi_id == UINT64_MAX ||
		!FiniteHull(&identity->standing_hull) ||
		!FiniteHull(&identity->crouching_hull))
		return 0;
	physics = &identity->physics;
	return isfinite(physics->gravity) && isfinite(physics->ground_acceleration) &&
		isfinite(physics->air_acceleration) &&
		isfinite(physics->water_acceleration) &&
		isfinite(physics->hook_acceleration) &&
		isfinite(physics->external_acceleration) && isfinite(physics->water_drag) &&
		isfinite(physics->max_velocity) && physics->gravity >= 0.0f &&
		physics->ground_acceleration >= 0.0f && physics->air_acceleration >= 0.0f &&
		physics->water_acceleration >= 0.0f && physics->hook_acceleration >= 0.0f &&
		physics->external_acceleration >= 0.0f && physics->water_drag >= 0.0f &&
		physics->max_velocity > 0.0f && physics->frame_ms && physics->substep_ms;
}

static sg_host_law_result_t CompareU32(uint32_t expected, uint32_t observed,
	sg_host_law_status_t status, sg_host_law_field_t field)
{
	return expected == observed ? Ok() : Result(status, field,
		SG_HOST_LAW_ELEMENT_NONE, expected, observed);
}

static sg_host_law_result_t CompareU64(uint64_t expected, uint64_t observed,
	sg_host_law_status_t status, sg_host_law_field_t field)
{
	return expected == observed ? Ok() : Result(status, field,
		SG_HOST_LAW_ELEMENT_NONE, expected, observed);
}

static sg_host_law_result_t CompareFloat(float expected, float observed,
	sg_host_law_status_t status, sg_host_law_field_t field)
{
	return SameFloat(expected, observed) ? Ok() : Result(status, field,
		SG_HOST_LAW_ELEMENT_NONE, FloatBits(expected), FloatBits(observed));
}

static sg_host_law_result_t CompareHull(
	const sg_rune_hull_profile_t *expected,
	const sg_rune_hull_profile_t *observed, sg_host_law_status_t status,
	sg_host_law_field_t mins_field, sg_host_law_field_t maxs_field)
{
	sg_host_law_result_t result;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		result = CompareFloat(expected->mins.value[axis],
			observed->mins.value[axis], status, mins_field);
		if (result.status != SG_HOST_LAW_OK)
		{
			result.element = axis;
			return result;
		}
		result = CompareFloat(expected->maxs.value[axis],
			observed->maxs.value[axis], status, maxs_field);
		if (result.status != SG_HOST_LAW_OK)
		{
			result.element = axis;
			return result;
		}
	}
	return Ok();
}

static sg_host_law_result_t CompareIdentity(
	const sg_rune_model_identity_t *expected,
	const sg_rune_model_identity_t *observed, sg_host_law_status_t status)
{
	static const sg_host_law_field_t fields[] = {
		SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_FIELD_ENTITY_SEMANTICS,
		SG_HOST_LAW_FIELD_PHYSICS_ABI, SG_HOST_LAW_FIELD_SOURCE_SET,
		SG_HOST_LAW_FIELD_SCHEMA, SG_HOST_LAW_FIELD_PRODUCER
	};
	sg_host_law_result_t result;
	uint32_t index;

	{
		const uint64_t expected_ids[] = {
			expected->bsp_content_id, expected->entity_semantics_id,
			expected->physics_abi_id, expected->source_set_identity,
			expected->schema_id, expected->producer_identity
		};
		const uint64_t observed_ids[] = {
			observed->bsp_content_id, observed->entity_semantics_id,
			observed->physics_abi_id, observed->source_set_identity,
			observed->schema_id, observed->producer_identity
		};

		for (index = 0U; index < 6U; index++)
		{
			result = CompareU64(expected_ids[index], observed_ids[index], status,
				fields[index]);
			if (result.status != SG_HOST_LAW_OK)
				return result;
		}
	}
	result = CompareHull(&expected->standing_hull, &observed->standing_hull,
		status, SG_HOST_LAW_FIELD_STANDING_HULL_MINS,
		SG_HOST_LAW_FIELD_STANDING_HULL_MAXS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareHull(&expected->crouching_hull, &observed->crouching_hull,
		status, SG_HOST_LAW_FIELD_CROUCHING_HULL_MINS,
		SG_HOST_LAW_FIELD_CROUCHING_HULL_MAXS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	{
		const float *left_float = &expected->physics.gravity;
		const float *right_float = &observed->physics.gravity;
		static const sg_host_law_field_t physics_fields[] = {
			SG_HOST_LAW_FIELD_GRAVITY,
			SG_HOST_LAW_FIELD_GROUND_ACCELERATION,
			SG_HOST_LAW_FIELD_AIR_ACCELERATION,
			SG_HOST_LAW_FIELD_WATER_ACCELERATION,
			SG_HOST_LAW_FIELD_HOOK_ACCELERATION,
			SG_HOST_LAW_FIELD_EXTERNAL_ACCELERATION,
			SG_HOST_LAW_FIELD_WATER_DRAG,
			SG_HOST_LAW_FIELD_MODEL_MAX_VELOCITY
		};

		for (index = 0U; index < 8U; index++)
		{
			result = CompareFloat(left_float[index], right_float[index], status,
				physics_fields[index]);
			if (result.status != SG_HOST_LAW_OK)
				return result;
		}
	}
	result = CompareU32(expected->physics.frame_ms, observed->physics.frame_ms,
		status, SG_HOST_LAW_FIELD_FRAME_MS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return CompareU32(expected->physics.substep_ms, observed->physics.substep_ms,
		status, SG_HOST_LAW_FIELD_SUBSTEP_MS);
}

static sg_host_law_result_t CompareStaticIdentity(
	const sg_host_static_identity_t *expected,
	const sg_host_static_identity_t *observed, sg_host_law_status_t status)
{
	sg_host_law_result_t result;
	uint32_t index;
	static const sg_host_law_field_t physics_fields[] = {
		SG_HOST_LAW_FIELD_GRAVITY,
		SG_HOST_LAW_FIELD_GROUND_ACCELERATION,
		SG_HOST_LAW_FIELD_AIR_ACCELERATION,
		SG_HOST_LAW_FIELD_WATER_ACCELERATION,
		SG_HOST_LAW_FIELD_HOOK_ACCELERATION,
		SG_HOST_LAW_FIELD_EXTERNAL_ACCELERATION,
		SG_HOST_LAW_FIELD_WATER_DRAG,
		SG_HOST_LAW_FIELD_MODEL_MAX_VELOCITY
	};
	const float *expected_physics = &expected->physics.gravity;
	const float *observed_physics = &observed->physics.gravity;

	if (memcmp(expected->bsp_identity.bytes, observed->bsp_identity.bytes,
		SG_BSP_CONTENT_ID_BYTES) != 0)
		return Result(status, SG_HOST_LAW_FIELD_BSP_CONTENT,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = CompareU64(expected->bsp_bytes, observed->bsp_bytes, status,
		SG_HOST_LAW_FIELD_BSP_CONTENT);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareU32(expected->engine_checksum, observed->engine_checksum,
		status, SG_HOST_LAW_FIELD_BSP_CONTENT);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareU32(expected->entity_crc32, observed->entity_crc32,
		status, SG_HOST_LAW_FIELD_ENTITY_SEMANTICS);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareU32(expected->host_physics_epoch,
		observed->host_physics_epoch, status, SG_HOST_LAW_FIELD_PHYSICS_ABI);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareU32(expected->reserved, observed->reserved, status,
		SG_HOST_LAW_FIELD_VERSION);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareU64(expected->physics_abi_id, observed->physics_abi_id,
		status, SG_HOST_LAW_FIELD_PHYSICS_ABI);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareHull(&expected->standing_hull, &observed->standing_hull,
		status, SG_HOST_LAW_FIELD_STANDING_HULL_MINS,
		SG_HOST_LAW_FIELD_STANDING_HULL_MAXS);
	if (result.status != SG_HOST_LAW_OK) return result;
	result = CompareHull(&expected->crouching_hull, &observed->crouching_hull,
		status, SG_HOST_LAW_FIELD_CROUCHING_HULL_MINS,
		SG_HOST_LAW_FIELD_CROUCHING_HULL_MAXS);
	if (result.status != SG_HOST_LAW_OK) return result;
	for (index = 0U; index < 8U; index++)
	{
		result = CompareFloat(expected_physics[index], observed_physics[index],
			status, physics_fields[index]);
		if (result.status != SG_HOST_LAW_OK) return result;
	}
	result = CompareU32(expected->physics.frame_ms,
		observed->physics.frame_ms, status, SG_HOST_LAW_FIELD_FRAME_MS);
	if (result.status != SG_HOST_LAW_OK) return result;
	return CompareU32(expected->physics.substep_ms,
		observed->physics.substep_ms, status, SG_HOST_LAW_FIELD_SUBSTEP_MS);
}

static sg_host_law_result_t ComparePmoveABI(
	const sg_host_engine_pmove_abi_t *expected,
	const sg_host_engine_pmove_abi_t *observed, sg_host_law_status_t status)
{
	sg_host_law_result_t result;

#define SG_COMPARE_ABI_U32(member) \
	result = CompareU32(expected->member, observed->member, status, \
		SG_HOST_LAW_FIELD_PMOVE_ABI); \
	if (result.status != SG_HOST_LAW_OK) return result;
	SG_COMPARE_ABI_U32(version);
	SG_COMPARE_ABI_U32(game_api_version);
	SG_COMPARE_ABI_U32(import_size);
	SG_COMPARE_ABI_U32(pmove_offset);
	SG_COMPARE_ABI_U32(pmove_size);
	SG_COMPARE_ABI_U32(state_size);
	SG_COMPARE_ABI_U32(command_size);
	SG_COMPARE_ABI_U32(fraction_bits);
	SG_COMPARE_ABI_U32(substep_ms);
#undef SG_COMPARE_ABI_U32
	return CompareU64(expected->identity, observed->identity, status,
		SG_HOST_LAW_FIELD_PMOVE_ABI);
}

static sg_host_law_result_t CompareHook(
	const sg_host_hook_law_t *expected, const sg_host_hook_law_t *observed,
	sg_host_law_status_t status)
{
	sg_host_law_result_t result;

#define SG_COMPARE_HOOK_U32(member, field) \
	result = CompareU32(expected->member, observed->member, status, field); \
	if (result.status != SG_HOST_LAW_OK) return result;
	SG_COMPARE_HOOK_U32(version, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_COMPARE_HOOK_U32(trace_mask, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_COMPARE_HOOK_U32(muzzle_forward_offset, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_COMPARE_HOOK_U32(muzzle_right_offset, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_COMPARE_HOOK_U32(muzzle_view_offset, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_COMPARE_HOOK_U32(fire_speed, SG_HOST_LAW_FIELD_HOOK_FIRE_SPEED);
	SG_COMPARE_HOOK_U32(pull_speed, SG_HOST_LAW_FIELD_HOOK_PULL_SPEED);
	SG_COMPARE_HOOK_U32(initial_damage, SG_HOST_LAW_FIELD_HOOK_INITIAL_DAMAGE);
	SG_COMPARE_HOOK_U32(attached_damage, SG_HOST_LAW_FIELD_HOOK_ATTACHED_DAMAGE);
	SG_COMPARE_HOOK_U32(projectile_health, SG_HOST_LAW_FIELD_HOOK_HEALTH);
	SG_COMPARE_HOOK_U32(attached_cadence_frames, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_COMPARE_HOOK_U32(no_grapple_damage, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
#undef SG_COMPARE_HOOK_U32
	result = CompareU64(expected->identity, observed->identity, status,
		SG_HOST_LAW_FIELD_HOOK_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->trace_epsilon, observed->trace_epsilon,
		status, SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->near_bite_distance,
		observed->near_bite_distance, status,
		SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return CompareFloat(expected->near_bite_gravity_zero_distance,
		observed->near_bite_gravity_zero_distance, status,
		SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
}

static sg_host_law_result_t CompareMechanism(
	const sg_host_mechanism_law_t *expected,
	const sg_host_mechanism_law_t *observed, sg_host_law_status_t status)
{
	sg_host_law_result_t result;

#define SG_COMPARE_MECH_U32(member) \
	result = CompareU32(expected->member, observed->member, status, \
		SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS); \
	if (result.status != SG_HOST_LAW_OK) return result;
	SG_COMPARE_MECH_U32(version);
	SG_COMPARE_MECH_U32(frame_ms);
	SG_COMPARE_MECH_U32(move_equation_id);
	SG_COMPARE_MECH_U32(acceleration_equation_id);
	SG_COMPARE_MECH_U32(door_equation_id);
	SG_COMPARE_MECH_U32(platform_equation_id);
	SG_COMPARE_MECH_U32(trigger_equation_id);
	SG_COMPARE_MECH_U32(train_equation_id);
	SG_COMPARE_MECH_U32(door_default_wait_ms);
	SG_COMPARE_MECH_U32(platform_top_dwell_ms);
	SG_COMPARE_MECH_U32(platform_top_touch_delay_ms);
	SG_COMPARE_MECH_U32(door_trigger_debounce_ms);
	SG_COMPARE_MECH_U32(door_message_debounce_ms);
	SG_COMPARE_MECH_U32(train_blocked_debounce_ms);
	SG_COMPARE_MECH_U32(trigger_default_wait_ms);
	SG_COMPARE_MECH_U32(trigger_remove_delay_ms);
	SG_COMPARE_MECH_U32(frame_schedule_ms);
#undef SG_COMPARE_MECH_U32
	result = CompareU64(expected->identity, observed->identity, status,
		SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
#define SG_COMPARE_MECH_FLOAT(member) \
	result = CompareFloat(expected->member, observed->member, status, \
		SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS); \
	if (result.status != SG_HOST_LAW_OK) return result;
	SG_COMPARE_MECH_FLOAT(door_default_speed);
	SG_COMPARE_MECH_FLOAT(door_rotating_default_speed);
	SG_COMPARE_MECH_FLOAT(platform_default_speed);
	SG_COMPARE_MECH_FLOAT(platform_default_accel);
	SG_COMPARE_MECH_FLOAT(platform_default_decel);
	SG_COMPARE_MECH_FLOAT(train_default_speed);
#undef SG_COMPARE_MECH_FLOAT
	return CompareU32(expected->train_default_damage,
		observed->train_default_damage, status,
		SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
}

static sg_host_law_result_t CompareViews(const sg_host_law_view_t *expected,
	const sg_host_law_view_t *observed, sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	result = CompareU32(expected->version, observed->version, status,
		SG_HOST_LAW_FIELD_VERSION);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->reserved, observed->reserved, status,
		SG_HOST_LAW_FIELD_VERSION);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->collision_law_id, observed->collision_law_id,
		status, SG_HOST_LAW_FIELD_COLLISION_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->pmove_law_id, observed->pmove_law_id, status,
		SG_HOST_LAW_FIELD_PMOVE_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->gravity_law_id, observed->gravity_law_id, status,
		SG_HOST_LAW_FIELD_GRAVITY_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->hook_law_id, observed->hook_law_id, status,
		SG_HOST_LAW_FIELD_HOOK_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->mechanism_law_id,
		observed->mechanism_law_id, status, SG_HOST_LAW_FIELD_MECHANISM_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (memcmp(&expected->bsp_identity, &observed->bsp_identity,
		sizeof(expected->bsp_identity)) != 0)
		return Result(status, SG_HOST_LAW_FIELD_BSP_CONTENT,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = CompareU64(expected->bsp_bytes, observed->bsp_bytes, status,
		SG_HOST_LAW_FIELD_BSP_CONTENT);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareStaticIdentity(&expected->static_identity,
		&observed->static_identity, status);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareIdentity(&expected->identity, &observed->identity, status);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = ComparePmoveABI(&expected->pmove_abi, &observed->pmove_abi,
		status);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->pmove_behavior_fingerprint,
		observed->pmove_behavior_fingerprint, status,
		SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->airaccelerate, observed->airaccelerate, status,
		SG_HOST_LAW_FIELD_AIRACCELERATE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->maxvelocity, observed->maxvelocity, status,
		SG_HOST_LAW_FIELD_MAXVELOCITY);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->movement_flags, observed->movement_flags, status,
		SG_HOST_LAW_FIELD_MOVEMENT_FLAGS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->physics_flags, observed->physics_flags, status,
		SG_HOST_LAW_FIELD_PHYSICS_FLAGS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->hook_fire_speed, observed->hook_fire_speed,
		status, SG_HOST_LAW_FIELD_HOOK_FIRE_SPEED);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->hook_pull_speed, observed->hook_pull_speed,
		status, SG_HOST_LAW_FIELD_HOOK_PULL_SPEED);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->hook_initial_damage,
		observed->hook_initial_damage, status,
		SG_HOST_LAW_FIELD_HOOK_INITIAL_DAMAGE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->hook_attached_damage,
		observed->hook_attached_damage, status,
		SG_HOST_LAW_FIELD_HOOK_ATTACHED_DAMAGE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->hook_health, observed->hook_health, status,
		SG_HOST_LAW_FIELD_HOOK_HEALTH);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareHook(&expected->hook, &observed->hook, status);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareMechanism(&expected->mechanism, &observed->mechanism,
		status);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return Ok();
}

static int ABIShapeValid(const sg_host_engine_pmove_abi_t *abi)
{
	return abi && abi->version == SG_HOST_ENGINE_PMOVE_ABI_VERSION &&
		abi->game_api_version == GAME_API_VERSION &&
		abi->import_size == (uint32_t)sizeof(game_import_t) &&
		abi->pmove_offset == (uint32_t)offsetof(game_import_t, Pmove) &&
		abi->pmove_size == (uint32_t)sizeof(pmove_t) &&
		abi->state_size == (uint32_t)sizeof(pmove_state_t) &&
		abi->command_size == (uint32_t)sizeof(usercmd_t) &&
		abi->fraction_bits == SG_HOST_ENGINE_PMOVE_FRACTION_BITS &&
		abi->substep_ms == SG_HOST_ENGINE_PMOVE_SUBSTEP_MS &&
		abi->identity == SG_HOST_ENGINE_PMOVE_ABI_ID;
}

static int ViewShapeValid(const sg_host_law_view_t *view)
{
	const sg_rune_physics_parameters_t *physics;
	int static_identity;
	int model_identity;

	if (!view)
		return 0;
	static_identity = StaticIdentityValid(&view->static_identity);
	model_identity = IdentityValid(&view->identity);
	if (static_identity + model_identity != 1)
		return 0;
	physics = static_identity ? &view->static_identity.physics :
		&view->identity.physics;
	return view && view->version == SG_HOST_LAW_PUBLICATION_VERSION &&
		view->reserved == 0U && view->collision_law_id == SG_HOST_COLLISION_ID &&
		view->pmove_law_id == SG_HOST_PMOVE_ID &&
		view->gravity_law_id == SG_HOST_GRAVITY_ID &&
		view->hook_law_id == SG_HOST_HOOK_LAW_ID &&
		view->mechanism_law_id == SG_HOST_MECHANISM_LAW_ID &&
		ContentIdentityValid(&view->bsp_identity) && view->bsp_bytes != 0U &&
		ABIShapeValid(&view->pmove_abi) &&
		view->pmove_behavior_fingerprint &&
		SameFloat(view->airaccelerate, 0.0f) &&
		SameFloat(view->maxvelocity, physics->max_velocity) &&
		view->movement_flags == 0U && view->physics_flags ==
		SG_HOST_ENGINE_PHYSICS_FLAGS &&
		view->hook_fire_speed == view->hook.fire_speed &&
		view->hook_pull_speed == view->hook.pull_speed &&
		view->hook_initial_damage == view->hook.initial_damage &&
		view->hook_attached_damage == view->hook.attached_damage &&
		view->hook_health == view->hook.projectile_health &&
		SG_HostHookLawValid(&view->hook) &&
		SG_HostMechanismLawValid(&view->mechanism);
}

static int PublicationValid(const sg_host_law_publication_t *publication)
{
	const sg_host_static_identity_t *runtime_identity;

	if (!publication || publication->state != SG_HOST_LAW_STATE ||
		publication->state_inverse != ~SG_HOST_LAW_STATE ||
		publication->self != publication || !publication->pmove_binding.entry ||
		!publication->pmove_binding.owner ||
		!publication->hook_live_capture ||
		!publication->mechanism_live_capture || !ViewShapeValid(&publication->view))
		return 0;
	if (publication->backend == SG_HOST_LAW_BACKEND_CONTROLLER)
	{
		if (!publication->authority.world || publication->runtime)
			return 0;
		if (
		memcmp(&publication->authority.content_identity,
			&publication->authority.world->content_identity,
			sizeof(publication->authority.content_identity)) != 0)
			return 0;
		if (memcmp(&publication->view.bsp_identity,
		&publication->authority.content_identity,
		sizeof(publication->view.bsp_identity)) != 0)
			return 0;
		return CompareIdentity(&publication->authority.identity,
			&publication->view.identity,
			SG_HOST_LAW_PRODUCTION_DRIFT).status == SG_HOST_LAW_OK;
	}
	if (publication->backend != SG_HOST_LAW_BACKEND_ENGINE_STATIC &&
		publication->backend != SG_HOST_LAW_BACKEND_ENGINE_RUNTIME)
		return 0;
	if (
		(memcmp(&publication->static_identity.bsp_identity,
			&publication->view.bsp_identity,
			sizeof(publication->static_identity.bsp_identity)) != 0 ||
		 publication->static_identity.bsp_bytes != publication->view.bsp_bytes))
		return 0;
	if (CompareStaticIdentity(&publication->static_identity,
			&publication->view.static_identity,
			SG_HOST_LAW_PRODUCTION_DRIFT).status != SG_HOST_LAW_OK)
		return 0;
	if (publication->backend == SG_HOST_LAW_BACKEND_ENGINE_STATIC)
		return !publication->runtime &&
			CurrentnessValid(publication->construction_currentness) &&
			publication->construction_currentness->active == 1U;
	if (publication->construction_currentness)
		return 0;
	runtime_identity = SG_HostEngineRuntimeStaticIdentity(publication->runtime);
	return publication->runtime && runtime_identity &&
		CompareStaticIdentity(&publication->static_identity, runtime_identity,
			SG_HOST_LAW_PRODUCTION_DRIFT).status == SG_HOST_LAW_OK;
}

static sg_host_law_result_t CaptureLive(
	const sg_rune_model_identity_t *identity,
	const sg_host_static_identity_t *static_identity,
	sg_host_law_view_t *view_out,
	sg_host_engine_pmove_binding_t *binding_out)
{
	sg_host_engine_pmove_abi_t abi;
	sg_host_engine_pmove_binding_t binding;
	sg_host_hook_law_t hook;
	sg_host_mechanism_law_t mechanism;
	cvar_t *airaccelerate;
	float gravity;
	float maxvelocity;
	float ctf_flags;
	sg_rune_physics_parameters_t host_physics;
	sg_rune_hull_profile_t standing_hull;
	sg_rune_hull_profile_t crouching_hull;
	const sg_rune_hull_profile_t *expected_standing;
	const sg_rune_hull_profile_t *expected_crouching;
	const sg_rune_physics_parameters_t *expected_physics;
	uint32_t index;

	if (!view_out || !binding_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (identity)
	{
		if (static_identity || !IdentityValid(identity))
			return Result(SG_HOST_LAW_INVALID_ARGUMENT,
				SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
		expected_standing = &identity->standing_hull;
		expected_crouching = &identity->crouching_hull;
		expected_physics = &identity->physics;
	}
	else
	{
		if (!static_identity || !StaticIdentityValid(static_identity))
			return Result(SG_HOST_LAW_INVALID_ARGUMENT,
				SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
		expected_standing = &static_identity->standing_hull;
		expected_crouching = &static_identity->crouching_hull;
		expected_physics = &static_identity->physics;
	}
	if (!SG_HostEnginePmoveABI(&abi) ||
		!SG_HostEnginePmoveBindingCapture(&binding))
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_PMOVE_ABI,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (abi.substep_ms != expected_physics->substep_ms ||
		abi.substep_ms != SG_HOST_ENGINE_PMOVE_SUBSTEP_MS ||
		expected_physics->frame_ms != SG_HOST_ENGINE_FRAME_MS)
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_PMOVE_ABI, SG_HOST_LAW_ELEMENT_NONE,
			SG_HOST_ENGINE_PMOVE_SUBSTEP_MS, abi.substep_ms);
	if (!gi.cvar || !sv_gravity || !sv_maxvelocity || !want_funky_gravity ||
		!ctfflags)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_GRAVITY,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	airaccelerate = gi.cvar("sv_airaccelerate", "0", 0);
	gravity = sv_gravity->value;
	maxvelocity = sv_maxvelocity->value;
	ctf_flags = ctfflags->value;
	if (!airaccelerate)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_AIRACCELERATE, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!isfinite(gravity) || gravity < (float)SG_HOST_ENGINE_GRAVITY_MIN ||
		gravity > (float)SG_HOST_ENGINE_GRAVITY_MAX || gravity != truncf(gravity) ||
		!SameFloat(gravity, expected_physics->gravity))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_GRAVITY, SG_HOST_LAW_ELEMENT_NONE,
			FloatBits(expected_physics->gravity), FloatBits(gravity));
	if (!SameFloat(airaccelerate->value, 0.0f))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_AIRACCELERATE, SG_HOST_LAW_ELEMENT_NONE, 0U,
			FloatBits(airaccelerate->value));
	if (!isfinite(maxvelocity) || maxvelocity <= 0.0f ||
		!SameFloat(maxvelocity, expected_physics->max_velocity))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_MAXVELOCITY, SG_HOST_LAW_ELEMENT_NONE,
			FloatBits(expected_physics->max_velocity),
			FloatBits(maxvelocity));
	if (!SameFloat(want_funky_gravity->value, 0.0f))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_PHYSICS_FLAGS, SG_HOST_LAW_ELEMENT_NONE, 0U, 1U);
	if (!SG_HostHookLiveCapture(&hook))
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	if (!isfinite(ctf_flags) || ctf_flags < 0.0f ||
		ctf_flags != truncf(ctf_flags) ||
		(double)ctf_flags > (double)INT_MAX)
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE,
			0U, FloatBits(ctf_flags));
	if (hook.no_grapple_damage != (uint32_t)
		(((uint32_t)ctf_flags & SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE) != 0U))
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE,
			((uint32_t)ctf_flags & SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE) != 0U,
			hook.no_grapple_damage);
	if (!SG_HostMechanismLiveCapture(&mechanism))
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	memset(view_out, 0, sizeof(*view_out));
	view_out->version = SG_HOST_LAW_PUBLICATION_VERSION;
	view_out->collision_law_id = SG_HOST_COLLISION_ID;
	view_out->pmove_law_id = SG_HOST_PMOVE_ID;
	view_out->gravity_law_id = SG_HOST_GRAVITY_ID;
	view_out->hook_law_id = hook.identity;
	view_out->mechanism_law_id = mechanism.identity;
	if (identity)
		view_out->identity = *identity;
	else
		view_out->static_identity = *static_identity;
	view_out->pmove_abi = abi;
	/* Record the engine Pmove ABI identity.  Runtime safety comes from exact
	 * callback equality plus per-call level and subject revalidation; parity
	 * tests provide regression coverage only. */
	view_out->pmove_behavior_fingerprint = abi.identity;
	view_out->airaccelerate = airaccelerate->value;
	view_out->maxvelocity = maxvelocity;
	view_out->physics_flags = SG_HOST_ENGINE_PHYSICS_FLAGS;
	view_out->hook = hook;
	view_out->mechanism = mechanism;
	view_out->hook_fire_speed = hook.fire_speed;
	view_out->hook_pull_speed = hook.pull_speed;
	view_out->hook_initial_damage = hook.initial_damage;
	view_out->hook_attached_damage = hook.attached_damage;
	view_out->hook_health = hook.projectile_health;
	if (!SG_HostHookLawValid(&hook))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	if (!SG_HostMechanismLawValid(&mechanism))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	if (!SG_HostEnginePhysicsLaw(&host_physics) ||
		!SG_HostEngineHullProfiles(&standing_hull, &crouching_hull))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	*binding_out = binding;
	for (index = 0U; index < 3U; index++)
	{
		if (!SameFloat(expected_standing->mins.value[index],
			standing_hull.mins.value[index]) ||
			!SameFloat(expected_standing->maxs.value[index],
			standing_hull.maxs.value[index]) ||
			!SameFloat(expected_crouching->mins.value[index],
			crouching_hull.mins.value[index]) ||
			!SameFloat(expected_crouching->maxs.value[index],
			crouching_hull.maxs.value[index]))
			return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
				SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, index, 1U, 0U);
	}
	if (!SameFloat(expected_physics->ground_acceleration,
			host_physics.ground_acceleration) ||
		!SameFloat(expected_physics->air_acceleration,
			host_physics.air_acceleration) ||
		!SameFloat(expected_physics->water_acceleration,
			host_physics.water_acceleration) ||
		!SameFloat(expected_physics->hook_acceleration,
			host_physics.hook_acceleration) ||
		!SameFloat(expected_physics->external_acceleration,
			host_physics.external_acceleration) ||
		!SameFloat(expected_physics->water_drag,
			host_physics.water_drag))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_GRAVITY_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

static sg_host_law_result_t InvalidPublication(sg_host_law_field_t field)
{
	return Result(SG_HOST_LAW_CORRUPT_PUBLICATION, field,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

#ifdef SG_HOST_LAW_TESTING
static sg_host_law_result_t IssueController(
	const sg_host_collision_authority_t *authority,
	sg_host_law_publication_t **publication_out)
{
	sg_host_law_publication_t *publication;
	sg_host_law_view_t view;
	sg_host_engine_pmove_binding_t binding;
	sg_host_law_result_t result;

	if (!publication_out || *publication_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (!authority || !authority->world ||
		memcmp(&authority->content_identity,
			&authority->world->content_identity,
			sizeof(authority->content_identity)) != 0)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = CaptureLive(&authority->identity, NULL, &view, &binding);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	view.bsp_identity = authority->content_identity;
	view.bsp_bytes = authority->world->source_size ?
		(uint64_t)authority->world->source_size : UINT64_C(1);
	publication = malloc(sizeof(*publication));
	if (!publication)
		return Result(SG_HOST_LAW_ALLOCATION_FAILED, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	publication->state = SG_HOST_LAW_STATE;
	publication->state_inverse = ~SG_HOST_LAW_STATE;
	publication->self = publication;
	publication->authority = *authority;
	publication->backend = SG_HOST_LAW_BACKEND_CONTROLLER;
	publication->runtime = NULL;
	publication->pmove_binding = binding;
	publication->hook_live_capture = SG_HostHookLiveCapture;
	publication->mechanism_live_capture = SG_HostMechanismLiveCapture;
	publication->construction_currentness = NULL;
	publication->view = view;
	*publication_out = publication;
	return Ok();
}

static sg_host_law_result_t IssueStatic(
	const sg_host_static_identity_t *identity,
	sg_host_law_publication_t **publication_out)
{
	sg_host_law_publication_t *publication;
	sg_host_law_view_t view;
	sg_host_engine_pmove_binding_t binding;
	sg_host_law_result_t result;

	if (!publication_out || *publication_out ||
		!StaticIdentityValid(identity))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = CaptureLive(NULL, identity, &view, &binding);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	view.bsp_identity = identity->bsp_identity;
	view.bsp_bytes = identity->bsp_bytes;
	publication = calloc(1U, sizeof(*publication));
	if (!publication)
		return Result(SG_HOST_LAW_ALLOCATION_FAILED, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	publication->state = SG_HOST_LAW_STATE;
	publication->state_inverse = ~SG_HOST_LAW_STATE;
	publication->self = publication;
	publication->backend = SG_HOST_LAW_BACKEND_ENGINE_STATIC;
	publication->static_identity = *identity;
	publication->pmove_binding = binding;
	publication->hook_live_capture = SG_HostHookLiveCapture;
	publication->mechanism_live_capture = SG_HostMechanismLiveCapture;
	publication->construction_currentness = CurrentnessCreate();
	publication->view = view;
	if (!publication->construction_currentness || !PublicationValid(publication))
	{
		CurrentnessRelease(publication->construction_currentness);
		free(publication);
		return InvalidPublication(SG_HOST_LAW_FIELD_BSP_CONTENT);
	}
	*publication_out = publication;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationOwnerIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_publication_t **publication_out)
{
	return IssueController(authority, publication_out);
}

sg_host_law_result_t SG_HostLawPublicationOwnerIssueStatic(
	const sg_host_static_identity_t *identity,
	sg_host_law_publication_t **publication_out)
{
	return IssueStatic(identity, publication_out);
}
#endif

sg_host_law_result_t SG_HostLawPublicationOwnerIssueEnginePair(
	sg_host_engine_runtime_t *runtime,
	sg_host_law_publication_t **construction_out,
	sg_host_law_publication_t **production_out)
{
	const sg_host_static_identity_t *identity;
	sg_host_law_publication_t *construction;
	sg_host_law_publication_t *production;
	sg_host_law_view_t view;
	sg_host_engine_pmove_binding_t binding;
	sg_host_engine_runtime_status_t runtime_status;
	sg_host_law_result_t result;

	if (!runtime || !construction_out || *construction_out ||
		!production_out || *production_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	runtime_status = SG_HostEngineRuntimeOwnerActivate(runtime);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE,
			SG_HOST_ENGINE_RUNTIME_OK, (uint64_t)runtime_status);
	identity = SG_HostEngineRuntimeStaticIdentity(runtime);
	if (!identity)
		return InvalidPublication(SG_HOST_LAW_FIELD_BSP_CONTENT);
	result = CaptureLive(NULL, identity, &view, &binding);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	view.bsp_identity = identity->bsp_identity;
	view.bsp_bytes = identity->bsp_bytes;
	construction = calloc(1U, sizeof(*construction));
	production = calloc(1U, sizeof(*production));
	if (!construction || !production)
	{
		free(production);
		free(construction);
		return Result(SG_HOST_LAW_ALLOCATION_FAILED, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	}
	construction->state = SG_HOST_LAW_STATE;
	construction->state_inverse = ~SG_HOST_LAW_STATE;
	construction->self = construction;
	construction->backend = SG_HOST_LAW_BACKEND_ENGINE_STATIC;
	construction->static_identity = *identity;
	construction->pmove_binding = binding;
	construction->hook_live_capture = SG_HostHookLiveCapture;
	construction->mechanism_live_capture = SG_HostMechanismLiveCapture;
	construction->construction_currentness = CurrentnessCreate();
	construction->view = view;
	production->state = SG_HOST_LAW_STATE;
	production->state_inverse = ~SG_HOST_LAW_STATE;
	production->self = production;
	production->backend = SG_HOST_LAW_BACKEND_ENGINE_RUNTIME;
	production->runtime = runtime;
	production->static_identity = *identity;
	production->pmove_binding = binding;
	production->hook_live_capture = SG_HostHookLiveCapture;
	production->mechanism_live_capture = SG_HostMechanismLiveCapture;
	production->construction_currentness = NULL;
	production->view = view;
	if (!construction->construction_currentness ||
		!PublicationValid(construction) || !PublicationValid(production))
	{
		SG_HostLawPublicationOwnerDestroy(production);
		SG_HostLawPublicationOwnerDestroy(construction);
		return InvalidPublication(SG_HOST_LAW_FIELD_BSP_CONTENT);
	}
	*construction_out = construction;
	*production_out = production;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationRead(
	const sg_host_law_publication_t *publication, sg_host_law_view_t *view_out)
{
	if (!view_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	memset(view_out, 0, sizeof(*view_out));
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_NONE);
	*view_out = publication->view;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationMatch(
	const sg_host_law_publication_t *publication,
	const sg_host_law_view_t *expected)
{
	if (!expected)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_NONE);
	return CompareViews(expected, &publication->view,
		SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawPublicationRevalidateProduction(
	const sg_host_law_publication_t *publication)
{
	sg_host_law_view_t current;
	sg_host_engine_pmove_binding_t binding;
	sg_host_law_result_t result;
	const sg_rune_model_identity_t *identity;
	const sg_host_static_identity_t *static_identity;

	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_NONE);
	if (publication->runtime &&
		!SG_HostEngineRuntimeAccepted(publication->runtime))
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostEnginePmoveBindingCurrent(&publication->pmove_binding))
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	if (publication->hook_live_capture != SG_HostHookLiveCapture)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	if (publication->mechanism_live_capture != SG_HostMechanismLiveCapture)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	identity = publication->backend == SG_HOST_LAW_BACKEND_CONTROLLER ?
		&publication->authority.identity : NULL;
	static_identity = publication->backend == SG_HOST_LAW_BACKEND_CONTROLLER ?
		NULL : &publication->static_identity;
	result = CaptureLive(identity, static_identity, &current, &binding);
	if (result.status != SG_HOST_LAW_OK)
	{
		if (result.status == SG_HOST_LAW_HOST_UNAVAILABLE ||
			result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW)
			result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
		return result;
	}
	if (binding.entry != publication->pmove_binding.entry ||
		binding.owner != publication->pmove_binding.owner)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	current.bsp_identity = publication->backend ==
		SG_HOST_LAW_BACKEND_CONTROLLER ? publication->authority.content_identity :
		publication->static_identity.bsp_identity;
	current.bsp_bytes = publication->view.bsp_bytes;
	return CompareViews(&publication->view, &current,
		SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawPublicationCollisionAuthority(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_authority_t **authority_out)
{
	sg_host_law_result_t result;

	if (!authority_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	*authority_out = NULL;
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_COLLISION_LAW);
	if (!publication->authority.world)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	*authority_out = &publication->authority;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationCollisionTrace(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	if (!start || !mins || !maxs || !end || !trace_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_COLLISION_LAW);
	if (!publication->authority.world)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(trace_out, 0, sizeof(*trace_out));
	if (!SG_HostCollisionTrace(&publication->authority, scene, start, mins, maxs,
		end, mask, trace_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationOwnerEngineTrace(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	sg_host_law_result_t result;

	if (!start || !end || !trace_out ||
		(mins && !FiniteVector(mins)) || (maxs && !FiniteVector(maxs)))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_COLLISION_LAW);
	if (!publication->runtime)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	memset(trace_out, 0, sizeof(*trace_out));
	if (!SG_HostEngineRuntimeTrace(publication->runtime, subject_index, start,
		mins, maxs, end, mask, trace_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationPmove(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene, const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;
	sg_host_law_result_t result;

	if (!request || !result_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_PMOVE_LAW,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_PMOVE_LAW);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (publication->backend == SG_HOST_LAW_BACKEND_ENGINE_STATIC ||
		publication->backend == SG_HOST_LAW_BACKEND_ENGINE_RUNTIME)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	}
	if (!SG_HostPmoveEvaluateFrame(&publication->authority, scene,
		publication->pmove_binding.entry, request, result_out, &error))
	{
		if (error_out)
			*error_out = error;
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U,
			(uint64_t)error);
	}
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return Ok();
}

static int ConstructionArrayEqual(const void *left, const void *right,
	uint32_t count, size_t element_size)
{
	if (count == 0U)
		return left == NULL && right == NULL;
	return left && right && element_size != 0U &&
		(size_t)count <= SIZE_MAX / element_size &&
		memcmp(left, right, (size_t)count * element_size) == 0;
}

static int ConstructionGeometryEqual(const sg_bsp_world_t *parsed,
	const sg_bsp_world_t *caller)
{
	return parsed && caller && parsed != caller &&
		memcmp(parsed->content_identity.bytes, caller->content_identity.bytes,
			sizeof(parsed->content_identity.bytes)) == 0 &&
		parsed->engine_checksum == caller->engine_checksum &&
		parsed->source_size == caller->source_size &&
		parsed->plane_count == caller->plane_count &&
		parsed->node_count == caller->node_count &&
		parsed->texinfo_count == caller->texinfo_count &&
		parsed->leaf_count == caller->leaf_count &&
		parsed->leaf_brush_count == caller->leaf_brush_count &&
		parsed->model_count == caller->model_count &&
		parsed->brush_count == caller->brush_count &&
		parsed->brush_side_count == caller->brush_side_count &&
		ConstructionArrayEqual(parsed->planes, caller->planes,
			parsed->plane_count, sizeof(*parsed->planes)) &&
		ConstructionArrayEqual(parsed->nodes, caller->nodes,
			parsed->node_count, sizeof(*parsed->nodes)) &&
		ConstructionArrayEqual(parsed->texinfos, caller->texinfos,
			parsed->texinfo_count, sizeof(*parsed->texinfos)) &&
		ConstructionArrayEqual(parsed->leaves, caller->leaves,
			parsed->leaf_count, sizeof(*parsed->leaves)) &&
		ConstructionArrayEqual(parsed->leaf_brushes, caller->leaf_brushes,
			parsed->leaf_brush_count, sizeof(*parsed->leaf_brushes)) &&
		ConstructionArrayEqual(parsed->models, caller->models,
			parsed->model_count, sizeof(*parsed->models)) &&
		ConstructionArrayEqual(parsed->brushes, caller->brushes,
			parsed->brush_count, sizeof(*parsed->brushes)) &&
		ConstructionArrayEqual(parsed->brush_sides, caller->brush_sides,
			parsed->brush_side_count, sizeof(*parsed->brush_sides));
}

static sg_host_law_result_t ConstructionLoadWorld(
	const sg_host_collision_authority_t *authority,
	sg_bsp_world_t **world_out)
{
	sg_bsp_error_t error;
	sg_bsp_world_t *parsed = NULL;

	if (!authority || !authority->world || !world_out || *world_out ||
		!authority->world->source_bytes || authority->world->source_size == 0U ||
		!SG_BspWorldSourceIdentityCurrent(authority->world) ||
		memcmp(&authority->content_identity,
			&authority->world->content_identity,
			sizeof(authority->content_identity)) != 0)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(&error, 0, sizeof(error));
	if (!SG_BspWorldLoadMemory(authority->world->source_bytes,
			authority->world->source_size, &parsed, &error))
		return Result(error.code == SG_BSP_ERROR_OUT_OF_MEMORY ?
			SG_HOST_LAW_ALLOCATION_FAILED :
			SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_BSP_CONTENT, error.record,
			SG_BSP_ERROR_NONE, (uint64_t)error.code);
	if (!ConstructionGeometryEqual(parsed, authority->world))
	{
		SG_BspWorldDestroy(parsed);
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	}
	*world_out = parsed;
	return Ok();
}

static uint64_t ConstructionHostBspId(
	const sg_bsp_content_identity_t *identity)
{
	uint64_t digest = SG_HOST_LAW_FNV_OFFSET;
	uint32_t index;

	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		digest = (digest ^ (uint64_t)identity->bytes[index]) *
			SG_HOST_LAW_FNV_PRIME;
	if (digest == 0U || digest == UINT64_MAX)
		digest = UINT64_C(1);
	return digest;
}

/* The collision library needs hull/physics fields on its private authority,
 * but an engine-static host publication does not own the downstream complete
 * model identity.  Populate only values authenticated by the host and leave
 * every downstream identity namespace absent. */
static void ConstructionPrivateIdentity(
	const sg_host_static_identity_t *source,
	sg_rune_model_identity_t *identity_out)
{
	memset(identity_out, 0, sizeof(*identity_out));
	identity_out->bsp_content_id = ConstructionHostBspId(
		&source->bsp_identity);
	identity_out->physics_abi_id = source->physics_abi_id;
	identity_out->standing_hull = source->standing_hull;
	identity_out->crouching_hull = source->crouching_hull;
	identity_out->physics = source->physics;
}

static int ConstructionPrivateIdentityMatches(
	const sg_host_static_identity_t *source,
	const sg_rune_model_identity_t *identity)
{
	sg_rune_model_identity_t expected;

	ConstructionPrivateIdentity(source, &expected);
	return memcmp(&expected, identity, sizeof(expected)) == 0;
}

static sg_host_law_result_t ConstructionAuthorityMatch(
	const sg_host_static_identity_t *expected,
	const sg_host_collision_authority_t *authority)
{
	sg_host_static_identity_t observed;

	memset(&observed, 0, sizeof(observed));
	observed.bsp_identity = authority->content_identity;
	observed.bsp_bytes = (uint64_t)authority->world->source_size;
	observed.engine_checksum = authority->world->engine_checksum;
	observed.entity_crc32 = expected->entity_crc32;
	observed.host_physics_epoch = expected->host_physics_epoch;
	observed.physics_abi_id = authority->identity.physics_abi_id;
	observed.standing_hull = authority->identity.standing_hull;
	observed.crouching_hull = authority->identity.crouching_hull;
	observed.physics = authority->identity.physics;
	return CompareStaticIdentity(expected, &observed,
		SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW);
}

static int ConstructionShapeValid(
	const sg_host_law_construction_t *construction)
{
	return construction &&
		construction->state == SG_HOST_LAW_CONSTRUCTION_STATE &&
		construction->state_inverse == ~SG_HOST_LAW_CONSTRUCTION_STATE &&
		construction->self == construction && construction->world &&
		construction->authority.world == construction->world &&
		CurrentnessValid(construction->currentness) &&
		StaticIdentityValid(&construction->static_identity) &&
		ConstructionPrivateIdentityMatches(&construction->static_identity,
			&construction->authority.identity) &&
		ViewShapeValid(&construction->laws) &&
		construction->pmove_binding.entry &&
		construction->pmove_binding.owner &&
		construction->hook_live_capture &&
		construction->mechanism_live_capture;
}

static sg_host_law_result_t ConstructionRevalidate(
	const sg_host_law_construction_t *construction)
{
	sg_host_law_view_t current;
	sg_host_engine_pmove_binding_t binding;
	sg_host_law_result_t result;

	if (!ConstructionShapeValid(construction))
		return InvalidPublication(SG_HOST_LAW_FIELD_PMOVE_LAW);
	if (construction->currentness->active != 1U)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE,
			construction->currentness->generation, 0U);
	if (!SG_HostEnginePmoveBindingCurrent(&construction->pmove_binding) ||
		construction->hook_live_capture != SG_HostHookLiveCapture ||
		construction->mechanism_live_capture != SG_HostMechanismLiveCapture)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = CaptureLive(NULL, &construction->static_identity, &current,
		&binding);
	if (result.status != SG_HOST_LAW_OK)
	{
		if (result.status == SG_HOST_LAW_HOST_UNAVAILABLE ||
			result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW)
			result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
		return result;
	}
	if (binding.entry != construction->pmove_binding.entry ||
		binding.owner != construction->pmove_binding.owner)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	current.bsp_identity = construction->static_identity.bsp_identity;
	current.bsp_bytes = construction->static_identity.bsp_bytes;
	return CompareViews(&construction->laws, &current,
		SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawPublicationOwnerConstructionIssue(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_authority_t *authority,
	sg_host_law_construction_t **construction_out)
{
	sg_host_law_construction_t *construction = NULL;
	sg_host_collision_error_t collision_error;
	sg_rune_model_identity_t private_identity;
	sg_host_law_result_t result;

	if (!construction_out || *construction_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication) ||
		publication->backend != SG_HOST_LAW_BACKEND_ENGINE_STATIC ||
		!publication->construction_currentness ||
		publication->construction_currentness->active != 1U)
		return InvalidPublication(SG_HOST_LAW_FIELD_PMOVE_LAW);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	construction = calloc(1U, sizeof(*construction));
	if (!construction)
		return Result(SG_HOST_LAW_ALLOCATION_FAILED, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = ConstructionLoadWorld(authority, &construction->world);
	if (result.status != SG_HOST_LAW_OK)
		goto failure;
	memset(&collision_error, 0, sizeof(collision_error));
	ConstructionPrivateIdentity(&publication->static_identity,
		&private_identity);
	if (!SG_HostCollisionInit(&construction->authority, construction->world,
			&private_identity, &collision_error))
	{
		result = Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE,
			SG_HOST_COLLISION_ERROR_NONE, (uint64_t)collision_error);
		goto failure;
	}
	result = ConstructionAuthorityMatch(&publication->static_identity,
		&construction->authority);
	if (result.status != SG_HOST_LAW_OK)
		goto failure;
	if (!CurrentnessRetain(publication->construction_currentness))
	{
		result = Result(SG_HOST_LAW_ALLOCATION_FAILED,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
		goto failure;
	}
	construction->static_identity = publication->static_identity;
	construction->pmove_binding = publication->pmove_binding;
	construction->hook_live_capture = publication->hook_live_capture;
	construction->mechanism_live_capture = publication->mechanism_live_capture;
	construction->currentness = publication->construction_currentness;
	construction->laws = publication->view;
	construction->state = SG_HOST_LAW_CONSTRUCTION_STATE;
	construction->state_inverse = ~SG_HOST_LAW_CONSTRUCTION_STATE;
	construction->self = construction;
	if (!ConstructionShapeValid(construction))
	{
		result = InvalidPublication(SG_HOST_LAW_FIELD_PMOVE_LAW);
		goto failure;
	}
	*construction_out = construction;
	return Ok();

failure:
	if (construction)
	{
		CurrentnessRelease(construction->currentness);
		SG_BspWorldDestroy(construction->world);
		free(construction);
	}
	return result;
}

sg_host_law_result_t SG_HostLawConstructionCurrent(
	const sg_host_law_construction_t *construction)
{
	return ConstructionRevalidate(construction);
}

sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out)
{
	sg_host_law_result_t result;

	if (!view_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(view_out, 0, sizeof(*view_out));
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	view_out->version = SG_HOST_LAW_PUBLICATION_VERSION;
	view_out->current = 1U;
	view_out->level_generation = construction->currentness->generation;
	view_out->host_static_identity = construction->static_identity;
	view_out->geometry.bsp_identity = construction->world->content_identity;
	view_out->geometry.bsp_bytes = (uint64_t)construction->world->source_size;
	view_out->geometry.engine_checksum = construction->world->engine_checksum;
	view_out->geometry.entity_bytes = construction->world->entity_byte_count;
	view_out->geometry.plane_count = construction->world->plane_count;
	view_out->geometry.node_count = construction->world->node_count;
	view_out->geometry.texinfo_count = construction->world->texinfo_count;
	view_out->geometry.leaf_count = construction->world->leaf_count;
	view_out->geometry.leaf_brush_count =
		construction->world->leaf_brush_count;
	view_out->geometry.model_count = construction->world->model_count;
	view_out->geometry.brush_count = construction->world->brush_count;
	view_out->geometry.brush_side_count =
		construction->world->brush_side_count;
	view_out->laws = construction->laws;
	return Ok();
}

sg_host_law_result_t SG_HostLawConstructionOwnerCopyBsp(
	const sg_host_law_construction_t *construction, uint8_t *bytes_out,
	size_t capacity, size_t *size_out,
	sg_host_static_identity_t *identity_out)
{
	sg_host_law_result_t result;
	size_t required;

	if (!size_out || (bytes_out == NULL && capacity != 0U))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	*size_out = 0U;
	if (identity_out)
		memset(identity_out, 0, sizeof(*identity_out));
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	required = construction->world->source_size;
	*size_out = required;
	if (!bytes_out)
		return Ok();
	if (capacity < required)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE,
			(uint64_t)required, (uint64_t)capacity);
	memcpy(bytes_out, construction->world->source_bytes, required);
	if (identity_out)
		*identity_out = construction->static_identity;
	return Ok();
}

static int ConstructionTransformValid(
	const sg_host_collision_transform_t *transform)
{
	return transform && FiniteVector(transform->origin) &&
		FiniteVector(transform->angles);
}

static int ConstructionSceneValid(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene)
{
	size_t first;
	size_t second;

	if (!scene)
		return 1;
	if (scene->instance_count != 0U && !scene->instances)
		return 0;
	for (first = 0U; first < scene->instance_count; first++)
	{
		const sg_host_collision_instance_t *instance =
			&scene->instances[first];

		if (instance->instance_id == 0U || instance->model_index == 0U ||
			instance->model_index >= construction->world->model_count ||
			!ConstructionTransformValid(&instance->transform))
			return 0;
		for (second = first + 1U; second < scene->instance_count; second++)
			if (scene->instances[second].instance_id == instance->instance_id)
				return 0;
	}
	return 1;
}

sg_host_law_result_t SG_HostLawConstructionCollisionTrace(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	sg_host_law_result_t result;

	if (!start || !mins || !maxs || !end || !trace_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!ConstructionSceneValid(construction, scene) ||
		!SG_HostCollisionTrace(&construction->authority, scene, start, mins,
			maxs, end, mask, trace_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawConstructionPointContents(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	sg_host_law_result_t result;

	if (!point || !contents_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	*contents_out = 0U;
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!FiniteVector(point) || !ConstructionSceneValid(construction, scene))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	*contents_out = SG_HostCollisionPointContents(&construction->authority,
		scene, point);
	return Ok();
}

sg_host_law_result_t SG_HostLawConstructionClassifyPose(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	sg_host_law_result_t result;

	if (!origin || !pose_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!ConstructionSceneValid(construction, scene) ||
		!SG_HostCollisionClassifyPose(&construction->authority, scene, origin,
			stance, pose_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawConstructionTransition(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out)
{
	sg_host_law_result_t result;

	if (!start || !end || !transition_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!ConstructionSceneValid(construction, scene) ||
		!SG_HostCollisionTransition(&construction->authority, scene, start,
			end, stance, transition_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawConstructionPmove(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;
	sg_host_law_result_t result;

	if (!request || !result_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_PMOVE_LAW,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_HostPmoveEvaluateFrame(&construction->authority, scene,
			construction->pmove_binding.entry, request, result_out, &error))
	{
		if (error_out)
			*error_out = error;
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U,
			(uint64_t)error);
	}
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return Ok();
}

sg_host_law_result_t SG_HostLawConstructionReplayFrame(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;
	sg_host_law_result_t result;

	if (!request || !workspace || !replay_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_PMOVE_LAW,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	result = ConstructionRevalidate(construction);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_HostPmoveReplayFrame(&construction->authority, scene,
			construction->pmove_binding.entry, request, workspace, replay_out,
			&error))
	{
		if (error_out)
			*error_out = error;
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U,
			(uint64_t)error);
	}
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return Ok();
}

void SG_HostLawConstructionDestroy(sg_host_law_construction_t *construction)
{
	if (!ConstructionShapeValid(construction))
		return;
	construction->state = 0U;
	construction->state_inverse = 0U;
	construction->self = NULL;
	SG_BspWorldDestroy(construction->world);
	construction->world = NULL;
	construction->authority.world = NULL;
	CurrentnessRelease(construction->currentness);
	construction->currentness = NULL;
	memset(&construction->authority, 0, sizeof(construction->authority));
	memset(&construction->static_identity, 0,
		sizeof(construction->static_identity));
	memset(&construction->laws, 0, sizeof(construction->laws));
	free(construction);
}

sg_host_law_result_t SG_HostLawPublicationOwnerPmove(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;
	sg_host_law_result_t result;

	if (!request || !result_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_PMOVE_LAW,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication) ||
		publication->backend != SG_HOST_LAW_BACKEND_ENGINE_RUNTIME)
		return InvalidPublication(SG_HOST_LAW_FIELD_PMOVE_LAW);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_HostEngineRuntimePmove(publication->runtime, subject_index, request,
		result_out, &error))
	{
		if (error_out)
			*error_out = error;
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U,
			(uint64_t)error);
	}
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationHookPullVelocity(
	const sg_host_law_publication_t *publication, const vec3_t start,
	const vec3_t bite, vec3_t velocity, int *rope_length_out)
{
	if (!FiniteVector(start) || !FiniteVector(bite) || !velocity ||
		!rope_length_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_HOOK_LAW,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_LAW);
	*rope_length_out = CTF_HookPullVelocity(start, bite, velocity);
	if (*rope_length_out < 0 || !FiniteVector(velocity))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_HOOK_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationHookMuzzle(
	const sg_host_law_publication_t *publication, const float origin[3],
	float viewheight, int hand, const float forward[3], const float right[3],
	float start_out[3])
{
	if (!origin || !forward || !right || !start_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_HOOK_LAW,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_LAW);
	if (!SG_HostHookMuzzle(origin, viewheight, hand, forward, right, start_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

#ifdef SG_HOST_LAW_TESTING
sg_host_law_result_t SG_HostLawPublicationHookStep(
	const sg_host_law_publication_t *publication,
	const sg_host_hook_observation_t *observation, sg_host_hook_step_t *step_out)
{
	if (!observation || !step_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	if (!SG_HostHookStep(&publication->view.hook, observation, step_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}
#endif

#ifdef SG_HOST_LAW_TESTING
static sg_host_hook_target_kind_t HookTargetKindFromCollision(
	const sg_host_collision_trace_t *trace)
{
	if (trace->instance_id == 0U)
		return SG_HOST_HOOK_TARGET_WORLD;
	/* A static scene carries geometry identity only.  It does not carry the
	 * live edict classname/team/dead state needed by hook_touch.  Refuse to
	 * invent FUNC semantics from a model number; only the runtime backend,
	 * which owns the traced edict, may admit a non-world target. */
	return SG_HOST_HOOK_TARGET_OTHER;
}

static uint64_t HookTargetIdentityFromCollision(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_trace_t *trace)
{
	return trace->instance_id != 0U ? trace->instance_id :
		authority->identity.bsp_content_id;
}

sg_host_law_result_t SG_HostLawPublicationHookFire(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene,
	const sg_host_hook_fire_request_t *request,
	sg_host_hook_step_t *step_out)
{
	sg_host_hook_observation_t observation;
	sg_host_hook_collision_t collision;
	sg_host_collision_trace_t trace;
	sg_host_law_result_t result;
	const float zero[3] = { 0.0f, 0.0f, 0.0f };

	if (!request || !step_out || !FiniteVector(request->start) ||
		!FiniteVector(request->end))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (publication->backend != SG_HOST_LAW_BACKEND_CONTROLLER)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(&collision, 0, sizeof(collision));
	memset(&trace, 0, sizeof(trace));
	if (!SG_HostCollisionTrace(&publication->authority, scene,
		request->start, zero, zero, request->end,
		(sg_host_collision_contents_t)publication->view.hook.trace_mask,
		&trace))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	{
		int hit = trace.fraction < 1.0f || trace.startsolid || trace.allsolid;

		collision.hit = hit;
		if (hit)
		{
			/* Static construction has no live entity table, so it can
			 * authenticate WORLD geometry only and never trusts the request's
			 * owner_instance_id.  Non-world geometry remains OTHER and is
			 * rejected by the hook chronology. */
			collision.sky = (trace.surface_flags & SG_HOST_SURFACE_SKY) != 0;
			collision.trace_epsilon_applied = 1;
			collision.target_kind = HookTargetKindFromCollision(&trace);
			collision.target_identity = HookTargetIdentityFromCollision(
				&publication->authority, &trace);
		}
	}
	memset(&observation, 0, sizeof(observation));
	observation.event = request->phase == SG_HOST_HOOK_COAST ?
		SG_HOST_HOOK_REFIRE : SG_HOST_HOOK_FIRE;
	observation.phase = request->phase;
	observation.attack_held = request->attack_held;
	/* FIRE consumes only owner-derived collision facts.  The caller's old
	 * observation booleans and target identity never enter this path. */
	if (!SG_HostHookStepWithCollision(&publication->view.hook, &observation,
		&collision, step_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	step_out->collision_hit = collision.hit;
	if (collision.hit)
	{
		memcpy(step_out->collision_end, trace.end,
			sizeof(step_out->collision_end));
		memcpy(step_out->collision_plane_normal, trace.plane.normal,
			sizeof(step_out->collision_plane_normal));
		step_out->collision_plane_distance = trace.plane.distance;
		step_out->collision_plane_type = trace.plane.type;
		step_out->collision_surface_flags = trace.surface_flags;
		step_out->collision_instance_id = trace.instance_id;
	}
	return Ok();
}
#endif

sg_host_law_result_t SG_HostLawPublicationOwnerHookFire(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	uint32_t hook_index, sg_host_hook_step_t *step_out)
{
	sg_host_hook_observation_t observation;
	sg_host_hook_collision_t collision;
	sg_host_collision_trace_t trace;
	sg_host_law_result_t result;

	if (!step_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication) ||
		publication->backend != SG_HOST_LAW_BACKEND_ENGINE_RUNTIME)
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	memset(&collision, 0, sizeof(collision));
	memset(&trace, 0, sizeof(trace));
	if (!SG_HostEngineRuntimeHookTrace(publication->runtime, subject_index,
		hook_index,
		(sg_host_collision_contents_t)publication->view.hook.trace_mask,
		&collision, &trace))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FIRE;
	observation.phase = SG_HOST_HOOK_IDLE;
	observation.attack_held = 1;
	if (!SG_HostHookStepWithCollision(&publication->view.hook, &observation,
		&collision, step_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	step_out->collision_hit = collision.hit;
	if (collision.hit)
	{
		memcpy(step_out->collision_end, trace.end,
			sizeof(step_out->collision_end));
		memcpy(step_out->collision_plane_normal, trace.plane.normal,
			sizeof(step_out->collision_plane_normal));
		step_out->collision_plane_distance = trace.plane.distance;
		step_out->collision_plane_type = trace.plane.type;
		step_out->collision_surface_flags = trace.surface_flags;
		step_out->collision_instance_id = trace.instance_id;
	}
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationOwnerHookTouch(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_step_t *step_out)
{
	sg_host_hook_observation_t observation;
	sg_host_law_result_t result;

	if (!step_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (publication->backend != SG_HOST_LAW_BACKEND_ENGINE_RUNTIME ||
		!publication->runtime ||
		!SG_HostEngineRuntimeOwnerHookCollision(publication->runtime,
			subject_index, hook_index, target_index, surface_flags,
			&observation))
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostHookStep(&publication->view.hook, &observation, step_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationOwnerHookPullVelocity(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	uint32_t hook_index, vec3_t velocity, int *rope_length_out)
{
	vec3_t start;
	vec3_t bite;
	sg_host_law_result_t result;

	if (!velocity || !rope_length_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_HOOK_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication) ||
		publication->backend != SG_HOST_LAW_BACKEND_ENGINE_RUNTIME)
		return InvalidPublication(SG_HOST_LAW_FIELD_HOOK_LAW);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_HostEngineRuntimeOwnerHookPullInputs(publication->runtime,
		subject_index, hook_index, start, bite))
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_HOOK_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return SG_HostLawPublicationHookPullVelocity(publication, start, bite,
		velocity, rope_length_out);
}

sg_host_law_result_t SG_HostLawPublicationMoveSchedule(
	const sg_host_law_publication_t *publication, float distance, float speed,
	float accel, float decel, int current_entity,
	sg_host_mechanism_move_result_t *result_out)
{
	if (!result_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
	if (!SG_HostMechanismMoveSchedule(&publication->view.mechanism, distance,
		speed, accel, decel, current_entity, result_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationDoorStep(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out)
{
	return SG_HostLawPublicationDoorStepEx(publication, event, flags, state,
		wait_seconds, now_ms, debounce_until_ms,
		SG_HOST_MECHANISM_BLOCKER_CLIENT,
		SG_HOST_MECHANISM_DEFAULT_DOOR_DAMAGE, result_out);
}

sg_host_law_result_t SG_HostLawPublicationDoorStepEx(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !PublicationValid(publication))
		return Result(!result_out ? SG_HOST_LAW_INVALID_ARGUMENT :
			SG_HOST_LAW_CORRUPT_PUBLICATION, SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostMechanismDoorStepEx(&publication->view.mechanism, event, flags,
		state, wait_seconds, now_ms, debounce_until_ms, blocker_kind, damage,
		result_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationPlatformStep(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_transition_t *result_out)
{
	return SG_HostLawPublicationPlatformStepEx(publication, event, state,
		now_ms, debounce_until_ms, SG_HOST_MECHANISM_BLOCKER_CLIENT,
		SG_HOST_MECHANISM_DEFAULT_DOOR_DAMAGE, result_out);
}

sg_host_law_result_t SG_HostLawPublicationPlatformStepEx(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_blocker_kind_t blocker_kind,
	uint32_t damage, sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !PublicationValid(publication))
		return Result(!result_out ? SG_HOST_LAW_INVALID_ARGUMENT :
			SG_HOST_LAW_CORRUPT_PUBLICATION, SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostMechanismPlatformStepEx(&publication->view.mechanism, event,
		state, now_ms, debounce_until_ms, blocker_kind, damage, result_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationTriggerStep(
	const sg_host_law_publication_t *publication, int already_triggered,
	float wait_seconds, uint64_t now_ms, sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !PublicationValid(publication))
		return Result(!result_out ? SG_HOST_LAW_INVALID_ARGUMENT :
			SG_HOST_LAW_CORRUPT_PUBLICATION, SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostMechanismTriggerStep(&publication->view.mechanism,
		already_triggered, wait_seconds, now_ms, result_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationTrainStep(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_train_event_t event, uint32_t flags, float wait_seconds,
	int state, int has_target, int has_current_target,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !PublicationValid(publication))
		return Result(!result_out ? SG_HOST_LAW_INVALID_ARGUMENT :
			SG_HOST_LAW_CORRUPT_PUBLICATION, SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostMechanismTrainStep(&publication->view.mechanism, event, flags,
		wait_seconds, state, has_target, has_current_target, blocker_kind,
		damage, now_ms, debounce_until_ms, result_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

void SG_HostLawPublicationOwnerDestroy(sg_host_law_publication_t *publication)
{
	if (!publication || publication->state != SG_HOST_LAW_STATE ||
		publication->state_inverse != ~SG_HOST_LAW_STATE ||
		publication->self != publication)
		return;
	CurrentnessRevoke(publication->construction_currentness);
	CurrentnessRelease(publication->construction_currentness);
	publication->construction_currentness = NULL;
	publication->state = 0U;
	publication->state_inverse = 0U;
	publication->self = NULL;
	publication->authority.world = NULL;
	publication->runtime = NULL;
	publication->backend = 0;
	memset(&publication->authority.identity, 0,
		sizeof(publication->authority.identity));
	memset(&publication->static_identity, 0,
		sizeof(publication->static_identity));
	memset(&publication->view, 0, sizeof(publication->view));
	free(publication);
}

const char *SG_HostLawStatusString(sg_host_law_status_t status)
{
	switch (status)
	{
	case SG_HOST_LAW_OK: return "ok";
	case SG_HOST_LAW_INVALID_ARGUMENT: return "invalid argument";
	case SG_HOST_LAW_HOST_UNAVAILABLE: return "engine provider unavailable";
	case SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW: return "unsupported engine state";
	case SG_HOST_LAW_ALLOCATION_FAILED: return "allocation failed";
	case SG_HOST_LAW_CORRUPT_PUBLICATION: return "corrupt publication";
	case SG_HOST_LAW_PRODUCTION_DRIFT: return "engine provider drift";
	case SG_HOST_LAW_EVALUATION_FAILED: return "engine evaluation failed";
	default: return "unknown engine provider status";
	}
}

const char *SG_HostLawFieldString(sg_host_law_field_t field)
{
	switch (field)
	{
	case SG_HOST_LAW_FIELD_NONE: return "none";
	case SG_HOST_LAW_FIELD_VERSION: return "version";
	case SG_HOST_LAW_FIELD_COLLISION_LAW: return "collision law";
	case SG_HOST_LAW_FIELD_PMOVE_LAW: return "Pmove law";
	case SG_HOST_LAW_FIELD_PMOVE_ABI: return "Pmove ABI";
	case SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR: return "Pmove behavior";
	case SG_HOST_LAW_FIELD_GRAVITY_LAW: return "gravity law";
	case SG_HOST_LAW_FIELD_HOOK_LAW: return "hook law";
	case SG_HOST_LAW_FIELD_MECHANISM_LAW: return "mechanism law";
	case SG_HOST_LAW_FIELD_BSP_CONTENT: return "BSP content";
	case SG_HOST_LAW_FIELD_ENTITY_SEMANTICS: return "entity semantics";
	case SG_HOST_LAW_FIELD_PHYSICS_ABI: return "physics ABI";
	case SG_HOST_LAW_FIELD_SCHEMA: return "schema";
	case SG_HOST_LAW_FIELD_SOURCE_SET: return "source set";
	case SG_HOST_LAW_FIELD_PRODUCER: return "producer";
	case SG_HOST_LAW_FIELD_STANDING_HULL_MINS: return "standing hull mins";
	case SG_HOST_LAW_FIELD_STANDING_HULL_MAXS: return "standing hull maxs";
	case SG_HOST_LAW_FIELD_CROUCHING_HULL_MINS: return "crouching hull mins";
	case SG_HOST_LAW_FIELD_CROUCHING_HULL_MAXS: return "crouching hull maxs";
	case SG_HOST_LAW_FIELD_GRAVITY: return "gravity";
	case SG_HOST_LAW_FIELD_GROUND_ACCELERATION: return "ground acceleration";
	case SG_HOST_LAW_FIELD_AIR_ACCELERATION: return "air acceleration";
	case SG_HOST_LAW_FIELD_WATER_ACCELERATION: return "water acceleration";
	case SG_HOST_LAW_FIELD_HOOK_ACCELERATION: return "hook acceleration";
	case SG_HOST_LAW_FIELD_EXTERNAL_ACCELERATION: return "external acceleration";
	case SG_HOST_LAW_FIELD_WATER_DRAG: return "water drag";
	case SG_HOST_LAW_FIELD_MODEL_MAX_VELOCITY: return "model max velocity";
	case SG_HOST_LAW_FIELD_FRAME_MS: return "frame ms";
	case SG_HOST_LAW_FIELD_SUBSTEP_MS: return "substep ms";
	case SG_HOST_LAW_FIELD_AIRACCELERATE: return "airaccelerate";
	case SG_HOST_LAW_FIELD_MAXVELOCITY: return "maxvelocity";
	case SG_HOST_LAW_FIELD_MOVEMENT_FLAGS: return "movement flags";
	case SG_HOST_LAW_FIELD_PHYSICS_FLAGS: return "physics flags";
	case SG_HOST_LAW_FIELD_HOOK_FIRE_SPEED: return "hook fire speed";
	case SG_HOST_LAW_FIELD_HOOK_PULL_SPEED: return "hook pull speed";
	case SG_HOST_LAW_FIELD_HOOK_INITIAL_DAMAGE: return "hook initial damage";
	case SG_HOST_LAW_FIELD_HOOK_ATTACHED_DAMAGE: return "hook attached damage";
	case SG_HOST_LAW_FIELD_HOOK_HEALTH: return "hook health";
	case SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY: return "hook chronology";
	case SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS: return "mechanism equations";
	default: return "unknown host-law field";
	}
}
