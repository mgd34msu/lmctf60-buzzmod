#ifndef SG_COMPOUND_HOOK_LIVE_H
#define SG_COMPOUND_HOOK_LIVE_H

#include <stdint.h>

#include "sg_compound.h"
#include "sg_compound_publication.h"
#include "sg_hook_live.h"

typedef enum sg_compound_hook_live_host_result_e
{
	SG_COMPOUND_HOOK_LIVE_HOST_ERROR = -1,
	SG_COMPOUND_HOOK_LIVE_HOST_DENIED = 0,
	SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED = 1
} sg_compound_hook_live_host_result_t;

typedef struct sg_compound_hook_live_bolt_s
{
	int key;
	uint64_t generation;
} sg_compound_hook_live_bolt_t;

typedef struct sg_compound_hook_live_snapshot_s
{
	sg_compound_publication_binding_t binding;
	int trigger_key;
	int mover_key;
} sg_compound_hook_live_snapshot_t;

typedef enum sg_compound_hook_live_event_e
{
	SG_COMPOUND_HOOK_LIVE_EVENT_NONE = 0,
	SG_COMPOUND_HOOK_LIVE_EVENT_LINKED,
	SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED,
	SG_COMPOUND_HOOK_LIVE_EVENT_PULL,
	SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE
} sg_compound_hook_live_event_t;

typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_bind_fn)(void *context, uint32_t link_index,
		sg_compound_hook_live_snapshot_t *snapshot_out);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_snapshot_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_hold_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot, int lease_ms);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_clear_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot,
		const sg_compound_hook_live_bolt_t *bolt);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_orphan_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot,
		const sg_compound_hook_live_bolt_t *bolt);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_abort_bolt_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot,
		const sg_compound_hook_live_bolt_t *bolt);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_checkpoint_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot,
		const sg_replay_pose_t *pose,
		const sg_replay_observation_t *observation);
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_event_auth_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot,
		sg_compound_hook_live_event_t event,
		const sg_compound_hook_live_bolt_t *bolt);
typedef struct sg_compound_hook_live_sweep_s
{
	qboolean start_outside;
	qboolean end_outside;
	qboolean crossed;
} sg_compound_hook_live_sweep_t;
typedef sg_compound_hook_live_host_result_t
	(*sg_compound_hook_live_sweep_fn)(void *context,
		const sg_compound_hook_live_snapshot_t *snapshot,
		const vec3_t start, const vec3_t end,
		sg_compound_hook_live_sweep_t *sweep_out);

typedef struct sg_compound_hook_live_host_s
{
	void *context;
	sg_compound_hook_live_bind_fn bind;
	sg_compound_hook_live_snapshot_fn acquire;
	sg_compound_hook_live_snapshot_fn authorize;
	sg_compound_hook_live_hold_fn hold_open;
	sg_compound_hook_live_clear_fn body_clear;
	sg_compound_hook_live_clear_fn bolt_clear;
	sg_compound_hook_live_snapshot_fn release;
	sg_compound_hook_live_orphan_fn orphan;
	sg_compound_hook_live_abort_bolt_fn abort_bolt;
	sg_compound_hook_live_checkpoint_fn source_checkpoint;
	sg_compound_hook_live_checkpoint_fn suffix_checkpoint;
	sg_compound_hook_live_event_auth_fn event_authorize;
	sg_compound_hook_live_sweep_fn sweep_segment;
	sg_hook_live_command_fn hook_shadow;
} sg_compound_hook_live_host_t;

typedef enum sg_compound_hook_live_outcome_e
{
	SG_COMPOUND_HOOK_LIVE_IDLE = 0,
	SG_COMPOUND_HOOK_LIVE_WAIT,
	SG_COMPOUND_HOOK_LIVE_RUNNING,
	SG_COMPOUND_HOOK_LIVE_RECOVERING,
	SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED,
	SG_COMPOUND_HOOK_LIVE_COMPLETE,
	SG_COMPOUND_HOOK_LIVE_REJECTED
} sg_compound_hook_live_outcome_t;

