/* Engine-owned Pmove entry point and its ABI description. */
#ifndef SG_HOST_ENGINE_PMOVE_H
#define SG_HOST_ENGINE_PMOVE_H

#include <stdint.h>

#include "sg_host_pmove.h"

#define SG_HOST_ENGINE_PMOVE_ABI_VERSION UINT32_C(1)
#define SG_HOST_ENGINE_PMOVE_FRACTION_BITS UINT32_C(3)
#define SG_HOST_ENGINE_PMOVE_SUBSTEP_MS UINT32_C(25)
#define SG_HOST_ENGINE_FRAME_MS UINT32_C(100)
#define SG_HOST_ENGINE_GRAVITY_MIN UINT32_C(1)
#define SG_HOST_ENGINE_GRAVITY_MAX UINT32_C(32767)
#define SG_HOST_ENGINE_PHYSICS_FLAGS UINT32_C(0)
#define SG_HOST_ENGINE_PMOVE_ABI_ID UINT64_C(0x51494d504f564531)

/* These are the non-cvar terms of the selected engine Pmove contract.  They
 * live beside the ABI binding so every publication backend obtains the same
 * law from one authoritative host contract rather than duplicating values. */
#define SG_HOST_ENGINE_GROUND_ACCELERATION 10.0f
#define SG_HOST_ENGINE_AIR_ACCELERATION 1.0f
#define SG_HOST_ENGINE_WATER_ACCELERATION 10.0f
#define SG_HOST_ENGINE_HOOK_ACCELERATION 800.0f
#define SG_HOST_ENGINE_EXTERNAL_ACCELERATION 1.0f
#define SG_HOST_ENGINE_WATER_DRAG 1.0f

typedef struct sg_host_engine_pmove_abi_s
{
	uint32_t version;
	uint32_t game_api_version;
	uint32_t import_size;
	uint32_t pmove_offset;
	uint32_t pmove_size;
	uint32_t state_size;
	uint32_t command_size;
	uint32_t fraction_bits;
	uint32_t substep_ms;
	uint64_t identity;
} sg_host_engine_pmove_abi_t;

/* A publication captures the exact callback and the import table that owns
 * it.  ABI constants describe the shape only; they are not authority for
 * executing a mutable import slot. */
typedef struct sg_host_engine_pmove_binding_s
{
	sg_host_pmove_function_t entry;
	const void *owner;
} sg_host_engine_pmove_binding_t;

/* Returns zero unless the game import contains the engine Pmove slot. */
int SG_HostEnginePmoveABI(sg_host_engine_pmove_abi_t *abi_out);

int SG_HostEnginePmoveBindingCapture(
	sg_host_engine_pmove_binding_t *binding_out);
int SG_HostEnginePmoveBindingCurrent(
	const sg_host_engine_pmove_binding_t *binding);
int SG_HostEnginePmoveBound(const sg_host_engine_pmove_binding_t *binding,
	pmove_t *pmove);

int SG_HostEnginePhysicsLaw(sg_rune_physics_parameters_t *law_out);
/* Player hulls are part of the selected engine movement contract. */
int SG_HostEngineHullProfiles(sg_rune_hull_profile_t *standing_out,
	sg_rune_hull_profile_t *crouching_out);

/* Calls the engine import directly; no caller-supplied function is accepted. */
int SG_HostEnginePmove(pmove_t *pmove);

#endif /* SG_HOST_ENGINE_PMOVE_H */
