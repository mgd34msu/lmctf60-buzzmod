/* Direct, host-free regression tests for the pure RUNE replay law. */
#include "q_shared.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_replay.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_STATUS(actual, expected) do { \
	sg_replay_status_t check_actual_ = (actual); \
	if (check_actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: status %d, expected %d: %s\n", \
		        __FILE__, __LINE__, (int)check_actual_, (int)(expected), \
		        #actual); \
		failures++; \
	} \
} while (0)

static sg_replay_pose_t TestPose(float x, float y, float z)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	VectorSet(pose.origin, x, y, z);
	pose.grounded = true;
	return pose;
}

static sg_replay_observation_t TestObservation(void)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.contact_clear = true;
	observation.ground_support_valid = true;
	observation.drop_arrival_contact_clear = true;
	observation.drop_recovery_contact_clear = true;
	observation.drop_recovery_admitted = true;
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 1000;
	return observation;
}

static sg_drop_replay_spec_t TestDropSpec(void)
{
	sg_drop_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	VectorSet(spec.destination, 1000.0f, 0.0f, 0.0f);
	VectorSet(spec.lip, 100.0f, 0.0f, 0.0f);
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	return spec;
}

static sg_swim_replay_spec_t TestSwimSpec(void)
{
	sg_swim_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	VectorSet(spec.destination, 1000.0f, 0.0f, 0.0f);
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	return spec;
}

static sg_hook_replay_spec_t TestHookSpec(void)
{
	sg_hook_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	VectorSet(spec.bite, 0.0f, 0.0f, 512.0f);
	VectorSet(spec.destination, 0.0f, 0.0f, 0.0f);
	VectorSet(spec.view_angles,
	          SHORT2ANGLE((short)ANGLE2SHORT(10.0f)), 90.0f, 0.0f);
	spec.flight_ms = 100;
	spec.settle_limit_ms = SG_RUNE_PROOF_HOOK_DRY_SETTLE_MS;
	spec.expected_release_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_pull_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_ms = SG_REPLAY_TIME_DISCOVER;
	return spec;
}

static sg_replay_status_t DropStep(sg_drop_replay_state_t *state,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation,
	usercmd_t *command)
{
	sg_replay_status_t status;

	status = SG_DropReplayPreStep(state, pose, command);
	if (status != SG_REPLAY_RUNNING)
		return status;
	return SG_DropReplayPostStep(state, pose, observation);
}

static sg_replay_status_t SwimStep(sg_swim_replay_state_t *state,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation,
	usercmd_t *command)
{
	sg_replay_status_t status;

	status = SG_SwimReplayPreStep(state, pose, command);
	if (status != SG_REPLAY_RUNNING)
		return status;
	return SG_SwimReplayPostStep(state, pose, observation);
}

static sg_replay_status_t HookStep(sg_hook_replay_state_t *state,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation,
	usercmd_t *command)
{
	sg_replay_status_t status;

	status = SG_HookReplayPreStep(state, pose, observation, command);
	if (status != SG_REPLAY_RUNNING)
		return status;
	return SG_HookReplayPostStep(state, pose, observation);
}

