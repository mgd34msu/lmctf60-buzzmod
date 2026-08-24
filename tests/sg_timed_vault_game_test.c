#include <stdio.h>
#include <string.h>

#include "slipgate/sg_timed_vault_game.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_timed_vault_game_spec_t Spec(void)
{
	sg_timed_vault_game_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.transaction.source_key = 10U;
	spec.transaction.short_relay_key = 20U;
	spec.transaction.restore_relay_key = 21U;
	spec.transaction.fanout_identity = 0xfeedU;
	spec.transaction.dispatch_target_count = 4U;
	spec.transaction.device_target_count = 9U;
	spec.transaction.door_leaf_count = 2U;
	spec.link_index = 7U;
	spec.short_relay_generation = 120U;
	spec.restore_relay_generation = 121U;
	spec.activator_identity = UINT64_C(0xabc);
	spec.short_ticket_id = UINT64_C(0x1001);
	spec.restore_ticket_id = UINT64_C(0x1002);
	return spec;
}

static sg_timed_vault_game_observation_t Observation(uint32_t frame)
{
	sg_timed_vault_game_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.frame = frame;
	observation.binding_source_key = 10U;
	observation.binding_fanout_identity = 0xfeedU;
	observation.alive = 1U;
	observation.connected = 1U;
	observation.binding_current = 1U;
	observation.door_top_count = 2U;
	observation.body_clear = 1U;
	observation.bot_controlled = 1U;
	return observation;
}

static sg_delayed_use_ticket_observation_t Ticket(
	const sg_delayed_use_ticket_t *ticket, int activator_current)
{
	sg_delayed_use_ticket_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.ticket_id = ticket->spec.ticket_id;
	observation.activator_identity = ticket->spec.activator_identity;
	observation.source_key = ticket->spec.source_key;
	observation.source_generation = ticket->spec.source_generation;
	observation.fanout_identity = ticket->spec.fanout_identity;
	observation.link_index = ticket->spec.link_index;
	observation.event_order = ticket->spec.event_order;
	observation.source_current = 1U;
	observation.fanout_current = 1U;
	observation.activator_current = activator_current ? 1U : 0U;
	return observation;
}

static void BeginAndDispatch(sg_timed_vault_game_state_t *state)
{
	sg_timed_vault_game_spec_t spec = Spec();
	sg_timed_vault_game_observation_t observation = Observation(100U);

	CHECK(SG_TimedVaultGameBegin(state, &spec, &observation) ==
	    SG_TIMED_VAULT_GAME_ACTIVE);
	CHECK(SG_TimedVaultGameCommand(state) == SG_TIMED_VAULT_COMMAND_HOLD);
	observation = Observation(101U);
	CHECK(SG_TimedVaultGameStep(state, &observation) ==
	    SG_TIMED_VAULT_COMMAND_HOLD);
	observation = Observation(102U);
	CHECK(SG_TimedVaultGameDispatch(state, &observation));
	CHECK(state->short_ticket.state == SG_DELAYED_USE_TICKET_ARMED);
	CHECK(state->restore_ticket.state == SG_DELAYED_USE_TICKET_ARMED);
	CHECK(state->restore_ticket.spec.durable == 1U);
	CHECK(state->transaction.restoration ==
	    SG_TIMED_VAULT_RESTORATION_REQUIRED);
}

static void OpenVault(sg_timed_vault_game_state_t *state)
{
	sg_timed_vault_game_observation_t observation;
	sg_delayed_use_ticket_observation_t ticket;

	BeginAndDispatch(state);
	observation = Observation(112U);
	ticket = Ticket(&state->short_ticket, 1);
	CHECK(SG_TimedVaultGameShortRelay(state, &ticket, &observation));
	CHECK(state->short_ticket.state == SG_DELAYED_USE_TICKET_CONSUMED);
	CHECK(SG_TimedVaultGameCommand(state) == SG_TIMED_VAULT_COMMAND_ENTER);
}

