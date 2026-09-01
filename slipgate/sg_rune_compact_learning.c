#include "sg_human_trace.h"

#define SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE 1
#include "sg_rune_compact_learning_owner.h"
#undef SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SG_RUNE_COMPACT_LEARNING_OBSERVATION_MAGIC UINT64_C(0x4c524e4556494431)

typedef enum learning_source_e
{
	LEARNING_SOURCE_HUMAN = 0,
	LEARNING_SOURCE_BOT,
	LEARNING_SOURCE_COUNT
} learning_source_t;

struct sg_rune_compact_learning_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_compact_identity_t expected_identity;
	sg_rune_compact_learning_prior_t *priors;
	uint32_t prior_count;
	uint32_t prior_capacity;
};

struct sg_rune_compact_learning_observation_s
{
	uint64_t magic;
	sg_rune_compact_identity_t model_identity;
	sg_rune_compact_learning_key_t key;
	uint64_t value_q16;
	learning_source_t source;
	uint8_t consumed;
};

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int StanceValid(sg_rune_stance_validity_t stance)
{
	return stance == SG_RUNE_STANCE_VALID_STANDING ||
		stance == SG_RUNE_STANCE_VALID_CROUCHING;
}

static int ReservedZero(const uint8_t reserved[3])
{
	return reserved[0] == 0U && reserved[1] == 0U && reserved[2] == 0U;
}

static int CostCompare(
	const sg_rune_compact_learning_stable_cell_capability_cost_ref_t *left,
	const sg_rune_compact_learning_stable_cell_capability_cost_ref_t *right)
{
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->capability.value,
			right->capability.value);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->stance,
			(uint32_t)right->stance);
	return comparison;
}

static int LandingCompare(
	const sg_rune_compact_learning_landing_preference_ref_t *left,
	const sg_rune_compact_learning_landing_preference_ref_t *right)
{
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->stance,
			(uint32_t)right->stance);
	return comparison;
}

static int CanonicalizeKey(const sg_rune_compact_learning_key_t *input,
	sg_rune_compact_learning_key_t *output)
{
	if (input == NULL || output == NULL ||
		(uint32_t)input->kind >=
			(uint32_t)SG_RUNE_COMPACT_LEARNING_KIND_COUNT)
		return 0;
	memset(output, 0, sizeof(*output));
	output->kind = input->kind;
	switch (input->kind) {
	case SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST:
		output->value.cost = input->value.cost;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE:
		output->value.landing = input->value.landing;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR:
		output->value.tactical = input->value.tactical;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME:
		output->value.strategy = input->value.strategy;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_KIND_COUNT:
		break;
	}
	return 0;
}

static int KeyCompare(const sg_rune_compact_learning_key_t *left,
	const sg_rune_compact_learning_key_t *right)
{
	int comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);

	if (comparison != 0)
		return comparison;
	switch (left->kind) {
	case SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST:
		return CostCompare(&left->value.cost, &right->value.cost);
	case SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE:
		return LandingCompare(&left->value.landing, &right->value.landing);
	case SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR:
		comparison = CompareU32(left->value.tactical.cell.value,
			right->value.tactical.cell.value);
		return comparison != 0 ? comparison : CompareU32(
			left->value.tactical.weapon_kernel,
			right->value.tactical.weapon_kernel);
	case SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME:
		comparison = CompareU32(
			left->value.strategy.landmark.value,
			right->value.strategy.landmark.value);
		return comparison != 0 ? comparison : CompareU32(
			(uint32_t)left->value.strategy.outcome,
			(uint32_t)right->value.strategy.outcome);
	case SG_RUNE_COMPACT_LEARNING_KIND_COUNT:
		break;
	}
	return 0;
}

