#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_authority_entropy.h"
#include "slipgate/sg_tactic_execution_owner_private.h"
#include "slipgate/sg_tactic_runtime_private.h"

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, \
			__LINE__, #expr); \
		return 0; \
	} \
} while (0)

static sg_tactic_runtime_prepared_step_t prepared_value;
static sg_tactic_runtime_status_t prepare_status = SG_TACTIC_RUNTIME_OK;
static sg_tactic_runtime_status_t consume_status = SG_TACTIC_RUNTIME_OK;
static int prepared_current = 1;
static int entropy_available = 1;
static uint8_t entropy_next = 1U;
static uint32_t prepare_calls;
static uint32_t consume_calls;
static uint32_t current_calls;
static uint32_t release_calls;
static uint32_t release_consumed_calls;
static uint32_t release_tentative_calls;
static uint32_t current_calls_at_release;

int SG_AuthorityEntropyFill(void *buffer, size_t size)
{
	uint8_t *bytes = buffer;
	size_t i;

	if (!buffer || !entropy_available)
		return 0;
	for (i = 0U; i < size; ++i)
		bytes[i] = entropy_next++;
	return 1;
}

sg_tactic_runtime_status_t SG_TacticRuntimePrepareStep(
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_runtime_prepared_step_t *prepared_out)
{
	++prepare_calls;
	if (!input || !prepared_out)
		return SG_TACTIC_RUNTIME_INVALID_ARGUMENT;
	memset(prepared_out, 0, sizeof(*prepared_out));
	if (prepare_status == SG_TACTIC_RUNTIME_OK)
		*prepared_out = prepared_value;
	return prepare_status;
}

sg_tactic_runtime_status_t SG_TacticRuntimePreparedStepConsume(
	sg_tactic_runtime_prepared_step_t *prepared)
{
	++consume_calls;
	if (!prepared)
		return SG_TACTIC_RUNTIME_INVALID_ARGUMENT;
	if (consume_status == SG_TACTIC_RUNTIME_OK)
	{
		prepared->local_context.mechanisms = NULL;
		prepared->local_context.portal_roots = NULL;
		prepared->consumed = 1U;
	}
	return consume_status;
}

int SG_TacticRuntimePreparedStepCurrent(
	const sg_tactic_runtime_prepared_step_t *prepared)
{
	++current_calls;
	return prepared && prepared_current &&
		prepared->consumed == 1U &&
		prepared->local_context.mechanisms == NULL &&
		prepared->local_context.portal_roots == NULL &&
		prepared->frame.subject.client_id ==
			prepared_value.frame.subject.client_id &&
		prepared->frame.subject.spawn_generation ==
			prepared_value.frame.subject.spawn_generation &&
		prepared->frame.frame_sequence == prepared_value.frame.frame_sequence &&
		prepared->provider.rune_identity ==
			prepared_value.provider.rune_identity &&
		prepared->provider.topology_revision ==
			prepared_value.provider.topology_revision &&
		prepared->provider.owner_epoch ==
			prepared_value.provider.owner_epoch &&
		prepared->exact_probe.provenance.kind ==
			prepared_value.exact_probe.provenance.kind;
}

int SG_TacticRuntimePreparedStepRelease(
	sg_tactic_runtime_prepared_step_t *prepared)
{
	++release_calls;
	current_calls_at_release = current_calls;
	if (!prepared)
		return 0;
	if (prepared->consumed == 1U)
		++release_consumed_calls;
	else
		++release_tentative_calls;
	memset(prepared, 0, sizeof(*prepared));
	return 1;
}

