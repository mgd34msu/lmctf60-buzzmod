/*
 * sg_replay.h -- pure pose/controller law for proved RUNE actions.
 *
 * This layer deliberately knows nothing about edicts, traces, Pmove,
 * ClientThink, rune action dispatch, or hook entities.  A generator, loader
 * publication replay, or live executor supplies authoritative poses and the
 * already-resolved world observations after each command.  In return it gets
 * the same literal command stream, phase transitions, terminal decisions, and
 * timing result.
 *
 * In particular, this file does not accept an RL_* action id.  Compound action
 * dispatch cannot be widened by calling a suffix reducer directly; the caller
 * that owns the outer action remains responsible for admission and leases.
 */
#ifndef SG_REPLAY_H
#define SG_REPLAY_H

/* q_shared.h intentionally has no include guard and must be included first by
 * the owning translation unit. */
#include "sg_action_contract.generated.h"

#define SG_REPLAY_STEP_MS               SG_RUNE_PROOF_PMOVE_SUBSTEP_MS
#define SG_REPLAY_FRAME_MS              SG_RUNE_PROOF_SERVER_FRAME_MS
#define SG_REPLAY_TIME_DISCOVER         (-1)

#define SG_REPLAY_ARRIVE_RADIUS         40.0f
#define SG_REPLAY_ARRIVE_Z              72.0f
#define SG_REPLAY_DROP_HANDOFF_RADIUS   8.0f
#define SG_REPLAY_DROP_APPROACH_MS      SG_RUNE_PROOF_DROP_APPROACH_MS
#define SG_REPLAY_DROP_TRAVEL_MS        SG_RUNE_PROOF_DROP_TRAVEL_MS
#define SG_REPLAY_DROP_TOTAL_MS         SG_RUNE_PROOF_DROP_TOTAL_MS
#define SG_REPLAY_DROP_BELOW_Z          512.0f
#define SG_REPLAY_SWIM_LIMIT_MS         3000
#define SG_REPLAY_HOOK_DEST_RADIUS      80.0f
#define SG_REPLAY_HOOK_DEST_Z           96.0f
#define SG_REPLAY_HOOK_RELEASE_ROPE     130
#define SG_REPLAY_HOOK_PULL_LIMIT_MS    3000
#define SG_REPLAY_SWIM_PITCH_LIMIT      85.0f
#define SG_REPLAY_HOOK_FLIGHT_MAX_MS \
	(((SG_RUNE_PROOF_HOOK_MAX_RAY + SG_RUNE_PROOF_HOOK_FRAME_DISTANCE - 1) / \
	  SG_RUNE_PROOF_HOOK_FRAME_DISTANCE) * SG_REPLAY_FRAME_MS)

typedef enum sg_replay_status_e
{
	SG_REPLAY_RUNNING = 0,
	SG_REPLAY_ARRIVED,
	SG_REPLAY_FAILED
} sg_replay_status_t;

typedef enum sg_replay_reason_e
{
	SG_REPLAY_REASON_NONE = 0,
	SG_REPLAY_REASON_INVALID_ARGUMENT,
	SG_REPLAY_REASON_INVALID_STATE,
	SG_REPLAY_REASON_INVALID_CONTROL,
	SG_REPLAY_REASON_NONFINITE_POSE,
	SG_REPLAY_REASON_CONTAMINATED,
	SG_REPLAY_REASON_DOOR_PASSED,
	SG_REPLAY_REASON_HAZARDOUS_LIQUID,
	SG_REPLAY_REASON_DAMAGING_FALL,
	SG_REPLAY_REASON_APPROACH_TIMEOUT,
	SG_REPLAY_REASON_TRAVEL_TIMEOUT,
	SG_REPLAY_REASON_ACTION_TIMEOUT,
	SG_REPLAY_REASON_BELOW_DESTINATION,
	SG_REPLAY_REASON_SHALLOW_WATER_CONTACT,
	SG_REPLAY_REASON_SHORT_LANDING,
	SG_REPLAY_REASON_RECOVERY_LOST,
	SG_REPLAY_REASON_ZERO_TIME_ARRIVAL,
	SG_REPLAY_REASON_TIMING_MISMATCH,
	SG_REPLAY_REASON_HOOK_ATTACH_TIMING,
	SG_REPLAY_REASON_HOOK_EVENT_ORDER,
	SG_REPLAY_REASON_HOOK_RELEASE_BEFORE_PULL,
	SG_REPLAY_REASON_HOOK_RELEASE_MISSED,
	SG_REPLAY_REASON_HOOK_PULL_TIMEOUT,
	SG_REPLAY_REASON_HOOK_SETTLE_TIMEOUT,
	SG_REPLAY_REASON_HOOK_TERMINAL_LOST
} sg_replay_reason_t;

