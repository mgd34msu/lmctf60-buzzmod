/* Exact selected-host Pmove evaluation over sg_host_collision authority. */
#ifndef SG_HOST_PMOVE_H
#define SG_HOST_PMOVE_H

#include <stddef.h>
#include <stdint.h>

#include "../q_shared.h"
#include "sg_host_collision.h"

/* Fixed by the accepted Quake II Pmove ABI, not caller configuration. */
#define SG_HOST_PMOVE_STEP_HEIGHT 18.0f

typedef enum sg_host_pmove_error_e
{
	SG_HOST_PMOVE_ERROR_NONE = 0,
	SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT,
	SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE,
	SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING,
	SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY,
	SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH,
	SG_HOST_PMOVE_ERROR_REENTRANT,
	SG_HOST_PMOVE_ERROR_CAPACITY,
	SG_HOST_PMOVE_ERROR_COLLISION
} sg_host_pmove_error_t;

typedef struct sg_host_pmove_request_s
{
	pmove_state_t state;
	/* The host uses this exact comparison to select its initial snap search. */
	pmove_state_t previous_state;
	usercmd_t command;
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
	/* Exact collision-callback chronology executed by the selected host. */
	uint64_t trace_count;
	uint64_t collision_trace_count;
	float gravity;
	uint64_t physics_abi_id;
} sg_host_pmove_result_t;

typedef struct sg_host_pmove_trace_s
{
	uint64_t ordinal;
	uint32_t substep;
	/* Exact network movement state visible when this trace was issued. */
	pmove_state_t state;
	float start[3];
	float mins[3];
	float maxs[3];
	float end[3];
	sg_host_collision_trace_t result;
} sg_host_pmove_trace_t;

typedef struct sg_host_pmove_substep_s
{
	pmove_state_t before_state;
	float before_origin[3];
	float before_velocity[3];
	pmove_state_t state;
	float origin[3];
	float velocity[3];
	sg_rune_stance_t stance;
	int grounded;
	uint32_t support_model_index;
	uint64_t support_instance_id;
	int water_type;
	int water_level;
	uint32_t step;
	uint32_t elapsed_ms;
	/* This substep owns a contiguous interval in the frame trace sequence. */
	uint64_t first_trace_ordinal;
	uint64_t trace_count;
	uint64_t collision_trace_count;
} sg_host_pmove_substep_t;

typedef struct sg_host_pmove_replay_workspace_s
{
	sg_host_pmove_substep_t *substeps;
	size_t substep_capacity;
	sg_host_pmove_trace_t *traces;
	size_t trace_capacity;
} sg_host_pmove_replay_workspace_t;

typedef struct sg_host_pmove_replay_s
{
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	const sg_host_pmove_substep_t *substeps;
	size_t substep_count;
	const sg_host_pmove_trace_t *traces;
	size_t trace_count;
	uint64_t bsp_content_id;
	uint64_t physics_abi_id;
	uint32_t frame_ms;
	uint32_t substep_ms;
} sg_host_pmove_replay_t;

typedef void (*sg_host_pmove_function_t)(pmove_t *pmove);

/* Runs exactly identity.frame_ms using identity.substep_ms host Pmove calls. */
int SG_HostPmoveEvaluateFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);

/* Executes the selected host and records every fixed-time substep. The caller
 * supplies storage, but the host executor writes every replay fact. */
int SG_HostPmoveReplayFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out);

const char *SG_HostPmoveErrorString(sg_host_pmove_error_t error);

#endif
