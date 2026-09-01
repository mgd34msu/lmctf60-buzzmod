#include "sg_strategy_runtime_bridge.h"
#include "sg_strategy_runtime_bridge_private.h"
#include "sg_authority_entropy.h"
#include "sg_rune_compact_mechanisms.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct sg_strategy_runtime_target_view_s
{
	const void *opaque;
} sg_strategy_runtime_target_view_t;

typedef int (*sg_strategy_runtime_target_locator_fn)(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_runtime_target_view_t *view_out);
typedef int (*sg_strategy_runtime_target_authority_fn)(void *context,
	const sg_strategy_runtime_target_request_t *request,
	const sg_strategy_runtime_target_view_t *view,
	sg_strategy_caller_target_binding_t *binding_out);
typedef void (*sg_strategy_runtime_target_release_fn)(void *context,
	const void *accepted_view);

/* Registration is one capability.  A resolver snapshots the entire object
 * before it calls untrusted code, then compares its non-wrapping identity
 * after every callback.  Replacing just one callback/context pair must never
 * make an old borrowed view cross into the new authority or release owner. */
typedef struct sg_strategy_runtime_provider_registration_s
{
	sg_strategy_runtime_target_locator_fn locator;
	void *locator_context;
	sg_strategy_runtime_target_authority_fn authority;
	void *authority_context;
	sg_strategy_runtime_target_release_fn release_view;
	void *release_context;
	uint64_t identity;
} sg_strategy_runtime_provider_registration_t;

static sg_strategy_runtime_provider_registration_t sg_strategy_runtime_provider;
static sg_rune_compact_field_service_t *sg_strategy_runtime_compact_service;
static sg_strategy_runtime_bot_observation_owner_t
	sg_strategy_runtime_bot_observation_owner;
static uint64_t sg_strategy_runtime_provider_next_identity = 1U;

typedef struct sg_strategy_runtime_query_authority_s
{
	const sg_strategy_caller_t *caller;
	uint64_t provider_identity;
	uint64_t issuance;
	uint8_t token[16];
	sg_strategy_caller_output_proof_t output_proof;
	sg_strategy_caller_output_receipt_t output_receipt;
	sg_strategy_runtime_caller_query_snapshot_t snapshot;
	sg_rune_compact_field_result_t field_result;
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms;
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots;
	uint8_t active;
	uint8_t receipt_validated;
} sg_strategy_runtime_query_authority_t;

static sg_strategy_runtime_query_authority_t
	sg_strategy_runtime_query_authority;
static uint64_t sg_strategy_runtime_query_next_issuance = UINT64_C(1);

_Static_assert(sizeof(uint64_t) + sizeof(uint64_t) + 16U <=
	SG_STRATEGY_RUNTIME_CALLER_QUERY_PROOF_BYTES,
	"strategy runtime query proof storage is too small");

static void RuntimeQueryAuthorityClear(void)
{
	memset(&sg_strategy_runtime_query_authority, 0,
		sizeof(sg_strategy_runtime_query_authority));
}

static int RuntimeBytesNonzero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0U;

	if (bytes == NULL)
		return 0;
	for (size_t index = 0U; index < size; index++)
		combined = (uint8_t)(combined | bytes[index]);
	return combined != 0U;
}

static int RuntimeBytesEqual(const uint8_t *left, const uint8_t *right,
	size_t size)
{
	uint8_t difference = 0U;

	if (left == NULL || right == NULL)
		return 0;
	for (size_t index = 0U; index < size; index++)
		difference = (uint8_t)(difference | (uint8_t)(left[index] ^
			right[index]));
	return difference == 0U;
}

static void RuntimeQueryProofEncode(
	sg_strategy_runtime_caller_query_proof_t *proof,
	const uint8_t token[16])
{
	uint64_t provider_identity =
		sg_strategy_runtime_query_authority.provider_identity;
	uint64_t issuance = sg_strategy_runtime_query_authority.issuance;

	memset(proof, 0, sizeof(*proof));
	memcpy(&proof->opaque[0], &provider_identity, sizeof(provider_identity));
	memcpy(&proof->opaque[sizeof(provider_identity)], &issuance,
		sizeof(issuance));
	memcpy(&proof->opaque[sizeof(provider_identity) + sizeof(issuance)],
		token, 16U);
}

static int RuntimeQueryProofMatches(
	const sg_strategy_runtime_caller_query_proof_t *proof)
{
	uint64_t encoded_provider_identity = 0U;
	uint64_t encoded_issuance = 0U;
	const size_t token_offset = sizeof(uint64_t) + sizeof(uint64_t);

	if (proof == NULL || !sg_strategy_runtime_query_authority.active)
		return 0;
	memcpy(&encoded_provider_identity, &proof->opaque[0],
		sizeof(encoded_provider_identity));
	memcpy(&encoded_issuance,
		&proof->opaque[sizeof(encoded_provider_identity)],
		sizeof(encoded_issuance));
	return encoded_provider_identity ==
			sg_strategy_runtime_query_authority.provider_identity &&
		encoded_issuance == sg_strategy_runtime_query_authority.issuance &&
		RuntimeBytesEqual(&proof->opaque[token_offset],
			sg_strategy_runtime_query_authority.token, 16U);
}

static int RuntimeProviderRegistrationAvailable(
	const sg_strategy_runtime_provider_registration_t *registration)
{
	return registration != NULL && registration->identity != 0U &&
		registration->locator != NULL && registration->authority != NULL &&
		registration->release_view != NULL;
}

static int RuntimeProviderRegistrationCurrent(
	const sg_strategy_runtime_provider_registration_t *registration)
{
	return registration != NULL && registration->identity != 0U &&
		registration->identity == sg_strategy_runtime_provider.identity;
}

static void RuntimeProviderRegistrationReplace(
	sg_strategy_runtime_target_locator_fn locator, void *locator_context,
	sg_strategy_runtime_target_authority_fn authority, void *authority_context,
	sg_strategy_runtime_target_release_fn release_view, void *release_context)
{
	sg_strategy_runtime_provider_registration_t registration;

	RuntimeQueryAuthorityClear();
	/* Do not wrap an identity and accidentally authenticate an ancient
	 * resolver snapshot.  Once the finite identity space is spent, every
	 * provider operation remains unavailable until process restart. */
	if (sg_strategy_runtime_provider_next_identity == UINT64_MAX)
	{
		memset(&sg_strategy_runtime_provider, 0,
			sizeof(sg_strategy_runtime_provider));
		sg_strategy_runtime_compact_service = NULL;
		memset(&sg_strategy_runtime_bot_observation_owner, 0,
			sizeof(sg_strategy_runtime_bot_observation_owner));
		return;
	}
	memset(&registration, 0, sizeof(registration));
	registration.identity = sg_strategy_runtime_provider_next_identity;
	sg_strategy_runtime_provider_next_identity++;
	if (locator != NULL && authority != NULL && release_view != NULL)
	{
		registration.locator = locator;
		registration.locator_context = locator_context;
		registration.authority = authority;
		registration.authority_context = authority_context;
		registration.release_view = release_view;
		registration.release_context = release_context;
	}
	sg_strategy_runtime_provider = registration;
	sg_strategy_runtime_compact_service = NULL;
	memset(&sg_strategy_runtime_bot_observation_owner, 0,
		sizeof(sg_strategy_runtime_bot_observation_owner));
}

static int RuntimeBotObservationOwnerAvailable(
	const sg_strategy_runtime_bot_observation_owner_t *owner)
{
	return owner != NULL && owner->context != NULL &&
		owner->validate != NULL && owner->current != NULL;
}

static int RuntimeBotObservationOwnerCurrent(
	const sg_strategy_runtime_bot_observation_owner_t *owner)
{
	return RuntimeBotObservationOwnerAvailable(owner) &&
		owner->context == sg_strategy_runtime_bot_observation_owner.context &&
		owner->validate == sg_strategy_runtime_bot_observation_owner.validate &&
		owner->current == sg_strategy_runtime_bot_observation_owner.current;
}

static int RuntimeAuthorityValid(
	const sg_strategy_caller_authority_t *authority)
{
	if (authority == NULL || authority->principal_id == 0U)
		return 0;
	switch (authority->rank)
	{
	case SG_STRATEGY_AUTHORITY_AUTONOMOUS:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	case SG_STRATEGY_AUTHORITY_TEAM:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_TEAM;
	case SG_STRATEGY_AUTHORITY_HUMAN:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_HUMAN;
	case SG_STRATEGY_AUTHORITY_EMERGENCY:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_EMERGENCY;
	default:
		return 0;
	}
}

