#include "sg_rune_compact_learning_consumer.h"

#define SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE 1
#include "sg_rune_compact_learning_owner.h"
#undef SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct accepted_root_key_s
{
	uint8_t terminal_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint64_t session;
	uint32_t root_segment;
} accepted_root_key_t;

struct sg_rune_compact_learning_consumer_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_compact_identity_t expected_identity;
	sg_rune_compact_learning_t *learning;
	accepted_root_key_t *processed_roots;
	uint32_t processed_root_count;
	uint32_t processed_root_capacity;
};

typedef struct collection_state_s
{
	sg_rune_compact_learning_consumer_t *consumer;
	const sg_level_identity_t *level_identity;
	sg_rune_compact_learning_consumer_validate_fn validate;
	void *validate_context;
	sg_rune_compact_learning_t *staged;
	sg_rune_compact_learning_consumer_report_t report;
	sg_rune_compact_learning_consumer_status_t status;
	const sg_human_trace_v3_spool_ref_t *root;
	const sg_human_trace_v3_segment_ref_t *segment;
	sg_human_trace_v3_segment_ref_t last_segment;
	accepted_root_key_t *new_roots;
	uint32_t new_root_count;
	uint32_t new_root_capacity;
	uint64_t prior_order;
	uint32_t session;
	uint32_t last_segment_number;
	uint8_t have_segment;
	uint8_t have_scope;
	uint8_t skip_root;
} collection_state_t;

static void RootKeyFromRoot(const sg_human_trace_v3_spool_ref_t *root,
	accepted_root_key_t *key)
{
	memcpy(key->terminal_sha256, root->completion.terminal_sha256,
		SG_HUMAN_TRACE_SHA256_BYTES);
	key->session = root->completion.session;
	key->root_segment = root->root_segment;
}

static int RootKeyEqual(const accepted_root_key_t *left,
	const accepted_root_key_t *right)
{
	return left != NULL && right != NULL && left->session == right->session &&
		left->root_segment == right->root_segment &&
		memcmp(left->terminal_sha256, right->terminal_sha256,
			SG_HUMAN_TRACE_SHA256_BYTES) == 0;
}

static int RootKeyListEnsure(accepted_root_key_t **items,
	uint32_t *capacity, uint32_t count, uint32_t additional)
{
	accepted_root_key_t *resized;
	uint32_t needed;
	uint32_t next_capacity;

	if (items == NULL || capacity == NULL || additional > UINT32_MAX - count)
		return 0;
	needed = count + additional;
	if (needed <= *capacity)
		return 1;
	next_capacity = *capacity == 0U ? 8U : *capacity;
	while (next_capacity < needed) {
		if (next_capacity > UINT32_MAX / 2U) {
			next_capacity = needed;
			break;
		}
		next_capacity *= 2U;
	}
#if SIZE_MAX <= UINT32_MAX
	if (next_capacity > SIZE_MAX / sizeof(*resized))
		return 0;
#endif
	resized = realloc(*items, (size_t)next_capacity * sizeof(*resized));
	if (resized == NULL)
		return 0;
	*items = resized;
	*capacity = next_capacity;
	return 1;
}

static int RootKeyKnown(const collection_state_t *state,
	const accepted_root_key_t *key)
{
	uint32_t index;

	if (state == NULL || state->consumer == NULL || key == NULL)
		return 0;
	for (index = 0U; index < state->consumer->processed_root_count; index++)
		if (RootKeyEqual(&state->consumer->processed_roots[index], key))
			return 1;
	for (index = 0U; index < state->new_root_count; index++)
		if (RootKeyEqual(&state->new_roots[index], key))
			return 1;
	return 0;
}

