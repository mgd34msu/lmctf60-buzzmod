#ifndef SG_RUNE_COMPACT_LEARNING_CONSUMER_H
#define SG_RUNE_COMPACT_LEARNING_CONSUMER_H

/* Post-match consumer for recorder-owned accepted human-trace evidence.
 * The recorder controls the collection and opaque life scopes.  The engine
 * validator maps each authenticated event to an existing compact reference.
 * This module never creates cells, geometry, portals, or connectivity. */

#include <stdint.h>

#include "sg_human_trace.h"
#include "sg_rune_compact_learning.h"

typedef struct sg_rune_compact_learning_consumer_s
	sg_rune_compact_learning_consumer_t;

typedef struct sg_rune_compact_learning_consumer_claim_s
{
	sg_rune_compact_learning_key_t key;
	float value;
} sg_rune_compact_learning_consumer_claim_t;

/* The validator is an engine-owned replay boundary.  It is called in the
 * exact order supplied by the recorder and receives the authenticated segment
 * that recorded the event.  SKIP means valid trace data is not a learnable
 * claim.  ACCEPT must fill claim_out with an existing model reference. */
typedef enum sg_rune_compact_learning_consumer_validation_e
{
	SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP = 0,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_FATAL,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_COUNT
} sg_rune_compact_learning_consumer_validation_t;

typedef sg_rune_compact_learning_consumer_validation_t
	(*sg_rune_compact_learning_consumer_validate_fn)(void *context,
		const sg_rune_compact_model_t *model,
		const sg_human_trace_v3_segment_ref_t *segment,
		const sg_human_trace_v3_event_t *event,
		sg_rune_compact_learning_consumer_claim_t *claim_out);

typedef enum sg_rune_compact_learning_consumer_status_e
{
	SG_RUNE_COMPACT_LEARNING_CONSUMER_OK = 0,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_MODEL,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_PHYSICS_MISMATCH,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_LIFE_MISMATCH,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_EVENT,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_ENGINE_REJECTED,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_CLAIM,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_REFERENCE,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_TRANSACTION_FAILED,
	SG_RUNE_COMPACT_LEARNING_CONSUMER_STATUS_COUNT
} sg_rune_compact_learning_consumer_status_t;

typedef struct sg_rune_compact_learning_consumer_report_s
{
	uint32_t event_count;
	uint32_t validated_count;
	uint32_t skipped_count;
	uint32_t applied_count;
	uint32_t prior_count_before;
	uint32_t prior_count_after;
} sg_rune_compact_learning_consumer_report_t;

/* Creation validates the full model and binds the consumer to one immutable
 * identity.  The consumer owns its prior accumulator and borrows model. */
sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningConsumerCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_consumer_t **consumer_out,
	sg_rune_compact_error_t *model_error_out);

void SG_RuneCompactLearningConsumerDestroy(
	sg_rune_compact_learning_consumer_t *consumer);

/* Visits only recorder-accepted, terminally committed roots.  Scope authority
 * is valid only during this call, and the segment supplied to the validator is
 * the authenticated segment that recorded each event.  This is post-match
 * ingestion; there is no live-ingestion entry point.  The validator must be
 * deterministic and read-only.  Any failure leaves both priors and report_out
 * unchanged.  A successfully committed root is checkpointed by its
 * recorder-authenticated terminal identity, so rescanning the accepted
 * collection is idempotent. */
sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningConsumerIngestAcceptedV3Collection(
	sg_rune_compact_learning_consumer_t *consumer,
	const sg_level_identity_t *level_identity,
	sg_rune_compact_learning_consumer_validate_fn validate,
	void *validate_context,
	sg_rune_compact_learning_consumer_report_t *report_out);

/* Production caller boundary.  The current level identity is copied from the
 * committed SG identity owner, then the recorder-owned accepted collection is
 * visited by the function above.  This keeps map selection and trace
 * authentication in production owners rather than fixture callers. */
sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningConsumerIngestCurrentV3Collection(
	sg_rune_compact_learning_consumer_t *consumer,
	const char *expected_mapname,
	sg_rune_compact_learning_consumer_validate_fn validate,
	void *validate_context,
	sg_rune_compact_learning_consumer_report_t *report_out);

uint32_t SG_RuneCompactLearningConsumerPriorCount(
	const sg_rune_compact_learning_consumer_t *consumer);

/* Copies one accepted prior; prior_out is unchanged when index is invalid. */
int SG_RuneCompactLearningConsumerPriorRead(
	const sg_rune_compact_learning_consumer_t *consumer, uint32_t index,
	sg_rune_compact_learning_prior_t *prior_out);

const char *SG_RuneCompactLearningConsumerStatusString(
	sg_rune_compact_learning_consumer_status_t status);

#endif /* SG_RUNE_COMPACT_LEARNING_CONSUMER_H */
