/* Focused host-free pairing checks; compile directly with sg_hook_diagnostics.c. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_hook_diagnostics.h"

typedef struct diagnostic_log_s
{
	char lines[48][640];
	int count;
} diagnostic_log_t;

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
		failures++; \
	} \
} while (0)

static void Capture(void *opaque, const char *line)
{
	diagnostic_log_t *log = opaque;

	if (log && log->count < (int)(sizeof(log->lines) / sizeof(log->lines[0])))
	{
		snprintf(log->lines[log->count], sizeof(log->lines[log->count]),
			"%s", line ? line : "");
		log->count++;
	}
}

static int Begin(sg_hook_diagnostic_state_t *state, diagnostic_log_t *log,
	int enabled, sg_hook_diagnostic_kind_t kind, int link,
	const char *bot, const char *map, const int32_t anchor_q8[3],
	uint64_t token)
{
	return SG_HookDiagnosticsBegin(state, enabled, kind, link, 3, bot, map,
		anchor_q8, token, Capture, log);
}

static int IsFire(const char *line)
{
	return line && strncmp(line, "HOOKFIRE ", 9) == 0;
}

static int IsEnd(const char *line)
{
	return line && strncmp(line, "HOOKEND ", 8) == 0;
}

static void CopyId(const char *line, char *out, size_t out_size)
{
	const char *start = line ? strstr(line, "id=") : NULL;
	size_t n = 0;

	if (!out || out_size == 0)
		return;
	if (start)
	{
		start += 3;
		while (start[n] && start[n] != ' ' && n + 1 < out_size)
		{
			out[n] = start[n];
			n++;
		}
	}
	out[n] = '\0';
}

int main(void)
{
	sg_hook_diagnostic_state_t graph = {0};
	sg_hook_diagnostic_state_t null_sink = {0};
	sg_hook_diagnostic_state_t zero_a = {0};
	sg_hook_diagnostic_state_t zero_b = {0};
	sg_hook_diagnostic_state_t wrap = {0};
	sg_hook_diagnostic_state_t disabled = {0};
	sg_hook_diagnostic_state_t map_terminal = {0};
	sg_hook_diagnostic_state_t slot_terminal = {0};
	sg_hook_diagnostic_state_t max_strings = {0};
	diagnostic_log_t log = {{{0}}, 0};
	int32_t anchor[3] = {80, -16, 320};
	char bot[128] = "Arach unsafe bot";
	char map[128] = "lmctf6 unsafe map";
	char long_bot[128];
	char long_map[128];
	char zero_id_a[96];
	char zero_id_b[96];
	char wrap_id_before[96];
	char wrap_id_after[96];
	int first_count;

	/* Begin snapshots all output fields; later host changes cannot rewrite END. */
	CHECK(Begin(&graph, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 12, bot, map,
	    anchor, UINT64_C(41)));
	CHECK(log.count == 1 && IsFire(log.lines[0]));
	CHECK(strstr(log.lines[0], "id=i41.0.1 bot=Arach_unsafe_bot") != NULL);
	CHECK(strstr(log.lines[0], "anchor_q8=80,-16,320") != NULL);
	strcpy(bot, "replaced");
	strcpy(map, "other");
	anchor[0] = 1;
	CHECK(SG_HookDiagnosticsFinish(&graph, "graph-fail", "begin failed"));
	CHECK(log.count == 2 && IsEnd(log.lines[1]));
	CHECK(strstr(log.lines[1], "id=i41.0.1 bot=Arach_unsafe_bot") != NULL);
	CHECK(strstr(log.lines[1], "map=lmctf6_unsafe_map anchor_q8=80,-16,320") != NULL);
	CHECK(strstr(log.lines[1], "detail=begin_failed") != NULL);
	CHECK(!SG_HookDiagnosticsFinish(&graph, "duplicate", "must-not-emit"));
	CHECK(log.count == 2);

	/* A stale diagnostic can never suppress a successful later fire. */
	CHECK(Begin(&graph, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 12, "Arach",
	    "lmctf6", anchor, UINT64_C(41)));
	CHECK(Begin(&graph, &log, 1, SG_HOOK_DIAGNOSTIC_SPEED, -1, "Arach",
	    "lmctf6", anchor, UINT64_C(41)));
	CHECK(log.count == 5 && IsFire(log.lines[2]) && IsEnd(log.lines[3]) &&
	    IsFire(log.lines[4]));
	CHECK(strstr(log.lines[3], "reason=superseded detail=reentrant-fire") != NULL);
	CHECK(SG_HookDiagnosticsFinish(&graph, "burst", "speed-terminal"));
	CHECK(log.count == 6 && strstr(log.lines[5], "kind=speed") != NULL);

	/* Rejecting a bad new sink must leave the preceding open record intact. */
	CHECK(Begin(&null_sink, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 5, "Rune",
	    "lmctf6", anchor, UINT64_C(42)));
	first_count = log.count;
	CHECK(!SG_HookDiagnosticsBegin(&null_sink, 1, SG_HOOK_DIAGNOSTIC_SPEED,
	    -1, 3, "Rune", "lmctf6", anchor, UINT64_C(42), NULL, NULL));
	CHECK(null_sink.open && log.count == first_count);
	CHECK(SG_HookDiagnosticsFinish(&null_sink, "arrived", "reducer"));
	CHECK(log.count == first_count + 1 && strstr(log.lines[log.count - 1],
	    "reason=arrived") != NULL);

	/* A zero SG instance token gets a collision-free, visibly fallback ID. */
	CHECK(Begin(&zero_a, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 12, "ZeroA",
	    "lmctf6", anchor, 0));
	CHECK(Begin(&zero_b, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 12, "ZeroB",
	    "lmctf6", anchor, 0));
	CHECK(zero_a.instance_token == 0 && zero_b.instance_token == 0);
	CHECK(strstr(log.lines[log.count - 2], "id=z") != NULL);
	CopyId(log.lines[log.count - 2], zero_id_a, sizeof(zero_id_a));
	CopyId(log.lines[log.count - 1], zero_id_b, sizeof(zero_id_b));
	CHECK(strcmp(zero_id_a, zero_id_b) != 0);
	CHECK(SG_HookDiagnosticsFinish(&zero_a, "death", "lifecycle"));
	CHECK(SG_HookDiagnosticsFinish(&zero_b, "slot-retirement", "lifecycle"));

	/* A forced sequence wrap advances an epoch, so the next ID is distinct. */
	CHECK(Begin(&wrap, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 9, "Wrap",
	    "lmctf6", anchor, UINT64_C(88)));
	CopyId(log.lines[log.count - 1], wrap_id_before, sizeof(wrap_id_before));
	CHECK(SG_HookDiagnosticsFinish(&wrap, "arrived", "reducer"));
	wrap.sequence = UINT64_MAX;
	wrap.sequence_epoch = UINT64_C(9);
	CHECK(Begin(&wrap, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 9, "Wrap",
	    "lmctf6", anchor, UINT64_C(88)));
	CopyId(log.lines[log.count - 1], wrap_id_after, sizeof(wrap_id_after));
	CHECK(strstr(log.lines[log.count - 1], "id=i88.10.1") != NULL);
	CHECK(strcmp(wrap_id_before, wrap_id_after) != 0);
	CHECK(SG_HookDiagnosticsFinish(&wrap, "arrived", "reducer"));

	/* sg_debug is sampled only at Begin; disabled attempts emit neither half. */
	first_count = log.count;
	CHECK(Begin(&disabled, &log, 0, SG_HOOK_DIAGNOSTIC_GRAPH, 12, "Quiet",
	    "lmctf6", anchor, UINT64_C(77)));
	CHECK(disabled.open && !disabled.debug_latched);
	CHECK(SG_HookDiagnosticsFinish(&disabled, "arrived", "reducer"));
	CHECK(log.count == first_count);

	/* Map teardown closes first; later slot teardown is an intentional no-op. */
	CHECK(Begin(&map_terminal, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 1, "Map",
	    "lmctf6", anchor, UINT64_C(91)));
	CHECK(SG_HookDiagnosticsFinish(&map_terminal, "map-transition", "level-change"));
	CHECK(!SG_HookDiagnosticsFinish(&map_terminal, "slot-retirement", "lifecycle"));
	CHECK(Begin(&slot_terminal, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 1, "Slot",
	    "lmctf6", anchor, UINT64_C(92)));
	CHECK(SG_HookDiagnosticsFinish(&slot_terminal, "slot-retirement", "lifecycle"));
	CHECK(!SG_HookDiagnosticsFinish(&slot_terminal, "map-transition", "level-change"));

	memset(long_bot, 'b', sizeof(long_bot) - 1);
	long_bot[sizeof(long_bot) - 1] = '\0';
	memset(long_map, 'm', sizeof(long_map) - 1);
	long_map[sizeof(long_map) - 1] = '\0';
	anchor[0] = INT32_MIN;
	anchor[1] = INT32_MAX;
	anchor[2] = 0;
	CHECK(Begin(&max_strings, &log, 1, SG_HOOK_DIAGNOSTIC_GRAPH, 2, long_bot,
	    long_map, anchor, UINT64_C(93)));
	CHECK(strchr(log.lines[log.count - 1], '\n') != NULL);
	CHECK(strstr(log.lines[log.count - 1], "anchor_q8=-2147483648,2147483647,0") != NULL);
	CHECK(SG_HookDiagnosticsFinish(&max_strings, "landing_timeout", long_map));
	CHECK(strchr(log.lines[log.count - 1], '\n') != NULL);

	if (failures)
	{
		fprintf(stderr, "%d sg_hook_diagnostics tests failed\n", failures);
		return 1;
	}
	return 0;
}
