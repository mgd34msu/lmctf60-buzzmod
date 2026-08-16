/* Host-free executable specification of the private terminal acceptance law. */
#include <stdio.h>
#include <string.h>

#define SG_ACCEPT_DROP 1
#define SG_ACCEPT_DROP_LEGACY_A 0
#include "slipgate/sg_accept_drop.c"

static void FillPassingState(sg_accept_drop_state_t *state,
	const sg_accept_drop_selector_t *selector, qboolean legacy)
{
	unsigned int steps =
	    (unsigned int)(selector->cost_ms / SG_REPLAY_STEP_MS);
	unsigned int boundaries =
	    (unsigned int)(selector->cost_ms / SG_REPLAY_FRAME_MS);

	memset(state, 0, sizeof(*state));
	state->started = true;
	state->action_begins = 1;
	state->historical_commands = steps;
	state->commands = steps;
	state->final_historical_matches = steps;
	state->arm_poses = 1;
	state->poses = steps;
	state->pusher_begins = 2;
	state->pusher_ends = 2;
	state->saw_walkoff = true;
	state->saw_airborne = true;
	state->handoffs = selector->recovery_required ? 3 : 2;
	state->last_arrival = true;
	state->arrival_predicates = 1;
	state->arrival_predicate_results = 1;
	state->arrival_predicate_true = 1;
	state->arrival_traces = 1;
	state->arrival_trace_true = 1;
	state->boundary_enters = boundaries;
	if (selector->recovery_required)
	{
		state->saw_recovery = true;
		state->last_recovery = true;
		state->recovery_predicates = 1;
		state->recovery_predicate_results = 1;
		state->recovery_predicate_true = 1;
		state->recovery_traces = 1;
		state->recovery_trace_true = 1;
	}
	if (legacy)
	{
		state->observer_began = true;
		state->observer.progress.status = SG_REPLAY_ARRIVED;
		state->observer.progress.reason = SG_REPLAY_REASON_NONE;
		state->observer.progress.elapsed_ms = selector->cost_ms;
		state->observer.progress.arrival_ms = selector->cost_ms;
		state->observer.recovery = selector->recovery_required;
		state->observer_recovery_start_ms = selector->recovery_start_ms;
		state->legacy_recovery_start_ms = selector->recovery_start_ms;
		state->observer_presteps = steps;
		state->observer_poststeps = steps;
		state->observer_boundaries = boundaries;
		state->observer_command_matches = steps;
		state->observer_arrival_cached = 1;
		state->observer_arrival_inferred = boundaries - 1;
		state->observer_arrival_cached_true = 1;
		if (selector->recovery_required)
		{
			state->observer_recovery_cached = 1;
			state->observer_recovery_inferred = boundaries - 1;
			state->observer_recovery_cached_true = 1;
		}
		else
			state->observer_recovery_inferred = boundaries;
	}
	else
	{
		state->boundary_exits = boundaries;
		state->boundary_results = boundaries;
		state->arrival_callbacks = 1;
		state->result_arrival_samples = 1;
		state->result_arrivals = 1;
		state->final_outcome = SG_DROP_LIVE_ARRIVED;
		state->final_reason = SG_REPLAY_REASON_NONE;
		state->production_status = SG_REPLAY_ARRIVED;
		state->production_reason = SG_REPLAY_REASON_NONE;
		state->production_elapsed_ms = selector->cost_ms;
		state->production_arrival_ms = selector->cost_ms;
		if (selector->recovery_required)
		{
			state->recovery_callbacks = 1;
			state->result_recovery_samples = 1;
			state->result_recovery_ready = 1;
			state->result_recovery_started = 1;
			state->production_recovery_start_ms =
			    selector->recovery_start_ms;
		}
	}
}

static int Expect(const char *name, const char *actual, const char *expected)
{
	if ((!actual && !expected) ||
	    (actual && expected && strcmp(actual, expected) == 0))
		return 1;
	fprintf(stderr, "%s: expected %s, got %s\n", name,
	    expected ? expected : "PASS", actual ? actual : "PASS");
	return 0;
}

