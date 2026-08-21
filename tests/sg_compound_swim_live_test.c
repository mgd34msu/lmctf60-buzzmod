#include "q_shared.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_compound_swim_live.h"

enum fixture_event_e
{
	FIXTURE_BIND = 1,
	FIXTURE_PLAN,
	FIXTURE_ACQUIRE,
	FIXTURE_AUTHORIZE,
	FIXTURE_TOP,
	FIXTURE_SUFFIX,
	FIXTURE_PROOF,
	FIXTURE_OUTSIDE,
	FIXTURE_HOLD,
	FIXTURE_SEGMENT,
	FIXTURE_RELEASE
};

typedef struct fixture_s
{
	sg_compound_swim_live_snapshot_t published;
	sg_compound_swim_live_plan_t planned;
	sg_compound_swim_live_start_t expected_start;
	sg_compound_swim_live_host_t host;
	sg_compound_swim_live_proof_t normal_proof;
	sg_compound_swim_live_proof_t recovery_proof;
	sg_compound_swim_live_host_result_t bind_result;
	sg_compound_swim_live_host_result_t prepare_result;
	sg_compound_swim_live_host_result_t acquire_result;
	sg_compound_swim_live_host_result_t authorize_result;
	sg_compound_swim_live_host_result_t top_result;
	sg_compound_swim_live_host_result_t hold_result;
	sg_compound_swim_live_host_result_t release_result;
	sg_compound_swim_live_host_result_t outside[32];
	sg_compound_swim_live_host_result_t segment[64];
	int outside_count;
	int outside_at;
	int segment_count;
	int segment_at;
	int segment_calls;
	vec3_t last_segment_from;
	vec3_t last_segment_to;
	vec3_t last_outside_origin;
	int events[512];
	int event_count;
	sg_compound_swim_live_event_t lifecycle[16];
	sg_compound_swim_live_failure_t lifecycle_failure[16];
	sg_replay_reason_t lifecycle_replay[16];
	uint32_t lifecycle_link[16];
	int lifecycle_count;
	int acquire_calls;
	int authorize_calls;
	int hold_calls;
	int release_calls;
	int proof_calls;
	int recovery_proof_calls;
	int plan_calls;
	qboolean expected_start_valid;
} fixture_t;

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void FixtureEvent(fixture_t *fixture, int event)
{
	if (fixture->event_count < (int)(sizeof(fixture->events) /
	                                sizeof(fixture->events[0])))
		fixture->events[fixture->event_count++] = event;
}

static void FixtureLifecycle(void *context,
	const sg_compound_swim_live_state_t *state,
	sg_compound_swim_live_event_t event,
	sg_compound_swim_live_failure_t failure,
	sg_replay_reason_t replay_reason)
{
	fixture_t *fixture = context;
	int index = fixture->lifecycle_count;

	CHECK(state != NULL);
	if (!state || index >= 16)
		return;
	fixture->lifecycle[index] = event;
	fixture->lifecycle_failure[index] = failure;
	fixture->lifecycle_replay[index] = replay_reason;
	fixture->lifecycle_link[index] = state->snapshot.binding.link_index;
	fixture->lifecycle_count++;
}

static sg_compound_swim_live_host_result_t FixtureBind(void *context,
	uint32_t link_index, sg_compound_swim_live_snapshot_t *snapshot_out)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_BIND);
	if (fixture->bind_result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return fixture->bind_result;
	if (!snapshot_out || link_index != fixture->published.binding.link_index)
		return SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	*snapshot_out = fixture->published;
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t FixturePlan(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_compound_swim_live_start_t *start,
	sg_compound_swim_live_plan_t *plan_out)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_PLAN);
	CHECK(snapshot != NULL && start != NULL && plan_out != NULL);
	if (!snapshot || !start || !plan_out)
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	fixture->plan_calls++;
	if (fixture->expected_start_valid)
		CHECK(memcmp(start, &fixture->expected_start,
		             sizeof(*start)) == 0);
	if (fixture->prepare_result != SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED)
		return fixture->prepare_result;
	*plan_out = fixture->planned;
	return fixture->prepare_result;
}

static sg_compound_swim_live_host_result_t FixtureSuffix(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_compound_swim_live_plan_t *plan)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_SUFFIX);
	CHECK(snapshot != NULL && plan != NULL);
	CHECK(plan && memcmp(plan, &fixture->planned, sizeof(*plan)) == 0);
	return snapshot && plan ? SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED :
	                          SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
}

static sg_compound_swim_live_host_result_t FixtureAcquire(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_ACQUIRE);
	CHECK(snapshot != NULL);
	fixture->acquire_calls++;
	return fixture->acquire_result;
}

static sg_compound_swim_live_host_result_t FixtureAuthorize(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_AUTHORIZE);
	CHECK(snapshot != NULL);
	fixture->authorize_calls++;
	return fixture->authorize_result;
}

static sg_compound_swim_live_host_result_t FixtureTop(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_TOP);
	CHECK(snapshot != NULL);
	return fixture->top_result;
}

static sg_compound_swim_live_host_result_t FixtureHold(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot, int lease_ms)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_HOLD);
	CHECK(snapshot != NULL);
	CHECK(lease_ms == SG_COMPOUND_HOLD_LEASE_MS);
	fixture->hold_calls++;
	return fixture->hold_result;
}

