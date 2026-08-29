#include "sg_host_engine_pmove.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"

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

int SG_HostEnginePmove(pmove_t *pmove)
{
	if (!pmove || !gi.Pmove)
		return 0;
	gi.Pmove(pmove);
	return 1;
}