typedef enum sg_compound_hook_live_failure_e
{
	SG_COMPOUND_HOOK_LIVE_FAILURE_NONE = 0,
	SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT,
	SG_COMPOUND_HOOK_LIVE_FAILURE_BINDING,
	SG_COMPOUND_HOOK_LIVE_FAILURE_PLAN,
	SG_COMPOUND_HOOK_LIVE_FAILURE_SOURCE_CHECKPOINT,
	SG_COMPOUND_HOOK_LIVE_FAILURE_SUFFIX_CHECKPOINT,
	SG_COMPOUND_HOOK_LIVE_FAILURE_ACQUIRE,
	SG_COMPOUND_HOOK_LIVE_FAILURE_AUTHORITY,
	SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE,
	SG_COMPOUND_HOOK_LIVE_FAILURE_TOUCH,
	SG_COMPOUND_HOOK_LIVE_FAILURE_ACTIVATION,
	SG_COMPOUND_HOOK_LIVE_FAILURE_TOP,
	SG_COMPOUND_HOOK_LIVE_FAILURE_LINK,
	SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY,
	SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY,
	SG_COMPOUND_HOOK_LIVE_FAILURE_HOLD,
	SG_COMPOUND_HOOK_LIVE_FAILURE_SWEEP,
	SG_COMPOUND_HOOK_LIVE_FAILURE_RELEASE,
	SG_COMPOUND_HOOK_LIVE_FAILURE_ORPHAN
} sg_compound_hook_live_failure_t;

typedef enum sg_compound_hook_live_control_e
{
	SG_COMPOUND_HOOK_LIVE_CONTROL_NONE = 0,
	SG_COMPOUND_HOOK_LIVE_CONTROL_APPROACH,
	SG_COMPOUND_HOOK_LIVE_CONTROL_OPENING,
	SG_COMPOUND_HOOK_LIVE_CONTROL_SUFFIX,
	SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY,
	SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING
} sg_compound_hook_live_control_t;

typedef struct sg_compound_hook_live_result_s
{
	sg_compound_hook_live_outcome_t outcome;
	sg_compound_hook_live_failure_t failure;
	sg_replay_reason_t replay_reason;
	qboolean command_ready;
} sg_compound_hook_live_result_t;

typedef struct sg_compound_hook_live_state_s
{
	sg_compound_state_t outer;
	sg_swim_replay_state_t swim;
	sg_hook_replay_state_t hook;
	sg_replay_progress_t opening_safety;
	sg_hook_live_command_guard_t hook_command_guard;
	sg_compound_hook_live_snapshot_t snapshot;
	sg_hook_replay_spec_t hook_spec;
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_control_t control;
	sg_compound_hook_live_failure_t failure;
	sg_replay_reason_t replay_reason;
	usercmd_t expected_command;
	vec3_t command_origin;
	int transaction_elapsed_ms;
	int last_boundary_ms;
	int touch_frame_serial;
	int pull_frame_serial;
	int last_event_frame_serial;
	int swim_link;
	int hook_link;
	int sweep_outside_since_ms;
	sg_compound_hook_live_event_t last_event;
	qboolean guard_owned;
	qboolean local_owned;
	qboolean swim_active;
	qboolean hook_active;
	qboolean bolt_linked;
	qboolean bolt_abort_applied;
	qboolean hook_released;
	qboolean command_pending;
	qboolean command_approved;
	qboolean command_replay_consumed;
	qboolean aborted_command_pending;
	qboolean recovering;
	qboolean sweep_clear;
	qboolean arrived;
	qboolean command_origin_valid;
	qboolean command_segment_checked;
	qboolean segment_clear_ready;
	qboolean recovery_sweep_dirty;
} sg_compound_hook_live_state_t;

#define SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER { 0 }

sg_compound_hook_live_result_t SG_CompoundHookLiveBegin(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);
sg_compound_hook_live_result_t SG_CompoundHookLivePreStep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command);
sg_compound_hook_live_result_t SG_CompoundHookLiveApproveCommand(
	sg_compound_hook_live_state_t *state, const usercmd_t *command);
sg_compound_hook_live_result_t SG_CompoundHookLivePostStep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);
sg_compound_hook_live_result_t SG_CompoundHookLiveBoundary(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);
sg_compound_hook_live_result_t SG_CompoundHookLiveTouch(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, int frame_serial);
sg_compound_hook_live_result_t SG_CompoundHookLiveActivate(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial);
sg_compound_hook_live_result_t SG_CompoundHookLiveLinked(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);
sg_compound_hook_live_result_t SG_CompoundHookLiveAttached(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose);
sg_compound_hook_live_result_t SG_CompoundHookLivePullApplied(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose);
sg_compound_hook_live_result_t SG_CompoundHookLiveReleaseApplied(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose);
sg_compound_hook_live_result_t SG_CompoundHookLiveRecover(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);
sg_compound_hook_live_result_t SG_CompoundHookLiveOrphan(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);

qboolean SG_CompoundHookLiveOwns(
	const sg_compound_hook_live_state_t *state, uint32_t link_index,
	int mover_key);

#endif