static sg_compound_swim_live_host_result_t FixtureOutside(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_OUTSIDE);
	CHECK(snapshot != NULL && pose != NULL);
	if (pose)
		VectorCopy(pose->origin, fixture->last_outside_origin);
	if (fixture->outside_at < fixture->outside_count)
		return fixture->outside[fixture->outside_at++];
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t FixtureSegment(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const vec3_t from, const vec3_t to)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_SEGMENT);
	CHECK(snapshot != NULL && from != NULL && to != NULL);
	fixture->segment_calls++;
	if (from && to)
	{
		VectorCopy(from, fixture->last_segment_from);
		VectorCopy(to, fixture->last_segment_to);
	}
	if (fixture->segment_at < fixture->segment_count)
		return fixture->segment[fixture->segment_at++];
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t FixtureProof(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose, qboolean recovery,
	sg_compound_swim_live_proof_t *proof_out)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_PROOF);
	CHECK(snapshot != NULL && pose != NULL && proof_out != NULL);
	fixture->proof_calls++;
	if (recovery)
	{
		fixture->recovery_proof_calls++;
		*proof_out = fixture->recovery_proof;
	}
	else
		*proof_out = fixture->normal_proof;
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t FixtureRelease(void *context,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	FixtureEvent(fixture, FIXTURE_RELEASE);
	CHECK(snapshot != NULL);
	fixture->release_calls++;
	return fixture->release_result;
}

static void FixtureInit(fixture_t *fixture)
{
	sg_compound_publication_binding_t *binding;
	rune_link_t *link;

	memset(fixture, 0, sizeof(*fixture));
	fixture->bind_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture->prepare_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture->acquire_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture->authorize_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture->top_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture->hold_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture->release_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	binding = &fixture->published.binding;
	link = &binding->link;
	binding->link_index = 3;
	link->from = 0;
	link->to = 1;
	link->action = RL_DOOR_SWIM;
	link->provenance = RL_CONTRACTED;
	link->exit_speed = 12;
	link->cost_ms = 800;
	VectorSet(link->mechanism_anchor, 80.0f, 0.0f, 0.0f);
	link->sweep_clear_ms = 200;
	link->mode = RLCM_PREOPEN;
	VectorSet(binding->source_seed.origin, 0.0f, 0.0f, 0.0f);
	binding->source_seed.flags = RSF_WATER;
	VectorSet(binding->destination_seed.origin, 200.0f, 0.0f, 0.0f);
	VectorSet(binding->canonical_hint, 72.0f, 0.0f, 0.0f);
	binding->mechanism_index = 0;
	binding->touch_ms = 25;
	binding->touch_frame_end_ms = 100;
	binding->mover_top_ms = 500;
	binding->suffix_start_ms = 400;
	binding->arrival_ms = 300;
	binding->sweep_clear_ms = 200;
	binding->total_cost_ms = 800;
	VectorCopy(link->mechanism_anchor, fixture->planned.mechanism_anchor);
	fixture->planned.suffix = binding->suffix;
	fixture->planned.touch_ms = binding->touch_ms;
	fixture->planned.touch_frame_end_ms = binding->touch_frame_end_ms;
	fixture->planned.mover_top_ms = binding->mover_top_ms;
	fixture->planned.suffix_start_ms = binding->suffix_start_ms;
	fixture->planned.arrival_ms = binding->arrival_ms;
	fixture->planned.sweep_clear_ms = binding->sweep_clear_ms;
	fixture->planned.total_cost_ms = binding->total_cost_ms;
	fixture->planned.exit_speed = link->exit_speed;
	fixture->published.trigger_key = 21;
	fixture->published.mover_key = 22;
	fixture->normal_proof.arrival_ms = 300;
	fixture->normal_proof.sweep_clear_ms = 200;
	fixture->normal_proof.exit_speed = 12;
	fixture->recovery_proof.arrival_ms = 100;
	fixture->recovery_proof.sweep_clear_ms = 100;
	fixture->recovery_proof.exit_speed = 12;
	fixture->host.context = fixture;
	fixture->host.bind = FixtureBind;
	fixture->host.prepare = FixturePlan;
	fixture->host.suffix_checkpoint = FixtureSuffix;
	fixture->host.acquire = FixtureAcquire;
	fixture->host.authorize = FixtureAuthorize;
	fixture->host.at_top = FixtureTop;
	fixture->host.hold_open = FixtureHold;
	fixture->host.outside_sweep = FixtureOutside;
	fixture->host.sweep_segment_clear = FixtureSegment;
	fixture->host.prove_suffix = FixtureProof;
	fixture->host.release = FixtureRelease;
	fixture->host.transition = FixtureLifecycle;
}

static sg_replay_pose_t FixturePose(float x, qboolean water, float speed)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	VectorSet(pose.origin, x, 0.0f, 0.0f);
	VectorSet(pose.velocity, speed, 0.0f, 0.0f);
	pose.pms.origin[0] = (short)(x * 8.0f);
	pose.pms.velocity[0] = (short)(speed * 8.0f);
	pose.grounded = water ? false : true;
	pose.watertype = water ? CONTENTS_WATER : 0;
	pose.waterlevel = water ? 3 : 0;
	return pose;
}

static sg_compound_swim_live_start_t FixtureStart(
	const sg_replay_pose_t *pose)
{
	sg_compound_swim_live_start_t start;

	memset(&start, 0, sizeof(start));
	start.pose = *pose;
	start.old_pms = pose->pms;
	start.old_pms.origin[0] += 8;
	start.old_pms.delta_angles[YAW] = 1234;
	start.old_frame_z = -17.0f;
	return start;
}

static sg_compound_swim_live_result_t FixtureBegin(
	sg_compound_swim_live_state_t *state, fixture_t *fixture,
	uint32_t link_index, const sg_replay_pose_t *pose)
{
	fixture->expected_start = FixtureStart(pose);
	fixture->expected_start_valid = true;
	return SG_CompoundSwimLiveBegin(state, &fixture->host, link_index,
	                                &fixture->expected_start);
}

static sg_replay_observation_t FixtureObservation(qboolean contact_clear)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.contact_clear = contact_clear;
	return observation;
}

