#include "sg_host_law_publication.h"
#include "sg_action_contract.generated.h"
#include "sg_weapon_host_constants.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"
#include "sg_hooks.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
extern cvar_t *sv_gravity;
extern cvar_t *sv_maxvelocity;
extern cvar_t *want_funky_gravity;
extern int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);
#define SG_HOST_LAW_PUBLICATION_STATE UINT32_C(0x484c5032)
#define SG_HOST_LAW_COLLISION_ID UINT64_C(0x434f4c4c49534932)
#define SG_HOST_LAW_PMOVE_ID UINT64_C(0x504d4f56454c5732)
#define SG_HOST_LAW_GRAVITY_ID UINT64_C(0x4752415649545932)
#define SG_HOST_LAW_HOOK_ID UINT64_C(0x484f4f4b4c415732)
#define SG_HOST_LAW_MECHANISM_ID UINT64_C(0x4d4543484c415732)

#define SG_HOST_LAW_GROUND_ACCELERATION 10.0f
#define SG_HOST_LAW_AIR_ACCELERATION 1.0f
#define SG_HOST_LAW_WATER_ACCELERATION 10.0f
#define SG_HOST_LAW_HOOK_ACCELERATION 800.0f
#define SG_HOST_LAW_EXTERNAL_ACCELERATION 1.0f
#define SG_HOST_LAW_WATER_DRAG 1.0f
static const sg_rune_hull_profile_t sg_host_law_standing_hull = {
	{ { -16.0f, -16.0f, -24.0f } },
	{ { 16.0f, 16.0f, 32.0f } }
};
static const sg_rune_hull_profile_t sg_host_law_crouching_hull = {
	{ { -16.0f, -16.0f, -24.0f } },
	{ { 16.0f, 16.0f, 4.0f } }
};
_Static_assert(SG_RUNE_PROOF_SERVER_FRAME_MS == SG_HOST_SERVER_FRAME_MS,
	"host frame laws diverged");
_Static_assert(SG_RUNE_PROOF_HOOK_BOLT_SPEED == SG_HOST_HOOK_FIRE_SPEED,
	"host hook bolt laws diverged");
_Static_assert(SG_HOST_HOOK_PULL_SPEED == 800,
	"host hook pull law identity must change with pull speed");
_Static_assert(SG_HOST_HOOK_INITIAL_DAMAGE == 8,
	"host hook initial damage law changed");
_Static_assert(SG_HOST_HOOK_ATTACHED_DAMAGE == 1,
	"host hook attached damage law changed");
_Static_assert(SG_HOST_HOOK_HEALTH == 59,
	"host hook health law changed");

struct sg_host_law_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	const sg_host_law_publication_t *self;
	/* The collision authority owns the canonical identity and borrows its BSP. */
	sg_host_collision_authority_t authority;
	/* Private capture only; no public API can replace or retrieve this pointer. */
	sg_host_pmove_function_t pmove;
	sg_host_law_view_t view;
};
static sg_host_law_result_t Result(sg_host_law_status_t status,
	sg_host_law_field_t field, uint32_t element, uint64_t expected,
	uint64_t observed)
{
	sg_host_law_result_t result;

	result.status = status;
	result.field = field;
	result.element = element;
	result.reserved = 0U;
	result.expected_bits = expected;
	result.observed_bits = observed;
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

static int FloatEqual(float left, float right)
{
	return FloatBits(left) == FloatBits(right);
}

static int FiniteHull(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(hull->mins.value[axis]) ||
			!isfinite(hull->maxs.value[axis]) ||
			hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	}
	return 1;
}

