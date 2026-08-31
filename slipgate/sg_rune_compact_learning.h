#ifndef SG_RUNE_COMPACT_LEARNING_H
#define SG_RUNE_COMPACT_LEARNING_H

#include <stdint.h>

#include "sg_rune_compact_static.h"

#define SG_RUNE_COMPACT_LEARNING_Q16_SCALE UINT32_C(65536)

typedef struct sg_rune_compact_learning_s sg_rune_compact_learning_t;
typedef struct sg_rune_compact_learning_observation_s
	sg_rune_compact_learning_observation_t;

typedef enum sg_rune_compact_learning_kind_e
{
	SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL = 0,
	SG_RUNE_COMPACT_LEARNING_LANDING,
	SG_RUNE_COMPACT_LEARNING_TACTIC,
	SG_RUNE_COMPACT_LEARNING_STRATEGY,
	SG_RUNE_COMPACT_LEARNING_KIND_COUNT
} sg_rune_compact_learning_kind_t;

typedef struct sg_rune_compact_learning_traversal_ref_s
{
	sg_rune_compact_cell_index_t source_cell;
	sg_rune_compact_cell_index_t target_cell;
	sg_rune_compact_portal_index_t portal;
	uint32_t movement_field;
	sg_rune_stance_validity_t stance;
	uint8_t reserved[3];
} sg_rune_compact_learning_traversal_ref_t;

typedef struct sg_rune_compact_learning_tactic_ref_s
{
	sg_rune_compact_cell_index_t cell;
	uint32_t weapon_kernel;
} sg_rune_compact_learning_tactic_ref_t;

typedef struct sg_rune_compact_learning_strategy_ref_s
{
	sg_rune_compact_cell_index_t cell;
	sg_rune_compact_landmark_index_t landmark;
} sg_rune_compact_learning_strategy_ref_t;

typedef struct sg_rune_compact_learning_key_s
{
	sg_rune_compact_learning_kind_t kind;
	union
	{
		sg_rune_compact_learning_traversal_ref_t traversal;
		sg_rune_compact_learning_traversal_ref_t landing;
		sg_rune_compact_learning_tactic_ref_t tactic;
		sg_rune_compact_learning_strategy_ref_t strategy;
	} value;
} sg_rune_compact_learning_key_t;

typedef struct sg_rune_compact_learning_prior_s
{
	sg_rune_compact_learning_key_t key;
	/* Each sample rounds to the nearest 1/65536. The aggregate saturates at
	 * UINT64_MAX rather than rejecting evidence after a long-lived total. */
	uint64_t value_total_q16;
	uint64_t human_samples;
	uint64_t bot_samples;
} sg_rune_compact_learning_prior_t;

typedef enum sg_rune_compact_learning_status_e
{
	SG_RUNE_COMPACT_LEARNING_OK = 0,
	SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_LEARNING_INVALID_MODEL,
	SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED,
	SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION,
	SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE,
	SG_RUNE_COMPACT_LEARNING_INVALID_VALUE,
	SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_LEARNING_STATUS_COUNT
} sg_rune_compact_learning_status_t;

/* The service borrows an immutable compact model. expected_identity binds all
 * accepted evidence to that exact model. */
sg_rune_compact_learning_status_t SG_RuneCompactLearningCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_t **learning_out,
	sg_rune_compact_error_t *model_error_out);

void SG_RuneCompactLearningDestroy(sg_rune_compact_learning_t *learning);

/* The owner-only capture API creates observations. Consumers can pass and
 * destroy them but cannot construct a HUMAN or BOT observation. */
void SG_RuneCompactLearningObservationDestroy(
	sg_rune_compact_learning_observation_t *observation);

/* Applies one owner-issued, exact-bound observation. On failure the learning
 * state and prior_out remain unchanged. */
sg_rune_compact_learning_status_t SG_RuneCompactLearningApply(
	sg_rune_compact_learning_t *learning,
	const sg_rune_compact_learning_observation_t *observation,
	sg_rune_compact_learning_prior_t *prior_out);

/* Merges independent evidence in canonical key order. Both states must bind
 * the same current compact model identity. */
sg_rune_compact_learning_status_t SG_RuneCompactLearningMerge(
	sg_rune_compact_learning_t *target,
	const sg_rune_compact_learning_t *source);

uint32_t SG_RuneCompactLearningPriorCount(
	const sg_rune_compact_learning_t *learning);
/* Copies one record. The caller owns prior_out and it remains valid after any
 * learning update or destroy. Failure leaves prior_out unchanged. */
int SG_RuneCompactLearningPriorRead(const sg_rune_compact_learning_t *learning,
	uint32_t index, sg_rune_compact_learning_prior_t *prior_out);
const char *SG_RuneCompactLearningStatusString(
	sg_rune_compact_learning_status_t status);

#endif
