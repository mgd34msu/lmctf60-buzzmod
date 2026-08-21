#include <math.h>
#include <string.h>

#include "sg_compound_hook_live_fixture.h"

static void TestCheckpointAndFirstBoltAuthority(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_bolt_t bolt = { 21, 9001U };
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	Setup(&fixture, &host, &pose, &observation);
	fixture.source_checkpoint = 0;
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_REJECTED);
	CHECK(result.failure ==
	      SG_COMPOUND_HOOK_LIVE_FAILURE_SOURCE_CHECKPOINT);
	CHECK(fixture.acquire_calls == 0 && !state.guard_owned);
	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	pose.pms.origin[0]++;
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.failure ==
	      SG_COMPOUND_HOOK_LIVE_FAILURE_SOURCE_CHECKPOINT);
	CHECK(fixture.acquire_calls == 0 && !state.guard_owned);
	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	fixture.source_old_frame_z = 1.0f;
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.failure ==
	      SG_COMPOUND_HOOK_LIVE_FAILURE_SOURCE_CHECKPOINT);
	CHECK(fixture.acquire_calls == 0 && !state.guard_owned);

	Setup(&fixture, &host, &pose, &observation);
	DriveToTop(&fixture, &host, &state, &pose, &observation);
	fixture.suffix_checkpoint = 0;
	result = SG_CompoundHookLiveLinked(&state, &host, &bolt, 3, &pose,
	                                   &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure ==
	      SG_COMPOUND_HOOK_LIVE_FAILURE_SUFFIX_CHECKPOINT);
	CHECK(state.guard_owned && fixture.release_calls == 0);
	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	DriveToTop(&fixture, &host, &state, &pose, &observation);
	pose.pms.origin[1]++;
	result = SG_CompoundHookLiveLinked(&state, &host, &bolt, 3, &pose,
	                                   &observation);
	CHECK(result.failure ==
	      SG_COMPOUND_HOOK_LIVE_FAILURE_SUFFIX_CHECKPOINT);
	CHECK(state.guard_owned && fixture.release_calls == 0);

	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	DriveToTop(&fixture, &host, &state, &pose, &observation);
	bolt.key = 22;
	result = SG_CompoundHookLiveLinked(&state, &host, &bolt, 3, &pose,
	                                   &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	CHECK(!state.bolt_linked);

	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	DriveToTop(&fixture, &host, &state, &pose, &observation);
	bolt.key = 21;
	result = SG_CompoundHookLiveLinked(&state, &host, &bolt, 0, &pose,
	                                   &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_LINK);
	CHECK(!state.bolt_linked);
	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	DriveToTop(&fixture, &host, &state, &pose, &observation);
	result = SG_CompoundHookLiveLinked(&state, &host, &bolt,
	                                   state.touch_frame_serial, &pose,
	                                   &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_LINK);
	CHECK(!state.bolt_linked);
}

static void PadRetainedFailureToBoundary(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_compound_hook_live_result_t result;

	while (state->transaction_elapsed_ms % SG_REPLAY_FRAME_MS != 0)
	{
		result = Step(state, host, pose, observation, NULL);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	}
	CHECK(state->control == SG_COMPOUND_HOOK_LIVE_CONTROL_NONE);
	CHECK(state->last_boundary_ms == state->transaction_elapsed_ms);
}

static void TestIssuedCommandFailurePadsBeforeRecovery(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;

	Setup(&fixture, &host, &pose, &observation);
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLiveTouch(&state, &host, 99, &pose,
	                                  &observation, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.command_pending && state.aborted_command_pending);
	result = SG_CompoundHookLivePostStep(&state, &host, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 25);
	PadRetainedFailureToBoundary(&state, &host, &pose, &observation);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);

	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	fixture.snapshot.trigger_key++;
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.command_pending && state.aborted_command_pending);
	fixture.snapshot.trigger_key--;
	result = SG_CompoundHookLivePostStep(&state, &host, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 325);
}

static void TestRejectedApprovalDiscardsUnconsumedCommand(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;

	Setup(&fixture, &host, &pose, &observation);
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	command.forwardmove++;
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 0);
	CHECK(!state.command_pending && !state.aborted_command_pending);
	CHECK(state.control == SG_COMPOUND_HOOK_LIVE_CONTROL_NONE);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.release_calls == 1 && fixture.abort_calls == 0);
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.guard_owned && state.transaction_elapsed_ms == 0);
	result = SG_CompoundHookLiveOrphan(&state, &host);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);

	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	command.angles[YAW]++;
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 300);
	CHECK(!state.command_pending && !state.aborted_command_pending);
	CHECK(state.control == SG_COMPOUND_HOOK_LIVE_CONTROL_NONE);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.abort_calls == 1 && fixture.release_calls == 1);

	Setup(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
	result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.transaction_elapsed_ms == 325);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	command.buttons ^= BUTTON_ATTACK;
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 325);
	CHECK(!state.command_pending && !state.aborted_command_pending);
	CHECK(state.control == SG_COMPOUND_HOOK_LIVE_CONTROL_PADDING);
	PadRetainedFailureToBoundary(&state, &host, &pose, &observation);
	CHECK(state.transaction_elapsed_ms == 400);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.abort_calls == 1 && fixture.release_calls == 1);
}

