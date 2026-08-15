/* Direct, host-free regression tests for ordinary revision-2 live DROP. */
#include "q_shared.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_drop_live.h"

typedef struct test_owner_s
{
	sg_drop_replay_state_t replay;
	qboolean active;
	int replay_link;
} test_owner_t;

typedef struct test_contact_s
{
	int arrival_calls;
	int recovery_calls;
	int pusher_epoch;
	int minimum_epoch;
	qboolean arrival_result;
	qboolean recovery_result;
} test_contact_t;

static int failures;
static int shadow_calls;
static int shadow_mismatch;
static int shadow_fail;
static sg_drop_live_events_t live_events;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static qboolean CommandEqual(const usercmd_t *first,
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

static qboolean ShadowCommand(const sg_drop_replay_state_t *state,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	vec3_t direction;
	double yaw;
	byte msec;

	shadow_calls++;
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
		yaw = atan2f(direction[1], direction[0]) * 180.0f / M_PI;
	}
	else if (state->walkoff)
		yaw = state->spec.heading * (360.0f / 256.0f);
	else
	{
		VectorSubtract(state->spec.lip, pose->origin, direction);
		yaw = atan2f(direction[1], direction[0]) * 180.0f / M_PI;
	}
	command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(yaw) -
	                       pose->pms.delta_angles[YAW];
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	command->forwardmove = 400;
	if (shadow_mismatch)
		command->buttons = BUTTON_USE;
	return !shadow_fail;
}

static qboolean Arrival(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	test_contact_t *contact = (test_contact_t *)context;

	CHECK(spec != NULL && pose != NULL && contact != NULL);
	if (!contact)
		return false;
	contact->arrival_calls++;
	CHECK(contact->pusher_epoch >= contact->minimum_epoch);
	return contact->arrival_result;
}

static qboolean Recovery(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	test_contact_t *contact = (test_contact_t *)context;

	CHECK(spec != NULL && pose != NULL && contact != NULL);
	if (!contact)
		return false;
	contact->recovery_calls++;
	CHECK(contact->pusher_epoch >= contact->minimum_epoch);
	return contact->recovery_result;
}

static sg_replay_pose_t Pose(float x, float y, float z, qboolean grounded)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	VectorSet(pose.origin, x, y, z);
	pose.grounded = grounded;
	return pose;
}

static void OwnerInit(test_owner_t *owner)
{
	memset(owner, 0, sizeof(*owner));
	owner->replay_link = -1;
	memset(&live_events, 0, sizeof(live_events));
}

static sg_drop_live_events_t EventsTake(void)
{
	sg_drop_live_events_t events = live_events;

	memset(&live_events, 0, sizeof(live_events));
	return events;
}

static sg_drop_live_result_t Begin(test_owner_t *owner,
	const sg_replay_pose_t *pose, const vec3_t destination, const vec3_t lip,
	qboolean destination_water, int expected_arrival_ms, float old_frame_z)
{
	sg_drop_live_events_t events = EventsTake();

	return SG_DropLiveBegin(&owner->replay, &owner->active,
	    &owner->replay_link, 17, destination, lip, 0, destination_water,
	    expected_arrival_ms, pose, pose->grounded, old_frame_z, &events);
}

