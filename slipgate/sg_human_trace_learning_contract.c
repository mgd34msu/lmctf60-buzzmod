/* Transactional, typed human-trace cost parameters. */
#include "sg_human_trace_learning_contract.h"

#include <limits.h>
#include <string.h>

static int LearningRangeBytes(const void *address, size_t count,
	size_t element_size, uintptr_t *first_out, uintptr_t *last_out)
{
	uintptr_t first;
	size_t bytes;

	if (!first_out || !last_out || (count != 0U && !address) ||
		(element_size != 0U && count > SIZE_MAX / element_size))
		return 0;
	bytes = count * element_size;
	first = (uintptr_t)address;
	if (bytes > UINTPTR_MAX - first)
		return 0;
	*first_out = first;
	*last_out = first + bytes;
	return 1;
}

static int LearningRangesOverlap(const void *left, size_t left_count,
	size_t left_size, const void *right, size_t right_count, size_t right_size)
{
	uintptr_t left_first;
	uintptr_t left_last;
	uintptr_t right_first;
	uintptr_t right_last;

	if (left_count == 0U || right_count == 0U)
		return 0;
	if (!LearningRangeBytes(left, left_count, left_size, &left_first,
		&left_last) || !LearningRangeBytes(right, right_count, right_size,
		&right_first, &right_last))
		return 1;
	return left_first < right_last && right_first < left_last;
}

