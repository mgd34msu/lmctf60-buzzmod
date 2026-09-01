/* Owner-private authority for one exact current strategy output. */
#ifndef SG_STRATEGY_CALLER_PRIVATE_H
#define SG_STRATEGY_CALLER_PRIVATE_H

#include "sg_strategy_caller.h"

#define SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES 32U

/* These byte strings are bearer capabilities.  Their representation has no
 * caller-visible meaning.  A proof is one-use; Consume replaces it with a
 * read-only receipt for the prepared transition's later currentness checks. */
typedef struct sg_strategy_caller_output_proof_s
{
	uint8_t opaque[SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES];
} sg_strategy_caller_output_proof_t;

typedef struct sg_strategy_caller_output_receipt_s
{
	uint8_t opaque[SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES];
} sg_strategy_caller_output_receipt_t;

int SG_StrategyCallerOutputCurrent(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output);

/* Issue is used only after the runtime bridge has revalidated the field query.
 * A successful issue supersedes every earlier proof and receipt. */
int SG_StrategyCallerOutputProofIssue(sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	sg_strategy_caller_output_proof_t *proof_out);
int SG_StrategyCallerOutputProofCurrent(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *proof);
int SG_StrategyCallerOutputProofConsume(sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *proof,
	sg_strategy_caller_output_receipt_t *receipt_out);
int SG_StrategyCallerOutputReceiptCurrent(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_receipt_t *receipt);

/* Valid only after Consume: proves that receipt is the phase transform of
 * this exact issued proof, not merely a newer proof for the same output. */
int SG_StrategyCallerOutputReceiptMatchesProof(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *proof,
	const sg_strategy_caller_output_receipt_t *receipt);

/* Non-current terminal cleanup check. It compares only the opaque owner,
 * issuance, discriminant, and token lineage, so an already-mutated caller can
 * still release retained borrows without authenticating a different receipt. */
int SG_StrategyCallerOutputReceiptLineageMatches(
	const sg_strategy_caller_output_proof_t *proof,
	const sg_strategy_caller_output_receipt_t *receipt);

#endif /* SG_STRATEGY_CALLER_PRIVATE_H */