static sg_drop_live_result_t Command(test_owner_t *owner,
	sg_replay_pose_t *pose, usercmd_t *command, qboolean complete)
{
	sg_drop_live_result_t result;

	memset(command, 0x5a, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	result = SG_DropLivePreStep(&owner->replay, &owner->active,
	    &owner->replay_link, 17, pose, ShadowCommand, command);
	if (result.outcome != SG_DROP_LIVE_RUNNING || !complete)
		return result;
	{
		sg_drop_live_events_t events = EventsTake();

		return SG_DropLivePostStep(&owner->replay, &owner->active,
		    &owner->replay_link, 17, pose, pose->grounded, &events);
	}
}

static void RunFrame(test_owner_t *owner, sg_replay_pose_t *pose,
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS], qboolean airborne)
{
	int step;

	for (step = 0; step < SG_DROP_LIVE_FRAME_STEPS; step++)
	{
		sg_drop_live_result_t result;
		usercmd_t expected;
		int saved_calls;

		if (airborne)
			pose->grounded = false;
		memset(&expected, 0, sizeof(expected));
		expected.msec = SG_REPLAY_STEP_MS;
		/* PreStep may latch walkoff, so compare the adapter result to an
		 * independent reconstruction after it returns below. */
		result = Command(owner, pose, &commands[step], false);
		CHECK(result.outcome == SG_DROP_LIVE_RUNNING);
		saved_calls = shadow_calls;
		CHECK(ShadowCommand(&owner->replay, pose, &expected));
		shadow_calls = saved_calls;
		CHECK(CommandEqual(&commands[step], &expected));
		if (step < SG_DROP_LIVE_FRAME_STEPS - 1)
		{
			sg_drop_live_events_t events = EventsTake();

			result = SG_DropLivePostStep(&owner->replay, &owner->active,
			    &owner->replay_link, 17, pose, pose->grounded, &events);
			CHECK(result.outcome == SG_DROP_LIVE_RUNNING);
		}
	}
}

static sg_drop_live_result_t Boundary(test_owner_t *owner,
	sg_replay_pose_t *pose, test_contact_t *contact)
{
	sg_drop_live_events_t events = EventsTake();

	return SG_DropLiveBoundary(&owner->replay, &owner->active,
	    &owner->replay_link, 17, pose, pose->grounded, &events, Arrival,
	    Recovery, contact);
}

static void TestCommandsAndDeferredBoundary(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 100.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	sg_drop_live_result_t result;

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	shadow_calls = 0;
	pose.pms.delta_angles[PITCH] = 3;
	pose.pms.delta_angles[YAW] = 5;
	pose.pms.delta_angles[ROLL] = 7;
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	CHECK(shadow_calls == 4);
	CHECK(owner.replay.progress.elapsed_ms == 75 &&
	      owner.replay.progress.step_pending);
	CHECK(contact.arrival_calls == 0 && contact.recovery_calls == 0);
	pose = Pose(100.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = 1;
	contact.minimum_epoch = 1;
	contact.arrival_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_ARRIVED);
	CHECK(result.arrival_sampled && result.arrived &&
	      !result.recovery_sampled);
	CHECK(contact.arrival_calls == 1 && contact.recovery_calls == 0);
	CHECK(owner.replay.progress.elapsed_ms == 100 &&
	      !owner.replay.progress.step_pending && !owner.active &&
	      owner.replay_link == -1);
}

static void TestTimingAndTwoPusherEpochs(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose;
	vec3_t destination = { 100.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	sg_drop_live_result_t result;

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(100.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.arrival_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_TIMING_MISMATCH);
	CHECK(contact.arrival_calls == 1);

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(100.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 2;
	contact.arrival_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_ARRIVED);
	CHECK(contact.arrival_calls == 2);

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_TIMING_MISMATCH);
}

