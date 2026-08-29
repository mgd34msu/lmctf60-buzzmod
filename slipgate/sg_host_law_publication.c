#include "sg_host_law_publication.h"
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

#define SG_HOST_GROUND_ACCELERATION 10.0f
#define SG_HOST_AIR_ACCELERATION 1.0f
#define SG_HOST_WATER_ACCELERATION 10.0f
#define SG_HOST_HOOK_ACCELERATION 800.0f
#define SG_HOST_EXTERNAL_ACCELERATION 1.0f
#define SG_HOST_WATER_DRAG 1.0f

static const sg_rune_hull_profile_t sg_standing_hull = {
	{ { -16.0f, -16.0f, -24.0f } }, { { 16.0f, 16.0f, 32.0f } }
};
static const sg_rune_hull_profile_t sg_crouching_hull = {
	{ { -16.0f, -16.0f, -24.0f } }, { { 16.0f, 16.0f, 4.0f } }
};

struct sg_host_law_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	const sg_host_law_publication_t *self;
	sg_host_collision_authority_t authority;
	sg_host_engine_pmove_binding_t pmove_binding;
	sg_host_hook_live_capture_function_t hook_live_capture;
	sg_host_mechanism_live_capture_function_t mechanism_live_capture;
	sg_host_law_view_t view;
};

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
		identity->physics_abi_id == UINT64_MAX || !identity->source_set_identity ||
		identity->source_set_identity == UINT64_MAX || !identity->schema_id ||
		identity->schema_id == UINT64_MAX || !identity->producer_identity ||
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
	SG_COMPARE_MECH_FLOAT(platform_default_speed);
	SG_COMPARE_MECH_FLOAT(platform_default_accel);
	SG_COMPARE_MECH_FLOAT(platform_default_decel);
	SG_COMPARE_MECH_FLOAT(train_default_speed);
#undef SG_COMPARE_MECH_FLOAT
	return Ok();
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
	return view && view->version == SG_HOST_LAW_PUBLICATION_VERSION &&
		view->reserved == 0U && view->collision_law_id == SG_HOST_COLLISION_ID &&
		view->pmove_law_id == SG_HOST_PMOVE_ID &&
		view->gravity_law_id == SG_HOST_GRAVITY_ID &&
		view->hook_law_id == SG_HOST_HOOK_LAW_ID &&
		view->mechanism_law_id == SG_HOST_MECHANISM_LAW_ID &&
		IdentityValid(&view->identity) && ABIShapeValid(&view->pmove_abi) &&
		view->pmove_behavior_fingerprint &&
		SameFloat(view->airaccelerate, 0.0f) &&
		SameFloat(view->maxvelocity, view->identity.physics.max_velocity) &&
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
	return publication && publication->state == SG_HOST_LAW_STATE &&
		publication->state_inverse == ~SG_HOST_LAW_STATE &&
		publication->self == publication && publication->authority.world &&
		publication->pmove_binding.entry &&
		publication->pmove_binding.owner != NULL &&
		publication->hook_live_capture != NULL &&
		publication->mechanism_live_capture != NULL &&
		ViewShapeValid(&publication->view) &&
		CompareIdentity(&publication->authority.identity,
			&publication->view.identity, SG_HOST_LAW_PRODUCTION_DRIFT).status ==
			SG_HOST_LAW_OK;
}

