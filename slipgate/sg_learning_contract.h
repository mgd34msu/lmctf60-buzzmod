/* Typed parameter-learning contracts. */
#ifndef SG_LEARNING_CONTRACT_H
#define SG_LEARNING_CONTRACT_H

#include <stdint.h>

#define SG_LEARNING_MAX_CAPABILITIES 32U
#define SG_LEARNING_MAX_TACTICS 32U
#define SG_LEARNING_MAX_REGIONS 128U
#define SG_LEARNING_MAX_GOAL_KINDS 16U

typedef enum sg_learning_update_kind_e
{
	SG_LEARNING_UPDATE_COST = 0,
	SG_LEARNING_UPDATE_TACTIC_PRIOR,
	SG_LEARNING_UPDATE_LANDING_PREFERENCE,
	SG_LEARNING_UPDATE_STRATEGY,
	SG_LEARNING_UPDATE_KIND_COUNT
} sg_learning_update_kind_t;

typedef enum sg_learning_transaction_state_e
{
	SG_LEARNING_TRANSACTION_EMPTY = 0,
	SG_LEARNING_TRANSACTION_PREPARED,
	SG_LEARNING_TRANSACTION_APPLIED,
	SG_LEARNING_TRANSACTION_COMMITTED,
	SG_LEARNING_TRANSACTION_ROLLED_BACK
} sg_learning_transaction_state_t;

typedef struct sg_learning_evidence_s
{
	uint64_t evidence_id;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t bsp_identity;
	uint64_t physics_identity;
	uint64_t trace_identity;
	uint64_t captured_at_ms;
	uint64_t authenticated_at_ms;
	uint8_t authenticated;
	uint8_t exact_bound;
	uint8_t host_verified;
	uint8_t post_match;
} sg_learning_evidence_t;

typedef struct sg_learning_update_s
{
	sg_learning_evidence_t evidence;
	sg_learning_update_kind_t kind;
	union
	{
		struct { uint32_t capability_id; int32_t delta_ms; } cost;
		struct { uint32_t tactic_id; float prior; } tactic;
		struct { uint32_t region_id; float preference; } landing;
		struct { uint32_t goal_kind; int16_t priority_delta; } strategy;
	} value;
} sg_learning_update_t;

typedef struct sg_learning_parameters_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t bsp_identity;
	uint64_t physics_identity;
	uint64_t generation;
	int32_t cost_delta_ms[SG_LEARNING_MAX_CAPABILITIES];
	float tactic_prior[SG_LEARNING_MAX_TACTICS];
	float landing_preference[SG_LEARNING_MAX_REGIONS];
	int16_t strategy_priority_delta[SG_LEARNING_MAX_GOAL_KINDS];
} sg_learning_parameters_t;

typedef struct sg_learning_transaction_s
{
	uint64_t transaction_id;
	uint64_t expected_generation;
	uint64_t applied_generation;
	uint64_t evidence_id;
	sg_learning_transaction_state_t state;
	sg_learning_parameters_t before;
	sg_learning_update_t authorized_update;
} sg_learning_transaction_t;

static inline int SG_LearningParametersValid(
	const sg_learning_parameters_t *parameters)
{
	return parameters && parameters->rune_identity != 0U &&
		parameters->topology_revision != 0U && parameters->bsp_identity != 0U &&
		parameters->physics_identity != 0U && parameters->generation != 0U;
}

static inline int SG_LearningEvidenceValid(
	const sg_learning_evidence_t *evidence)
{
	return evidence && evidence->evidence_id != 0U &&
		evidence->rune_identity != 0U && evidence->topology_revision != 0U &&
		evidence->bsp_identity != 0U && evidence->physics_identity != 0U &&
		evidence->trace_identity != 0U && evidence->captured_at_ms != 0U &&
		evidence->authenticated_at_ms >= evidence->captured_at_ms &&
		evidence->authenticated == 1U && evidence->exact_bound == 1U &&
		evidence->host_verified == 1U && evidence->post_match == 1U;
}

static inline int SG_LearningEvidenceMatches(
	const sg_learning_parameters_t *parameters,
	const sg_learning_evidence_t *evidence)
{
	return SG_LearningParametersValid(parameters) &&
		SG_LearningEvidenceValid(evidence) &&
		parameters->rune_identity == evidence->rune_identity &&
		parameters->topology_revision == evidence->topology_revision &&
		parameters->bsp_identity == evidence->bsp_identity &&
		parameters->physics_identity == evidence->physics_identity;
}

static inline int SG_LearningBoundedFloat(float value)
{
	return value == value && value >= 0.0f && value <= 1.0f;
}

static inline int SG_LearningUpdateValid(const sg_learning_update_t *update)
{
	if (!update || !SG_LearningEvidenceValid(&update->evidence) ||
	    update->kind < SG_LEARNING_UPDATE_COST ||
	    update->kind >= SG_LEARNING_UPDATE_KIND_COUNT)
		return 0;
	switch (update->kind)
	{
	case SG_LEARNING_UPDATE_COST:
		return update->value.cost.capability_id <
			SG_LEARNING_MAX_CAPABILITIES &&
			update->value.cost.delta_ms >= -30000 &&
			update->value.cost.delta_ms <= 30000;
	case SG_LEARNING_UPDATE_TACTIC_PRIOR:
		return update->value.tactic.tactic_id < SG_LEARNING_MAX_TACTICS &&
			SG_LearningBoundedFloat(update->value.tactic.prior);
	case SG_LEARNING_UPDATE_LANDING_PREFERENCE:
		return update->value.landing.region_id < SG_LEARNING_MAX_REGIONS &&
			SG_LearningBoundedFloat(update->value.landing.preference);
	case SG_LEARNING_UPDATE_STRATEGY:
		return update->value.strategy.goal_kind < SG_LEARNING_MAX_GOAL_KINDS;
	case SG_LEARNING_UPDATE_KIND_COUNT:
	default:
		return 0;
	}
}

