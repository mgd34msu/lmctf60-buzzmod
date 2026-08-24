#include <stdio.h>
#include <string.h>

#include "slipgate/sg_mechanism_timeline.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_mechanism_timeline_spec_t TimedGate(void)
{
	sg_mechanism_timeline_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.source_key = 10U;
	spec.fanout_identity = 20U;
	spec.approach_timeout_frames = 4U;
	spec.activation_timeout_frames = 2U;
	spec.trigger_delay_frames = 2U;
	spec.cooldown_frames = 8U;
	spec.lease_frames = 6U;
	spec.egress_timeout_frames = 3U;
	return spec;
}

static sg_mechanism_timeline_spec_t Station(void)
{
	sg_mechanism_timeline_spec_t spec = TimedGate();

	spec.trigger_delay_frames = 0U;
	spec.cooldown_frames = 0U;
	spec.lease_frames = 0U;
	spec.station_wait_frames = 3U;
	spec.egress_timeout_frames = 2U;
	return spec;
}

static sg_mechanism_timeline_observation_t Observation(uint32_t frame)
{
	sg_mechanism_timeline_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.frame = frame;
	observation.source_key = 10U;
	observation.fanout_identity = 20U;
	observation.alive = 1U;
	observation.connected = 1U;
	observation.binding_current = 1U;
	return observation;
}

static void ReachDelayedActivation(sg_mechanism_timeline_state_t *state,
	sg_mechanism_timeline_observation_t *observation)
{
	observation->frame = 101U;
	observation->approach_arrived = 1U;
	observation->activation_authenticated = 1U;
	CHECK(SG_MechanismTimelineStep(state, observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	CHECK(state->phase == SG_MECHANISM_TIMELINE_DELAY_PENDING);
	CHECK(state->activation_frame == 101U);
	CHECK(state->ready_frame == 103U);
	CHECK(state->cooldown_ready_frame == 109U);
	CHECK(state->lease_deadline_frame == 109U);
	CHECK(state->station_deadline_frame ==
	    SG_MECHANISM_TIMELINE_FRAME_UNSET);
}

static void TestTimedGateHappyPath(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = TimedGate();
	sg_mechanism_timeline_observation_t observation = Observation(100U);

	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	CHECK(state.phase == SG_MECHANISM_TIMELINE_APPROACH);
	CHECK(state.approach_deadline_frame == 104U);
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_APPROACH);
	ReachDelayedActivation(&state, &observation);

	observation.frame = 102U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	observation.frame = 103U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_ACTIVE);

	observation.frame = 104U;
	observation.egress_requested = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_EGRESS);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_EGRESS);
	CHECK(state.egress_deadline_frame == 107U);

	observation.frame = 106U;
	observation.egress_arrived = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_COMPLETE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_NONE);
}

static void TestStationWaitIsIndependent(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = Station();
	sg_mechanism_timeline_observation_t observation = Observation(20U);

	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.approach_arrived = 1U;
	observation.activation_authenticated = 1U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_ACTIVE);
	CHECK(state.ready_frame == 20U);
	CHECK(state.cooldown_ready_frame == 20U);
	CHECK(state.lease_deadline_frame ==
	    SG_MECHANISM_TIMELINE_FRAME_UNSET);
	CHECK(state.station_deadline_frame == 23U);

	observation.frame = 21U;
	observation.egress_requested = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_EGRESS);
	CHECK(state.egress_deadline_frame == 23U);
	observation.frame = 22U;
	observation.egress_arrived = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_COMPLETE);

	observation = Observation(20U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.approach_arrived = 1U;
	observation.activation_authenticated = 1U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	observation.frame = 23U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_FAILED);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_STATION_EXPIRED);
}

