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
} sg_rune_proof_hook_frontier_t;

/* Select a fixed, canonical set of exact-oracle trials from an ordinary
 * graph topology. The selector never claims a traversal; ProveHook remains
 * the sole authority that can turn one selected pair into RL_HOOK. */
size_t SG_RuneProofSelectHookFrontier(
	const sg_rune_proof_hook_frontier_t *frontier);

/* Nominal oracle placement uses 800 outside this single-owner scope. Active
 * generation begins with its already captured integral gravity and must end
 * the scope on every exit. Nested begin attempts fail without mutation. */
int SG_RuneProofScopeBegin(float gravity);
void SG_RuneProofScopeEnd(void);
short SG_RuneProofGravity(void);
int SG_RuneProofScopeActive(void);

#endif /* SG_RUNE_PROOF_H */
