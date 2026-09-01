/* Trusted level-owner interface. Never include this from RUNE or wire code. */
#ifndef SG_TACTIC_EXECUTION_OWNER_PRIVATE_H
#define SG_TACTIC_EXECUTION_OWNER_PRIVATE_H

#include "sg_tactic_execution_owner.h"
#include "sg_tactic_runtime.h"

typedef struct sg_tactic_execution_owner_s sg_tactic_execution_owner_t;

sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCreate(
	sg_tactic_execution_owner_t **owner_out,
	sg_tactic_execution_diagnostic_t *diagnostic_out);
void SG_TacticExecutionOwnerDestroy(sg_tactic_execution_owner_t *owner);

/* Prepare derives the selected private witness itself. There is no operation
 * that accepts a caller-built prepared step, action, command, or callback. */
sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerPrepare(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_execution_token_t *token_out,
	sg_tactic_execution_diagnostic_t *diagnostic_out);

/* A matching Commit is consumed before any currentness check or action. A
 * mismatched token cannot consume a different pending capability. */
sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCommit(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_execution_token_t *token,
	sg_tactic_execution_diagnostic_t *diagnostic_out);
sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCancel(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_execution_token_t *token,
	sg_tactic_execution_diagnostic_t *diagnostic_out);

/* Lifecycle owners may revoke the exact life or the complete level slot. */
void SG_TacticExecutionOwnerCancelSubject(
	sg_tactic_execution_owner_t *owner,
	const sg_localization_subject_t *subject);
void SG_TacticExecutionOwnerCancelAll(sg_tactic_execution_owner_t *owner);
void SG_TacticExecutionOwnerLost(sg_tactic_execution_owner_t *owner);
int SG_TacticExecutionOwnerCurrent(
	const sg_tactic_execution_owner_t *owner);
int SG_TacticExecutionOwnerPending(
	const sg_tactic_execution_owner_t *owner);

#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
typedef struct sg_tactic_execution_owner_test_receipt_s
{
	uint64_t attempt_sequence;
	uint64_t diagnostic_digest;
	sg_tactic_execution_diagnostic_family_t family;
	sg_tactic_execution_owner_status_t status;
	uint8_t consumed_before_currentness;
	uint8_t action_attempted;
	uint8_t reserved[6];
} sg_tactic_execution_owner_test_receipt_t;

/* This seam changes only an internal synthetic adapter epoch/outcome. It
 * cannot inject executable data or a callback. */
void SG_TacticExecutionOwnerTestConfigure(uint64_t seal_epoch,
	uint64_t current_epoch,
	sg_tactic_execution_owner_status_t action_outcome);
void SG_TacticExecutionOwnerTestSetCurrentEpoch(uint64_t current_epoch);
int SG_TacticExecutionOwnerTestLastReceipt(
	const sg_tactic_execution_owner_t *owner,
	sg_tactic_execution_owner_test_receipt_t *receipt_out);
#endif

#endif /* SG_TACTIC_EXECUTION_OWNER_PRIVATE_H */
