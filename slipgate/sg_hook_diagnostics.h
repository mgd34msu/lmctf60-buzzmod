/* Host-free, latched diagnostic pairing for successful bot hook fires. */
#ifndef SG_HOOK_DIAGNOSTICS_H
#define SG_HOOK_DIAGNOSTICS_H

#include <stdint.h>

#define SG_HOOK_DIAGNOSTIC_MAP_CAPACITY 64
#define SG_HOOK_DIAGNOSTIC_BOT_CAPACITY 64

typedef enum sg_hook_diagnostic_kind_e
{
	SG_HOOK_DIAGNOSTIC_GRAPH = 0,
	SG_HOOK_DIAGNOSTIC_SPEED
} sg_hook_diagnostic_kind_t;

typedef void (*sg_hook_diagnostic_emit_fn)(void *opaque, const char *line);

typedef struct sg_hook_diagnostic_state_s
{
	int open;
	int debug_latched;
	sg_hook_diagnostic_kind_t kind;
	int fire_link;
	int role_snapshot;
	char map_snapshot[SG_HOOK_DIAGNOSTIC_MAP_CAPACITY];
	char bot_snapshot[SG_HOOK_DIAGNOSTIC_BOT_CAPACITY];
	int32_t anchor_q8[3];
	/* The immutable SG token is retained even if a zero-token diagnostic
	 * fallback is needed. sequence_epoch makes a forced sequence wrap distinct. */
	uint64_t instance_token;
	uint64_t diagnostic_token;
	uint64_t diagnostic_token_epoch;
	uint64_t sequence_epoch;
	uint64_t sequence;
	int diagnostic_token_is_fallback;
	sg_hook_diagnostic_emit_fn emit;
	void *emit_opaque;
} sg_hook_diagnostic_state_t;

void SG_HookDiagnosticsReset(sg_hook_diagnostic_state_t *state);
/* Begin/Finish return state-transition success only. The void sink is invoked
 * exactly once per enabled event but provides no transport durability ack. */
int SG_HookDiagnosticsBegin(sg_hook_diagnostic_state_t *state,
	int debug_enabled, sg_hook_diagnostic_kind_t kind, int fire_link,
	int role_snapshot, const char *bot_snapshot, const char *map_snapshot,
	const int32_t anchor_q8[3], uint64_t instance_token,
	sg_hook_diagnostic_emit_fn emit, void *emit_opaque);
int SG_HookDiagnosticsFinish(sg_hook_diagnostic_state_t *state,
	const char *reason, const char *detail);

#endif
