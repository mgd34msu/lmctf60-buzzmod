/* sg_rune_proof.h -- scoped nominal gravity for RUNE generation. */
#ifndef SG_RUNE_PROOF_H
#define SG_RUNE_PROOF_H

#include <stddef.h>
#include <stdint.h>

int SG_RuneFunkyGravityCompatible(const float *value);

/* Open-sky hook lips are a compact, local fallback.  One vertical seed tier
 * is still local, but must not be confused with the general hook prover. */
#define SG_RUNE_PROOF_HOOK_LATERAL_MIN_RISE 32.0f
#define SG_RUNE_PROOF_HOOK_LATERAL_MAX_RISE 128.0f
#define SG_RUNE_PROOF_HOOK_LATERAL_MAX_HORIZONTAL 128.0f
int SG_RuneProofHookLateralWindow(float horizontal, float rise);

#define SG_RUNE_PROOF_HOOK_FRONTIER_MAX 8192U

typedef struct sg_rune_proof_hook_seed_s
{
	int32_t origin_q8[3];
	int component;
	uint8_t objective_mask;
	uint8_t water;
	uint8_t stable;
	uint8_t waterlevel;
} sg_rune_proof_hook_seed_t;

typedef struct sg_rune_proof_hook_candidate_s
{
	int from;
	int to;
	uint8_t rank;
} sg_rune_proof_hook_candidate_t;

/* Exact two-rope low-gravity traversal envelope.  Frontier nomination and
 * the chain prover share this contract so every provable pair is scheduled. */
#define SG_RUNE_PROOF_CHAIN_HOOK_MAX_HORIZONTAL 3200
#define SG_RUNE_PROOF_CHAIN_HOOK_MAX_VERTICAL 1024

typedef struct sg_rune_proof_hook_frontier_cursor_s
{
	size_t rank;
	size_t next_component;
	int initialized;
	int exhausted;
} sg_rune_proof_hook_frontier_cursor_t;

typedef struct sg_rune_proof_hook_frontier_s
{
	const sg_rune_proof_hook_seed_t *seeds;
	size_t seed_count;
	size_t component_count;
	size_t global_limit;
	uint16_t component_limit;
	uint16_t source_limit;
	uint16_t *component_trials;
	uint16_t *source_trials;
	size_t *source_cursor;
	size_t *component_source_cursor;
	sg_rune_proof_hook_candidate_t *output;
	size_t output_capacity;
	sg_rune_proof_hook_frontier_cursor_t *cursor;
} sg_rune_proof_hook_frontier_t;

/* With cursor == NULL, select one fixed canonical frontier. With a cursor,
 * select one bounded canonical batch and resume until exhausted. The selector
 * never claims a traversal; ProveHook remains the sole authority that can
 * turn one selected pair into RL_HOOK. */
size_t SG_RuneProofSelectHookFrontier(
	const sg_rune_proof_hook_frontier_t *frontier);
void SG_RuneProofHookFrontierCursorReset(
	sg_rune_proof_hook_frontier_cursor_t *cursor);

#define SG_RUNE_PROOF_OBJECTIVE_RUN_MIN_HORIZONTAL_Q8 (192 * 8)
#define SG_RUNE_PROOF_OBJECTIVE_RUN_MAX_HORIZONTAL_Q8 (768 * 8)
#define SG_RUNE_PROOF_OBJECTIVE_RUN_MAX_VERTICAL_Q8 (16 * 8)

typedef struct sg_rune_proof_objective_run_seed_s
{
	int32_t origin_q8[3];
	int component;
	uint8_t forward_mask;
	uint8_t stable;
	uint8_t waterlevel;
} sg_rune_proof_objective_run_seed_t;

int SG_RuneProofObjectiveRunCandidate(
	const sg_rune_proof_objective_run_seed_t *from,
	const sg_rune_proof_objective_run_seed_t *to,
	uint8_t objective_bit);
int SG_RuneProofObjectiveRunReplayAccepted(int edge_seek, int airborne);

/* Nominal oracle placement uses 800 outside this single-owner scope. Active
 * generation begins with its already captured integral gravity and must end
 * the scope on every exit. Nested begin attempts fail without mutation. */
int SG_RuneProofScopeBegin(float gravity);
void SG_RuneProofScopeEnd(void);
short SG_RuneProofGravity(void);
int SG_RuneProofScopeActive(void);

#endif /* SG_RUNE_PROOF_H */