/* Exact state presented to the next 25 ms command.  The integer pmove state
 * is retained alongside decoded floats because command angles consume the
 * authoritative delta angles, while terminal and fall laws consume the
 * decoded pose. */
typedef struct sg_replay_pose_s
{
	pmove_state_t pms;
	vec3_t origin;
	vec3_t velocity;
	qboolean grounded;
	int watertype;
	int waterlevel;
} sg_replay_pose_t;

/* World-dependent answers are data, never hidden calls from the reducer.
 *
 * contact_clear: the caller's chest-height player-solid trace policy for
 *   SWIM/HOOK.
 * ground_support_valid: DROP's caller-specific world/immutable support law.
 * drop_arrival_contact_clear/drop_recovery_contact_clear: the two ordered
 *   DROP chest traces.  They are separate because a failed terminal trace is
 *   followed by the conditional recovery trace at the same 100 ms boundary.
 * drop_recovery_admitted/drop_landing_observed are retained as initialized
 *   adapter diagnostics.  Admission and landing are derived directly
 *   from the serialized destination policy and authoritative pose instead of
 *   permitting either field to select a different controller law.
 * contaminated: a disallowed trigger, solid overlap, or non-world oracle hit.
 * door_passed: a door transition hidden inside an ordinary action.
 * hook_rope_valid/hook_rope_length: the host hook law evaluated at this pose.
 */
typedef struct sg_replay_observation_s
{
	qboolean contact_clear;
	qboolean ground_support_valid;
	qboolean drop_arrival_contact_clear;
	qboolean drop_recovery_contact_clear;
	qboolean drop_recovery_admitted;
	qboolean drop_landing_observed;
	qboolean contaminated;
	qboolean door_passed;
	qboolean hook_rope_valid;
	int hook_rope_length;
} sg_replay_observation_t;

typedef struct sg_replay_progress_s
{
	sg_replay_status_t status;
	sg_replay_reason_t reason;
	int elapsed_ms;
	int arrival_ms;
	byte exit_speed;
	float old_frame_z;
	qboolean step_pending;
	/* SWIM/HOOK preserve their exact trajectory after a door transition and
	 * reject only an otherwise successful terminal. DROP rejects post-command
	 * door passage immediately and does not use this latch. */
	qboolean door_passed_latched;
} sg_replay_progress_t;

typedef struct sg_drop_replay_spec_s
{
	vec3_t destination;
	vec3_t lip;
	byte heading;
	qboolean destination_water;
	/* Approach/recovery yaw uses the generator/proof's selected double-M_PI
	 * byte.  Live DROP independently rebuilds and differentially
	 * checks that same logical command before executing the reducer result. */
	/* -1 records the witnessed arrival.  A nonnegative value additionally
	 * makes a live adapter require that exact production boundary. */
	int expected_arrival_ms;
} sg_drop_replay_spec_t;

typedef struct sg_drop_replay_state_s
{
	sg_drop_replay_spec_t spec;
	sg_replay_progress_t progress;
	qboolean walkoff;
	qboolean airborne;
	qboolean recovery;
	int walkoff_ms;
} sg_drop_replay_state_t;

typedef struct sg_swim_replay_spec_s
{
	vec3_t destination;
	qboolean destination_water;
	int expected_arrival_ms;
} sg_swim_replay_spec_t;

typedef struct sg_swim_replay_state_s
{
	sg_swim_replay_spec_t spec;
	sg_replay_progress_t progress;
} sg_swim_replay_state_t;

