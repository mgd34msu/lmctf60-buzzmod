/* Direct host-free tests for the ordinary revision-2 RL_HOOK adapter. */
#include "q_shared.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_hook_live.h"

typedef struct test_owner_s
{
	sg_hook_replay_state_t replay;
	qboolean active;
	int replay_link;
	sg_hook_live_command_guard_t guard;
} test_owner_t;

static int failures;
static int legacy_calls;
static int legacy_zero_before_arrival;
static qboolean legacy_mismatch;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_replay_pose_t Pose(float x, float y, float z, qboolean grounded)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	VectorSet(pose.origin, x, y, z);
	pose.grounded = grounded;
	return pose;
}

static sg_replay_observation_t Observation(void)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.contact_clear = true;
	observation.hook_rope_valid = true;
	observation.hook_rope_length = 200;
	return observation;
}

static qboolean LegacySettled(const sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	float dx, dy, dz;

	if (!state || !pose || !observation || !observation->contact_clear ||
	    (!pose->grounded && pose->waterlevel < 2) ||
	    (pose->waterlevel > 0 &&
	     (pose->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
		return false;
	dx = state->spec.destination[0] - pose->origin[0];
	dy = state->spec.destination[1] - pose->origin[1];
	dz = state->spec.destination[2] - pose->origin[2];
	return dx * dx + dy * dy < SG_REPLAY_ARRIVE_RADIUS *
	                             SG_REPLAY_ARRIVE_RADIUS &&
	       dz > -SG_REPLAY_ARRIVE_Z && dz < SG_REPLAY_ARRIVE_Z;
}

static qboolean LegacyCommand(const sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation,
	usercmd_t *command)
{
	float yaw;

	legacy_calls++;
	if (!state || !pose || !observation || !command)
		return false;
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	if (state->phase == SG_HOOK_REPLAY_SETTLE &&
	    (state->arrived_in_frame || LegacySettled(state, pose, observation)))
	{
		/* Host-free settlement policy: frozen literal zero-fill. */
		if (!state->arrived_in_frame)
			legacy_zero_before_arrival++;
		if (legacy_mismatch)
			command->buttons = BUTTON_USE;
		return true;
	}
	if (state->phase == SG_HOOK_REPLAY_SETTLE)
	{
		yaw = atan2f(state->spec.destination[1] - pose->origin[1],
		             state->spec.destination[0] - pose->origin[0]) *
		      180.0f / (float)M_PI;
		command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
		command->angles[YAW] = ANGLE2SHORT(yaw) - pose->pms.delta_angles[YAW];
		command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
		command->forwardmove = 400;
	}
	else
	{
		command->angles[PITCH] = ANGLE2SHORT(state->spec.view_angles[PITCH]) -
		                         pose->pms.delta_angles[PITCH];
		command->angles[YAW] = ANGLE2SHORT(state->spec.view_angles[YAW]) -
		                       pose->pms.delta_angles[YAW];
		command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	}
	if (legacy_mismatch)
		command->buttons = BUTTON_USE;
	return true;
}

static void OwnerInit(test_owner_t *owner)
{
	SG_HookLiveReset(&owner->replay, &owner->active, &owner->replay_link,
	    &owner->guard);
}

static sg_hook_live_result_t Begin(test_owner_t *owner,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation)
{
	sg_hook_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	VectorSet(spec.bite, 50.0f, 0.0f, 0.0f);
	VectorSet(spec.destination, 100.0f, 0.0f, 0.0f);
	VectorSet(spec.view_angles, 0.0f, 90.0f, 0.0f);
	spec.flight_ms = 100;
	spec.settle_limit_ms = SG_RUNE_PROOF_HOOK_DRY_SETTLE_MS;
	spec.expected_release_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_pull_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_ms = SG_REPLAY_TIME_DISCOVER;
	return SG_HookLiveBegin(&owner->replay, &owner->active,
	    &owner->replay_link, 17, true, &spec, pose, observation, 0.0f,
	    &owner->guard);
}

static sg_hook_live_result_t Step(test_owner_t *owner,
	sg_replay_pose_t *pose, sg_replay_observation_t *observation,
	usercmd_t *command)
{
	sg_hook_live_result_t result;

	memset(command, 0x5a, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	result = SG_HookLivePreStep(&owner->replay, &owner->active,
	    &owner->replay_link, 17, true, pose, observation, LegacyCommand,
	    command, &owner->guard);
	if (result.outcome != SG_HOOK_LIVE_RUNNING)
		return result;
	result = SG_HookLiveValidateStoredFinalCommand(&owner->replay,
	    &owner->active, &owner->replay_link, 17, true, &owner->guard,
	    command);
	if (result.outcome != SG_HOOK_LIVE_RUNNING)
		return result;
	return SG_HookLivePostStep(&owner->replay, &owner->active,
	    &owner->replay_link, 17, true, pose, observation);
}

static void TestOrderedHookAndZeroSettlement(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	sg_replay_observation_t observation = Observation();
	sg_hook_live_result_t result;
	usercmd_t command;
	int step;

	OwnerInit(&owner);
	legacy_calls = 0;
	legacy_zero_before_arrival = 0;
	legacy_mismatch = false;
	pose.pms.delta_angles[PITCH] = 3;
	pose.pms.delta_angles[YAW] = 5;
	pose.pms.delta_angles[ROLL] = 7;
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(owner.replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	CHECK(SG_HookLiveAttached(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	for (step = 0; step < 4; step++)
		CHECK(Step(&owner, &pose, &observation, &command).outcome ==
		      SG_HOOK_LIVE_RUNNING);
	CHECK(owner.replay.phase == SG_HOOK_REPLAY_WAIT_PULL);
	CHECK(SG_HookLivePullApplied(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	pose = Pose(100.0f, 0.0f, 0.0f, true);
	result = Step(&owner, &pose, &observation, &command);
	CHECK(result.outcome == SG_HOOK_LIVE_RUNNING &&
	      owner.replay.release_requested && !owner.replay.release_applied);
	CHECK(SG_HookLiveReleaseApplied(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	for (step = 0; step < 3; step++)
		CHECK(Step(&owner, &pose, &observation, &command).outcome ==
		      SG_HOOK_LIVE_RUNNING);
	CHECK(owner.replay.phase == SG_HOOK_REPLAY_SETTLE);
	for (step = 0; step < 3; step++)
	{
		result = Step(&owner, &pose, &observation, &command);
		CHECK(result.outcome == SG_HOOK_LIVE_RUNNING);
		CHECK(command.msec == SG_REPLAY_STEP_MS && command.buttons == 0 &&
		      command.forwardmove == 0 && command.sidemove == 0 &&
		      command.upmove == 0 && command.impulse == 0 &&
		      command.lightlevel == 0 && command.angles[0] == 0 &&
		      command.angles[1] == 0 && command.angles[2] == 0);
	}
	result = Step(&owner, &pose, &observation, &command);
	CHECK(result.outcome == SG_HOOK_LIVE_ARRIVED && !owner.active &&
	      owner.replay_link == -1 && legacy_calls == 12 &&
		      legacy_zero_before_arrival == 1);
}

static void TestLateAttachBridge(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	sg_replay_observation_t observation = Observation();
	sg_hook_replay_state_t parked;
	sg_hook_live_result_t result;
	usercmd_t command;
	int step;

	/* flight_ms=100 enters the event barrier immediately.  A real bolt may
	 * still take more than one complete host frame to attach; the bridge must
	 * keep the reducer's exact attachment timestamp parked while it emits the
	 * old fixed view. */
	OwnerInit(&owner);
	legacy_mismatch = false;
	pose.pms.delta_angles[PITCH] = 11;
	pose.pms.delta_angles[YAW] = -7;
	pose.pms.delta_angles[ROLL] = 3;
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(owner.replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH);
	parked = owner.replay;
	for (step = 0; step < 8; step++)
	{
		memset(&command, 0x5a, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		result = SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
		    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
		    &command, &owner.guard);
		CHECK(result.outcome == SG_HOOK_LIVE_RUNNING && owner.active &&
		      owner.replay_link == 17 &&
		      owner.replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH &&
		      owner.replay.phase_step == 0 && owner.replay.flight_body_ms == 0 &&
		      owner.replay.progress.elapsed_ms == 0 &&
		      !owner.replay.progress.step_pending);
		CHECK(memcmp(&owner.replay, &parked, sizeof(owner.replay)) == 0);
		CHECK(owner.guard.pending && owner.guard.action_link == 17);
		CHECK(command.msec == SG_REPLAY_STEP_MS && command.buttons == 0 &&
		      command.forwardmove == 0 && command.sidemove == 0 &&
		      command.upmove == 0 && command.impulse == 0 &&
		      command.lightlevel == 0 && command.angles[PITCH] == -11 &&
		      command.angles[YAW] == ANGLE2SHORT(90.0f) + 7 &&
		      command.angles[ROLL] == -3);
		CHECK(SG_HookLiveValidateStoredFinalCommand(&owner.replay,
		    &owner.active, &owner.replay_link, 17, true, &owner.guard,
		    &command).outcome == SG_HOOK_LIVE_RUNNING && !owner.guard.pending &&
		      owner.guard.action_link == -1);
	}
	/* The first authoritative attached frame consumes its one event. */
	CHECK(SG_HookLiveAttached(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(owner.replay.phase == SG_HOOK_REPLAY_ATTACH_FRAME &&
	      owner.replay.flight_body_ms == 0 &&
	      owner.replay.progress.elapsed_ms == 0);
	result = SG_HookLiveAttached(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose);
	CHECK(result.outcome == SG_HOOK_LIVE_FAILED &&
	      result.failure == SG_HOOK_LIVE_FAILURE_ATTACH && !owner.active);

	/* A legacy-command drift at the bridge, or tampering after the bridge,
	 * remains fail-closed before ClientThink. */
	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	legacy_mismatch = true;
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_COMMAND_DIFFERENTIAL &&
	      !owner.active && command.buttons == BUTTON_USE);
	legacy_mismatch = false;

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	CHECK(SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard).outcome == SG_HOOK_LIVE_RUNNING);
	command.buttons = BUTTON_USE;
	result = SG_HookLiveValidateStoredFinalCommand(&owner.replay,
	    &owner.active, &owner.replay_link, 17, true, &owner.guard, &command);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_FINAL_COMMAND && !owner.active &&
	      !owner.guard.pending && owner.guard.action_link == -1);

	/* The ordinary active reducer path captures an equally independent guard
	 * before a hypothetical late writer. The final boundary must reject that
	 * mutation rather than comparing command with a newly copied twin. */
	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(SG_HookLiveAttached(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	CHECK(SG_HookLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard).outcome == SG_HOOK_LIVE_RUNNING &&
	      owner.guard.pending && owner.guard.action_link == 17);
	command.buttons = BUTTON_USE;
	result = SG_HookLiveValidateStoredFinalCommand(&owner.replay,
	    &owner.active, &owner.replay_link, 17, true, &owner.guard, &command);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_FINAL_COMMAND && !owner.active &&
	      !owner.guard.pending && owner.guard.action_link == -1);

	/* Reset itself must discard an unconsumed bridge approval; a later action
	 * on the same link may not validate command data from the earlier owner. */
	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	CHECK(SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard).outcome == SG_HOOK_LIVE_RUNNING &&
	      owner.guard.pending);
	SG_HookLiveReset(&owner.replay, &owner.active, &owner.replay_link,
	    &owner.guard);
	CHECK(!owner.guard.pending && owner.guard.action_link == -1);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	result = SG_HookLiveValidateStoredFinalCommand(&owner.replay,
	    &owner.active, &owner.replay_link, 17, true, &owner.guard, &command);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_FINAL_COMMAND && !owner.active &&
	      !owner.guard.pending && owner.guard.action_link == -1);

	/* Begin is also a lifecycle boundary.  Even if an embedding misses its
	 * reset, reusing the exact same link must erase the old approval. */
	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	CHECK(SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard).outcome == SG_HOOK_LIVE_RUNNING &&
	      owner.guard.pending);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(!owner.guard.pending && owner.guard.action_link == -1);
	result = SG_HookLiveValidateStoredFinalCommand(&owner.replay,
	    &owner.active, &owner.replay_link, 17, true, &owner.guard, &command);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_FINAL_COMMAND && !owner.active &&
	      !owner.guard.pending && owner.guard.action_link == -1);

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, false, &pose, &observation, LegacyCommand,
	    &command, &owner.guard);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_IDENTITY && !owner.active);

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(SG_HookLiveAttached(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_HookLiveWaitAttachStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_ATTACH && !owner.active);
}

static void TestStaleIdentityAndDifferentialFallback(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	sg_replay_observation_t observation = Observation();
	sg_hook_live_result_t result;
	usercmd_t command;

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_HookLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, false, &pose, &observation, LegacyCommand,
	    &command, &owner.guard);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_IDENTITY && !owner.active &&
	      owner.replay_link == -1 && command.msec == SG_REPLAY_STEP_MS);

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	CHECK(SG_HookLiveAttached(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose).outcome == SG_HOOK_LIVE_RUNNING);
	legacy_mismatch = true;
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_HookLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_COMMAND_DIFFERENTIAL &&
	      !owner.active && command.buttons == BUTTON_USE);
	legacy_mismatch = false;
}

static void TestEventOrderAndFinalCommand(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	sg_replay_observation_t observation = Observation();
	sg_hook_live_result_t result;
	usercmd_t expected, actual;

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	result = SG_HookLivePullApplied(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose);
	CHECK(result.outcome == SG_HOOK_LIVE_FAILED &&
	      result.failure == SG_HOOK_LIVE_FAILURE_PULL &&
	      result.replay_reason == SG_REPLAY_REASON_HOOK_EVENT_ORDER &&
	      !owner.active);

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, &observation).outcome == SG_HOOK_LIVE_RUNNING);
	memset(&expected, 0, sizeof(expected));
	expected.msec = SG_REPLAY_STEP_MS;
	actual = expected;
	actual.buttons = BUTTON_USE;
	result = SG_HookLiveValidateFinalCommand(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &expected, &actual);
	CHECK(result.outcome == SG_HOOK_LIVE_FALLBACK &&
	      result.failure == SG_HOOK_LIVE_FAILURE_FINAL_COMMAND &&
	      !owner.active && owner.replay_link == -1);

	SG_HookLiveReset(&owner.replay, &owner.active, &owner.replay_link,
	    &owner.guard);
	CHECK(!owner.active && owner.replay_link == -1 &&
	      owner.replay.progress.status == SG_REPLAY_RUNNING);
	memset(&actual, 0x5a, sizeof(actual));
	SG_HookLiveZeroCommand(&actual);
	CHECK(actual.msec == SG_REPLAY_STEP_MS && actual.buttons == 0 &&
	      actual.forwardmove == 0 && actual.sidemove == 0 && actual.upmove == 0 &&
	      actual.angles[0] == 0 && actual.angles[1] == 0 && actual.angles[2] == 0);
}