static uint64_t SaturatingAdd(uint64_t left, uint64_t right)
{
	return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

static int Current(const sg_rune_compact_learning_t *learning)
{
	return learning != NULL && learning->model != NULL &&
		SG_RuneCompactIdentityMatches(&learning->model->identity,
			&learning->expected_identity);
}

static int CostValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_stable_cell_capability_cost_ref_t *reference)
{
	const sg_rune_movement_capability_t *capability;

	if (model == NULL || reference == NULL ||
		!ReservedZero(reference->reserved) || !StanceValid(reference->stance) ||
		reference->cell.value >= model->cell_count ||
		reference->capability.value >= model->movement_capability_count)
		return 0;
	capability = &model->movement_capabilities[reference->capability.value];
	return capability->cell.value == reference->cell.value &&
		(capability->source_stances & reference->stance) != 0U &&
		(model->cells[reference->cell.value].valid_stances &
			reference->stance) != 0U;
}

static int LandingValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_landing_preference_ref_t *reference)
{
	return model != NULL && reference != NULL &&
		ReservedZero(reference->reserved) && StanceValid(reference->stance) &&
		reference->cell.value < model->cell_count &&
		(model->cells[reference->cell.value].valid_stances &
			reference->stance) != 0U;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

static int WeaponKernelValid(const sg_rune_compact_model_t *model,
	uint32_t kernel_index)
{
	const sg_rune_weapon_response_kernel_t *kernel;
	const sg_rune_weapon_profile_t *profile;
	sg_rune_weapon_event_law_t expected_law;
	uint32_t expected_count;
	uint32_t ordinal;

	if (model == NULL || model->weapon_profiles == NULL ||
		model->weapon_kernels == NULL || model->weapon_function_refs == NULL ||
		model->analytic == NULL || kernel_index >= model->weapon_kernel_count)
		return 0;
	kernel = &model->weapon_kernels[kernel_index];
	if (kernel->profile >= model->weapon_profile_count ||
		(uint32_t)kernel->family >=
			(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT)
		return 0;
	profile = &model->weapon_profiles[kernel->profile];
	if (profile->source_profile != kernel->profile + 1U ||
		(profile->response_families &
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(kernel->family)) == 0U ||
		!SG_RuneCompactWeaponCanonicalEventLaw(profile->source_profile,
			kernel->family, &expected_law) ||
		kernel->event_law.kind != expected_law.kind ||
		kernel->event_law.requirements != expected_law.requirements ||
		!SG_RuneCompactWeaponKernelReferenceCount(profile, kernel->family,
			&expected_count) || kernel->functions.count != expected_count ||
		!SpanWithin(kernel->functions.first, kernel->functions.count,
			model->weapon_function_ref_count))
		return 0;
	for (ordinal = 0U; ordinal < kernel->functions.count; ordinal++) {
		const sg_rune_weapon_function_ref_t *function =
			&model->weapon_function_refs[kernel->functions.first + ordinal];
		sg_rune_weapon_effect_channel_t expected_channel;
		uint32_t expected_instance;
		sg_rune_analytic_output_meaning_t expected_output;

		if (function->function.value >= model->analytic->function_count ||
			!SG_RuneCompactWeaponFunctionRefExpected(profile, kernel->family,
				ordinal, &expected_channel, &expected_instance, &expected_output) ||
			function->channel != expected_channel ||
			function->instance != expected_instance ||
			model->analytic->functions[function->function.value].output !=
				expected_output)
			return 0;
	}
	return 1;
}

static int CertifiedResponseForCell(const sg_rune_compact_model_t *model,
	uint32_t cell_index)
{
	uint32_t fragment_index;

	if (model == NULL || cell_index >= model->cell_count ||
		model->response.source_fragments == NULL ||
		model->response.source_fragment_count == 0U)
		return 0;
	for (fragment_index = 0U;
		fragment_index < model->response.source_fragment_count;
		fragment_index++) {
		const sg_rune_compact_response_fragment_t *fragment =
			&model->response.source_fragments[fragment_index];

		/* Source fragments are the normalized response authority.  A tactic
		 * may be learned in any response-bearing cell; it does not need a
		 * HOOK movement attachment. */
		if (fragment->parent_cell.value == cell_index &&
			fragment->valid_stances != 0U &&
			SpanWithin(fragment->boundary_incidences.first,
				fragment->boundary_incidences.count,
				model->cell_incidence_count))
			return 1;
	}
	return 0;
}

static int TacticalPriorValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_tactical_prior_ref_t *reference)
{
	return reference != NULL && model != NULL &&
		reference->cell.value < model->cell_count &&
		WeaponKernelValid(model, reference->weapon_kernel) &&
		CertifiedResponseForCell(model, reference->cell.value);
}

static int StrategyOutcomeValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_strategy_outcome_ref_t *reference)
{
	const sg_rune_compact_static_t *static_data;

	if (model == NULL || reference == NULL)
		return 0;
	static_data = model->static_data;
	return static_data != NULL &&
		reference->landmark.value < static_data->landmark_count &&
		(uint32_t)reference->outcome <
			(uint32_t)SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME_COUNT;
}

