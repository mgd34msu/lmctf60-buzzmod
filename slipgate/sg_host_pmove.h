/* Exact selected-host Pmove evaluation over sg_host_collision authority. */
#ifndef SG_HOST_PMOVE_H
#define SG_HOST_PMOVE_H

/* q_shared.h predates include guards.  Reuse an already included copy when
 * this interface is included by a game module after g_local.h. */
#ifndef CVAR
#include "../q_shared.h"
#endif
#include "sg_host_collision.h"

struct sg_host_engine_pmove_binding_s;

/* The only dynamic gravity override accepted by the published evaluator is
 * the live human hook state.  It is deliberately identity-bound rather than
 * a caller-provided "gravity = 0" switch. */
#define SG_HOST_PMOVE_HOOK_LAW_ID UINT64_C(0x484f4f4b4c573031)
#define SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO 50U

typedef enum sg_host_pmove_error_e
{
	SG_HOST_PMOVE_ERROR_NONE = 0,
	SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT,
	SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE,
	SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING,
	SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY,
	SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH,
	SG_HOST_PMOVE_ERROR_REENTRANT,
	SG_HOST_PMOVE_ERROR_COLLISION
} sg_host_pmove_error_t;

typedef struct sg_host_pmove_request_s
{
	pmove_state_t state;
	/* The host uses this exact comparison to select its initial snap search. */
	pmove_state_t previous_state;
	usercmd_t command;
	/* These fields mirror ClientThink's authoritative hook branch.  All zero
	 * means ordinary map gravity; nonzero values must name the published hook
	 * law and its attached state. */
	uint64_t hook_law_id;
	uint32_t hook_attached;
	uint32_t hook_length;
} sg_host_pmove_request_t;

typedef struct sg_host_pmove_result_s
{
	pmove_state_t state;
	float origin[3];
	float velocity[3];
	float mins[3];
	float maxs[3];
	float view_height;
	float view_angles[3];
	int grounded;
	uint32_t support_model_index;
	uint64_t support_instance_id;
	int water_type;
	int water_level;
	uint32_t evaluated_steps;
	uint32_t elapsed_ms;
	float gravity;
	uint64_t physics_abi_id;
	uint64_t gravity_law_id;
} sg_host_pmove_result_t;

typedef void (*sg_host_pmove_function_t)(pmove_t *pmove);

/* Runs exactly identity.frame_ms using identity.substep_ms host Pmove calls. */
int SG_HostPmoveEvaluateFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);

/* Same evaluator, bound to the engine-owned gi.Pmove adapter. */
int SG_HostPmoveEvaluateEngineFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);

/* Same operation using the callback captured by a publication. */
int SG_HostPmoveEvaluateBoundEngineFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const struct sg_host_engine_pmove_binding_s *binding,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);

const char *SG_HostPmoveErrorString(sg_host_pmove_error_t error);

#endif
