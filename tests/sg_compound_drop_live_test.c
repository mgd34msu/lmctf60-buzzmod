/* Production-law fixture for PREOPEN RL_DOOR_DROP. */
#include "q_shared.h"

#include <stdio.h>
#include <string.h>

#include "slipgate/sg_compound_drop_live.h"

typedef struct fixture_s
{
	sg_compound_drop_live_snapshot_t published;
	sg_compound_drop_live_host_t host;
	sg_compound_drop_live_proof_t proof;
	int acquire_calls;
	int hold_calls;
	int release_calls;
	int orphan_calls;
	int segment_calls;
	int deny_segments;
	int recovery_proof_calls;
	qboolean deny_recovery_proof;
	sg_compound_drop_live_host_result_t outside_result;
} fixture_t;

static int failures;
static qboolean corrupt_shadow;

vec_t VectorLength(vec3_t value)
{
	return sqrtf(value[0] * value[0] + value[1] * value[1] +
	             value[2] * value[2]);
}

qboolean SG_DeclaredCommand(const vec3_t origin, const vec3_t target,
	const pmove_state_t *pms, usercmd_t *command)
{
	vec3_t delta;
	float horizontal, yaw;
	byte msec;

	if (!origin || !target || !pms || !command)
		return false;
	VectorSubtract(target, origin, delta);
	delta[2] = 0.0f;
	horizontal = VectorLength(delta);
	if (!isfinite(horizontal))
		return false;
	yaw = horizontal > 0.01f ?
	      atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI : 0.0f;
	msec = command->msec;
	memset(command, 0, sizeof(*command));
	command->msec = msec;
	command->angles[PITCH] = -pms->delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(yaw) - pms->delta_angles[YAW];
	command->angles[ROLL] = -pms->delta_angles[ROLL];
	if (horizontal > 4.0f)
		command->forwardmove = horizontal < 32.0f ? 200 : 400;
	return true;
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_compound_drop_live_host_result_t Bind(void *context,
	uint32_t link_index, sg_compound_drop_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot ||
	    link_index != fixture->published.binding.link_index)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	*snapshot = fixture->published;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t SourceCheckpoint(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot,
	sg_compound_publication_angle_bias_t *bias)
{
	(void)context;
	if (!snapshot || !bias)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	memset(bias, 0, sizeof(*bias));
	bias->axis[YAW] = 11;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t SuffixCheckpoint(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_compound_publication_angle_bias_t *bias)
{
	(void)context;
	return snapshot && bias && bias->axis[YAW] == 11 ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_ERROR;
}

static sg_compound_drop_live_host_result_t Acquire(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	fixture->acquire_calls++;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t Accepted(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot)
{
	(void)context;
	return snapshot ? SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	                  SG_COMPOUND_DROP_LIVE_HOST_ERROR;
}

static sg_compound_drop_live_host_result_t Hold(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot, int lease_ms)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot || lease_ms != SG_COMPOUND_HOLD_LEASE_MS)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	fixture->hold_calls++;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t Outside(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose)
{
	fixture_t *fixture = (fixture_t *)context;

	return fixture && snapshot && pose ? fixture->outside_result :
	                                    SG_COMPOUND_DROP_LIVE_HOST_ERROR;
}

static sg_compound_drop_live_host_result_t GroundSupport(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose)
{
	(void)context;
	return snapshot && pose && pose->grounded ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t Segment(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot,
	const vec3_t from, const vec3_t to)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot || !from || !to)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	fixture->segment_calls++;
	if (fixture->deny_segments > 0)
	{
		fixture->deny_segments--;
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	}
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t Prove(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose, qboolean recovery,
	sg_compound_drop_live_proof_t *proof)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot || !pose || !proof)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	if (recovery)
	{
		fixture->recovery_proof_calls++;
		if (fixture->deny_recovery_proof)
			return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	}
	*proof = fixture->proof;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t Release(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	fixture->release_calls++;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t Orphan(void *context,
	const sg_compound_drop_live_snapshot_t *snapshot, int bolt_key)
{
	fixture_t *fixture = (fixture_t *)context;

	if (!fixture || !snapshot || bolt_key < 0)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	fixture->orphan_calls++;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

/* Independent final-writer shadow used by SG_DropLivePreStep. */
static qboolean DropShadow(const sg_drop_replay_state_t *state,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	vec3_t direction;
	short yaw;
	byte msec;

	if (!state || !pose || !command)
		return false;
	msec = command->msec;
	memset(command, 0, sizeof(*command));
	command->msec = msec;
	if (msec != SG_REPLAY_STEP_MS)
		return false;
	if (state->recovery)
	{
		VectorSubtract(state->spec.destination, pose->origin, direction);
		if (!SG_DropReplayPlanarYawCommand(direction[0], direction[1],
		        pose->pms.delta_angles[YAW], &yaw))
			return false;
	}
	else if (state->walkoff)
		yaw = ANGLE2SHORT(state->spec.heading * (360.0f / 256.0f)) -
		      pose->pms.delta_angles[YAW];
	else
	{
		VectorSubtract(state->spec.lip, pose->origin, direction);
		if (!SG_DropReplayPlanarYawCommand(direction[0], direction[1],
		        pose->pms.delta_angles[YAW], &yaw))
			return false;
	}
	command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
	command->angles[YAW] = yaw;
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	command->forwardmove = 400;
	if (corrupt_shadow)
		command->buttons = BUTTON_USE;
	return true;
}

static sg_replay_pose_t Pose(float x, float z, qboolean grounded,
	float horizontal_speed, float vertical_speed)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	VectorSet(pose.origin, x, 0.0f, z);
	VectorSet(pose.velocity, horizontal_speed, 0.0f, vertical_speed);
	pose.pms.origin[0] = (short)(x * 8.0f);
	pose.pms.origin[2] = (short)(z * 8.0f);
	pose.pms.velocity[0] = (short)(horizontal_speed * 8.0f);
	pose.pms.velocity[2] = (short)(vertical_speed * 8.0f);
	pose.grounded = grounded;
	return pose;
}

static sg_replay_observation_t Observation(qboolean arrival)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.ground_support_valid = true;
	observation.contact_clear = arrival;
	observation.drop_arrival_contact_clear = arrival;
	observation.drop_recovery_contact_clear = arrival;
	return observation;
}

static void FixtureInit(fixture_t *fixture)
{
	sg_compound_publication_binding_t *binding;
	rune_link_t *link;

	memset(fixture, 0, sizeof(*fixture));
	binding = &fixture->published.binding;
	link = &binding->link;
	binding->link_index = 3;
	binding->mechanism_index = 0;
	link->from = 0;
	link->to = 1;
	link->action = RL_DOOR_DROP;
	link->provenance = RL_CONTRACTED;
	link->mode = RLCM_PREOPEN;
	link->heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	link->exit_speed = 12;
	link->cost_ms = 500;
	link->sweep_clear_ms = 100;
	VectorSet(link->anchor, 64.0f, 0.0f, 64.0f);
	VectorSet(link->mechanism_anchor, 64.0f, 0.0f, 64.0f);
	VectorSet(binding->source_seed.origin, 0.0f, 0.0f, 64.0f);
	VectorSet(binding->destination_seed.origin, 128.0f, 0.0f, 0.0f);
	VectorSet(binding->canonical_hint, 56.0f, 0.0f, 64.0f);
	binding->touch_ms = 100;
	binding->touch_frame_end_ms = 100;
	binding->mover_top_ms = 300;
	binding->suffix_start_ms = 200;
	binding->arrival_ms = 200;
	binding->sweep_clear_ms = 100;
	binding->total_cost_ms = 500;
	fixture->published.trigger_key = 21;
	fixture->published.mover_key = 22;
	fixture->proof.arrival_ms = 200;
	fixture->proof.sweep_clear_ms = 100;
	fixture->proof.exit_speed = 12;
	fixture->outside_result = SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
	fixture->host.context = fixture;
	fixture->host.bind = Bind;
	fixture->host.source_checkpoint = SourceCheckpoint;
	fixture->host.suffix_checkpoint = SuffixCheckpoint;
	fixture->host.acquire = Acquire;
	fixture->host.authorize = Accepted;
	fixture->host.activate = Accepted;
	fixture->host.at_top = Accepted;
	fixture->host.hold_open = Hold;
	fixture->host.outside_sweep = Outside;
	fixture->host.ground_support = GroundSupport;
	fixture->host.sweep_segment_clear = Segment;
	fixture->host.prove_suffix = Prove;
	fixture->host.release = Release;
	fixture->host.orphan = Orphan;
	fixture->host.drop_shadow = DropShadow;
}

static sg_compound_drop_live_result_t ConsumeCommand(
	sg_compound_drop_live_state_t *state, fixture_t *fixture,
	const sg_replay_pose_t *before, const sg_replay_pose_t *after,
	const sg_replay_observation_t *observation, qboolean boundary)
{
	sg_compound_drop_live_result_t result;
	usercmd_t command;

	result = SG_CompoundDropLivePreStep(state, &fixture->host, before,
	                                    &command);
	CHECK(result.command_ready && command.msec == SG_REPLAY_STEP_MS);
	if (!result.command_ready)
		return result;
	result = SG_CompoundDropLivePostStep(state, &fixture->host, after,
	                                     observation);
	if (boundary)
	{
		CHECK(state->command_pending);
		result = SG_CompoundDropLiveBoundary(state, &fixture->host, after,
		                                     observation);
	}
	return result;
}

/* Execute all four real 25 ms approach commands.  The fourth command's touch
 * and activation happen inside ClientThink, while its pose is not consumed
 * until the later entity/pusher boundary. */
static void EnterOpening(sg_compound_drop_live_state_t *state,
	fixture_t *fixture)
{
	sg_replay_pose_t before, after;
	sg_replay_observation_t observation = Observation(false);
	sg_compound_drop_live_result_t result;
	usercmd_t command;
	int step;

	before = Pose(0.0f, 64.0f, true, 0.0f, 0.0f);
	CHECK(SG_CompoundDropLiveBegin(state, &fixture->host, 3, &before).outcome ==
	      SG_COMPOUND_DROP_LIVE_RUNNING);
	for (step = 1; step <= 3; step++)
	{
		after = Pose(step * 16.0f, 64.0f, true, 64.0f, 0.0f);
		result = ConsumeCommand(state, fixture, &before, &after,
		                        &observation, false);
		CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
		before = after;
	}
	result = SG_CompoundDropLivePreStep(state, &fixture->host, &before,
	                                    &command);
	CHECK(result.command_ready);
	after = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	CHECK(SG_CompoundDropLiveAuthorizeTouch(state, &fixture->host, 21,
	    &after, 17).outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
	CHECK(SG_CompoundDropLiveAuthorizeActivation(state, &fixture->host,
	    21, 22, 17).outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
	result = SG_CompoundDropLivePostStep(state, &fixture->host, &after,
	                                     &observation);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
	CHECK(state->command_pending && state->transaction_elapsed_ms == 75);
	result = SG_CompoundDropLiveBoundary(state, &fixture->host, &after,
	                                     &observation);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
	CHECK(state->outer.phase == SG_COMPOUND_OPENING &&
	      state->transaction_elapsed_ms == 100);
}

static void EnterSuffix(sg_compound_drop_live_state_t *state,
	fixture_t *fixture)
{
	sg_replay_pose_t pose = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	sg_replay_observation_t observation = Observation(false);
	int step;

	EnterOpening(state, fixture);
	for (step = 1; step <= 8; step++)
		CHECK(ConsumeCommand(state, fixture, &pose, &pose, &observation,
		                     step % 4 == 0).outcome ==
		      SG_COMPOUND_DROP_LIVE_RUNNING);
	CHECK(state->outer.phase == SG_COMPOUND_SUFFIX_LEASED);
	CHECK(state->drop_active && state->drop_link == 3);
}

static void TestExactTransaction(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t before, after;
	sg_replay_observation_t observation;
	sg_compound_drop_live_result_t result = { 0 };
	int step;

	FixtureInit(&fixture);
	EnterSuffix(&state, &fixture);
	CHECK(state.transaction_elapsed_ms == 300);
	CHECK(fixture.acquire_calls == 1 && fixture.hold_calls == 1);
	fixture.deny_segments = 3;
	before = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	for (step = 1; step <= 8; step++)
	{
		qboolean terminal = step == 8;
		float x = 64.0f + step * 8.0f;
		float z = terminal ? 0.0f : 64.0f - step * 7.0f;

		after = Pose(x, z, terminal, terminal ? 48.0f : 64.0f,
		             terminal ? 0.0f : -96.0f);
		observation = Observation(terminal);
		result = ConsumeCommand(&state, &fixture, &before, &after,
		                        &observation, step % 4 == 0);
		before = after;
		if (!terminal)
			CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
		if (step == 4)
			CHECK(state.sweep_clear && !state.arrived);
	}
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_COMPLETE);
	CHECK(!state.guard_owned && !state.drop_active && state.drop_link == -1);
	CHECK(state.transaction_elapsed_ms == 500);
	CHECK(fixture.release_calls == 1 && fixture.orphan_calls == 0);
	CHECK(fixture.hold_calls == 1);
}

static void TestActionAdmissionAndPublicationDrift(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t source = Pose(0.0f, 64.0f, true, 0.0f, 0.0f);
	usercmd_t command;
	sg_compound_drop_live_result_t result;

	FixtureInit(&fixture);
	fixture.published.binding.link.action = RL_DOOR_HOOK;
	result = SG_CompoundDropLiveBegin(&state, &fixture.host, 3, &source);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_REJECTED);
	CHECK(fixture.acquire_calls == 0 && !state.guard_owned);

	FixtureInit(&fixture);
	memset(&state, 0, sizeof(state));
	CHECK(SG_CompoundDropLiveBegin(&state, &fixture.host, 3, &source).outcome ==
	      SG_COMPOUND_DROP_LIVE_RUNNING);
	fixture.published.binding.link.cost_ms++;
	result = SG_CompoundDropLivePreStep(&state, &fixture.host, &source,
	                                    &command);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_AUTHORITY);
	CHECK(state.guard_owned && fixture.release_calls == 0);
}

