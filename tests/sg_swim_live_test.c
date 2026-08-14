/* Direct, host-free regression tests for the ordinary live SWIM adapter. */
#include "q_shared.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_swim_live.h"

typedef struct test_owner_s
{
	sg_swim_replay_state_t replay;
	qboolean active;
	int replay_link;
	qboolean validated;
	int proved_ms;
	int elapsed_ms;
} test_owner_t;

typedef struct test_arrival_s
{
	int calls;
	int pusher_epoch;
	int minimum_epoch;
	qboolean allow;
} test_arrival_t;

static int failures;
static int legacy_command_calls;
static int legacy_command_mismatch;
static int legacy_command_fail;
static vec3_t test_destination = { 100.0f, 0.0f, 0.0f };

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static qboolean TestCommandEqual(const usercmd_t *first,
	const usercmd_t *second)
{
	int axis;

	if (!first || !second || first->msec != second->msec ||
	    first->buttons != second->buttons ||
	    first->forwardmove != second->forwardmove ||
	    first->sidemove != second->sidemove ||
	    first->upmove != second->upmove ||
	    first->impulse != second->impulse ||
	    first->lightlevel != second->lightlevel)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (first->angles[axis] != second->angles[axis])
			return false;
	return true;
}

/* The production call site passes SG_SwimCommand.  This host-free callback is
 * the same legacy law and can inject a one-field drift to refute the adapter's
 * differential guard without modifying the frozen reducer. */
static qboolean TestLegacyCommand(const vec3_t origin,
	const vec3_t destination, const pmove_state_t *pms, usercmd_t *command)
{
	vec3_t direction;
	float horizontal, yaw, pitch;
	byte msec;

	legacy_command_calls++;
	if (!origin || !destination || !pms || !command)
		return false;
	msec = command->msec;
	memset(command, 0, sizeof(*command));
	command->msec = msec;
	if (msec != SG_REPLAY_STEP_MS)
		return false;
	VectorSubtract(destination, origin, direction);
	horizontal = sqrtf(direction[0] * direction[0] +
	                   direction[1] * direction[1]);
	if (!isfinite(horizontal) || !isfinite(direction[2]) ||
	    (horizontal < 0.01f && fabsf(direction[2]) < 0.01f))
		return false;
	yaw = atan2f(direction[1], direction[0]) *
	      180.0f / (float)M_PI;
	pitch = -atan2f(direction[2], horizontal) *
	        180.0f / (float)M_PI;
	if (pitch > 85.0f)
		pitch = 85.0f;
	if (pitch < -85.0f)
		pitch = -85.0f;
	command->angles[YAW] = ANGLE2SHORT(yaw) - pms->delta_angles[YAW];
	command->angles[PITCH] = ANGLE2SHORT(pitch) - pms->delta_angles[PITCH];
	command->angles[ROLL] = -pms->delta_angles[ROLL];
	command->forwardmove = 400;
	if (legacy_command_mismatch)
		command->buttons = BUTTON_USE;
	return !legacy_command_fail;
}

static qboolean TestArrival(const sg_swim_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	test_arrival_t *arrival = (test_arrival_t *)context;

	CHECK(spec != NULL);
	CHECK(pose != NULL);
	CHECK(arrival != NULL);
	if (!arrival)
		return false;
	arrival->calls++;
	CHECK(arrival->pusher_epoch >= arrival->minimum_epoch);
	return arrival->allow;
}

static sg_replay_pose_t TestPose(float x, float y, float z)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	VectorSet(pose.origin, x, y, z);
	pose.grounded = true;
	return pose;
}

static void TestOwnerInit(test_owner_t *owner)
{
	memset(owner, 0, sizeof(*owner));
	owner->replay_link = -1;
}

static sg_swim_live_result_t TestBegin(test_owner_t *owner,
	const sg_replay_pose_t *pose, qboolean destination_water,
	int expected_arrival_ms, float old_frame_z)
{
	owner->validated = true;
	owner->proved_ms = expected_arrival_ms;
	owner->elapsed_ms = 0;
	return SG_SwimLiveBegin(&owner->replay, &owner->active,
	    &owner->replay_link, 17, test_destination, destination_water,
	    expected_arrival_ms, pose, old_frame_z);
}

