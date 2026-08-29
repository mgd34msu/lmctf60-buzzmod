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
#define SG_HOST_ENGINE_MAXVELOCITY_MIN UINT32_C(800)
#define SG_HOST_ENGINE_PHYSICS_FLAGS UINT32_C(0)
#define SG_HOST_ENGINE_PMOVE_ABI_ID UINT64_C(0x51494d504f564531)

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

/* Returns zero unless the game import contains the engine Pmove slot. */
int SG_HostEnginePmoveABI(sg_host_engine_pmove_abi_t *abi_out);

/* Calls the engine import directly; no caller-supplied function is accepted. */
int SG_HostEnginePmove(pmove_t *pmove);

#endif /* SG_HOST_ENGINE_PMOVE_H */