static void ResetFixture(
	sg_rune_compact_field_probe_provenance_kind_t kind)
{
	memset(&prepared_value, 0, sizeof(prepared_value));
	prepared_value.result.status = SG_TACTIC_RESULT_PROGRESS;
	prepared_value.frame.subject.client_id = 7U;
	prepared_value.frame.subject.spawn_generation = UINT64_C(91);
	prepared_value.frame.frame_sequence = UINT64_C(17);
	prepared_value.frame.observed_at_ms = UINT64_C(4250);
	prepared_value.frame.owner_epoch = UINT64_C(23);
	prepared_value.frame.token = UINT64_C(29);
	prepared_value.provider.rune_identity = UINT64_C(31);
	prepared_value.provider.topology_revision = UINT64_C(37);
	prepared_value.provider.owner_epoch = UINT64_C(41);
	prepared_value.exact_probe.provenance.kind = kind;
	prepare_status = SG_TACTIC_RUNTIME_OK;
	consume_status = SG_TACTIC_RUNTIME_OK;
	prepared_current = 1;
	entropy_available = 1;
	prepare_calls = 0U;
	consume_calls = 0U;
	current_calls = 0U;
	release_calls = 0U;
	release_consumed_calls = 0U;
	release_tentative_calls = 0U;
	current_calls_at_release = 0U;
}

static int TokenZero(const sg_tactic_execution_token_t *token)
{
	size_t i;

	for (i = 0U; i < sizeof(token->opaque); ++i)
	{
		if (token->opaque[i] != 0U)
			return 0;
	}
	return 1;
}

#ifndef SG_TACTIC_EXECUTION_OWNER_TESTING
static int TestProductionWitnessesFailClosed(void)
{
	static const sg_rune_compact_field_probe_provenance_kind_t kinds[] = {
		SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE,
		SG_RUNE_COMPACT_FIELD_PROBE_PMOVE,
		SG_RUNE_COMPACT_FIELD_PROBE_HOOK,
		SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION,
		SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER
	};
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_diagnostic_t diagnostic;
	sg_tactic_runtime_step_input_t input;
	size_t i;

	memset(&input, 0, sizeof(input));
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCurrent(owner));
	for (i = 0U; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
	{
		ResetFixture(kinds[i]);
		memset(&token, 0xa5, sizeof(token));
		CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &token,
			&diagnostic) == SG_TACTIC_EXECUTION_OWNER_WITNESS_INCOMPLETE);
		CHECK(TokenZero(&token));
		CHECK(!SG_TacticExecutionOwnerPending(owner));
		CHECK(diagnostic.reason == SG_TACTIC_EXECUTION_REASON_WITNESS);
		CHECK(consume_calls == 0U);
		CHECK(release_calls == 1U);
		CHECK(release_tentative_calls == 1U);
		CHECK(release_consumed_calls == 0U);
	}
	SG_TacticExecutionOwnerDestroy(owner);
	return 1;
}
#else
static int SubjectEqual(const sg_localization_subject_t *left,
	const sg_localization_subject_t *right)
{
	return left->client_id == right->client_id &&
		left->spawn_generation == right->spawn_generation;
}

static int PrepareOne(sg_tactic_execution_owner_t *owner,
	sg_tactic_execution_token_t *token_out,
	sg_tactic_execution_diagnostic_t *diagnostic_out)
{
	sg_tactic_runtime_step_input_t input;

	memset(&input, 0, sizeof(input));
	return SG_TacticExecutionOwnerPrepare(owner, &input, token_out,
		diagnostic_out) == SG_TACTIC_EXECUTION_OWNER_OK;
}

static int TestOneSlotAndReplay(void)
{
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_token_t second;
	sg_tactic_execution_token_t hostile;
	sg_tactic_execution_diagnostic_t diagnostic;
	sg_tactic_execution_owner_test_receipt_t receipt;
	sg_tactic_runtime_step_input_t input;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_PMOVE);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(100), UINT64_C(100),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	CHECK(!TokenZero(&token));
	CHECK(SG_TacticExecutionOwnerPending(owner));
	memset(&input, 0, sizeof(input));
	memset(&second, 0xa5, sizeof(second));
	CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &second,
		&diagnostic) == SG_TACTIC_EXECUTION_OWNER_BUSY);
	CHECK(TokenZero(&second));
	CHECK(prepare_calls == 1U);
	CHECK(consume_calls == 1U);

	hostile = token;
	hostile.opaque[3] ^= UINT8_C(0x80);
	CHECK(SG_TacticExecutionOwnerCommit(owner, &hostile, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_TOKEN_REJECTED);
	CHECK(SG_TacticExecutionOwnerPending(owner));
	CHECK(current_calls == 0U);
	CHECK(release_calls == 0U);
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.consumed_before_currentness == 1U);
	CHECK(receipt.action_attempted == 1U);
	CHECK(receipt.status == SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(release_calls == 1U);
	CHECK(release_consumed_calls == 1U);
	CHECK(current_calls_at_release == 1U);
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_EMPTY);
	CHECK(release_calls == 1U);
	SG_TacticExecutionOwnerDestroy(owner);
	return 1;
}

