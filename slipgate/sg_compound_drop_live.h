/*
 * sg_compound_drop_live.h -- live law for PREOPEN RL_DOOR_DROP.
 *
 * The controller owns only copied publication data and pure reducer state.
 * The engine adapter provides every world mutation and observation through
 * the callbacks below. Once acquire succeeds there is no ordinary-action
 * handoff: failure enters retained-lease recovery until release succeeds.
 */
#ifndef SG_COMPOUND_DROP_LIVE_H
#define SG_COMPOUND_DROP_LIVE_H

#include <stdint.h>

#include "sg_compound.h"
#include "sg_compound_publication.h"
#include "sg_drop_live.h"

typedef enum sg_compound_drop_live_host_result_e
{
	SG_COMPOUND_DROP_LIVE_HOST_ERROR = -1,
	SG_COMPOUND_DROP_LIVE_HOST_DENIED = 0,
	SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED = 1
} sg_compound_drop_live_host_result_t;

/* The host completely initializes this pointer-free snapshot.  bind is called
 * again before every owned mutation; byte drift in either the publication or
 * the resolved physical identity fails closed into recovery. */
typedef struct sg_compound_drop_live_snapshot_s
{
	sg_compound_publication_binding_t binding;
	int trigger_key;
	int mover_key;
} sg_compound_drop_live_snapshot_t;

typedef struct sg_compound_drop_live_proof_s
{
	int arrival_ms;
	int sweep_clear_ms;
	byte exit_speed;
} sg_compound_drop_live_proof_t;

typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_bind_fn)(void *context, uint32_t link_index,
		sg_compound_drop_live_snapshot_t *snapshot_out);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_source_checkpoint_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot,
		sg_compound_publication_angle_bias_t *bias_out);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_suffix_checkpoint_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot,
		const sg_compound_publication_angle_bias_t *bias);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_snapshot_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_hold_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot, int lease_ms);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_pose_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot,
		const sg_replay_pose_t *pose);
/* ACCEPTED means the complete segment stayed outside the member's swept
 * volume.  DENIED is an observed sweep contact, not a host error. */
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_segment_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot,
		const vec3_t from, const vec3_t to);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_proof_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot,
		 const sg_replay_pose_t *pose, qboolean recovery,
		 sg_compound_drop_live_proof_t *proof_out);
typedef sg_compound_drop_live_host_result_t
	(*sg_compound_drop_live_orphan_fn)(void *context,
		const sg_compound_drop_live_snapshot_t *snapshot, int bolt_key);
typedef qboolean (*sg_compound_drop_live_command_fn)(
	const sg_drop_replay_state_t *state, const sg_replay_pose_t *pose,
	usercmd_t *command);

typedef struct sg_compound_drop_live_host_s
{
	void *context;
	sg_compound_drop_live_bind_fn bind;
	sg_compound_drop_live_source_checkpoint_fn source_checkpoint;
	sg_compound_drop_live_suffix_checkpoint_fn suffix_checkpoint;
	sg_compound_drop_live_snapshot_fn acquire;
	sg_compound_drop_live_snapshot_fn authorize;
	sg_compound_drop_live_snapshot_fn activate;
	sg_compound_drop_live_snapshot_fn at_top;
	sg_compound_drop_live_hold_fn hold_open;
	sg_compound_drop_live_pose_fn outside_sweep;
	sg_compound_drop_live_pose_fn ground_support;
	sg_compound_drop_live_segment_fn sweep_segment_clear;
	sg_compound_drop_live_proof_fn prove_suffix;
	sg_compound_drop_live_snapshot_fn release;
	sg_compound_drop_live_orphan_fn orphan;
	sg_compound_drop_live_command_fn drop_shadow;
} sg_compound_drop_live_host_t;

typedef enum sg_compound_drop_live_outcome_e
{
	SG_COMPOUND_DROP_LIVE_IDLE = 0,
	SG_COMPOUND_DROP_LIVE_WAIT,
	SG_COMPOUND_DROP_LIVE_RUNNING,
	SG_COMPOUND_DROP_LIVE_RECOVERING,
	SG_COMPOUND_DROP_LIVE_SAFE_STOPPED,
	SG_COMPOUND_DROP_LIVE_COMPLETE,
	SG_COMPOUND_DROP_LIVE_REJECTED
} sg_compound_drop_live_outcome_t;