static void TestRunCommandFrame(test_owner_t *owner,
	sg_replay_pose_t *pose, usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS])
{
	int step;

	for (step = 0; step < SG_SWIM_LIVE_FRAME_STEPS; step++)
	{
		sg_swim_live_result_t result;
		usercmd_t expected;
		int saved_calls;

		memset(&expected, 0, sizeof(expected));
		expected.msec = SG_REPLAY_STEP_MS;
		memset(&commands[step], 0, sizeof(commands[step]));
		commands[step].msec = SG_REPLAY_STEP_MS;
		saved_calls = legacy_command_calls;
		CHECK(TestLegacyCommand(pose->origin, test_destination,
		                        &pose->pms, &expected));
		legacy_command_calls = saved_calls;
		result = SG_SwimLivePreStep(&owner->replay, &owner->active,
		    &owner->replay_link, 17, pose, test_destination, TestLegacyCommand,
		    &commands[step]);
		CHECK(result.outcome == SG_SWIM_LIVE_RUNNING);
		CHECK(TestCommandEqual(&commands[step], &expected));
		if (step < SG_SWIM_LIVE_FRAME_STEPS - 1)
		{
			result = SG_SwimLivePostStep(&owner->replay,
			    &owner->active, &owner->replay_link, 17, pose);
			CHECK(result.outcome == SG_SWIM_LIVE_RUNNING);
		}
		owner->elapsed_ms += SG_REPLAY_STEP_MS;
	}
}

static void TestFourCommandsAndDeferredTrace(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	sg_swim_live_result_t result;
	int calls_before_fifth;

	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	legacy_command_calls = 0;
	legacy_command_mismatch = 0;
	pose.pms.delta_angles[PITCH] = 137;
	pose.pms.delta_angles[YAW] = -911;
	pose.pms.delta_angles[ROLL] = 23;
	result = TestBegin(&owner, &pose, false, 100, 0.0f);
	CHECK(result.outcome == SG_SWIM_LIVE_RUNNING);
	CHECK(arrival.calls == 0); /* no elapsed-zero arrival/contact call */
	TestRunCommandFrame(&owner, &pose, commands);
	CHECK(legacy_command_calls == SG_SWIM_LIVE_FRAME_STEPS);
	CHECK(owner.replay.progress.elapsed_ms == 75);
	CHECK(owner.replay.progress.step_pending);
	CHECK(arrival.calls == 0); /* no 25/50/75 arrival/contact call */

	/* The live entity/pusher pass moves the body before the deferred sample. */
	pose.origin[0] = 90.0f;
	arrival.pusher_epoch = 1;
	arrival.minimum_epoch = 1;
	arrival.allow = true;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_ARRIVED);
	CHECK(result.arrival_sampled && result.legacy_arrived);
	CHECK(arrival.calls == 1);
	CHECK(owner.replay.progress.elapsed_ms == 100);
	CHECK(!owner.replay.progress.step_pending);
	CHECK(!owner.active && owner.replay_link == -1);

	/* Terminal retirement leaves no active SWIM writer for a fifth command;
	 * the outer frame is free to perform its existing generic handoff. */
	calls_before_fifth = legacy_command_calls;
	CHECK(!owner.active && !owner.replay.progress.step_pending);
	CHECK(legacy_command_calls == calls_before_fifth);
}

static void TestTwoPusherBoundaries(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	sg_swim_live_result_t result;

	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	CHECK(TestBegin(&owner, &pose, false, 200, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	CHECK(arrival.calls == 0);
	arrival.pusher_epoch = 1;
	arrival.minimum_epoch = 1;
	arrival.allow = false;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_RUNNING);
	CHECK(arrival.calls == 1);

	TestRunCommandFrame(&owner, &pose, commands);
	CHECK(arrival.calls == 1);
	pose.origin[0] = 90.0f;
	arrival.pusher_epoch = 2;
	arrival.minimum_epoch = 2;
	arrival.allow = true;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_ARRIVED);
	CHECK(arrival.calls == 2); /* exactly one call at each 100/200 boundary */
}