static sg_compound_swim_live_result_t FixtureRunStep(
	sg_compound_swim_live_state_t *state, fixture_t *fixture,
	const sg_replay_pose_t *before, const sg_replay_pose_t *after,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	sg_compound_swim_live_result_t result;

	result = SG_CompoundSwimLivePreStep(state, &fixture->host, before,
	                                    command);
	CHECK(result.command_ready);
	if (!result.command_ready)
		return result;
	result = SG_CompoundSwimLivePostStep(state, &fixture->host, after,
	                                     observation);
	if (state->command_pending ||
	    (state->transaction_elapsed_ms > 0 &&
	     state->transaction_elapsed_ms % SG_REPLAY_FRAME_MS == 0 &&
	     state->last_boundary_ms != state->transaction_elapsed_ms))
		result = SG_CompoundSwimLiveBoundary(state, &fixture->host, after,
		                                     observation);
	return result;
}

static void FixtureEnterOpening(sg_compound_swim_live_state_t *state,
	fixture_t *fixture, const sg_replay_pose_t *source,
	const sg_replay_pose_t *mechanism,
	const sg_replay_observation_t *blocked)
{
	sg_compound_swim_live_result_t result;
	usercmd_t command;

	result = FixtureBegin(state, fixture, 3, source);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLivePreStep(state, &fixture->host, source,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundSwimLiveAuthorizeTouch(state, &fixture->host, 21,
	                                           mechanism, 17);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLiveAuthorizeActivation(state, &fixture->host,
	                                                21, 22, 17);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLivePostStep(state, &fixture->host, mechanism,
	                                     blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(state->outer.phase == SG_COMPOUND_OPENING);
	CHECK(state->transaction_elapsed_ms == SG_REPLAY_STEP_MS);
}

static void FixtureEnterSuffix(sg_compound_swim_live_state_t *state,
	fixture_t *fixture, const sg_replay_pose_t *source,
	const sg_replay_pose_t *mechanism,
	const sg_replay_observation_t *blocked)
{
	sg_compound_swim_live_result_t result;
	usercmd_t command;
	int step;

	FixtureEnterOpening(state, fixture, source, mechanism, blocked);
	for (step = 0; step < 19; step++)
	{
		result = FixtureRunStep(state, fixture, mechanism, mechanism,
		                        blocked, &command);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	}
	CHECK(state->outer.phase == SG_COMPOUND_SUFFIX_LEASED);
	CHECK(state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_SUFFIX);
	CHECK(state->transaction_elapsed_ms == 500);
}

static void FixtureEnterRecoveryReplay(
	sg_compound_swim_live_state_t *state, fixture_t *fixture,
	const sg_replay_pose_t *source, const sg_replay_pose_t *mechanism,
	const sg_replay_observation_t *blocked)
{
	sg_compound_swim_live_result_t result;
	usercmd_t command;
	int step;

	result = FixtureBegin(state, fixture, 3, source);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLivePreStep(state, &fixture->host, source,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundSwimLiveAuthorizeTouch(state, &fixture->host, 999,
	                                           mechanism, 3);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	result = SG_CompoundSwimLivePostStep(state, &fixture->host, mechanism,
	                                     blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	for (step = 0; step < 3; step++)
	{
		result = FixtureRunStep(state, fixture, mechanism, mechanism,
		                        blocked, &command);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	}
	CHECK(state->transaction_elapsed_ms == SG_REPLAY_FRAME_MS);
	CHECK(state->last_boundary_ms == SG_REPLAY_FRAME_MS);
	result = SG_CompoundSwimLiveRecover(state, &fixture->host, mechanism,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(state->replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY);
}

static int FixtureFirstEvent(const fixture_t *fixture, int event)
{
	int index;

	for (index = 0; index < fixture->event_count; index++)
		if (fixture->events[index] == event)
			return index;
	return -1;
}

static void TestExactDoorSwimTransaction(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, before, after;
	sg_replay_observation_t blocked, clear;
	usercmd_t command;
	int step;

	FixtureInit(&fixture);
	fixture.outside[0] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside[1] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.outside[2] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside[3] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside_count = 4;
	/* The complete touch-ending approach command is checked before mutation,
	 * and all 19 opening zero commands are checked outside.  The twelve suffix
	 * commands follow, with contact through the first 100 ms. */
	for (step = 0; step < 20; step++)
		fixture.segment[step] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	for (; step < 24; step++)
		fixture.segment[step] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	for (; step < 32; step++)
		fixture.segment[step] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.segment_count = 32;
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	clear = FixtureObservation(true);
	result = FixtureBegin(&state, &fixture, 3, &source);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(state.outer.phase == SG_COMPOUND_APPROACH);
	CHECK(fixture.acquire_calls == 1 && fixture.authorize_calls == 0);
	CHECK(FixtureFirstEvent(&fixture, FIXTURE_BIND) <
	      FixtureFirstEvent(&fixture, FIXTURE_ACQUIRE));
	CHECK(FixtureFirstEvent(&fixture, FIXTURE_ACQUIRE) <
	      FixtureFirstEvent(&fixture, FIXTURE_PLAN));

	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
	                                    &command);
	CHECK(result.command_ready && command.msec == SG_REPLAY_STEP_MS);
	CHECK(command.forwardmove == 400);
	result = SG_CompoundSwimLiveAuthorizeTouch(&state, &fixture.host, 21,
	                                           &mechanism, 17);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLiveAuthorizeActivation(&state, &fixture.host,
	                                                21, 22, 17);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(state.transaction_elapsed_ms == 25);
	CHECK(state.outer.phase == SG_COMPOUND_OPENING);
	CHECK(state.replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_NONE);

	for (step = 0; step < 19; step++)
	{
		result = FixtureRunStep(&state, &fixture, &mechanism, &mechanism,
		                        &blocked, &command);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
		CHECK(command.forwardmove == 0 && command.sidemove == 0 &&
		      command.upmove == 0);
	}
	CHECK(state.transaction_elapsed_ms == 500);
	CHECK(state.outer.phase == SG_COMPOUND_SUFFIX_LEASED);
	CHECK(state.replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_SUFFIX);
	CHECK(fixture.segment_calls == 20);
	CHECK(fixture.proof_calls == 1 && fixture.hold_calls == 1);

	before = mechanism;
	for (step = 1; step <= 12; step++)
	{
		float x = step <= 4 ? 120.0f : (step <= 8 ? 160.0f :
		          (step < 12 ? 180.0f : 200.0f));
		qboolean terminal = step == 12 ? true : false;

		after = FixturePose(x, terminal ? false : true,
		                    terminal ? 48.0f : 0.0f);
		result = FixtureRunStep(&state, &fixture, &before, &after,
		                        terminal ? &clear : &blocked, &command);
		before = after;
		if (step < 12)
			CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
		if (step == 4)
			CHECK(!state.sweep_clear && !state.arrived &&
			      fixture.release_calls == 0);
		if (step == 8)
			CHECK(state.sweep_clear && !state.arrived &&
			      fixture.release_calls == 0);
	}
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_COMPLETE);
	CHECK(state.outer.phase == SG_COMPOUND_NONE);
	CHECK(!state.guard_owned);
	CHECK(fixture.segment_at == 32 && fixture.segment_calls == 32 &&
	      fixture.outside_at == 4);
	CHECK(fixture.hold_calls == 2); /* suffix elapsed 0 and 100, not 200 */
	CHECK(fixture.release_calls == 1);
	CHECK(FixtureFirstEvent(&fixture, FIXTURE_ACQUIRE) <
	      FixtureFirstEvent(&fixture, FIXTURE_AUTHORIZE));
	CHECK(FixtureFirstEvent(&fixture, FIXTURE_HOLD) <
	      FixtureFirstEvent(&fixture, FIXTURE_RELEASE));
	CHECK(fixture.lifecycle_count == 7);
	CHECK(fixture.lifecycle[0] == SG_COMPOUND_SWIM_LIVE_EVENT_BEGIN);
	CHECK(fixture.lifecycle[1] == SG_COMPOUND_SWIM_LIVE_EVENT_TOUCH);
	CHECK(fixture.lifecycle[2] == SG_COMPOUND_SWIM_LIVE_EVENT_ACTIVATION);
	CHECK(fixture.lifecycle[3] == SG_COMPOUND_SWIM_LIVE_EVENT_TOP);
	CHECK(fixture.lifecycle[4] == SG_COMPOUND_SWIM_LIVE_EVENT_SWEEP_CLEAR);
	CHECK(fixture.lifecycle[5] == SG_COMPOUND_SWIM_LIVE_EVENT_ARRIVAL);
	CHECK(fixture.lifecycle[6] == SG_COMPOUND_SWIM_LIVE_EVENT_COMPLETE);
	for (step = 0; step < fixture.lifecycle_count; step++)
	{
		CHECK(fixture.lifecycle_link[step] == 3U);
		CHECK(fixture.lifecycle_failure[step] ==
		      SG_COMPOUND_SWIM_LIVE_FAILURE_NONE);
		CHECK(fixture.lifecycle_replay[step] == SG_REPLAY_REASON_NONE);
	}
}

static void TestDoorSwimIsTheOnlyFixtureAdmission(void)
{
	int action;

	for (action = RL_RUN; action <= RL_DOOR_HOOK; action++)
	{
		fixture_t fixture;
		sg_compound_swim_live_state_t state =
			SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
		sg_compound_swim_live_result_t result;
		sg_replay_pose_t source;

		FixtureInit(&fixture);
		fixture.published.binding.link.action = (byte)action;
		source = FixturePose(0.0f, true, 0.0f);
		result = FixtureBegin(&state, &fixture, 3, &source);
		if (action == RL_DOOR_SWIM)
		{
			CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
			CHECK(fixture.acquire_calls == 1 && state.guard_owned);
		}
		else
		{
			CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_REJECTED);
			CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_PLAN);
			CHECK(fixture.acquire_calls == 0 && !state.guard_owned);
		}
	}
	{
		fixture_t fixture;
		sg_compound_swim_live_state_t state =
			SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
		sg_replay_pose_t source;
		sg_compound_swim_live_result_t result;

		FixtureInit(&fixture);
		fixture.acquire_result = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
		source = FixturePose(0.0f, true, 0.0f);
		result = FixtureBegin(&state, &fixture, 3, &source);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_WAIT);
		CHECK(!state.guard_owned && state.outer.phase == SG_COMPOUND_NONE);
	}
}

