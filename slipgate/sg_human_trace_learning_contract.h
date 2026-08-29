/* Authenticated, parameter-only costs over immutable continuous dynamics. */
#ifndef SG_HUMAN_TRACE_LEARNING_CONTRACT_H
#define SG_HUMAN_TRACE_LEARNING_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_dynamics_model.h"

#define SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES 32U

#if defined(__GNUC__) || defined(__clang__)
#define SG_HUMAN_TRACE_LEARNING_LOCAL __attribute__((visibility("hidden")))
#else
#define SG_HUMAN_TRACE_LEARNING_LOCAL
#endif

typedef struct sg_human_trace_learning_trace_id_s
{
	uint8_t bytes[SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES];
} sg_human_trace_learning_trace_id_t;

typedef struct sg_human_trace_learning_identity_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t bsp_identity;
	uint64_t physics_identity;
} sg_human_trace_learning_identity_t;

/* Field guidance already identifies its choice by control fiber and the
 * runtime model identifies the traversal by kernel. Neither is an ordinal. */
typedef struct sg_human_trace_learning_kernel_key_s
{
	sg_rune_control_fiber_ref_t control;
	sg_rune_kernel_ref_t kernel;
} sg_human_trace_learning_kernel_key_t;

typedef struct sg_human_trace_learning_domain_s
{
	sg_human_trace_learning_identity_t identity;
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_human_trace_learning_kernel_key_t *kernel_keys;
	size_t kernel_key_count;
} sg_human_trace_learning_domain_t;

typedef struct sg_human_trace_learning_storage_s
{
	uint64_t *effective_cost_us;
	size_t effective_cost_capacity;
} sg_human_trace_learning_storage_t;

typedef enum sg_human_trace_learning_update_kind_e
{
	SG_HUMAN_TRACE_LEARNING_UPDATE_COST = 0,
	SG_HUMAN_TRACE_LEARNING_UPDATE_KIND_COUNT
} sg_human_trace_learning_update_kind_t;

typedef enum sg_human_trace_learning_transaction_state_e
{
	SG_HUMAN_TRACE_LEARNING_TRANSACTION_EMPTY = 0,
	SG_HUMAN_TRACE_LEARNING_TRANSACTION_PREPARED,
	SG_HUMAN_TRACE_LEARNING_TRANSACTION_APPLIED,
	SG_HUMAN_TRACE_LEARNING_TRANSACTION_COMMITTED,
	SG_HUMAN_TRACE_LEARNING_TRANSACTION_ROLLED_BACK
} sg_human_trace_learning_transaction_state_t;

typedef struct sg_human_trace_learning_evidence_s
{
	uint64_t evidence_id;
	sg_human_trace_learning_identity_t identity;
	sg_human_trace_learning_trace_id_t trace;
	uint64_t captured_at_ms;
} sg_human_trace_learning_evidence_t;

typedef struct sg_human_trace_learning_update_s
{
	sg_human_trace_learning_evidence_t evidence;
	sg_human_trace_learning_update_kind_t kind;
	sg_human_trace_learning_kernel_key_t key;
	uint64_t effective_cost_us;
} sg_human_trace_learning_update_t;

typedef struct sg_human_trace_learning_parameters_s
{
	sg_human_trace_learning_domain_t domain;
	uint64_t generation;
	uint64_t *effective_cost_us;
	size_t effective_cost_capacity;
} sg_human_trace_learning_parameters_t;

/* Caller-owned clone storage rejects the entire batch when too small. It is a
 * representation capacity, never a cap on model size or learning work. */
typedef struct sg_human_trace_learning_workspace_s
{
	uint64_t *effective_cost_us;
	size_t effective_cost_capacity;
} sg_human_trace_learning_workspace_t;

typedef struct sg_human_trace_learning_transaction_s
{
	uint64_t transaction_id;
	uint64_t expected_generation;
	uint64_t applied_generation;
	uint64_t evidence_id;
	sg_human_trace_learning_transaction_state_t state;
	sg_human_trace_learning_update_t authorized_update;
	uint64_t before_effective_cost_us;
} sg_human_trace_learning_transaction_t;

static inline int SG_HumanTraceLearningTraceIdValid(const sg_human_trace_learning_trace_id_t *trace)
{
	uint32_t index;
	uint8_t nonzero = 0U;

	if (!trace)
		return 0;
	for (index = 0U; index < SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES; index++)
		nonzero |= trace->bytes[index];
	return nonzero != 0U;
}

