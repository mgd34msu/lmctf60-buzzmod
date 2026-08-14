/* sg_rune_proof.h -- scoped nominal gravity for RUNE generation. */
#ifndef SG_RUNE_PROOF_H
#define SG_RUNE_PROOF_H

/* Preserve the historical v2 predicate exactly, including its short cast. */
int SG_RuneV2GravityCompatible(float gravity);
int SG_RuneV3FunkyGravityCompatible(const float *value);

/* Nominal oracle placement is legacy/800 outside this single-owner scope.
 * V3 generation begins with its already captured integral gravity and must
 * end the scope on every exit.  Nested begin attempts fail without mutation. */
int SG_RuneProofScopeBegin(float gravity);
void SG_RuneProofScopeEnd(void);
short SG_RuneProofGravity(void);
int SG_RuneProofScopeActive(void);

#endif /* SG_RUNE_PROOF_H */