static int TestMatchingFailuresConsume(void)
{
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_diagnostic_t diagnostic;
	sg_tactic_execution_owner_test_receipt_t receipt;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_HOOK);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(200), UINT64_C(200),
		SG_TACTIC_EXECUTION_OWNER_ACTION_REJECTED);
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_ACTION_REJECTED);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.consumed_before_currentness == 1U);
	CHECK(receipt.action_attempted == 1U);
	CHECK(release_calls == 1U);
	CHECK(current_calls_at_release == 1U);

	SG_TacticExecutionOwnerTestConfigure(UINT64_C(300), UINT64_C(300),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	prepared_value.frame.frame_sequence++;
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.consumed_before_currentness == 1U);
	CHECK(receipt.action_attempted == 0U);
	CHECK(release_calls == 2U);
	CHECK(current_calls_at_release == 2U);

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_HOOK);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(350), UINT64_C(350),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	prepared_value.frame.subject.spawn_generation++;
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.action_attempted == 0U);
	CHECK(release_calls == 1U);
	CHECK(current_calls_at_release == 1U);

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_HOOK);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(375), UINT64_C(375),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	prepared_value.provider.topology_revision++;
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.action_attempted == 0U);
	CHECK(release_calls == 1U);
	CHECK(current_calls_at_release == 1U);

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_HOOK);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(400), UINT64_C(400),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	SG_TacticExecutionOwnerTestSetCurrentEpoch(UINT64_C(401));
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.action_attempted == 0U);
	CHECK(release_calls == 1U);
	CHECK(current_calls_at_release == 1U);
	SG_TacticExecutionOwnerDestroy(owner);
	return 1;
}

static int TestCancelAndLifeFences(void)
{
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_token_t hostile;
	sg_tactic_execution_diagnostic_t diagnostic;
	sg_localization_subject_t subject;
	sg_localization_subject_t other;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(500), UINT64_C(500),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	hostile = token;
	hostile.opaque[0] ^= UINT8_C(1);
	CHECK(SG_TacticExecutionOwnerCancel(owner, &hostile, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_TOKEN_REJECTED);
	CHECK(SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 0U);
	CHECK(SG_TacticExecutionOwnerCancel(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 1U);

	CHECK(PrepareOne(owner, &token, &diagnostic));
	subject = prepared_value.frame.subject;
	other = subject;
	other.spawn_generation++;
	CHECK(!SubjectEqual(&subject, &other));
	SG_TacticExecutionOwnerCancelSubject(owner, &other);
	CHECK(SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 1U);
	SG_TacticExecutionOwnerCancelSubject(owner, &subject);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 2U);
	/* Duplicate host edges are idempotent after the matching life consumed the
	 * only pending slot. */
	SG_TacticExecutionOwnerCancelSubject(owner, &subject);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 2U);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	SG_TacticExecutionOwnerCancelAll(owner);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 3U);
	SG_TacticExecutionOwnerDestroy(owner);
	return 1;
}