static void TestWaitAttachSafetyObservation(void)
{
	static const sg_replay_reason_t reasons[] = {
		SG_REPLAY_REASON_CONTAMINATED,
		SG_REPLAY_REASON_DOOR_PASSED,
		SG_REPLAY_REASON_HAZARDOUS_LIQUID,
		SG_REPLAY_REASON_NONFINITE_POSE,
		SG_REPLAY_REASON_DAMAGING_FALL
	};
	int scenario, step;

	for (scenario = 0; scenario < 5; scenario++)
	{
		fixture_t fixture;
		sg_compound_hook_live_host_t host;
		sg_compound_hook_live_state_t state =
			SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
		sg_compound_hook_live_result_t result;
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		usercmd_t command;
		int elapsed;

		SetupLateAttach(&fixture, &host, &pose, &observation);
		(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
		for (step = 0; step < 4; step++)
			result = Step(&state, &host, &pose, &observation, NULL);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
		elapsed = state.hook.progress.elapsed_ms;
		result = SG_CompoundHookLivePreStep(&state, &host, &pose,
		                                    &observation, &command);
		CHECK(result.command_ready);
		result = SG_CompoundHookLiveApproveCommand(&state, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		if (scenario == 0)
			observation.contaminated = true;
		else if (scenario == 1)
			observation.door_passed = true;
		else if (scenario == 2)
		{
			pose.waterlevel = 2;
			pose.watertype = CONTENTS_LAVA;
		}
		else if (scenario == 3)
			pose.origin[0] = NAN;
		else
		{
			pose.waterlevel = 0;
			pose.grounded = true;
			pose.velocity[2] = -700.0f;
		}
		result = SG_CompoundHookLivePostStep(&state, &host, &pose,
		                                     &observation);
		if (scenario == 4 &&
		    result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING)
		{
			for (step = 0; step < 3; step++)
				result = Step(&state, &host, &pose, &observation, NULL);
		}
		if (result.outcome != SG_COMPOUND_HOOK_LIVE_RECOVERING ||
		    result.replay_reason != reasons[scenario])
			fprintf(stderr,
			        "wait-attach scenario=%d outcome=%d reason=%d expected=%d\n",
			        scenario, result.outcome, result.replay_reason,
			        reasons[scenario]);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
		CHECK(result.replay_reason == reasons[scenario]);
		CHECK(state.hook.progress.elapsed_ms == elapsed);
	}
}

static void TestOpeningSafetyObservation(void)
{
	static const sg_replay_reason_t reasons[] = {
		SG_REPLAY_REASON_CONTAMINATED,
		SG_REPLAY_REASON_DOOR_PASSED,
		SG_REPLAY_REASON_HAZARDOUS_LIQUID,
		SG_REPLAY_REASON_NONFINITE_POSE
	};
	int scenario;

	for (scenario = 0; scenario < 4; scenario++)
	{
		fixture_t fixture;
		sg_compound_hook_live_host_t host;
		sg_compound_hook_live_state_t state =
			SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
		sg_compound_hook_live_result_t result;
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		usercmd_t command;

		Setup(&fixture, &host, &pose, &observation);
		result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose,
		                                  &observation);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		result = SG_CompoundHookLivePreStep(&state, &host, &pose,
		                                    &observation, &command);
		CHECK(result.command_ready);
		result = SG_CompoundHookLiveApproveCommand(&state, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		result = SG_CompoundHookLiveTouch(&state, &host, 11, &pose,
		                                  &observation, 1);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		result = SG_CompoundHookLiveActivate(&state, &host, 11, 12, 1);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		if (scenario == 0)
			observation.contaminated = true;
		else if (scenario == 1)
			observation.door_passed = true;
		else if (scenario == 2)
		{
			pose.waterlevel = 2;
			pose.watertype = CONTENTS_LAVA;
		}
		else
			pose.origin[0] = NAN;
		result = SG_CompoundHookLivePostStep(&state, &host, &pose,
		                                     &observation);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
		CHECK(result.replay_reason == reasons[scenario]);
	}
}

static void TestOpeningAndWaitAttachUseFrameEntryFallSpeed(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;
	int step;

	Setup(&fixture, &host, &pose, &observation);
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	pose.velocity[2] = -175.0f;
	result = SG_CompoundHookLiveTouch(&state, &host, 11, &pose,
	                                  &observation, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLiveActivate(&state, &host, 11, 12, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	pose.waterlevel = 0;
	pose.watertype = 0;
	result = SG_CompoundHookLivePostStep(&state, &host, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	for (step = 2; step <= 4; step++)
	{
		pose.velocity[2] = -175.0f * step;
		pose.grounded = step == 4;
		result = Step(&state, &host, &pose, &observation, NULL);
	}
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.replay_reason == SG_REPLAY_REASON_DAMAGING_FALL);

	SetupLateAttach(&fixture, &host, &pose, &observation);
	memset(&state, 0, sizeof(state));
	(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	pose.waterlevel = 0;
	pose.watertype = 0;
	for (step = 1; step <= 4; step++)
	{
		pose.velocity[2] = -175.0f * step;
		pose.grounded = step == 4;
		result = Step(&state, &host, &pose, &observation, NULL);
	}
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.replay_reason == SG_REPLAY_REASON_DAMAGING_FALL);
}

static void TestAttachedRequiresOuterBoundary(void)
{
	int prior_steps;

	for (prior_steps = 0; prior_steps < 3; prior_steps++)
	{
		fixture_t fixture;
		sg_compound_hook_live_host_t host;
		sg_compound_hook_live_state_t state =
			SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
		sg_compound_hook_live_bolt_t bolt;
		sg_compound_hook_live_result_t result;
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		usercmd_t command;
		int step;

		Setup(&fixture, &host, &pose, &observation);
		bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
		for (step = 0; step < prior_steps; step++)
			(void)Step(&state, &host, &pose, &observation, NULL);
		if (prior_steps >= 0)
		{
			result = SG_CompoundHookLivePreStep(&state, &host, &pose,
			                                    &observation, &command);
			CHECK(result.command_ready);
			result = SG_CompoundHookLiveApproveCommand(&state, &command);
			CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		}
		result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4,
		                                     &pose);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
		CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE);
	}
	for (prior_steps = 1; prior_steps <= 3; prior_steps++)
	{
		fixture_t fixture;
		sg_compound_hook_live_host_t host;
		sg_compound_hook_live_state_t state =
			SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
		sg_compound_hook_live_bolt_t bolt;
		sg_compound_hook_live_result_t result;
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		int step;

		Setup(&fixture, &host, &pose, &observation);
		bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
		for (step = 0; step < prior_steps; step++)
			(void)Step(&state, &host, &pose, &observation, NULL);
		CHECK(!state.command_pending);
		result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4,
		                                     &pose);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
		CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE);
	}
	{
		fixture_t fixture;
		sg_compound_hook_live_host_t host;
		sg_compound_hook_live_state_t state =
			SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
		sg_compound_hook_live_bolt_t bolt;
		sg_compound_hook_live_result_t result;
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		int step;

		SetupLateAttach(&fixture, &host, &pose, &observation);
		bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
		for (step = 0; step < 4; step++)
			result = Step(&state, &host, &pose, &observation, NULL);
		CHECK(state.transaction_elapsed_ms == 400);
		CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
		result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4,
		                                     &pose);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	}
}

