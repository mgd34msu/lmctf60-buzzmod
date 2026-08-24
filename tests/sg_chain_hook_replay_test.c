#include "q_shared.h"

#include <stdio.h>
#include <string.h>

#include "slipgate/sg_chain_hook_replay.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_replay_pose_t TestPose(void)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	pose.pms.gravity = 100;
	pose.velocity[0] = 500.0f;
	pose.grounded = false;
	return pose;
}

static sg_replay_observation_t TestObservation(void)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.contact_clear = true;
	observation.ground_support_valid = true;
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 1000;
	return observation;
}

static sg_hook_replay_spec_t TestRope(float yaw,
	sg_hook_replay_terminal_t terminal)
{
	sg_hook_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	VectorSet(spec.bite, 0.0f, 0.0f, 512.0f);
	VectorSet(spec.destination, 1000.0f, 0.0f, 0.0f);
	VectorSet(spec.view_angles, 0.0f,
	          SHORT2ANGLE((short)ANGLE2SHORT(yaw)), 0.0f);
	spec.flight_ms = SG_REPLAY_FRAME_MS;
	spec.expected_release_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_pull_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_ms = SG_REPLAY_TIME_DISCOVER;
	spec.terminal = terminal;
	if (terminal == SG_HOOK_REPLAY_TERMINAL_SETTLE)
		spec.settle_limit_ms = SG_RUNE_PROOF_HOOK_DRY_SETTLE_MS;
	return spec;
}

static sg_chain_hook_replay_spec_t TestSpec(const sg_replay_pose_t *pose)
{
	sg_chain_hook_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.rope[0] = TestRope(0.0f,
	    SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF);
	spec.rope[1] = TestRope(90.0f, SG_HOOK_REPLAY_TERMINAL_SETTLE);
	spec.refire_start.pose = *pose;
	spec.refire_start.old_frame_z = pose->velocity[2];
	spec.second_fire.pose = *pose;
	spec.second_fire.old_frame_z = pose->velocity[2];
	return spec;
}

static sg_chain_hook_replay_result_t Step(sg_chain_hook_replay_state_t *state,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation,
	usercmd_t *command)
{
	sg_chain_hook_replay_result_t result;

	result = SG_ChainHookReplayPreStep(state, pose, observation, command);
	if (result.status != SG_REPLAY_RUNNING)
		return result;
	return SG_ChainHookReplayPostStep(state, pose, observation,
	                                  pose->velocity[2]);
}

static void ReachFirstPull(sg_chain_hook_replay_state_t *state,
	const sg_chain_hook_replay_spec_t *spec, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	sg_chain_hook_replay_result_t result;
	usercmd_t command;
	int step;

	result = SG_ChainHookReplayBegin(state, spec, pose, observation, 0.0f);
	CHECK(result.status == SG_REPLAY_RUNNING);
	result = SG_ChainHookReplayEvent(state,
	    SG_CHAIN_HOOK_REPLAY_EVENT_ATTACHED, pose, observation, 0.0f);
	CHECK(result.status == SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++) {
		result = Step(state, pose, observation, &command);
		CHECK(result.status == SG_REPLAY_RUNNING);
	}
	result = SG_ChainHookReplayEvent(state,
	    SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED,
	    pose, observation, 0.0f);
	CHECK(result.status == SG_REPLAY_RUNNING);
}