static int FinitePhysics(const sg_rune_physics_parameters_t *physics)
{
	return physics && isfinite(physics->gravity) &&
		isfinite(physics->ground_acceleration) &&
		isfinite(physics->air_acceleration) &&
		isfinite(physics->water_acceleration) &&
		isfinite(physics->hook_acceleration) &&
		isfinite(physics->external_acceleration) &&
		isfinite(physics->water_drag) &&
		isfinite(physics->max_velocity) && physics->gravity >= 0.0f &&
		physics->ground_acceleration >= 0.0f &&
		physics->air_acceleration >= 0.0f &&
		physics->water_acceleration >= 0.0f &&
		physics->hook_acceleration >= 0.0f &&
		physics->external_acceleration >= 0.0f &&
		physics->water_drag >= 0.0f && physics->max_velocity > 0.0f &&
		physics->frame_ms != 0U && physics->substep_ms != 0U;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	return identity && identity->bsp_content_id != 0U &&
		identity->bsp_content_id != UINT64_MAX &&
		identity->entity_semantics_id != 0U &&
		identity->entity_semantics_id != UINT64_MAX &&
		identity->physics_abi_id != 0U &&
		identity->physics_abi_id != UINT64_MAX &&
		identity->source_set_identity != 0U &&
		identity->source_set_identity != UINT64_MAX &&
		identity->schema_id != 0U && identity->schema_id != UINT64_MAX &&
		identity->producer_identity != 0U &&
		identity->producer_identity != UINT64_MAX &&
		FiniteHull(&identity->standing_hull) &&
		FiniteHull(&identity->crouching_hull) &&
		FinitePhysics(&identity->physics);
}

static sg_host_law_result_t CompareFloat(float expected, float observed,
	sg_host_law_status_t status, sg_host_law_field_t field, uint32_t element)
{
	if (FloatEqual(expected, observed))
		return Ok();
	return Result(status, field, element, FloatBits(expected),
		FloatBits(observed));
}

static sg_host_law_result_t CompareU32(uint32_t expected, uint32_t observed,
	sg_host_law_status_t status, sg_host_law_field_t field)
{
	if (expected == observed)
		return Ok();
	return Result(status, field, SG_HOST_LAW_ELEMENT_NONE, expected,
		observed);
}

