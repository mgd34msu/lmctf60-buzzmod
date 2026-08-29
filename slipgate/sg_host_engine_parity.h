/* Behavioral checks for the accepted engine movement ABI. */
#ifndef SG_HOST_ENGINE_PARITY_H
#define SG_HOST_ENGINE_PARITY_H

#include <stdint.h>

typedef enum sg_host_engine_parity_case_e
{
	SG_HOST_ENGINE_PARITY_ACCELERATION = 1U << 0,
	SG_HOST_ENGINE_PARITY_GRAVITY = 1U << 1,
	SG_HOST_ENGINE_PARITY_COLLISION = 1U << 2,
	SG_HOST_ENGINE_PARITY_WATER = 1U << 3,
	SG_HOST_ENGINE_PARITY_STANCE = 1U << 4,
	SG_HOST_ENGINE_PARITY_TIMING = 1U << 5
} sg_host_engine_parity_case_t;

#define SG_HOST_ENGINE_PARITY_ALL UINT32_C(0x3f)

typedef struct sg_host_engine_parity_result_s
{
	uint64_t fingerprint;
	uint32_t cases;
	uint32_t engine_calls;
	uint32_t trace_calls;
	uint32_t contents_calls;
} sg_host_engine_parity_result_t;

/* Runs all six fixed movement behaviors through gi.Pmove. */
int SG_HostEnginePmoveParity(sg_host_engine_parity_result_t *result_out);

#endif /* SG_HOST_ENGINE_PARITY_H */