int main(void)
{
	sg_accept_drop_state_t base, changed;
	usercmd_t command;
	int case_index;
	int legacy;
	int ok = 1;

	for (case_index = 0; case_index < SG_ACCEPT_DROP_NATURAL_CASE_COUNT;
	     case_index++)
		for (legacy = 0; legacy <= 1; legacy++)
		{
			const sg_accept_drop_selector_t *selector =
			    &accept_selectors[case_index];

			FillPassingState(&base, selector, legacy);
			ok &= Expect("positive", AcceptFinishFailure(&base, selector,
			    legacy), NULL);

			changed = base;
			if (legacy)
				changed.observer.progress.elapsed_ms -= SG_REPLAY_STEP_MS;
			else
				changed.production_elapsed_ms -= SG_REPLAY_STEP_MS;
			ok &= Expect("wrong-time-arrival",
			    AcceptFinishFailure(&changed, selector, legacy),
			    legacy ? "finish-observer-terminal" :
			             "finish-production-terminal");

			changed = base;
			changed.commands--;
			ok &= Expect("command-count",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-command-count");

			changed = base;
			changed.pusher_order_errors = 1;
			ok &= Expect("pusher-order",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-pusher-order");

			changed = base;
			changed.arm_poses = 0;
			ok &= Expect("arm-pose-count",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-pose-count");

			changed = base;
			changed.boundary_enters--;
			ok &= Expect("boundary-count",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-boundary-count");

			changed = base;
			changed.shelves = 1;
			ok &= Expect("shelf-teach",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-shelf-teach");

			changed = base;
			changed.saw_airborne = false;
			ok &= Expect("handoff-evidence",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-handoff-evidence");

			changed = base;
			changed.arrival_predicate_results++;
			ok &= Expect("contact-history",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-contact-history");

			changed = base;
			changed.zero_final_commands = 1;
			ok &= Expect("final-command-mutation",
			    AcceptFinishFailure(&changed, selector, legacy),
			    "finish-final-command-zeroed");

			if (legacy)
			{
				changed = base;
				changed.observer_command_matches--;
				changed.observer_command_mismatches++;
				ok &= Expect("observer-command-mismatch",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-observer-command");

				changed = base;
				changed.final_historical_matches--;
				changed.final_historical_mismatches++;
				ok &= Expect("nonzero-final-command-mutation",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-observer-command");

				changed = base;
				changed.observer_arrival_inferred--;
				ok &= Expect("observer-contact-cache",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-observer-contact-cache");
			}
			else
			{
				changed = base;
				changed.boundary_results--;
				ok &= Expect("production-result-count",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-production-boundary");
			}

			if (selector->recovery_required)
			{
				changed = base;
				changed.saw_recovery = false;
				ok &= Expect("missing-recovery",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-recovery-evidence");

				changed = base;
				if (legacy)
					changed.observer_recovery_start_ms -=
					    SG_REPLAY_FRAME_MS;
				else
					changed.production_recovery_start_ms -=
					    SG_REPLAY_FRAME_MS;
				ok &= Expect("wrong-recovery-boundary",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-recovery-timing");
			}
			else
			{
				changed = base;
				changed.saw_recovery = true;
				ok &= Expect("unexpected-recovery",
				    AcceptFinishFailure(&changed, selector, legacy),
				    "finish-unexpected-recovery");
			}
		}

	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	if (!AcceptCommandZeroed(&command))
	{
		fprintf(stderr, "zero command classifier rejected canonical zero\n");
		ok = 0;
	}
	command.forwardmove = 1;
	if (AcceptCommandZeroed(&command))
	{
		fprintf(stderr, "zero command classifier accepted mutation\n");
		ok = 0;
	}

	if (ok)
		printf("finish-selftest ok cases=%d variants=2 negative-matrix=complete\n",
		    SG_ACCEPT_DROP_NATURAL_CASE_COUNT);
	return ok ? 0 : 1;
}
