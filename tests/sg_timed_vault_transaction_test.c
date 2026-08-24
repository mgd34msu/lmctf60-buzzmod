#include <stdio.h>
#include <string.h>

#include "slipgate/sg_timed_vault_transaction.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_timed_vault_spec_t Vault(void)
{
	sg_timed_vault_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.source_key = 10U;
	spec.short_relay_key = 20U;
	spec.restore_relay_key = 21U;
	spec.fanout_identity = 0x1234U;
	spec.dispatch_target_count = 4U;
	spec.device_target_count = 9U;
	spec.door_leaf_count = 2U;
	return spec;
}

static sg_timed_vault_observation_t Observation(uint32_t frame)
{
	sg_timed_vault_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.frame = frame;
	observation.binding_source_key = 10U;
	observation.binding_fanout_identity = 0x1234U;
	observation.alive = 1U;
	observation.connected = 1U;
	observation.binding_current = 1U;
	observation.body_clear = 1U;
	return observation;
}

static void SetMechanismEvent(sg_timed_vault_observation_t *observation,
	uint32_t source_key, uint16_t target_count)
{
	observation->event_source_key = source_key;
	observation->event_fanout_identity = 0x1234U;
	observation->event_target_count = target_count;
}

static sg_timed_vault_state_t BeginAt(uint32_t frame)
{
	sg_timed_vault_spec_t spec = Vault();
	sg_timed_vault_observation_t observation = Observation(frame);
	sg_timed_vault_reduction_t reduction;

	observation.touch_authenticated = 1U;
	observation.event_source_key = spec.source_key;
	reduction = SG_TimedVaultBegin(&spec, &observation);
	CHECK(reduction.command == SG_TIMED_VAULT_COMMAND_HOLD);
	CHECK(reduction.state.phase == SG_TIMED_VAULT_PHASE_WAIT_DISPATCH);
	CHECK(reduction.state.outcome == SG_TIMED_VAULT_OUTCOME_PENDING);
	CHECK(reduction.state.restoration == SG_TIMED_VAULT_RESTORATION_NONE);
	return reduction.state;
}

static sg_timed_vault_reduction_t Step(sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation)
{
	sg_timed_vault_reduction_t reduction =
		SG_TimedVaultReduce(state, observation);

	*state = reduction.state;
	return reduction;
}

static sg_timed_vault_state_t BeginAndDispatch(void)
{
	sg_timed_vault_state_t state = BeginAt(100U);
	sg_timed_vault_observation_t observation = Observation(102U);
	sg_timed_vault_reduction_t reduction;

	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 10U, 4U);
	reduction = Step(&state, &observation);
	CHECK(reduction.command == SG_TIMED_VAULT_COMMAND_HOLD);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_READY);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);
	return state;
}

static sg_timed_vault_state_t OpenVault(void)
{
	sg_timed_vault_state_t state = BeginAndDispatch();
	sg_timed_vault_observation_t observation = Observation(112U);
	sg_timed_vault_reduction_t reduction;

	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 2U;
	SetMechanismEvent(&observation, 20U, 9U);
	reduction = Step(&state, &observation);
	CHECK(reduction.command == SG_TIMED_VAULT_COMMAND_ENTER);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_ACTIVE);
	return state;
}

static void TestExactTimelinePickupEgressAndBodyClear(void)
{
	sg_timed_vault_state_t state = BeginAt(100U);
	sg_timed_vault_observation_t observation = Observation(101U);
	sg_timed_vault_reduction_t reduction;

	CHECK(state.dispatch_frame == 102U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_HOLD);

	observation = Observation(102U);
	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 10U, 4U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_HOLD);
	CHECK(state.ready_frame == 112U);
	CHECK(state.restore_frame == 202U);
	CHECK(state.lease_deadline_frame == 202U);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(111U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_HOLD);
	observation = Observation(112U);
	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 2U;
	SetMechanismEvent(&observation, 20U, 9U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_ENTER);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_ACTIVE);

	observation = Observation(113U);
	observation.flag_pickup = 1U;
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_EGRESS);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_EGRESS);
	CHECK(state.pickup_seen);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);
	CHECK(state.lease_deadline_frame == 202U);

	observation = Observation(150U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_EGRESS);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(202U);
	observation.restore_relay_authenticated = 1U;
	observation.body_clear = 0U;
	SetMechanismEvent(&observation, 21U, 9U);
	reduction = Step(&state, &observation);
	CHECK(reduction.command == SG_TIMED_VAULT_COMMAND_HOLD);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_OBSERVED);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_PENDING);

	observation = Observation(203U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_NONE);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_SUCCEEDED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_NONE);
}