static void TestFallAndTerminalBoundaries(void)
{
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	sg_drop_replay_spec_t drop = TestDropSpec();
	sg_swim_replay_spec_t swim = TestSwimSpec();
	sg_hook_replay_spec_t hook = TestHookSpec();

	CHECK(fabsf(SG_ReplayFallDelta(-600.0f, 0.0f, true, 0) - 36.0f) <
	      0.001f);
	CHECK(fabsf(SG_ReplayFallDelta(-600.0f, 0.0f, true, 1) - 18.0f) <
	      0.001f);
	CHECK(fabsf(SG_ReplayFallDelta(-600.0f, 0.0f, true, 2) - 9.0f) <
	      0.001f);
	CHECK(SG_ReplayFallDelta(-600.0f, 0.0f, true, 3) == 0.0f);
	CHECK(SG_ReplayFallDelta(-600.0f, -700.0f, false, 0) == 0.0f);

	VectorClear(drop.destination);
	pose.origin[0] = 39.999f;
	pose.origin[2] = 71.999f;
	CHECK(SG_DropReplayArrived(&drop, &pose, &observation));
	pose.origin[0] = 40.0f;
	CHECK(!SG_DropReplayArrived(&drop, &pose, &observation));
	pose.origin[0] = 0.0f;
	pose.origin[2] = 72.0f;
	CHECK(!SG_DropReplayArrived(&drop, &pose, &observation));
	pose.origin[2] = -72.0f;
	CHECK(!SG_DropReplayArrived(&drop, &pose, &observation));
	pose.origin[2] = 0.0f;
	observation.contact_clear = false;
	observation.drop_arrival_contact_clear = false;
	CHECK(!SG_DropReplayArrived(&drop, &pose, &observation));
	observation.contact_clear = true;
	observation.drop_arrival_contact_clear = true;
	observation.ground_support_valid = false;
	CHECK(!SG_DropReplayArrived(&drop, &pose, &observation));
	pose.grounded = false;
	pose.waterlevel = 2;
	CHECK(SG_DropReplayArrived(&drop, &pose, &observation));
	drop.destination_water = true;
	CHECK(!SG_DropReplayArrived(&drop, &pose, &observation));
	pose.waterlevel = 3;
	CHECK(SG_DropReplayArrived(&drop, &pose, &observation));

	drop.destination_water = false;
	pose = TestPose(95.999f, 0.0f, 71.999f);
	observation = TestObservation();
	CHECK(SG_DropReplayRecoveryReady(&drop, &pose, &observation));
	pose.origin[0] = 96.0f;
	CHECK(!SG_DropReplayRecoveryReady(&drop, &pose, &observation));
	pose.origin[0] = 0.0f;
	pose.origin[2] = 72.0f;
	CHECK(!SG_DropReplayRecoveryReady(&drop, &pose, &observation));
	/* Recovery admission is bound to a dry serialized destination. */
	drop.destination_water = true;
	pose = TestPose(95.999f, 0.0f, 71.999f);
	observation.drop_recovery_admitted = false;
	CHECK(!SG_DropReplayRecoveryReady(&drop, &pose, &observation));

	VectorClear(swim.destination);
	pose = TestPose(39.999f, 0.0f, 71.999f);
	CHECK(SG_SwimReplayArrived(&swim, &pose, &observation));
	pose.origin[0] = 40.0f;
	CHECK(!SG_SwimReplayArrived(&swim, &pose, &observation));
	pose.origin[0] = 0.0f;
	pose.origin[2] = -72.0f;
	CHECK(!SG_SwimReplayArrived(&swim, &pose, &observation));
	pose.origin[2] = 0.0f;
	swim.destination_water = true;
	pose.grounded = false;
	pose.waterlevel = 2;
	pose.watertype = CONTENTS_WATER;
	CHECK(SG_SwimReplayArrived(&swim, &pose, &observation));
	pose.watertype = CONTENTS_SLIME;
	CHECK(!SG_SwimReplayArrived(&swim, &pose, &observation));

	pose = TestPose(79.999f, 0.0f, 95.999f);
	observation = TestObservation();
	CHECK(SG_HookReplayReleaseReady(&hook, &pose, &observation));
	pose.origin[0] = 80.0f;
	CHECK(!SG_HookReplayReleaseReady(&hook, &pose, &observation));
	pose.origin[0] = 0.0f;
	pose.origin[2] = 96.0f;
	CHECK(!SG_HookReplayReleaseReady(&hook, &pose, &observation));
	pose.origin[0] = 1000.0f;
	pose.origin[2] = 0.0f;
	observation.hook_rope_length = 129;
	CHECK(SG_HookReplayReleaseReady(&hook, &pose, &observation));
	observation.hook_rope_length = 130;
	CHECK(!SG_HookReplayReleaseReady(&hook, &pose, &observation));
	observation.hook_rope_valid = false;
	CHECK(!SG_HookReplayReleaseReady(&hook, &pose, &observation));

	pose = TestPose(39.999f, 0.0f, 71.999f);
	observation = TestObservation();
	CHECK(SG_HookReplaySettled(&hook, &pose, &observation));
	pose.origin[0] = 40.0f;
	CHECK(!SG_HookReplaySettled(&hook, &pose, &observation));
	pose.origin[0] = 0.0f;
	pose.origin[2] = 72.0f;
	CHECK(!SG_HookReplaySettled(&hook, &pose, &observation));
}

