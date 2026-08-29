#include "sg_phase_catalog_internal.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_phase_provider_support_sort_s
{
	sg_phase_mover_support_t support;
} sg_phase_provider_support_sort_t;

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static int StableIdCompare(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static int SupportCompare(const void *left_value, const void *right_value)
{
	const sg_phase_provider_support_sort_t *left = left_value;
	const sg_phase_provider_support_sort_t *right = right_value;
	int comparison;

	if (left->support.semantic_region_id !=
		right->support.semantic_region_id)
		return left->support.semantic_region_id <
			right->support.semantic_region_id ? -1 : 1;
	comparison = StableIdCompare(&left->support.mechanism.value,
		&right->support.mechanism.value);
	if (comparison != 0)
		return comparison;
	if (left->support.mechanism_state_mask !=
		right->support.mechanism_state_mask)
		return left->support.mechanism_state_mask <
			right->support.mechanism_state_mask ? -1 : 1;
	return 0;
}

static uint32_t StateBit(sg_mechanism_state_t state)
{
	if (state < SG_MECHANISM_STATE_INACTIVE ||
		state >= SG_MECHANISM_STATE_COUNT)
		return 0U;
	return UINT32_C(1) << (uint32_t)state;
}

static uint64_t DigestBytes(uint64_t digest, const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0U; index < size; index++)
		digest = (digest ^ (uint64_t)bytes[index]) *
			UINT64_C(1099511628211);
	return digest;
}

static uint64_t DigestU32(uint64_t digest, uint32_t value)
{
	unsigned char bytes[4];
	uint32_t index;

	for (index = 0U; index < 4U; index++)
		bytes[index] = (unsigned char)(value >> (index * 8U));
	return DigestBytes(digest, bytes, sizeof(bytes));
}

static uint64_t DigestU64(uint64_t digest, uint64_t value)
{
	unsigned char bytes[8];
	uint32_t index;

	for (index = 0U; index < 8U; index++)
		bytes[index] = (unsigned char)(value >> (index * 8U));
	return DigestBytes(digest, bytes, sizeof(bytes));
}

static uint64_t ProviderDigest(
	const sg_phase_mover_support_provider_payload_t *provider)
{
	uint64_t digest = UINT64_C(1469598103934665603);

	uint32_t index;

	digest = DigestU64(digest, UINT64_C(0x534750524f563031));
	digest = DigestU64(digest,
		SG_MechanismModelIdentityValue(&provider->identity));
	digest = DigestU32(digest, (uint32_t)provider->completion);
	digest = DigestU64(digest, provider->accepted_capability_identity);
	digest = DigestU32(digest, provider->support_count);
	digest = DigestU32(digest, provider->fact_count);
	if (!AllocationFits((size_t)provider->support_count,
			sizeof(*provider->supports)) ||
		!AllocationFits((size_t)provider->fact_count,
			sizeof(*provider->facts)))
		return 0U;
	for (index = 0U; index < provider->support_count; index++)
	{
		const sg_phase_mover_support_t *support = &provider->supports[index];
		digest = DigestU64(digest, support->semantic_region_id);
		digest = DigestU64(digest,
			support->mechanism.value.source_set_identity);
		digest = DigestU64(digest, support->mechanism.value.high);
		digest = DigestU64(digest, support->mechanism.value.low);
		digest = DigestU32(digest, support->mechanism_state_mask);
	}
	for (index = 0U; index < provider->fact_count; index++)
		digest = DigestU64(digest,
			SG_MechanismCapabilityFactIdentity(&provider->facts[index]));
	return digest == 0U ? UINT64_C(1) : digest;
}

sg_phase_mover_support_provider_payload_t *
SG_PhaseMoverSupportProviderPayload(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider)
{
	sg_phase_mover_support_provider_record_t *record;

	if (!owner || !provider)
		return NULL;
	for (record = owner->live; record; record = record->next)
		if (record->token == provider)
			return record->payload;
	return NULL;
}

int SG_PhaseMoverSupportProviderHeaderValid(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider)
{
	sg_phase_mover_support_provider_payload_t *payload =
		SG_PhaseMoverSupportProviderPayload(owner, provider);

	if (!payload)
		return 0;
	return
		payload->completion >= SG_PHASE_CATALOG_COMPLETE &&
		payload->completion < SG_PHASE_CATALOG_COMPLETION_COUNT &&
		payload->verifier_identity != 0U &&
		payload->verifier_identity != UINT64_MAX &&
		(payload->support_count == 0U || payload->supports) &&
		(payload->fact_count == 0U || payload->facts) &&
		payload->verifier_identity == ProviderDigest(payload);
}