static void TestExactDispatchAndRelayFrames(void)
{
	sg_timed_vault_state_t state = BeginAt(100U);
	sg_timed_vault_observation_t observation = Observation(101U);
	sg_timed_vault_reduction_t reduction;

	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 10U, 4U);
	reduction = Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);
	CHECK(state.ready_frame == 111U);
	CHECK(state.restore_frame == 201U);
	observation = Observation(111U);
	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 2U;
	SetMechanismEvent(&observation, 20U, 9U);
	Step(&state, &observation);
	observation = Observation(201U);
	observation.restore_relay_authenticated = 1U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);

	state = BeginAt(100U);
	observation = Observation(102U);
	reduction = Step(&state, &observation);
	CHECK(reduction.command == SG_TIMED_VAULT_COMMAND_NONE);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_MISSING_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_NONE);

	state = BeginAndDispatch();
	observation = Observation(111U);
	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 2U;
	SetMechanismEvent(&observation, 20U, 9U);
	reduction = Step(&state, &observation);
	CHECK(reduction.command == SG_TIMED_VAULT_COMMAND_NONE);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(202U);
	observation.restore_relay_authenticated = 1U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);

	state = OpenVault();
	observation = Observation(201U);
	observation.restore_relay_authenticated = 1U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);

}

static void TestWrongPartialAndDuplicateEvents(void)
{
	sg_timed_vault_state_t state = BeginAt(100U);
	sg_timed_vault_observation_t observation = Observation(102U);

	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 99U, 4U);
	Step(&state, &observation);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_SOURCE_DRIFT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_NONE);

	state = BeginAt(100U);
	observation = Observation(102U);
	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 10U, 3U);
	Step(&state, &observation);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_PARTIAL_EVENT);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	state = BeginAndDispatch();
	observation = Observation(103U);
	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 10U, 4U);
	Step(&state, &observation);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	state = BeginAndDispatch();
	observation = Observation(112U);
	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 1U;
	SetMechanismEvent(&observation, 20U, 9U);
	Step(&state, &observation);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_PARTIAL_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	state = OpenVault();
	observation = Observation(113U);
	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 2U;
	SetMechanismEvent(&observation, 20U, 9U);
	Step(&state, &observation);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);

	state = OpenVault();
	observation = Observation(202U);
	observation.restore_relay_authenticated = 1U;
	observation.body_clear = 0U;
	SetMechanismEvent(&observation, 21U, 8U);
	Step(&state, &observation);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_PARTIAL_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);

	observation = Observation(203U);
	observation.restore_relay_authenticated = 1U;
	observation.body_clear = 0U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_OBSERVED);
	observation = Observation(204U);
	CHECK(Step(&state, &observation).command ==
	    SG_TIMED_VAULT_COMMAND_NONE);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);

	state = OpenVault();
	observation = Observation(202U);
	observation.restore_relay_authenticated = 1U;
	observation.body_clear = 0U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	observation = Observation(203U);
	observation.restore_relay_authenticated = 1U;
	observation.body_clear = 0U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_DUPLICATE_EVENT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_OBSERVED);
}

static void TestMissingRelaysKeepRestoreObligation(void)
{
	sg_timed_vault_state_t state = BeginAndDispatch();
	sg_timed_vault_observation_t observation = Observation(112U);

	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_MISSING_EVENT);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(202U);
	observation.restore_relay_authenticated = 1U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);

	state = OpenVault();
	observation = Observation(202U);
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_MISSING_EVENT);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(203U);
	observation.restore_relay_authenticated = 1U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);
}