static int RuntimeAuthorityEqual(
	const sg_strategy_caller_authority_t *left,
	const sg_strategy_caller_authority_t *right)
{
	return left != NULL && right != NULL && left->rank == right->rank &&
		left->principal_kind == right->principal_kind &&
		left->principal_id == right->principal_id;
}

static int RuntimeDestinationEqual(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (left == NULL || right == NULL || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_DESTINATION_FLAG:
		return left->value.flag.team == right->value.flag.team &&
			left->value.flag.location == right->value.flag.location;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return left->value.item.item_id == right->value.item.item_id;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return left->value.carrier.client_id ==
				right->value.carrier.client_id &&
			left->value.carrier.team == right->value.carrier.team &&
			left->value.carrier.selector == right->value.carrier.selector;
	case SG_DESTINATION_DEFENSIVE_POST:
		return left->value.post.region_id == right->value.post.region_id;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return left->value.point.point_id == right->value.point.point_id;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int RuntimeCompactDestinationEqual(
	const sg_rune_compact_destination_t *left,
	const sg_rune_compact_destination_t *right)
{
	if (left == NULL || right == NULL || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_DESTINATION_POINT:
		return left->value.point.value[0] == right->value.point.value[0] &&
			left->value.point.value[1] == right->value.point.value[1] &&
			left->value.point.value[2] == right->value.point.value[2];
	case SG_RUNE_COMPACT_DESTINATION_CELL:
		return left->value.cell.value == right->value.cell.value;
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
		return left->value.surface.value == right->value.surface.value;
	case SG_RUNE_COMPACT_DESTINATION_ITEM:
		return left->value.item.value == right->value.item.value;
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

typedef struct sg_strategy_runtime_digest_s
{
	uint64_t value[2];
} sg_strategy_runtime_digest_t;

static int RuntimeFloatEqual(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static void RuntimeDigestInit(sg_strategy_runtime_digest_t *digest)
{
	digest->value[0] = UINT64_C(1469598103934665603);
	digest->value[1] = UINT64_C(7809847782465536322);
}

static void RuntimeDigestByte(sg_strategy_runtime_digest_t *digest,
	uint8_t value)
{
	digest->value[0] = (digest->value[0] ^ value) *
		UINT64_C(1099511628211);
	digest->value[1] ^= (uint64_t)value + UINT64_C(0x9e3779b97f4a7c15) +
		(digest->value[1] << 6U) + (digest->value[1] >> 2U);
}

static void RuntimeDigestU32(sg_strategy_runtime_digest_t *digest,
	uint32_t value)
{
	for (uint32_t shift = 0U; shift < 32U; shift += 8U)
		RuntimeDigestByte(digest, (uint8_t)(value >> shift));
}

static void RuntimeDigestU64(sg_strategy_runtime_digest_t *digest,
	uint64_t value)
{
	for (uint32_t shift = 0U; shift < 64U; shift += 8U)
		RuntimeDigestByte(digest, (uint8_t)(value >> shift));
}

static void RuntimeDigestFloat(sg_strategy_runtime_digest_t *digest,
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	RuntimeDigestU32(digest, bits);
}

static int RuntimeMechanismDigest(
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms,
	uint64_t digest_out[2])
{
	sg_strategy_runtime_digest_t digest;

	if (digest_out == NULL)
		return 0;
	RuntimeDigestInit(&digest);
	RuntimeDigestByte(&digest, mechanisms != NULL ? UINT8_C(1) : UINT8_C(0));
	if (mechanisms != NULL)
	{
		if (mechanisms->model_identity == NULL ||
			(mechanisms->phase_count != 0U && mechanisms->phases == NULL))
			return 0;
		RuntimeDigestU64(&digest, mechanisms->frame_sequence);
		RuntimeDigestU32(&digest, mechanisms->phase_count);
		for (uint32_t index = 0U; index < mechanisms->phase_count; index++)
		{
			RuntimeDigestU32(&digest,
				mechanisms->phases[index].mechanism.value);
			RuntimeDigestFloat(&digest, mechanisms->phases[index].phase);
		}
	}
	digest_out[0] = digest.value[0];
	digest_out[1] = digest.value[1];
	return 1;
}

static int RuntimePortalRootDigest(
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots,
	uint64_t digest_out[2])
{
	sg_strategy_runtime_digest_t digest;

	if (digest_out == NULL)
		return 0;
	RuntimeDigestInit(&digest);
	RuntimeDigestByte(&digest, portal_roots != NULL ? UINT8_C(1) : UINT8_C(0));
	if (portal_roots != NULL)
	{
		if (portal_roots->model_identity == NULL ||
			(portal_roots->root_count != 0U && portal_roots->roots == NULL))
			return 0;
		RuntimeDigestU64(&digest, portal_roots->frame_sequence);
		RuntimeDigestU32(&digest, portal_roots->root_count);
		for (uint32_t index = 0U; index < portal_roots->root_count; index++)
		{
			RuntimeDigestU32(&digest,
				portal_roots->roots[index].portal.value);
			RuntimeDigestU32(&digest,
				portal_roots->roots[index].mechanism.value);
			RuntimeDigestU32(&digest,
				(uint32_t)portal_roots->roots[index].state);
		}
	}
	digest_out[0] = digest.value[0];
	digest_out[1] = digest.value[1];
	return 1;
}

static int RuntimeQuerySnapshotBuild(
	const sg_rune_compact_field_local_context_t *context,
	sg_strategy_runtime_caller_query_snapshot_t *snapshot_out)
{
	if (context == NULL || snapshot_out == NULL)
		return 0;
	memset(snapshot_out, 0, sizeof(*snapshot_out));
	for (uint32_t axis = 0U; axis < 3U; axis++)
	{
		snapshot_out->origin.value[axis] = context->origin.value[axis];
		snapshot_out->velocity[axis] = context->velocity[axis];
		snapshot_out->direction[axis] = context->direction[axis];
	}
	snapshot_out->stance = context->stance;
	snapshot_out->support = context->support;
	snapshot_out->water = context->water;
	snapshot_out->hook_phase = context->hook_phase;
	snapshot_out->state_flags = context->state_flags;
	snapshot_out->mover_mechanism = context->mover_mechanism;
	snapshot_out->time_seconds = context->time_seconds;
	snapshot_out->distance = context->distance;
	snapshot_out->support_distance = context->support_distance;
	snapshot_out->fluid_fraction = context->fluid_fraction;
	snapshot_out->hook_length = context->hook_length;
	snapshot_out->target_radius = context->target_radius;
	snapshot_out->frame_sequence = context->frame_sequence;
	return RuntimeMechanismDigest(context->mechanisms,
			snapshot_out->mechanism_digest) &&
		RuntimePortalRootDigest(context->portal_roots,
			snapshot_out->portal_root_digest);
}

static int RuntimeQuerySnapshotEqual(
	const sg_strategy_runtime_caller_query_snapshot_t *left,
	const sg_strategy_runtime_caller_query_snapshot_t *right)
{
	if (left == NULL || right == NULL || left->stance != right->stance ||
		left->support != right->support || left->water != right->water ||
		left->hook_phase != right->hook_phase ||
		left->state_flags != right->state_flags ||
		left->mover_mechanism != right->mover_mechanism ||
		left->frame_sequence != right->frame_sequence)
		return 0;
	for (uint32_t axis = 0U; axis < 3U; axis++)
		if (left->origin.value[axis] != right->origin.value[axis] ||
			!RuntimeFloatEqual(left->velocity[axis], right->velocity[axis]) ||
			!RuntimeFloatEqual(left->direction[axis], right->direction[axis]))
			return 0;
	return RuntimeFloatEqual(left->time_seconds, right->time_seconds) &&
		RuntimeFloatEqual(left->distance, right->distance) &&
		RuntimeFloatEqual(left->support_distance, right->support_distance) &&
		RuntimeFloatEqual(left->fluid_fraction, right->fluid_fraction) &&
		RuntimeFloatEqual(left->hook_length, right->hook_length) &&
		RuntimeFloatEqual(left->target_radius, right->target_radius) &&
		left->mechanism_digest[0] == right->mechanism_digest[0] &&
		left->mechanism_digest[1] == right->mechanism_digest[1] &&
		left->portal_root_digest[0] == right->portal_root_digest[0] &&
		left->portal_root_digest[1] == right->portal_root_digest[1];
}

static void RuntimeQuerySnapshotContext(
	const sg_strategy_runtime_caller_query_snapshot_t *snapshot,
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms,
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots,
	sg_rune_compact_field_local_context_t *context_out)
{
	memset(context_out, 0, sizeof(*context_out));
	for (uint32_t axis = 0U; axis < 3U; axis++)
	{
		context_out->origin.value[axis] = snapshot->origin.value[axis];
		context_out->velocity[axis] = snapshot->velocity[axis];
		context_out->direction[axis] = snapshot->direction[axis];
	}
	context_out->stance = snapshot->stance;
	context_out->support = snapshot->support;
	context_out->water = snapshot->water;
	context_out->hook_phase = snapshot->hook_phase;
	context_out->state_flags = snapshot->state_flags;
	context_out->mover_mechanism = snapshot->mover_mechanism;
	context_out->time_seconds = snapshot->time_seconds;
	context_out->distance = snapshot->distance;
	context_out->support_distance = snapshot->support_distance;
	context_out->fluid_fraction = snapshot->fluid_fraction;
	context_out->hook_length = snapshot->hook_length;
	context_out->target_radius = snapshot->target_radius;
	context_out->frame_sequence = snapshot->frame_sequence;
	context_out->mechanisms = mechanisms;
	context_out->portal_roots = portal_roots;
}

static int RuntimeFieldResultEqual(
	const sg_rune_compact_field_result_t *left,
	const sg_rune_compact_field_result_t *right)
{
	if (left == NULL || right == NULL || left->kind != right->kind ||
		left->current_cell.value != right->current_cell.value)
		return 0;
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_FIELD_DISCONNECTED:
	case SG_RUNE_COMPACT_FIELD_BLOCKED_NOW:
		return 1;
	case SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION:
	case SG_RUNE_COMPACT_FIELD_CELL_DESTINATION:
		return RuntimeCompactDestinationEqual(&left->value.destination,
			&right->value.destination);
	case SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED:
		if (left->value.requirements.portal.value !=
				right->value.requirements.portal.value ||
			left->value.requirements.mechanism_count !=
				right->value.requirements.mechanism_count ||
			left->value.requirements.state != right->value.requirements.state ||
			(left->value.requirements.mechanism_count != 0U &&
			 (left->value.requirements.mechanisms == NULL ||
			  right->value.requirements.mechanisms == NULL)))
			return 0;
		for (uint32_t index = 0U;
			index < left->value.requirements.mechanism_count; index++)
			if (left->value.requirements.mechanisms[index].value !=
				right->value.requirements.mechanisms[index].value)
				return 0;
		return 1;
	case SG_RUNE_COMPACT_FIELD_STEP:
		if (left->value.step.kind != right->value.step.kind ||
			left->value.step.cost_to_go.units !=
				right->value.step.cost_to_go.units ||
			left->value.step.next_cost_to_go.units !=
				right->value.step.next_cost_to_go.units ||
			left->value.step.target_stance != right->value.step.target_stance)
			return 0;
		switch (left->value.step.kind)
		{
		case SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL:
			return RuntimeFloatEqual(left->value.step.value.portal.local_cost,
				right->value.step.value.portal.local_cost) &&
				left->value.step.value.portal.next_cell.value ==
					right->value.step.value.portal.next_cell.value &&
				left->value.step.value.portal.next_portal.value ==
					right->value.step.value.portal.next_portal.value;
		case SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT:
			return RuntimeFloatEqual(left->value.step.value.direct.local_cost,
				right->value.step.value.direct.local_cost) &&
				left->value.step.value.direct.next_cell.value ==
					right->value.step.value.direct.next_cell.value;
		case SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE:
			return 1;
		case SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT:
		default:
			return 0;
		}
	case SG_RUNE_COMPACT_FIELD_RESULT_KIND_COUNT:
	default:
		return 0;
	}
}

static int RuntimeQueryAuthorityLiveCurrent(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_rune_compact_field_result_t *field_result)
{
	sg_rune_compact_field_local_context_t context;
	sg_strategy_runtime_caller_query_snapshot_t current_snapshot;
	sg_rune_compact_field_result_t current_result;

	memset(&context, 0, sizeof(context));
	memset(&current_snapshot, 0, sizeof(current_snapshot));
	memset(&current_result, 0, sizeof(current_result));
	if (caller == NULL || output == NULL || field_result == NULL ||
		caller->plan.mechanisms !=
			sg_strategy_runtime_query_authority.mechanisms ||
		caller->plan.portal_roots !=
			sg_strategy_runtime_query_authority.portal_roots)
		return 0;
	RuntimeQuerySnapshotContext(&sg_strategy_runtime_query_authority.snapshot,
		sg_strategy_runtime_query_authority.mechanisms,
		sg_strategy_runtime_query_authority.portal_roots, &context);
	return RuntimeQuerySnapshotBuild(&context, &current_snapshot) &&
		RuntimeQuerySnapshotEqual(&current_snapshot,
			&sg_strategy_runtime_query_authority.snapshot) &&
		RuntimeFieldResultEqual(field_result,
			&sg_strategy_runtime_query_authority.field_result) &&
		SG_RuneCompactFieldServiceQuery(output->field_service,
			&output->field_handle, &context, &current_result) ==
				SG_RUNE_COMPACT_FIELD_SERVICE_OK &&
		RuntimeFieldResultEqual(&current_result, field_result);
}

static int RuntimeCompactTargetEqual(
	const sg_rune_compact_field_target_t *left,
	const sg_rune_compact_field_target_t *right)
{
	return left != NULL && right != NULL && left->target_id == right->target_id &&
		left->target_generation == right->target_generation &&
		left->motion == right->motion && RuntimeCompactDestinationEqual(
			&left->destination, &right->destination) &&
		RuntimeDestinationEqual(&left->semantic_destination,
			&right->semantic_destination);
}

static int RuntimeLocalizedPlayerValid(
	const sg_compact_localized_state_t *player)
{
	uint32_t axis;

	if (player == NULL || player->valid != 1U ||
		player->subject.reserved != 0U ||
		player->subject.client_id == UINT32_MAX ||
		player->subject.spawn_generation == 0U || player->rune_identity == 0U ||
		player->topology_revision == 0U || player->frame_sequence == 0U ||
		player->localized_at_ms == 0U ||
		player->location.cell.value == SG_RUNE_COMPACT_INDEX_NONE ||
		player->stance >= SG_RUNE_STANCE_COUNT)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_DestinationFloatValid(player->position[axis]) ||
			!SG_DestinationFloatValid(player->velocity[axis]))
			return 0;
	return player->water_level <= 3U;
}

static int RuntimeBotObservationViewValid(
	const sg_strategy_runtime_bot_observation_view_t *view,
	const sg_compact_localized_state_t *localized)
{
	return view != NULL && RuntimeLocalizedPlayerValid(localized) &&
		view->subject.reserved == 0U &&
		view->subject.client_id == localized->subject.client_id &&
		view->subject.spawn_generation ==
			localized->subject.spawn_generation &&
		view->host_authority_epoch != 0U &&
		view->frame_sequence == localized->frame_sequence &&
		view->observed_at_ms == localized->localized_at_ms &&
		(uint32_t)view->hook_phase <= (uint32_t)SG_HOST_HOOK_COAST &&
		isfinite(view->hook_length) && view->hook_length >= 0.0f &&
		isfinite(view->target_radius) && view->target_radius >= 0.0f;
}

static int RuntimeBotObservationEqual(
	const sg_strategy_caller_bot_observation_t *snapshot,
	const sg_strategy_runtime_bot_observation_view_t *view)
{
	return snapshot != NULL && view != NULL &&
		snapshot->life_identity.client_id == view->subject.client_id &&
		snapshot->life_identity.reserved == view->subject.reserved &&
		snapshot->life_identity.spawn_generation ==
			view->subject.spawn_generation &&
		snapshot->host_authority_epoch == view->host_authority_epoch &&
		snapshot->frame_sequence == view->frame_sequence &&
		snapshot->observed_at_ms == view->observed_at_ms &&
		snapshot->hook_phase == view->hook_phase &&
		memcmp(&snapshot->hook_length, &view->hook_length,
			sizeof(snapshot->hook_length)) == 0 &&
		memcmp(&snapshot->target_radius, &view->target_radius,
			sizeof(snapshot->target_radius)) == 0;
}

static void RuntimeBotObservationCopy(
	const sg_strategy_runtime_bot_observation_view_t *view,
	sg_strategy_caller_bot_observation_t *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->life_identity.client_id = view->subject.client_id;
	snapshot->life_identity.reserved = view->subject.reserved;
	snapshot->life_identity.spawn_generation = view->subject.spawn_generation;
	snapshot->host_authority_epoch = view->host_authority_epoch;
	snapshot->frame_sequence = view->frame_sequence;
	snapshot->observed_at_ms = view->observed_at_ms;
	snapshot->hook_phase = view->hook_phase;
	snapshot->hook_length = view->hook_length;
	snapshot->target_radius = view->target_radius;
}

/* The bridge validates the complete compact capability chain returned by an
 * authority.  This is intentionally independent of the concrete provider:
 * custom owners and the production compact adapter receive the same checks. */
static int RuntimeBindingAccepted(
	const sg_strategy_runtime_target_request_t *target,
	const sg_strategy_runtime_target_view_t *view,
	const sg_strategy_caller_target_binding_t *binding)
{
	sg_rune_compact_field_target_t current_target;
	uint32_t current_region;
	uint64_t current_region_epoch;

	if (target == NULL || !RuntimeLocalizedPlayerValid(target->localized_player) ||
		view == NULL || view->opaque == NULL || binding == NULL ||
		binding->commitment_id != target->commitment_id ||
		!RuntimeAuthorityEqual(&binding->authority, &target->authority) ||
		binding->goal_id != target->goal_id ||
		binding->target_id != target->target_id ||
		!RuntimeDestinationEqual(&binding->destination, &target->destination) ||
		binding->role != target->role || binding->accepted_view != view->opaque ||
		binding->field_service == NULL || binding->compact_target.target_id !=
			binding->target_id || binding->compact_target.target_generation == 0U ||
		binding->compact_target.motion >=
			SG_RUNE_COMPACT_FIELD_TARGET_MOTION_COUNT ||
		binding->field_handle.rune_identity !=
			target->localized_player->rune_identity ||
		binding->field_handle.topology_revision !=
			target->localized_player->topology_revision ||
		binding->field_handle.target_id != binding->target_id ||
		binding->field_handle.target_generation !=
			binding->compact_target.target_generation ||
		!SG_RuneCompactFieldServiceHandleCurrent(binding->field_service,
			&binding->field_handle, &current_target, &current_region,
			&current_region_epoch) ||
		!RuntimeCompactTargetEqual(&current_target, &binding->compact_target))
		return 0;
	(void)current_region;
	(void)current_region_epoch;
	return 1;
}

static int RuntimeQ8FromFloat(float value, int32_t *out)
{
	double scaled;
	long long rounded;

	if (out == NULL || !isfinite(value))
		return 0;
	scaled = (double)value * 8.0;
	if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
		scaled > (double)INT32_MAX)
		return 0;
	rounded = llround(scaled);
	if (rounded < (long long)INT32_MIN || rounded > (long long)INT32_MAX)
		return 0;
	*out = (int32_t)rounded;
	return 1;
}

static int RuntimeLocalContext(
	const sg_rune_compact_model_t *model,
	const sg_compact_localized_state_t *localized,
	const sg_strategy_caller_bot_observation_t *bot_observation,
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms,
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots,
	sg_rune_compact_field_local_context_t *context_out)
{
	double speed_squared = 0.0;
	double speed;
	uint32_t mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t axis;
	uint32_t transition_index;

	if (model == NULL || !RuntimeLocalizedPlayerValid(localized) ||
	    bot_observation == NULL || context_out == NULL ||
	    bot_observation->life_identity.client_id !=
		localized->subject.client_id ||
	    bot_observation->life_identity.reserved != localized->subject.reserved ||
	    bot_observation->life_identity.spawn_generation !=
		localized->subject.spawn_generation ||
	    bot_observation->host_authority_epoch == 0U ||
	    bot_observation->frame_sequence != localized->frame_sequence ||
	    bot_observation->observed_at_ms != localized->localized_at_ms ||
	    (uint32_t)bot_observation->hook_phase >
		(uint32_t)SG_HOST_HOOK_COAST ||
	    !isfinite(bot_observation->hook_length) ||
	    bot_observation->hook_length < 0.0f ||
	    !isfinite(bot_observation->target_radius) ||
	    bot_observation->target_radius < 0.0f)
		return 0;
	memset(context_out, 0, sizeof(*context_out));
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!RuntimeQ8FromFloat(localized->position[axis],
			&context_out->origin.value[axis]))
			return 0;
		speed_squared += (double)localized->velocity[axis] *
			(double)localized->velocity[axis];
	}
	if (!isfinite(speed_squared) || speed_squared < 0.0)
		return 0;
	speed = sqrt(speed_squared);
	context_out->stance = localized->stance == SG_RUNE_STANCE_CROUCHING ?
		SG_RUNE_COMPACT_FIELD_CROUCHING : SG_RUNE_COMPACT_FIELD_STANDING;
	if (localized->support == SG_RUNE_SUPPORT_MOVER ||
	    localized->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE)
	{
		if (localized->support != SG_RUNE_SUPPORT_MOVER ||
		    localized->reference_frame != SG_RUNE_FRAME_MOVER_RELATIVE ||
		    localized->support_model_index == SG_RUNE_COMPACT_INDEX_NONE ||
		    model->mechanism_authority_transitions == NULL)
			return 0;
		for (transition_index = 0U;
		     transition_index < model->mechanism_authority_transition_count;
		     transition_index++)
		{
			const sg_rune_compact_mechanism_transition_t *transition =
				&model->mechanism_authority_transitions[transition_index];

			if (transition->kind !=
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT ||
			    transition->value.transport.mover_model !=
				localized->support_model_index)
				continue;
			if (transition->mechanism >= model->mechanism_authority_count ||
			    (mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE &&
			     mover_mechanism != transition->mechanism))
				return 0;
			mover_mechanism = transition->mechanism;
		}
		if (mover_mechanism == SG_RUNE_COMPACT_INDEX_NONE)
			return 0;
		context_out->support = SG_RUNE_MOVEMENT_SUPPORT_MOVER;
	}
	else
		context_out->support = localized->support == SG_RUNE_SUPPORT_SUPPORTED ?
			SG_RUNE_MOVEMENT_SUPPORT_STATIC : SG_RUNE_MOVEMENT_SUPPORT_NONE;
	context_out->water = localized->water_level == 0U ?
		SG_RUNE_MOVEMENT_WATER_DRY : localized->water_level >= 3U ?
			SG_RUNE_MOVEMENT_WATER_SUBMERGED :
			SG_RUNE_MOVEMENT_WATER_PARTIAL;
	context_out->hook_phase = bot_observation->hook_phase;
	context_out->state_flags = localized->motion == SG_RUNE_MOTION_AIRBORNE ?
		SG_RUNE_MOVEMENT_STATE_AIRBORNE : 0U;
	if (localized->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE)
		context_out->state_flags |= SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
	context_out->mover_mechanism = mover_mechanism;
	for (axis = 0U; axis < 3U; axis++)
	{
		context_out->velocity[axis] = localized->velocity[axis];
		context_out->direction[axis] = speed > 0.0 ?
			(float)((double)localized->velocity[axis] / speed) : 0.0f;
	}
	context_out->time_seconds = 0.0f;
	context_out->distance = 0.0f;
	context_out->support_distance = 0.0f;
	context_out->fluid_fraction = (float)localized->water_level / 3.0f;
	context_out->hook_length = bot_observation->hook_length;
	context_out->target_radius = bot_observation->target_radius;
	context_out->frame_sequence = localized->frame_sequence;
	context_out->mechanisms = mechanisms;
	context_out->portal_roots = portal_roots;
	return 1;
}

