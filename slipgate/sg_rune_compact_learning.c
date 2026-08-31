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

static int TraversalCompare(const sg_rune_compact_learning_traversal_ref_t *left,
	const sg_rune_compact_learning_traversal_ref_t *right)
{
	int comparison = CompareU32(left->source_cell.value,
		right->source_cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->target_cell.value,
			right->target_cell.value);
	if (comparison == 0)
		comparison = CompareU32(left->portal.value, right->portal.value);
	if (comparison == 0)
		comparison = CompareU32(left->movement_field, right->movement_field);
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
	case SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL:
		output->value.traversal = input->value.traversal;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_LANDING:
		output->value.landing = input->value.landing;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_TACTIC:
		output->value.tactic = input->value.tactic;
		return 1;
	case SG_RUNE_COMPACT_LEARNING_STRATEGY:
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
	case SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL:
		return TraversalCompare(&left->value.traversal,
			&right->value.traversal);
	case SG_RUNE_COMPACT_LEARNING_LANDING:
		return TraversalCompare(&left->value.landing, &right->value.landing);
	case SG_RUNE_COMPACT_LEARNING_TACTIC:
		comparison = CompareU32(left->value.tactic.cell.value,
			right->value.tactic.cell.value);
		return comparison != 0 ? comparison : CompareU32(
			left->value.tactic.weapon_kernel,
			right->value.tactic.weapon_kernel);
	case SG_RUNE_COMPACT_LEARNING_STRATEGY:
		comparison = CompareU32(left->value.strategy.cell.value,
			right->value.strategy.cell.value);
		return comparison != 0 ? comparison : CompareU32(
			left->value.strategy.landmark.value,
			right->value.strategy.landmark.value);
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

static int PortalConnects(const sg_rune_compact_model_t *model,
	uint32_t portal_index, uint32_t source_cell, uint32_t target_cell)
{
	const sg_rune_compact_portal_t *portal;
	uint32_t negative;
	uint32_t positive;

	if (portal_index >= model->portal_count || source_cell >= model->cell_count ||
		target_cell >= model->cell_count)
		return 0;
	portal = &model->portals[portal_index];
	negative = model->incidences[portal->negative_incidence.value].cell.value;
	positive = model->incidences[portal->positive_incidence.value].cell.value;
	return (source_cell == negative && target_cell == positive &&
		(portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH ||
		 portal->direction == SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE)) ||
		(source_cell == positive && target_cell == negative &&
		(portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH ||
		 portal->direction == SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE));
}

static int TraversalValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_traversal_ref_t *reference)
{
	const sg_rune_movement_field_attachment_t *field;

	if (!ReservedZero(reference->reserved) || !StanceValid(reference->stance) ||
		reference->movement_field >= model->movement_field_count ||
		!PortalConnects(model, reference->portal.value,
			reference->source_cell.value, reference->target_cell.value))
		return 0;
	field = &model->movement_fields[reference->movement_field];
	return field->cell.value == reference->source_cell.value &&
		field->boundary_portal.value == reference->portal.value &&
		(field->valid_stances & reference->stance) != 0U &&
		(model->cells[reference->source_cell.value].valid_stances &
			reference->stance) != 0U &&
		(model->cells[reference->target_cell.value].valid_stances &
			reference->stance) != 0U &&
		(model->portals[reference->portal.value].valid_stances &
			reference->stance) != 0U;
}

static int TacticValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_tactic_ref_t *reference)
{
	const sg_rune_weapon_response_kernel_t *kernel;

	if (reference->cell.value >= model->cell_count ||
		reference->weapon_kernel >= model->weapon_kernel_count)
		return 0;
	kernel = &model->weapon_kernels[reference->weapon_kernel];
	return kernel->region.value < model->weapon_region_count &&
		model->weapon_regions[kernel->region.value].cell.value ==
			reference->cell.value;
}

static int StrategyValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_strategy_ref_t *reference)
{
	const sg_rune_compact_static_t *static_data = model->static_data;
	const sg_rune_compact_landmark_t *landmark;
	uint32_t index;

	if (reference->cell.value >= model->cell_count || static_data == NULL ||
		reference->landmark.value >= static_data->landmark_count)
		return 0;
	landmark = &static_data->landmarks[reference->landmark.value];
	for (index = landmark->cells.first;
		index < landmark->cells.first + landmark->cells.count; index++)
		if (static_data->landmark_cells[index].value == reference->cell.value)
			return 1;
	return 0;
}

static int KeyValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_learning_key_t *key)
{
	if (key == NULL)
		return 0;
	switch (key->kind) {
	case SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL:
		return TraversalValid(model, &key->value.traversal);
	case SG_RUNE_COMPACT_LEARNING_LANDING:
		return TraversalValid(model, &key->value.landing);
	case SG_RUNE_COMPACT_LEARNING_TACTIC:
		return TacticValid(model, &key->value.tactic);
	case SG_RUNE_COMPACT_LEARNING_STRATEGY:
		return StrategyValid(model, &key->value.strategy);
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
};

static int IssuerCurrent(const sg_rune_compact_learning_issuer_t *issuer)
{
	return issuer != NULL && issuer->model != NULL &&
		(uint32_t)issuer->source < (uint32_t)LEARNING_SOURCE_COUNT &&
		SG_RuneCompactIdentityMatches(&issuer->model->identity,
			&issuer->expected_identity);
}

static sg_rune_compact_learning_status_t IssuerAcquire(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	learning_source_t source,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out)
{
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_error_t local_error;
	sg_rune_compact_error_t *error = model_error_out != NULL ? model_error_out :
		&local_error;

	if (model == NULL || expected_identity == NULL || issuer_out == NULL ||
		(uint32_t)source >= (uint32_t)LEARNING_SOURCE_COUNT)
		return SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT;
	if (!SG_RuneCompactModelValidateBound(model, expected_identity, error))
		return error->code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH ?
			SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH :
			SG_RUNE_COMPACT_LEARNING_INVALID_MODEL;
	issuer = calloc(1U, sizeof(*issuer));
	if (issuer == NULL)
		return SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED;
	issuer->model = model;
	issuer->expected_identity = *expected_identity;
	issuer->source = source;
	*issuer_out = issuer;
	return SG_RUNE_COMPACT_LEARNING_OK;
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerAcquireHuman(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out)
{
	return IssuerAcquire(model, expected_identity, LEARNING_SOURCE_HUMAN,
		issuer_out, model_error_out);
}

sg_rune_compact_learning_status_t SG_RuneCompactLearningIssuerAcquireBot(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_issuer_t **issuer_out,
	sg_rune_compact_error_t *model_error_out)
{
	return IssuerAcquire(model, expected_identity, LEARNING_SOURCE_BOT,
		issuer_out, model_error_out);
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
	if (!IssuerCurrent(issuer))
		return SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH;
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
	const sg_rune_compact_learning_observation_t *observation,
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
