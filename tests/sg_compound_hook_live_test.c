#include <string.h>

#include "sg_compound_hook_live_fixture.h"

int failures;

static void TestBeginOwnsOneOuterTransaction(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_result_t result = { 0 };
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	Setup(&fixture, &host, &pose, &observation);
	result = SG_CompoundHookLiveBegin(&state, &host, 7, &pose,
	                                  &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.outer.phase == SG_COMPOUND_APPROACH);
	CHECK(state.guard_owned && state.local_owned);
	CHECK(SG_CompoundHookLiveOwns(&state, 7, 12));
	CHECK(fixture.acquire_calls == 1);
}


static void TestNominalHookLifecycle(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_bolt_t wrong;
	sg_compound_hook_live_state_t wrong_state;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	int step;

	Setup(&fixture, &host, &pose, &observation);
	bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
	wrong = bolt;
	wrong.generation++;
	wrong_state = state;
	result = SG_CompoundHookLiveLinked(&wrong_state, &host, &wrong, 3,
	                                   &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	wrong_state = state;
	result = SG_CompoundHookLiveAttached(&wrong_state, &host, &wrong, 4,
	                                     &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 200;
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_PULL);
	wrong_state = state;
	result = SG_CompoundHookLivePullApplied(&wrong_state, &host, &wrong, 4,
	                                        &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	observation.hook_rope_length = 100;
	result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.sweep_clear);
	wrong_state = state;
	result = SG_CompoundHookLiveReleaseApplied(&wrong_state, &host, &wrong,
	                                           5, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	result = SG_CompoundHookLiveReleaseApplied(&state, &host, &bolt, 5,
	                                           &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLiveReleaseApplied(&state, &host, &bolt, 5,
	                                           &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	pose.grounded = true;
	pose.waterlevel = 0;
	pose.watertype = 0;
	observation.contact_clear = true;
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	{
		usercmd_t command;

		result = SG_CompoundHookLivePreStep(&state, &host, &pose,
		                                    &observation, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		CHECK(result.command_ready);
		result = SG_CompoundHookLiveApproveCommand(&state, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		pose.origin[0] = 96.0f;
		pose.pms.origin[0] = 96 * 8;
		result = SG_CompoundHookLiveBoundary(&state, &host, &pose,
		                                     &observation);
	}
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_COMPLETE);
	CHECK(!state.guard_owned && !state.local_owned);
	CHECK(state.outer.phase == SG_COMPOUND_NONE);
	CHECK(fixture.release_calls == 1);
}

static void TestWrongGenerationRetainsLeaseForRecovery(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_bolt_t bolt, wrong;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	Setup(&fixture, &host, &pose, &observation);
	bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
	wrong = bolt;
	wrong.generation++;
	fixture.body_clear = 0;
	fixture.bolt_clear = 0;
	result = SG_CompoundHookLiveAttached(&state, &host, &wrong, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	CHECK(state.guard_owned && state.local_owned);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER);
	CHECK(fixture.release_calls == 0);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY);
	CHECK(fixture.abort_calls == 1);
	CHECK(fixture.body_clear_calls > 0 && fixture.bolt_clear_calls > 0);
	fixture.body_clear = 1;
	fixture.bolt_clear = 1;
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.release_calls == 1);
	CHECK(fixture.abort_calls == 1);
	CHECK(!state.recovering && !state.bolt_linked &&
	      !state.bolt_abort_applied);
}

static void TestDeathOrphansExactBolt(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	Setup(&fixture, &host, &pose, &observation);
	bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
	result = SG_CompoundHookLiveOrphan(&state, &host);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.orphan_calls == 1 && fixture.release_calls == 0);
	CHECK(fixture.orphan_bolt.key == bolt.key);
	CHECK(fixture.orphan_bolt.generation == bolt.generation);
	CHECK(!state.guard_owned && !state.local_owned);
	CHECK(state.outer.phase == SG_COMPOUND_NONE);
}

static void TestPrelinkDeathOrphansPendingBodyAndMover(void)
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
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.command_pending && !state.bolt_linked);
	result = SG_CompoundHookLiveOrphan(&state, &host);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.orphan_calls == 1 && !fixture.orphan_had_bolt);
	CHECK(!state.guard_owned && !state.local_owned);
	CHECK(!state.command_pending && !state.command_approved);
	CHECK(state.outer.phase == SG_COMPOUND_NONE);
}