static void TestAirborneAndContactPolicy(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose;
	vec3_t destination = { 200.0f, 0.0f, 0.0f };
	vec3_t close_destination = { 16.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	sg_drop_live_result_t result;

	/* A close supported lip handoff is not terminal and performs no arrival
	 * trace.  Once an airborne step has been observed, the same supported pose
	 * receives terminal first refusal at the next production boundary. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, close_destination, lip, false, 200,
	            0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, false);
	pose = Pose(16.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.arrival_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && !owner.replay.airborne &&
	      !result.arrival_sampled && !result.recovery_sampled);
	CHECK(contact.arrival_calls == 0 && contact.recovery_calls == 0);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(16.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 2;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_ARRIVED && result.arrival_sampled &&
	      result.arrived && contact.arrival_calls == 1 &&
	      contact.recovery_calls == 0);

	/* Wet destination: a dry shelf never samples or starts recovery. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, true, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(120.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.recovery_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_SHORT_LANDING);
	CHECK(!result.recovery_sampled && contact.recovery_calls == 0);

	/* Dry deep-water contact gives terminal first refusal, then rejects. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(200.0f, 0.0f, 0.0f, false);
	pose.waterlevel = 2;
	pose.watertype = CONTENTS_WATER;
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.arrival_result = true;
	CHECK(Boundary(&owner, &pose, &contact).outcome == SG_DROP_LIVE_ARRIVED);

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(200.0f, 0.0f, 0.0f, false);
	pose.waterlevel = 2;
	pose.watertype = CONTENTS_WATER;
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_SHORT_LANDING);
}

static void TestRecoveryAndFailureDifferentials(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose;
	vec3_t destination = { 200.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	usercmd_t command;
	sg_drop_live_result_t result;

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 300, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(120.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.recovery_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && result.recovery_started &&
	      result.recovery_sampled && owner.replay.recovery);
	CHECK(contact.arrival_calls == 1 && contact.recovery_calls == 1);
	result = Command(&owner, &pose, &command, false);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && command.forwardmove == 400);
	pose.grounded = false;
	{
		sg_drop_live_events_t events = EventsTake();

		result = SG_DropLivePostStep(&owner.replay, &owner.active,
		    &owner.replay_link, 17, &pose, false, &events);
	}
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_RECOVERY_LOST);

	/* At an aligned boundary terminal precedes recovery revalidation.  The
	 * terminal callback runs first and a successful dry depth-2 arrival skips
	 * the recovery callback, despite no grounded support at that pose. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(120.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.recovery_result = true;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && result.recovery_started &&
	      contact.arrival_calls == 1 && contact.recovery_calls == 1);
	RunFrame(&owner, &pose, commands, false);
	pose = Pose(200.0f, 0.0f, 0.0f, false);
	pose.waterlevel = 2;
	pose.watertype = CONTENTS_WATER;
	contact.pusher_epoch = contact.minimum_epoch = 2;
	contact.arrival_result = true;
	contact.recovery_result = false;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_ARRIVED && result.arrived &&
	      result.arrival_sampled && !result.recovery_sampled &&
	      contact.arrival_calls == 2 && contact.recovery_calls == 1);

	/* Logical-field drift is detected without structure-padding dependence. */
	OwnerInit(&owner);
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	shadow_mismatch = 1;
	result = Command(&owner, &pose, &command, false);
	shadow_mismatch = 0;
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL);
	CHECK(command.buttons == BUTTON_USE && command.msec == 25 &&
	      !owner.active && owner.replay_link == -1);

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	{
		sg_drop_live_events_t events = EventsTake();

		result = SG_DropLivePostStep(&owner.replay, &owner.active,
		    &owner.replay_link, 17, &pose, true, &events);
	}
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_CADENCE &&
	      !owner.active && owner.replay_link == -1);
}

static void TestPrecisionOwnershipAndReset(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 1000.0f, 0.0f, 0.0f };
	vec3_t lip = { -94.25f, -0.75f, 0.0f };
	usercmd_t command;
	sg_drop_live_result_t result;

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	owner.replay.spec.heading = 128;
	result = Command(&owner, &pose, &command, false);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING);
	CHECK(!owner.replay.walkoff && command.angles[YAW] == (short)-32684);

	/* Stale identity cannot emit or revive a reducer command. */
	owner.replay_link = 99;
	result = Command(&owner, &pose, &command, false);
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_LINK);
	CHECK(!owner.active && owner.replay_link == -1);

	memset(&owner, 0x5a, sizeof(owner));
	owner.active = true;
	owner.replay_link = 17;
	SG_DropLiveReset(&owner.replay, &owner.active, &owner.replay_link,
	                 &live_events);
	CHECK(!owner.active && owner.replay_link == -1 &&
	      owner.replay.progress.elapsed_ms == 0 &&
	      !owner.replay.progress.step_pending);
	memset(&command, 0x5a, sizeof(command));
	SG_DropLiveZeroCommand(&command);
	CHECK(command.msec == 25 && command.buttons == 0 &&
	      command.angles[0] == 0 && command.angles[1] == 0 &&
	      command.angles[2] == 0 && command.forwardmove == 0 &&
	      command.sidemove == 0 && command.upmove == 0 &&
	      command.impulse == 0 && command.lightlevel == 0);
	CHECK(strcmp(SG_DropLiveFailureName(
	    SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL),
	    "command-differential") == 0);
}

