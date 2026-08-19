#include "sg_hook_diagnostics.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static uint64_t sg_hook_diagnostic_fallback_token = 1U;
static uint64_t sg_hook_diagnostic_fallback_epoch;

static const char *HookDiagnosticsKindName(sg_hook_diagnostic_kind_t kind)
{
	return kind == SG_HOOK_DIAGNOSTIC_SPEED ? "speed" : "graph";
}

static void HookDiagnosticsCopyValue(char *out, size_t out_size,
	const char *value)
{
	size_t i;

	if (!out || out_size == 0)
		return;
	if (!value || !*value)
	{
		if (out_size == 1)
		{
			out[0] = '\0';
			return;
		}
		out[0] = '-';
		out[1] = '\0';
		return;
	}
	for (i = 0; i + 1 < out_size && value[i]; i++)
	{
		unsigned char c = (unsigned char)value[i];

		out[i] = (isalnum(c) || c == '_' || c == '-' || c == '.') ?
			(char)c : '_';
	}
	out[i] = '\0';
}

static void HookDiagnosticsAdvanceSequence(sg_hook_diagnostic_state_t *state)
{
	if (state->sequence == UINT64_MAX)
	{
		state->sequence = 1U;
		state->sequence_epoch++;
	}
	else
		state->sequence++;
}

static void HookDiagnosticsAssignToken(sg_hook_diagnostic_state_t *state,
	uint64_t instance_token)
{
	state->instance_token = instance_token;
	state->diagnostic_token_is_fallback = instance_token == 0U;
	if (!state->diagnostic_token_is_fallback)
	{
		state->diagnostic_token = instance_token;
		state->diagnostic_token_epoch = 0U;
		return;
	}
	state->diagnostic_token = sg_hook_diagnostic_fallback_token++;
	state->diagnostic_token_epoch = sg_hook_diagnostic_fallback_epoch;
	if (sg_hook_diagnostic_fallback_token == 0U)
	{
		sg_hook_diagnostic_fallback_token = 1U;
		sg_hook_diagnostic_fallback_epoch++;
	}
}

static void HookDiagnosticsFormatId(const sg_hook_diagnostic_state_t *state,
	char *out, size_t out_size)
{
	if (!out || out_size == 0 || !state)
		return;
	if (state->diagnostic_token_is_fallback)
		snprintf(out, out_size, "z%llu.%llu.%llu.%llu",
			(unsigned long long)state->diagnostic_token_epoch,
			(unsigned long long)state->diagnostic_token,
			(unsigned long long)state->sequence_epoch,
			(unsigned long long)state->sequence);
	else
		snprintf(out, out_size, "i%llu.%llu.%llu",
			(unsigned long long)state->diagnostic_token,
			(unsigned long long)state->sequence_epoch,
			(unsigned long long)state->sequence);
}

static void HookDiagnosticsEmit(const sg_hook_diagnostic_state_t *state,
	const char *event, const char *reason, const char *detail)
{
	char line[640];
	char id[96];
	char safe_reason[64];
	char safe_detail[96];

	if (!state || !state->debug_latched || !state->emit)
		return;
	HookDiagnosticsFormatId(state, id, sizeof(id));
	HookDiagnosticsCopyValue(safe_reason, sizeof(safe_reason), reason);
	HookDiagnosticsCopyValue(safe_detail, sizeof(safe_detail), detail);
	if (event && event[0] == 'F')
		snprintf(line, sizeof(line),
			"HOOKFIRE id=%s bot=%s kind=%s link=%d role=%d map=%s anchor_q8=%d,%d,%d\n",
			id, state->bot_snapshot, HookDiagnosticsKindName(state->kind),
			state->fire_link, state->role_snapshot, state->map_snapshot,
			(int)state->anchor_q8[0], (int)state->anchor_q8[1],
			(int)state->anchor_q8[2]);
	else
		snprintf(line, sizeof(line),
			"HOOKEND id=%s bot=%s kind=%s link=%d role=%d map=%s anchor_q8=%d,%d,%d reason=%s detail=%s\n",
			id, state->bot_snapshot, HookDiagnosticsKindName(state->kind),
			state->fire_link, state->role_snapshot, state->map_snapshot,
			(int)state->anchor_q8[0], (int)state->anchor_q8[1],
			(int)state->anchor_q8[2],
			safe_reason, safe_detail);
	state->emit(state->emit_opaque, line);
}

void SG_HookDiagnosticsReset(sg_hook_diagnostic_state_t *state)
{
	if (state)
		memset(state, 0, sizeof(*state));
}

int SG_HookDiagnosticsBegin(sg_hook_diagnostic_state_t *state,
	int debug_enabled, sg_hook_diagnostic_kind_t kind, int fire_link,
	int role_snapshot, const char *bot_snapshot, const char *map_snapshot,
	const int32_t anchor_q8[3], uint64_t instance_token,
	sg_hook_diagnostic_emit_fn emit, void *emit_opaque)
{
	if (!state || !emit)
		return 0;
	/* A stale diagnostic record must never suppress a later real fire. This is
	 * observational only: it deliberately does not touch host hook state. */
	if (state->open)
		(void)SG_HookDiagnosticsFinish(state, "superseded", "reentrant-fire");
	HookDiagnosticsAdvanceSequence(state);
	state->open = 1;
	state->debug_latched = debug_enabled ? 1 : 0;
	state->kind = kind;
	state->fire_link = fire_link;
	state->role_snapshot = role_snapshot;
	HookDiagnosticsAssignToken(state, instance_token);
	state->emit = emit;
	state->emit_opaque = emit_opaque;
	HookDiagnosticsCopyValue(state->bot_snapshot, sizeof(state->bot_snapshot),
		bot_snapshot);
	HookDiagnosticsCopyValue(state->map_snapshot, sizeof(state->map_snapshot),
		map_snapshot);
	if (anchor_q8)
		memcpy(state->anchor_q8, anchor_q8, sizeof(state->anchor_q8));
	else
		memset(state->anchor_q8, 0, sizeof(state->anchor_q8));
	HookDiagnosticsEmit(state, "FIRE", NULL, NULL);
	return 1;
}

int SG_HookDiagnosticsFinish(sg_hook_diagnostic_state_t *state,
	const char *reason, const char *detail)
{
	if (!state || !state->open)
		return 0;
	HookDiagnosticsEmit(state, "END", reason, detail);
	state->open = 0;
	state->debug_latched = 0;
	state->emit = NULL;
	state->emit_opaque = NULL;
	return 1;
}
