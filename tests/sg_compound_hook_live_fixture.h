#ifndef SG_COMPOUND_HOOK_LIVE_FIXTURE_H
#define SG_COMPOUND_HOOK_LIVE_FIXTURE_H

#include <stdio.h>

#include "../q_shared.h"
#include "../slipgate/sg_compound_hook_live.h"

extern int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_compound_hook_live_snapshot_t snapshot;
	int acquire_calls;
	int hold_calls;
	int release_calls;
	int orphan_calls;
	int body_clear;
	int bolt_clear;
	int body_clear_calls;
	int bolt_clear_calls;
	int sweep_calls;
	int sweep_cross_call;
	int sweep_inside_call;
	int sweep_invalid_call;
	int sweep_error_call;
	int source_checkpoint;
	int suffix_checkpoint;
	int event_authorized;
	int abort_calls;
	int orphan_had_bolt;
	float source_old_frame_z;
	float suffix_old_frame_z;
	sg_compound_hook_live_bolt_t orphan_bolt;
} fixture_t;

void Setup(fixture_t *fixture, sg_compound_hook_live_host_t *host,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation);
void SetupLateAttach(fixture_t *fixture,
	sg_compound_hook_live_host_t *host, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation);
void SetTouchPose(const fixture_t *fixture, sg_replay_pose_t *pose);
sg_compound_hook_live_result_t Step(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command_out);
void DriveToTop(fixture_t *fixture,
	sg_compound_hook_live_host_t *host,
	sg_compound_hook_live_state_t *state, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation);
sg_compound_hook_live_bolt_t DriveToLinked(fixture_t *fixture,
	sg_compound_hook_live_host_t *host,
	sg_compound_hook_live_state_t *state, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation);
void RunCompoundHookSafetyTests(void);

#endif