static int RuntimeExecutionFor(const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_goal_id_t goal_id, sg_strategy_target_id_t target_id,
	const sg_strategy_runtime_execution_t **execution_out)
{
	const sg_strategy_runtime_execution_t *found = NULL;
	uint16_t index;

	if (request == NULL || execution_out == NULL)
		return 0;
	for (index = 0U; index < request->execution_count; index++)
	{
		const sg_strategy_runtime_execution_t *execution =
			&request->executions[index];

		if (execution->goal_id != goal_id || execution->target_id != target_id)
			continue;
		if (found != NULL)
			return 0;
		found = execution;
	}
	if (found == NULL)
		return 0;
	*execution_out = found;
	return 1;
}

static int RuntimeRequestCompile(const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_plan_t *compiled)
{
	sg_strategy_compile_error_t error;
	sg_strategy_plan_spec_t spec;
	uint16_t target_count = 0U;
	uint16_t goal_index;

	if (request == NULL || compiled == NULL || request->commitment_id == 0U ||
		!RuntimeLocalizedPlayerValid(request->localized_player) ||
		!RuntimeAuthorityValid(&request->authority) || request->spec.plan_id != 0U ||
		request->spec.reserved != 0U || request->reserved != 0U ||
		request->spec.goal_count == 0U ||
		request->spec.goal_count > SG_STRATEGY_MAX_GOALS ||
		request->execution_count == 0U ||
		request->execution_count > SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	for (goal_index = 0U; goal_index < request->spec.goal_count; goal_index++)
	{
		const sg_strategy_goal_spec_t *goal = &request->spec.goals[goal_index];

		if (goal->choice_count > SG_STRATEGY_MAX_CHOICES ||
			target_count > UINT16_MAX - goal->choice_count)
			return 0;
		target_count = (uint16_t)(target_count + goal->choice_count);
	}
	if (target_count != request->execution_count)
		return 0;
	memset(&spec, 0, sizeof(spec));
	spec = request->spec;
	spec.plan_id = 1U;
	if (!SG_StrategyPlanCompile(&spec, compiled, &error))
		return 0;
	for (goal_index = 0U; goal_index < compiled->goal_count; goal_index++)
	{
		const sg_strategy_goal_t *goal = &compiled->goals[goal_index];
		uint8_t choice_index;

		for (choice_index = 0U; choice_index < goal->choice_count;
			choice_index++)
		{
			const sg_strategy_runtime_execution_t *execution;

			if (!RuntimeExecutionFor(request, goal->id,
				goal->choices[choice_index].id, &execution))
				return 0;
		}
	}
	return 1;
}

static int RuntimeCompactTargetFor(
	sg_rune_compact_field_service_t *service,
	const sg_strategy_runtime_target_request_t *request,
	sg_rune_compact_field_target_t *target_out,
	sg_destination_handle_t *destination_out)
{
	const sg_rune_compact_field_service_live_pose_t *live_pose =
		request != NULL ? &request->live_pose : NULL;

	return service != NULL && request != NULL &&
		SG_RuneCompactFieldServiceResolveSemanticTarget(service,
			request->target_id, &request->destination, live_pose, target_out,
			destination_out);
}

static int CompactProviderLocate(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_runtime_target_view_t *view_out)
{
	sg_rune_compact_field_service_t *service = context;
	sg_rune_compact_field_service_provider_t provider;
	sg_rune_compact_field_service_target_request_t compact_request;
	sg_rune_compact_field_service_target_view_t compact_view;
	sg_rune_compact_field_target_t target;
	sg_destination_handle_t destination;

	if (view_out != NULL)
		memset(view_out, 0, sizeof(*view_out));
	if (request == NULL || view_out == NULL || request->commitment_id == 0U ||
		!RuntimeCompactTargetFor(service, request, &target, &destination) ||
		!SG_RuneCompactFieldServiceProvider(service, &provider))
		return 0;
	memset(&compact_request, 0, sizeof(compact_request));
	compact_request.commitment_id = request->commitment_id;
	compact_request.target = target;
	memset(&compact_view, 0, sizeof(compact_view));
	if (!provider.locator(provider.context, &compact_request, &compact_view))
		return 0;
	view_out->opaque = compact_view.opaque;
	return view_out->opaque != NULL;
}

static int CompactProviderAuthority(void *context,
	const sg_strategy_runtime_target_request_t *request,
	const sg_strategy_runtime_target_view_t *view,
	sg_strategy_caller_target_binding_t *binding_out)
{
	sg_rune_compact_field_service_t *service = context;
	sg_rune_compact_field_service_provider_t provider;
	sg_rune_compact_field_service_target_request_t compact_request;
	sg_rune_compact_field_service_target_view_t compact_view;
	sg_rune_compact_field_service_target_binding_t compact_binding;
	sg_rune_compact_field_target_t target;
	sg_destination_handle_t destination;

	if (binding_out != NULL)
		memset(binding_out, 0, sizeof(*binding_out));
	if (request == NULL || view == NULL || view->opaque == NULL ||
		binding_out == NULL || request->commitment_id == 0U ||
		!RuntimeCompactTargetFor(service, request, &target, &destination) ||
		!SG_RuneCompactFieldServiceProvider(service, &provider))
		return 0;
	memset(&compact_request, 0, sizeof(compact_request));
	compact_request.commitment_id = request->commitment_id;
	compact_request.target = target;
	memset(&compact_view, 0, sizeof(compact_view));
	compact_view.opaque = view->opaque;
	memset(&compact_binding, 0, sizeof(compact_binding));
	if (!provider.authority(provider.context, &compact_request, &compact_view,
		&compact_binding))
		return 0;
	if (!RuntimeCompactTargetEqual(&target, &compact_binding.target) ||
		!RuntimeDestinationEqual(&request->destination,
			&compact_binding.target.semantic_destination))
	{
		provider.release_view(provider.context, compact_binding.accepted_view);
		return 0;
	}
	if (!SG_DestinationHandleValid(&destination))
	{
		provider.release_view(provider.context, compact_binding.accepted_view);
		return 0;
	}
	binding_out->commitment_id = request->commitment_id;
	binding_out->authority = request->authority;
	binding_out->goal_id = request->goal_id;
	binding_out->target_id = request->target_id;
	binding_out->destination = request->destination;
	binding_out->role = request->role;
	binding_out->accepted_view = compact_binding.accepted_view;
	binding_out->field_service = service;
	binding_out->compact_target = compact_binding.target;
	binding_out->field_handle = compact_binding.handle;
	return 1;
}

static void CompactProviderRelease(void *context, const void *accepted_view)
{
	sg_rune_compact_field_service_t *service = context;

	SG_RuneCompactFieldServiceTargetRelease(service, accepted_view);
}

static int RuntimeObserveTarget(const sg_strategy_caller_plan_t *plan,
	const sg_strategy_caller_target_binding_t *binding,
	sg_strategy_caller_target_observation_t *observation_out);

static int RuntimePlanBotObservationCurrent(
	const sg_strategy_caller_plan_t *plan)
{
	sg_strategy_runtime_bot_observation_view_t view;

	if (plan == NULL ||
		!RuntimeBotObservationOwnerAvailable(
			&sg_strategy_runtime_bot_observation_owner))
		return 0;
	memset(&view, 0, sizeof(view));
	view.subject.client_id = plan->bot_observation.life_identity.client_id;
	view.subject.reserved = plan->bot_observation.life_identity.reserved;
	view.subject.spawn_generation =
		plan->bot_observation.life_identity.spawn_generation;
	view.host_authority_epoch = plan->bot_observation.host_authority_epoch;
	view.frame_sequence = plan->bot_observation.frame_sequence;
	view.observed_at_ms = plan->bot_observation.observed_at_ms;
	view.hook_phase = plan->bot_observation.hook_phase;
	view.hook_length = plan->bot_observation.hook_length;
	view.target_radius = plan->bot_observation.target_radius;
	return sg_strategy_runtime_bot_observation_owner.current(
		sg_strategy_runtime_bot_observation_owner.context, &view);
}

static int RuntimePlanCurrent(const sg_strategy_caller_plan_t *plan)
{
	const sg_compact_localized_state_t *localized;
	uint16_t binding_index;

	if (plan == NULL || plan->provider_generation == 0U ||
		plan->provider_generation != sg_strategy_runtime_provider.identity ||
		plan->frame_capability == NULL || plan->frame_sequence == 0U ||
		plan->observed_at_ms == 0U ||
		plan->plan_current != RuntimePlanCurrent ||
		plan->observe_target != RuntimeObserveTarget ||
		plan->release_view != sg_strategy_runtime_provider.release_view ||
		plan->release_context != sg_strategy_runtime_provider.release_context ||
		!RuntimeProviderRegistrationAvailable(&sg_strategy_runtime_provider) ||
		!RuntimePlanBotObservationCurrent(plan))
		return 0;
	localized = plan->frame_capability;
	if (!RuntimeLocalizedPlayerValid(localized) ||
		localized->subject.client_id != plan->life_identity.client_id ||
		localized->subject.reserved != plan->life_identity.reserved ||
		localized->subject.spawn_generation !=
			plan->life_identity.spawn_generation ||
		localized->frame_sequence != plan->frame_sequence ||
		localized->localized_at_ms != plan->observed_at_ms ||
		plan->binding_count == 0U ||
		plan->binding_count > SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	if (plan->mechanisms != NULL &&
		plan->mechanisms->frame_sequence != plan->frame_sequence)
		return 0;
	if (plan->portal_roots != NULL &&
		plan->portal_roots->frame_sequence != plan->frame_sequence)
		return 0;
	for (binding_index = 0U; binding_index < plan->binding_count;
	     binding_index++)
	{
		const sg_strategy_caller_target_binding_t *binding =
			&plan->bindings[binding_index];
		sg_rune_compact_field_target_t current_target;
		uint32_t current_region;
		uint64_t current_region_epoch;

		if (binding->commitment_id != plan->commitment_id ||
			!RuntimeAuthorityEqual(&binding->authority, &plan->authority) ||
			binding->accepted_view == NULL ||
			binding->field_service != sg_strategy_runtime_compact_service ||
			binding->compact_target.target_id != binding->target_id ||
			binding->compact_target.target_generation == 0U ||
			binding->field_handle.target_id != binding->target_id ||
			binding->field_handle.target_generation !=
				binding->compact_target.target_generation ||
			binding->field_handle.rune_identity != localized->rune_identity ||
			binding->field_handle.topology_revision !=
				localized->topology_revision ||
			!SG_RuneCompactFieldServiceHandleCurrent(binding->field_service,
				&binding->field_handle, &current_target, &current_region,
				&current_region_epoch) ||
			!RuntimeCompactTargetEqual(&current_target,
				&binding->compact_target))
			return 0;
		(void)current_region;
		(void)current_region_epoch;
	}
	return 1;
}

static int RuntimeObserveTarget(const sg_strategy_caller_plan_t *plan,
	const sg_strategy_caller_target_binding_t *binding,
	sg_strategy_caller_target_observation_t *observation_out)
{
	const sg_compact_localized_state_t *localized;
	sg_rune_compact_field_target_t current_target;
	sg_rune_compact_field_local_context_t local_context;
	sg_rune_compact_field_result_t result;
	uint32_t current_region;
	uint64_t current_region_epoch;

	if (observation_out != NULL)
		memset(observation_out, 0, sizeof(*observation_out));
	if (!RuntimePlanCurrent(plan) || binding == NULL ||
		observation_out == NULL || binding->field_service == NULL ||
		binding->field_service != sg_strategy_runtime_compact_service ||
		binding->commitment_id != plan->commitment_id ||
		!RuntimeAuthorityEqual(&binding->authority, &plan->authority) ||
		binding->compact_target.target_id != binding->target_id ||
		binding->compact_target.target_generation == 0U ||
		binding->field_handle.target_id != binding->target_id ||
		binding->field_handle.target_generation !=
			binding->compact_target.target_generation ||
		!SG_RuneCompactFieldServiceHandleCurrent(binding->field_service,
			&binding->field_handle, &current_target, &current_region,
			&current_region_epoch) ||
		!RuntimeCompactTargetEqual(&current_target, &binding->compact_target))
		return 0;
	localized = plan->frame_capability;
	if (!RuntimeLocalContext(SG_RuneCompactFieldServiceModel(
		binding->field_service), localized, &plan->bot_observation,
		plan->mechanisms, plan->portal_roots,
		&local_context) ||
		SG_RuneCompactFieldServiceQuery(binding->field_service,
			&binding->field_handle, &local_context, &result) !=
				SG_RUNE_COMPACT_FIELD_SERVICE_OK ||
		!SG_StrategyCallerFieldObservationFromResult(&result,
			&observation_out->field))
		return 0;
	observation_out->target_revision =
		binding->compact_target.target_generation;
	observation_out->observation_revision = plan->frame_sequence;
	observation_out->observed_at_ms = plan->observed_at_ms;
	(void)current_region;
	(void)current_region_epoch;
	return 1;
}

static void RuntimeProviderSet(
	sg_strategy_runtime_target_locator_fn locator, void *locator_context,
	sg_strategy_runtime_target_authority_fn authority, void *authority_context,
	sg_strategy_runtime_target_release_fn release_view, void *release_context)
{
	RuntimeProviderRegistrationReplace(locator, locator_context, authority,
		authority_context, release_view, release_context);
}

int SG_StrategyRuntimeCompactProviderAvailable(void)
{
	return RuntimeProviderRegistrationAvailable(&sg_strategy_runtime_provider) &&
		RuntimeBotObservationOwnerAvailable(
			&sg_strategy_runtime_bot_observation_owner);
}

int SG_StrategyRuntimeCompactProviderInstall(
	sg_rune_compact_field_service_t *service,
	const sg_strategy_runtime_bot_observation_owner_t *bot_observation)
{
	sg_rune_compact_field_service_provider_t provider;

	if (service == NULL || SG_RuneCompactFieldServiceModel(service) == NULL ||
		SG_RuneCompactFieldServiceIdentity(service) == 0U ||
		SG_RuneCompactFieldServiceGeneration(service) == 0U ||
		!SG_RuneCompactFieldServiceProvider(service, &provider) ||
		!RuntimeBotObservationOwnerAvailable(bot_observation))
	{
		RuntimeProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
		return 0;
	}
	RuntimeProviderSet(CompactProviderLocate, service,
		CompactProviderAuthority, service, CompactProviderRelease, service);
	sg_strategy_runtime_compact_service = service;
	sg_strategy_runtime_bot_observation_owner = *bot_observation;
	return SG_StrategyRuntimeCompactProviderAvailable();
}

int SG_StrategyRuntimeCompactProviderInstalledFor(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL && service == sg_strategy_runtime_compact_service &&
		SG_StrategyRuntimeCompactProviderAvailable();
}

void SG_StrategyRuntimeCompactProviderClear(
	sg_rune_compact_field_service_t *service)
{
	if (service == NULL || service == sg_strategy_runtime_compact_service)
	{
		RuntimeProviderSet(NULL, NULL, NULL, NULL, NULL, NULL);
		sg_strategy_runtime_compact_service = NULL;
	}
}

int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out)
{
	sg_strategy_caller_plan_t candidate;
	sg_strategy_plan_t compiled;
	sg_strategy_runtime_provider_registration_t registration;
	sg_strategy_runtime_bot_observation_owner_t observation_owner;
	sg_strategy_runtime_bot_observation_view_t observation_view;
	uint16_t goal_index;
	uint16_t binding_index = 0U;

	if (plan_out == NULL || plan_out->binding_count != 0U ||
		plan_out->provider_generation != 0U ||
		plan_out->frame_use_at_ms != 0U ||
		plan_out->frame_capability != NULL || plan_out->plan_current != NULL ||
		plan_out->observe_target != NULL || plan_out->release_view != NULL ||
		plan_out->release_context != NULL)
		return 0;
	registration = sg_strategy_runtime_provider;
	observation_owner = sg_strategy_runtime_bot_observation_owner;
	if (request == NULL || !RuntimeProviderRegistrationAvailable(&registration) ||
		!RuntimeBotObservationOwnerCurrent(&observation_owner) ||
		request->bot_observation == NULL ||
		!RuntimeRequestCompile(request, &compiled))
		return 0;
	memset(&observation_view, 0, sizeof(observation_view));
	if (!observation_owner.validate(observation_owner.context,
			request->bot_observation, &observation_view) ||
		!RuntimeProviderRegistrationCurrent(&registration) ||
		!RuntimeBotObservationOwnerCurrent(&observation_owner) ||
		!RuntimeBotObservationViewValid(&observation_view,
			request->localized_player) ||
		!observation_owner.current(observation_owner.context,
			&observation_view))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.commitment_id = request->commitment_id;
	candidate.provider_generation = registration.identity;
	candidate.frame_sequence = request->localized_player->frame_sequence;
	candidate.observed_at_ms = request->localized_player->localized_at_ms;
	candidate.life_identity.client_id =
		request->localized_player->subject.client_id;
	candidate.life_identity.reserved =
		request->localized_player->subject.reserved;
	candidate.life_identity.spawn_generation =
		request->localized_player->subject.spawn_generation;
	RuntimeBotObservationCopy(&observation_view, &candidate.bot_observation);
	candidate.authority = request->authority;
	candidate.spec = request->spec;
	candidate.binding_count = request->execution_count;
	candidate.frame_capability = request->localized_player;
	candidate.mechanisms = request->mechanisms;
	candidate.portal_roots = request->portal_roots;
	candidate.plan_current = RuntimePlanCurrent;
	candidate.observe_target = RuntimeObserveTarget;
	candidate.release_view = registration.release_view;
	candidate.release_context = registration.release_context;
	for (goal_index = 0U; goal_index < compiled.goal_count; goal_index++)
	{
		const sg_strategy_goal_t *goal = &compiled.goals[goal_index];
		uint8_t choice_index;

		for (choice_index = 0U; choice_index < goal->choice_count;
			choice_index++)
		{
			const sg_strategy_runtime_execution_t *execution;
			sg_strategy_runtime_target_request_t target;
			sg_strategy_runtime_target_view_t view;
			sg_strategy_caller_target_binding_t binding;
			int located;
			int authority_accepted;

			if (binding_index >= candidate.binding_count ||
				!RuntimeExecutionFor(request, goal->id,
					goal->choices[choice_index].id, &execution))
				goto reject;
			memset(&target, 0, sizeof(target));
			target.commitment_id = request->commitment_id;
			target.localized_player = request->localized_player;
			target.mechanisms = request->mechanisms;
			target.portal_roots = request->portal_roots;
			target.authority = request->authority;
			target.goal_id = goal->id;
			target.target_id = goal->choices[choice_index].id;
			target.destination = goal->choices[choice_index].destination;
			target.role = execution->role;
			target.live_pose = execution->live_pose;
			memset(&view, 0, sizeof(view));
			memset(&binding, 0, sizeof(binding));
			located = registration.locator(registration.locator_context, &target,
				&view);
			if (!located || view.opaque == NULL)
				goto reject;
			if (!RuntimeProviderRegistrationCurrent(&registration))
			{
				registration.release_view(registration.release_context,
					view.opaque);
				goto reject;
			}
			authority_accepted = registration.authority(
				registration.authority_context, &target, &view, &binding);
			/* An accepted view belongs to this snapshot's release owner even if
			 * authority changes the live registration before returning. */
			if (!RuntimeProviderRegistrationCurrent(&registration))
			{
				/* The locator lends this token whether or not authority accepts
				 * it.  Always return it through the registration snapshot; an
				 * authority that already rejected it must make release idempotent. */
				registration.release_view(registration.release_context,
					view.opaque);
				goto reject;
			}
			if (!authority_accepted)
			{
				/* Rejection does not transfer ownership of the locator token to
				 * the rollback plan. */
				registration.release_view(registration.release_context,
					view.opaque);
				goto reject;
			}
			if (!RuntimeBindingAccepted(&target, &view, &binding))
			{
				/* The authority accepted this lease even though its emitted
				 * binding failed the caller contract.  Attach only the opaque
				 * lease to the rollback plan so every accepted view retires from
				 * one detached callback/context snapshot. */
				candidate.bindings[binding_index].accepted_view = view.opaque;
				binding_index++;
				goto reject;
			}
			candidate.bindings[binding_index] = binding;
			binding_index++;
		}
	}
	if (binding_index != candidate.binding_count ||
		!RuntimeProviderRegistrationCurrent(&registration))
		goto reject;
	*plan_out = candidate;
	return 1;

reject:
	candidate.binding_count = binding_index;
	SG_StrategyCallerPlanDiscard(&candidate);
	return 0;
}

static int RuntimeQueryOutputWithObservation(
	const sg_strategy_caller_output_t *output,
	const sg_compact_localized_state_t *localized_player,
	const sg_strategy_caller_bot_observation_t *bot_observation,
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms,
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots,
	sg_rune_compact_field_result_t *result_out,
	sg_rune_compact_field_local_context_t *local_context_out)
{
	sg_rune_compact_field_target_t current_target;
	sg_rune_compact_field_local_context_t local_context;
	sg_rune_compact_field_result_t result;
	uint32_t current_region;
	uint64_t current_region_epoch;

	if (result_out != NULL)
		memset(result_out, 0, sizeof(*result_out));
	if (local_context_out != NULL)
		memset(local_context_out, 0, sizeof(*local_context_out));
	if (output == NULL || result_out == NULL || local_context_out == NULL ||
	    !RuntimeLocalizedPlayerValid(localized_player) ||
	    output->commitment_id == 0U || output->plan_id == 0U ||
	    output->instruction.plan_id != output->plan_id ||
	    output->instruction.goal_id == 0U ||
	    output->instruction.target_id == 0U ||
	    output->frame_sequence != localized_player->frame_sequence ||
	    output->observed_at_ms != localized_player->localized_at_ms ||
	    output->life_identity.client_id !=
		localized_player->subject.client_id ||
	    output->life_identity.reserved != localized_player->subject.reserved ||
	    output->life_identity.spawn_generation !=
		localized_player->subject.spawn_generation ||
	    output->field_service == NULL ||
	    output->field_service != sg_strategy_runtime_compact_service ||
	    output->compact_target.target_id != output->instruction.target_id ||
	    output->compact_target.target_generation == 0U ||
	    output->field_handle.target_id != output->instruction.target_id ||
	    output->field_handle.target_generation !=
		output->compact_target.target_generation ||
	    output->field_handle.rune_identity != localized_player->rune_identity ||
	    output->field_handle.topology_revision !=
		localized_player->topology_revision ||
	    output->instruction.field_state >= SG_STRATEGY_FIELD_STATE_COUNT ||
	    (output->instruction.kind != SG_STRATEGY_INSTRUCTION_EXECUTE &&
	     output->instruction.kind != SG_STRATEGY_INSTRUCTION_SUSPENDED &&
	     output->instruction.kind != SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION) ||
	    !RuntimeDestinationEqual(&output->instruction.destination,
		&output->compact_target.semantic_destination) ||
	    !SG_RuneCompactFieldServiceHandleCurrent(output->field_service,
		&output->field_handle, &current_target, &current_region,
		&current_region_epoch) ||
	    !RuntimeCompactTargetEqual(&current_target, &output->compact_target) ||
	    !RuntimeLocalContext(SG_RuneCompactFieldServiceModel(
		output->field_service), localized_player, bot_observation, mechanisms,
		portal_roots,
		&local_context) ||
	    SG_RuneCompactFieldServiceQuery(output->field_service,
		&output->field_handle, &local_context, &result) !=
		SG_RUNE_COMPACT_FIELD_SERVICE_OK ||
	    result.kind >= SG_RUNE_COMPACT_FIELD_RESULT_KIND_COUNT ||
	    (sg_strategy_field_state_t)result.kind !=
		output->instruction.field_state ||
	    result.current_cell.value != localized_player->location.cell.value)
		return 0;
	*result_out = result;
	*local_context_out = local_context;
	(void)current_region;
	(void)current_region_epoch;
	return 1;
}

int SG_StrategyRuntimeQueryCallerOutputWithContext(
	sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_compact_localized_state_t *localized_player,
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms,
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots,
	const sg_strategy_runtime_bot_observation_t *bot_observation,
	sg_rune_compact_field_result_t *result_out,
	sg_rune_compact_field_local_context_t *local_context_out,
	sg_strategy_caller_output_proof_t *proof_out,
	sg_strategy_runtime_caller_query_proof_t *query_proof_out)
{
	sg_strategy_runtime_bot_observation_owner_t observation_owner;
	sg_strategy_runtime_bot_observation_view_t observation_view;
	sg_strategy_runtime_caller_query_snapshot_t query_snapshot;
	uint8_t query_token[16];

	if (result_out != NULL)
		memset(result_out, 0, sizeof(*result_out));
	if (local_context_out != NULL)
		memset(local_context_out, 0, sizeof(*local_context_out));
	if (proof_out != NULL)
		memset(proof_out, 0, sizeof(*proof_out));
	if (query_proof_out != NULL)
		memset(query_proof_out, 0, sizeof(*query_proof_out));
	observation_owner = sg_strategy_runtime_bot_observation_owner;
	memset(&observation_view, 0, sizeof(observation_view));
	memset(&query_snapshot, 0, sizeof(query_snapshot));
	memset(query_token, 0, sizeof(query_token));
	if (caller == NULL || output == NULL || bot_observation == NULL ||
		result_out == NULL ||
		local_context_out == NULL || proof_out == NULL ||
		query_proof_out == NULL ||
		output->instruction.kind != SG_STRATEGY_INSTRUCTION_EXECUTE ||
		!RuntimeBotObservationOwnerCurrent(&observation_owner) ||
		!SG_StrategyCallerOutputCurrent(caller, output) ||
		!observation_owner.validate(observation_owner.context, bot_observation,
			&observation_view) ||
		!RuntimeBotObservationOwnerCurrent(&observation_owner) ||
		!RuntimeBotObservationViewValid(&observation_view, localized_player) ||
		!RuntimeBotObservationEqual(&caller->plan.bot_observation,
			&observation_view) ||
		!observation_owner.current(observation_owner.context,
			&observation_view) ||
		!RuntimeQueryOutputWithObservation(output, localized_player,
			&caller->plan.bot_observation, mechanisms, portal_roots,
			result_out, local_context_out) ||
		!RuntimeBotObservationOwnerCurrent(&observation_owner) ||
		!observation_owner.current(observation_owner.context,
			&observation_view) ||
		!SG_StrategyCallerOutputCurrent(caller, output) ||
		sg_strategy_runtime_query_next_issuance == UINT64_MAX ||
		!RuntimeQuerySnapshotBuild(local_context_out, &query_snapshot) ||
		!SG_AuthorityEntropyFill(query_token, sizeof(query_token)) ||
		!RuntimeBytesNonzero(query_token, sizeof(query_token)) ||
		!SG_StrategyCallerOutputProofIssue(caller, output, proof_out))
	{
		if (result_out != NULL)
			memset(result_out, 0, sizeof(*result_out));
		if (local_context_out != NULL)
			memset(local_context_out, 0, sizeof(*local_context_out));
		if (proof_out != NULL)
			memset(proof_out, 0, sizeof(*proof_out));
		if (query_proof_out != NULL)
			memset(query_proof_out, 0, sizeof(*query_proof_out));
		return 0;
	}
	sg_strategy_runtime_query_authority.caller = caller;
	sg_strategy_runtime_query_authority.provider_identity =
		sg_strategy_runtime_provider.identity;
	sg_strategy_runtime_query_authority.issuance =
		sg_strategy_runtime_query_next_issuance;
	sg_strategy_runtime_query_next_issuance++;
	memcpy(sg_strategy_runtime_query_authority.token, query_token,
		sizeof(query_token));
	sg_strategy_runtime_query_authority.output_proof = *proof_out;
	sg_strategy_runtime_query_authority.snapshot = query_snapshot;
	sg_strategy_runtime_query_authority.field_result = *result_out;
	sg_strategy_runtime_query_authority.mechanisms =
		local_context_out->mechanisms;
	sg_strategy_runtime_query_authority.portal_roots =
		local_context_out->portal_roots;
	sg_strategy_runtime_query_authority.active = 1U;
	RuntimeQueryProofEncode(query_proof_out, query_token);
	return 1;
}

int SG_StrategyRuntimeCallerQueryProofCurrent(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *output_proof,
	const sg_rune_compact_field_local_context_t *local_context,
	const sg_rune_compact_field_result_t *field_result,
	const sg_strategy_runtime_caller_query_proof_t *query_proof,
	sg_strategy_runtime_caller_query_snapshot_t *snapshot_out)
{
	sg_strategy_runtime_caller_query_snapshot_t current_snapshot;

	if (snapshot_out != NULL)
		memset(snapshot_out, 0, sizeof(*snapshot_out));
	memset(&current_snapshot, 0, sizeof(current_snapshot));
	if (caller == NULL || output == NULL || output_proof == NULL ||
		local_context == NULL || field_result == NULL || query_proof == NULL ||
		snapshot_out == NULL ||
		sg_strategy_runtime_query_authority.caller != caller ||
		sg_strategy_runtime_query_authority.provider_identity == 0U ||
		sg_strategy_runtime_query_authority.provider_identity !=
			sg_strategy_runtime_provider.identity ||
		output->field_service == NULL ||
		output->field_service != sg_strategy_runtime_compact_service ||
		caller->plan.mechanisms != local_context->mechanisms ||
		caller->plan.portal_roots != local_context->portal_roots ||
		!RuntimeQueryProofMatches(query_proof) ||
		!RuntimeBytesEqual(output_proof->opaque,
			sg_strategy_runtime_query_authority.output_proof.opaque,
			SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES) ||
		!SG_StrategyCallerOutputProofCurrent(caller, output, output_proof) ||
		!RuntimeQuerySnapshotBuild(local_context, &current_snapshot) ||
		!RuntimeQuerySnapshotEqual(&current_snapshot,
			&sg_strategy_runtime_query_authority.snapshot) ||
		!RuntimeFieldResultEqual(field_result,
			&sg_strategy_runtime_query_authority.field_result) ||
		!RuntimeQueryAuthorityLiveCurrent(caller, output, field_result))
		return 0;
	*snapshot_out = sg_strategy_runtime_query_authority.snapshot;
	return 1;
}

int SG_StrategyRuntimeCallerQueryReceiptCurrent(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_receipt_t *output_receipt,
	const sg_strategy_runtime_caller_query_snapshot_t *snapshot,
	const sg_rune_compact_field_result_t *field_result,
	const sg_strategy_runtime_caller_query_proof_t *query_proof)
{
	int current;

	current = caller != NULL && output != NULL && output_receipt != NULL &&
		snapshot != NULL && field_result != NULL && query_proof != NULL &&
		field_result->kind == SG_RUNE_COMPACT_FIELD_STEP &&
		sg_strategy_runtime_query_authority.caller == caller &&
		sg_strategy_runtime_query_authority.provider_identity != 0U &&
		sg_strategy_runtime_query_authority.provider_identity ==
			sg_strategy_runtime_provider.identity &&
		output->field_service != NULL &&
		output->field_service == sg_strategy_runtime_compact_service &&
		RuntimeQueryProofMatches(query_proof) &&
		SG_StrategyCallerOutputReceiptMatchesProof(caller, output,
			&sg_strategy_runtime_query_authority.output_proof,
			output_receipt) &&
		RuntimeQuerySnapshotEqual(snapshot,
			&sg_strategy_runtime_query_authority.snapshot) &&
		RuntimeFieldResultEqual(field_result,
			&sg_strategy_runtime_query_authority.field_result) &&
		RuntimeQueryAuthorityLiveCurrent(caller, output, field_result);
	if (!current ||
		(sg_strategy_runtime_query_authority.receipt_validated != 0U &&
		 !RuntimeBytesEqual(output_receipt->opaque,
			sg_strategy_runtime_query_authority.output_receipt.opaque,
			SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES)))
		return 0;
	if (sg_strategy_runtime_query_authority.receipt_validated == 0U)
	{
		sg_strategy_runtime_query_authority.output_receipt = *output_receipt;
		sg_strategy_runtime_query_authority.receipt_validated = 1U;
	}
	return 1;
}

int SG_StrategyRuntimeCallerQueryReceiptRelease(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_receipt_t *output_receipt,
	const sg_strategy_runtime_caller_query_proof_t *query_proof)
{
	if (caller == NULL || output == NULL || output_receipt == NULL ||
		query_proof == NULL ||
		sg_strategy_runtime_query_authority.caller != caller ||
		!RuntimeQueryProofMatches(query_proof) ||
		!SG_StrategyCallerOutputReceiptLineageMatches(
			&sg_strategy_runtime_query_authority.output_proof,
			output_receipt) ||
		(sg_strategy_runtime_query_authority.receipt_validated == 1U &&
		 !RuntimeBytesEqual(output_receipt->opaque,
			sg_strategy_runtime_query_authority.output_receipt.opaque,
			SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES)))
		return 0;
	RuntimeQueryAuthorityClear();
	return 1;
}

int SG_StrategyRuntimeCallerQueryProofRelease(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *output_proof,
	const sg_strategy_runtime_caller_query_proof_t *query_proof)
{
	if (caller == NULL || output == NULL || output_proof == NULL ||
		query_proof == NULL ||
		sg_strategy_runtime_query_authority.caller != caller ||
		sg_strategy_runtime_query_authority.receipt_validated != 0U ||
		!RuntimeQueryProofMatches(query_proof) ||
		!RuntimeBytesEqual(output_proof->opaque,
			sg_strategy_runtime_query_authority.output_proof.opaque,
			SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES))
		return 0;
	RuntimeQueryAuthorityClear();
	return 1;
}