static int KeyValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_key_t *key)
{
	if (key == NULL)
		return 0;
	switch (key->kind) {
	case SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST:
		return CostValid(model, &key->value.cost);
	case SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE:
		return LandingValid(model, &key->value.landing);
	case SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR:
		return TacticalPriorValid(model, &key->value.tactical);
	case SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME:
		return StrategyOutcomeValid(model, &key->value.strategy);
	case SG_RUNE_COMPACT_LEARNING_KIND_COUNT:
		break;
	}
	return 0;
}

static int QuantizeValue(float value, uint64_t *value_q16)
{
	const long double scaled = (long double)value *
		(long double)SG_RUNE_COMPACT_LEARNING_Q16_SCALE;
	const long double upper_exclusive = ldexpl(1.0L, 64);
	long double rounded;

	if (!isfinite(value) || value < 0.0f ||
		scaled >= upper_exclusive)
		return 0;
	rounded = floorl(scaled);
	if (rounded >= upper_exclusive)
		return 0;
	if (scaled - rounded >= 0.5L && rounded + 1.0L < upper_exclusive)
		rounded += 1.0L;
	*value_q16 = (uint64_t)rounded;
	return 1;
}

static uint32_t LowerBound(const sg_rune_compact_learning_t *learning,
	const sg_rune_compact_learning_key_t *key, int *found)
{
	uint32_t first = 0U;
	uint32_t count = learning->prior_count;

	while (count != 0U) {
		const uint32_t half = count / 2U;
		const uint32_t middle = first + half;
		const int comparison = KeyCompare(&learning->priors[middle].key, key);

		if (comparison < 0) {
			first = middle + 1U;
			count -= half + 1U;
		} else {
			count = half;
		}
	}
	*found = first < learning->prior_count &&
		KeyCompare(&learning->priors[first].key, key) == 0;
	return first;
}

static int EnsureCapacity(sg_rune_compact_learning_t *learning,
	uint32_t minimum)
{
	sg_rune_compact_learning_prior_t *prior;
	uint32_t capacity;
	size_t bytes;

	if (minimum <= learning->prior_capacity)
		return 1;
	capacity = learning->prior_capacity == 0U ? 8U : learning->prior_capacity;
	while (capacity < minimum) {
		if (capacity > UINT32_MAX / 2U) {
			capacity = minimum;
			break;
		}
		capacity *= 2U;
	}
	bytes = (size_t)capacity * sizeof(*learning->priors);
	if (capacity != 0U && bytes / sizeof(*learning->priors) != capacity)
		return 0;
	prior = realloc(learning->priors, bytes);
	if (prior == NULL)
		return 0;
	learning->priors = prior;
	learning->prior_capacity = capacity;
	return 1;
}