typedef enum sg_compound_drop_live_failure_e
{
	SG_COMPOUND_DROP_LIVE_FAILURE_NONE = 0,
	SG_COMPOUND_DROP_LIVE_FAILURE_ARGUMENT,
	SG_COMPOUND_DROP_LIVE_FAILURE_BINDING,
	SG_COMPOUND_DROP_LIVE_FAILURE_PLAN,
	SG_COMPOUND_DROP_LIVE_FAILURE_SOURCE_CHECKPOINT,
	SG_COMPOUND_DROP_LIVE_FAILURE_ACQUIRE,
	SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY,
	SG_COMPOUND_DROP_LIVE_FAILURE_CADENCE,
	SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH,
	SG_COMPOUND_DROP_LIVE_FAILURE_ACTIVATION,
	SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY,
	SG_COMPOUND_DROP_LIVE_FAILURE_TOP,
	SG_COMPOUND_DROP_LIVE_FAILURE_HOLD,
	SG_COMPOUND_DROP_LIVE_FAILURE_SUFFIX_CHECKPOINT,
	SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF,
	SG_COMPOUND_DROP_LIVE_FAILURE_SWEEP,
	SG_COMPOUND_DROP_LIVE_FAILURE_TIMING,
	SG_COMPOUND_DROP_LIVE_FAILURE_RELEASE,
	SG_COMPOUND_DROP_LIVE_FAILURE_ORPHAN
} sg_compound_drop_live_failure_t;

typedef struct sg_compound_drop_live_result_s
{
	sg_compound_drop_live_outcome_t outcome;
	sg_compound_drop_live_failure_t failure;
	sg_replay_reason_t replay_reason;
	qboolean command_ready;
} sg_compound_drop_live_result_t;

typedef enum sg_compound_drop_live_replay_e
{
	SG_COMPOUND_DROP_LIVE_REPLAY_NONE = 0,
	SG_COMPOUND_DROP_LIVE_REPLAY_APPROACH,
	SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX,
	SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY
} sg_compound_drop_live_replay_t;

typedef struct sg_compound_drop_live_state_s
{
	sg_compound_state_t outer;
	sg_drop_replay_state_t replay;
	sg_drop_live_events_t drop_events;
	sg_compound_drop_live_snapshot_t snapshot;
	sg_compound_publication_angle_bias_t angle_bias;
	sg_compound_drop_live_proof_t proof;
	sg_compound_drop_live_replay_t replay_kind;
	sg_compound_drop_live_failure_t failure;
	sg_replay_reason_t replay_reason;
	vec3_t command_origin;
	vec3_t command_segment_end;
	float zero_frame_old_z;
	int transaction_elapsed_ms;
	int last_boundary_ms;
	int touch_frame_serial;
	int last_sweep_contact_ms;
	int drop_link;
	qboolean guard_owned;
	qboolean drop_active;
	qboolean command_pending;
	qboolean zero_command_pending;
	qboolean aborted_command_pending;
	qboolean command_segment_checked;
	qboolean recovering;
	qboolean sweep_clear;
	qboolean arrived;
} sg_compound_drop_live_state_t;

/* Initialization only.  Do not overwrite or reinitialize this state while
 * guard_owned; local ownership ends by proved release or lifecycle orphan. */
#define SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER { 0 }

const char *SG_CompoundDropLiveFailureName(
	sg_compound_drop_live_failure_t failure);

sg_compound_drop_live_result_t SG_CompoundDropLiveBegin(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose);
sg_compound_drop_live_result_t SG_CompoundDropLivePreStep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, usercmd_t *command);
sg_compound_drop_live_result_t SG_CompoundDropLiveAuthorizeTouch(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose, int frame_serial);
sg_compound_drop_live_result_t SG_CompoundDropLiveAuthorizeActivation(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial);
/* A replay step whose next elapsed time is a 100 ms boundary remains pending
 * here.  Boundary consumes it after the intervening entity/pusher pass. */
sg_compound_drop_live_result_t SG_CompoundDropLivePostStep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);
sg_compound_drop_live_result_t SG_CompoundDropLiveBoundary(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);
sg_compound_drop_live_result_t SG_CompoundDropLiveRecover(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, float old_frame_z);
/* Transfer a live durable claim to the guard's ownerless death lifecycle.
 * A successful transfer ends local command ownership but does not release the
 * mover lease. */
sg_compound_drop_live_result_t SG_CompoundDropLiveOrphan(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int bolt_key);

qboolean SG_CompoundDropLiveOwns(
	const sg_compound_drop_live_state_t *state, uint32_t link_index,
	int mover_key);

#endif /* SG_COMPOUND_DROP_LIVE_H */