static void TestDropWalkoffCommandsAndRecovery(void)
{
	sg_drop_replay_spec_t spec;
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;

	/* Radius witness, including the exact <= 8 boundary. */
	spec = TestDropSpec();
	spec.heading = 0;
	pose = TestPose(0.0f, 0.0f, 0.0f);
	pose.pms.delta_angles[PITCH] = 3;
	pose.pms.delta_angles[YAW] = 7;
	pose.pms.delta_angles[ROLL] = 11;
	observation = TestObservation();
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	CHECK(!state.walkoff);
	CHECK(command.msec == 25 && command.forwardmove == 400);
	CHECK(command.angles[PITCH] == -3 && command.angles[YAW] == -7 &&
	      command.angles[ROLL] == -11);
	pose.origin[0] = 92.0f;
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(state.walkoff && state.walkoff_ms == 25);
	CHECK(command.angles[YAW] == -7);
	CHECK_STATUS(SG_DropReplayPostStep(&state, &pose, &observation),
	             SG_REPLAY_RUNNING);

	/* Signed-projection witness is independent of the radius. */
	spec = TestDropSpec();
	pose = TestPose(0.0f, 100.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	pose.origin[0] = 100.0f;
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(state.walkoff);

	/* Airborne witness is ignored at elapsed zero and latches thereafter. */
	spec = TestDropSpec();
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	pose.grounded = false;
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(state.walkoff);

	/* First production-visible aligned dry landing starts one recovery. */
	spec = TestDropSpec();
	VectorSet(spec.lip, 0.0f, 0.0f, 0.0f);
	VectorSet(spec.destination, 200.0f, 0.0f, 0.0f);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (int step = 0; step < 3; step++)
	{
		pose.grounded = false;
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	pose = TestPose(120.0f, 0.0f, 0.0f); /* 80 from destination */
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	CHECK(state.recovery && state.airborne);
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(command.forwardmove == 400 && command.angles[YAW] == 0);
	pose.grounded = false;
	CHECK_STATUS(SG_DropReplayPostStep(&state, &pose, &observation),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_RECOVERY_LOST);

	/* On an aligned boundary, an already-recovering DROP evaluates terminal
	 * before recovery support.  A dry destination admits its canonical depth-2
	 * terminal even though the same pose is not a valid grounded recovery. */
	spec = TestDropSpec();
	VectorClear(spec.lip);
	VectorSet(spec.destination, 200.0f, 0.0f, 0.0f);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation = TestObservation();
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (int step = 0; step < 3; step++)
	{
		pose.grounded = false;
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	pose = TestPose(120.0f, 0.0f, 0.0f);
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	CHECK(state.recovery && state.progress.elapsed_ms == 100);
	for (int step = 0; step < 3; step++)
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	pose = TestPose(200.0f, 0.0f, 0.0f);
	pose.grounded = false;
	pose.waterlevel = 2;
	pose.watertype = CONTENTS_WATER;
	observation.ground_support_valid = false;
	observation.drop_recovery_contact_clear = false;
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_ARRIVED);
	CHECK(state.progress.arrival_ms == 200);
}

static void TestDropTerminalFailuresAndCaps(void)
{
	sg_drop_replay_spec_t spec = TestDropSpec();
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command, egress, hold, zero;
	vec3_t target;
	int step;

	VectorClear(target);
	memset(&zero, 0, sizeof(zero));
	zero.msec = SG_REPLAY_STEP_MS;
	memset(&hold, 0x5a, sizeof(hold));
	CHECK(SG_SwimReplayCommand(&pose, target, SG_SWIM_REPLAY_HOLD,
	                           &hold));
	CHECK(memcmp(&hold, &zero, sizeof(hold)) == 0);
	memset(&egress, 0x5a, sizeof(egress));
	command = egress;
	CHECK(!SG_SwimReplayCommand(&pose, target, SG_SWIM_REPLAY_EGRESS,
	                            &egress));
	CHECK(memcmp(&egress, &command, sizeof(egress)) == 0);
	pose.origin[0] = 1.0f;
	CHECK(SG_SwimReplayCommand(&pose, target, SG_SWIM_REPLAY_HOLD,
	                           &hold));
	CHECK(SG_SwimReplayCommand(&pose, target, SG_SWIM_REPLAY_EGRESS,
	                           &egress));
	CHECK(memcmp(&hold, &egress, sizeof(hold)) == 0 &&
	      hold.forwardmove == 400);
	pose.origin[0] = 0.0f;

	/* Arrival exists only on the 100 ms production boundary. */
	VectorClear(spec.lip);
	VectorSet(spec.destination, 100.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 3; step++)
	{
		pose.grounded = false;
		pose.origin[0] = 99.0f;
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	pose = TestPose(100.0f, 0.0f, 0.0f);
	pose.velocity[0] = 800.0f;
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_ARRIVED);
	CHECK(state.progress.arrival_ms == 100 && state.progress.exit_speed == 200);

	/* A close supported handoff is not a terminal contact.  It remains running
	 * through the first boundary, then the identical supported pose may arrive
	 * only after an intervening authoritative airborne step. */
	spec = TestDropSpec();
	VectorClear(spec.lip);
	VectorSet(spec.destination, 16.0f, 0.0f, 0.0f);
	spec.expected_arrival_ms = 200;
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation = TestObservation();
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
	{
		pose = TestPose(16.0f, 0.0f, 0.0f);
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	CHECK(!state.airborne && state.progress.elapsed_ms == 100);
	for (step = 0; step < 3; step++)
	{
		pose.grounded = false;
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	pose = TestPose(16.0f, 0.0f, 0.0f);
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_ARRIVED);
	CHECK(state.airborne && state.progress.arrival_ms == 200);

	/* A dry first landing outside recovery is terminal, not a new walk. */
	spec = TestDropSpec();
	VectorClear(spec.lip);
	VectorSet(spec.destination, 300.0f, 0.0f, 0.0f);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 3; step++)
	{
		pose.grounded = false;
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation.drop_landing_observed = true;
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_SHORT_LANDING);
	observation.drop_landing_observed = false;

	/* Deep-water contact at level one or two fails on its first end frame. */
	spec = TestDropSpec();
	VectorClear(spec.lip);
	spec.destination_water = true;
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
	{
		pose.grounded = false;
		if (step == 3)
		{
			pose.waterlevel = 2;
			pose.watertype = CONTENTS_WATER;
		}
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	}
	CHECK(state.progress.reason == SG_REPLAY_REASON_SHALLOW_WATER_CONTACT);

	/* Source contamination rejects before any command is written. */
	spec = TestDropSpec();
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation = TestObservation();
	observation.contaminated = true;
	memset(&command, 0x5a, sizeof(command));
	{
		usercmd_t untouched = command;

		CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation,
		                                0.0f), SG_REPLAY_FAILED);
		CHECK(state.progress.elapsed_ms == 0 &&
		      state.progress.reason == SG_REPLAY_REASON_CONTAMINATED);
		CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
		             SG_REPLAY_FAILED);
		CHECK(memcmp(&command, &untouched, sizeof(command)) == 0);
	}

	/* Unlike SWIM/HOOK, a post-command door transition ends DROP now. */
	observation = TestObservation();
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	observation.door_passed = true;
	CHECK_STATUS(SG_DropReplayPostStep(&state, &pose, &observation),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.elapsed_ms == 25 &&
	      state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED);

	/* Fall damage and post-step contamination are independently fail-closed. */
	spec = TestDropSpec();
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation = TestObservation();
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation,
	                                -600.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	CHECK(state.progress.reason == SG_REPLAY_REASON_DAMAGING_FALL);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	observation.contaminated = true;
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_CONTAMINATED);
	observation = TestObservation();

	/* No walkoff at exactly 2500 ms fails before another command. */
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < SG_REPLAY_DROP_APPROACH_MS / SG_REPLAY_STEP_MS;
	     step++)
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_APPROACH_TIMEOUT);

	/* Walkoff at zero gets exactly 2000 ms of suffix commands, with no command
	 * beginning at the cap. */
	spec = TestDropSpec();
	VectorClear(spec.lip);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < SG_REPLAY_DROP_TRAVEL_MS / SG_REPLAY_STEP_MS; step++)
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_DropReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_TRAVEL_TIMEOUT);
}