static void CheckPostDispatchFailure(
	sg_timed_vault_observation_t observation,
	sg_timed_vault_reason_t expected_reason)
{
	sg_timed_vault_state_t state = BeginAndDispatch();

	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.reason == expected_reason);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);
}

static void TestOwnershipAndIdentityDriftFailClosed(void)
{
	sg_timed_vault_state_t state = BeginAndDispatch();
	sg_timed_vault_observation_t observation = Observation(103U);

	observation.alive = 0U;
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_DEAD);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(112U);
	observation.alive = 0U;
	observation.short_relay_authenticated = 1U;
	observation.door_top_count = 2U;
	SetMechanismEvent(&observation, 20U, 9U);
	Step(&state, &observation);
	CHECK(state.short_relay_seen);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(202U);
	observation.alive = 0U;
	observation.restore_relay_authenticated = 1U;
	observation.body_clear = 0U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_OBSERVED);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR);
	observation = Observation(203U);
	observation.alive = 0U;
	Step(&state, &observation);
	CHECK(state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);

	observation = Observation(103U);
	observation.connected = 0U;
	CheckPostDispatchFailure(observation,
	    SG_TIMED_VAULT_REASON_DISCONNECTED);
	observation = Observation(103U);
	observation.binding_current = 0U;
	CheckPostDispatchFailure(observation,
	    SG_TIMED_VAULT_REASON_BINDING_DRIFT);
	observation = Observation(103U);
	observation.binding_source_key = 99U;
	CheckPostDispatchFailure(observation,
	    SG_TIMED_VAULT_REASON_SOURCE_DRIFT);
	observation = Observation(103U);
	observation.binding_fanout_identity = 0x9999U;
	CheckPostDispatchFailure(observation,
	    SG_TIMED_VAULT_REASON_FANOUT_DRIFT);
}

static void TestRestoreFanoutMustAuthenticateCompletely(void)
{
	sg_timed_vault_state_t state = OpenVault();
	sg_timed_vault_observation_t observation = Observation(202U);

	observation.restore_relay_authenticated = 1U;
	observation.event_fanout_identity = 0x9999U;
	observation.event_source_key = 21U;
	observation.event_target_count = 9U;
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_FANOUT_DRIFT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);

	observation = Observation(203U);
	observation.restore_relay_authenticated = 1U;
	SetMechanismEvent(&observation, 21U, 9U);
	Step(&state, &observation);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_DISCHARGED);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
}

static void TestBoundaryInputsFailClosed(void)
{
	sg_timed_vault_spec_t spec = Vault();
	sg_timed_vault_observation_t observation =
		Observation(UINT32_MAX - 101U);
	sg_timed_vault_reduction_t reduction;
	sg_timed_vault_state_t state;

	observation.touch_authenticated = 1U;
	observation.event_source_key = 10U;
	reduction = SG_TimedVaultBegin(&spec, &observation);
	CHECK(reduction.state.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(reduction.state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(reduction.state.reason == SG_TIMED_VAULT_REASON_INVALID);

	state = BeginAt(100U);
	observation = Observation(UINT32_MAX - 50U);
	observation.dispatch_authenticated = 1U;
	SetMechanismEvent(&observation, 10U, 4U);
	Step(&state, &observation);
	CHECK(state.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.reason == SG_TIMED_VAULT_REASON_FRAME_DRIFT);
	CHECK(state.restoration == SG_TIMED_VAULT_RESTORATION_REQUIRED);
	CHECK(state.restore_frame == UINT32_MAX);
}

int main(void)
{
	TestExactTimelinePickupEgressAndBodyClear();
	TestExactDispatchAndRelayFrames();
	TestWrongPartialAndDuplicateEvents();
	TestMissingRelaysKeepRestoreObligation();
	TestOwnershipAndIdentityDriftFailClosed();
	TestRestoreFanoutMustAuthenticateCompletely();
	TestBoundaryInputsFailClosed();
	if (failures)
	{
		fprintf(stderr, "sg_timed_vault_transaction_test: %d failures\n",
		    failures);
		return 1;
	}
	puts("sg_timed_vault_transaction_test: ok");
	return 0;
}