static int TestRetainedBorrowStale(void)
{
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_diagnostic_t diagnostic;
	sg_tactic_execution_owner_test_receipt_t receipt;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(525), UINT64_C(525),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	prepared_current = 0;
	CHECK(SG_TacticExecutionOwnerCommit(owner, &token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(SG_TacticExecutionOwnerTestLastReceipt(owner, &receipt));
	CHECK(receipt.consumed_before_currentness == 1U);
	CHECK(receipt.action_attempted == 0U);
	CHECK(current_calls == 1U);
	CHECK(release_calls == 1U);
	CHECK(current_calls_at_release == 1U);
	SG_TacticExecutionOwnerDestroy(owner);
	return 1;
}

static int TestCrossOwnerTokenRejected(void)
{
	sg_tactic_execution_owner_t *left = NULL;
	sg_tactic_execution_owner_t *right = NULL;
	sg_tactic_execution_token_t left_token;
	sg_tactic_execution_token_t right_token;
	sg_tactic_execution_diagnostic_t diagnostic;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(550), UINT64_C(550),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&left, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&right, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(left, &left_token, &diagnostic));
	CHECK(PrepareOne(right, &right_token, &diagnostic));
	CHECK(SG_TacticExecutionOwnerCommit(right, &left_token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_TOKEN_REJECTED);
	CHECK(SG_TacticExecutionOwnerPending(right));
	CHECK(release_calls == 0U);
	CHECK(SG_TacticExecutionOwnerCancel(left, &left_token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCancel(right, &right_token, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(release_calls == 2U);
	SG_TacticExecutionOwnerDestroy(left);
	SG_TacticExecutionOwnerDestroy(right);
	return 1;
}

static int TestPreparationFailuresAndOwnerLoss(void)
{
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_diagnostic_t diagnostic;
	sg_tactic_runtime_step_input_t input;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(600), UINT64_C(600),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	memset(&input, 0, sizeof(input));
	prepare_status = SG_TACTIC_RUNTIME_NOT_CURRENT;
	CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &token,
		&diagnostic) == SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED);
	CHECK(TokenZero(&token));
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	prepare_status = SG_TACTIC_RUNTIME_OK;
	prepared_value.result.status = SG_TACTIC_RESULT_HOLD;
	CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &token,
		&diagnostic) == SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED);
	CHECK(TokenZero(&token));
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 1U);
	CHECK(release_tentative_calls == 1U);
	prepared_value.result.status = SG_TACTIC_RESULT_PROGRESS;
	consume_status = SG_TACTIC_RUNTIME_NOT_CURRENT;
	CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &token,
		&diagnostic) == SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED);
	CHECK(TokenZero(&token));
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 2U);
	CHECK(release_tentative_calls == 2U);
	consume_status = SG_TACTIC_RUNTIME_OK;
	entropy_available = 0;
	CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &token,
		&diagnostic) == SG_TACTIC_EXECUTION_OWNER_ENTROPY_REJECTED);
	CHECK(TokenZero(&token));
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 3U);
	CHECK(release_consumed_calls == 1U);
	entropy_available = 1;
	CHECK(PrepareOne(owner, &token, &diagnostic));
	SG_TacticExecutionOwnerLost(owner);
	CHECK(!SG_TacticExecutionOwnerPending(owner));
	CHECK(release_calls == 4U);
	CHECK(!SG_TacticExecutionOwnerCurrent(owner));
	CHECK(SG_TacticExecutionOwnerPrepare(owner, &input, &token,
		&diagnostic) == SG_TACTIC_EXECUTION_OWNER_LOST);
	SG_TacticExecutionOwnerDestroy(owner);
	return 1;
}

static int TestDestroyReleasesPending(void)
{
	sg_tactic_execution_owner_t *owner = NULL;
	sg_tactic_execution_token_t token;
	sg_tactic_execution_diagnostic_t diagnostic;

	ResetFixture(SG_RUNE_COMPACT_FIELD_PROBE_PMOVE);
	SG_TacticExecutionOwnerTestConfigure(UINT64_C(700), UINT64_C(700),
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(SG_TacticExecutionOwnerCreate(&owner, &diagnostic) ==
		SG_TACTIC_EXECUTION_OWNER_OK);
	CHECK(PrepareOne(owner, &token, &diagnostic));
	CHECK(release_calls == 0U);
	SG_TacticExecutionOwnerDestroy(owner);
	CHECK(release_calls == 1U);
	CHECK(release_consumed_calls == 1U);
	return 1;
}
#endif

int main(void)
{
#ifndef SG_TACTIC_EXECUTION_OWNER_TESTING
	CHECK(TestProductionWitnessesFailClosed());
#else
	CHECK(TestOneSlotAndReplay());
	CHECK(TestMatchingFailuresConsume());
	CHECK(TestCancelAndLifeFences());
	CHECK(TestRetainedBorrowStale());
	CHECK(TestCrossOwnerTokenRejected());
	CHECK(TestPreparationFailuresAndOwnerLoss());
	CHECK(TestDestroyReleasesPending());
#endif
	return 0;
}