static void TestMutationBoundariesFailClosed(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_snapshot_t original;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source;
	usercmd_t command;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	CHECK(FixtureBegin(&state, &fixture, 3, &source).outcome ==
	      SG_COMPOUND_SWIM_LIVE_RUNNING);
	original = fixture.published;
	fixture.published.binding.total_cost_ms += 100;
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY);
	CHECK(!result.command_ready);
	CHECK(state.guard_owned && state.outer.phase == SG_COMPOUND_RECOVER);
	CHECK(fixture.release_calls == 0);

	/* Ownership does not fall through to an ordinary command while identity is
	 * stale.  Recovery remains leased until the exact snapshot is available. */
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(!result.command_ready && state.guard_owned);
	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &source, 0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(fixture.release_calls == 0 && state.guard_owned);

	fixture.published = original;
	fixture.outside[0] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside_count = 1;
	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &source, 0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED);
	CHECK(fixture.release_calls == 1 && !state.guard_owned);
	CHECK(fixture.lifecycle_count == 3);
	CHECK(fixture.lifecycle[0] == SG_COMPOUND_SWIM_LIVE_EVENT_BEGIN);
	CHECK(fixture.lifecycle[1] == SG_COMPOUND_SWIM_LIVE_EVENT_RECOVERY);
	CHECK(fixture.lifecycle[2] == SG_COMPOUND_SWIM_LIVE_EVENT_COMPLETE);
	CHECK(fixture.lifecycle_failure[2] ==
	      SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY);
}