static void TestDropWetShelfPolicies(void)
{
	sg_drop_replay_spec_t spec = TestDropSpec();
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;
	int step;

	VectorClear(spec.lip);
	VectorSet(spec.destination, 200.0f, 0.0f, 0.0f);
	spec.destination_water = true;
	/* Adapter policy bits cannot reopen recovery for a wet destination.  Its
	 * first post-airborne dry contact fails even inside the recovery envelope. */
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation = TestObservation();
	observation.drop_recovery_admitted = true;
	observation.drop_landing_observed = false;
	CHECK_STATUS(SG_DropReplayBegin(&state, &spec, &pose, &observation,
	                                0.0f), SG_REPLAY_RUNNING);
	for (step = 0; step < 3; step++)
	{
		CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
		pose.grounded = false;
	}
	pose = TestPose(120.0f, 0.0f, 0.0f);
	CHECK_STATUS(DropStep(&state, &pose, &observation, &command),
	             SG_REPLAY_FAILED);
	CHECK(!state.recovery &&
	      state.progress.reason == SG_REPLAY_REASON_SHORT_LANDING);
}

static void TestSwimControllerAndCadence(void)
{
	sg_swim_replay_spec_t spec = TestSwimSpec();
	sg_swim_replay_state_t state;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int step;

	pose.pms.delta_angles[PITCH] = 5;
	pose.pms.delta_angles[YAW] = 7;
	pose.pms.delta_angles[ROLL] = 9;
	VectorSet(spec.destination, 0.0f, 0.0f, 1000.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	memset(&command, 0x5a, sizeof(command));
	CHECK_STATUS(SG_SwimReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(command.msec == 25 && command.forwardmove == 400 &&
	      command.sidemove == 0 && command.upmove == 0 &&
	      command.buttons == 0);
	CHECK(command.angles[PITCH] == (short)(ANGLE2SHORT(-85.0f) - 5));
	CHECK(command.angles[YAW] == -7 && command.angles[ROLL] == -9);
	CHECK_STATUS(SG_SwimReplayPostStep(&state, &pose, &observation),
	             SG_REPLAY_RUNNING);

	VectorSet(spec.destination, 0.0f, 0.0f, -1000.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_SwimReplayPreStep(&state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(command.angles[PITCH] == (short)(ANGLE2SHORT(85.0f) - 5));

	/* Arrival observed at 25/50/75 ms does not retire until 100 ms. */
	spec = TestSwimSpec();
	VectorSet(spec.destination, 100.0f, 0.0f, 0.0f);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
	{
		pose.origin[0] = 99.0f;
		pose.velocity[0] = 400.0f;
		CHECK_STATUS(SwimStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_ARRIVED : SG_REPLAY_RUNNING);
	}
	CHECK(state.progress.arrival_ms == 100 && state.progress.exit_speed == 100);

	/* A source already satisfying the terminal is not a traversal. */
	pose = TestPose(100.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_ZERO_TIME_ARRIVAL);

	/* Exact live timing is optional and rejects an early witness. */
	spec.expected_arrival_ms = 200;
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
	{
		pose.origin[0] = 99.0f;
		CHECK_STATUS(SwimStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	}
	CHECK(state.progress.reason == SG_REPLAY_REASON_TIMING_MISMATCH);
}

static void TestSwimFailuresAndCap(void)
{
	sg_swim_replay_spec_t spec = TestSwimSpec();
	sg_swim_replay_state_t state;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int step;

	/* Damaging fall is checked only after a complete production frame. */
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation,
	                                -600.0f), SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
		CHECK_STATUS(SwimStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	CHECK(state.progress.reason == SG_REPLAY_REASON_DAMAGING_FALL);

	/* Hazard is likewise a 100 ms law; contamination and doors are per-step. */
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
	{
		if (step == 3)
		{
			pose.waterlevel = 1;
			pose.watertype = CONTENTS_LAVA;
		}
		CHECK_STATUS(SwimStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	}
	CHECK(state.progress.reason == SG_REPLAY_REASON_HAZARDOUS_LIQUID);

	/* Door passage latches without truncating the command stream.  Only the
	 * otherwise-valid 100 ms terminal is replaced by DOOR_PASSED. */
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation = TestObservation();
	observation.door_passed = true;
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK(state.progress.door_passed_latched);
	observation.door_passed = false;
	for (step = 0; step < 4; step++)
	{
		if (step == 3)
			pose.origin[0] = 999.0f;
		memset(&command, 0x5a, sizeof(command));
		CHECK_STATUS(SwimStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
		CHECK(command.msec == 25 && command.forwardmove == 400);
	}
	CHECK(state.progress.elapsed_ms == 100 &&
	      state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED);

	/* The state reached at exactly 3000 ms is deliberately not a terminal
	 * sampling boundary, matching SG_OracleSwimTraverse's loop fence. */
	observation = TestObservation();
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_SwimReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	for (step = 0; step < SG_REPLAY_SWIM_LIMIT_MS / SG_REPLAY_STEP_MS; step++)
		CHECK_STATUS(SwimStep(&state, &pose, &observation, &command),
		             step == SG_REPLAY_SWIM_LIMIT_MS / SG_REPLAY_STEP_MS - 1 ?
		                 SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	CHECK(state.progress.elapsed_ms == SG_REPLAY_SWIM_LIMIT_MS);
	CHECK(state.progress.reason == SG_REPLAY_REASON_ACTION_TIMEOUT);
}

static void HookAttachAndStartPull(sg_hook_replay_state_t *state,
	const sg_hook_replay_spec_t *spec, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	usercmd_t command;
	int step;

	CHECK_STATUS(SG_HookReplayBegin(state, spec, pose, observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK(state->phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	CHECK_STATUS(SG_HookReplayAttached(state, pose), SG_REPLAY_RUNNING);
	CHECK(state->attach_pms.gravity == pose->pms.gravity);
	for (step = 0; step < 4; step++)
		CHECK_STATUS(HookStep(state, pose, observation, &command),
		             SG_REPLAY_RUNNING);
	CHECK(state->phase == SG_HOOK_REPLAY_WAIT_PULL);
	CHECK_STATUS(SG_HookReplayPullApplied(state, pose), SG_REPLAY_RUNNING);
}

static void HookReleaseToSettle(sg_hook_replay_state_t *state,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation,
	int release_step)
{
	usercmd_t command;
	int step;

	for (step = 1; step <= 4; step++)
	{
		observation->hook_rope_length = step == release_step ? 129 : 1000;
		CHECK_STATUS(HookStep(state, pose, observation, &command),
		             SG_REPLAY_RUNNING);
		if (step == release_step)
		{
			CHECK(state->release_requested);
			CHECK_STATUS(SG_HookReplayReleaseApplied(state, pose),
			             SG_REPLAY_RUNNING);
		}
	}
	observation->hook_rope_length = 1000;
	CHECK(state->phase == SG_HOOK_REPLAY_SETTLE);
}

static void TestPlanarAnglePrecision(void)
{
	sg_drop_replay_spec_t drop = TestDropSpec();
	sg_drop_replay_state_t drop_state;
	sg_hook_replay_spec_t hook = TestHookSpec();
	sg_hook_replay_state_t hook_state;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;

	/* This exact delta straddles one usercmd-short boundary. Live DROP
	 * independently reconstructs and checks the selected -32684 byte. Hook
	 * settlement intentionally retains its float byte here. */
	VectorSet(drop.lip, -94.25f, -0.75f, 0.0f);
	drop.heading = 128;
	CHECK_STATUS(SG_DropReplayBegin(&drop_state, &drop, &pose, &observation,
	                                0.0f), SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_DropReplayPreStep(&drop_state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK(!drop_state.walkoff && command.angles[YAW] == (short)-32684);

	VectorSet(hook.destination, -94.25f, -0.75f, 0.0f);
	HookAttachAndStartPull(&hook_state, &hook, &pose, &observation);
	HookReleaseToSettle(&hook_state, &pose, &observation, 1);
	observation.contact_clear = false;
	CHECK_STATUS(SG_HookReplayPreStep(&hook_state, &pose, &observation,
	                                  &command), SG_REPLAY_RUNNING);
	CHECK(command.angles[YAW] == (short)-32685);
}

static void TestHookDoorLatch(void)
{
	sg_hook_replay_spec_t spec = TestHookSpec();
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose = TestPose(200.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int step;

	observation.door_passed = true;
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK(state.progress.door_passed_latched);
	observation.door_passed = false;
	CHECK_STATUS(SG_HookReplayAttached(&state, &pose), SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_HookReplayPullApplied(&state, &pose), SG_REPLAY_RUNNING);
	HookReleaseToSettle(&state, &pose, &observation, 2);

	/* The full attach/pull/settle stream still runs; the otherwise successful
	 * terminal, not the early door observation, supplies the failure point. */
	pose.origin[0] = 30.0f;
	for (step = 0; step < 4; step++)
	{
		memset(&command, 0x5a, sizeof(command));
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
		CHECK(command.msec == 25 && command.angles[PITCH] == 0 &&
		      command.angles[YAW] == 0 && command.forwardmove == 0);
	}
	CHECK(state.progress.elapsed_ms == 300 &&
	      state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED);
}

static void TestHookEventsCommandsAndMidframe(void)
{
	sg_hook_replay_spec_t spec = TestHookSpec();
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose = TestPose(200.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int step;

	pose.pms.gravity = 777;
	pose.pms.delta_angles[PITCH] = 3;
	pose.pms.delta_angles[YAW] = 5;
	pose.pms.delta_angles[ROLL] = 7;
	HookAttachAndStartPull(&state, &spec, &pose, &observation);

	/* Release at 50 ms: the remainder of the pull frame keeps the exact fixed
	 * view and zero movement, then settlement starts at 100 ms. */
	for (step = 1; step <= 4; step++)
	{
		observation.hook_rope_length = step == 2 ? 129 : 1000;
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
		CHECK(command.msec == 25 && command.forwardmove == 0 &&
		      command.sidemove == 0 && command.upmove == 0);
		CHECK(command.angles[PITCH] ==
		      (short)(ANGLE2SHORT(spec.view_angles[PITCH]) - 3));
		CHECK(command.angles[YAW] ==
		      (short)(ANGLE2SHORT(spec.view_angles[YAW]) - 5));
		if (step == 2)
			CHECK_STATUS(SG_HookReplayReleaseApplied(&state, &pose),
			             SG_REPLAY_RUNNING);
	}
	CHECK(state.release_ms == 50 && state.pull_ms == 100);
	CHECK(state.phase == SG_HOOK_REPLAY_SETTLE);

	/* First settle arrival at 25 ms latches; substeps 2--4 are literal zeros,
	 * and the boundary must remain terminal before success. */
	pose.origin[0] = 100.0f;
	observation.contact_clear = false;
	CHECK_STATUS(SG_HookReplayPreStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	CHECK(command.forwardmove == 400);
	pose.origin[0] = 30.0f;
	pose.velocity[0] = 400.0f;
	observation.contact_clear = true;
	CHECK_STATUS(SG_HookReplayPostStep(&state, &pose, &observation),
	             SG_REPLAY_RUNNING);
	CHECK(state.settle_arrival_ms == 25);
	for (step = 1; step < 4; step++)
	{
		memset(&command, 0x5a, sizeof(command));
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_ARRIVED : SG_REPLAY_RUNNING);
		CHECK(command.msec == 25 && command.buttons == 0 &&
		      command.angles[PITCH] == 0 && command.angles[YAW] == 0 &&
		      command.angles[ROLL] == 0 && command.forwardmove == 0 &&
		      command.sidemove == 0 && command.upmove == 0);
	}
	CHECK(state.settle_ms == 100 && state.progress.arrival_ms == 25 &&
	      state.progress.exit_speed == 100);

	/* A release discovered on substep four cannot be acknowledged until after
	 * PostStep returns; ReleaseApplied retires that same boundary immediately. */
	pose = TestPose(200.0f, 0.0f, 0.0f);
	observation = TestObservation();
	HookAttachAndStartPull(&state, &spec, &pose, &observation);
	HookReleaseToSettle(&state, &pose, &observation, 4);
	CHECK(state.phase == SG_HOOK_REPLAY_SETTLE && state.phase_step == 0);
	observation.contact_clear = false;
	CHECK_STATUS(SG_HookReplayPreStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	CHECK(state.phase == SG_HOOK_REPLAY_SETTLE && command.forwardmove == 400);
}

static void TestHookFlightReleaseAndEventFailures(void)
{
	sg_hook_replay_spec_t spec = TestHookSpec();
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose = TestPose(200.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int step;

	/* flight_ms includes the attachment body frame: 300 ms means two complete
	 * outbound frames, then an explicit attachment event. */
	spec.flight_ms = 300;
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK(state.phase == SG_HOOK_REPLAY_FLIGHT);
	for (step = 0; step < 8; step++)
	{
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
		if (step == 3)
			CHECK(state.phase == SG_HOOK_REPLAY_FLIGHT);
	}
	CHECK(state.flight_body_ms == 200 &&
	      state.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	CHECK_STATUS(SG_HookReplayAttached(&state, &pose), SG_REPLAY_RUNNING);

	/* Attaching before the derived boundary fails closed. */
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_HookReplayAttached(&state, &pose), SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_HOOK_ATTACH_TIMING);

	/* A rope already in either release envelope after the attachment frame is
	 * rejected before the first pull. */
	spec = TestHookSpec();
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_HookReplayAttached(&state, &pose), SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++)
	{
		observation.hook_rope_length = step == 3 ? 129 : 1000;
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             step == 3 ? SG_REPLAY_FAILED : SG_REPLAY_RUNNING);
	}
	CHECK(state.progress.reason == SG_REPLAY_REASON_HOOK_RELEASE_BEFORE_PULL);

	/* Expected release cadence rejects a missing 25 ms release immediately. */
	spec = TestHookSpec();
	spec.expected_release_ms = 25;
	spec.expected_pull_ms = 100;
	pose = TestPose(200.0f, 0.0f, 0.0f);
	observation = TestObservation();
	HookAttachAndStartPull(&state, &spec, &pose, &observation);
	CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_HOOK_RELEASE_MISSED);

	/* Initial pending fall and initial contamination are adapter-visible inputs. */
	spec = TestHookSpec();
	pose = TestPose(200.0f, 0.0f, 0.0f);
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation,
	                                -600.0f), SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_DAMAGING_FALL);
	observation.contaminated = true;
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation, 0.0f),
	             SG_REPLAY_FAILED);
	CHECK(state.progress.reason == SG_REPLAY_REASON_CONTAMINATED);
}

static void TestHookFlingRelease(void)
{
	sg_hook_replay_spec_t spec = TestHookSpec();
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();

	VectorSet(spec.destination, 1200.0f, 0.0f, -200.0f);
	spec.fling_release = true;
	spec.settle_limit_ms = SG_REPLAY_HOOK_FLING_SETTLE_MS;
	pose.pms.gravity = 100;
	VectorSet(pose.velocity, 600.0f, 0.0f, 0.0f);
	CHECK(SG_HookReplayFlingReleaseReady(&spec, &pose, &observation));
	pose.velocity[0] = -600.0f;
	CHECK(!SG_HookReplayFlingReleaseReady(&spec, &pose, &observation));
	pose.velocity[0] = 299.0f;
	CHECK(!SG_HookReplayFlingReleaseReady(&spec, &pose, &observation));
	pose.velocity[0] = 600.0f;
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation,
	                                0.0f), SG_REPLAY_RUNNING);

	spec.fling_release = false;
	CHECK_STATUS(SG_HookReplayBegin(&state, &spec, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
}

static void TestHookPullAndSettleCaps(void)
{
	sg_hook_replay_spec_t spec = TestHookSpec();
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose = TestPose(200.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int frame, step;

	HookAttachAndStartPull(&state, &spec, &pose, &observation);
	for (frame = 0; frame <
	                    SG_REPLAY_HOOK_PULL_LIMIT_MS / SG_REPLAY_FRAME_MS;
	     frame++)
	{
		if (frame > 0)
			CHECK_STATUS(SG_HookReplayPullApplied(&state, &pose),
			             SG_REPLAY_RUNNING);
		for (step = 0; step < 4; step++)
			CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
			             (frame == 29 && step == 3) ? SG_REPLAY_FAILED :
			                                            SG_REPLAY_RUNNING);
	}
	CHECK(state.pull_ms == SG_REPLAY_HOOK_PULL_LIMIT_MS);
	CHECK(state.progress.reason == SG_REPLAY_REASON_HOOK_PULL_TIMEOUT);

	/* Dry settlement begins its last frame at 900 ms and fails at 1000 ms. */
	pose = TestPose(200.0f, 0.0f, 0.0f);
	observation = TestObservation();
	HookAttachAndStartPull(&state, &spec, &pose, &observation);
	HookReleaseToSettle(&state, &pose, &observation, 1);
	observation.contact_clear = false;
	for (frame = 0; frame < 10; frame++)
		for (step = 0; step < 4; step++)
			CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
			             (frame == 9 && step == 3) ? SG_REPLAY_FAILED :
			                                           SG_REPLAY_RUNNING);
	CHECK(state.settle_ms == 1000);
	CHECK(state.progress.reason == SG_REPLAY_REASON_HOOK_SETTLE_TIMEOUT);

	/* The wet 1250 ms loop fence permits the complete frame that starts at
	 * 1200 ms. Arrival on its fourth substep is therefore 1300/1300. */
	spec = TestHookSpec();
	spec.settle_limit_ms = SG_RUNE_PROOF_HOOK_WATER_SETTLE_MS;
	pose = TestPose(200.0f, 0.0f, 0.0f);
	observation = TestObservation();
	HookAttachAndStartPull(&state, &spec, &pose, &observation);
	HookReleaseToSettle(&state, &pose, &observation, 1);
	observation.contact_clear = false;
	for (frame = 0; frame < 12; frame++)
		for (step = 0; step < 4; step++)
			CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
			             SG_REPLAY_RUNNING);
	for (step = 0; step < 3; step++)
	{
		CHECK_STATUS(HookStep(&state, &pose, &observation, &command),
		             SG_REPLAY_RUNNING);
	}
	CHECK_STATUS(SG_HookReplayPreStep(&state, &pose, &observation, &command),
	             SG_REPLAY_RUNNING);
	pose.origin[0] = 0.0f;
	observation.contact_clear = true;
	CHECK_STATUS(SG_HookReplayPostStep(&state, &pose, &observation),
	             SG_REPLAY_ARRIVED);
	CHECK(state.settle_arrival_ms == 1300 && state.settle_ms == 1300);
}

static void TestMalformedAndCadenceFailures(void)
{
	sg_drop_replay_spec_t drop = TestDropSpec();
	sg_drop_replay_state_t drop_state;
	sg_swim_replay_spec_t swim = TestSwimSpec();
	sg_swim_replay_state_t swim_state;
	sg_hook_replay_spec_t hook = TestHookSpec();
	sg_hook_replay_state_t hook_state;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	int variant;

	drop.destination[0] = NAN;
	CHECK_STATUS(SG_DropReplayBegin(&drop_state, &drop, &pose, &observation,
	                                0.0f),
	             SG_REPLAY_FAILED);
	CHECK_STATUS(SG_SwimReplayBegin(&swim_state, &swim, &pose, &observation,
	                                0.0f), SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_SwimReplayPreStep(&swim_state, &pose, &command),
	             SG_REPLAY_RUNNING);
	CHECK_STATUS(SG_SwimReplayPreStep(&swim_state, &pose, &command),
	             SG_REPLAY_FAILED);
	CHECK(swim_state.progress.reason == SG_REPLAY_REASON_INVALID_STATE);

	hook.flight_ms = 125;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	hook = TestHookSpec();
	hook.settle_limit_ms = 999;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);

	/* A canonical bolt ray yields 100..10300 ms. Zero and even very large
	 * cadence-aligned values must fail before any loop or integer accumulation. */
	hook = TestHookSpec();
	CHECK(SG_REPLAY_HOOK_FLIGHT_MAX_MS == 10300);
	hook.flight_ms = 0;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	hook.flight_ms = 10400;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	hook.flight_ms = INT_MAX - (INT_MAX % SG_REPLAY_FRAME_MS);
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	hook.flight_ms = SG_REPLAY_HOOK_FLIGHT_MAX_MS;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_RUNNING);

	/* ANGLE2SHORT casts to int. Finite-but-unbounded views are rejected before
	 * command generation, including FLT_MAX under UBSan. */
	hook = TestHookSpec();
	hook.view_angles[PITCH] = FLT_MAX;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	hook = TestHookSpec();
	hook.view_angles[YAW] = 180.0f;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);

	/* Decoded RUNE control is stronger than ANGLE2SHORT's safe range. Every
	 * malformed view fails atomically: no spec/timing copy and no command byte. */
	for (variant = 0; variant < 4; variant++)
	{
		hook = TestHookSpec();
		switch (variant)
		{
		case 0: hook.view_angles[PITCH] = 179.0f; break;
		case 1: hook.view_angles[PITCH] = 10.001f; break;
		case 2: hook.view_angles[YAW] = 90.001f; break;
		default: hook.view_angles[ROLL] = 0.125f; break;
		}
		memset(&command, 0x5a, sizeof(command));
		{
			usercmd_t untouched = command;

			CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose,
			                                &observation, 0.0f),
			             SG_REPLAY_FAILED);
			CHECK(hook_state.progress.reason ==
			          SG_REPLAY_REASON_INVALID_ARGUMENT &&
			      hook_state.progress.elapsed_ms == 0 &&
			      !hook_state.progress.step_pending &&
			      hook_state.spec.flight_ms == 0 &&
			      hook_state.spec.view_angles[PITCH] == 0.0f &&
			      hook_state.spec.view_angles[YAW] == 0.0f &&
			      hook_state.spec.view_angles[ROLL] == 0.0f);
			CHECK_STATUS(SG_HookReplayPreStep(&hook_state, &pose,
			                                  &observation, &command),
			             SG_REPLAY_FAILED);
			CHECK(memcmp(&command, &untouched, sizeof(command)) == 0);
		}
	}

	/* Adapter inputs are canonical data, not truthy C integers. */
	pose = TestPose(0.0f, 0.0f, 0.0f);
	pose.waterlevel = -1;
	CHECK_STATUS(SG_SwimReplayBegin(&swim_state, &swim, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	pose.waterlevel = 4;
	CHECK_STATUS(SG_SwimReplayBegin(&swim_state, &swim, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	pose.grounded = (qboolean)2;
	drop = TestDropSpec();
	CHECK_STATUS(SG_DropReplayBegin(&drop_state, &drop, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	observation.contact_clear = (qboolean)2;
	CHECK_STATUS(SG_DropReplayBegin(&drop_state, &drop, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
	observation = TestObservation();
	drop.destination_water = (qboolean)2;
	CHECK_STATUS(SG_DropReplayBegin(&drop_state, &drop, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);

	/* A declared valid rope length is never negative. */
	hook = TestHookSpec();
	observation = TestObservation();
	observation.hook_rope_length = -1;
	CHECK_STATUS(SG_HookReplayBegin(&hook_state, &hook, &pose, &observation,
	                                0.0f), SG_REPLAY_FAILED);
}

int main(void)
{
	TestFallAndTerminalBoundaries();
	TestDropWalkoffCommandsAndRecovery();
	TestDropTerminalFailuresAndCaps();
	TestDropWetShelfPolicies();
	TestSwimControllerAndCadence();
	TestSwimFailuresAndCap();
	TestPlanarAnglePrecision();
	TestHookDoorLatch();
	TestHookEventsCommandsAndMidframe();
	TestHookFlightReleaseAndEventFailures();
	TestHookFlingRelease();
	TestHookPullAndSettleCaps();
	TestMalformedAndCadenceFailures();

	if (failures)
	{
		fprintf(stderr, "sg_replay_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_replay_test: ok");
	return 0;
}
