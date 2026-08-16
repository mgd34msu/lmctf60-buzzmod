/* Host-free proof that legacy A remains authority while its pure revision-2
 * observer consumes the source-door bit at the first 25 ms poststep. */
#include <stdio.h>
#include <string.h>

#define SG_ACCEPT_DROP 1
#ifndef SG_ACCEPT_DROP_LEGACY_A
#define SG_ACCEPT_DROP_LEGACY_A 1
#endif
#include "slipgate/sg_accept_drop.c"

sg_host_t sg_host;
level_locals_t level;

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		        #expression); \
		failures++; \
	} \
} while (0)

static sg_drop_replay_spec_t TestSpec(void)
{
	sg_drop_replay_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	VectorSet(spec.destination, 1000.0f, 0.0f, 0.0f);
	VectorSet(spec.lip, 100.0f, 0.0f, 0.0f);
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	return spec;
}

static sg_replay_pose_t TestPose(void)
{
	sg_replay_pose_t pose;

	memset(&pose, 0, sizeof(pose));
	pose.grounded = true;
	return pose;
}

static sg_replay_observation_t TestObservation(void)
{
	sg_replay_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.ground_support_valid = true;
	return observation;
}

int main(void)
{
	sg_bot_t bot;
	sg_drop_live_events_t events;
	sg_drop_replay_spec_t spec = TestSpec();
	sg_replay_pose_t pose = TestPose();
	sg_replay_observation_t observation = TestObservation();
	usercmd_t command;
	qboolean source_door_pending = true;
	const int link_index = 8556;

	memset(&bot, 0, sizeof(bot));
	memset(&events, 0, sizeof(events));
	memset(&command, 0, sizeof(command));
	memset(&accept_drop, 0, sizeof(accept_drop));
	accept_drop.phase = SGAD_ACTIVE;
	accept_drop.requested_case = 3;
	accept_drop.armed = true;
	accept_drop.bot = &bot;
	accept_drop.link = link_index;
	accept_drop.started = true;
	bot.commit_link = link_index;
	bot.drop_started = true;

	CHECK(SG_DropReplayBegin(&accept_drop.observer, &spec, &pose,
	                         &observation, 0.0f) == SG_REPLAY_RUNNING);
	accept_drop.observer_active = true;
	/* A's authority must not silently turn on production revision 2. */
	CHECK(SG_AcceptDropLegacyAuthority(&bot, link_index));
	CHECK(!bot.drop_replay_active);
	CHECK(SG_AcceptDropObserverEventOwner(&bot));

	/* This is precisely the source-preflight carry: it is installed after the
	 * command-start clear, not passed to Begin and not left for command two. */
	CHECK(SG_AcceptDropObserverBeginCommand(&bot, link_index, &events,
	                                        &source_door_pending));
	CHECK(!source_door_pending && !events.contaminated && events.door_passed);
	SG_AcceptDropObserverTakeEvents(&bot, link_index, 0, &events);
	CHECK(!events.contaminated && !events.door_passed &&
	      accept_drop.observer_events_pending &&
	      accept_drop.observer_events_step == 0);

	observation = TestObservation();
	CHECK(AcceptObserverEventsApply(0, &observation));
	CHECK(!observation.contaminated && observation.door_passed &&
	      !accept_drop.observer_events_pending);
	CHECK(SG_DropReplayPreStep(&accept_drop.observer, &pose, &command) ==
	      SG_REPLAY_RUNNING);
	CHECK(SG_DropReplayPostStep(&accept_drop.observer, &pose, &observation) ==
	      SG_REPLAY_FAILED);
	CHECK(accept_drop.observer.progress.elapsed_ms == SG_REPLAY_STEP_MS &&
	      accept_drop.observer.progress.reason == SG_REPLAY_REASON_DOOR_PASSED);
	CHECK(!bot.drop_replay_active);

	if (failures == 0)
		printf("observer-events-selftest ok legacy-authority=1 rev2-active=0 "
		       "source-door=one-shot first-poststep=door-passed elapsed_ms=25\n");
	return failures == 0 ? 0 : 1;
}