static void TestRecoveryRetainsMover(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, before, after;
	sg_replay_observation_t blocked, clear;
	usercmd_t command;
	int step;

	FixtureInit(&fixture);
	fixture.outside[0] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.outside[1] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside_count = 2;
	/* The aborted approach command and three zero padding commands are still
	 * swept, then the four proved recovery commands observe contact. */
	for (step = 0; step < 8; step++)
		fixture.segment[step] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.segment_count = 8;
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	clear = FixtureObservation(true);
	CHECK(FixtureBegin(&state, &fixture, 3, &source).outcome ==
	      SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
	                                 &command).command_ready);
	result = SG_CompoundSwimLiveAuthorizeTouch(&state, &fixture.host, 999,
	                                           &mechanism, 3);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(state.guard_owned && state.outer.phase == SG_COMPOUND_RECOVER);
	CHECK(fixture.release_calls == 0);

	/* The rejected trigger occurred inside an already-emitted 25 ms command.
	 * Recovery cannot reproof until that command and the rest of its server
	 * frame have been consumed under retained ownership. */
	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &mechanism,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE);
	CHECK(fixture.hold_calls == 0 && state.command_pending);
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(state.transaction_elapsed_ms == 25 && !state.command_pending);
	for (step = 0; step < 3; step++)
	{
		result = FixtureRunStep(&state, &fixture, &mechanism, &mechanism,
		                        &blocked, &command);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	}
	CHECK(state.transaction_elapsed_ms == 100 &&
	      state.last_boundary_ms == 100);

	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &mechanism,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(state.replay_kind == SG_COMPOUND_SWIM_LIVE_REPLAY_RECOVERY);
	CHECK(fixture.hold_calls == 1 && fixture.recovery_proof_calls == 1);
	CHECK(state.guard_owned);

	before = mechanism;
	for (step = 1; step <= 4; step++)
	{
		qboolean terminal = step == 4 ? true : false;
		float x = terminal ? 200.0f : 100.0f + (float)step * 20.0f;

		after = FixturePose(x, terminal ? false : true,
		                    terminal ? 48.0f : 0.0f);
		result = FixtureRunStep(&state, &fixture, &before, &after,
		                        terminal ? &clear : &blocked, &command);
		before = after;
	}
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED);
	CHECK(!state.guard_owned && state.outer.phase == SG_COMPOUND_NONE);
	CHECK(fixture.release_calls == 1);
	CHECK(fixture.outside_at == 2 && fixture.segment_at == 8);
}

static void TestReleaseRetryRequiresCurrentOutsideSweep(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, before, after, retry_pose;
	sg_replay_observation_t blocked, clear;
	usercmd_t command;
	const sg_compound_swim_live_host_result_t rejected[3] = {
		SG_COMPOUND_SWIM_LIVE_HOST_DENIED,
		SG_COMPOUND_SWIM_LIVE_HOST_ERROR,
		(sg_compound_swim_live_host_result_t)7
	};
	int rejected_index;
	int step;

	FixtureInit(&fixture);
	fixture.release_result = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	/* Recovery first observes contact, then a clear terminal pose.  Each
	 * RELEASE_READY retry must consume its own current-pose result. */
	fixture.outside[0] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.outside[1] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside[2] = rejected[0];
	fixture.outside[3] = rejected[1];
	fixture.outside[4] = rejected[2];
	fixture.outside[5] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.outside_count = 6;
	for (step = 0; step < 8; step++)
		fixture.segment[step] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.segment_count = 8;
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	clear = FixtureObservation(true);
	FixtureEnterRecoveryReplay(&state, &fixture, &source, &mechanism,
	                           &blocked);

	before = mechanism;
	for (step = 1; step <= 4; step++)
	{
		qboolean terminal = step == 4 ? true : false;
		float x = terminal ? 200.0f : 100.0f + (float)step * 20.0f;

		after = FixturePose(x, terminal ? false : true,
		                    terminal ? 48.0f : 0.0f);
		result = FixtureRunStep(&state, &fixture, &before, &after,
		                        terminal ? &clear : &blocked, &command);
		before = after;
	}
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_RELEASE);
	CHECK(state.outer.phase == SG_COMPOUND_RELEASE_READY);
	CHECK(state.guard_owned && fixture.release_calls == 1);
	CHECK(fixture.outside_at == 2);

	for (rejected_index = 0; rejected_index < 3; rejected_index++)
	{
		retry_pose = FixturePose(190.0f - (float)rejected_index * 10.0f,
		                         true, 0.0f);
		fixture.event_count = 0;
		result = SG_CompoundSwimLiveRecover(&state, &fixture.host,
		                                    &retry_pose, 0.0f);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
		CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP);
		CHECK(result.replay_reason == SG_REPLAY_REASON_INVALID_STATE);
		CHECK(state.outer.phase == SG_COMPOUND_RELEASE_READY);
		CHECK(state.guard_owned &&
		      SG_CompoundSwimLiveOwns(&state, 3, 22));
		CHECK(fixture.release_calls == 1);
		CHECK(fixture.outside_at == 3 + rejected_index);
		CHECK(fixture.last_outside_origin[0] == retry_pose.origin[0]);
		CHECK(fixture.event_count > 0 &&
		      fixture.events[fixture.event_count - 1] == FIXTURE_OUTSIDE);
	}

	fixture.release_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	retry_pose = FixturePose(220.0f, false, 48.0f);
	fixture.event_count = 0;
	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &retry_pose,
	                                    0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_NONE);
	CHECK(!state.guard_owned && state.outer.phase == SG_COMPOUND_NONE);
	CHECK(fixture.outside_at == 6 && fixture.release_calls == 2);
	CHECK(fixture.last_outside_origin[0] == retry_pose.origin[0]);
	CHECK(fixture.event_count >= 2 &&
	      fixture.events[fixture.event_count - 2] == FIXTURE_OUTSIDE &&
	      fixture.events[fixture.event_count - 1] == FIXTURE_RELEASE);
}