static void TestLongApproachHonorsPublishedTouchBoundary(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t pose = Pose(0.0f, 64.0f, true, 0.0f, 0.0f);
	sg_replay_observation_t observation = Observation(false);
	sg_compound_drop_live_result_t result = { 0 };
	int frame, step;

	FixtureInit(&fixture);
	fixture.published.binding.touch_ms = 1000;
	fixture.published.binding.touch_frame_end_ms = 1000;
	fixture.published.binding.total_cost_ms = 1400;
	fixture.published.binding.link.cost_ms = 1400;
	CHECK(SG_CompoundDropLiveBegin(&state, &fixture.host, 3, &pose).outcome ==
	      SG_COMPOUND_DROP_LIVE_RUNNING);
	for (frame = 1; frame <= 9; frame++)
	{
		for (step = 1; step <= 4; step++)
			result = ConsumeCommand(&state, &fixture, &pose, &pose,
			                        &observation, step == 4);
		CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING);
		CHECK(state.outer.phase == SG_COMPOUND_APPROACH);
		CHECK(state.transaction_elapsed_ms == frame * SG_REPLAY_FRAME_MS);
	}
	for (step = 1; step <= 4; step++)
		result = ConsumeCommand(&state, &fixture, &pose, &pose,
		                        &observation, step == 4);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH);
	CHECK(state.transaction_elapsed_ms == 1000);
}