static inline int SG_HumanTraceLearningIdentityValid(const sg_human_trace_learning_identity_t *identity)
{
	return identity && identity->rune_identity != 0U &&
		identity->topology_revision != 0U && identity->bsp_identity != 0U &&
		identity->physics_identity != 0U;
}

static inline int SG_HumanTraceLearningIdentityEqual(const sg_human_trace_learning_identity_t *left,
	const sg_human_trace_learning_identity_t *right)
{
	return SG_HumanTraceLearningIdentityValid(left) && SG_HumanTraceLearningIdentityValid(right) &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->bsp_identity == right->bsp_identity &&
		left->physics_identity == right->physics_identity;
}

static inline int SG_HumanTraceLearningEffectiveCostValid(uint64_t cost_us)
{
	return cost_us != 0U && cost_us != SG_RUNE_FIELD_COST_INFINITE;
}

static inline int SG_HumanTraceLearningKernelKeyValid(const sg_human_trace_learning_kernel_key_t *key)
{
	sg_rune_order_key_t control_order;

	return key && SG_RuneModelStableIdToOrderKey(&key->control.value,
		&control_order) && control_order.domain == SG_RUNE_ORDER_CONTROL_FIBER &&
		SG_RuneModelStableIdValid(&key->kernel.value);
}

static inline int SG_HumanTraceLearningEvidenceValid(const sg_human_trace_learning_evidence_t *evidence)
{
	return evidence && evidence->evidence_id != 0U &&
		SG_HumanTraceLearningIdentityValid(&evidence->identity) &&
		SG_HumanTraceLearningTraceIdValid(&evidence->trace) &&
		evidence->captured_at_ms != 0U;
}

static inline int SG_HumanTraceLearningUpdateValid(const sg_human_trace_learning_update_t *update)
{
	return update && SG_HumanTraceLearningEvidenceValid(&update->evidence) &&
		update->kind == SG_HUMAN_TRACE_LEARNING_UPDATE_COST &&
		SG_HumanTraceLearningKernelKeyValid(&update->key) &&
		SG_HumanTraceLearningEffectiveCostValid(update->effective_cost_us);
}

/* Updates have no geometry, connectivity, or model mutation member. */
static inline int SG_HumanTraceLearningUpdateTouchesGeometry(const sg_human_trace_learning_update_t *update)
{
	(void)update;
	return 0;
}

SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningParametersInit(sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_domain_t *domain, const sg_human_trace_learning_storage_t *storage);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningParametersValid(const sg_human_trace_learning_parameters_t *parameters);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningWorkspaceValid(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_workspace_t *workspace);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningParametersClone(const sg_human_trace_learning_parameters_t *source,
	const sg_human_trace_learning_workspace_t *workspace, sg_human_trace_learning_parameters_t *out);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningParametersReplace(sg_human_trace_learning_parameters_t *destination,
	const sg_human_trace_learning_parameters_t *source);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningEvidenceMatches(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_evidence_t *evidence);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningUpdateTargetsParameters(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_update_t *update);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningUpdateSame(const sg_human_trace_learning_update_t *left,
	const sg_human_trace_learning_update_t *right);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningParametersSameIdentity(const sg_human_trace_learning_parameters_t *left,
	const sg_human_trace_learning_parameters_t *right);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionValid(const sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionBoundToParameters(
	const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionMayCommit(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionMayRollback(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionBegin(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_update_t *update, uint64_t transaction_id,
	sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningApplyUpdate(sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_update_t *update,
	sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionCommit(const sg_human_trace_learning_parameters_t *parameters,
	sg_human_trace_learning_transaction_t *transaction);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTransactionRollback(sg_human_trace_learning_parameters_t *parameters,
	sg_human_trace_learning_transaction_t *transaction);

/* The field-service owner calls this with its published parameter view. NULL
 * is an exact neutral fallback; there is no global publication authority. */
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningEffectiveKernelCost(const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_kernel_key_t *key, uint64_t static_cost_us,
	uint64_t *effective_cost_us_out);

#endif /* SG_HUMAN_TRACE_LEARNING_CONTRACT_H */
