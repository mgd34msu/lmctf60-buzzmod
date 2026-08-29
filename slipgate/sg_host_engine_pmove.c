#include "../g_local.h"
#undef world

#include "sg_host_engine_pmove.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

extern game_import_t gi;

_Static_assert(sizeof(short) == 2U, "Pmove fixed point ABI requires 16-bit shorts");
_Static_assert(offsetof(game_import_t, Pmove) <= UINT32_MAX,
	"Pmove import offset must fit the published ABI");

int SG_HostEnginePmoveABI(sg_host_engine_pmove_abi_t *abi_out)
{
	if (!abi_out)
		return 0;
	memset(abi_out, 0, sizeof(*abi_out));
	abi_out->version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	abi_out->game_api_version = GAME_API_VERSION;
	abi_out->import_size = (uint32_t)sizeof(game_import_t);
	abi_out->pmove_offset = (uint32_t)offsetof(game_import_t, Pmove);
	abi_out->pmove_size = (uint32_t)sizeof(pmove_t);
	abi_out->state_size = (uint32_t)sizeof(pmove_state_t);
	abi_out->command_size = (uint32_t)sizeof(usercmd_t);
	abi_out->fraction_bits = SG_HOST_ENGINE_PMOVE_FRACTION_BITS;
	abi_out->substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	abi_out->identity = SG_HOST_ENGINE_PMOVE_ABI_ID;
	return gi.Pmove != NULL;
}

int SG_HostEnginePmoveBindingCapture(
	sg_host_engine_pmove_binding_t *binding_out)
{
	if (!binding_out)
		return 0;
	memset(binding_out, 0, sizeof(*binding_out));
	if (!gi.Pmove)
		return 0;
	binding_out->entry = gi.Pmove;
	binding_out->owner = (const void *)&gi;
	return 1;
}

int SG_HostEnginePmoveBindingCurrent(
	const sg_host_engine_pmove_binding_t *binding)
{
	return binding && binding->entry && binding->owner == (const void *)&gi &&
		gi.Pmove == binding->entry;
}

int SG_HostEnginePmoveBound(const sg_host_engine_pmove_binding_t *binding,
	pmove_t *pmove)
{
	if (!pmove || !SG_HostEnginePmoveBindingCurrent(binding))
		return 0;
	binding->entry(pmove);
	return 1;
}

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

int SG_HostEngineHullProfiles(sg_rune_hull_profile_t *standing_out,
	sg_rune_hull_profile_t *crouching_out)
{
	if (!standing_out || !crouching_out)
		return 0;
	memset(standing_out, 0, sizeof(*standing_out));
	memset(crouching_out, 0, sizeof(*crouching_out));
	standing_out->mins.value[0] = -16.0f;
	standing_out->mins.value[1] = -16.0f;
	standing_out->mins.value[2] = -24.0f;
	standing_out->maxs.value[0] = 16.0f;
	standing_out->maxs.value[1] = 16.0f;
	standing_out->maxs.value[2] = 32.0f;
	crouching_out->mins.value[0] = -16.0f;
	crouching_out->mins.value[1] = -16.0f;
	crouching_out->mins.value[2] = -24.0f;
	crouching_out->maxs.value[0] = 16.0f;
	crouching_out->maxs.value[1] = 16.0f;
	crouching_out->maxs.value[2] = 4.0f;
	return 1;
}

int SG_HostEnginePmove(pmove_t *pmove)
{
	if (!pmove || !gi.Pmove)
		return 0;
	gi.Pmove(pmove);
	return 1;
}