static void TestEarlyLateAndBlocked(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose;
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	sg_swim_live_result_t result;

	/* Early arrival is cached once, then handed to legacy timing fallback. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, false, 200, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	pose.origin[0] = 90.0f;
	arrival.allow = true;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.replay_reason == SG_REPLAY_REASON_TIMING_MISMATCH);
	CHECK(result.arrival_sampled && result.legacy_arrived);
	CHECK(arrival.calls == 1);
	CHECK(owner.validated && owner.proved_ms == 200 && owner.elapsed_ms == 100);

	/* If the live clock has already passed the pending reducer boundary, the
	 * adapter does not revive or trace stale state.  The caller performs its
	 * retained one legacy sample and classifies the arrival as late. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	owner.elapsed_ms = 200;
	pose.origin[0] = 90.0f;
	arrival.allow = true;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_CADENCE);
	CHECK(!result.arrival_sampled && arrival.calls == 0);
	CHECK(TestArrival(&owner.replay.spec, &pose, &arrival));
	CHECK(arrival.calls == 1 && owner.elapsed_ms > owner.proved_ms);

	/* A blocked contact at the expected boundary is the existing timing fail. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	pose.origin[0] = 90.0f;
	arrival.allow = false;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.replay_reason == SG_REPLAY_REASON_TIMING_MISMATCH);
	CHECK(result.arrival_sampled && !result.legacy_arrived);

	/* Remaining blocked through a later expected boundary is still blocked. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, false, 200, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.replay_reason == SG_REPLAY_REASON_TIMING_MISMATCH);
}

static void TestHazardFallbackBoundaries(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	usercmd_t command;
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	sg_swim_live_result_t result;
	int step;

	CHECK(SG_SWIM_LIVE_EARLY_HAZARD_SHELF_SECONDS == 60.0f);
	CHECK(SG_SWIM_LIVE_TIMING_SHELF_SECONDS == 10.0f);
	TestOwnerInit(&owner);
	CHECK(TestBegin(&owner, &pose, false, 200, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_SwimLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, test_destination, TestLegacyCommand,
	    &command);
	CHECK(result.outcome == SG_SWIM_LIVE_RUNNING);
	owner.elapsed_ms += SG_REPLAY_STEP_MS;
	pose.waterlevel = 1;
	pose.watertype = CONTENTS_LAVA;
	result = SG_SwimLivePostStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_HAZARDOUS_LIQUID);
	CHECK(owner.validated && owner.proved_ms == 200 && owner.elapsed_ms == 25);
	/* The already-computed outer frame still has three legacy commands. */
	pose.waterlevel = 0;
	pose.watertype = 0;
	for (step = 1; step < SG_SWIM_LIVE_FRAME_STEPS; step++)
	{
		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		CHECK(TestLegacyCommand(pose.origin, test_destination,
		                        &pose.pms, &command));
		owner.elapsed_ms += SG_REPLAY_STEP_MS;
	}
	CHECK(owner.elapsed_ms == 100);

	/* Hazard first visible at the expected boundary remains the generic 10 s
	 * timing path; it does not get reclassified as the early 60 s interrupt. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	pose.waterlevel = 1;
	pose.watertype = CONTENTS_SLIME;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.replay_reason == SG_REPLAY_REASON_HAZARDOUS_LIQUID);
	CHECK(result.arrival_sampled && !result.legacy_arrived);
	CHECK(owner.elapsed_ms == owner.proved_ms);
}

static void TestSurvivedFallAndDifferentials(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	usercmd_t command;
	sg_swim_live_result_t result;

	/* Live gameplay already decided that the bot survived.  Reducer-only fall
	 * rejection therefore deactivates to the retained legacy terminal result. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	CHECK(TestBegin(&owner, &pose, false, 100, -600.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	pose.origin[0] = 90.0f;
	pose.velocity[2] = 0.0f;
	pose.grounded = true;
	arrival.allow = true;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.replay_reason == SG_REPLAY_REASON_DAMAGING_FALL);
	CHECK(result.arrival_sampled && result.legacy_arrived);
	CHECK(owner.validated && owner.elapsed_ms == owner.proved_ms);

	/* A one-field command drift is detected without comparing structure
	 * padding, and the emitted command is the initialized legacy fallback. */
	TestOwnerInit(&owner);
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	legacy_command_mismatch = 1;
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_SwimLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, test_destination, TestLegacyCommand,
	    &command);
	legacy_command_mismatch = 0;
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_COMMAND_DIFFERENTIAL);
	CHECK(command.buttons == BUTTON_USE && command.msec == 25);
	CHECK(owner.validated && owner.proved_ms == 100 && owner.elapsed_ms == 0);

	/* Post without a pending command is a state/cadence mismatch fallback. */
	TestOwnerInit(&owner);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	result = SG_SwimLivePostStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_CADENCE);
}