static inline int SG_LearningUpdateTouchesGeometry(
	const sg_learning_update_t *update)
{
	(void)update;
	return 0;
}

static inline int SG_LearningUpdateSame(const sg_learning_update_t *left,
	const sg_learning_update_t *right)
{
	if (!SG_LearningUpdateValid(left) || !SG_LearningUpdateValid(right) ||
	    left->kind != right->kind ||
	    left->evidence.evidence_id != right->evidence.evidence_id ||
	    left->evidence.rune_identity != right->evidence.rune_identity ||
	    left->evidence.topology_revision != right->evidence.topology_revision ||
	    left->evidence.bsp_identity != right->evidence.bsp_identity ||
	    left->evidence.physics_identity != right->evidence.physics_identity ||
	    left->evidence.trace_identity != right->evidence.trace_identity ||
	    left->evidence.captured_at_ms != right->evidence.captured_at_ms ||
	    left->evidence.authenticated_at_ms !=
		    right->evidence.authenticated_at_ms)
		return 0;
	switch (left->kind)
	{
	case SG_LEARNING_UPDATE_COST:
		return left->value.cost.capability_id ==
			right->value.cost.capability_id &&
			left->value.cost.delta_ms == right->value.cost.delta_ms;
	case SG_LEARNING_UPDATE_TACTIC_PRIOR:
		return left->value.tactic.tactic_id == right->value.tactic.tactic_id &&
			left->value.tactic.prior == right->value.tactic.prior;
	case SG_LEARNING_UPDATE_LANDING_PREFERENCE:
		return left->value.landing.region_id ==
			right->value.landing.region_id &&
			left->value.landing.preference ==
			right->value.landing.preference;
	case SG_LEARNING_UPDATE_STRATEGY:
		return left->value.strategy.goal_kind ==
			right->value.strategy.goal_kind &&
			left->value.strategy.priority_delta ==
			right->value.strategy.priority_delta;
	case SG_LEARNING_UPDATE_KIND_COUNT:
	default:
		return 0;
	}
}

static inline int SG_LearningParametersSameIdentity(
	const sg_learning_parameters_t *left,
	const sg_learning_parameters_t *right)
{
	return SG_LearningParametersValid(left) &&
		SG_LearningParametersValid(right) &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->bsp_identity == right->bsp_identity &&
		left->physics_identity == right->physics_identity;
}

static inline int SG_LearningTransactionValid(
	const sg_learning_transaction_t *transaction)
{
	if (!transaction || transaction->transaction_id == 0U ||
	    transaction->evidence_id == 0U ||
	    transaction->state < SG_LEARNING_TRANSACTION_PREPARED ||
	    transaction->state > SG_LEARNING_TRANSACTION_ROLLED_BACK ||
	    !SG_LearningParametersValid(&transaction->before) ||
	    !SG_LearningUpdateValid(&transaction->authorized_update) ||
	    !SG_LearningEvidenceMatches(&transaction->before,
		    &transaction->authorized_update.evidence) ||
	    transaction->expected_generation != transaction->before.generation ||
	    transaction->evidence_id !=
		    transaction->authorized_update.evidence.evidence_id)
		return 0;
	if (transaction->state == SG_LEARNING_TRANSACTION_PREPARED)
		return transaction->applied_generation == 0U;
	return transaction->expected_generation != UINT64_MAX &&
		transaction->applied_generation ==
		transaction->expected_generation + 1U;
}

static inline int SG_LearningTransactionBoundToParameters(
	const sg_learning_parameters_t *parameters,
	const sg_learning_transaction_t *transaction)
{
	if (!SG_LearningParametersValid(parameters) ||
	    !SG_LearningTransactionValid(transaction) ||
	    !SG_LearningParametersSameIdentity(parameters, &transaction->before))
		return 0;
	if (transaction->state == SG_LEARNING_TRANSACTION_PREPARED ||
	    transaction->state == SG_LEARNING_TRANSACTION_ROLLED_BACK)
		return parameters->generation == transaction->expected_generation;
	return parameters->generation == transaction->applied_generation;
}

static inline int SG_LearningTransactionMayCommit(
	const sg_learning_parameters_t *parameters,
	const sg_learning_transaction_t *transaction)
{
	return transaction && transaction->state == SG_LEARNING_TRANSACTION_APPLIED &&
		SG_LearningTransactionBoundToParameters(parameters, transaction);
}

static inline int SG_LearningTransactionMayRollback(
	const sg_learning_parameters_t *parameters,
	const sg_learning_transaction_t *transaction)
{
	return transaction && transaction->state == SG_LEARNING_TRANSACTION_APPLIED &&
		SG_LearningTransactionBoundToParameters(parameters, transaction);
}

int SG_LearningParametersInit(sg_learning_parameters_t *parameters,
	uint64_t rune_identity, uint64_t topology_revision,
	uint64_t bsp_identity, uint64_t physics_identity);
int SG_LearningTransactionBegin(const sg_learning_parameters_t *parameters,
	const sg_learning_update_t *update, uint64_t transaction_id,
	sg_learning_transaction_t *transaction);
int SG_LearningApplyUpdate(sg_learning_parameters_t *parameters,
	const sg_learning_update_t *update,
	sg_learning_transaction_t *transaction);
int SG_LearningTransactionCommit(const sg_learning_parameters_t *parameters,
	sg_learning_transaction_t *transaction);
int SG_LearningTransactionRollback(sg_learning_parameters_t *parameters,
	sg_learning_transaction_t *transaction);

#endif /* SG_LEARNING_CONTRACT_H */