static void AddObservation(sg_rune_compact_learning_prior_t *prior,
	learning_source_t source, uint64_t value_q16)
{
	prior->value_total_q16 = SaturatingAdd(prior->value_total_q16,
		(uint64_t)value_q16);
	if (source == LEARNING_SOURCE_HUMAN)
		prior->human_samples = SaturatingAdd(prior->human_samples, 1U);
	else
		prior->bot_samples = SaturatingAdd(prior->bot_samples, 1U);
}

struct sg_rune_compact_learning_issuer_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_compact_identity_t expected_identity;
	learning_source_t source;
	const sg_human_trace_v3_scope_acceptance_t *human_scope;
	uint32_t human_client_id;
	uint64_t human_spawn_generation;
};

static int HumanScopeCurrent(
	const sg_human_trace_v3_scope_acceptance_t *scope,
	uint32_t *client_id_out, uint64_t *spawn_generation_out)
{
	const sg_human_trace_v3_spool_ref_t *root = NULL;
	uint32_t client_id = 0U;
	uint64_t spawn_generation = 0U;

	if (scope == NULL || !SG_HumanTraceAcceptedV3ScopeView(scope, &root,
		&client_id, &spawn_generation) || root == NULL || client_id == 0U ||
		spawn_generation == 0U)
		return 0;
	if (client_id_out != NULL)
		*client_id_out = client_id;
	if (spawn_generation_out != NULL)
		*spawn_generation_out = spawn_generation;
	return 1;
}

static int IssuerModelCurrent(const sg_rune_compact_learning_issuer_t *issuer)
{
	return issuer != NULL && issuer->model != NULL &&
		(uint32_t)issuer->source < (uint32_t)LEARNING_SOURCE_COUNT &&
		SG_RuneCompactIdentityMatches(&issuer->model->identity,
			&issuer->expected_identity);
}

static int IssuerCurrent(const sg_rune_compact_learning_issuer_t *issuer)
{
	uint32_t client_id;
	uint64_t spawn_generation;

	if (!IssuerModelCurrent(issuer))
		return 0;
	if (issuer->source != LEARNING_SOURCE_HUMAN)
		return 1;
	return HumanScopeCurrent(issuer->human_scope, &client_id,
		&spawn_generation) && client_id == issuer->human_client_id &&
		spawn_generation == issuer->human_spawn_generation;
}