static void ProviderRefreshView(
	sg_phase_mover_support_provider_record_t *record)
{
	const sg_phase_mover_support_provider_payload_t *provider = record->payload;

	if (provider->support_count != 0U)
		memcpy(record->view_supports, provider->supports,
			(size_t)provider->support_count * sizeof(*provider->supports));
	if (provider->fact_count != 0U)
		memcpy(record->view_facts, provider->facts,
			(size_t)provider->fact_count * sizeof(*provider->facts));
	memset(&record->view, 0, sizeof(record->view));
	record->view.identity = provider->identity;
	record->view.completion = provider->completion;
	record->view.verifier_identity = provider->verifier_identity;
	record->view.supports = record->view_supports;
	record->view.support_count = provider->support_count;
	record->view.facts = record->view_facts;
	record->view.fact_count = provider->fact_count;
}

static int ProviderIdentityUsable(const sg_rune_model_identity_t *identity)
{
	uint32_t axis;

	if (!identity || identity->bsp_content_id == 0U ||
		identity->entity_semantics_id == 0U || identity->physics_abi_id == 0U ||
		identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX || identity->schema_id == 0U ||
		identity->producer_identity == 0U ||
		!isfinite(identity->physics.gravity) || identity->physics.gravity < 0.0f ||
		!isfinite(identity->physics.ground_acceleration) ||
		identity->physics.ground_acceleration < 0.0f ||
		!isfinite(identity->physics.air_acceleration) ||
		identity->physics.air_acceleration < 0.0f ||
		!isfinite(identity->physics.water_acceleration) ||
		identity->physics.water_acceleration < 0.0f ||
		!isfinite(identity->physics.hook_acceleration) ||
		identity->physics.hook_acceleration < 0.0f ||
		!isfinite(identity->physics.external_acceleration) ||
		identity->physics.external_acceleration < 0.0f ||
		!isfinite(identity->physics.water_drag) ||
		identity->physics.water_drag < 0.0f ||
		!isfinite(identity->physics.max_velocity) ||
		identity->physics.max_velocity <= 0.0f ||
		identity->physics.gravity > (float)SHRT_MAX ||
		truncf(identity->physics.gravity) != identity->physics.gravity ||
		identity->physics.frame_ms == 0U || identity->physics.substep_ms == 0U ||
		identity->physics.substep_ms > UCHAR_MAX ||
		identity->physics.substep_ms > identity->physics.frame_ms ||
		identity->physics.frame_ms % identity->physics.substep_ms != 0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(identity->standing_hull.mins.value[axis]) ||
			!isfinite(identity->standing_hull.maxs.value[axis]) ||
			!isfinite(identity->crouching_hull.mins.value[axis]) ||
			!isfinite(identity->crouching_hull.maxs.value[axis]) ||
			identity->standing_hull.mins.value[axis] >=
				identity->standing_hull.maxs.value[axis] ||
			identity->crouching_hull.mins.value[axis] >=
				identity->crouching_hull.maxs.value[axis])
			return 0;
	return 1;
}

static int ProviderInputsValid(const sg_configuration_semantics_t *semantics,
	const sg_mechanism_capability_payload_t *capabilities)
{
	uint32_t index;

	if (!semantics || !capabilities || !ProviderIdentityUsable(&semantics->identity) ||
		!SG_PhaseCatalogIdentityEqual(&semantics->identity,
			&capabilities->identity) ||
		(semantics->region_count != 0U && !semantics->regions) ||
		(capabilities->fact_count != 0U && !capabilities->facts))
		return 0;
	for (index = 0U; index < capabilities->fact_count; index++)
	{
		const sg_mechanism_capability_fact_t *fact =
			&capabilities->facts[index];
		sg_rune_order_key_t mechanism_order;
		uint32_t source_bit = StateBit(fact->source_state);
		uint32_t destination_bit = StateBit(fact->destination_state);

		if (fact->mechanism_id.value.source_set_identity !=
			semantics->identity.source_set_identity ||
			!SG_RuneModelStableIdValid(&fact->mechanism_id.value) ||
			!SG_RuneModelStableIdToOrderKey(&fact->mechanism_id.value,
				&mechanism_order) || mechanism_order.domain !=
				SG_RUNE_ORDER_MECHANISM ||
			fact->source_region >= semantics->region_count ||
			fact->destination_region >= semantics->region_count ||
			source_bit == 0U || destination_bit == 0U)
			return 0;
	}
	return capabilities->fact_count <= UINT32_MAX / 2U;
}