static void TestReleaseNeedsBodyAndBoltClear(void)
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
	fixture.bolt_clear = 0;
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 200;
	(void)Step(&state, &host, &pose, &observation, NULL);
	(void)Step(&state, &host, &pose, &observation, NULL);
	(void)Step(&state, &host, &pose, &observation, NULL);
	result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 4, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	(void)Step(&state, &host, &pose, &observation, NULL);
	(void)Step(&state, &host, &pose, &observation, NULL);
	(void)Step(&state, &host, &pose, &observation, NULL);
	observation.hook_rope_length = 100;
	result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.sweep_clear);
	result = SG_CompoundHookLiveReleaseApplied(&state, &host, &bolt, 5,
	                                           &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	pose.grounded = true;
	pose.waterlevel = 0;
	pose.watertype = 0;
	observation.contact_clear = true;
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	{
		usercmd_t command;

		result = SG_CompoundHookLivePreStep(&state, &host, &pose,
		                                    &observation, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		result = SG_CompoundHookLiveApproveCommand(&state, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		pose.origin[0] = 96.0f;
		pose.pms.origin[0] = 96 * 8;
		result = SG_CompoundHookLiveBoundary(&state, &host, &pose,
		                                     &observation);
	}
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(fixture.release_calls == 0);
	CHECK(state.guard_owned && state.outer.phase == SG_COMPOUND_RECOVER);
}

static void TestLateAttachStopsAtPublishedTotal(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_bolt_t bolt;
	sg_hook_replay_state_t parked;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;
	int hold_calls, step;

	SetupLateAttach(&fixture, &host, &pose, &observation);
	bolt = DriveToLinked(&fixture, &host, &state, &pose, &observation);
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	parked = state.hook;
	hold_calls = fixture.hold_calls;
	for (step = 0; step < 4; step++)
	{
		result = Step(&state, &host, &pose, &observation, &command);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		CHECK(command.msec == SG_REPLAY_STEP_MS);
		CHECK(command.angles[PITCH] == (short)ANGLE2SHORT(
		      fixture.snapshot.hook_proof.view_angles[PITCH]));
		CHECK(command.angles[YAW] == (short)ANGLE2SHORT(
		      fixture.snapshot.hook_proof.view_angles[YAW]));
		CHECK(command.angles[ROLL] == 0);
		CHECK(command.forwardmove == 0 && command.sidemove == 0 &&
		      command.upmove == 0 && command.buttons == 0);
	}
	CHECK(memcmp(&parked, &state.hook, sizeof(parked)) == 0);
	CHECK(state.transaction_elapsed_ms -
	      state.snapshot.binding.mover_top_ms -
	      state.hook.progress.elapsed_ms == SG_REPLAY_FRAME_MS);
	CHECK(state.transaction_elapsed_ms == 500);
	CHECK(fixture.hold_calls == hold_calls + 1);
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 5, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_ATTACH_FRAME);
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 200;
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_PULL);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 5, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	observation.hook_rope_length = 100;
	result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.sweep_clear);
	result = SG_CompoundHookLiveReleaseApplied(&state, &host, &bolt, 6,
	                                           &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(!result.command_ready);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY);
	CHECK(result.replay_reason == SG_REPLAY_REASON_ACTION_TIMEOUT);
	CHECK(state.transaction_elapsed_ms == 700);
	CHECK(state.guard_owned && fixture.release_calls == 0);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(!state.guard_owned && fixture.release_calls == 1);
}

static void TestMissingAttachTimesOutIntoRetainedRecovery(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;
	int budget_wait, step;

	SetupLateAttach(&fixture, &host, &pose, &observation);
	(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	budget_wait = state.snapshot.binding.total_cost_ms -
	              state.transaction_elapsed_ms;
	for (step = 0; step < budget_wait / SG_REPLAY_STEP_MS; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.transaction_elapsed_ms ==
	      state.snapshot.binding.total_cost_ms);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(!result.command_ready);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY);
	CHECK(result.replay_reason == SG_REPLAY_REASON_ACTION_TIMEOUT);
	CHECK(state.guard_owned && state.local_owned);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER);
	CHECK(fixture.release_calls == 0);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(!state.guard_owned && fixture.release_calls == 1);
}

static void TestPublishedTimeoutAllowsBoundedRecovery(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_result_t result = { 0 };
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;
	int step;

	SetupLateAttach(&fixture, &host, &pose, &observation);
	(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
	while (state.transaction_elapsed_ms <
	       state.snapshot.binding.total_cost_ms)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	fixture.body_clear = 0;
	fixture.bolt_clear = 0;
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(!result.command_ready);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.control == SG_COMPOUND_HOOK_LIVE_CONTROL_RECOVERY);
	CHECK(fixture.abort_calls == 1);
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	VectorCopy(state.snapshot.binding.destination_seed.origin, pose.origin);
	pose.pms.origin[0] = (short)(pose.origin[0] * 8.0f);
	pose.pms.origin[1] = (short)(pose.origin[1] * 8.0f);
	pose.pms.origin[2] = (short)(pose.origin[2] * 8.0f);
	pose.grounded = true;
	pose.waterlevel = 0;
	pose.watertype = 0;
	result = SG_CompoundHookLiveBoundary(&state, &host, &pose,
	                                     &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 800);
	fixture.body_clear = 1;
	fixture.bolt_clear = 1;
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(!state.guard_owned && fixture.release_calls == 1);
	CHECK(fixture.abort_calls == 1);
}


int main(void)
{
	TestBeginOwnsOneOuterTransaction();
	TestNominalHookLifecycle();
	TestWrongGenerationRetainsLeaseForRecovery();
	TestDeathOrphansExactBolt();
	TestPrelinkDeathOrphansPendingBodyAndMover();
	TestReleaseNeedsBodyAndBoltClear();
	TestLateAttachStopsAtPublishedTotal();
	TestMissingAttachTimesOutIntoRetainedRecovery();
	TestPublishedTimeoutAllowsBoundedRecovery();
	RunCompoundHookSafetyTests();
	if (failures)
	{
		fprintf(stderr, "sg_compound_hook_live_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_hook_live_test: ok");
	return 0;
}
