#ifndef SG_RUNE_HOOK_FRONTIER_H
#define SG_RUNE_HOOK_FRONTIER_H

#include <stdint.h>

#include "sg_rune.h"

typedef struct sg_rune_hook_frontier_input_s
{
	const rune_seed_t *seeds;
	int seed_count;
	const byte *source_stable;
	const byte *source_waterlevel;
	const int *component;
	const byte *objective_mask;
	int component_count;
	rune_link_t *links;
	int *link_count;
	qboolean *link_overflow;
	int *hook_envelope_count;
	uint32_t *prover_calls;
} sg_rune_hook_frontier_input_t;

typedef struct sg_rune_hook_nomination_proof_s
{
	rune_action_t action;
	vec3_t control[2];
	short cost_ms;
	byte exit_speed;
} sg_rune_hook_nomination_proof_t;

qboolean SG_RuneGenerateHookFrontier(
	const sg_rune_hook_frontier_input_t *input);
qboolean SG_RuneProveHook(const sg_rune_hook_frontier_input_t *input,
	int from, int to, vec3_t control_out, short *cost_ms, byte *exit_speed);
qboolean SG_RuneProveHookNomination(
	const sg_rune_hook_frontier_input_t *input, int from, int to,
	uint8_t rope_count, const int32_t bite_q8[2][3],
	sg_rune_hook_nomination_proof_t *out);
qboolean SG_RuneReproveHookControl(
	const sg_rune_hook_frontier_input_t *input, int from, int to,
	rune_action_t action, const vec3_t control[2],
	sg_rune_hook_nomination_proof_t *out);

#endif /* SG_RUNE_HOOK_FRONTIER_H */