int SG_PhaseMoverSupportProviderOwnerCreate(
	sg_phase_mover_support_provider_owner_t **owner_out)
{
	sg_phase_mover_support_provider_owner_t *owner;

	if (!owner_out || *owner_out)
		return 0;
	owner = calloc(1U, sizeof(*owner));
	if (!owner)
		return 0;
	*owner_out = owner;
	return 1;
}

int SG_PhaseMoverSupportProviderBuild(
	sg_phase_mover_support_provider_owner_t *owner,
	const sg_mechanism_capability_owner_t *capability_owner,
	const sg_configuration_semantics_t *semantics,
	const sg_mechanism_capability_set_t *accepted_capabilities,
	sg_phase_mover_support_provider_t **provider_out,
	sg_phase_catalog_error_t *error_out)
{
	sg_phase_mover_support_provider_payload_t *provider = NULL;
	sg_phase_mover_support_provider_record_t *record = NULL;
	const sg_mechanism_capability_payload_t *capabilities = NULL;
	sg_phase_provider_support_sort_t *sorted = NULL;
	uint32_t sorted_count = 0U;
	uint32_t index;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	capabilities = SG_MechanismCapabilityOwnerAcceptedPayload(capability_owner,
		accepted_capabilities);
	if (!owner || !provider_out || *provider_out || !capabilities ||
		!ProviderInputsValid(semantics, capabilities))
	{
		SG_PhaseCatalogSetError(error_out,
			!provider_out ? SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT :
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	*provider_out = NULL;
	if (capabilities->fact_count != 0U)
	{
		if (!AllocationFits((size_t)capabilities->fact_count,
			2U * sizeof(*sorted)))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OVERFLOW,
				capabilities->fact_count);
			return 0;
		}
		sorted = calloc((size_t)capabilities->fact_count * 2U,
			sizeof(*sorted));
		if (!sorted)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY,
				capabilities->fact_count);
			return 0;
		}
		for (index = 0U; index < capabilities->fact_count; index++)
		{
			const sg_mechanism_capability_fact_t *fact =
				&capabilities->facts[index];
			sg_phase_mechanism_state_mask_t source_mask =
				(sg_phase_mechanism_state_mask_t)StateBit(fact->source_state);
			sg_phase_mechanism_state_mask_t destination_mask =
				(sg_phase_mechanism_state_mask_t)StateBit(
					fact->destination_state);
			uint64_t source_region = semantics->regions[
				fact->source_region].id;
			uint64_t destination_region = semantics->regions[
				fact->destination_region].id;

			sorted[sorted_count].support.semantic_region_id = source_region;
			sorted[sorted_count].support.mechanism = fact->mechanism_id;
			sorted[sorted_count].support.mechanism_state_mask = source_mask;
			sorted_count++;
			if (destination_region != source_region)
			{
				sorted[sorted_count].support.semantic_region_id =
					destination_region;
				sorted[sorted_count].support.mechanism = fact->mechanism_id;
				sorted[sorted_count].support.mechanism_state_mask =
					destination_mask;
				sorted_count++;
			}
			else
				sorted[sorted_count - 1U].support.mechanism_state_mask |=
					destination_mask;
		}
		qsort(sorted, sorted_count, sizeof(*sorted), SupportCompare);
	}
	provider = calloc(1U, sizeof(*provider));
	if (!provider)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		free(sorted);
		return 0;
	}
	provider->identity = semantics->identity;
	provider->completion = capabilities->fact_count == 0U ?
		SG_PHASE_CATALOG_PROVEN_EMPTY : SG_PHASE_CATALOG_COMPLETE;
	provider->accepted_capability_identity = capabilities->content_identity;
	if (provider->accepted_capability_identity == 0U)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		free(sorted);
		free(provider);
		return 0;
	}
	provider->support_count = 0U;
	for (index = 0U; index < sorted_count; index++)
	{
		if (provider->support_count != 0U &&
			sorted[index].support.semantic_region_id ==
				sorted[index - 1U].support.semantic_region_id &&
			SG_RuneModelStableIdEqual(
				&sorted[index].support.mechanism.value,
				&sorted[index - 1U].support.mechanism.value))
			continue;
		provider->support_count++;
	}
	if (provider->support_count != 0U)
	{
		if (!AllocationFits((size_t)provider->support_count,
			sizeof(*provider->supports)))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OVERFLOW, provider->support_count);
			free(sorted);
			free(provider);
			return 0;
		}
		provider->supports = malloc((size_t)provider->support_count *
			sizeof(*provider->supports));
		if (!provider->supports)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			free(sorted);
			free(provider);
			return 0;
		}
		{
			uint32_t output = 0U;

			for (index = 0U; index < sorted_count; index++)
			{
				if (output != 0U &&
					sorted[index].support.semantic_region_id ==
						provider->supports[output - 1U].semantic_region_id &&
					SG_RuneModelStableIdEqual(
						&sorted[index].support.mechanism.value,
						&provider->supports[output - 1U].mechanism.value))
				{
					provider->supports[output - 1U].mechanism_state_mask |=
						sorted[index].support.mechanism_state_mask;
					continue;
				}
				provider->supports[output++] = sorted[index].support;
			}
		}
	}
	if (capabilities->fact_count != 0U)
	{
		if (!AllocationFits((size_t)capabilities->fact_count,
			sizeof(*provider->facts)))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OVERFLOW,
				capabilities->fact_count);
			free(sorted);
			free(provider->supports);
			free(provider);
			return 0;
		}
		provider->facts = malloc((size_t)capabilities->fact_count *
			sizeof(*provider->facts));
		if (!provider->facts)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			free(sorted);
			free(provider->supports);
			free(provider);
			return 0;
		}
		memcpy(provider->facts, capabilities->facts,
			(size_t)capabilities->fact_count *
				sizeof(*provider->facts));
		provider->fact_count = capabilities->fact_count;
	}
	provider->verifier_identity = ProviderDigest(provider);
	if (owner->live_count == UINT32_MAX)
	{
		SG_PhaseCatalogSetError(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
		free(sorted);
		free(provider->supports);
		free(provider->facts);
		free(provider);
		return 0;
	}
	record = calloc(1U, sizeof(*record));
	if (!record)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		free(sorted);
		free(provider->supports);
		free(provider->facts);
		free(provider);
		return 0;
	}
	record->payload = provider;
	if (provider->support_count != 0U)
		record->view_supports = malloc((size_t)provider->support_count *
			sizeof(*record->view_supports));
	if (provider->fact_count != 0U)
		record->view_facts = malloc((size_t)provider->fact_count *
			sizeof(*record->view_facts));
	if ((provider->support_count != 0U && !record->view_supports) ||
		(provider->fact_count != 0U && !record->view_facts))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		free(sorted);
		free(record->view_supports);
		free(record->view_facts);
		free(record);
		free(provider->supports);
		free(provider->facts);
		free(provider);
		return 0;
	}
	{
		uintptr_t token;

		if (!SG_AuthorityTokenMint(&token))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
			free(record->view_supports);
			free(record->view_facts);
			free(record);
			free(provider->supports);
			free(provider->facts);
			free(provider);
			free(sorted);
			return 0;
		}
		record->token =
			(sg_phase_mover_support_provider_t *)(uintptr_t)token;
	}
	ProviderRefreshView(record);
	record->next = owner->live;
	owner->live = record;
	owner->live_count++;
	free(sorted);
	if (!SG_PhaseMoverSupportProviderHeaderValid(owner, record->token))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		SG_PhaseMoverSupportProviderDestroy(owner, record->token);
		return 0;
	}
	*provider_out = record->token;
	return 1;
}

