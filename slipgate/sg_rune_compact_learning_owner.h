#ifndef SG_RUNE_COMPACT_LEARNING_OWNER_H
#define SG_RUNE_COMPACT_LEARNING_OWNER_H

#ifndef SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE
#error "only authenticated capture adapters may include this header"
#endif

#include "sg_rune_compact_learning.h"

typedef struct sg_rune_compact_learning_issuer_s
	sg_rune_compact_learning_issuer_t;

typedef struct sg_rune_compact_learning_claim_s
{
	sg_rune_compact_learning_key_t key;
	float value;
} sg_rune_compact_learning_claim_t;

/* Authenticated human-trace and bot capture adapters define
 * SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE before including this internal header.
 * Acquiring an issuer fixes its evidence class and exact model identity. */
sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerAcquireHuman(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out);
sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerAcquireBot(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out);
void SG_RuneCompactLearningIssuerDestroy(
	sg_rune_compact_learning_issuer_t *issuer);

/* The opaque issuer, rather than a caller-selected field, determines whether
 * this is authenticated human or bot evidence. */
sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerIssue(
	const sg_rune_compact_learning_issuer_t *issuer,
	const sg_rune_compact_learning_claim_t *claim,
	sg_rune_compact_learning_observation_t **observation_out);

#endif