static void TestOpeningRefusesRepeatedTriggerWithoutAborting(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t pose = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	sg_compound_drop_live_result_t result;
	usercmd_t command;

	FixtureInit(&fixture);
	EnterOpening(&state, &fixture);
	result = SG_CompoundDropLivePreStep(&state, &fixture.host, &pose,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundDropLiveAuthorizeTouch(&state, &fixture.host, 21,
	    &pose, 18);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_WAIT);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE);
	CHECK(state.guard_owned && state.outer.phase == SG_COMPOUND_OPENING);
	CHECK(state.command_pending && state.transaction_elapsed_ms == 100);
}

static void TestSuffixRefusesRepeatedTriggerWithoutAborting(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t pose = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	sg_compound_drop_live_result_t result;
	usercmd_t command;

	FixtureInit(&fixture);
	EnterSuffix(&state, &fixture);
	result = SG_CompoundDropLivePreStep(&state, &fixture.host, &pose,
	                                    &command);
	CHECK(result.command_ready);
	result = SG_CompoundDropLiveAuthorizeTouch(&state, &fixture.host, 21,
	    &pose, 19);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_WAIT);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE);
	CHECK(state.guard_owned &&
	      state.outer.phase == SG_COMPOUND_SUFFIX_LEASED);
	CHECK(state.command_pending && state.drop_active &&
	      state.replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_SUFFIX);
	result = SG_CompoundDropLiveAuthorizeTouch(&state, &fixture.host, 20,
	    &pose, 19);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_TOUCH);
}