static void TestCanonicalYawCounterexample(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 1000.0f, 0.0f, 0.0f };
	vec3_t lip = { 31.75f, -27.25f, 0.0f };
	usercmd_t command, final_command;
	sg_drop_live_result_t result;
	double canonical_yaw;
	float narrowed_yaw;
	short shared_command_yaw;

	canonical_yaw = atan2f(lip[1], lip[0]) * 180.0f / M_PI;
	narrowed_yaw = (float)canonical_yaw;
	CHECK((unsigned short)ANGLE2SHORT(canonical_yaw) == 58139u);
	CHECK((unsigned short)ANGLE2SHORT(narrowed_yaw) == 58138u);
	CHECK(SG_DropReplayPlanarYawCommand(lip[0], lip[1], 0,
	                                  &shared_command_yaw));
	CHECK((unsigned short)shared_command_yaw == 58139u);

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	result = Command(&owner, &pose, &command, false);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING);
	CHECK(command.msec == SG_REPLAY_STEP_MS &&
	      (unsigned short)command.angles[YAW] == 58139u &&
	      command.angles[PITCH] == 0 && command.angles[ROLL] == 0 &&
	      command.forwardmove == 400 && command.sidemove == 0 &&
	      command.upmove == 0 && command.buttons == 0 &&
	      command.impulse == 0 && command.lightlevel == 0);
	final_command = command;
	result = SG_DropLiveValidateFinalCommand(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &command, &final_command);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && owner.active &&
	      owner.replay_link == 17);
}