static sg_host_law_result_t CompareU64(uint64_t expected, uint64_t observed,
	sg_host_law_status_t status, sg_host_law_field_t field)
{
	if (expected == observed)
		return Ok();
	return Result(status, field, SG_HOST_LAW_ELEMENT_NONE, expected,
		observed);
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
			observed->mins.value[axis], status, mins_field, axis);
		if (result.status != SG_HOST_LAW_OK)
			return result;
		result = CompareFloat(expected->maxs.value[axis],
			observed->maxs.value[axis], status, maxs_field, axis);
		if (result.status != SG_HOST_LAW_OK)
			return result;
	}
	return Ok();
}
static sg_host_law_result_t CompareIdentity(
	const sg_rune_model_identity_t *expected,
	const sg_rune_model_identity_t *observed, sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	result = CompareU64(expected->bsp_content_id, observed->bsp_content_id,
		status, SG_HOST_LAW_FIELD_BSP_CONTENT);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->entity_semantics_id,
		observed->entity_semantics_id, status,
		SG_HOST_LAW_FIELD_ENTITY_SEMANTICS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->physics_abi_id, observed->physics_abi_id,
		status, SG_HOST_LAW_FIELD_PHYSICS_ABI);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->source_set_identity,
		observed->source_set_identity, status, SG_HOST_LAW_FIELD_SOURCE_SET);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->schema_id, observed->schema_id, status,
		SG_HOST_LAW_FIELD_SCHEMA);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->producer_identity,
		observed->producer_identity, status, SG_HOST_LAW_FIELD_PRODUCER);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareHull(&expected->standing_hull, &observed->standing_hull,
		status, SG_HOST_LAW_FIELD_STANDING_HULL_MINS,
		SG_HOST_LAW_FIELD_STANDING_HULL_MAXS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareHull(&expected->crouching_hull,
		&observed->crouching_hull, status,
		SG_HOST_LAW_FIELD_CROUCHING_HULL_MINS,
		SG_HOST_LAW_FIELD_CROUCHING_HULL_MAXS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.gravity,
		observed->physics.gravity, status, SG_HOST_LAW_FIELD_GRAVITY,
		SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.ground_acceleration,
		observed->physics.ground_acceleration, status,
		SG_HOST_LAW_FIELD_GROUND_ACCELERATION, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.air_acceleration,
		observed->physics.air_acceleration, status,
		SG_HOST_LAW_FIELD_AIR_ACCELERATION, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.water_acceleration,
		observed->physics.water_acceleration, status,
		SG_HOST_LAW_FIELD_WATER_ACCELERATION, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.hook_acceleration,
		observed->physics.hook_acceleration, status,
		SG_HOST_LAW_FIELD_HOOK_ACCELERATION, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.external_acceleration,
		observed->physics.external_acceleration, status,
		SG_HOST_LAW_FIELD_EXTERNAL_ACCELERATION, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.water_drag,
		observed->physics.water_drag, status, SG_HOST_LAW_FIELD_WATER_DRAG,
		SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->physics.max_velocity,
		observed->physics.max_velocity, status,
		SG_HOST_LAW_FIELD_MODEL_MAX_VELOCITY, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->physics.frame_ms,
		observed->physics.frame_ms, status, SG_HOST_LAW_FIELD_FRAME_MS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return CompareU32(expected->physics.substep_ms,
		observed->physics.substep_ms, status, SG_HOST_LAW_FIELD_SUBSTEP_MS);
}

static sg_host_law_result_t CompareViews(const sg_host_law_view_t *expected,
	const sg_host_law_view_t *observed, sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	result = CompareU32(expected->version, observed->version, status,
		SG_HOST_LAW_FIELD_VERSION);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->collision_law_id,
		observed->collision_law_id, status, SG_HOST_LAW_FIELD_COLLISION_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->pmove_law_id, observed->pmove_law_id,
		status, SG_HOST_LAW_FIELD_PMOVE_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->gravity_law_id, observed->gravity_law_id,
		status, SG_HOST_LAW_FIELD_GRAVITY_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->hook_law_id, observed->hook_law_id, status,
		SG_HOST_LAW_FIELD_HOOK_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU64(expected->mechanism_law_id,
		observed->mechanism_law_id, status,
		SG_HOST_LAW_FIELD_MECHANISM_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareIdentity(&expected->identity, &observed->identity,
		status);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->airaccelerate, observed->airaccelerate,
		status, SG_HOST_LAW_FIELD_AIRACCELERATE,
		SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareFloat(expected->maxvelocity, observed->maxvelocity,
		status, SG_HOST_LAW_FIELD_MAXVELOCITY, SG_HOST_LAW_ELEMENT_NONE);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->movement_flags, observed->movement_flags,
		status, SG_HOST_LAW_FIELD_MOVEMENT_FLAGS);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareU32(expected->physics_flags, observed->physics_flags,
		status, SG_HOST_LAW_FIELD_PHYSICS_FLAGS);
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
	result = CompareU32(expected->action_contract_crc32,
		observed->action_contract_crc32, status,
		SG_HOST_LAW_FIELD_ACTION_CONTRACT);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return CompareU32(expected->mechanism_contract_crc32,
		observed->mechanism_contract_crc32, status,
		SG_HOST_LAW_FIELD_MECHANISM_CONTRACT);
}

static trace_t ProbeTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	return trace;
}

static int ProbePointContents(vec3_t point)
{
	(void)point;
	return 0;
}

static int ProbeHull(sg_host_pmove_function_t pmove, int crouching,
	float gravity, sg_rune_hull_profile_t *hull_out)
{
	pmove_t probe;
	uint32_t axis;

	if (!pmove || !hull_out)
		return 0;
	memset(&probe, 0, sizeof(probe));
	probe.s.pm_type = PM_NORMAL;
	probe.s.pm_flags = crouching ? PMF_DUCKED : 0;
	probe.s.gravity = (short)gravity;
	probe.cmd.msec = 1U;
	probe.trace = ProbeTrace;
	probe.pointcontents = ProbePointContents;
	pmove(&probe);
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(probe.mins[axis]) || !isfinite(probe.maxs[axis]) ||
			probe.mins[axis] >= probe.maxs[axis])
			return 0;
		hull_out->mins.value[axis] = probe.mins[axis];
		hull_out->maxs.value[axis] = probe.maxs[axis];
	}
	return 1;
}