static void TestDeathTransfersLeaseToOrphan(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t source = Pose(0.0f, 64.0f, true, 0.0f, 0.0f);
	sg_compound_drop_live_result_t result;

	FixtureInit(&fixture);
	CHECK(SG_CompoundDropLiveBegin(&state, &fixture.host, 3, &source).outcome ==
	      SG_COMPOUND_DROP_LIVE_RUNNING);
	state.recovering = true;
	state.sweep_clear = true;
	state.arrived = true;
	state.failure = SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY;
	state.replay_reason = SG_REPLAY_REASON_CONTAMINATED;
	state.last_sweep_contact_ms = 100;
	result = SG_CompoundDropLiveOrphan(&state, &fixture.host, 0);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_SAFE_STOPPED);
	CHECK(fixture.orphan_calls == 1 && fixture.release_calls == 0);
	CHECK(!state.guard_owned && !state.command_pending &&
	      state.outer.phase == SG_COMPOUND_NONE);
	CHECK(!state.recovering && !state.sweep_clear && !state.arrived &&
	      state.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE &&
	      state.replay_reason == SG_REPLAY_REASON_NONE &&
	      state.last_sweep_contact_ms == 0);
	CHECK(SG_CompoundDropLiveBegin(&state, &fixture.host, 3, &source).outcome ==
	      SG_COMPOUND_DROP_LIVE_RUNNING);
}

