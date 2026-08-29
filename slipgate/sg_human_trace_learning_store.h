/* Durable model state and incorporated-scope ownership. */
#ifndef SG_HUMAN_TRACE_LEARNING_STORE_H
#define SG_HUMAN_TRACE_LEARNING_STORE_H

#include "sg_human_trace_learning.h"

#define SG_HUMAN_TRACE_LEARNING_STATE_PATH_BYTES 1024U

typedef struct sg_human_trace_learning_scope_receipt_s
{
	sg_human_trace_learning_trace_id_t terminal_sha256;
	uint32_t client_id;
	uint32_t reserved;
	uint64_t spawn_generation;
	uint64_t state_generation;
} sg_human_trace_learning_scope_receipt_t;

typedef struct sg_human_trace_learning_store_s
{
	char path[SG_HUMAN_TRACE_LEARNING_STATE_PATH_BYTES];
	sg_human_trace_learning_identity_t identity;
	uint64_t generation;
	uint64_t next_transaction_id;
	uint64_t *effective_cost_us;
	size_t effective_cost_count;
	sg_human_trace_learning_scope_receipt_t *receipts;
	size_t receipt_count;
	size_t receipt_capacity;
	size_t *receipt_slots;
	size_t receipt_slot_capacity;
	uint64_t temporary_nonce;
	uint8_t initialized;
} sg_human_trace_learning_store_t;

SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningStoreOpen(
	sg_human_trace_learning_store_t *store, const char *directory,
	const sg_level_identity_t *level_identity,
	const sg_human_trace_learning_parameters_t *parameters,
	uint64_t next_transaction_id);
SG_HUMAN_TRACE_LEARNING_LOCAL void SG_HumanTraceLearningStoreClose(
	sg_human_trace_learning_store_t *store);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningStoreRestore(
	const sg_human_trace_learning_store_t *store,
	sg_human_trace_learning_parameters_t *parameters,
	uint64_t *next_transaction_id_out);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningStoreScopeConsumed(
	const sg_human_trace_learning_store_t *store,
	const sg_human_trace_learning_trace_scope_t *scope);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningStoreCommitScope(
	sg_human_trace_learning_store_t *store,
	const sg_human_trace_learning_parameters_t *candidate,
	uint64_t next_transaction_id, uint64_t committed_transaction_count,
	const sg_human_trace_learning_trace_scope_t *scope);

#endif /* SG_HUMAN_TRACE_LEARNING_STORE_H */