static void TestAdmissionAndOwnershipFallbacks(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	usercmd_t command, expected;
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	sg_swim_live_result_t result;

	/* A reducer-only Begin rejection does not revoke the successful online
	 * admission or its exact legacy timing witness. */
	TestOwnerInit(&owner);
	result = TestBegin(&owner, &pose, false, 125, 0.0f);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_BEGIN);
	CHECK(owner.validated && owner.proved_ms == 125 && owner.elapsed_ms == 0);
	CHECK(!owner.active && owner.replay_link == -1);

	/* Stale identity cannot drive or trace the current link.  PreStep still
	 * returns that current link's fully initialized legacy command. */
	TestOwnerInit(&owner);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	owner.replay_link = 99;
	memset(&command, 0x5a, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	memset(&expected, 0, sizeof(expected));
	expected.msec = SG_REPLAY_STEP_MS;
	CHECK(TestLegacyCommand(pose.origin, test_destination,
	                        &pose.pms, &expected));
	result = SG_SwimLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, test_destination, TestLegacyCommand,
	    &command);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_LINK);
	CHECK(TestCommandEqual(&command, &expected));
	CHECK(!owner.active && owner.replay_link == -1);

	/* Frozen reducer state corruption is likewise diagnostic-only for live
	 * behavior: the initialized legacy byte stream remains authoritative. */
	TestOwnerInit(&owner);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	owner.replay.progress.status = SG_REPLAY_FAILED;
	owner.replay.progress.reason = SG_REPLAY_REASON_INVALID_STATE;
	memset(&command, 0x5a, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_SwimLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, test_destination, TestLegacyCommand,
	    &command);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_REDUCER_CONTROL);
	CHECK(TestCommandEqual(&command, &expected));

	/* The legacy controller's own failure remains a legacy command/result,
	 * never an excuse to emit the reducer command authoritatively. */
	TestOwnerInit(&owner);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	legacy_command_fail = 1;
	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	result = SG_SwimLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, test_destination, TestLegacyCommand,
	    &command);
	legacy_command_fail = 0;
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_LEGACY_CONTROL);
	CHECK(TestCommandEqual(&command, &expected));

	/* A malformed outer cadence preserves the legacy controller's own
	 * initialized failure command instead of silently manufacturing 25 ms. */
	TestOwnerInit(&owner);
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	memset(&command, 0x5a, sizeof(command));
	command.msec = 20;
	result = SG_SwimLivePreStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, test_destination, TestLegacyCommand,
	    &command);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_LEGACY_CONTROL);
	CHECK(command.msec == 20 && command.forwardmove == 0 &&
	      command.buttons == 0);

	/* A link mismatch at the deferred boundary must not call the contact
	 * predicate.  The caller can execute its one retained legacy sample. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	owner.replay_link = 99;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_FALLBACK);
	CHECK(result.failure == SG_SWIM_LIVE_FAILURE_LINK);
	CHECK(!result.arrival_sampled && arrival.calls == 0);
}

static void TestResetAndZeroCadence(void)
{
	test_owner_t owner;
	usercmd_t zero[2][SG_SWIM_LIVE_FRAME_STEPS];
	int path, step, axis;
	int swim_air_seed = 23;

	/* The BUSY and FAIL admission paths independently spend the same four
	 * initialized zero-input 25 ms commands. */
	memset(zero, 0x5a, sizeof(zero));
	for (path = 0; path < 2; path++)
	{
		SG_SwimLiveZeroFrame(zero[path]);
		for (step = 0; step < SG_SWIM_LIVE_FRAME_STEPS; step++)
		{
			CHECK(zero[path][step].msec == 25);
			CHECK(zero[path][step].buttons == 0);
			for (axis = 0; axis < 3; axis++)
				CHECK(zero[path][step].angles[axis] == 0);
			CHECK(zero[path][step].forwardmove == 0);
			CHECK(zero[path][step].sidemove == 0);
			CHECK(zero[path][step].upmove == 0);
			CHECK(zero[path][step].impulse == 0);
			CHECK(zero[path][step].lightlevel == 0);
		}
	}

	memset(&owner, 0x5a, sizeof(owner));
	owner.active = true;
	owner.replay_link = 17;
	owner.validated = true;
	owner.proved_ms = 200;
	owner.elapsed_ms = 100;
	SG_SwimLiveReset(&owner.replay, &owner.active, &owner.replay_link,
	    &owner.validated, &owner.proved_ms, &owner.elapsed_ms);
	CHECK(!owner.active && owner.replay_link == -1);
	CHECK(!owner.validated && owner.proved_ms == 0 && owner.elapsed_ms == 0);
	CHECK(owner.replay.progress.status == SG_REPLAY_RUNNING);
	CHECK(swim_air_seed == 23); /* reset cannot clear life/dry breath state */
	CHECK(strcmp(SG_SwimLiveFailureName(
	    SG_SWIM_LIVE_FAILURE_COMMAND_DIFFERENTIAL),
	    "command-differential") == 0);
}