typedef enum sg_hook_replay_phase_e
{
	SG_HOOK_REPLAY_FLIGHT = 0,
	SG_HOOK_REPLAY_WAIT_ATTACH,
	SG_HOOK_REPLAY_ATTACH_FRAME,
	SG_HOOK_REPLAY_WAIT_PULL,
	SG_HOOK_REPLAY_PULL_FRAME,
	SG_HOOK_REPLAY_SETTLE
} sg_hook_replay_phase_t;

typedef struct sg_hook_replay_spec_s
{
	vec3_t bite;             /* identity for the host adapter; not ray-traced here */
	vec3_t destination;
	vec3_t view_angles;      /* decoded canonical shorts; pitch +/-89, roll zero */
	int flight_ms;           /* 100..SG_REPLAY_HOOK_FLIGHT_MAX_MS, cadence 100 */
	int settle_limit_ms;     /* current law admits 1000 through 1250 */
	int expected_release_ms;
	int expected_pull_ms;
	int expected_settle_arrival_ms;
	int expected_settle_ms;
} sg_hook_replay_spec_t;

typedef struct sg_hook_replay_state_s
{
	sg_hook_replay_spec_t spec;
	sg_replay_progress_t progress;
	sg_hook_replay_phase_t phase;
	int phase_step;
	int flight_body_ms;
	int pull_ms;
	int release_ms;
	int settle_ms;
	int settle_arrival_ms;
	qboolean release_requested;
	qboolean release_applied;
	qboolean arrived_in_frame;
	pmove_state_t attach_pms;
	qboolean attach_grounded;
	int attach_watertype;
	int attach_waterlevel;
} sg_hook_replay_state_t;

const char *SG_ReplayReasonName(sg_replay_reason_t reason);

/* The serialized DROP yaw byte is shared by reducer, live writer and shadow.
 * atan2f's float result is intentionally promoted for the double-M_PI divide
 * before ANGLE2SHORT; callers must not narrow the intermediate. */
qboolean SG_DropReplayPlanarYawCommand(float dx, float dy,
	short delta_yaw, short *command_yaw);
float SG_ReplayFallDelta(float old_velocity_z, float velocity_z,
	qboolean grounded, int waterlevel);

qboolean SG_DropReplayArrived(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation);
qboolean SG_DropReplayRecoveryReady(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation);
qboolean SG_SwimReplayArrived(const sg_swim_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation);
qboolean SG_HookReplaySettled(const sg_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation);
qboolean SG_HookReplayReleaseReady(const sg_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation);
/* Pure fixed-view command renderer used by the live adapter's immutable
 * WAIT_ATTACH shadow.  It neither reads nor mutates replay state. */
qboolean SG_HookReplayFixedViewCommand(const sg_replay_pose_t *pose,
	const vec3_t view_angles, usercmd_t *command);

sg_replay_status_t SG_DropReplayBegin(sg_drop_replay_state_t *state,
	const sg_drop_replay_spec_t *spec, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);
sg_replay_status_t SG_DropReplayPreStep(sg_drop_replay_state_t *state,
	const sg_replay_pose_t *pose, usercmd_t *command);
sg_replay_status_t SG_DropReplayPostStep(sg_drop_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);

sg_replay_status_t SG_SwimReplayBegin(sg_swim_replay_state_t *state,
	const sg_swim_replay_spec_t *spec, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);
sg_replay_status_t SG_SwimReplayPreStep(sg_swim_replay_state_t *state,
	const sg_replay_pose_t *pose, usercmd_t *command);
sg_replay_status_t SG_SwimReplayPostStep(sg_swim_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);

sg_replay_status_t SG_HookReplayBegin(sg_hook_replay_state_t *state,
	const sg_hook_replay_spec_t *spec, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);
/* Host events are explicit and ordered.  Attached begins the attachment body
 * frame; PullApplied follows each production end-frame pull; ReleaseApplied
 * acknowledges the immediate hook abort requested by a release-ready poststep. */
sg_replay_status_t SG_HookReplayAttached(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose);
sg_replay_status_t SG_HookReplayPullApplied(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose);
sg_replay_status_t SG_HookReplayReleaseApplied(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose);
sg_replay_status_t SG_HookReplayPreStep(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command);
sg_replay_status_t SG_HookReplayPostStep(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation);

#endif /* SG_REPLAY_H */
