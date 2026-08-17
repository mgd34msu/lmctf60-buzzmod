/* sg_rune_proof.h -- scoped nominal gravity for RUNE generation. */
#ifndef SG_RUNE_PROOF_H
#define SG_RUNE_PROOF_H

int SG_RuneFunkyGravityCompatible(const float *value);

/* Open-sky hook lips are a compact, local fallback.  One vertical seed tier
 * is still local, but must not be confused with the general hook prover. */
#define SG_RUNE_PROOF_HOOK_LATERAL_MIN_RISE 32.0f
#define SG_RUNE_PROOF_HOOK_LATERAL_MAX_RISE 128.0f
#define SG_RUNE_PROOF_HOOK_LATERAL_MAX_HORIZONTAL 128.0f
int SG_RuneProofHookLateralWindow(float horizontal, float rise);

/* Nominal oracle placement uses 800 outside this single-owner scope. Active
 * generation begins with its already captured integral gravity and must end
 * the scope on every exit. Nested begin attempts fail without mutation. */
int SG_RuneProofScopeBegin(float gravity);
void SG_RuneProofScopeEnd(void);
short SG_RuneProofGravity(void);
int SG_RuneProofScopeActive(void);

#endif /* SG_RUNE_PROOF_H */