static void TestDropDifferentialRetainsGuard(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t lip = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	usercmd_t command;
	sg_compound_drop_live_result_t result;

	FixtureInit(&fixture);
	EnterSuffix(&state, &fixture);
	corrupt_shadow = true;
	result = SG_CompoundDropLivePreStep(&state, &fixture.host, &lip,
	                                    &command);
	corrupt_shadow = false;
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_REPLAY);
	CHECK(state.guard_owned && fixture.release_calls == 0);
}

static void TestInsideSweepFailureRecoversAndReleases(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t before, after;
	sg_replay_observation_t observation;
	sg_compound_drop_live_result_t result = { 0 };
	usercmd_t command;
	int step;

	FixtureInit(&fixture);
	EnterSuffix(&state, &fixture);
	before = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	corrupt_shadow = true;
	result = SG_CompoundDropLivePreStep(&state, &fixture.host, &before,
	                                    &command);
	corrupt_shadow = false;
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(state.guard_owned && state.outer.phase == SG_COMPOUND_RECOVER);
	fixture.outside_result = SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	result = SG_CompoundDropLiveRecover(&state, &fixture.host, &before, 0.0f);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE);
	CHECK(fixture.recovery_proof_calls == 1);
	CHECK(state.replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY);
	fixture.outside_result = SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
	fixture.deny_segments = 1;
	for (step = 1; step <= 8; step++)
	{
		qboolean terminal = step == 8;

		after = Pose(64.0f + step * 8.0f,
		             terminal ? 0.0f : 64.0f - step * 7.0f,
		             terminal, terminal ? 48.0f : 64.0f,
		             terminal ? 0.0f : -96.0f);
		observation = Observation(terminal);
		result = ConsumeCommand(&state, &fixture, &before, &after,
		                        &observation, step % 4 == 0);
		before = after;
	}
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_SAFE_STOPPED);
	CHECK(!state.guard_owned && !state.drop_active && state.drop_link == -1);
	CHECK(fixture.release_calls == 1 && fixture.orphan_calls == 0);
	CHECK(!state.recovering && !state.sweep_clear && !state.arrived &&
	      state.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE &&
	      state.replay_reason == SG_REPLAY_REASON_NONE &&
	      state.last_sweep_contact_ms == 0);
	before = Pose(0.0f, 64.0f, true, 0.0f, 0.0f);
	CHECK(SG_CompoundDropLiveBegin(&state, &fixture.host, 3, &before).outcome ==
	      SG_COMPOUND_DROP_LIVE_RUNNING);
}