static void TestSettlementReleaseAnnulus(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(50.0f, 0.0f, 0.0f, true);
	sg_replay_observation_t observation = Observation();
	sg_hook_live_result_t result;
	usercmd_t command;

	/* 50 units is release-ready but not settled: the independent legacy
	 * builder must preserve planar movement and match the frozen reducer. */
	OwnerInit(&owner);
	owner.active = true;
	owner.replay_link = 17;
	owner.replay.phase = SG_HOOK_REPLAY_SETTLE;
	VectorSet(owner.replay.spec.destination, 100.0f, 0.0f, 0.0f);
	owner.replay.spec.settle_limit_ms = SG_RUNE_PROOF_HOOK_DRY_SETTLE_MS;
	owner.replay.spec.expected_settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	owner.replay.spec.expected_settle_ms = SG_REPLAY_TIME_DISCOVER;
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_HookLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, true, &pose, &observation, LegacyCommand,
	    &command, &owner.guard);
	CHECK(result.outcome == SG_HOOK_LIVE_RUNNING && owner.active &&
	      command.msec == SG_REPLAY_STEP_MS && command.forwardmove == 400 &&
	      command.buttons == 0);
}

int main(void)
{
	TestOrderedHookAndZeroSettlement();
	TestLateAttachBridge();
	TestStaleIdentityAndDifferentialFallback();
	TestEventOrderAndFinalCommand();
	TestSettlementReleaseAnnulus();
	if (failures)
	{
		fprintf(stderr, "%d sg_hook_live tests failed\\n", failures);
		return 1;
	}
	printf("sg_hook_live tests passed\\n");
	return 0;
}