static sg_host_law_result_t CaptureProduction(
	const sg_host_collision_authority_t *authority,
	sg_host_law_view_t *view_out, sg_host_pmove_function_t *pmove_out)
{
	cvar_t *airaccelerate;
	float gravity;
	float maxvelocity;
	sg_rune_hull_profile_t standing;
	sg_rune_hull_profile_t crouching;

	if (!authority || !view_out || !pmove_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	*pmove_out = NULL;
	if (!authority->world || !IdentityValid(&authority->identity))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_PHYSICS_ABI, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!sg_host.pmove || !sg_host.cvar || !sv_gravity || !sv_maxvelocity)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	airaccelerate = sg_host.cvar("sv_airaccelerate", "0", 0);
	if (!airaccelerate)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_AIRACCELERATE, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	gravity = sv_gravity->value;
	maxvelocity = sv_maxvelocity->value;
	if (!isfinite(gravity) ||
		gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
		gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
		gravity != truncf(gravity))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_GRAVITY, SG_HOST_LAW_ELEMENT_NONE, 0U,
			FloatBits(gravity));
	if (!FloatEqual(airaccelerate->value, 0.0f))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_AIRACCELERATE, SG_HOST_LAW_ELEMENT_NONE,
			FloatBits(0.0f), FloatBits(airaccelerate->value));
	if (!isfinite(maxvelocity) ||
		maxvelocity < (float)SG_RUNE_PROOF_MAXVELOCITY_MIN)
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_MAXVELOCITY, SG_HOST_LAW_ELEMENT_NONE,
			FloatBits((float)SG_RUNE_PROOF_MAXVELOCITY_MIN),
			FloatBits(maxvelocity));
	if ((want_funky_gravity && !FloatEqual(want_funky_gravity->value, 0.0f)) ||
		!FloatEqual((float)SG_HOST_SERVER_FRAME_MS / 1000.0f,
			(float)SG_RUNE_PROOF_SERVER_FRAME_MS / 1000.0f))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_PHYSICS_FLAGS, SG_HOST_LAW_ELEMENT_NONE, 0U, 1U);

	memset(view_out, 0, sizeof(*view_out));
	view_out->version = SG_HOST_LAW_PUBLICATION_VERSION;
	view_out->collision_law_id = SG_HOST_LAW_COLLISION_ID;
	view_out->pmove_law_id = SG_HOST_LAW_PMOVE_ID;
	view_out->gravity_law_id = SG_HOST_LAW_GRAVITY_ID;
	view_out->hook_law_id = SG_HOST_LAW_HOOK_ID;
	view_out->mechanism_law_id = SG_HOST_LAW_MECHANISM_ID;
	view_out->identity = authority->identity;
	view_out->identity.physics.gravity = gravity;
	view_out->identity.physics.ground_acceleration =
		SG_HOST_LAW_GROUND_ACCELERATION;
	view_out->identity.physics.air_acceleration =
		SG_HOST_LAW_AIR_ACCELERATION;
	view_out->identity.physics.water_acceleration =
		SG_HOST_LAW_WATER_ACCELERATION;
	view_out->identity.physics.hook_acceleration =
		SG_HOST_LAW_HOOK_ACCELERATION;
	view_out->identity.physics.external_acceleration =
		SG_HOST_LAW_EXTERNAL_ACCELERATION;
	view_out->identity.physics.water_drag = SG_HOST_LAW_WATER_DRAG;
	view_out->identity.physics.max_velocity = maxvelocity;
	view_out->identity.physics.frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	view_out->identity.physics.substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	view_out->airaccelerate = airaccelerate->value;
	view_out->maxvelocity = maxvelocity;
	view_out->movement_flags = 0U;
	view_out->physics_flags = SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED;
	view_out->hook_fire_speed = SG_HOST_HOOK_FIRE_SPEED;
	view_out->hook_pull_speed = SG_HOST_HOOK_PULL_SPEED;
	view_out->hook_initial_damage = SG_HOST_HOOK_INITIAL_DAMAGE;
	view_out->hook_attached_damage = SG_HOST_HOOK_ATTACHED_DAMAGE;
	view_out->hook_health = SG_HOST_HOOK_HEALTH;
	view_out->action_contract_crc32 = SG_RUNE_ACTION_CONTRACT_CRC32;
	view_out->mechanism_contract_crc32 = SG_MECHANISM_CONTRACT_CRC32;
	if (!ProbeHull(sg_host.pmove, 0, gravity, &standing) ||
		!ProbeHull(sg_host.pmove, 1, gravity, &crouching))
		return Result(SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	{
		sg_host_law_result_t result = CompareHull(&sg_host_law_standing_hull,
			&standing, SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_STANDING_HULL_MINS,
			SG_HOST_LAW_FIELD_STANDING_HULL_MAXS);
		if (result.status != SG_HOST_LAW_OK)
			return result;
		result = CompareHull(&sg_host_law_crouching_hull, &crouching,
			SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
			SG_HOST_LAW_FIELD_CROUCHING_HULL_MINS,
			SG_HOST_LAW_FIELD_CROUCHING_HULL_MAXS);
		if (result.status != SG_HOST_LAW_OK)
			return result;
	}
	view_out->identity.standing_hull = standing;
	view_out->identity.crouching_hull = crouching;
	*pmove_out = sg_host.pmove;
	return Ok();
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return CompareIdentity(left, right, SG_HOST_LAW_PRODUCTION_DRIFT).status ==
		SG_HOST_LAW_OK;
}