static int BytesNonzero(const uint8_t *bytes, size_t count)
{
	size_t index;

	if (bytes == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (bytes[index] != 0U)
			return 1;
	return 0;
}

static int BoundedStringPresent(const char *value, size_t capacity)
{
	size_t index;

	if (value == NULL)
		return 0;
	for (index = 0U; index < capacity; index++)
		if (value[index] == '\0')
			return index != 0U;
	return 0;
}

static int BoundedStringEqual(const char *left, const char *right,
	size_t capacity)
{
	size_t index;

	if (!BoundedStringPresent(left, capacity) ||
		!BoundedStringPresent(right, capacity))
		return 0;
	for (index = 0U; index < capacity; index++) {
		if (left[index] != right[index])
			return 0;
		if (left[index] == '\0')
			return 1;
	}
	return 0;
}

static int LevelIdentityEqual(const sg_level_identity_t *left,
	const sg_level_identity_t *right)
{
	return left != NULL && right != NULL &&
		BoundedStringEqual(left->mapname, right->mapname,
			SG_LEVEL_IDENTITY_MAPNAME_BYTES) &&
		left->bsp_checksum == right->bsp_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_id == right->host_physics_id &&
		left->bsp_bytes == right->bsp_bytes &&
		memcmp(left->bsp_sha256, right->bsp_sha256,
			SG_LEVEL_BSP_SHA256_BYTES) == 0;
}

static int LevelIdentityMatchesModel(const sg_level_identity_t *level_identity,
	const sg_rune_compact_identity_t *model_identity)
{
	return level_identity != NULL && model_identity != NULL &&
		BoundedStringPresent(level_identity->mapname,
			SG_LEVEL_IDENTITY_MAPNAME_BYTES) &&
		level_identity->host_physics_id == SG_HOST_PHYSICS_EPOCH &&
		level_identity->bsp_checksum == model_identity->bsp_checksum &&
		level_identity->entity_crc32 == model_identity->entity_crc32 &&
		level_identity->bsp_bytes == model_identity->bsp_bytes &&
		memcmp(level_identity->bsp_sha256, model_identity->bsp_sha256,
			SG_LEVEL_BSP_SHA256_BYTES) == 0;
}

static int Current(const sg_rune_compact_learning_consumer_t *consumer)
{
	return consumer != NULL && consumer->model != NULL &&
		consumer->learning != NULL &&
		SG_RuneCompactIdentityMatches(&consumer->model->identity,
			&consumer->expected_identity);
}

static sg_rune_compact_learning_consumer_status_t ModelStatus(
	const sg_rune_compact_error_t *error)
{
	if (error != NULL &&
		error->code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED;
	if (error != NULL &&
		error->code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH;
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_MODEL;
}

static sg_rune_compact_learning_consumer_status_t IdentityStatus(
	sg_identity_status_t status)
{
	switch (status) {
	case SG_IDENTITY_OK:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_OK;
	case SG_IDENTITY_MAPNAME_MISMATCH:
	case SG_IDENTITY_BSP_CHECKSUM_MISMATCH:
	case SG_IDENTITY_ENTITY_CRC_MISMATCH:
	case SG_IDENTITY_PHYSICS_ID_MISMATCH:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH;
	case SG_IDENTITY_INVALID_ARGUMENT:
	case SG_IDENTITY_INVALID_MAPNAME:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT;
	default:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE;
	}
}

static int EventShapeValid(const sg_human_trace_v3_event_t *event)
{
	if (event == NULL || event->kind < SG_HUMAN_TRACE_V3_EVENT_STEP ||
		event->kind >= SG_HUMAN_TRACE_V3_EVENT_KIND_COUNT ||
		event->order == 0U || event->client_id == 0U ||
		event->spawn_generation == 0U || event->grounded > 1U ||
		(event->step_evidence &
			~SG_HUMAN_TRACE_V3_STEP_EVIDENCE_FLAGS_KNOWN) != 0U)
		return 0;
	if (event->kind == SG_HUMAN_TRACE_V3_EVENT_STEP)
		return event->command != 0U && event->hook_event == 0U &&
			event->after_command == 0U && event->hook_entity == 0;
	return event->command == 0U && event->hook_event != 0U &&
		event->hook_entity > 0 && event->command_msec == 0U &&
		event->step_evidence == 0U;
}

static int RootShapeValid(const collection_state_t *state,
	const sg_human_trace_v3_spool_ref_t *root)
{
	const sg_human_trace_completion_t *completion;

	if (state == NULL || root == NULL || state->level_identity == NULL)
		return 0;
	completion = &root->completion;
	return root->root_segment != UINT32_MAX && completion->session != 0U &&
		completion->session <= UINT32_MAX &&
		completion->segment != UINT32_MAX && completion->continuation <= 1U &&
		completion->end_order != 0U &&
		BytesNonzero(completion->terminal_sha256,
			SG_HUMAN_TRACE_SHA256_BYTES) &&
		BoundedStringEqual(completion->mapname,
			state->level_identity->mapname, SG_LEVEL_IDENTITY_MAPNAME_BYTES) &&
		completion->bsp_checksum == state->level_identity->bsp_checksum &&
		completion->entity_crc32 == state->level_identity->entity_crc32 &&
		completion->bsp_bytes == state->level_identity->bsp_bytes &&
		memcmp(completion->bsp_sha256, state->level_identity->bsp_sha256,
			SG_LEVEL_BSP_SHA256_BYTES) == 0 &&
		completion->host_physics_id == state->level_identity->host_physics_id &&
		completion->host_physics_id != 0U &&
		BoundedStringPresent(completion->module_version,
			SG_HUMAN_TRACE_VERSION_BYTES);
}

static int SegmentPhysicsMatchesModel(const collection_state_t *state,
	const sg_human_trace_v3_segment_ref_t *segment)
{
	const sg_rune_compact_physics_t *physics;

	if (state == NULL || segment == NULL || state->consumer == NULL ||
		state->consumer->model == NULL)
		return 0;
	physics = &state->consumer->model->identity.physics;
	return segment->gravity_bits == physics->gravity_bits &&
		segment->airaccelerate_bits == physics->air_acceleration_bits &&
		segment->maxvelocity_bits == physics->max_velocity_bits &&
		(uint32_t)segment->pmove_substep_ms == physics->substep_ms &&
		(uint32_t)segment->server_frame_ms == physics->frame_ms &&
		segment->physics_flags == 0U;
}

static int SegmentMetadataValid(const sg_human_trace_v3_segment_ref_t *segment)
{
	return segment != NULL && segment->session != 0U &&
		segment->segment != UINT32_MAX && segment->continuation <= 1U &&
		segment->physics_id == 0U && segment->identity.host_physics_id != 0U &&
		BoundedStringPresent(segment->module_version,
			SG_HUMAN_TRACE_VERSION_BYTES) && segment->start_order != 0U &&
		segment->start_command != 0U && segment->start_hook_event != 0U &&
		BytesNonzero(segment->header_sha256, SG_HUMAN_TRACE_SHA256_BYTES);
}

static int SegmentIdentityValid(const collection_state_t *state,
	const sg_human_trace_v3_segment_ref_t *segment)
{
	return state != NULL && state->level_identity != NULL && segment != NULL &&
		LevelIdentityEqual(&segment->identity, state->level_identity);
}

static int Fail(collection_state_t *state,
	sg_rune_compact_learning_consumer_status_t status)
{
	if (state != NULL && state->status == SG_RUNE_COMPACT_LEARNING_CONSUMER_OK)
		state->status = status;
	return 0;
}

static sg_rune_compact_learning_consumer_status_t LearningStatus(
	sg_rune_compact_learning_status_t status);

static int ScopeViewMatchesRoot(const collection_state_t *state,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	uint32_t *client_id_out, uint64_t *spawn_generation_out);

static int BeginRoot(void *opaque,
	const sg_human_trace_v3_spool_ref_t *root)
{
	collection_state_t *state = opaque;
	accepted_root_key_t key;

	if (state == NULL || state->root != NULL)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (!RootShapeValid(state, root))
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	state->root = root;
	state->segment = NULL;
	state->prior_order = 0U;
	state->session = 0U;
	state->last_segment_number = 0U;
	state->have_segment = 0U;
	state->have_scope = 0U;
	state->skip_root = 0U;
	RootKeyFromRoot(root, &key);
	if (RootKeyKnown(state, &key)) {
		state->skip_root = 1U;
		return 1;
	}
	if (!RootKeyListEnsure(&state->new_roots, &state->new_root_capacity,
		state->new_root_count, 1U))
		return Fail(state,
			SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED);
	state->new_roots[state->new_root_count++] = key;
	return 1;
}

static int Segment(void *opaque,
	const sg_human_trace_v3_segment_ref_t *segment)
{
	collection_state_t *state = opaque;

	if (state == NULL || state->root == NULL)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (state->skip_root)
		return 1;
	if (segment == NULL)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (!SegmentIdentityValid(state, segment))
		return Fail(state,
			SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH);
	if (!SegmentMetadataValid(segment))
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (!SegmentPhysicsMatchesModel(state, segment))
		return Fail(state,
			SG_RUNE_COMPACT_LEARNING_CONSUMER_PHYSICS_MISMATCH);
	if (segment->session != (uint32_t)state->root->completion.session)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH);
	if (!state->have_segment) {
		if (segment->segment != state->root->root_segment)
			return Fail(state,
				SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH);
		state->session = segment->session;
	} else if (segment->session != state->session ||
		segment->segment <= state->last_segment_number ||
		segment->continuation != 1U) {
		return Fail(state,
			SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH);
	}
	state->segment = segment;
	state->last_segment = *segment;
	state->last_segment_number = segment->segment;
	state->have_segment = 1U;
	return 1;
}

static int Scope(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope)
{
	collection_state_t *state = opaque;
	uint32_t client_id = 0U;
	uint64_t spawn_generation = 0U;

	if (state == NULL || state->root == NULL || scope == NULL)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (state->skip_root)
		return 1;
	if (!ScopeViewMatchesRoot(state, scope, &client_id, &spawn_generation))
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	state->have_scope = 1U;
	return 1;
}

static int ScopeViewMatchesRoot(const collection_state_t *state,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	uint32_t *client_id_out, uint64_t *spawn_generation_out)
{
	const sg_human_trace_v3_spool_ref_t *root = NULL;
	uint32_t client_id = 0U;
	uint64_t spawn_generation = 0U;

	if (state == NULL || state->root == NULL || scope == NULL ||
		client_id_out == NULL || spawn_generation_out == NULL ||
		!SG_HumanTraceAcceptedV3ScopeView(scope, &root, &client_id,
			&spawn_generation) || root != state->root || client_id == 0U ||
		spawn_generation == 0U)
		return 0;
	*client_id_out = client_id;
	*spawn_generation_out = spawn_generation;
	return 1;
}

static int RootCompletionMatchesSegment(const collection_state_t *state)
{
	const sg_human_trace_completion_t *completion;
	const sg_human_trace_v3_segment_ref_t *segment;

	if (state == NULL || state->root == NULL || !state->have_segment)
		return 0;
	completion = &state->root->completion;
	segment = &state->last_segment;
	return completion->session == (uint64_t)segment->session &&
		completion->segment == segment->segment &&
		completion->continuation == segment->continuation &&
		completion->bsp_bytes == segment->identity.bsp_bytes &&
		memcmp(completion->bsp_sha256, segment->identity.bsp_sha256,
			SG_LEVEL_BSP_SHA256_BYTES) == 0 &&
		completion->gravity_bits == segment->gravity_bits &&
		completion->airaccelerate_bits == segment->airaccelerate_bits &&
		completion->maxvelocity_bits == segment->maxvelocity_bits &&
		completion->pmove_substep_ms == segment->pmove_substep_ms &&
		completion->server_frame_ms == segment->server_frame_ms &&
		completion->physics_flags == segment->physics_flags &&
		completion->module_revision == segment->module_revision &&
		BoundedStringEqual(completion->module_version,
			segment->module_version, SG_HUMAN_TRACE_VERSION_BYTES);
}

static int Event(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event)
{
	collection_state_t *state = opaque;
	sg_rune_compact_learning_consumer_claim_t consumer_claim;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_observation_t *observation = NULL;
	sg_rune_compact_learning_issuer_t *issuer = NULL;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_consumer_validation_t validation;
	sg_rune_compact_learning_status_t learning_status;
	uint32_t client_id = 0U;
	uint64_t spawn_generation = 0U;

	if (state == NULL || state->root == NULL)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (state->skip_root)
		return 1;
	if (!state->have_segment || !state->have_scope || segment != state->segment)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (!ScopeViewMatchesRoot(state, scope, &client_id, &spawn_generation))
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (!EventShapeValid(event))
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_EVENT);
	if (event->client_id != client_id ||
		event->spawn_generation != spawn_generation)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_LIFE_MISMATCH);
	if (event->order <= state->prior_order ||
		event->order < segment->start_order ||
		event->order > state->root->completion.end_order)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH);
	if (state->report.event_count == UINT32_MAX)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	state->prior_order = event->order;
	state->report.event_count++;
	memset(&consumer_claim, 0, sizeof(consumer_claim));
	validation = state->validate(state->validate_context, state->consumer->model,
		segment, event, &consumer_claim);
	if (validation == SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP) {
		state->report.skipped_count++;
		return 1;
	}
	if (validation != SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_ENGINE_REJECTED);
	state->report.validated_count++;
	claim.key = consumer_claim.key;
	claim.value = consumer_claim.value;
	learning_status = SG_RuneCompactLearningIssuerAcquireHuman(
		state->consumer->model, &state->consumer->expected_identity, scope,
		&issuer, NULL);
	if (learning_status != SG_RUNE_COMPACT_LEARNING_OK)
		return Fail(state, LearningStatus(learning_status));
	learning_status = SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&observation);
	if (learning_status != SG_RUNE_COMPACT_LEARNING_OK) {
		SG_RuneCompactLearningIssuerDestroy(issuer);
		return Fail(state, LearningStatus(learning_status));
	}
	learning_status = SG_RuneCompactLearningApply(state->staged, observation,
		&prior);
	SG_RuneCompactLearningObservationDestroy(observation);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	if (learning_status != SG_RUNE_COMPACT_LEARNING_OK)
		return Fail(state, LearningStatus(learning_status));
	state->report.applied_count++;
	return 1;
}