static void TestLeasePickupDisconnectAndDurableRestore(void)
{
	sg_timed_vault_game_state_t state;
	sg_timed_vault_game_observation_t observation;
	sg_delayed_use_ticket_observation_t ticket;

	OpenVault(&state);
	observation = Observation(113U);
	CHECK(SG_TimedVaultGameFlagPickup(&state, &observation) ==
	    SG_TIMED_VAULT_COMMAND_EGRESS);
	CHECK(state.transaction.lease_deadline_frame == 202U);
	observation = Observation(150U);
	CHECK(SG_TimedVaultGameStep(&state, &observation) ==
	    SG_TIMED_VAULT_COMMAND_EGRESS);
	observation = Observation(151U);
	observation.connected = 0U;
	CHECK(SG_TimedVaultGameStep(&state, &observation) ==
	    SG_TIMED_VAULT_COMMAND_NONE);
	CHECK(state.transaction.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.transaction.phase == SG_TIMED_VAULT_PHASE_WAIT_RESTORE);
	CHECK(SG_TimedVaultGameRetireActivator(&state, UINT64_C(0xabc)));
	CHECK(state.restore_ticket.state == SG_DELAYED_USE_TICKET_ARMED);

	observation = Observation(202U);
	observation.connected = 0U;
	observation.body_clear = 0U;
	ticket = Ticket(&state.restore_ticket, 0);
	CHECK(SG_TimedVaultGameRestoreRelay(&state, &ticket, &observation));
	CHECK(state.restore_ticket.state == SG_DELAYED_USE_TICKET_CONSUMED);
	CHECK(state.transaction.restoration ==
	    SG_TIMED_VAULT_RESTORATION_OBSERVED);
	CHECK(state.transaction.phase == SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR);
	observation = Observation(203U);
	observation.connected = 0U;
	CHECK(SG_TimedVaultGameStep(&state, &observation) ==
	    SG_TIMED_VAULT_COMMAND_NONE);
	CHECK(state.transaction.phase == SG_TIMED_VAULT_PHASE_TERMINAL);
	CHECK(state.transaction.restoration ==
	    SG_TIMED_VAULT_RESTORATION_DISCHARGED);
}

static void TestTicketDriftBurnsShortButPreservesRestore(void)
{
	sg_timed_vault_game_state_t state;
	sg_timed_vault_game_observation_t observation;
	sg_delayed_use_ticket_observation_t ticket;

	BeginAndDispatch(&state);
	observation = Observation(112U);
	ticket = Ticket(&state.short_ticket, 1);
	ticket.source_generation++;
	CHECK(!SG_TimedVaultGameShortRelay(&state, &ticket, &observation));
	CHECK(state.short_ticket.state == SG_DELAYED_USE_TICKET_INVALID);
	CHECK(state.restore_ticket.state == SG_DELAYED_USE_TICKET_ARMED);
	observation = Observation(202U);
	ticket = Ticket(&state.restore_ticket, 1);
	CHECK(SG_TimedVaultGameRestoreRelay(&state, &ticket, &observation));
	CHECK(state.transaction.outcome == SG_TIMED_VAULT_OUTCOME_FAILED);
	CHECK(state.transaction.restoration ==
	    SG_TIMED_VAULT_RESTORATION_DISCHARGED);
}

static void TestExactFramesAndOneShotEvents(void)
{
	sg_timed_vault_game_state_t state;
	sg_timed_vault_game_observation_t observation;
	sg_delayed_use_ticket_observation_t ticket;

	BeginAndDispatch(&state);
	observation = Observation(111U);
	ticket = Ticket(&state.short_ticket, 1);
	CHECK(!SG_TimedVaultGameShortRelay(&state, &ticket, &observation));
	CHECK(state.transaction.reason == SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT);
	CHECK(state.restore_ticket.state == SG_DELAYED_USE_TICKET_ARMED);

	BeginAndDispatch(&state);
	observation = Observation(112U);
	ticket = Ticket(&state.short_ticket, 1);
	CHECK(SG_TimedVaultGameShortRelay(&state, &ticket, &observation));
	CHECK(!SG_TimedVaultGameShortRelay(&state, &ticket, &observation));
	CHECK(state.short_ticket.state == SG_DELAYED_USE_TICKET_CONSUMED);
}

static void TestStockHumanBypassesAdapter(void)
{
	sg_timed_vault_game_state_t state;
	sg_timed_vault_game_spec_t spec = Spec();
	sg_timed_vault_game_observation_t observation = Observation(100U);

	observation.bot_controlled = 0U;
	memset(&state, 0xa5, sizeof(state));
	CHECK(SG_TimedVaultGameBegin(&state, &spec, &observation) ==
	    SG_TIMED_VAULT_GAME_BYPASS);
	CHECK(state.short_ticket.state == SG_DELAYED_USE_TICKET_EMPTY);
	CHECK(state.restore_ticket.state == SG_DELAYED_USE_TICKET_EMPTY);
}

int main(void)
{
	TestLeasePickupDisconnectAndDurableRestore();
	TestTicketDriftBurnsShortButPreservesRestore();
	TestExactFramesAndOneShotEvents();
	TestStockHumanBypassesAdapter();
	if (failures)
	{
		fprintf(stderr, "sg_timed_vault_game_test: %d failures\n", failures);
		return 1;
	}
	puts("sg_timed_vault_game_test: ok");
	return 0;
}