static int ViewShapeValid(const sg_host_law_view_t *view)
{
	return view && view->version == SG_HOST_LAW_PUBLICATION_VERSION &&
		view->reserved == 0U && view->collision_law_id == SG_HOST_LAW_COLLISION_ID &&
		view->pmove_law_id == SG_HOST_LAW_PMOVE_ID &&
		view->gravity_law_id == SG_HOST_LAW_GRAVITY_ID &&
		view->hook_law_id == SG_HOST_LAW_HOOK_ID &&
		view->mechanism_law_id == SG_HOST_LAW_MECHANISM_ID &&
		IdentityValid(&view->identity) &&
		FloatEqual(view->airaccelerate, 0.0f) &&
		FloatEqual(view->maxvelocity, view->identity.physics.max_velocity) &&
		view->movement_flags == 0U &&
		view->physics_flags == SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED &&
		view->hook_fire_speed == SG_HOST_HOOK_FIRE_SPEED &&
		view->hook_pull_speed == SG_HOST_HOOK_PULL_SPEED &&
		view->hook_initial_damage == SG_HOST_HOOK_INITIAL_DAMAGE &&
		view->hook_attached_damage == SG_HOST_HOOK_ATTACHED_DAMAGE &&
		view->hook_health == SG_HOST_HOOK_HEALTH &&
		view->action_contract_crc32 == SG_RUNE_ACTION_CONTRACT_CRC32 &&
		view->mechanism_contract_crc32 == SG_MECHANISM_CONTRACT_CRC32;
}

static int PublicationValid(const sg_host_law_publication_t *publication)
{
	return publication &&
		publication->state == SG_HOST_LAW_PUBLICATION_STATE &&
		publication->state_inverse == ~SG_HOST_LAW_PUBLICATION_STATE &&
		publication->self == publication && publication->authority.world &&
		publication->pmove && ViewShapeValid(&publication->view) &&
		IdentityEqual(&publication->authority.identity,
			&publication->view.identity);
}