static void TestDynamicEventCadenceAndFailClosed(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 1000.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	usercmd_t command;
	sg_drop_live_events_t events;
	sg_drop_live_result_t result;

	/* Host latches are monotonic within a command.  The g_phys observer calls
	 * the same atom at its authoritative contact point before displacement, so
	 * both a successful door push and a later blocked/rolled-back push survive
	 * to the reducer boundary.  Clean world/support contacts call it with no
	 * event and cannot erase an already observed dynamic contact. */
	memset(&live_events, 0, sizeof(live_events));
	CHECK(SG_DropLiveEventsLatch(&live_events, false, false));
	CHECK(!live_events.contaminated && !live_events.door_passed);
	CHECK(SG_DropLiveEventsLatch(&live_events, false, true));
	CHECK(!live_events.contaminated && live_events.door_passed);
	CHECK(SG_DropLiveEventsLatch(&live_events, true, false));
	CHECK(live_events.contaminated && live_events.door_passed);
	CHECK(!SG_DropLiveEventsLatch(&live_events, (qboolean)2, false));
	/* The production pre-ClientThink atom clears stale events first, then
	 * installs a deferred Begin-source door exactly once. */
	{
		qboolean source_door_pending = true;

		CHECK(SG_DropLiveEventsBeginCommand(&live_events,
		                                     &source_door_pending));
		CHECK(!live_events.contaminated && live_events.door_passed &&
		      !source_door_pending);
		CHECK(SG_DropLiveEventsBeginCommand(&live_events,
		                                     &source_door_pending));
		CHECK(!live_events.contaminated && !live_events.door_passed);
	}

	/* A clean snapshot at every reducer observation remains a clean control. */
	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	memset(&contact, 0, sizeof(contact));
	contact.pusher_epoch = contact.minimum_epoch = 1;
	CHECK(Boundary(&owner, &pose, &contact).outcome == SG_DROP_LIVE_RUNNING);
	CHECK(owner.replay.progress.elapsed_ms == 100 && owner.active);

	/* Actual dynamic contamination is consumed at its exact 25 ms post-step. */
	OwnerInit(&owner);
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	CHECK(Command(&owner, &pose, &command, false).outcome ==
	      SG_DROP_LIVE_RUNNING);
	live_events.contaminated = true;
	events = EventsTake();
	result = SG_DropLivePostStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, true, &events);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.failure == SG_DROP_LIVE_FAILURE_POSTSTEP &&
	      result.replay_reason == SG_REPLAY_REASON_CONTAMINATED &&
	      !owner.active && owner.replay_link == -1);

	/* A validated live door event has its distinct canonical reason. */
	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	CHECK(Command(&owner, &pose, &command, false).outcome ==
	      SG_DROP_LIVE_RUNNING);
	live_events.door_passed = true;
	events = EventsTake();
	result = SG_DropLivePostStep(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &pose, true, &events);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.failure == SG_DROP_LIVE_FAILURE_POSTSTEP &&
	      result.replay_reason == SG_REPLAY_REASON_DOOR_PASSED &&
	      !owner.active && owner.replay_link == -1);

	/* The fourth command is pending: an intervening pusher/trigger event must
	 * survive until Boundary and must preempt both terminal callbacks. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	live_events.door_passed = true;
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.failure == SG_DROP_LIVE_FAILURE_BOUNDARY &&
	      result.replay_reason == SG_REPLAY_REASON_DOOR_PASSED &&
	      contact.arrival_calls == 0 && contact.recovery_calls == 0);
	CHECK(!live_events.contaminated && !live_events.door_passed);

	/* Missing or non-boolean host evidence is integration failure, never a
	 * silently clean reducer observation. */
	OwnerInit(&owner);
	memset(&events, 0, sizeof(events));
	events.contaminated = (qboolean)2;
	result = SG_DropLiveBegin(&owner.replay, &owner.active,
	    &owner.replay_link, 17, destination, lip, 0, false, 200, &pose, true,
	    0.0f, &events);
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_BEGIN &&
	      result.replay_reason == SG_REPLAY_REASON_INVALID_ARGUMENT);

	/* A pending command-four event is action-owned.  Every lifecycle reset
	 * clears it before a later link can begin, so stale dynamic evidence cannot
	 * poison the next clean owner. */
	OwnerInit(&owner);
	live_events.contaminated = true;
	live_events.door_passed = true;
	owner.active = true;
	owner.replay_link = 17;
	SG_DropLiveReset(&owner.replay, &owner.active, &owner.replay_link,
	                 &live_events);
	CHECK(!owner.active && owner.replay_link == -1 &&
	      !live_events.contaminated && !live_events.door_passed);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
}

