#include "sg_compound_hook_live_fixture.h"

#include <string.h>

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
	if (fixture)
		fixture->body_clear_calls++;
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
	if (fixture)
		fixture->bolt_clear_calls++;

	return fixture && fixture->bolt_clear &&
	       (!bolt || (bolt->key == 21 && bolt->generation == 9001U)) &&
	       AcceptSnapshot(context, snapshot) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t SweepSegment(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const vec3_t start, const vec3_t end,
	sg_compound_hook_live_sweep_t *sweep_out)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !start || !end || !sweep_out ||
	    AcceptSnapshot(context, snapshot) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	fixture->sweep_calls++;
	if (fixture->sweep_error_call == fixture->sweep_calls)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	memset(sweep_out, 0, sizeof(*sweep_out));
	sweep_out->start_outside = true;
	sweep_out->end_outside = true;
	if (fixture->sweep_cross_call == fixture->sweep_calls)
		sweep_out->crossed = true;
	if (fixture->sweep_inside_call == fixture->sweep_calls)
	{
		sweep_out->start_outside = false;
		sweep_out->crossed = true;
	}
	if (fixture->sweep_invalid_call == fixture->sweep_calls)
		sweep_out->crossed = 2;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
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

	if (AcceptSnapshot(context, snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	fixture->orphan_calls++;
	fixture->orphan_had_bolt = bolt != NULL;
	if (bolt)
		fixture->orphan_bolt = *bolt;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t SourceCheckpoint(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	fixture_t *fixture = (fixture_t *)context;
	const sg_compound_publication_checkpoint_t *expected;

	if (!fixture)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	expected = &fixture->snapshot.binding.source;

	return fixture && fixture->source_checkpoint && pose && observation &&
	       memcmp(&expected->pms, &pose->pms, sizeof(expected->pms)) == 0 &&
	       expected->grounded == pose->grounded &&
	       expected->watertype == pose->watertype &&
	       expected->waterlevel == pose->waterlevel &&
	       expected->old_frame_z == fixture->source_old_frame_z &&
	       AcceptSnapshot(context, snapshot) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t SuffixCheckpoint(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	fixture_t *fixture = (fixture_t *)context;
	const sg_compound_publication_checkpoint_t *expected;

	if (!fixture)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	expected = &fixture->snapshot.binding.suffix;

	return fixture && fixture->suffix_checkpoint && pose && observation &&
	       memcmp(&expected->pms, &pose->pms, sizeof(expected->pms)) == 0 &&
	       expected->grounded == pose->grounded &&
	       expected->watertype == pose->watertype &&
	       expected->waterlevel == pose->waterlevel &&
	       expected->old_frame_z == fixture->suffix_old_frame_z &&
	       AcceptSnapshot(context, snapshot) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t EventAuthorize(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt)
{
	fixture_t *fixture = (fixture_t *)context;

	return fixture && fixture->event_authorized &&
	       event > SG_COMPOUND_HOOK_LIVE_EVENT_NONE && bolt &&
	       bolt->key == 21 && bolt->generation == 9001U &&
	       AcceptSnapshot(context, snapshot) ==
	           SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t AbortBolt(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !bolt || bolt->key != 21 ||
	    bolt->generation != 9001U ||
	    AcceptSnapshot(context, snapshot) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	fixture->abort_calls++;
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

void Setup(fixture_t *fixture, sg_compound_hook_live_host_t *host,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation)
{
	sg_compound_publication_binding_t *binding;
	sg_hook_replay_spec_t *hook;

	memset(fixture, 0, sizeof(*fixture));
	memset(host, 0, sizeof(*host));
	memset(pose, 0, sizeof(*pose));
	memset(observation, 0, sizeof(*observation));
	binding = &fixture->snapshot.binding;
	hook = &fixture->snapshot.binding.hook_proof.spec;
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
	binding->arrival_ms = 400;
	binding->sweep_clear_ms = 100;
	binding->total_cost_ms = 700;
	binding->link.cost_ms = 700;
	binding->link.sweep_clear_ms = 100;
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
	fixture->source_checkpoint = 1;
	fixture->suffix_checkpoint = 1;
	fixture->event_authorized = 1;
	host->context = fixture;
	host->bind = Bind;
	host->acquire = Acquire;
	host->authorize = AcceptSnapshot;
	host->hold_open = Hold;
	host->body_clear = BodyClear;
	host->bolt_clear = BoltClear;
	host->release = Release;
	host->orphan = Orphan;
	host->abort_bolt = AbortBolt;
	host->source_checkpoint = SourceCheckpoint;
	host->suffix_checkpoint = SuffixCheckpoint;
	host->event_authorize = EventAuthorize;
	host->sweep_segment = SweepSegment;
	host->hook_shadow = HookShadow;
	pose->waterlevel = 2;
	pose->watertype = CONTENTS_WATER;
	binding->source.pms = pose->pms;
	binding->source.grounded = pose->grounded;
	binding->source.waterlevel = pose->waterlevel;
	binding->source.watertype = pose->watertype;
	binding->suffix.pms = pose->pms;
	binding->suffix.pms.origin[0] = (short)(
		binding->link.mechanism_anchor[0] *
		SG_RUNE_PROOF_DOOR_ANCHOR_SCALE);
	binding->suffix.grounded = pose->grounded;
	binding->suffix.waterlevel = pose->waterlevel;
	binding->suffix.watertype = pose->watertype;
}

void SetTouchPose(const fixture_t *fixture, sg_replay_pose_t *pose)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		pose->origin[axis] =
			fixture->snapshot.binding.link.mechanism_anchor[axis];
		pose->pms.origin[axis] = (short)(pose->origin[axis] *
			SG_RUNE_PROOF_DOOR_ANCHOR_SCALE);
	}
}

void SetupLateAttach(fixture_t *fixture,
	sg_compound_hook_live_host_t *host, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	Setup(fixture, host, pose, observation);
	fixture->snapshot.binding.link.anchor[ROLL] = 160.0f;
	fixture->snapshot.binding.arrival_ms = 500;
	fixture->snapshot.binding.sweep_clear_ms = 100;
	fixture->snapshot.binding.total_cost_ms = 800;
	fixture->snapshot.binding.link.cost_ms = 800;
	fixture->snapshot.binding.link.sweep_clear_ms = 100;
	fixture->snapshot.binding.hook_proof.spec.flight_ms = 200;
}

sg_compound_hook_live_result_t Step(
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

void DriveToTop(fixture_t *fixture,
	sg_compound_hook_live_host_t *host,
	sg_compound_hook_live_state_t *state, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	sg_compound_hook_live_result_t result;
	usercmd_t final_aim;
	int final_step_ms, step;

	result = SG_CompoundHookLiveBegin(state, host, 7, pose, observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePreStep(state, host, pose, observation,
	                                    &final_aim);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(result.command_ready);
	result = SG_CompoundHookLiveApproveCommand(state, &final_aim);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	SetTouchPose(fixture, pose);
	result = SG_CompoundHookLiveTouch(state, host, 11, pose, observation, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state->command_pending && state->command_replay_consumed);
	result = SG_CompoundHookLiveActivate(state, host, 11, 12, 1);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	result = SG_CompoundHookLivePostStep(state, host, pose, observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state->transaction_elapsed_ms == 25);
	CHECK(!state->command_pending && !state->command_replay_consumed);
	final_step_ms = fixture->snapshot.binding.touch_frame_end_ms +
	                fixture->snapshot.binding.mover_top_ms -
	                SG_REPLAY_STEP_MS;
	for (step = 0; step < 20 &&
	     state->transaction_elapsed_ms < final_step_ms; step++)
	{
		result = Step(state, host, pose, observation, NULL);
		CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
		if (result.outcome != SG_COMPOUND_HOOK_LIVE_RUNNING)
			break;
	}
	CHECK(state->transaction_elapsed_ms == final_step_ms);
	if (state->transaction_elapsed_ms != final_step_ms)
		return;
	result = Step(state, host, pose, observation, &final_aim);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(final_aim.angles[PITCH] == (short)
	      ANGLE2SHORT(fixture->snapshot.binding.hook_proof.spec.view_angles[PITCH]));
	CHECK(final_aim.angles[YAW] == (short)
	      ANGLE2SHORT(fixture->snapshot.binding.hook_proof.spec.view_angles[YAW]));
	CHECK(final_aim.angles[ROLL] == 0 && final_aim.msec == 25);
	CHECK(state->outer.phase == SG_COMPOUND_TOP);
}

sg_compound_hook_live_bolt_t DriveToLinked(fixture_t *fixture,
	sg_compound_hook_live_host_t *host,
	sg_compound_hook_live_state_t *state, sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	sg_compound_hook_live_bolt_t bolt;
	sg_compound_hook_live_result_t result;

	DriveToTop(fixture, host, state, pose, observation);
	bolt.key = 21;
	bolt.generation = 9001U;
	result = SG_CompoundHookLiveLinked(state, host, &bolt, 4, pose,
	                                   observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(state->outer.phase == SG_COMPOUND_SUFFIX_LEASED);
	CHECK(state->hook.phase ==
	      (fixture->snapshot.binding.hook_proof.spec.flight_ms >
	       SG_REPLAY_FRAME_MS ?
	       SG_HOOK_REPLAY_FLIGHT : SG_HOOK_REPLAY_WAIT_ATTACH));
	result = SG_CompoundHookLiveLinked(state, host, &bolt, 4, pose,
	                                   observation);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	return bolt;
}