static int FinishRoot(void *opaque)
{
	collection_state_t *state = opaque;

	if (state == NULL || state->root == NULL)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (state->skip_root) {
		state->root = NULL;
		state->segment = NULL;
		state->have_segment = 0U;
		state->have_scope = 0U;
		state->skip_root = 0U;
		return 1;
	}
	if (!state->have_segment)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	if (!RootCompletionMatchesSegment(state))
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_PHYSICS_MISMATCH);
	if (state->prior_order > state->root->completion.end_order)
		return Fail(state, SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH);
	state->root = NULL;
	state->segment = NULL;
	state->have_segment = 0U;
	state->have_scope = 0U;
	state->skip_root = 0U;
	return 1;
}

static sg_rune_compact_learning_consumer_status_t LearningStatus(
	sg_rune_compact_learning_status_t status)
{
	switch (status) {
	case SG_RUNE_COMPACT_LEARNING_OK:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_OK;
	case SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH;
	case SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_REFERENCE;
	case SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED;
	case SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION:
	case SG_RUNE_COMPACT_LEARNING_INVALID_VALUE:
	case SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_CLAIM;
	case SG_RUNE_COMPACT_LEARNING_INVALID_ARGUMENT:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT;
	case SG_RUNE_COMPACT_LEARNING_INVALID_MODEL:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_MODEL;
	case SG_RUNE_COMPACT_LEARNING_STATUS_COUNT:
		break;
	}
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_TRANSACTION_FAILED;
}

sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningConsumerCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_learning_consumer_t **consumer_out,
	sg_rune_compact_error_t *model_error_out)
{
	sg_rune_compact_learning_consumer_t *consumer;
	sg_rune_compact_learning_status_t status;
	sg_rune_compact_error_t local_error;
	sg_rune_compact_error_t *error = model_error_out != NULL ?
		model_error_out : &local_error;

	if (model == NULL || expected_identity == NULL || consumer_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT;
	if (!SG_RuneCompactModelValidateBound(model, expected_identity, error))
		return ModelStatus(error);
	consumer = calloc(1U, sizeof(*consumer));
	if (consumer == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED;
	status = SG_RuneCompactLearningCreate(model, expected_identity,
		&consumer->learning, error);
	if (status != SG_RUNE_COMPACT_LEARNING_OK) {
		free(consumer);
		return LearningStatus(status);
	}
	consumer->model = model;
	consumer->expected_identity = *expected_identity;
	*consumer_out = consumer;
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_OK;
}

void SG_RuneCompactLearningConsumerDestroy(
	sg_rune_compact_learning_consumer_t *consumer)
{
	if (consumer == NULL)
		return;
	SG_RuneCompactLearningDestroy(consumer->learning);
	free(consumer->processed_roots);
	free(consumer);
}

sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningConsumerIngestAcceptedV3Collection(
	sg_rune_compact_learning_consumer_t *consumer,
	const sg_level_identity_t *level_identity,
	sg_rune_compact_learning_consumer_validate_fn validate,
	void *validate_context,
	sg_rune_compact_learning_consumer_report_t *report_out)
{
	collection_state_t state;
	sg_human_trace_v3_collection_visitor_t visitor;
	sg_rune_compact_learning_status_t learning_status;
	sg_rune_compact_error_t model_error;

	if (consumer == NULL || level_identity == NULL || validate == NULL ||
		report_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT;
	if (!Current(consumer) || !LevelIdentityMatchesModel(level_identity,
		&consumer->expected_identity))
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH;
	memset(&state, 0, sizeof(state));
	state.consumer = consumer;
	state.level_identity = level_identity;
	state.validate = validate;
	state.validate_context = validate_context;
	state.status = SG_RUNE_COMPACT_LEARNING_CONSUMER_OK;
	state.report.prior_count_before =
		SG_RuneCompactLearningPriorCount(consumer->learning);
	learning_status = SG_RuneCompactLearningCreate(consumer->model,
		&consumer->expected_identity, &state.staged, &model_error);
	if (learning_status != SG_RUNE_COMPACT_LEARNING_OK)
		return LearningStatus(learning_status);
	memset(&visitor, 0, sizeof(visitor));
	visitor.begin_root = BeginRoot;
	visitor.segment = Segment;
	visitor.scope = Scope;
	visitor.event = Event;
	visitor.finish_root = FinishRoot;
	{
		sg_human_trace_visit_status_t visit_status =
			SG_HumanTraceVisitAcceptedV3CollectionStatus(level_identity, &visitor,
				&state);

		if (visit_status != SG_HUMAN_TRACE_VISIT_OK) {
			if (state.status == SG_RUNE_COMPACT_LEARNING_CONSUMER_OK)
				state.status = visit_status ==
					SG_HUMAN_TRACE_VISIT_ALLOCATION_FAILED
					? SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED
					: SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE;
			goto failed;
		}
	}
	if (state.root != NULL || state.have_segment || state.have_scope) {
		state.status = SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE;
		goto failed;
	}
	if (state.new_root_count != 0U &&
		!RootKeyListEnsure(&consumer->processed_roots,
			&consumer->processed_root_capacity,
			consumer->processed_root_count, state.new_root_count)) {
		state.status = SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED;
		goto failed;
	}
	if (state.report.applied_count != 0U) {
		learning_status = SG_RuneCompactLearningMerge(consumer->learning,
			state.staged);
		if (learning_status != SG_RUNE_COMPACT_LEARNING_OK) {
			state.status = LearningStatus(learning_status);
			goto failed;
		}
	}
	if (state.new_root_count != 0U) {
		memcpy(&consumer->processed_roots[consumer->processed_root_count],
			state.new_roots,
			(size_t)state.new_root_count * sizeof(*state.new_roots));
		consumer->processed_root_count += state.new_root_count;
	}
	state.report.prior_count_after =
		SG_RuneCompactLearningPriorCount(consumer->learning);
	SG_RuneCompactLearningDestroy(state.staged);
	free(state.new_roots);
	*report_out = state.report;
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_OK;

failed:
	SG_RuneCompactLearningDestroy(state.staged);
	free(state.new_roots);
	return state.status;
}

sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningConsumerIngestCurrentV3Collection(
	sg_rune_compact_learning_consumer_t *consumer,
	const char *expected_mapname,
	sg_rune_compact_learning_consumer_validate_fn validate,
	void *validate_context,
	sg_rune_compact_learning_consumer_report_t *report_out)
{
	sg_level_identity_t level_identity;
	sg_identity_status_t identity_status;

	if (consumer == NULL || expected_mapname == NULL ||
		expected_mapname[0] == '\0' || validate == NULL || report_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT;
	identity_status = SG_LevelIdentitySnapshot(expected_mapname,
		&level_identity);
	if (identity_status != SG_IDENTITY_OK)
		return IdentityStatus(identity_status);
	return SG_RuneCompactLearningConsumerIngestAcceptedV3Collection(consumer,
		&level_identity, validate, validate_context, report_out);
}

uint32_t SG_RuneCompactLearningConsumerPriorCount(
	const sg_rune_compact_learning_consumer_t *consumer)
{
	return Current(consumer) ? SG_RuneCompactLearningPriorCount(
		consumer->learning) : 0U;
}

int SG_RuneCompactLearningConsumerPriorRead(
	const sg_rune_compact_learning_consumer_t *consumer, uint32_t index,
	sg_rune_compact_learning_prior_t *prior_out)
{
	return Current(consumer) ? SG_RuneCompactLearningPriorRead(
		consumer->learning, index, prior_out) : 0;
}

const char *SG_RuneCompactLearningConsumerStatusString(
	sg_rune_compact_learning_consumer_status_t status)
{
	switch (status) {
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_OK:
		return "ok";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_MODEL:
		return "invalid compact model";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH:
		return "compact model identity mismatch";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_PHYSICS_MISMATCH:
		return "trace segment physics mismatch";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE:
		return "invalid trace";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_LIFE_MISMATCH:
		return "trace life mismatch";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH:
		return "trace order mismatch";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_EVENT:
		return "invalid trace event";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_ENGINE_REJECTED:
		return "engine rejected sample";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_CLAIM:
		return "invalid learning claim";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_REFERENCE:
		return "learning reference is not in compact model";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED:
		return "allocation failed";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_TRANSACTION_FAILED:
		return "learning transaction failed";
	case SG_RUNE_COMPACT_LEARNING_CONSUMER_STATUS_COUNT:
		break;
	}
	return "unknown compact learning consumer status";
}