static void TestDeniedInsideSweepRecoveryRetainsGuard(void)
{
	fixture_t fixture;
	sg_compound_drop_live_state_t state =
		SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
	sg_replay_pose_t pose = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
	sg_compound_drop_live_result_t result;
	usercmd_t command;

	FixtureInit(&fixture);
	EnterSuffix(&state, &fixture);
	corrupt_shadow = true;
	result = SG_CompoundDropLivePreStep(&state, &fixture.host, &pose,
	                                    &command);
	corrupt_shadow = false;
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	fixture.outside_result = SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	fixture.deny_recovery_proof = true;
	result = SG_CompoundDropLiveRecover(&state, &fixture.host, &pose, 0.0f);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF);
	CHECK(!result.command_ready);
	CHECK(fixture.recovery_proof_calls == 1);
	CHECK(state.guard_owned && fixture.release_calls == 0);
}

static void TestConsumedSubstepFailuresBeginRecovery(void)
{
	int failure_step;

	for (failure_step = 1; failure_step <= 3; failure_step++)
	{
		fixture_t fixture;
		sg_compound_drop_live_state_t state =
			SG_COMPOUND_DROP_LIVE_STATE_INITIALIZER;
		sg_replay_pose_t pose = Pose(64.0f, 64.0f, true, 0.0f, 0.0f);
		sg_replay_observation_t observation;
		sg_compound_drop_live_result_t result = { 0 };
		usercmd_t command;
		int step;

		FixtureInit(&fixture);
		EnterSuffix(&state, &fixture);
		for (step = 1; step <= failure_step; step++)
		{
			observation = Observation(false);
			if (step == failure_step)
				observation.contaminated = true;
			result = SG_CompoundDropLivePreStep(&state, &fixture.host,
			                                    &pose, &command);
			CHECK(result.command_ready);
			result = SG_CompoundDropLivePostStep(&state, &fixture.host,
			                                     &pose, &observation);
		}
		CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
		CHECK(state.transaction_elapsed_ms ==
		      300 + failure_step * SG_REPLAY_STEP_MS);
		CHECK(!state.command_pending && state.guard_owned);
		fixture.outside_result = SG_COMPOUND_DROP_LIVE_HOST_DENIED;
		result = SG_CompoundDropLiveRecover(&state, &fixture.host, &pose,
		                                    0.0f);
		CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
		CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE);
		CHECK(state.replay_kind == SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY);
		result = SG_CompoundDropLivePreStep(&state, &fixture.host, &pose,
		                                    &command);
		CHECK(result.command_ready);
	}
}

int main(void)
{
	TestExactTransaction();
	TestActionAdmissionAndPublicationDrift();
	TestLongApproachHonorsPublishedTouchBoundary();
	TestOpeningRefusesRepeatedTriggerWithoutAborting();
	TestSuffixRefusesRepeatedTriggerWithoutAborting();
	TestDeathTransfersLeaseToOrphan();
	TestDropDifferentialRetainsGuard();
	TestInsideSweepFailureRecoversAndReleases();
	TestDeniedInsideSweepRecoveryRetainsGuard();
	TestConsumedSubstepFailuresBeginRecovery();
	if (failures)
	{
		fprintf(stderr, "compound_drop_live_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("compound_drop_live_test: ok");
	return 0;
}