static void TestInitializerStartsUnowned(void)
{
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;

	CHECK(state.outer.phase == SG_COMPOUND_NONE);
	CHECK(!state.guard_owned && !state.command_pending);
	CHECK(!state.direct_command_pending && !state.aborted_command_pending);
	CHECK(!SG_CompoundSwimLiveOwns(&state, 3, 22));
}

static void TestTouchEndingSweepPrecedesMutation(void)
{
	const sg_compound_swim_live_host_result_t host_results[2] = {
		SG_COMPOUND_SWIM_LIVE_HOST_DENIED,
		SG_COMPOUND_SWIM_LIVE_HOST_ERROR
	};
	const sg_replay_reason_t reasons[2] = {
		SG_REPLAY_REASON_TIMING_MISMATCH,
		SG_REPLAY_REASON_INVALID_STATE
	};
	int test_case;

	for (test_case = 0; test_case < 2; test_case++)
	{
		fixture_t fixture;
		sg_compound_swim_live_state_t state =
			SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
		sg_compound_swim_live_result_t result;
		sg_replay_pose_t source, mechanism;
		usercmd_t command;

		FixtureInit(&fixture);
		fixture.segment[0] = host_results[test_case];
		fixture.segment_count = 1;
		source = FixturePose(0.0f, true, 0.0f);
		mechanism = FixturePose(80.0f, true, 0.0f);
		CHECK(FixtureBegin(&state, &fixture, 3, &source).outcome ==
		      SG_COMPOUND_SWIM_LIVE_RUNNING);
		CHECK(SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
		                                    &command).command_ready);
		result = SG_CompoundSwimLiveAuthorizeTouch(&state, &fixture.host,
		                                           21, &mechanism, 17);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
		CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP);
		CHECK(result.replay_reason == reasons[test_case]);
		CHECK(state.outer.phase == SG_COMPOUND_RECOVER);
		CHECK(state.touch_frame_serial == 0);
		CHECK(state.command_pending && state.aborted_command_pending);
		CHECK(state.guard_owned &&
		      SG_CompoundSwimLiveOwns(&state, 3, 22));
		CHECK(fixture.segment_calls == 1 && fixture.segment_at == 1);
		CHECK(fixture.last_segment_from[0] == 0.0f &&
		      fixture.last_segment_to[0] == 80.0f);

		/* A failed sweep never publishes TOUCH, so activation cannot be
		 * authorized after either a contact or a host error. */
		result = SG_CompoundSwimLiveAuthorizeActivation(&state,
		                                                &fixture.host,
		                                                21, 22, 17);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
		CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_ACTIVATION);
		CHECK(state.outer.phase == SG_COMPOUND_RECOVER);
		CHECK(fixture.segment_calls == 1 && fixture.release_calls == 0);
	}
}

static void TestRepeatedAuthenticatedTouchIsIdempotent(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, overlap;
	sg_replay_observation_t blocked;
	int lifecycle_count;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	overlap = FixturePose(81.25f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterOpening(&state, &fixture, &source, &mechanism, &blocked);
	lifecycle_count = fixture.lifecycle_count;
	result = SG_CompoundSwimLiveAuthorizeTouch(&state, &fixture.host, 21,
	                                           &overlap, 18);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_NONE);
	CHECK(state.outer.phase == SG_COMPOUND_OPENING && state.guard_owned);
	CHECK(fixture.lifecycle_count == lifecycle_count);

	result = SG_CompoundSwimLiveAuthorizeTouch(&state, &fixture.host, 23,
	                                           &overlap, 18);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_TOUCH);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
}

static void TestMalformedSegmentResultFailsClosed(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism;
	usercmd_t command;

	FixtureInit(&fixture);
	fixture.segment[0] = (sg_compound_swim_live_host_result_t)7;
	fixture.segment_count = 1;
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	CHECK(FixtureBegin(&state, &fixture, 3, &source).outcome ==
	      SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
	                                &command).command_ready);
	result = SG_CompoundSwimLiveAuthorizeTouch(&state, &fixture.host, 21,
	                                           &mechanism, 17);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP);
	CHECK(result.replay_reason == SG_REPLAY_REASON_INVALID_STATE);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
	CHECK(state.command_pending && state.aborted_command_pending);
	CHECK(fixture.segment_calls == 1 && fixture.release_calls == 0);
}

static void TestOpeningZeroSegmentDenied(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism;
	sg_replay_observation_t blocked;
	usercmd_t command;

	FixtureInit(&fixture);
	fixture.segment[0] = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	fixture.segment[1] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.segment_count = 2;
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterOpening(&state, &fixture, &source, &mechanism, &blocked);
	CHECK(fixture.segment_calls == 1);
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready);
	CHECK(command.forwardmove == 0 && command.sidemove == 0 &&
	      command.upmove == 0);
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_SWEEP);
	CHECK(result.replay_reason == SG_REPLAY_REASON_TIMING_MISMATCH);
	CHECK(fixture.segment_calls == 2 && fixture.segment_at == 2);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
	CHECK(!state.command_pending && fixture.release_calls == 0);
}

static void TestOpeningHoldUsesSwimReplayCommand(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, drifted;
	sg_replay_observation_t blocked;
	usercmd_t command, expected, zero;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	drifted = FixturePose(81.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterOpening(&state, &fixture, &source, &mechanism, &blocked);
	memset(&zero, 0, sizeof(zero));
	zero.msec = SG_REPLAY_STEP_MS;
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready && state.direct_command_pending);
	CHECK(memcmp(&command, &zero, sizeof(command)) == 0);
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &drifted,
	                                    &command);
	CHECK(result.command_ready && state.direct_command_pending);
	CHECK(SG_SwimReplayCommand(&drifted, fixture.planned.mechanism_anchor,
	                           SG_SWIM_REPLAY_EGRESS, &expected));
	CHECK(memcmp(&command, &expected, sizeof(command)) == 0 &&
	      command.forwardmove == 400);
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
}

