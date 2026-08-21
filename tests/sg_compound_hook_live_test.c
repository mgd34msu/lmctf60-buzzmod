#include <stdio.h>
#include <string.h>

#include "../q_shared.h"
#include "../slipgate/sg_compound_hook_live.h"

static int failures;

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
	sg_compound_hook_live_bolt_t orphan_bolt;
} fixture_t;

static sg_compound_hook_live_host_result_t Bind(void *context,
	uint32_t link_index, sg_compound_hook_live_snapshot_t *snapshot_out)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot_out || link_index !=
	    fixture->snapshot.binding.link_index)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	*snapshot_out = fixture->snapshot;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t AcceptSnapshot(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	return fixture && snapshot &&
	       memcmp(snapshot, &fixture->snapshot, sizeof(*snapshot)) == 0 ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
}

static sg_compound_hook_live_host_result_t Acquire(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	if (AcceptSnapshot(context, snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	fixture->acquire_calls++;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t Hold(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot, int lease_ms)
{
	fixture_t *fixture = (fixture_t *)context;

	if (lease_ms != SG_COMPOUND_HOLD_LEASE_MS ||
	    AcceptSnapshot(context, snapshot) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	fixture->hold_calls++;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t BodyClear(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	fixture_t *fixture = (fixture_t *)context;

	(void)bolt;
	return fixture && fixture->body_clear &&
	       AcceptSnapshot(context, snapshot) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t BoltClear(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	fixture_t *fixture = (fixture_t *)context;

	return fixture && fixture->bolt_clear && bolt && bolt->key == 21 &&
	       bolt->generation == 9001U &&
	       AcceptSnapshot(context, snapshot) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t Release(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	if (AcceptSnapshot(context, snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	fixture->release_calls++;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t Orphan(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!bolt || AcceptSnapshot(context, snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	fixture->orphan_calls++;
	fixture->orphan_bolt = *bolt;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static qboolean HookShadow(const sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	sg_hook_replay_state_t copy;

	if (!state || !pose || !observation || !command)
		return false;
	if (state->phase == SG_HOOK_REPLAY_WAIT_ATTACH)
		return SG_HookReplayFixedViewCommand(pose, state->spec.view_angles,
		                                     command);
	copy = *state;
	return SG_HookReplayPreStep(&copy, pose, observation, command) ==
	       SG_REPLAY_RUNNING;
}

static void Setup(fixture_t *fixture, sg_compound_hook_live_host_t *host,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation)
{
	sg_compound_publication_binding_t *binding;
	sg_hook_replay_spec_t *hook;

	memset(fixture, 0, sizeof(*fixture));
	memset(host, 0, sizeof(*host));
	memset(pose, 0, sizeof(*pose));
	memset(observation, 0, sizeof(*observation));
	binding = &fixture->snapshot.binding;
	hook = &fixture->snapshot.hook_proof;
	binding->link_index = 7;
	binding->mechanism_index = 3;
	binding->link.from = 0;
	binding->link.to = 1;
	binding->link.action = RL_DOOR_HOOK;
	binding->link.provenance = RL_CONTRACTED;
	binding->link.mode = RLCM_PREOPEN;
	binding->link.mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	binding->link.anchor[PITCH] =
		SHORT2ANGLE((short)ANGLE2SHORT(-15.0f));
	binding->link.anchor[YAW] = SHORT2ANGLE(ANGLE2SHORT(90.0f));
	binding->link.anchor[ROLL] = 80.0f;
	binding->link.heading_slack = SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
	binding->link.mechanism_anchor[0] = 8.0f;
	binding->canonical_hint[0] = 8.0f;
	binding->source_seed.origin[0] = 1.0f;
	binding->source_seed.flags = RSF_WATER;
	binding->destination_seed.origin[0] = 96.0f;
	binding->touch_ms = 25;
	binding->touch_frame_end_ms = 100;
	binding->mover_top_ms = 300;
	binding->suffix_start_ms = 200;
	binding->arrival_ms = 300;
	binding->sweep_clear_ms = 200;
	binding->total_cost_ms = 600;
	binding->link.cost_ms = 600;
	binding->link.sweep_clear_ms = 200;
	binding->source.old_frame_z = 0.0f;
	hook->bite[0] = 160.0f;
	hook->bite[2] = 64.0f;
	hook->destination[0] = 96.0f;
	hook->view_angles[PITCH] = binding->link.anchor[PITCH];
	hook->view_angles[YAW] = binding->link.anchor[YAW];
	hook->flight_ms = 100;
	hook->settle_limit_ms = RUNE_HOOK_WATER_SETTLE_MS;
	hook->expected_release_ms = 100;
	hook->expected_pull_ms = 100;
	hook->expected_settle_arrival_ms = 100;
	hook->expected_settle_ms = 100;
	fixture->snapshot.trigger_key = 11;
	fixture->snapshot.mover_key = 12;
	fixture->body_clear = 1;
	fixture->bolt_clear = 1;
	host->context = fixture;
	host->bind = Bind;
	host->acquire = Acquire;
	host->authorize = AcceptSnapshot;
	host->hold_open = Hold;
	host->body_clear = BodyClear;
	host->bolt_clear = BoltClear;
	host->release = Release;
	host->orphan = Orphan;
	host->hook_shadow = HookShadow;
	pose->waterlevel = 2;
	pose->watertype = CONTENTS_WATER;
}

static void SetupLateAttach(fixture_t *fixture,
	sg_compound_hook_live_host_t *host, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	Setup(fixture, host, pose, observation);
	fixture->snapshot.binding.link.anchor[ROLL] = 160.0f;
	fixture->snapshot.binding.arrival_ms = 400;
	fixture->snapshot.binding.sweep_clear_ms = 300;
	fixture->snapshot.binding.total_cost_ms = 700;
	fixture->snapshot.binding.link.cost_ms = 700;
	fixture->snapshot.binding.link.sweep_clear_ms = 300;
	fixture->snapshot.hook_proof.flight_ms = 200;
}

static void TestBeginOwnsOneOuterTransaction(void)
{
	fixture_t fixture;
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_state_t state =
		SG_COMPOUND_HOOK_LIVE_STATE_INITIALIZER;
	sg_compound_hook_live_result_t result;
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

static sg_compound_hook_live_result_t Step(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command_out)
{
	sg_compound_hook_live_result_t result;
	usercmd_t command;
	int next_ms;

	memset(&command, 0, sizeof(command));
	result = SG_CompoundHookLivePreStep(state, host, pose, observation,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING ||
	      result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.command_ready);
	if (command_out)
		*command_out = command;
	result = SG_CompoundHookLiveApproveCommand(state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING ||
	      result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	next_ms = state->transaction_elapsed_ms + SG_REPLAY_STEP_MS;
	return (next_ms % SG_REPLAY_FRAME_MS) == 0 ?
	       SG_CompoundHookLiveBoundary(state, host, pose, observation) :
	       SG_CompoundHookLivePostStep(state, host, pose, observation);
}

static void DriveToTop(fixture_t *fixture,
	sg_compound_hook_live_host_t *host,
	sg_compound_hook_live_state_t *state, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	sg_compound_hook_live_result_t result;
	usercmd_t final_aim;

	result = SG_CompoundHookLiveBegin(state, host, 7, pose, observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = Step(state, host, pose, observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state->transaction_elapsed_ms == 25);
	result = SG_CompoundHookLiveTouch(state, host, 11, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLiveActivate(state, host, 11, 12, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	while (state->transaction_elapsed_ms < 275)
	{
		result = Step(state, host, pose, observation, NULL);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	}
	result = Step(state, host, pose, observation, &final_aim);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(final_aim.angles[PITCH] == (short)
	      ANGLE2SHORT(fixture->snapshot.hook_proof.view_angles[PITCH]));
	CHECK(final_aim.angles[YAW] == (short)
	      ANGLE2SHORT(fixture->snapshot.hook_proof.view_angles[YAW]));
	CHECK(final_aim.angles[ROLL] == 0 && final_aim.msec == 25);
	CHECK(state->outer.phase == SG_COMPOUND_TOP);
}

static sg_compound_hook_live_bolt_t DriveToLinked(fixture_t *fixture,
	sg_compound_hook_live_host_t *host,
	sg_compound_hook_live_state_t *state, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_result_t result;

	DriveToTop(fixture, host, state, pose, observation);
	bolt.key = 21;
	bolt.generation = 9001U;
	result = SG_CompoundHookLiveLinked(state, host, &bolt, 3, pose,
	                                   observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state->outer.phase == SG_COMPOUND_SUFFIX_LEASED);
	CHECK(state->hook.phase ==
	      (fixture->snapshot.hook_proof.flight_ms > SG_REPLAY_FRAME_MS ?
	       SG_HOOK_REPLAY_FLIGHT : SG_HOOK_REPLAY_WAIT_ATTACH));
	result = SG_CompoundHookLiveLinked(state, host, &bolt, 3, pose,
	                                   observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	return bolt;
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
	result = SG_CompoundHookLivePullApplied(&wrong_state, &host, &wrong, 5,
	                                        &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 5, &pose);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 5, &pose);
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
	fixture.body_clear = 1;
	fixture.bolt_clear = 1;
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(fixture.release_calls == 1);
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
	result = SG_CompoundHookLivePullApplied(&state, &host, &bolt, 5, &pose);
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

static void TestLateAttachWaitsOneWholeFrame(void)
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
	result = SG_CompoundHookLiveAttached(&state, &host, &bolt, 4, &pose);
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
	pose.grounded = true;
	pose.waterlevel = 0;
	pose.watertype = 0;
	observation.contact_clear = true;
	for (step = 0; step < 3; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLiveApproveCommand(&state, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	pose.origin[0] = 96.0f;
	pose.pms.origin[0] = 96 * 8;
	result = SG_CompoundHookLiveBoundary(&state, &host, &pose, &observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_COMPLETE);
	CHECK(state.transaction_elapsed_ms == 800);
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
	int allowed_wait, step;

	SetupLateAttach(&fixture, &host, &pose, &observation);
	(void)DriveToLinked(&fixture, &host, &state, &pose, &observation);
	for (step = 0; step < 4; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.hook.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	allowed_wait = SG_REPLAY_HOOK_FLIGHT_MAX_MS - state.hook.flight_body_ms;
	for (step = 0; step < allowed_wait / SG_REPLAY_STEP_MS; step++)
		result = Step(&state, &host, &pose, &observation, NULL);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state.transaction_elapsed_ms -
	      state.snapshot.binding.mover_top_ms -
	      state.hook.progress.elapsed_ms == allowed_wait);
	result = SG_CompoundHookLivePreStep(&state, &host, &pose, &observation,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(!result.command_ready);
	CHECK(result.failure == SG_COMPOUND_HOOK_LIVE_FAILURE_REPLAY);
	CHECK(result.replay_reason == SG_REPLAY_REASON_HOOK_ATTACH_TIMING);
	CHECK(state.guard_owned && state.local_owned);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER);
	CHECK(fixture.release_calls == 0);
	result = SG_CompoundHookLiveRecover(&state, &host, &pose, &observation,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED);
	CHECK(!state.guard_owned && fixture.release_calls == 1);
}

int main(void)
{
	TestBeginOwnsOneOuterTransaction();
	TestNominalHookLifecycle();
	TestWrongGenerationRetainsLeaseForRecovery();
	TestDeathOrphansExactBolt();
	TestReleaseNeedsBodyAndBoltClear();
	TestLateAttachWaitsOneWholeFrame();
	TestMissingAttachTimesOutIntoRetainedRecovery();
	if (failures)
	{
		fprintf(stderr, "sg_compound_hook_live_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_hook_live_test: ok");
	return 0;
}