static sg_host_law_result_t ProductionDrift(sg_host_law_field_t field)
{
	return Result(SG_HOST_LAW_PRODUCTION_DRIFT, field,
		SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
}

sg_host_law_result_t SG_HostLawPublicationIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_publication_t **publication_out)
{
	sg_host_law_publication_t *publication;
	sg_host_law_view_t view;
	sg_host_pmove_function_t pmove;
	sg_host_law_result_t result;

	if (!publication_out || *publication_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	result = CaptureProduction(authority, &view, &pmove);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = CompareIdentity(&authority->identity, &view.identity,
		SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	publication = malloc(sizeof(*publication));
	if (!publication)
		return Result(SG_HOST_LAW_ALLOCATION_FAILED,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	publication->state = SG_HOST_LAW_PUBLICATION_STATE;
	publication->state_inverse = ~SG_HOST_LAW_PUBLICATION_STATE;
	publication->self = publication;
	publication->authority = *authority;
	publication->pmove = pmove;
	publication->view = view;
	*publication_out = publication;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationRead(
	const sg_host_law_publication_t *publication,
	sg_host_law_view_t *view_out)
{
	if (!view_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	memset(view_out, 0, sizeof(*view_out));
	if (!PublicationValid(publication))
		return Result(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	*view_out = publication->view;
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationMatch(
	const sg_host_law_publication_t *publication,
	const sg_host_law_view_t *expected)
{
	if (!expected)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (!PublicationValid(publication))
		return Result(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	return CompareViews(expected, &publication->view,
		SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawPublicationRevalidateProduction(
	const sg_host_law_publication_t *publication)
{
	sg_host_law_view_t current;
	sg_host_pmove_function_t pmove;
	sg_host_law_result_t result;

	if (!PublicationValid(publication))
		return Result(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_NONE, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (sg_host.pmove != publication->pmove)
		return ProductionDrift(SG_HOST_LAW_FIELD_PMOVE_LAW);
	result = CaptureProduction(&publication->authority, &current, &pmove);
	if (result.status != SG_HOST_LAW_OK)
	{
		if (result.status == SG_HOST_LAW_HOST_UNAVAILABLE ||
			result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW)
			result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
		return result;
	}
	return CompareViews(&publication->view, &current,
		SG_HOST_LAW_PRODUCTION_DRIFT);
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
		return Result(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	memset(trace_out, 0, sizeof(*trace_out));
	if (!SG_HostCollisionTrace(&publication->authority, scene, start, mins,
		maxs, end, mask, trace_out))
		return Result(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	return Ok();
}

sg_host_law_result_t SG_HostLawPublicationPmove(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;

	if (!request || !result_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return Result(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_PMOVE_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	if (sg_host.pmove != publication->pmove)
		return ProductionDrift(SG_HOST_LAW_FIELD_PMOVE_LAW);
	if (!SG_HostPmoveEvaluateFrame(&publication->authority, scene,
		publication->pmove, request, result_out, &error))
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
	if (!start || !bite || !velocity || !rope_length_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_HOOK_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	if (!PublicationValid(publication))
		return Result(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_HOOK_LAW, SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
	*rope_length_out = CTF_HookPullVelocity(start, bite, velocity);
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
	publication->pmove = NULL;
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
	case SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW:
		return "unsupported production law";
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
	case SG_HOST_LAW_FIELD_EXTERNAL_ACCELERATION:
		return "external acceleration";
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
	case SG_HOST_LAW_FIELD_HOOK_ATTACHED_DAMAGE:
		return "hook attached damage";
	case SG_HOST_LAW_FIELD_HOOK_HEALTH: return "hook health";
	case SG_HOST_LAW_FIELD_ACTION_CONTRACT: return "action contract";
	case SG_HOST_LAW_FIELD_MECHANISM_CONTRACT:
		return "mechanism contract";
	default: return "unknown host-law field";
	}
}