static void TestReleaseSubstepAndRefire(int release_step)
{
	sg_replay_pose_t pose = TestPose();
	sg_replay_observation_t observation = TestObservation();
	sg_chain_hook_replay_spec_t spec = TestSpec(&pose);
	sg_chain_hook_replay_state_t state;
	sg_chain_hook_replay_result_t result;
	usercmd_t command;
	int step;

	ReachFirstPull(&state, &spec, &pose, &observation);
	for (step = 1; step <= 4; step++) {
		observation.hook_rope_length = step == release_step ? 129 : 1000;
		result = Step(&state, &pose, &observation, &command);
		CHECK(result.status == SG_REPLAY_RUNNING);
		if (step == release_step) {
			CHECK(result.effect == SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE);
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED,
			    &pose, &observation, pose.velocity[2]);
			CHECK(result.status == SG_REPLAY_RUNNING);
		}
	}
	CHECK(state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_AIM);
	observation.hook_rope_length = 1000;
	for (step = 1; step <= 4; step++) {
		result = Step(&state, &pose, &observation, &command);
		CHECK(result.status == SG_REPLAY_RUNNING);
		CHECK(command.msec == SG_REPLAY_STEP_MS);
		CHECK(command.angles[YAW] == ANGLE2SHORT(90.0f));
		CHECK(result.effect == (step == 4 ?
		    SG_CHAIN_HOOK_REPLAY_EFFECT_FIRE_NEXT :
		    SG_CHAIN_HOOK_REPLAY_EFFECT_NONE));
	}
	CHECK(state.phase == SG_CHAIN_HOOK_REPLAY_WAIT_SECOND_FIRE);
	result = SG_ChainHookReplayEvent(&state,
	    SG_CHAIN_HOOK_REPLAY_EVENT_NEXT_FIRED,
	    &pose, &observation, pose.velocity[2]);
	CHECK(result.status == SG_REPLAY_RUNNING);
	CHECK(state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE);
	result = SG_ChainHookReplayEvent(&state,
	    SG_CHAIN_HOOK_REPLAY_EVENT_ATTACHED,
	    &pose, &observation, pose.velocity[2]);
	CHECK(result.status == SG_REPLAY_RUNNING);
	for (step = 0; step < 4; step++) {
		result = Step(&state, &pose, &observation, &command);
		CHECK(result.status == SG_REPLAY_RUNNING);
	}
	result = SG_ChainHookReplayEvent(&state,
	    SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED,
	    &pose, &observation, pose.velocity[2]);
	CHECK(result.status == SG_REPLAY_RUNNING);
	observation.hook_rope_length = 129;
	for (step = 0; step < 4; step++) {
		result = Step(&state, &pose, &observation, &command);
		CHECK(result.status == SG_REPLAY_RUNNING);
		if (result.effect == SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE)
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED,
			    &pose, &observation, pose.velocity[2]);
	}
	CHECK(state.rope.phase == SG_HOOK_REPLAY_SETTLE);
	VectorCopy(spec.rope[1].destination, pose.origin);
	pose.grounded = true;
	for (step = 0; step < 4; step++)
		result = Step(&state, &pose, &observation, &command);
	CHECK(result.status == SG_REPLAY_ARRIVED);
	CHECK(state.phase == SG_CHAIN_HOOK_REPLAY_COMPLETE);
}

static void TestEventOrderAndCheckpointMismatch(void)
{
	sg_replay_pose_t pose = TestPose();
	sg_replay_observation_t observation = TestObservation();
	sg_chain_hook_replay_spec_t spec = TestSpec(&pose);
	sg_chain_hook_replay_state_t state;
	sg_chain_hook_replay_result_t result;
	usercmd_t command;
	int step;

	result = SG_ChainHookReplayBegin(&state, &spec, &pose, &observation, 0.0f);
	CHECK(result.status == SG_REPLAY_RUNNING);
	result = SG_ChainHookReplayEvent(&state,
	    SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED,
	    &pose, &observation, 0.0f);
	CHECK(result.status == SG_REPLAY_FAILED);
	CHECK(result.reason == SG_REPLAY_REASON_HOOK_EVENT_ORDER);

	spec = TestSpec(&pose);
	spec.refire_start.pose.origin[0] = 1.0f;
	ReachFirstPull(&state, &spec, &pose, &observation);
	for (step = 1; step <= 4; step++) {
		observation.hook_rope_length = step == 1 ? 129 : 1000;
		result = Step(&state, &pose, &observation, &command);
		if (step == 1)
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED,
			    &pose, &observation, pose.velocity[2]);
	}
	CHECK(result.status == SG_REPLAY_FAILED);
	CHECK(result.reason == SG_REPLAY_REASON_CHECKPOINT_MISMATCH);
}

int main(void)
{
	int release_step;

	for (release_step = 1; release_step <= 4; release_step++)
		TestReleaseSubstepAndRefire(release_step);
	TestEventOrderAndCheckpointMismatch();
	if (failures) {
		fprintf(stderr, "sg_chain_hook_replay_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_chain_hook_replay_test: ok");
	return 0;
}
