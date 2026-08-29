/* Test-only raw-batch regression seam; production application stays host-local. */
#include "sg_human_trace_learning_game_private.h"

#include <string.h>

#ifdef SG_HUMAN_TRACE_LEARNING_TEST
#include "sg_human_trace_learning_game_test.h"

static int LearningRuntimeValid(const sg_human_trace_learning_runtime_t *runtime)
{
	return runtime && SG_HumanTraceLearningParametersValid(runtime->parameters) &&
		SG_HumanTraceLearningWorkspaceValid(runtime->parameters, &runtime->workspace) &&
		runtime->playthroughs && runtime->playthrough_capacity != 0U &&
		runtime->next_transaction_id != 0U;
}

static int LearningPlaythroughSame(
	const sg_human_trace_learning_playthrough_t *playthrough,
	const sg_human_trace_learning_trace_scope_t *scope)
{
	return playthrough->used == 1U &&
		memcmp(playthrough->terminal_sha256.bytes,
			scope->trace.terminal_sha256.bytes,
			sizeof(playthrough->terminal_sha256.bytes)) == 0 &&
		playthrough->client_id == scope->client_id &&
		playthrough->spawn_generation == scope->spawn_generation;
}

typedef enum learning_playthrough_slot_kind_e
{
	LEARNING_PLAYTHROUGH_SLOT_NONE = 0,
	LEARNING_PLAYTHROUGH_SLOT_EXISTING,
	LEARNING_PLAYTHROUGH_SLOT_VACANT
} learning_playthrough_slot_kind_t;

static int LearningPlaythroughSlot(const sg_human_trace_learning_runtime_t *runtime,
	const sg_human_trace_learning_trace_scope_t *scope, uint32_t *slot_out)
{
	uint32_t index;
	uint32_t vacant = runtime->playthrough_capacity;

	for (index = 0U; index < runtime->playthrough_capacity; index++)
	{
		const sg_human_trace_learning_playthrough_t *playthrough =
			&runtime->playthroughs[index];

		if (LearningPlaythroughSame(playthrough, scope))
		{
			*slot_out = index;
			return LEARNING_PLAYTHROUGH_SLOT_EXISTING;
		}
		if (playthrough->used == 0U && vacant == runtime->playthrough_capacity)
			vacant = index;
	}
	if (vacant == runtime->playthrough_capacity)
		return LEARNING_PLAYTHROUGH_SLOT_NONE;
	*slot_out = vacant;
	return LEARNING_PLAYTHROUGH_SLOT_VACANT;
}

static int LearningCursorAccepts(
	const sg_human_trace_learning_playthrough_t *playthrough,
	const sg_human_trace_learning_record_t *first_record)
{
	return first_record->first_order > playthrough->last_order &&
		first_record->first_frame >= playthrough->last_frame;
}

int SG_HumanTraceLearningTestRuntimeInit(sg_human_trace_learning_runtime_t *runtime,
	sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_workspace_t *workspace,
	sg_human_trace_learning_playthrough_t *playthroughs,
	uint32_t playthrough_capacity, uint64_t first_transaction_id)
{
	if (!runtime || !SG_HumanTraceLearningParametersValid(parameters) || !workspace ||
		!SG_HumanTraceLearningWorkspaceValid(parameters, workspace) || !playthroughs ||
		playthrough_capacity == 0U || first_transaction_id == 0U)
		return 0;
#if SIZE_MAX <= UINT32_MAX
	if (playthrough_capacity > (uint32_t)(SIZE_MAX / sizeof(*playthroughs)))
		return 0;
#endif
	memset(playthroughs, 0,
		(size_t)playthrough_capacity * sizeof(*playthroughs));
	memset(runtime, 0, sizeof(*runtime));
	runtime->parameters = parameters;
	runtime->workspace = *workspace;
	runtime->playthroughs = playthroughs;
	runtime->playthrough_capacity = playthrough_capacity;
	runtime->next_transaction_id = first_transaction_id;
	return 1;
}

sg_human_trace_learning_apply_status_t SG_HumanTraceLearningTestApplyBatch(
	sg_human_trace_learning_runtime_t *runtime,
	const sg_human_trace_learning_batch_t *batch,
	const sg_human_trace_learning_trace_v3_auth_t *authenticated_trace)
{
	sg_human_trace_learning_parameters_t candidate;
	sg_human_trace_learning_playthrough_t cursor;
	uint64_t next_transaction_id;
	uint32_t slot;
	uint32_t index;
	int slot_kind;

	if (!LearningRuntimeValid(runtime) || !SG_HumanTraceLearningBatchValid(batch) ||
		!authenticated_trace || !SG_HumanTraceLearningTraceV3AuthEqual(
			&batch->trace.trace, authenticated_trace))
		return SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED;
	slot_kind = LearningPlaythroughSlot(runtime, &batch->trace, &slot);
	if (slot_kind == LEARNING_PLAYTHROUGH_SLOT_NONE ||
		(slot_kind == LEARNING_PLAYTHROUGH_SLOT_EXISTING && !LearningCursorAccepts(
		&runtime->playthroughs[slot], &batch->records[0])))
		return SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED;
	if (!SG_HumanTraceLearningParametersClone(runtime->parameters, &runtime->workspace,
		&candidate))
		return SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED;
	next_transaction_id = runtime->next_transaction_id;
	for (index = 0U; index < batch->record_count; index++)
	{
		const sg_human_trace_learning_record_t *record = &batch->records[index];
		sg_human_trace_learning_transaction_t transaction;

		if (next_transaction_id == UINT64_MAX ||
			!SG_HumanTraceLearningTransactionBegin(&candidate, &record->update,
				next_transaction_id, &transaction) ||
			!SG_HumanTraceLearningApplyUpdate(&candidate, &record->update,
				&transaction) || !SG_HumanTraceLearningTransactionCommit(&candidate,
				&transaction))
			return SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED;
		next_transaction_id++;
	}
	cursor = runtime->playthroughs[slot];
	cursor.terminal_sha256 = batch->trace.trace.terminal_sha256;
	cursor.client_id = batch->trace.client_id;
	cursor.spawn_generation = batch->trace.spawn_generation;
	cursor.last_frame = batch->records[batch->record_count - 1U].last_frame;
	cursor.last_order = batch->records[batch->record_count - 1U].last_order;
	cursor.used = 1U;
	if (!SG_HumanTraceLearningParametersReplace(runtime->parameters, &candidate))
		return SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED;
	runtime->playthroughs[slot] = cursor;
	runtime->next_transaction_id = next_transaction_id;
	return SG_HUMAN_TRACE_LEARNING_APPLY_COMMITTED;
}

#else

typedef int sg_human_trace_learning_game_production_translation_unit_t;

#endif /* SG_HUMAN_TRACE_LEARNING_TEST */
