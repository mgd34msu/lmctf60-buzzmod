/*
 * sg_drop_live.h -- host-free live adapter for ordinary revision-2 RL_DROP.
 *
 * The pure replay reducer owns command, phase and timing law.  The game side
 * supplies authoritative poses, immutable-support truth and the two ordered
 * legacy contact predicates.  There is deliberately no action dispatcher in
 * this interface, so compound suffixes cannot acquire DROP ownership here.
 */
#ifndef SG_DROP_LIVE_H
#define SG_DROP_LIVE_H

#include "sg_replay.h"

#define SG_DROP_LIVE_FRAME_STEPS 4
#define SG_DROP_LIVE_FAILURE_SHELF_SECONDS 10.0f

typedef qboolean (*sg_drop_live_command_fn)(
	const sg_drop_replay_state_t *state, const sg_replay_pose_t *pose,
	usercmd_t *command);
typedef qboolean (*sg_drop_live_contact_fn)(
	const sg_drop_replay_spec_t *spec, const sg_replay_pose_t *pose,
	void *context);

/* Authoritative events raised while the real host consumed one command.
 * The adapter never replays touches or traces: the live host latches them,
 * then supplies exactly one snapshot to the corresponding reducer boundary. */
typedef struct sg_drop_live_events_s
{
	qboolean contaminated;
	qboolean door_passed;
} sg_drop_live_events_t;

qboolean SG_DropLiveEventsLatch(sg_drop_live_events_t *events,
	qboolean contaminated, qboolean door_passed);
qboolean SG_DropLiveEventsBeginCommand(sg_drop_live_events_t *events,
	qboolean *source_door_pending);

typedef enum sg_drop_live_outcome_e
{
	SG_DROP_LIVE_RUNNING = 0,
	SG_DROP_LIVE_ARRIVED,
	SG_DROP_LIVE_FAILED,
	SG_DROP_LIVE_FALLBACK
} sg_drop_live_outcome_t;

typedef enum sg_drop_live_failure_e
{
	SG_DROP_LIVE_FAILURE_NONE = 0,
	SG_DROP_LIVE_FAILURE_OWNER,
	SG_DROP_LIVE_FAILURE_LINK,
	SG_DROP_LIVE_FAILURE_CADENCE,
	SG_DROP_LIVE_FAILURE_BEGIN,
	SG_DROP_LIVE_FAILURE_REDUCER_CONTROL,
	SG_DROP_LIVE_FAILURE_SHADOW_CONTROL,
	SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
	SG_DROP_LIVE_FAILURE_POSTSTEP,
	SG_DROP_LIVE_FAILURE_BOUNDARY
} sg_drop_live_failure_t;

typedef struct sg_drop_live_result_s
{
	sg_drop_live_outcome_t outcome;
	sg_drop_live_failure_t failure;
	sg_replay_reason_t replay_reason;
	qboolean arrival_sampled;
	qboolean arrived;
	qboolean recovery_sampled;
	qboolean recovery_ready;
	qboolean recovery_started;
} sg_drop_live_result_t;

const char *SG_DropLiveFailureName(sg_drop_live_failure_t failure);

void SG_DropLiveReset(sg_drop_replay_state_t *replay, qboolean *active,
	int *replay_link, sg_drop_live_events_t *events);
void SG_DropLiveDeactivate(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link);

void SG_DropLivePose(sg_replay_pose_t *pose, const pmove_state_t *pms,
	const vec3_t origin, const vec3_t velocity, qboolean grounded,
	int watertype, int waterlevel);

sg_drop_live_result_t SG_DropLiveBegin(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const vec3_t destination, const vec3_t lip, byte heading,
	qboolean destination_water, int expected_arrival_ms,
	const sg_replay_pose_t *pose, qboolean ground_support_valid,
	float old_frame_z, const sg_drop_live_events_t *events);

/* The callback independently builds a fully initialized revision-2 shadow
 * command.  Logical usercmd fields are compared explicitly; padding never
 * participates. */
sg_drop_live_result_t SG_DropLivePreStep(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, sg_drop_live_command_fn shadow_command,
	usercmd_t *command);

/* Authenticate the genuinely final command immediately before ClientThink.
 * The expected command is the reducer result already checked against the
 * independent revision-2 shadow; only logical usercmd fields participate. */
sg_drop_live_result_t SG_DropLiveValidateFinalCommand(
	sg_drop_replay_state_t *replay, qboolean *active, int *replay_link,
	int action_link, const usercmd_t *expected, const usercmd_t *command);

/* Consume only a 25/50/75 ms pose.  The fourth command remains pending until
 * SG_DropLiveBoundary runs after the next entity/pusher pass. */
sg_drop_live_result_t SG_DropLivePostStep(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, qboolean ground_support_valid,
	const sg_drop_live_events_t *events);

sg_drop_live_result_t SG_DropLiveBoundary(sg_drop_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, qboolean ground_support_valid,
	const sg_drop_live_events_t *events,
	sg_drop_live_contact_fn arrival, sg_drop_live_contact_fn recovery,
	void *context);

void SG_DropLiveZeroCommand(usercmd_t *command);

#endif /* SG_DROP_LIVE_H */