static void TestSourceEventAdmission(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 1000.0f, 0.0f, 0.0f };
	vec3_t lip = { 32.0f, 0.0f, 0.0f };
	usercmd_t command;
	sg_drop_live_result_t result;

	/* A clean read-only source snapshot admits ownership without constructing
	 * or issuing the first command. */
	OwnerInit(&owner);
	shadow_calls = 0;
	result = Begin(&owner, &pose, destination, lip, false, 200, 0.0f);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && owner.active &&
	      owner.replay_link == 17 && shadow_calls == 0);
	result = Command(&owner, &pose, &command, false);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && shadow_calls == 1);

	/* Arbitrary trigger/player/solid overlap is canonical contamination at
	 * elapsed zero.  Begin retires ownership before any command callback runs. */
	OwnerInit(&owner);
	live_events.contaminated = true;
	shadow_calls = 0;
	result = Begin(&owner, &pose, destination, lip, false, 200, 0.0f);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.failure == SG_DROP_LIVE_FAILURE_BEGIN &&
	      result.replay_reason == SG_REPLAY_REASON_CONTAMINATED &&
	      !owner.active && owner.replay_link == -1 && shadow_calls == 0);

	/* Current func_door/sweep occupancy retains DROP's post-command door
	 * policy.  Begin admits it; the production adapter carries this bit to the
	 * first 25 ms observation, where it has the distinct door reason. */
	OwnerInit(&owner);
	live_events.door_passed = true;
	shadow_calls = 0;
	result = Begin(&owner, &pose, destination, lip, false, 200, 0.0f);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && owner.active &&
	      owner.replay_link == 17 && shadow_calls == 0);
	{
		qboolean source_door_pending = true;

		CHECK(SG_DropLiveEventsBeginCommand(&live_events,
		                                     &source_door_pending));
	}
	result = Command(&owner, &pose, &command, true);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.failure == SG_DROP_LIVE_FAILURE_POSTSTEP &&
	      result.replay_reason == SG_REPLAY_REASON_DOOR_PASSED &&
	      !owner.active && owner.replay_link == -1 && shadow_calls == 1);
}

static void TestFinalCommandInvariant(void)
{
	test_owner_t owner;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 1000.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t expected, actual;
	sg_drop_live_result_t result;

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	CHECK(Command(&owner, &pose, &expected, false).outcome ==
	      SG_DROP_LIVE_RUNNING);
	actual = expected;
	result = SG_DropLiveValidateFinalCommand(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &expected, &actual);
	CHECK(result.outcome == SG_DROP_LIVE_RUNNING && owner.active &&
	      owner.replay_link == 17);

	/* Model the reachable late generic-door writer: it used to replace the
	 * proved forward=400 command after PreStep but before ClientThink. */
	actual.forwardmove = -200;
	actual.sidemove = 37;
	result = SG_DropLiveValidateFinalCommand(&owner.replay, &owner.active,
	    &owner.replay_link, 17, &expected, &actual);
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_COMMAND_DIFFERENTIAL &&
	      result.replay_reason == SG_REPLAY_REASON_INVALID_CONTROL &&
	      !owner.active && owner.replay_link == -1);
}