static sg_rune_compact_learning_status_t IssuerAcquire(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	learning_source_t source,
	const sg_human_trace_v3_scope_acceptance_t *human_scope,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out)
{
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_error_t local_error;
	sg_rune_compact_error_t *error = model_error_out != NULL ? model_error_out :
		&local_error;
	uint32_t human_client_id = 0U;
	uint64_t human_spawn_generation = 0U;

	if (model == NULL || expected_identity == NULL || issuer_out == NULL ||
		(uint32_t)source >= (uint32_t)LEARNING_SOURCE_COUNT)
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	if (source == LEARNING_SOURCE_HUMAN) {
		if (!HumanScopeCurrent(human_scope, &human_client_id,
			&human_spawn_generation))
			return SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED;
	} else if (human_scope != NULL) {
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	}
	if (!SG_RuneCompactModelValidateBound(model, expected_identity, error))
		return error->code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH ?
			SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH :
			error->code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY ?
			SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED :
			SG_RUNE_COMPACT_LEARNING_INVALID_MODEL;
	issuer = calloc(1U, sizeof(*issuer));
	if (issuer == NULL)
		return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
	issuer->model = model;
	issuer->expected_identity = *expected_identity;
	issuer->source = source;
	issuer->human_scope = human_scope;
	issuer->human_client_id = human_client_id;
	issuer->human_spawn_generation = human_spawn_generation;
	*issuer_out = issuer;
	return SG_RUNE_COMPACT_LEARNING_OK;
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerAcquireHuman(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_human_trace_v3_scope_acceptance_t *accepted_scope,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out)
{
	return IssuerAcquire(model, expected_identity, LEARNING_SOURCE_HUMAN,
		accepted_scope, issuer_out, model_error_out);
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerAcquireBot(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out)
{
	return IssuerAcquire(model, expected_identity, LEARNING_SOURCE_BOT,
		NULL, issuer_out, model_error_out);
}

void SG_RuneCompactLearningIssuerDestroy(
	sg_rune_compact_learning_issuer_t *issuer)
{
	free(issuer);
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerIssue(
	const sg_rune_compact_learning_issuer_t *issuer,
	const sg_rune_compact_learning_claim_t *claim,
	sg_rune_compact_learning_observation_t **observation_out)
{
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_key_t key;
	uint64_t value_q16;

	if (issuer == NULL || claim == NULL || observation_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	if (!IssuerModelCurrent(issuer))
		return SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH;
	if (issuer->source == LEARNING_SOURCE_HUMAN && !IssuerCurrent(issuer))
		return SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED;
	if (!CanonicalizeKey(&claim->key, &key))
		return SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION;
	if (!KeyValid(issuer->model, &key))
		return SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE;
	if (!QuantizeValue(claim->value, &value_q16))
		return SG_RUNE_COMPACT_LEARNING_INVALID_VALUE;
	observation = calloc(1U, sizeof(*observation));
	if (observation == NULL)
		return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
	observation->magic = SG_RUNE_COMPACT_LEARNING_OBSERVATION_MAGIC;
	observation->model_identity = issuer->expected_identity;
	memcpy(&observation->key, &key, sizeof(observation->key));
	observation->value_q16 = value_q16;
	observation->source = issuer->source;
	observation->consumed = 0U;
	*observation_out = observation;
	return SG_RUNE_COMPACT_LEARNING_OK;
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_t **learning_out,
	sg_rune_compact_error_t *model_error_out)
{
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_error_t local_error;
	sg_rune_compact_error_t *error = model_error_out != NULL ? model_error_out :
		&local_error;

	if (model == NULL || expected_identity == NULL || learning_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	if (!SG_RuneCompactModelValidateBound(model, expected_identity, error))
		return error->code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH ?
			SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH :
			error->code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY ?
			SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED :
			SG_RUNE_COMPACT_LEARNING_INVALID_MODEL;
	learning = calloc(1U, sizeof(*learning));
	if (learning == NULL)
		return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
	learning->model = model;
	learning->expected_identity = *expected_identity;
	*learning_out = learning;
	return SG_RUNE_COMPACT_LEARNING_OK;
}

void SG_RuneCompactLearningDestroy(sg_rune_compact_learning_t *learning)
{
	if (learning == NULL)
		return;
	free(learning->priors);
	free(learning);
}

void SG_RuneCompactLearningObservationDestroy(
	sg_rune_compact_learning_observation_t *observation)
{
	free(observation);
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningApply(
	sg_rune_compact_learning_t *learning,
	sg_rune_compact_learning_observation_t *observation,
	sg_rune_compact_learning_prior_t *prior_out)
{
	uint32_t index;
	int found;
	sg_rune_compact_learning_key_t canonical_key;

	if (learning == NULL || observation == NULL || prior_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	if (!Current(learning) ||
		!SG_RuneCompactIdentityMatches(&observation->model_identity,
			&learning->expected_identity))
		return SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH;
	if (observation->magic != SG_RUNE_COMPACT_LEARNING_OBSERVATION_MAGIC ||
		(uint32_t)observation->source >= (uint32_t)LEARNING_SOURCE_COUNT)
		return SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED;
	if (observation->consumed != 0U)
		return SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION;
	if (!CanonicalizeKey(&observation->key, &canonical_key) ||
		memcmp(&canonical_key, &observation->key, sizeof(canonical_key)) != 0)
		return SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION;
	if (!KeyValid(learning->model, &canonical_key))
		return SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE;
	index = LowerBound(learning, &canonical_key, &found);
	if (!found) {
		if (learning->prior_count == UINT32_MAX ||
			!EnsureCapacity(learning, learning->prior_count + 1U))
			return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
		memmove(&learning->priors[index + 1U], &learning->priors[index],
			(size_t)(learning->prior_count - index) *
			sizeof(*learning->priors));
		memset(&learning->priors[index], 0, sizeof(learning->priors[index]));
		memcpy(&learning->priors[index].key, &canonical_key,
			sizeof(learning->priors[index].key));
		learning->prior_count++;
	}
	AddObservation(&learning->priors[index], observation->source,
		observation->value_q16);
	memcpy(prior_out, &learning->priors[index], sizeof(*prior_out));
	observation->consumed = 1U;
	return SG_RUNE_COMPACT_LEARNING_OK;
}

static void MergePrior(sg_rune_compact_learning_prior_t *target,
	const sg_rune_compact_learning_prior_t *source)
{
	target->value_total_q16 = SaturatingAdd(target->value_total_q16,
		source->value_total_q16);
	target->human_samples = SaturatingAdd(target->human_samples,
		source->human_samples);
	target->bot_samples = SaturatingAdd(target->bot_samples,
		source->bot_samples);
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningMerge(
	sg_rune_compact_learning_t *target,
	const sg_rune_compact_learning_t *source)
{
	uint32_t missing = 0U;
	uint32_t source_index;

	if (target == NULL || source == NULL)
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	if (!Current(target) || !Current(source) ||
		!SG_RuneCompactIdentityMatches(&target->expected_identity,
			&source->expected_identity))
		return SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH;
	if (target == source)
		return SG_RUNE_COMPACT_LEARNING_OK;
	for (source_index = 0U; source_index < source->prior_count;
		source_index++) {
		int found;

		(void)LowerBound(target, &source->priors[source_index].key, &found);
		if (!found) {
			if (missing == UINT32_MAX)
				return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
			missing++;
		}
	}
	if (missing > UINT32_MAX - target->prior_count ||
		!EnsureCapacity(target, target->prior_count + missing))
		return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
	for (source_index = 0U; source_index < source->prior_count;
		source_index++) {
		const sg_rune_compact_learning_prior_t *prior =
			&source->priors[source_index];
		uint32_t index;
		int found;

		index = LowerBound(target, &prior->key, &found);
		if (!found) {
			memmove(&target->priors[index + 1U], &target->priors[index],
				(size_t)(target->prior_count - index) *
				sizeof(*target->priors));
			memcpy(&target->priors[index], prior,
				sizeof(target->priors[index]));
			target->prior_count++;
		} else {
			MergePrior(&target->priors[index], prior);
		}
	}
	return SG_RUNE_COMPACT_LEARNING_OK;
}

uint32_t SG_RuneCompactLearningPriorCount(
	const sg_rune_compact_learning_t *learning)
{
	return learning == NULL ? 0U : learning->prior_count;
}

int SG_RuneCompactLearningPriorRead(const sg_rune_compact_learning_t *learning,
	uint32_t index, sg_rune_compact_learning_prior_t *prior_out)
{
	if (learning == NULL || prior_out == NULL || index >= learning->prior_count)
		return 0;
	memcpy(prior_out, &learning->priors[index], sizeof(*prior_out));
	return 1;
}

const char *SG_RuneCompactLearningStatusString(
	sg_rune_compact_learning_status_t status)
{
	switch (status) {
	case SG_RUNE_COMPACT_LEARNING_OK:
		return "ok";
	case SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_LEARNING_INVALID_MODEL:
		return "invalid compact model";
	case SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH:
		return "compact model identity mismatch";
	case SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED:
		return "unauthenticated observation";
	case SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION:
		return "invalid observation";
	case SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE:
		return "invalid static reference";
	case SG_RUNE_COMPACT_LEARNING_INVALID_VALUE:
		return "invalid observation value";
	case SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED:
		return "allocation failed";
	case SG_RUNE_COMPACT_LEARNING_STATUS_COUNT:
		break;
	}
	return "unknown compact learning status";
}