static void CheckOpeningZeroObservationFailure(qboolean invalid_pose,
	qboolean contaminated, qboolean door_passed,
	sg_replay_reason_t expected_reason)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, after;
	sg_replay_observation_t observation;
	usercmd_t command;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	observation = FixtureObservation(false);
	FixtureEnterOpening(&state, &fixture, &source, &mechanism,
	                     &observation);
	CHECK(fixture.segment_calls == 1);
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready);
	after = mechanism;
	if (invalid_pose)
		after.origin[0] = NAN;
	observation.contaminated = contaminated;
	observation.door_passed = door_passed;
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &after,
	                                     &observation);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY);
	CHECK(result.replay_reason == expected_reason);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
	CHECK(SG_CompoundSwimLiveOwns(&state, 3, 22));
	CHECK(!state.command_pending && fixture.release_calls == 0);
	/* Invalid endpoints cannot be submitted to the host sweep callback. */
	CHECK(fixture.segment_calls == (invalid_pose ? 1 : 2));
}

static void TestOpeningZeroObservationFailuresRetainOwnership(void)
{
	CheckOpeningZeroObservationFailure(false, true, false,
	                                  SG_REPLAY_REASON_CONTAMINATED);
	CheckOpeningZeroObservationFailure(false, false, true,
	                                  SG_REPLAY_REASON_DOOR_PASSED);
	CheckOpeningZeroObservationFailure(true, false, false,
	                                  SG_REPLAY_REASON_NONFINITE_POSE);
}

static void TestZeroBoundaryConsumesPostPusherState(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, pushed;
	sg_replay_observation_t blocked, pre_pusher, post_pusher;
	usercmd_t command;
	int segment_calls;
	int step;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	pushed = FixturePose(81.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterOpening(&state, &fixture, &source, &mechanism, &blocked);
	for (step = 0; step < 2; step++)
	{
		result = FixtureRunStep(&state, &fixture, &mechanism, &mechanism,
		                        &blocked, &command);
		CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	}
	CHECK(state.transaction_elapsed_ms == 75);
	segment_calls = fixture.segment_calls;
	CHECK(segment_calls == 3);
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready && state.command_pending);
	CHECK(command.forwardmove == 0 && command.sidemove == 0 &&
	      command.upmove == 0);
	pre_pusher = FixtureObservation(false);
	pre_pusher.door_passed = true;
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &pre_pusher);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(state.command_pending && state.transaction_elapsed_ms == 75);
	CHECK(fixture.segment_calls == segment_calls);

	post_pusher = FixtureObservation(false);
	post_pusher.contaminated = true;
	result = SG_CompoundSwimLiveBoundary(&state, &fixture.host, &pushed,
	                                     &post_pusher);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_REPLAY);
	CHECK(result.replay_reason == SG_REPLAY_REASON_CONTAMINATED);
	CHECK(!state.command_pending && state.transaction_elapsed_ms == 100);
	CHECK(fixture.segment_calls == segment_calls + 1);
	CHECK(fixture.last_segment_from[0] == 80.0f &&
	      fixture.last_segment_to[0] == 81.0f);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
}

static void TestSuffixAuthorityFailureStillConsumesSegment(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, after;
	sg_replay_observation_t blocked;
	usercmd_t command;
	int segment_calls;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	after = FixturePose(100.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterSuffix(&state, &fixture, &source, &mechanism, &blocked);
	segment_calls = fixture.segment_calls;
	CHECK(segment_calls == 20);
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready);
	fixture.authorize_result = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &after,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY);
	CHECK(state.command_pending && state.aborted_command_pending);
	CHECK(fixture.segment_calls == segment_calls);
	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &after, 0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE);
	CHECK(state.command_pending && fixture.segment_calls == segment_calls);
	fixture.authorize_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &after,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(!state.command_pending && !state.aborted_command_pending);
	CHECK(fixture.segment_calls == segment_calls + 1);
	CHECK(fixture.last_segment_from[0] == 80.0f &&
	      fixture.last_segment_to[0] == 100.0f);
	CHECK(state.transaction_elapsed_ms == 525);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
	CHECK(fixture.release_calls == 0);
}

static void TestRecoveryAuthorityFailureStillConsumesSegment(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism, after;
	sg_replay_observation_t blocked;
	usercmd_t command;
	int segment_calls;

	FixtureInit(&fixture);
	fixture.outside[0] = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	fixture.outside_count = 1;
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	after = FixturePose(120.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterRecoveryReplay(&state, &fixture, &source, &mechanism,
	                           &blocked);
	segment_calls = fixture.segment_calls;
	CHECK(segment_calls == 4);
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready);
	fixture.authorize_result = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &after,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_AUTHORITY);
	CHECK(state.command_pending && state.aborted_command_pending);
	CHECK(fixture.segment_calls == segment_calls);
	result = SG_CompoundSwimLiveRecover(&state, &fixture.host, &after, 0.0f);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_CADENCE);
	CHECK(state.command_pending && fixture.segment_calls == segment_calls);
	fixture.authorize_result = SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &after,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(!state.command_pending && !state.aborted_command_pending);
	CHECK(fixture.segment_calls == segment_calls + 1);
	CHECK(fixture.last_segment_from[0] == 80.0f &&
	      fixture.last_segment_to[0] == 120.0f);
	CHECK(state.transaction_elapsed_ms == 125);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
	CHECK(fixture.release_calls == 0);
}

