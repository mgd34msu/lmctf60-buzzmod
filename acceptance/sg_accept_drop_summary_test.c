/* Host-free bounded-format and strict ordered-parser regression. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SG_ACCEPT_DROP 1
#ifndef SG_ACCEPT_DROP_LEGACY_A
#define SG_ACCEPT_DROP_LEGACY_A 0
#endif
#define SG_ReplayReasonName AcceptTestReasonName
#include "slipgate/sg_accept_drop.c"
#undef SG_ReplayReasonName

const char *AcceptTestReasonName(sg_replay_reason_t reason)
{
	/* Deliberately mirror the production human-readable whitespace.  Summary
	 * formatting must not use this display string as a wire token. */
	return reason == SG_REPLAY_REASON_NONE ? "none" : "invalid state";
}

static const char *const summary_events[SG_ACCEPT_DROP_SUMMARY_PARTS] = {
	"summary-begin",
	"summary-command",
	"summary-contact",
	"summary-boundary",
	"summary-injection-order",
	"summary-observer",
	"summary-observer-contact",
	"summary-production",
	"summary-end"
};

static const char *const summary_payloads[SG_ACCEPT_DROP_SUMMARY_PARTS] = {
	("selector link started fixture_seed injection_attempts injection_applied "
	 "injection_zero_ms injection_errors injection_step expected_checkpoint "
	 "checkpoint generic_handoff_begins generic_handoff_ends "
	 "generic_completed_substeps generic_handoff_pending generic_begin_valid "
	 "generic_begin_substeps generic_total_msec"),
	("action_begins action_begin_errors historical_commands final_commands "
	"zero_final_commands final_historical_matches final_historical_mismatches "
	"historical_pending arm_poses poses_25ms pusher_begins pusher_ends "
	"pusher_depth pusher_order_errors"),
	("arrival_callback recovery_callback arrival_predicate recovery_predicate "
	"arrival_predicate_results recovery_predicate_results "
	"arrival_predicate_true recovery_predicate_true arrival_trace "
	"recovery_trace arrival_trace_true recovery_trace_true shelves teaches "
	"handoffs walkoff airborne recovery"),
	("boundary_enters boundary_exits boundary_results result_arrival_samples "
	 "result_arrivals result_recovery_samples result_recovery_ready "
	 "result_recovery_started "
	 "pre_contact_validated pre_contact_sampled pre_contact_last_sampled "
	 "pre_contact_errors pre_contact_captured pre_arrival_samples"),
	("injection_frame checkpoint_frame order_stage order_errors entity_passes "
	 "sg_frames fixture_boundary_checks fixture_boundary_mode "
	 "post_command_captures post_command_validations"),
	("observer_began observer_active observer_status observer_reason "
	"observer_elapsed_ms observer_arrival_ms observer_presteps "
	"observer_poststeps observer_boundaries observer_command_matches "
	"observer_command_mismatches observer_recovery_start_ms "
	"legacy_recovery_start_ms"),
	("observer_arrival_cached observer_arrival_inferred "
	"observer_arrival_cached_true observer_recovery_cached "
	"observer_recovery_inferred observer_recovery_cached_true "
	"injection_deferrals deferral_events deferral_last_ordinal "
	"deferral_order_errors"),
	("production_status production_reason production_elapsed_ms "
	"production_arrival_ms production_recovery_start_ms fixture_pmove_traces "
	"fixture_pointcontents fixture_grounded support_valid watertype waterlevel "
	"terminal_geometry recovery_geometry fixture_health fixture_deadflag "
	"fixture_movetype oldvelocity_zero private_stops generic_handoffs"),
	"terminal outcome reason diagnostic complete"
};

static const char summary_string_payloads[] =
	("selector fixture_boundary_mode expected_checkpoint checkpoint observer_reason production_reason "
	 "terminal reason diagnostic");

typedef struct summary_parse_s
{
	int next_part;
	unsigned long first_sequence;
	int run;
	int case_number;
	int frame;
	int summary_set;
	char variant[32];
	char terminal[64];
	char reason[64];
	char diagnostic[96];
	int complete;
} summary_parse_t;