static int LearningTraceIdEqual(const sg_human_trace_learning_trace_id_t *left,
	const sg_human_trace_learning_trace_id_t *right)
{
	return SG_HumanTraceLearningTraceIdValid(left) && SG_HumanTraceLearningTraceIdValid(right) &&
		memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static int LearningKeyEqual(const sg_human_trace_learning_kernel_key_t *left,
	const sg_human_trace_learning_kernel_key_t *right)
{
	return SG_HumanTraceLearningKernelKeyValid(left) && SG_HumanTraceLearningKernelKeyValid(right) &&
		SG_RuneModelStableIdEqual(&left->control.value, &right->control.value) &&
		SG_RuneModelStableIdEqual(&left->kernel.value, &right->kernel.value);
}

static int LearningKernelInSnapshot(const sg_rune_runtime_snapshot_t *snapshot,
	sg_rune_kernel_ref_t kernel)
{
	uint32_t index;

	if (!snapshot || !snapshot->model ||
		!SG_RuneModelStableIdValid(&kernel.value))
		return 0;
	for (index = 0U; index < snapshot->model->kernel_count; index++)
		if (SG_RuneModelStableIdEqual(&snapshot->model->kernels[index].id.value,
			&kernel.value))
			return 1;
	return 0;
}

static int LearningDomainValid(const sg_human_trace_learning_domain_t *domain)
{
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_rune_model_t *model;
	size_t left;

	if (!domain || !SG_HumanTraceLearningIdentityValid(&domain->identity) ||
		!(snapshot = domain->snapshot) || !SG_RuneRuntimeSnapshotValid(snapshot) ||
		!(model = snapshot->model) ||
		domain->identity.rune_identity != snapshot->identity ||
		domain->identity.topology_revision != snapshot->topology_revision ||
		domain->identity.bsp_identity != model->identity.bsp_content_id ||
		domain->identity.physics_identity != model->identity.physics_abi_id ||
		domain->kernel_key_count != (size_t)model->kernel_count ||
		(domain->kernel_key_count != 0U && !domain->kernel_keys))
		return 0;
	for (left = 0U; left < domain->kernel_key_count; left++)
	{
		size_t right;

		if (!SG_HumanTraceLearningKernelKeyValid(&domain->kernel_keys[left]) ||
			!LearningKernelInSnapshot(snapshot, domain->kernel_keys[left].kernel))
			return 0;
		for (right = 0U; right < left; right++)
			if (SG_RuneModelStableIdEqual(
				&domain->kernel_keys[left].kernel.value,
				&domain->kernel_keys[right].kernel.value))
				return 0;
	}
	return 1;
}

static int LearningStorageValid(const sg_human_trace_learning_domain_t *domain,
	const sg_human_trace_learning_storage_t *storage)
{
	if (!LearningDomainValid(domain) || !storage ||
		storage->effective_cost_capacity < domain->kernel_key_count ||
		(domain->kernel_key_count != 0U && !storage->effective_cost_us) ||
		LearningRangesOverlap(storage->effective_cost_us,
			domain->kernel_key_count, sizeof(*storage->effective_cost_us),
			domain->kernel_keys, domain->kernel_key_count,
			sizeof(*domain->kernel_keys)) ||
		LearningRangesOverlap(storage->effective_cost_us,
			domain->kernel_key_count, sizeof(*storage->effective_cost_us),
			domain->snapshot, 1U, sizeof(*domain->snapshot)) ||
		LearningRangesOverlap(storage->effective_cost_us,
			domain->kernel_key_count, sizeof(*storage->effective_cost_us),
			domain->snapshot->model, 1U, sizeof(*domain->snapshot->model)))
		return 0;
	return 1;
}

static int LearningParametersContentsValid(
	const sg_human_trace_learning_parameters_t *parameters)
{
	size_t index;

	if (!parameters || !LearningDomainValid(&parameters->domain) ||
		parameters->generation == 0U ||
		parameters->effective_cost_capacity < parameters->domain.kernel_key_count ||
		(parameters->domain.kernel_key_count != 0U &&
		 !parameters->effective_cost_us) ||
		LearningRangesOverlap(parameters, 1U, sizeof(*parameters),
			parameters->effective_cost_us, parameters->domain.kernel_key_count,
			sizeof(*parameters->effective_cost_us)) ||
		LearningRangesOverlap(parameters->effective_cost_us,
			parameters->domain.kernel_key_count,
			sizeof(*parameters->effective_cost_us), parameters->domain.kernel_keys,
			parameters->domain.kernel_key_count,
			sizeof(*parameters->domain.kernel_keys)))
		return 0;
	for (index = 0U; index < parameters->domain.kernel_key_count; index++)
		if (parameters->effective_cost_us[index] != 0U &&
			!SG_HumanTraceLearningEffectiveCostValid(parameters->effective_cost_us[index]))
			return 0;
	return 1;
}

static int LearningKeyIndex(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_kernel_key_t *key, size_t *index_out)
{
	size_t index;

	if (index_out)
		*index_out = SIZE_MAX;
	if (!parameters || !key || !index_out || !SG_HumanTraceLearningKernelKeyValid(key))
		return 0;
	for (index = 0U; index < parameters->domain.kernel_key_count; index++)
		if (LearningKeyEqual(&parameters->domain.kernel_keys[index], key))
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

int SG_HumanTraceLearningParametersInit(sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_domain_t *domain, const sg_human_trace_learning_storage_t *storage)
{
	if (!parameters || !LearningStorageValid(domain, storage))
		return 0;
	memset(parameters, 0, sizeof(*parameters));
	parameters->domain = *domain;
	parameters->generation = 1U;
	parameters->effective_cost_us = storage->effective_cost_us;
	parameters->effective_cost_capacity = storage->effective_cost_capacity;
	if (domain->kernel_key_count != 0U)
		memset(parameters->effective_cost_us, 0,
			domain->kernel_key_count * sizeof(*parameters->effective_cost_us));
	return LearningParametersContentsValid(parameters);
}

int SG_HumanTraceLearningParametersValid(const sg_human_trace_learning_parameters_t *parameters)
{
	return LearningParametersContentsValid(parameters);
}

int SG_HumanTraceLearningWorkspaceValid(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_workspace_t *workspace)
{
	if (!LearningParametersContentsValid(parameters) || !workspace ||
		workspace->effective_cost_capacity < parameters->domain.kernel_key_count ||
		(parameters->domain.kernel_key_count != 0U &&
		 !workspace->effective_cost_us) ||
		LearningRangesOverlap(workspace->effective_cost_us,
			parameters->domain.kernel_key_count,
			sizeof(*workspace->effective_cost_us), parameters->effective_cost_us,
			parameters->domain.kernel_key_count,
			sizeof(*parameters->effective_cost_us)) ||
		LearningRangesOverlap(workspace->effective_cost_us,
			parameters->domain.kernel_key_count,
			sizeof(*workspace->effective_cost_us), parameters->domain.kernel_keys,
			parameters->domain.kernel_key_count,
			sizeof(*parameters->domain.kernel_keys)))
		return 0;
	return 1;
}

int SG_HumanTraceLearningParametersClone(const sg_human_trace_learning_parameters_t *source,
	const sg_human_trace_learning_workspace_t *workspace, sg_human_trace_learning_parameters_t *out)
{
	if (!source || !out || out == source ||
		!LearningParametersContentsValid(source) ||
		!SG_HumanTraceLearningWorkspaceValid(source, workspace))
		return 0;
	memset(out, 0, sizeof(*out));
	out->domain = source->domain;
	out->generation = source->generation;
	out->effective_cost_us = workspace->effective_cost_us;
	out->effective_cost_capacity = workspace->effective_cost_capacity;
	if (source->domain.kernel_key_count != 0U)
		memcpy(out->effective_cost_us, source->effective_cost_us,
			source->domain.kernel_key_count * sizeof(*out->effective_cost_us));
	return LearningParametersContentsValid(out);
}

int SG_HumanTraceLearningParametersSameIdentity(const sg_human_trace_learning_parameters_t *left,
	const sg_human_trace_learning_parameters_t *right)
{
	return LearningParametersContentsValid(left) &&
		LearningParametersContentsValid(right) &&
		SG_HumanTraceLearningIdentityEqual(&left->domain.identity, &right->domain.identity) &&
		left->domain.snapshot == right->domain.snapshot &&
		left->domain.kernel_keys == right->domain.kernel_keys &&
		left->domain.kernel_key_count == right->domain.kernel_key_count;
}

int SG_HumanTraceLearningParametersReplace(sg_human_trace_learning_parameters_t *destination,
	const sg_human_trace_learning_parameters_t *source)
{
	if (!destination || !source || destination == source ||
		!SG_HumanTraceLearningParametersSameIdentity(destination, source) ||
		destination->effective_cost_capacity <
			destination->domain.kernel_key_count)
		return 0;
	if (destination->domain.kernel_key_count != 0U)
		memcpy(destination->effective_cost_us, source->effective_cost_us,
			destination->domain.kernel_key_count *
			sizeof(*destination->effective_cost_us));
	destination->generation = source->generation;
	return LearningParametersContentsValid(destination);
}

int SG_HumanTraceLearningEvidenceMatches(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_evidence_t *evidence)
{
	return LearningParametersContentsValid(parameters) &&
		SG_HumanTraceLearningEvidenceValid(evidence) &&
		SG_HumanTraceLearningIdentityEqual(&parameters->domain.identity,
			&evidence->identity);
}

int SG_HumanTraceLearningUpdateTargetsParameters(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_update_t *update)
{
	size_t index;

	return LearningParametersContentsValid(parameters) &&
		SG_HumanTraceLearningUpdateValid(update) &&
		SG_HumanTraceLearningEvidenceMatches(parameters, &update->evidence) &&
		!SG_HumanTraceLearningUpdateTouchesGeometry(update) &&
		LearningKeyIndex(parameters, &update->key, &index);
}

int SG_HumanTraceLearningUpdateSame(const sg_human_trace_learning_update_t *left,
	const sg_human_trace_learning_update_t *right)
{
	return SG_HumanTraceLearningUpdateValid(left) && SG_HumanTraceLearningUpdateValid(right) &&
		left->kind == right->kind &&
		left->evidence.evidence_id == right->evidence.evidence_id &&
		SG_HumanTraceLearningIdentityEqual(&left->evidence.identity,
			&right->evidence.identity) &&
		LearningTraceIdEqual(&left->evidence.trace, &right->evidence.trace) &&
		left->evidence.captured_at_ms == right->evidence.captured_at_ms &&
		LearningKeyEqual(&left->key, &right->key) &&
		left->effective_cost_us == right->effective_cost_us;
}

int SG_HumanTraceLearningTransactionValid(const sg_human_trace_learning_transaction_t *transaction)
{
	if (!transaction || transaction->transaction_id == 0U ||
		transaction->evidence_id == 0U ||
		transaction->state < SG_HUMAN_TRACE_LEARNING_TRANSACTION_PREPARED ||
		transaction->state > SG_HUMAN_TRACE_LEARNING_TRANSACTION_ROLLED_BACK ||
		!SG_HumanTraceLearningUpdateValid(&transaction->authorized_update) ||
		transaction->evidence_id !=
			transaction->authorized_update.evidence.evidence_id)
		return 0;
	if (transaction->state == SG_HUMAN_TRACE_LEARNING_TRANSACTION_PREPARED)
		return transaction->applied_generation == 0U;
	return transaction->expected_generation != UINT64_MAX &&
		transaction->applied_generation == transaction->expected_generation + 1U;
}

int SG_HumanTraceLearningTransactionBoundToParameters(
	const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_transaction_t *transaction)
{
	if (!LearningParametersContentsValid(parameters) ||
		!SG_HumanTraceLearningTransactionValid(transaction) ||
		!SG_HumanTraceLearningUpdateTargetsParameters(parameters,
			&transaction->authorized_update))
		return 0;
	if (transaction->state == SG_HUMAN_TRACE_LEARNING_TRANSACTION_PREPARED ||
		transaction->state == SG_HUMAN_TRACE_LEARNING_TRANSACTION_ROLLED_BACK)
		return parameters->generation == transaction->expected_generation;
	return parameters->generation == transaction->applied_generation;
}

int SG_HumanTraceLearningTransactionMayCommit(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_transaction_t *transaction)
{
	return transaction && transaction->state == SG_HUMAN_TRACE_LEARNING_TRANSACTION_APPLIED &&
		SG_HumanTraceLearningTransactionBoundToParameters(parameters, transaction);
}

int SG_HumanTraceLearningTransactionMayRollback(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_transaction_t *transaction)
{
	return transaction && transaction->state == SG_HUMAN_TRACE_LEARNING_TRANSACTION_APPLIED &&
		SG_HumanTraceLearningTransactionBoundToParameters(parameters, transaction);
}

int SG_HumanTraceLearningTransactionBegin(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_update_t *update, uint64_t transaction_id,
	sg_human_trace_learning_transaction_t *transaction)
{
	size_t index;

	if (!LearningParametersContentsValid(parameters) || !update ||
		transaction_id == 0U || !transaction ||
		!SG_HumanTraceLearningUpdateTargetsParameters(parameters, update) ||
		!LearningKeyIndex(parameters, &update->key, &index))
		return 0;
	memset(transaction, 0, sizeof(*transaction));
	transaction->transaction_id = transaction_id;
	transaction->expected_generation = parameters->generation;
	transaction->evidence_id = update->evidence.evidence_id;
	transaction->state = SG_HUMAN_TRACE_LEARNING_TRANSACTION_PREPARED;
	transaction->authorized_update = *update;
	transaction->before_effective_cost_us = parameters->effective_cost_us[index];
	return SG_HumanTraceLearningTransactionValid(transaction);
}

int SG_HumanTraceLearningApplyUpdate(sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_update_t *update, sg_human_trace_learning_transaction_t *transaction)
{
	size_t index;

	if (!LearningParametersContentsValid(parameters) || !update || !transaction ||
		transaction->state != SG_HUMAN_TRACE_LEARNING_TRANSACTION_PREPARED ||
		!SG_HumanTraceLearningTransactionBoundToParameters(parameters, transaction) ||
		!SG_HumanTraceLearningUpdateSame(update, &transaction->authorized_update) ||
		parameters->generation == UINT64_MAX ||
		!LearningKeyIndex(parameters, &update->key, &index))
		return 0;
	parameters->effective_cost_us[index] = update->effective_cost_us;
	parameters->generation++;
	if (!LearningParametersContentsValid(parameters))
	{
		parameters->effective_cost_us[index] = transaction->before_effective_cost_us;
		parameters->generation--;
		return 0;
	}
	transaction->applied_generation = parameters->generation;
	transaction->state = SG_HUMAN_TRACE_LEARNING_TRANSACTION_APPLIED;
	return SG_HumanTraceLearningTransactionValid(transaction);
}

int SG_HumanTraceLearningTransactionCommit(const sg_human_trace_learning_parameters_t *parameters,
	sg_human_trace_learning_transaction_t *transaction)
{
	if (!LearningParametersContentsValid(parameters) || !transaction ||
		!SG_HumanTraceLearningTransactionMayCommit(parameters, transaction))
		return 0;
	transaction->state = SG_HUMAN_TRACE_LEARNING_TRANSACTION_COMMITTED;
	return SG_HumanTraceLearningTransactionValid(transaction);
}

int SG_HumanTraceLearningTransactionRollback(sg_human_trace_learning_parameters_t *parameters,
	sg_human_trace_learning_transaction_t *transaction)
{
	size_t index;

	if (!LearningParametersContentsValid(parameters) || !transaction ||
		!SG_HumanTraceLearningTransactionMayRollback(parameters, transaction) ||
		!LearningKeyIndex(parameters, &transaction->authorized_update.key, &index))
		return 0;
	parameters->effective_cost_us[index] = transaction->before_effective_cost_us;
	parameters->generation = transaction->expected_generation;
	transaction->state = SG_HUMAN_TRACE_LEARNING_TRANSACTION_ROLLED_BACK;
	return SG_HumanTraceLearningTransactionValid(transaction) &&
		LearningParametersContentsValid(parameters);
}

int SG_HumanTraceLearningEffectiveKernelCost(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_kernel_key_t *key, uint64_t static_cost_us,
	uint64_t *effective_cost_us_out)
{
	size_t index;

	if (!effective_cost_us_out || !SG_HumanTraceLearningKernelKeyValid(key) ||
		!SG_HumanTraceLearningEffectiveCostValid(static_cost_us))
		return 0;
	*effective_cost_us_out = static_cost_us;
	if (!parameters)
		return 1;
	if (!LearningParametersContentsValid(parameters) ||
		!LearningKeyIndex(parameters, key, &index))
		return 0;
	if (parameters->effective_cost_us[index] != 0U)
		*effective_cost_us_out = parameters->effective_cost_us[index];
	return SG_HumanTraceLearningEffectiveCostValid(*effective_cost_us_out);
}
