/* Private engine-backed host queries captured from the game import table. */
#ifndef SG_HOST_ENGINE_RUNTIME_H
#define SG_HOST_ENGINE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_world.h"
#include "sg_host_collision.h"
#include "sg_host_pmove.h"
#include "sg_host_hook_law.h"
#include "sg_identity.h"

/* The implementation owns the callback slots.  Callers receive only a
 * borrowed opaque view and can never install a replacement callback. */
typedef struct sg_host_engine_runtime_s sg_host_engine_runtime_t;

typedef enum sg_host_engine_runtime_status_e
{
	SG_HOST_ENGINE_RUNTIME_OK = 0,
	SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT,
	SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE,
	SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE,
	SG_HOST_ENGINE_RUNTIME_NOT_ACCEPTED,
	SG_HOST_ENGINE_RUNTIME_DRIFT,
	SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY,
	SG_HOST_ENGINE_RUNTIME_INVALID_CONTENT_ID,
	SG_HOST_ENGINE_RUNTIME_EVALUATION_FAILED,
	SG_HOST_ENGINE_RUNTIME_ALLOCATION_FAILED
} sg_host_engine_runtime_status_t;

/* Captures the exact engine import callbacks and committed level epoch.  No
 * filesystem lookup is performed and no BSP is reparsed. */
sg_host_engine_runtime_status_t SG_HostEngineRuntimeBegin(
	const char *mapname, sg_host_engine_runtime_t **runtime_out);

int SG_HostEngineRuntimeCurrent(const sg_host_engine_runtime_t *runtime);
int SG_HostEngineRuntimeAccepted(const sg_host_engine_runtime_t *runtime);

/* These are the only runtime collision queries.  They use the captured
 * engine callbacks and the owner-bound subject context, and return
 * zero/failure after lifetime or law drift. */
int SG_HostEngineRuntimeTrace(const sg_host_engine_runtime_t *runtime,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);
/* The same owner-issued trace, with hook target facts classified from the
 * traced live edict.  This is the only runtime hook-collision seam. */
int SG_HostEngineRuntimeHookTrace(const sg_host_engine_runtime_t *runtime,
	const float start[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_hook_collision_t *collision_out);
int SG_HostEngineRuntimePointContents(
	const sg_host_engine_runtime_t *runtime, const float point[3],
	sg_host_collision_contents_t *contents_out);

/* Evaluate one accepted runtime frame through the captured exact Pmove. */
int SG_HostEngineRuntimePmove(const sg_host_engine_runtime_t *runtime,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);

const sg_rune_model_identity_t *SG_HostEngineRuntimeIdentity(
	const sg_host_engine_runtime_t *runtime);
const sg_bsp_content_identity_t *SG_HostEngineRuntimeContentIdentity(
	const sg_host_engine_runtime_t *runtime);
uint64_t SG_HostEngineRuntimeGeneration(
	const sg_host_engine_runtime_t *runtime);
uint64_t SG_HostEngineRuntimeTopologyRevision(
	const sg_host_engine_runtime_t *runtime);
uint64_t SG_HostEngineRuntimeSubjectInstance(
	const sg_host_engine_runtime_t *runtime);

void SG_HostEngineRuntimeDestroy(sg_host_engine_runtime_t *runtime);
const char *SG_HostEngineRuntimeStatusString(
	sg_host_engine_runtime_status_t status);

#endif /* SG_HOST_ENGINE_RUNTIME_H */
