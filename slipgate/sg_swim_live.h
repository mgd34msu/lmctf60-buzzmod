/*
 * sg_swim_live.h -- behavior-neutral live adapter for ordinary RL_SWIM.
 *
 * The replay reducer is host-free.  This adapter keeps that property: callers
 * supply the authoritative live pose and the two legacy predicates used for
 * command and terminal differential checks.  It deliberately has no action
 * dispatcher and therefore cannot authorize a compound SWIM suffix.
 */
#ifndef SG_SWIM_LIVE_H
#define SG_SWIM_LIVE_H

#include "sg_replay.h"

#define SG_SWIM_LIVE_FRAME_STEPS 4
#define SG_SWIM_LIVE_EARLY_HAZARD_SHELF_SECONDS 60.0f
#define SG_SWIM_LIVE_TIMING_SHELF_SECONDS       10.0f

typedef qboolean (*sg_swim_live_command_fn)(const vec3_t origin,
	const vec3_t destination, const pmove_state_t *pms, usercmd_t *command);
typedef qboolean (*sg_swim_live_arrival_fn)(
	const sg_swim_replay_spec_t *spec, const sg_replay_pose_t *pose,
	void *context);

typedef enum sg_swim_live_outcome_e
{
	SG_SWIM_LIVE_RUNNING = 0,
	SG_SWIM_LIVE_ARRIVED,
	SG_SWIM_LIVE_FALLBACK
} sg_swim_live_outcome_t;

typedef enum sg_swim_live_failure_e
{
	SG_SWIM_LIVE_FAILURE_NONE = 0,
	SG_SWIM_LIVE_FAILURE_OWNER,
	SG_SWIM_LIVE_FAILURE_LINK,
	SG_SWIM_LIVE_FAILURE_CADENCE,
	SG_SWIM_LIVE_FAILURE_BEGIN,
	SG_SWIM_LIVE_FAILURE_REDUCER_CONTROL,
	SG_SWIM_LIVE_FAILURE_LEGACY_CONTROL,
	SG_SWIM_LIVE_FAILURE_COMMAND_DIFFERENTIAL,
	SG_SWIM_LIVE_FAILURE_POSTSTEP,
	SG_SWIM_LIVE_FAILURE_HAZARDOUS_LIQUID,
	SG_SWIM_LIVE_FAILURE_BOUNDARY
} sg_swim_live_failure_t;

typedef struct sg_swim_live_result_s
{
	sg_swim_live_outcome_t outcome;
	sg_swim_live_failure_t failure;
	sg_replay_reason_t replay_reason;
	qboolean arrival_sampled;
	qboolean legacy_arrived;
} sg_swim_live_result_t;

const char *SG_SwimLiveFailureName(sg_swim_live_failure_t failure);

/* The reset owns only action state.  swim_air_seed is intentionally absent:
 * breath escape belongs to the life/dry/authority policies that already own
 * it, not to an individual SWIM attempt. */
void SG_SwimLiveReset(sg_swim_replay_state_t *replay, qboolean *active,
	int *replay_link, qboolean *validated, int *proved_ms, int *elapsed_ms);
void SG_SwimLiveDeactivate(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link);

void SG_SwimLivePose(sg_replay_pose_t *pose, const pmove_state_t *pms,
	const vec3_t origin, const vec3_t velocity, qboolean grounded,
	int watertype, int waterlevel);

sg_swim_live_result_t SG_SwimLiveBegin(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const vec3_t destination, qboolean destination_water,
	int expected_arrival_ms, const sg_replay_pose_t *pose,
	float old_frame_z);

/* PreStep builds both commands.  On a differential failure `command` receives
 * the fully initialized legacy byte stream so the current action continues. */
sg_swim_live_result_t SG_SwimLivePreStep(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose, const vec3_t legacy_destination,
	sg_swim_live_command_fn legacy_command, usercmd_t *command);

/* Consume only a 25/50/75 ms pose.  The 100 ms pose is deliberately deferred
 * to SG_SwimLiveBoundary after the next entity/pusher pass. */
sg_swim_live_result_t SG_SwimLivePostStep(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	const sg_replay_pose_t *pose);

sg_swim_live_result_t SG_SwimLiveBoundary(sg_swim_replay_state_t *replay,
	qboolean *active, int *replay_link, int action_link,
	int live_elapsed_ms, const sg_replay_pose_t *pose,
	sg_swim_live_arrival_fn legacy_arrival, void *context);

void SG_SwimLiveZeroFrame(usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS]);

#endif /* SG_SWIM_LIVE_H */