static int WordInList(const char *list, const char *word)
{
	size_t length = strlen(word);
	const char *cursor = list;

	while (cursor && *cursor)
	{
		const char *end;

		while (*cursor == ' ')
			cursor++;
		end = strchr(cursor, ' ');
		if (!end)
			end = cursor + strlen(cursor);
		if ((size_t)(end - cursor) == length &&
		    memcmp(cursor, word, length) == 0)
			return 1;
		cursor = *end ? end + 1 : end;
	}
	return 0;
}

static int ParseLong(const char *text, long *value)
{
	char *end;
	long parsed;

	if (!text || !*text || !value)
		return 0;
	parsed = strtol(text, &end, 10);
	if (!end || *end)
		return 0;
	*value = parsed;
	return 1;
}

static int ParseRecord(summary_parse_t *parsed, const char *record)
{
	char copy[SG_ACCEPT_DROP_SUMMARY_LINE_CAP];
	char *seen[64];
	char *token;
	char *event = NULL;
	char *variant = NULL;
	char *terminal = NULL;
	char *final_reason = NULL;
	char *diagnostic = NULL;
	unsigned long sequence = 0;
	long run = -1, case_number = -1, frame = -1, summary_set = -1;
	int part = -1, total = -1, complete = 0;
	int common_seen = 0;
	int payload_seen = 0;
	int seen_count = 0;
	int index;
	size_t length;

	if (!parsed || !record || (length = strlen(record)) == 0 ||
	    length >= SG_ACCEPT_DROP_SUMMARY_LINE_MAX || record[length - 1] != '\n' ||
	    strchr(record, '\n') != record + length - 1 || length >= sizeof(copy))
		return 0;
	memcpy(copy, record, length + 1);
	copy[length - 1] = '\0';
	token = strtok(copy, " ");
	if (!token || strcmp(token, "SG_ACCEPT_DROP") != 0)
		return 0;
	while ((token = strtok(NULL, " ")) != NULL)
	{
		char *equals = strchr(token, '=');
		char *key;
		char *value;
		long number;

		if (!equals || equals == token || !equals[1] || seen_count >= 64)
			return 0;
		*equals = '\0';
		key = token;
		value = equals + 1;
		for (index = 0; index < seen_count; index++)
			if (strcmp(seen[index], key) == 0)
				return 0;
		seen[seen_count++] = key;

		if (strcmp(key, "seq") == 0)
		{
			char *end;
			sequence = strtoul(value, &end, 10);
			if (!end || *end)
				return 0;
			common_seen++;
		}
		else if (strcmp(key, "variant") == 0)
		{
			variant = value;
			common_seen++;
		}
		else if (strcmp(key, "run") == 0)
		{
			if (!ParseLong(value, &run)) return 0;
			common_seen++;
		}
		else if (strcmp(key, "case") == 0)
		{
			if (!ParseLong(value, &case_number)) return 0;
			common_seen++;
		}
		else if (strcmp(key, "frame") == 0)
		{
			if (!ParseLong(value, &frame)) return 0;
			common_seen++;
		}
		else if (strcmp(key, "event") == 0)
		{
			event = value;
			common_seen++;
		}
		else if (strcmp(key, "summary_set") == 0)
		{
			if (!ParseLong(value, &summary_set)) return 0;
			common_seen++;
		}
		else if (strcmp(key, "part") == 0)
		{
			int consumed = 0;

			if (sscanf(value, "%d/%d%n", &part, &total, &consumed) != 2 ||
			    consumed <= 0 || value[consumed] != '\0')
				return 0;
			common_seen++;
		}
		else
		{
			if (part < 1 || part > SG_ACCEPT_DROP_SUMMARY_PARTS ||
			    !WordInList(summary_payloads[part - 1], key))
				return 0;
			if (!WordInList(summary_string_payloads, key) &&
			    !ParseLong(value, &number))
				return 0;
			payload_seen++;
			if (strcmp(key, "terminal") == 0) terminal = value;
			if (strcmp(key, "reason") == 0) final_reason = value;
			if (strcmp(key, "diagnostic") == 0) diagnostic = value;
			if (strcmp(key, "complete") == 0)
			{
				if (!ParseLong(value, &number)) return 0;
				complete = (int)number;
			}
		}
	}
	if (common_seen != 8 || part != parsed->next_part + 1 ||
	    total != SG_ACCEPT_DROP_SUMMARY_PARTS || !event || !variant ||
	    strcmp(event, summary_events[part - 1]) != 0 ||
	    payload_seen == 0)
		return 0;
	/* Every expected payload key was present exactly once: unknowns and
	 * duplicates were rejected above, so equal word counts prove completeness. */
	{
		int expected = 0;
		const char *cursor = summary_payloads[part - 1];
		while (*cursor)
		{
			while (*cursor == ' ') cursor++;
			if (!*cursor) break;
			expected++;
			while (*cursor && *cursor != ' ') cursor++;
		}
		if (payload_seen != expected)
			return 0;
	}
	if (part == 1)
	{
		parsed->first_sequence = sequence;
		parsed->run = (int)run;
		parsed->case_number = (int)case_number;
		parsed->frame = (int)frame;
		parsed->summary_set = (int)summary_set;
		snprintf(parsed->variant, sizeof(parsed->variant), "%s", variant);
	}
	else if (sequence != parsed->first_sequence + (unsigned long)(part - 1) ||
	         run != parsed->run || case_number != parsed->case_number ||
	         frame != parsed->frame || summary_set != parsed->summary_set ||
	         strcmp(variant, parsed->variant) != 0)
		return 0;
	if (summary_set != run)
		return 0;
	if (part == SG_ACCEPT_DROP_SUMMARY_PARTS)
	{
		if (!terminal || !final_reason || !diagnostic || complete != 1)
			return 0;
		snprintf(parsed->terminal, sizeof(parsed->terminal), "%s", terminal);
		snprintf(parsed->reason, sizeof(parsed->reason), "%s", final_reason);
		snprintf(parsed->diagnostic, sizeof(parsed->diagnostic), "%s", diagnostic);
		parsed->complete = complete;
	}
	parsed->next_part++;
	return 1;
}

