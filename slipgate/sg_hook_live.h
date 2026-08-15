/* sg_hook_live.h -- host-free live adapter for ordinary revision-2 RL_HOOK.
 *
 * This interface deliberately contains no edicts, trace calls, ClientThink,
 * or hook-entity mutation.  The integration layer supplies fresh ownership
 * identity, poses and observations, and calls the three production events in
 * their actual order.  The frozen replay reducer remains the sole owner of
 * command, phase, timing, and terminal law.
 */
#ifndef SG_HOOK_LIVE_H
#define SG_HOOK_LIVE_H

#include "sg_replay.h"

typedef qboolean (*sg_hook_live_command_fn)(
	const sg_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command);

typedef enum sg_hook_live_outcome_e
{
	SG_HOOK_LIVE_RUNNING = 0,
	SG_HOOK_LIVE_ARRIVED,
	SG_HOOK_LIVE_FAILED,
	SG_HOOK_LIVE_FALLBACK
} sg_hook_live_outcome_t;

typedef enum sg_hook_live_failure_e
{
	SG_HOOK_LIVE_FAILURE_NONE = 0,
	SG_HOOK_LIVE_FAILURE_OWNER,
	SG_HOOK_LIVE_FAILURE_LINK,
	SG_HOOK_LIVE_FAILURE_IDENTITY,
	SG_HOOK_LIVE_FAILURE_BEGIN,
	SG_HOOK_LIVE_FAILURE_REDUCER_CONTROL,
	SG_HOOK_LIVE_FAILURE_LEGACY_CONTROL,
	SG_HOOK_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
	SG_HOOK_LIVE_FAILURE_POSTSTEP,
	SG_HOOK_LIVE_FAILURE_ATTACH,
	SG_HOOK_LIVE_FAILURE_PULL,
	SG_HOOK_LIVE_FAILURE_RELEASE,
	SG_HOOK_LIVE_FAILURE_FINAL_COMMAND
} sg_hook_live_failure_t;

typedef struct sg_hook_live_result_s
{
	sg_hook_live_outcome_t outcome;
	sg_hook_live_failure_t failure;
	sg_replay_reason_t replay_reason;
} sg_hook_live_result_t;

/* The adapter captures its approved logical usercmd here before any host
 * code can run another command writer. The link stamp makes a stale guard
 * fail closed even if a host reset path is accidentally missed. */
typedef struct sg_hook_live_command_guard_s
{
	usercmd_t expected;
	int action_link;
	qboolean pending;
} sg_hook_live_command_guard_t;

const char *SG_HookLiveFailureName(sg_hook_live_failure_t failure);

void SG_HookLiveReset(sg_hook_replay_state_t *replay, qboolean *active,
	int *replay_link, sg_hook_live_command_guard_t *guard);
void SG_HookLiveDeactivate(sg_hook_replay_state_t *replay, qboolean *active,
	int *replay_link);
void SG_HookLiveCommandGuardClear(sg_hook_live_command_guard_t *guard);
/* Frozen settlement fill: all logical fields zero, msec fixed to 25. */
void SG_HookLiveZeroCommand(usercmd_t *command);

/* identity_current is the integration layer's exact link-and-hook-entity
 * identity check.  The adapter never guesses identity from a position. */
sg_hook_live_result_t SG_HookLiveBegin(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation,
	float old_frame_z, sg_hook_live_command_guard_t *guard);

/* legacy_command receives the immutable pre-reducer state and observation and
 * must independently reconstruct the legacy logical usercmd.
 * During an arrived settlement substep it must produce the frozen literal
 * zero-fill: every logical field is zero except msec, which remains 25. */
sg_hook_live_result_t SG_HookLivePreStep(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation,
	sg_hook_live_command_fn legacy_command, usercmd_t *command,
	sg_hook_live_command_guard_t *guard);

/* The legacy engine can leave a bolt in flight for one or more whole frames
 * after the predicted attachment boundary.  While the host still observes
 * that exact outbound bolt, this performs one independently-differentialled
 * fixed-view 25 ms command but deliberately leaves the reducer parked at
 * WAIT_ATTACH.  The eventual Attached event therefore remains the only
 * acknowledgement that advances the attachment phase. */
sg_hook_live_result_t SG_HookLiveWaitAttachStep(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation,
	sg_hook_live_command_fn legacy_command, usercmd_t *command,
	sg_hook_live_command_guard_t *guard);

sg_hook_live_result_t SG_HookLiveValidateFinalCommand(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current, const usercmd_t *expected,
	const usercmd_t *command);

/* Consume the adapter-owned approval exactly once at the final live command
 * boundary. A missing, stale, or changed command deactivates the owner. */
sg_hook_live_result_t SG_HookLiveValidateStoredFinalCommand(
	sg_hook_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, qboolean identity_current,
	sg_hook_live_command_guard_t *guard, const usercmd_t *command);

sg_hook_live_result_t SG_HookLivePostStep(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);

/* Call each event immediately after its corresponding legacy host mutation:
 * attach after hook attachment, pull after end-frame pull, and release after
 * hook abort plus its authoritative zero-velocity pose update. */
sg_hook_live_result_t SG_HookLiveAttached(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose);
sg_hook_live_result_t SG_HookLivePullApplied(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose);
sg_hook_live_result_t SG_HookLiveReleaseApplied(sg_hook_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	qboolean identity_current, const sg_replay_pose_t *pose);

#endif /* SG_HOOK_LIVE_H */