static void TestCanonicalFailuresAndRecoveryRevalidation(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose;
	vec3_t destination = { 1000.0f, 0.0f, 0.0f };
	vec3_t recovery_destination = { 200.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	usercmd_t command;
	sg_drop_live_result_t result;

	/* Harm, fall, shallow wet contact and below-destination escape are reducer
	 * failures, not adapter fallbacks. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose.waterlevel = 1;
	pose.watertype = CONTENTS_LAVA;
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_HAZARDOUS_LIQUID);

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, -600.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose.velocity[2] = 0.0f;
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_DAMAGING_FALL);

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, true, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose.waterlevel = 1;
	pose.watertype = CONTENTS_WATER;
	contact.pusher_epoch = contact.minimum_epoch = 1;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_SHALLOW_WATER_CONTACT);

	OwnerInit(&owner);
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, destination, lip, false, 200, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	CHECK(Command(&owner, &pose, &command, false).outcome ==
	      SG_DROP_LIVE_RUNNING);
	pose.origin[2] = -513.0f;
	{
		sg_drop_live_events_t events = EventsTake();

		result = SG_DropLivePostStep(&owner.replay, &owner.active,
		    &owner.replay_link, 17, &pose, false, &events);
	}
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_BELOW_DESTINATION);

	/* Recovery is traced and fully revalidated again at every later 100 ms. */
	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	CHECK(Begin(&owner, &pose, recovery_destination, lip, false, 300,
	            0.0f).outcome == SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	pose = Pose(120.0f, 0.0f, 0.0f, true);
	contact.pusher_epoch = contact.minimum_epoch = 1;
	contact.recovery_result = true;
	CHECK(Boundary(&owner, &pose, &contact).recovery_started);
	RunFrame(&owner, &pose, commands, false);
	pose = Pose(0.0f, 0.0f, 0.0f, true); /* outside the 96-unit envelope */
	contact.pusher_epoch = contact.minimum_epoch = 2;
	contact.recovery_result = false;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_RECOVERY_LOST);
	CHECK(contact.arrival_calls == 2 && contact.recovery_calls == 2);

	/* The 2500 ms approach cap fails before issuing a new command. */
	OwnerInit(&owner);
	pose = Pose(0.0f, 0.0f, 0.0f, true);
	VectorSet(lip, 100.0f, 0.0f, 0.0f);
	CHECK(Begin(&owner, &pose, destination, lip, false, 300, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	owner.replay.progress.elapsed_ms = SG_REPLAY_DROP_APPROACH_MS;
	result = Command(&owner, &pose, &command, false);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.replay_reason == SG_REPLAY_REASON_APPROACH_TIMEOUT);
}

static void TestIntegrationFallbacksDoNotTrace(void)
{
	test_owner_t owner;
	test_contact_t contact;
	sg_replay_pose_t pose = Pose(0.0f, 0.0f, 0.0f, true);
	vec3_t destination = { 100.0f, 0.0f, 0.0f };
	vec3_t lip = { 0.0f, 0.0f, 0.0f };
	usercmd_t commands[SG_DROP_LIVE_FRAME_STEPS];
	usercmd_t command;
	sg_drop_live_result_t result;

	OwnerInit(&owner);
	result = Begin(&owner, &pose, destination, lip, false, 125, 0.0f);
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_BEGIN &&
	      !owner.active && owner.replay_link == -1);

	/* Canonical Begin failures use the same fail/shelf disposition class as
	 * later reducer failures; malformed integration inputs remain fallbacks. */
	OwnerInit(&owner);
	pose.waterlevel = 1;
	pose.watertype = CONTENTS_LAVA;
	result = Begin(&owner, &pose, destination, lip, false, 100, 0.0f);
	CHECK(result.outcome == SG_DROP_LIVE_FAILED &&
	      result.failure == SG_DROP_LIVE_FAILURE_BEGIN &&
	      result.replay_reason == SG_REPLAY_REASON_HAZARDOUS_LIQUID &&
	      !owner.active && owner.replay_link == -1);
	pose.waterlevel = 0;
	pose.watertype = 0;

	OwnerInit(&owner);
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	shadow_fail = 1;
	result = Command(&owner, &pose, &command, false);
	shadow_fail = 0;
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_SHADOW_CONTROL &&
	      !owner.active && owner.replay_link == -1);

	OwnerInit(&owner);
	memset(&contact, 0, sizeof(contact));
	CHECK(Begin(&owner, &pose, destination, lip, false, 100, 0.0f).outcome ==
	      SG_DROP_LIVE_RUNNING);
	RunFrame(&owner, &pose, commands, true);
	owner.replay_link = 99;
	result = Boundary(&owner, &pose, &contact);
	CHECK(result.outcome == SG_DROP_LIVE_FALLBACK &&
	      result.failure == SG_DROP_LIVE_FAILURE_LINK);
	CHECK(!result.arrival_sampled && !result.recovery_sampled &&
	      contact.arrival_calls == 0 && contact.recovery_calls == 0);
}

int main(void)
{
	TestCommandsAndDeferredBoundary();
	TestTimingAndTwoPusherEpochs();
	TestAirborneAndContactPolicy();
	TestRecoveryAndFailureDifferentials();
	TestPrecisionOwnershipAndReset();
	TestCanonicalYawCounterexample();
	TestDynamicEventCadenceAndFailClosed();
	TestSourceEventAdmission();
	TestFinalCommandInvariant();
	TestCanonicalFailuresAndRecoveryRevalidation();
	TestIntegrationFallbacksDoNotTrace();

	if (failures)
	{
		fprintf(stderr, "sg_drop_live_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_drop_live_test: ok");
	return 0;
}