static int ParseSet(const char *const *records, int count,
	summary_parse_t *parsed)
{
	int index;

	if (!records || !parsed || count != SG_ACCEPT_DROP_SUMMARY_PARTS)
		return 0;
	memset(parsed, 0, sizeof(*parsed));
	for (index = 0; index < count; index++)
		if (!ParseRecord(parsed, records[index]))
			return 0;
	return parsed->next_part == SG_ACCEPT_DROP_SUMMARY_PARTS &&
	       parsed->complete == 1;
}

int main(void)
{
	sg_accept_drop_state_t state;
	char records[SG_ACCEPT_DROP_SUMMARY_PARTS]
	    [SG_ACCEPT_DROP_SUMMARY_LINE_CAP];
	const char *ordered[SG_ACCEPT_DROP_SUMMARY_PARTS];
	const char *mutated[SG_ACCEPT_DROP_SUMMARY_PARTS];
	char truncated[SG_ACCEPT_DROP_SUMMARY_LINE_CAP];
	char malformed[SG_ACCEPT_DROP_SUMMARY_LINE_CAP];
	summary_parse_t parsed;
	unsigned long first_sequence = ULONG_MAX - SG_ACCEPT_DROP_SUMMARY_PARTS;
	int longest = 0;
	int part;

	memset(&state, 0, sizeof(state));
	state.observed_run = INT_MAX;
	state.requested_case = 4;
	state.link = 85;
	state.started = true;
	state.finish_diagnostic = "finish-observer-contact-cache";
	state.final_outcome = SG_DROP_LIVE_ARRIVED;
	state.final_reason = SG_REPLAY_REASON_INVALID_STATE;
	state.observer.progress.status = SG_REPLAY_ARRIVED;
	state.observer.progress.reason = SG_REPLAY_REASON_NONE;
	state.production_status = SG_REPLAY_ARRIVED;
	state.production_reason = SG_REPLAY_REASON_NONE;
	/* Maximal decimal widths exercise the transport bound, not just the natural
	 * small counters. */
	state.action_begins = state.action_begin_errors = UINT_MAX;
	state.historical_commands = state.commands = UINT_MAX;
	state.zero_final_commands = state.final_historical_matches = UINT_MAX;
	state.final_historical_mismatches = state.arm_poses = state.poses = UINT_MAX;
	state.pusher_begins = state.pusher_ends = state.pusher_depth = UINT_MAX;
	state.pusher_order_errors = state.arrival_callbacks = UINT_MAX;
	state.recovery_callbacks = state.arrival_predicates = UINT_MAX;
	state.recovery_predicates = state.arrival_predicate_results = UINT_MAX;
	state.recovery_predicate_results = state.arrival_predicate_true = UINT_MAX;
	state.recovery_predicate_true = state.arrival_traces = UINT_MAX;
	state.recovery_traces = state.arrival_trace_true = UINT_MAX;
	state.recovery_trace_true = state.shelves = state.teaches = UINT_MAX;
	state.handoffs = state.boundary_enters = state.boundary_exits = UINT_MAX;
	state.boundary_results = state.result_arrival_samples = UINT_MAX;
	state.result_arrivals = state.result_recovery_samples = UINT_MAX;
	state.result_recovery_ready = state.result_recovery_started = UINT_MAX;
	state.observer_presteps = state.observer_poststeps = UINT_MAX;
	state.observer_boundaries = state.observer_command_matches = UINT_MAX;
	state.observer_command_mismatches = state.observer_arrival_cached = UINT_MAX;
	state.observer_arrival_inferred = state.observer_arrival_cached_true = UINT_MAX;
	state.observer_recovery_cached = state.observer_recovery_inferred = UINT_MAX;
	state.observer_recovery_cached_true = UINT_MAX;
	state.injection_attempts = state.injection_applied = UINT_MAX;
	state.injection_zero_ms = state.injection_errors = UINT_MAX;
	state.injection_pmove_traces = state.injection_pointcontents = UINT_MAX;
	state.injection_boundary_checks = state.injection_post_command_captures =
	    state.injection_post_command_validations =
	    state.injection_order_errors = UINT_MAX;
	state.injection_pre_contact_captured = true;
	state.injection_pre_arrival_samples = UINT_MAX;
	state.pre_contact_validated = state.pre_contact_sampled = UINT_MAX;
	state.pre_contact_last_sampled = state.pre_contact_errors = UINT_MAX;
	state.injection_entity_passes = state.injection_sg_frames = UINT_MAX;
	state.private_stops = state.generic_handoffs = UINT_MAX;
	state.generic_handoff_begins = state.generic_handoff_ends = UINT_MAX;
	state.generic_handoff_completed_substeps = UINT_MAX;
	state.generic_handoff_pending = true;
	state.generic_handoff_begin_valid = true;
	state.generic_handoff_substeps = state.generic_handoff_total_msec = INT_MAX;
	state.injection_fixture_seed = state.injection_step = INT_MAX;
	state.injection_frame = state.checkpoint_frame = INT_MAX;
	state.injection_order_stage = INT_MAX;
	state.injection_grounded = state.injection_support_valid = true;
	state.injection_watertype = state.injection_waterlevel = INT_MAX;
	state.injection_health = state.injection_deadflag = INT_MAX;
	state.injection_movetype = INT_MAX;
	state.injection_oldvelocity_zero = true;
	state.injection_terminal_geometry = state.injection_recovery_geometry = true;
	state.checkpoint = SGAD_CHECKPOINT_REV2_SHORT_LANDING;

	for (part = 0; part < SG_ACCEPT_DROP_SUMMARY_PARTS; part++)
	{
		int length = AcceptSummaryFormatPart(records[part],
		    sizeof(records[part]), &state, &accept_selectors[3],
		    "acceptance-rejected",
		    part, first_sequence + (unsigned long)part, INT_MAX, "A-legacy");

		if (length <= 0 || length >= SG_ACCEPT_DROP_SUMMARY_LINE_MAX ||
		    records[part][length - 1] != '\n')
		{
			fprintf(stderr, "part %d violated line bound/newline: %d\n",
			    part + 1, length);
			return 1;
		}
		if (length > longest) longest = length;
		ordered[part] = records[part];
	}
	if (!ParseSet(ordered, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed) ||
	    strcmp(parsed.terminal, "acceptance-rejected") != 0 ||
	    strcmp(parsed.reason, "invalid-state") != 0 ||
	    strcmp(parsed.diagnostic, "finish-observer-contact-cache") != 0)
	{
		fprintf(stderr, "complete rejection roundtrip rejected\n");
		return 1;
	}
	memcpy(mutated, ordered, sizeof(mutated));
	mutated[3] = ordered[2];
	if (ParseSet(mutated, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed))
	{
		fprintf(stderr, "duplicate/out-of-order set accepted\n");
		return 1;
	}
	if (ParseSet(ordered, SG_ACCEPT_DROP_SUMMARY_PARTS - 1, &parsed))
	{
		fprintf(stderr, "missing terminal set accepted\n");
		return 1;
	}
	snprintf(truncated, sizeof(truncated), "%s", records[8]);
	{
		char *complete = strstr(truncated, " complete=1");
		if (!complete)
			return 1;
		*complete++ = '\n';
		*complete = '\0';
	}
	memcpy(mutated, ordered, sizeof(mutated));
	mutated[8] = truncated;
	if (ParseSet(mutated, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed))
	{
		fprintf(stderr, "truncated terminal accepted\n");
		return 1;
	}
	snprintf(malformed, sizeof(malformed), "%s", records[0]);
	{
		char *tail = strstr(malformed, " part=1/9");

		if (!tail)
			return 1;
		tail += strlen(" part=1/9");
		memmove(tail + 1, tail, strlen(tail) + 1);
		*tail = 'x';
	}
	memcpy(mutated, ordered, sizeof(mutated));
	mutated[0] = malformed;
	if (ParseSet(mutated, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed))
	{
		fprintf(stderr, "malformed part accepted\n");
		return 1;
	}
	snprintf(malformed, sizeof(malformed), "%s", records[1]);
	{
		char *value = strstr(malformed, " action_begins=");

		if (!value)
			return 1;
		value += strlen(" action_begins=");
		*value = 'x';
	}
	memcpy(mutated, ordered, sizeof(mutated));
	mutated[1] = malformed;
	if (ParseSet(mutated, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed))
	{
		fprintf(stderr, "malformed numeric payload accepted\n");
		return 1;
	}
	snprintf(malformed, sizeof(malformed), "%s", records[8]);
	{
		char *reason = strstr(malformed, " reason=invalid-state");
		char *separator;

		if (!reason || !(separator = strchr(reason + 1, '-')))
			return 1;
		*separator = ' ';
	}
	memcpy(mutated, ordered, sizeof(mutated));
	mutated[8] = malformed;
	if (ParseSet(mutated, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed))
	{
		fprintf(stderr, "whitespace-bearing reason accepted\n");
		return 1;
	}
	snprintf(malformed, sizeof(malformed), "%s", records[8]);
	{
		char *diagnostic = strstr(malformed,
		    " diagnostic=finish-observer-contact-cache");
		char *separator;

		if (!diagnostic || !(separator = strchr(diagnostic + 1, '-')))
			return 1;
		*separator = ' ';
	}
	memcpy(mutated, ordered, sizeof(mutated));
	mutated[8] = malformed;
	if (ParseSet(mutated, SG_ACCEPT_DROP_SUMMARY_PARTS, &parsed))
	{
		fprintf(stderr, "whitespace-bearing diagnostic accepted\n");
		return 1;
	}
	for (part = SG_REPLAY_REASON_NONE;
	     part <= SG_REPLAY_REASON_HOOK_TERMINAL_LOST; part++)
	{
		const char *token = AcceptReplayReasonToken((sg_replay_reason_t)part);

		if (!token || !*token ||
		    strspn(token, "abcdefghijklmnopqrstuvwxyz0123456789-") !=
		        strlen(token))
		{
			fprintf(stderr, "reason %d is not a stable token\n", part);
			return 1;
		}
	}
	printf("summary-selftest ok parts=%d longest=%d max=%d "
	       "strict-set=complete rejection-roundtrip=complete\n",
	    SG_ACCEPT_DROP_SUMMARY_PARTS, longest,
	    SG_ACCEPT_DROP_SUMMARY_LINE_MAX - 1);
	return 0;
}