static sg_host_law_result_t CaptureProduction(
	const sg_host_collision_authority_t *authority,
	sg_host_law_view_t *view_out,
	sg_host_engine_pmove_binding_t *binding_out)
{
	sg_host_engine_pmove_abi_t abi;
	sg_host_engine_pmove_binding_t binding;
	sg_host_engine_parity_result_t parity;
	sg_host_hook_law_t hook;
	sg_host_mechanism_law_t mechanism;
	cvar_t *airaccelerate;
	float gravity;
	float maxvelocity;
	float ctf_flags;
	uint32_t index;

	if (!authority || !view_out || !binding_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (!authority->world || !IdentityValid(&authority->identity))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT, SG_HOST_LAW_FIELD_PHYSICS_ABI,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostEnginePmoveABI(&abi) ||
		!SG_HostEnginePmoveBindingCapture(&binding))
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_PMOVE_ABI,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (abi.substep_ms != authority->identity.physics.substep_ms ||
		abi.substep_ms != SG_HOST_ENGINE_PMOVE_SUBSTEP_MS ||
		authority->identity.physics.frame_ms != SG_HOST_ENGINE_FRAME_MS)
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
		!SameFloat(gravity, authority->identity.physics.gravity))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_GRAVITY, SG_HOST_LAW_ELEMENT_NONE,
			FloatBits(authority->identity.physics.gravity), FloatBits(gravity));
	if (!SameFloat(airaccelerate->value, 0.0f))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_AIRACCELERATE, SG_HOST_LAW_ELEMENT_NONE, 0U,
			FloatBits(airaccelerate->value));
	if (!isfinite(maxvelocity) || maxvelocity < (float)SG_HOST_ENGINE_MAXVELOCITY_MIN ||
		!SameFloat(maxvelocity, authority->identity.physics.max_velocity))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_MAXVELOCITY, SG_HOST_LAW_ELEMENT_NONE,
			FloatBits(authority->identity.physics.max_velocity),
			FloatBits(maxvelocity));
	if (!SameFloat(want_funky_gravity->value, 0.0f))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_PHYSICS_FLAGS, SG_HOST_LAW_ELEMENT_NONE, 0U, 1U);
	{
		sg_host_engine_parity_inputs_t inputs = {
			authority->identity.physics.gravity,
			authority->identity.physics.max_velocity,
			authority->identity.physics.air_acceleration,
			authority->identity.physics.frame_ms,
			authority->identity.physics.substep_ms
		};

		if (!SG_HostEnginePmoveParityBound(&binding, &inputs, &parity))
			return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
				SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, SG_HOST_LAW_ELEMENT_NONE,
				SG_HOST_ENGINE_PARITY_ALL, parity.cases);
	}
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
	view_out->identity = authority->identity;
	view_out->pmove_abi = abi;
	view_out->pmove_behavior_fingerprint = parity.fingerprint;
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
	*binding_out = binding;
	for (index = 0U; index < 3U; index++)
	{
		if (!SameFloat(view_out->identity.standing_hull.mins.value[index],
			sg_standing_hull.mins.value[index]) ||
			!SameFloat(view_out->identity.standing_hull.maxs.value[index],
			sg_standing_hull.maxs.value[index]) ||
			!SameFloat(view_out->identity.crouching_hull.mins.value[index],
			sg_crouching_hull.mins.value[index]) ||
			!SameFloat(view_out->identity.crouching_hull.maxs.value[index],
			sg_crouching_hull.maxs.value[index]))
			return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
				SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR, index, 1U, 0U);
	}
	if (!SameFloat(view_out->identity.physics.ground_acceleration,
		SG_HOST_GROUND_ACCELERATION) ||
		!SameFloat(view_out->identity.physics.air_acceleration,
		SG_HOST_AIR_ACCELERATION) ||
		!SameFloat(view_out->identity.physics.water_acceleration,
		SG_HOST_WATER_ACCELERATION) ||
		!SameFloat(view_out->identity.physics.hook_acceleration,
		SG_HOST_HOOK_ACCELERATION) ||
		!SameFloat(view_out->identity.physics.external_acceleration,
		SG_HOST_EXTERNAL_ACCELERATION) ||
		!SameFloat(view_out->identity.physics.water_drag, SG_HOST_WATER_DRAG))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_GRAVITY_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

static sg_host_law_result_t InvalidPublication(sg_host_law_field_t field)
{
	return Result(SG_HOST_LAW_CORRUPT_PUBLICATION, field,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

sg_host_law_result_t SG_HostLawPublicationIssue(
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
	result = CaptureProduction(authority, &view, &binding);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	publication = malloc(sizeof(*publication));
	if (!publication)
		return Result(SG_HOST_LAW_ALLOCATION_FAILED, SG_HOST_LAW_FIELD_NONE,
			SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	publication->state = SG_HOST_LAW_STATE;
	publication->state_inverse = ~SG_HOST_LAW_STATE;
	publication->self = publication;
	publication->authority = *authority;
	publication->pmove_binding = binding;
	publication->hook_live_capture = SG_HostHookLiveCapture;
	publication->mechanism_live_capture = SG_HostMechanismLiveCapture;
	publication->view = view;
	*publication_out = publication;
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

	if (!PublicationValid(publication))
		return InvalidPublication(SG_HOST_LAW_FIELD_NONE);
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
	result = CaptureProduction(&publication->authority, &current, &binding);
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
	memset(trace_out, 0, sizeof(*trace_out));
	if (!SG_HostCollisionTrace(&publication->authority, scene, start, mins, maxs,
		end, mask, trace_out))
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
	if (!SG_HostPmoveEvaluateBoundEngineFrame(&publication->authority, scene,
		request, &publication->pmove_binding,
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
	int state, int has_target, int has_current_target, int has_damage,
	int other_is_client_or_monster, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out)
{
	if (!result_out || !PublicationValid(publication))
		return Result(!result_out ? SG_HOST_LAW_INVALID_ARGUMENT :
			SG_HOST_LAW_CORRUPT_PUBLICATION, SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS,
			SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!SG_HostMechanismTrainStep(&publication->view.mechanism, event, flags,
		wait_seconds, state, has_target, has_current_target, has_damage,
		other_is_client_or_monster, now_ms, debounce_until_ms, result_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

void SG_HostLawPublicationDestroy(sg_host_law_publication_t *publication)
{
	if (!PublicationValid(publication))
		return;
	publication->state = 0U;
	publication->state_inverse = 0U;
	publication->self = NULL;
	publication->authority.world = NULL;
	memset(&publication->authority.identity, 0,
		sizeof(publication->authority.identity));
	memset(&publication->view, 0, sizeof(publication->view));
	free(publication);
}

const char *SG_HostLawStatusString(sg_host_law_status_t status)
{
	switch (status)
	{
	case SG_HOST_LAW_OK: return "ok";
	case SG_HOST_LAW_INVALID_ARGUMENT: return "invalid argument";
	case SG_HOST_LAW_HOST_UNAVAILABLE: return "host unavailable";
	case SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW: return "unsupported production law";
	case SG_HOST_LAW_ALLOCATION_FAILED: return "allocation failed";
	case SG_HOST_LAW_CORRUPT_PUBLICATION: return "corrupt publication";
	case SG_HOST_LAW_PRODUCTION_DRIFT: return "production law drift";
	case SG_HOST_LAW_EVALUATION_FAILED: return "law evaluation failed";
	default: return "unknown host-law status";
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