static void TestHookEventFrameOrder(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_state_t wrong_state;
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	int step;

	Setup(&fixture, &host, &pose, &observation);
	bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
	wrong_state = state;
	result = SG_CompoundHookLiveAttached(&wrong_state, &host, &bolt, 3,
	                                     &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE);
	CHECK(result.replay_reason == SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 200;
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	wrong_state = state;
	result = SG_CompoundHookLivePullApplied(&wrong_state, &host, &bolt, 5,
	                                        &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE);
	CHECK(result.replay_reason == SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	observation.hook_rope_length = 100;
	result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	wrong_state = state;
	result = SG_CompoundHookLiveReleaseApplied(&wrong_state, &host, &bolt,
	                                           4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_CADENCE);
	CHECK(result.replay_reason == SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	result = SG_CompoundHookLiveReleaseApplied(&state, &host, &bolt, 5,
	                                           &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
}

void RunCompoundHookSafetyTests(void)
{
	TestCheckpointAndFirstBoltAuthority();
	TestIssuedCommandFailurePadsBeforeRecovery();
	TestRejectedApprovalDiscardsUnconsumedCommand();
	TestWaitAttachSafetyObservation();
	TestOpeningSafetyObservation();
	TestOpeningAndWaitAttachUseFrameEntryFallSpeed();
	TestAttachedRequiresOuterBoundary();
	TestHookEventFrameOrder();
}