static void TestExactReadyFrame(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = TimedGate();
	sg_mechanism_timeline_observation_t observation = Observation(100U);

	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	ReachDelayedActivation(&state, &observation);
	observation.frame = 102U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_FAILED);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_EARLY_ACTIVE);

	observation = Observation(100U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	ReachDelayedActivation(&state, &observation);
	observation.frame = 103U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_READY_MISSED);

	observation = Observation(100U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	ReachDelayedActivation(&state, &observation);
	observation.frame = 104U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_READY_MISSED);
}

static void TestLeaseAndEgressDeadlines(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = TimedGate();
	sg_mechanism_timeline_observation_t observation = Observation(100U);

	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	ReachDelayedActivation(&state, &observation);
	observation.frame = 103U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	observation.frame = 104U;
	observation.egress_requested = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_EGRESS);
	observation.frame = 107U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_EGRESS_TIMEOUT);

	spec.egress_timeout_frames = 20U;
	observation = Observation(100U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	ReachDelayedActivation(&state, &observation);
	observation.frame = 103U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	observation.frame = 104U;
	observation.egress_requested = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_EGRESS);
	observation.frame = 109U;
	observation.egress_arrived = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_LEASE_EXPIRED);
}

static void TestLivenessAndIdentityFailures(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = TimedGate();
	sg_mechanism_timeline_observation_t observation = Observation(10U);

	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.alive = 0U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_DEAD);

	observation = Observation(10U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.connected = 0U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_DISCONNECTED);

	observation = Observation(10U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.binding_current = 0U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_BINDING_DRIFT);

	observation = Observation(10U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.source_key++;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT);

	observation = Observation(10U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.fanout_identity++;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_FANOUT_DRIFT);
}

static void TestTimeoutsAndActiveLoss(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = TimedGate();
	sg_mechanism_timeline_observation_t observation = Observation(50U);

	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.frame = 54U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_APPROACH_TIMEOUT);

	observation = Observation(50U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	observation.approach_arrived = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	CHECK(state.phase == SG_MECHANISM_TIMELINE_ACTIVATION);
	observation.frame = 52U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_ACTIVATION_TIMEOUT);

	observation = Observation(100U);
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	ReachDelayedActivation(&state, &observation);
	observation.frame = 103U;
	observation.mechanism_active = 1U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_ZERO);
	observation.frame = 104U;
	observation.mechanism_active = 0U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_ACTIVE_LOST);
}

static void TestInvalidAndRegressingFrames(void)
{
	sg_mechanism_timeline_state_t state;
	sg_mechanism_timeline_spec_t spec = TimedGate();
	sg_mechanism_timeline_observation_t observation = Observation(10U);

	spec.cooldown_frames = 0U;
	CHECK(SG_MechanismTimelineBegin(&state, &spec, &observation));
	CHECK(state.cooldown_ready_frame == SG_MECHANISM_TIMELINE_FRAME_UNSET);
	observation.frame = 9U;
	CHECK(SG_MechanismTimelineStep(&state, &observation) ==
	    SG_MECHANISM_TIMELINE_COMMAND_NONE);
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_FRAME_REGRESSION);

	spec = TimedGate();
	spec.lease_frames = 0U;
	CHECK(!SG_MechanismTimelineBegin(&state, &spec, &observation));
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_INVALID);

	spec = TimedGate();
	observation = Observation(UINT32_MAX - 1U);
	CHECK(!SG_MechanismTimelineBegin(&state, &spec, &observation));
	CHECK(state.reason == SG_MECHANISM_TIMELINE_REASON_FRAME_OVERFLOW);
}

int main(void)
{
	TestTimedGateHappyPath();
	TestStationWaitIsIndependent();
	TestExactReadyFrame();
	TestLeaseAndEgressDeadlines();
	TestLivenessAndIdentityFailures();
	TestTimeoutsAndActiveLoss();
	TestInvalidAndRegressingFrames();
	if (failures != 0)
	{
		fprintf(stderr, "sg_mechanism_timeline_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_mechanism_timeline_test: ok");
	return 0;
}