int SG_PhaseMoverSupportProviderRead(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider,
	const sg_phase_mover_support_provider_view_t **view_out)
{
	sg_phase_mover_support_provider_record_t *record;

	if (view_out)
		*view_out = NULL;
	if (!view_out || !SG_PhaseMoverSupportProviderHeaderValid(owner, provider))
		return 0;
	for (record = owner->live; record; record = record->next)
		if (record->token == provider)
		{
			ProviderRefreshView(record);
			*view_out = &record->view;
			return 1;
		}
	return 0;
}

void SG_PhaseMoverSupportProviderDestroy(
	sg_phase_mover_support_provider_owner_t *owner,
	sg_phase_mover_support_provider_t *provider)
{
	sg_phase_mover_support_provider_record_t **link;
	sg_phase_mover_support_provider_record_t *record;

	if (!owner || !provider)
		return;
	for (link = &owner->live; *link; link = &(*link)->next)
		if ((*link)->token == provider)
		{
			record = *link;
			*link = record->next;
			free(record->view_supports);
			free(record->view_facts);
			free(record->payload->supports);
			free(record->payload->facts);
			free(record->payload);
			free(record);
			owner->live_count--;
			break;
		}
}

void SG_PhaseMoverSupportProviderOwnerDestroy(
	sg_phase_mover_support_provider_owner_t *owner)
{
	sg_phase_mover_support_provider_record_t *record;

	if (!owner)
		return;
	while (owner->live)
	{
		record = owner->live;
		owner->live = record->next;
		free(record->view_supports);
		free(record->view_facts);
		free(record->payload->supports);
		free(record->payload->facts);
		free(record->payload);
		free(record);
	}
	free(owner);
}