static void TestElapsedOverflowFailsOwned(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source, mechanism;
	sg_replay_observation_t blocked;
	usercmd_t command;
	const int near_int_max = INT_MAX - SG_REPLAY_STEP_MS + 1;
	int segment_calls;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	mechanism = FixturePose(80.0f, true, 0.0f);
	blocked = FixtureObservation(false);
	FixtureEnterOpening(&state, &fixture, &source, &mechanism, &blocked);
	state.transaction_elapsed_ms = near_int_max;
	segment_calls = fixture.segment_calls;
	result = SG_CompoundSwimLivePreStep(&state, &fixture.host, &mechanism,
	                                    &command);
	CHECK(result.command_ready && state.command_pending);
	result = SG_CompoundSwimLivePostStep(&state, &fixture.host, &mechanism,
	                                     &blocked);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_TIMING);
	CHECK(result.replay_reason == SG_REPLAY_REASON_ACTION_TIMEOUT);
	CHECK(state.transaction_elapsed_ms == near_int_max);
	CHECK(!state.command_pending && fixture.segment_calls == segment_calls + 1);
	CHECK(state.outer.phase == SG_COMPOUND_RECOVER && state.guard_owned);
	CHECK(SG_CompoundSwimLiveOwns(&state, 3, 22));
	CHECK(fixture.release_calls == 0);
}

static void TestDynamicPlanIsPreparedAfterOwnership(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_state_t before;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source;

	FixtureInit(&fixture);
	VectorSet(fixture.planned.mechanism_anchor, 88.0f, 4.0f, -2.0f);
	fixture.planned.touch_ms = 50;
	fixture.planned.touch_frame_end_ms = 100;
	fixture.planned.mover_top_ms = 600;
	fixture.planned.suffix_start_ms = 500;
	fixture.planned.arrival_ms = 300;
	fixture.planned.sweep_clear_ms = 200;
	fixture.planned.total_cost_ms = 900;
	fixture.planned.exit_speed = 17;
	fixture.planned.suffix.old_frame_z = -33.0f;
	source = FixturePose(12.0f, true, 48.0f);
	result = FixtureBegin(&state, &fixture, 3, &source);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(fixture.plan_calls == 1 && fixture.acquire_calls == 1);
	CHECK(FixtureFirstEvent(&fixture, FIXTURE_ACQUIRE) <
	      FixtureFirstEvent(&fixture, FIXTURE_PLAN));
	CHECK(memcmp(&state.plan, &fixture.planned,
	             sizeof(state.plan)) == 0);
	CHECK(state.plan.touch_ms != fixture.published.binding.touch_ms);
	CHECK(state.plan.total_cost_ms !=
	      fixture.published.binding.total_cost_ms);

	memset(&state, 0, sizeof(state));
	before = state;
	fixture.event_count = 0;
	fixture.acquire_calls = 0;
	fixture.plan_calls = 0;
	fixture.release_calls = 0;
	fixture.prepare_result = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	result = FixtureBegin(&state, &fixture, 3, &source);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_WAIT);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_PLAN);
	CHECK(fixture.plan_calls == 1 && fixture.acquire_calls == 1);
	CHECK(fixture.release_calls == 1);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	fixture.event_count = 0;
	fixture.acquire_calls = 0;
	fixture.plan_calls = 0;
	fixture.release_calls = 0;
	fixture.release_result = SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	result = FixtureBegin(&state, &fixture, 3, &source);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_SWIM_LIVE_FAILURE_PLAN);
	CHECK(fixture.acquire_calls == 1 && fixture.plan_calls == 1);
	CHECK(fixture.release_calls == 1);
	CHECK(state.guard_owned && state.outer.phase == SG_COMPOUND_RECOVER);
}

static void TestExternalOrphanClearsPendingCommand(void)
{
	fixture_t fixture;
	sg_compound_swim_live_state_t state =
		SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t source;
	usercmd_t command;

	FixtureInit(&fixture);
	source = FixturePose(0.0f, true, 0.0f);
	CHECK(FixtureBegin(&state, &fixture, 3, &source).outcome ==
	      SG_COMPOUND_SWIM_LIVE_RUNNING);
	CHECK(SG_CompoundSwimLivePreStep(&state, &fixture.host, &source,
	                                &command).command_ready);
	CHECK(state.command_pending && state.guard_owned);
	result = SG_CompoundSwimLiveOrphaned(&state, 4, 22);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_REJECTED);
	CHECK(state.command_pending && state.guard_owned);
	result = SG_CompoundSwimLiveOrphaned(&state, 3, 22);
	CHECK(result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED);
	CHECK(memcmp(&state, &(sg_compound_swim_live_state_t)
	      SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER, sizeof(state)) == 0);
}

int main(void)
{
	TestInitializerStartsUnowned();
	TestExactDoorSwimTransaction();
	TestDoorSwimIsTheOnlyFixtureAdmission();
	TestMutationBoundariesFailClosed();
	TestRecoveryRetainsMover();
	TestReleaseRetryRequiresCurrentOutsideSweep();
	TestTouchEndingSweepPrecedesMutation();
	TestRepeatedAuthenticatedTouchIsIdempotent();
	TestMalformedSegmentResultFailsClosed();
	TestOpeningZeroSegmentDenied();
	TestOpeningHoldUsesSwimReplayCommand();
	TestOpeningZeroObservationFailuresRetainOwnership();
	TestZeroBoundaryConsumesPostPusherState();
	TestSuffixAuthorityFailureStillConsumesSegment();
	TestRecoveryAuthorityFailureStillConsumesSegment();
	TestElapsedOverflowFailsOwned();
	TestDynamicPlanIsPreparedAfterOwnership();
	TestExternalOrphanClearsPendingCommand();
	if (failures)
	{
		fprintf(stderr, "compound_swim_live_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("compound_swim_live_test: ok");
	return 0;
}