static void TestResetSitesAndLegacyExclusions(void)
{
	/* Slot/life/authority/stale-rope/proof-fail/hazard/terminal/new-link all
	 * invoke this one policy.  Exercise each reset-site class independently so
	 * a later state-field addition cannot leave one scenario partially live. */
	static const char *const reset_sites[] = {
		"slot", "life", "authority", "stale-rope", "proof-fail",
		"hazard", "terminal", "new-link"
	};
	test_owner_t owner;
	sg_replay_pose_t pose = TestPose(0.0f, 0.0f, 0.0f);
	usercmd_t command;
	vec3_t ascent = { 0.0f, 0.0f, 256.0f };
	int swim_air_seed = 31;
	int site, step;

	for (site = 0; site < (int)(sizeof(reset_sites) /
	                              sizeof(reset_sites[0])); site++)
	{
		(void)reset_sites[site];
		memset(&owner, 0x5a, sizeof(owner));
		owner.active = true;
		owner.replay_link = 17;
		owner.validated = true;
		owner.proved_ms = 200;
		owner.elapsed_ms = 100;
		SG_SwimLiveReset(&owner.replay, &owner.active, &owner.replay_link,
		    &owner.validated, &owner.proved_ms, &owner.elapsed_ms);
		CHECK(!owner.active && owner.replay_link == -1);
		CHECK(!owner.validated && owner.proved_ms == 0 &&
		      owner.elapsed_ms == 0);
		CHECK(swim_air_seed == 31);
	}

	/* Water-origin TELEPORT never admits the ordinary reducer: its approach
	 * remains four calls to the retained legacy swim controller. */
	TestOwnerInit(&owner);
	for (step = 0; step < SG_SWIM_LIVE_FRAME_STEPS; step++)
	{
		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		CHECK(TestLegacyCommand(pose.origin, test_destination,
		                        &pose.pms, &command));
	}
	CHECK(!owner.active && owner.replay_link == -1);

	/* Emergency ascent first retires ordinary action state, then likewise
	 * emits the legacy ascent controller for the complete four-command frame. */
	CHECK(TestBegin(&owner, &pose, false, 200, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	SG_SwimLiveReset(&owner.replay, &owner.active, &owner.replay_link,
	    &owner.validated, &owner.proved_ms, &owner.elapsed_ms);
	for (step = 0; step < SG_SWIM_LIVE_FRAME_STEPS; step++)
	{
		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		CHECK(TestLegacyCommand(pose.origin, ascent, &pose.pms, &command));
		CHECK(command.msec == 25 && command.forwardmove == 400);
	}
	CHECK(!owner.active && owner.replay_link == -1 && swim_air_seed == 31);
}

static void TestDryWaterTransitions(void)
{
	test_owner_t owner;
	test_arrival_t arrival;
	sg_replay_pose_t pose;
	usercmd_t commands[SG_SWIM_LIVE_FRAME_STEPS];
	sg_swim_live_result_t result;

	/* Dry to water: authoritative water state is sampled after the mover pass. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	CHECK(TestBegin(&owner, &pose, true, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	pose.origin[0] = 90.0f;
	pose.grounded = false;
	pose.waterlevel = 2;
	pose.watertype = CONTENTS_WATER;
	arrival.allow = true;
	arrival.pusher_epoch = arrival.minimum_epoch = 1;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_ARRIVED);

	/* Water to dry uses the same reducer but the dry support terminal. */
	TestOwnerInit(&owner);
	memset(&arrival, 0, sizeof(arrival));
	pose = TestPose(0.0f, 0.0f, 0.0f);
	pose.grounded = false;
	pose.waterlevel = 3;
	pose.watertype = CONTENTS_WATER;
	CHECK(TestBegin(&owner, &pose, false, 100, 0.0f).outcome ==
	      SG_SWIM_LIVE_RUNNING);
	TestRunCommandFrame(&owner, &pose, commands);
	pose.origin[0] = 90.0f;
	pose.grounded = true;
	pose.waterlevel = 0;
	pose.watertype = 0;
	arrival.allow = true;
	arrival.pusher_epoch = arrival.minimum_epoch = 2;
	result = SG_SwimLiveBoundary(&owner.replay, &owner.active,
	    &owner.replay_link, 17, owner.elapsed_ms, &pose,
	    TestArrival, &arrival);
	CHECK(result.outcome == SG_SWIM_LIVE_ARRIVED);
}

int main(void)
{
	TestFourCommandsAndDeferredTrace();
	TestTwoPusherBoundaries();
	TestEarlyLateAndBlocked();
	TestHazardFallbackBoundaries();
	TestSurvivedFallAndDifferentials();
	TestAdmissionAndOwnershipFallbacks();
	TestResetAndZeroCadence();
	TestResetSitesAndLegacyExclusions();
	TestDryWaterTransitions();
	if (failures)
	{
		fprintf(stderr, "sg_swim_live_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("sg_swim_live_test: ok\n");
	return 0;
}
