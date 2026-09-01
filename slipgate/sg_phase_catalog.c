#include "sg_phase_catalog_internal.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static void SetErrorOnce(sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_error_code_t code, uint32_t source_index)
{
	if (error_out && error_out->code == SG_PHASE_CATALOG_ERROR_NONE)
	{
		error_out->code = code;
		error_out->source_index = source_index;
	}
}

void SG_PhaseCatalogSetError(sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_error_code_t code, uint32_t source_index)
{
	SetErrorOnce(error_out, code, source_index);
}

static int Finite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull || !Finite3(hull->mins.value) || !Finite3(hull->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int PhysicsValid(const sg_rune_physics_parameters_t *physics)
{
	return physics && isfinite(physics->gravity) && physics->gravity >= 0.0f &&
		isfinite(physics->ground_acceleration) &&
		physics->ground_acceleration >= 0.0f &&
		isfinite(physics->air_acceleration) &&
		physics->air_acceleration >= 0.0f &&
		isfinite(physics->water_acceleration) &&
		physics->water_acceleration >= 0.0f &&
		isfinite(physics->hook_acceleration) &&
		physics->hook_acceleration >= 0.0f &&
		isfinite(physics->external_acceleration) &&
		physics->external_acceleration >= 0.0f &&
		isfinite(physics->water_drag) && physics->water_drag >= 0.0f &&
		isfinite(physics->max_velocity) && physics->max_velocity > 0.0f &&
		physics->gravity <= (float)SHRT_MAX &&
		truncf(physics->gravity) == physics->gravity && physics->frame_ms != 0U &&
		physics->substep_ms != 0U && physics->substep_ms <= UCHAR_MAX &&
		physics->substep_ms <= physics->frame_ms &&
		physics->frame_ms % physics->substep_ms == 0U;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	return identity && identity->bsp_content_id != 0U &&
		identity->entity_semantics_id != 0U && identity->physics_abi_id != 0U &&
		identity->source_set_identity != 0U &&
		identity->source_set_identity != UINT64_MAX && identity->schema_id != 0U &&
		identity->producer_identity != 0U && HullValid(&identity->standing_hull) &&
		HullValid(&identity->crouching_hull) && PhysicsValid(&identity->physics);
}

int SG_PhaseCatalogIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	uint32_t axis;

	if (!left || !right || left->bsp_content_id != right->bsp_content_id ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->source_set_identity != right->source_set_identity ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->standing_hull.mins.value[axis] !=
				right->standing_hull.mins.value[axis] ||
			left->standing_hull.maxs.value[axis] !=
				right->standing_hull.maxs.value[axis] ||
			left->crouching_hull.mins.value[axis] !=
				right->crouching_hull.mins.value[axis] ||
			left->crouching_hull.maxs.value[axis] !=
				right->crouching_hull.maxs.value[axis])
			return 0;
	return left->physics.gravity == right->physics.gravity &&
		left->physics.ground_acceleration ==
			right->physics.ground_acceleration &&
		left->physics.air_acceleration == right->physics.air_acceleration &&
		left->physics.water_acceleration == right->physics.water_acceleration &&
		left->physics.hook_acceleration == right->physics.hook_acceleration &&
		left->physics.external_acceleration ==
			right->physics.external_acceleration &&
		left->physics.water_drag == right->physics.water_drag &&
		left->physics.max_velocity == right->physics.max_velocity &&
		left->physics.frame_ms == right->physics.frame_ms &&
		left->physics.substep_ms == right->physics.substep_ms;
}

static int StableIdEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return left && right && SG_RuneModelStableIdEqual(left, right);
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

static int IntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return left->min_value == right->min_value &&
		left->max_value == right->max_value;
}

static int Interval3Equal(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return IntervalEqual(&left->x, &right->x) &&
		IntervalEqual(&left->y, &right->y) &&
		IntervalEqual(&left->z, &right->z);
}

static int RegionMedium(const sg_configuration_semantic_region_t *region,
	sg_rune_medium_t *medium_out)
{
	uint32_t flags;

	if (!region || !medium_out)
		return 0;
	flags = region->flags & (SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
		SG_CONFIGURATION_SEMANTIC_REGION_SLIME);
	if ((flags & (flags - 1U)) != 0U)
		return 0;
	if (flags == SG_CONFIGURATION_SEMANTIC_REGION_WATER)
		*medium_out = SG_RUNE_MEDIUM_WATER;
	else if (flags == SG_CONFIGURATION_SEMANTIC_REGION_LAVA)
		*medium_out = SG_RUNE_MEDIUM_LAVA;
	else if (flags == SG_CONFIGURATION_SEMANTIC_REGION_SLIME)
		*medium_out = SG_RUNE_MEDIUM_SLIME;
	else
		*medium_out = SG_RUNE_MEDIUM_DRY;
	return 1;
}

static int RegionFactsValid(const sg_phase_catalog_source_t *source,
	uint32_t region_index)
{
	const sg_configuration_semantic_region_t *region =
		&source->semantics->regions[region_index];
	uint32_t flags = region->flags;
	uint32_t water_flags = region->water_type & SG_HOST_MASK_WATER;
	sg_rune_medium_t medium;

	/* Semantic producers derive IDs from the immutable source position.  The
	 * cell is not enough to identify a region: reject caller-selected or merely
	 * increasing IDs before any phase can be issued from them. */
	if (region->cell >= source->configuration->cell_count ||
		region->id != (((uint64_t)region->cell << 32) |
			(uint64_t)region_index) ||
		!Finite3(region->interior_witness.value) ||
		!Finite3(region->bounds.mins.value) || !Finite3(region->bounds.maxs.value) ||
		region->bounds.mins.value[0] >= region->bounds.maxs.value[0] ||
		region->bounds.mins.value[1] >= region->bounds.maxs.value[1] ||
		region->bounds.mins.value[2] >= region->bounds.maxs.value[2] ||
		region->interior_witness.value[0] < region->bounds.mins.value[0] ||
		region->interior_witness.value[0] > region->bounds.maxs.value[0] ||
		region->interior_witness.value[1] < region->bounds.mins.value[1] ||
		region->interior_witness.value[1] > region->bounds.maxs.value[1] ||
		region->interior_witness.value[2] < region->bounds.mins.value[2] ||
		region->interior_witness.value[2] > region->bounds.maxs.value[2] ||
		region->first_face > source->semantics->face_count ||
		region->face_count > source->semantics->face_count - region->first_face ||
		(flags & ~(uint32_t)(SG_CONFIGURATION_SEMANTIC_REGION_WATER |
			SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
			SG_CONFIGURATION_SEMANTIC_REGION_SLIME |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD |
			SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT |
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)) != 0U ||
		(flags & (SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)) == 0U ||
		(flags & (SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)) ==
			(SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
				SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) ||
		region->water_level > 3U || !RegionMedium(region, &medium))
		return 0;
	if ((water_flags & (water_flags - 1U)) != 0U)
		return 0;
	if (region->water_level == 0U)
	{
		if (medium != SG_RUNE_MEDIUM_DRY || water_flags != 0U)
			return 0;
	}
	else
	{
		if (medium == SG_RUNE_MEDIUM_DRY || water_flags == 0U)
			return 0;
		if (medium == SG_RUNE_MEDIUM_WATER &&
			(region->water_type & SG_HOST_CONTENTS_WATER) == 0U)
			return 0;
		if (medium == SG_RUNE_MEDIUM_LAVA &&
			(region->water_type & SG_HOST_CONTENTS_LAVA) == 0U)
			return 0;
		if (medium == SG_RUNE_MEDIUM_SLIME &&
			(region->water_type & SG_HOST_CONTENTS_SLIME) == 0U)
			return 0;
	}
	return (medium == SG_RUNE_MEDIUM_LAVA || medium == SG_RUNE_MEDIUM_SLIME) ==
		((flags & SG_CONFIGURATION_SEMANTIC_REGION_HAZARD) != 0U);
}

static int CellReferenceValid(const sg_phase_catalog_source_t *source,
	uint32_t cell)
{
	const sg_configuration_cell_t *record = &source->configuration->cells[cell];
	sg_rune_order_key_t order;

	return SG_RuneModelStableIdValid(&record->id.value) &&
		SG_RuneModelOrderKeyValid(&record->order) &&
		SG_RuneModelStableIdToOrderKey(&record->id.value, &order) &&
		order.source_set_identity == source->authority->identity.source_set_identity &&
		order.domain == SG_RUNE_ORDER_CELL &&
		SG_RuneModelOrderKeyCompare(&record->order, &order) == 0;
}

static int FindRegion(const sg_phase_catalog_source_t *source,
	uint64_t region_id, uint32_t *region_out)
{
	uint32_t first = 0U;
	uint32_t last = source->semantics->region_count;

	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		uint64_t candidate = source->semantics->regions[middle].id;

		if (candidate == region_id)
		{
			if (region_out)
				*region_out = middle;
			return 1;
		}
		if (candidate < region_id)
			first = middle + 1U;
		else
			last = middle;
	}
	return 0;
}

static int ProviderSupportCompare(const sg_phase_mover_support_t *left,
	const sg_phase_mover_support_t *right)
{
	int comparison;

	if (left->semantic_region_id != right->semantic_region_id)
		return left->semantic_region_id < right->semantic_region_id ? -1 : 1;
	comparison = StableIdCompare(&left->mechanism.value,
		&right->mechanism.value);
	if (comparison != 0)
		return comparison;
	return 0;
}

static int ProviderSupportsValid(const sg_phase_catalog_source_t *source,
	const sg_phase_mover_support_provider_payload_t *provider)
{
	uint32_t index;

	if (!provider || !SG_PhaseCatalogIdentityEqual(&provider->identity,
			&source->authority->identity) ||
		(provider->support_count != 0U && !provider->supports) ||
		(provider->fact_count != 0U && !provider->facts) ||
		provider->completion < SG_PHASE_CATALOG_COMPLETE ||
		provider->completion >= SG_PHASE_CATALOG_COMPLETION_COUNT)
		return 0;
	for (index = 0U; index < provider->support_count; index++)
	{
		const sg_phase_mover_support_t *support = &provider->supports[index];
		sg_rune_order_key_t order;
		uint32_t region;

		if (!FindRegion(source, support->semantic_region_id, &region) ||
			support->mechanism.value.source_set_identity !=
				source->authority->identity.source_set_identity ||
			!SG_RuneModelStableIdValid(&support->mechanism.value) ||
			!SG_RuneModelStableIdToOrderKey(&support->mechanism.value, &order) ||
			order.domain != SG_RUNE_ORDER_MECHANISM ||
			order.source_index >= SG_RUNE_MODEL_MAX_MECHANISMS ||
			support->mechanism_state_mask == 0U ||
			(support->mechanism_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			(index != 0U && ProviderSupportCompare(
				&provider->supports[index - 1U], support) >= 0))
			return 0;
	}
	return (provider->support_count == 0U) ==
		(provider->completion == SG_PHASE_CATALOG_PROVEN_EMPTY) &&
		(provider->fact_count == 0U) ==
		(provider->completion == SG_PHASE_CATALOG_PROVEN_EMPTY);
}

static int ConfigurationRelationsValid(const sg_phase_catalog_source_t *source)
{
	const sg_configuration_space_t *configuration = source->configuration;
	uint32_t index;

	if ((configuration->portal_count != 0U && !configuration->portals) ||
		(configuration->stance_overlap_count != 0U &&
			!configuration->stance_overlaps))
		return 0;
	for (index = 0U; index < configuration->portal_count; index++)
	{
		const sg_configuration_portal_t *portal = &configuration->portals[index];
		sg_rune_order_key_t order;

		if (!SG_RuneModelStableIdValid(&portal->id.value) ||
			portal->id.value.source_set_identity !=
				source->authority->identity.source_set_identity ||
			!SG_RuneModelOrderKeyValid(&portal->order) ||
			portal->order.domain != SG_RUNE_ORDER_PORTAL ||
			portal->order.source_set_identity !=
				source->authority->identity.source_set_identity ||
			!SG_RuneModelStableIdToOrderKey(&portal->id.value, &order) ||
			SG_RuneModelOrderKeyCompare(&portal->order, &order) != 0 ||
			portal->from_cell >= configuration->cell_count ||
			portal->to_cell >= configuration->cell_count ||
			portal->from_cell == portal->to_cell ||
			portal->stance < 0 || portal->stance >= SG_RUNE_STANCE_COUNT ||
			configuration->cells[portal->from_cell].stance != portal->stance ||
			configuration->cells[portal->to_cell].stance != portal->stance ||
			!Finite3(portal->plane.normal) || !isfinite(portal->plane.distance) ||
			!isfinite(portal->clearance) || portal->clearance <= 0.0f)
			return 0;
	}
	for (index = 0U; index < configuration->stance_overlap_count; index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&configuration->stance_overlaps[index];

		if (overlap->standing_cell >= configuration->cell_count ||
			overlap->crouching_cell >= configuration->cell_count ||
			overlap->standing_cell == overlap->crouching_cell ||
			configuration->cells[overlap->standing_cell].stance !=
				SG_RUNE_STANCE_STANDING ||
			configuration->cells[overlap->crouching_cell].stance !=
				SG_RUNE_STANCE_CROUCHING || !Finite3(overlap->interior_witness.value) ||
			!Finite3(overlap->bounds.mins.value) ||
			!Finite3(overlap->bounds.maxs.value) ||
			overlap->bounds.mins.value[0] >= overlap->bounds.maxs.value[0] ||
			overlap->bounds.mins.value[1] >= overlap->bounds.maxs.value[1] ||
			overlap->bounds.mins.value[2] >= overlap->bounds.maxs.value[2])
			return 0;
	}
	return 1;
}

static int SourceValidateWithProvider(const sg_phase_catalog_source_t *source,
	const sg_phase_mover_support_provider_payload_t *provider,
	sg_phase_catalog_error_t *error_out)
{
	const sg_rune_model_identity_t *identity;
	uint32_t cell;
	uint32_t region;

	if (!source || !source->authority || !source->configuration ||
		!source->semantics || !provider)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	identity = &source->authority->identity;
	if (!IdentityValid(identity))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	if (!SG_PhaseCatalogIdentityEqual(identity,
		&source->configuration->identity) ||
		!SG_PhaseCatalogIdentityEqual(identity, &source->semantics->identity) ||
		!SG_PhaseCatalogIdentityEqual(identity, &provider->identity))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_IDENTITY_MISMATCH, 0U);
		return 0;
	}
	/* These are hostile-input limits on producer-owned source collections.
	 * Exact phase and transition limits are enforced again after semantic
	 * deduplication, where they constrain the published output. */
	if (source->configuration->cell_count > SG_RUNE_MODEL_MAX_CELLS ||
		source->configuration->portal_count > SG_RUNE_MODEL_MAX_PORTALS ||
		source->configuration->stance_overlap_count >
			SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		source->semantics->region_count > SG_RUNE_MODEL_MAX_PHASES ||
		source->semantics->face_count > SG_RUNE_MODEL_MAX_PLANES ||
		provider->support_count > SG_RUNE_MODEL_MAX_PHASES ||
		provider->fact_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
		return 0;
	}
	if ((source->configuration->cell_count != 0U &&
			!source->configuration->cells) ||
		(source->semantics->region_count != 0U && !source->semantics->regions) ||
		(source->semantics->face_count != 0U && !source->semantics->faces) ||
		!ConfigurationRelationsValid(source))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	if (!ProviderSupportsValid(source, provider))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	for (cell = 0U; cell < source->configuration->cell_count; cell++)
	{
		const sg_configuration_cell_t *record =
			&source->configuration->cells[cell];

		if (record->stance < 0 || record->stance >= SG_RUNE_STANCE_COUNT ||
			!CellReferenceValid(source, cell))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, cell);
			return 0;
		}
	}
	if (source->configuration->cell_count == 0U)
	{
		if (source->semantics->region_count != 0U ||
			source->semantics->face_count != 0U ||
			provider->support_count != 0U || provider->fact_count != 0U ||
			provider->completion !=
				SG_PHASE_CATALOG_PROVEN_EMPTY)
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
			return 0;
		}
		return 1;
	}
	if (source->semantics->region_count == 0U)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		return 0;
	}
	for (region = 0U; region < source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&source->semantics->regions[region];

		if (!RegionFactsValid(source, region) ||
			(region != 0U &&
				(source->semantics->regions[region - 1U].cell > record->cell ||
				 (source->semantics->regions[region - 1U].cell == record->cell &&
					source->semantics->regions[region - 1U].id >= record->id) ||
				 source->semantics->regions[region - 1U].id >= record->id)))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
			return 0;
		}
	}
	region = 0U;
	for (cell = 0U; cell < source->configuration->cell_count; cell++)
	{
		uint32_t first = region;

		while (region < source->semantics->region_count &&
			source->semantics->regions[region].cell == cell)
			region++;
		if (first == region)
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, cell);
			return 0;
		}
	}
	if (region != source->semantics->region_count)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
		return 0;
	}
	{
		const sg_configuration_semantic_region_t *last =
			&source->semantics->regions[source->semantics->region_count - 1U];

		if (last->first_face > source->semantics->face_count ||
			last->face_count > source->semantics->face_count - last->first_face ||
			last->first_face + last->face_count != source->semantics->face_count)
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				source->semantics->region_count - 1U);
			return 0;
		}
	}
	return 1;
}

int SG_PhaseCatalogSourceValidate(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_error_t *error_out)
{
	const sg_phase_mover_support_provider_payload_t *provider;

	if (!source || !source->mover_support_owner ||
		!source->mover_support_provider)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	if (!SG_PhaseMoverSupportProviderHeaderValid(
			source->mover_support_owner, source->mover_support_provider))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	provider = SG_PHASE_SOURCE_PROVIDER(source);
	return SourceValidateWithProvider(source, provider, error_out);
}

static int PhaseEquivalent(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->stance == right->stance &&
		left->motion == right->motion && left->support == right->support &&
		left->medium == right->medium && left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		StableIdEqual(&left->mover.value, &right->mover.value) &&
		Interval3Equal(&left->velocity, &right->velocity) &&
		IntervalEqual(&left->elapsed_ms, &right->elapsed_ms) &&
		left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

int SG_PhaseCatalogPhaseEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && StableIdEqual(&left->id.value, &right->id.value) &&
		SG_RuneModelOrderKeyCompare(&left->order, &right->order) == 0 &&
		PhaseEquivalent(left, right);
}

int SG_PhaseCatalogBindingEqual(const sg_phase_catalog_binding_t *left,
	const sg_phase_catalog_binding_t *right)
{
	return left && right &&
		left->semantic_region_id == right->semantic_region_id &&
		left->configuration_cell == right->configuration_cell &&
		StableIdEqual(&left->phase.value, &right->phase.value) &&
		left->mechanism_state_mask == right->mechanism_state_mask;
}

static uint32_t CanonicalFloatBits(float value)
{
	uint32_t bits;

	if (value == 0.0f)
		value = 0.0f;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static uint64_t HashMix(uint64_t hash, uint64_t value)
{
	hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
	return hash;
}

static uint64_t PhaseHash(const sg_rune_phase_basis_t *phase,
	uint32_t cell)
{
	uint32_t bits;
	uint64_t hash = UINT64_C(0xcbf29ce484222325);

	hash = HashMix(hash, cell);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->stance);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->motion);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->support);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->medium);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->void_relation);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->reference_frame);
	hash = HashMix(hash, phase->mover.value.source_set_identity);
	hash = HashMix(hash, phase->mover.value.high);
	hash = HashMix(hash, phase->mover.value.low);
	bits = CanonicalFloatBits(phase->velocity.x.min_value);
	hash = HashMix(hash, bits);
	bits = CanonicalFloatBits(phase->velocity.x.max_value);
	hash = HashMix(hash, bits);
	bits = CanonicalFloatBits(phase->elapsed_ms.min_value);
	hash = HashMix(hash, bits);
	bits = CanonicalFloatBits(phase->elapsed_ms.max_value);
	hash = HashMix(hash, bits);
	hash = HashMix(hash, phase->time_quantum_ms);
	hash = HashMix(hash, phase->time_horizon_ms);
	return hash;
}

static uint64_t NeutralPhaseHash(const sg_rune_phase_basis_t *phase,
	uint32_t cell)
{
	uint32_t bits;
	uint64_t hash = UINT64_C(0xcbf29ce484222325);

	hash = HashMix(hash, cell);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->motion);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->support);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->medium);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->void_relation);
	hash = HashMix(hash, (uint64_t)(uint32_t)phase->reference_frame);
	hash = HashMix(hash, phase->mover.value.source_set_identity);
	hash = HashMix(hash, phase->mover.value.high);
	hash = HashMix(hash, phase->mover.value.low);
	bits = CanonicalFloatBits(phase->velocity.x.min_value);
	hash = HashMix(hash, bits);
	bits = CanonicalFloatBits(phase->velocity.x.max_value);
	hash = HashMix(hash, bits);
	bits = CanonicalFloatBits(phase->elapsed_ms.min_value);
	hash = HashMix(hash, bits);
	bits = CanonicalFloatBits(phase->elapsed_ms.max_value);
	hash = HashMix(hash, bits);
	hash = HashMix(hash, phase->time_quantum_ms);
	hash = HashMix(hash, phase->time_horizon_ms);
	return hash;
}

static int HashCapacityFor(uint32_t item_count, uint32_t *capacity_out)
{
	uint32_t capacity = 16U;

	while ((uint64_t)item_count * UINT64_C(10) >=
		(uint64_t)capacity * UINT64_C(7))
	{
		if (capacity > UINT32_MAX / 2U)
			return 0;
		capacity *= 2U;
	}
	*capacity_out = capacity;
	return 1;
}

static int HashRebuild(uint32_t **hash_out, uint32_t *capacity_out,
	const sg_rune_phase_basis_t *phases, uint32_t phase_count,
	int neutral)
{
	uint32_t capacity;
	uint32_t *hash;
	uint32_t index;

	if (!HashCapacityFor(phase_count + 1U, &capacity) ||
		!AllocationFits((size_t)capacity, sizeof(*hash)))
		return 0;
	hash = malloc((size_t)capacity * sizeof(*hash));
	if (!hash)
		return 0;
	for (index = 0U; index < capacity; index++)
		hash[index] = SG_PHASE_CATALOG_INDEX_NONE;
	for (index = 0U; index < phase_count; index++)
	{
		uint64_t value = neutral ? NeutralPhaseHash(&phases[index],
			phases[index].order.source_index) : PhaseHash(&phases[index],
			phases[index].order.source_index);
		uint32_t slot = (uint32_t)value & (capacity - 1U);

		while (hash[slot] != SG_PHASE_CATALOG_INDEX_NONE)
			slot = (slot + 1U) & (capacity - 1U);
		hash[slot] = index;
	}
	free(*hash_out);
	*hash_out = hash;
	*capacity_out = capacity;
	return 1;
}

static int HashInsert(const sg_rune_phase_basis_t *phase, uint32_t phase_index,
	uint32_t *hash, uint32_t capacity, int neutral)
{
	uint64_t value = neutral ? NeutralPhaseHash(phase,
		phase->order.source_index) : PhaseHash(phase, phase->order.source_index);
	uint32_t slot;

	if (!hash || capacity == 0U || (capacity & (capacity - 1U)) != 0U)
		return 0;
	slot = (uint32_t)value & (capacity - 1U);
	while (hash[slot] != SG_PHASE_CATALOG_INDEX_NONE)
		slot = (slot + 1U) & (capacity - 1U);
	hash[slot] = phase_index;
	return 1;
}

static int HashEnsure(sg_phase_catalog_expected_t *expected,
	uint32_t phase_index)
{
	uint32_t required = phase_index + 1U;

	if (required == 0U)
		return 0;
	if (expected->phase_hash_capacity != 0U &&
		(uint64_t)required * UINT64_C(10) <
			(uint64_t)expected->phase_hash_capacity * UINT64_C(7) &&
		expected->phase_neutral_hash_capacity != 0U &&
		(uint64_t)required * UINT64_C(10) <
			(uint64_t)expected->phase_neutral_hash_capacity * UINT64_C(7))
		return 1;
	if (!HashRebuild(&expected->phase_hash, &expected->phase_hash_capacity,
		expected->phases, expected->phase_count - 1U, 0) ||
		!HashRebuild(&expected->phase_neutral_hash,
			&expected->phase_neutral_hash_capacity, expected->phases,
			expected->phase_count - 1U, 1))
		return 0;
	return 1;
}

static int PhaseFindForRegion(const sg_phase_catalog_expected_t *expected,
	uint32_t cell, uint32_t region, const sg_rune_phase_basis_t *candidate,
	uint32_t *phase_out)
{
	uint64_t value;
	uint32_t slot;

	if (!expected || !candidate || !expected->phase_hash ||
		expected->phase_hash_capacity == 0U)
		return 0;
	value = PhaseHash(candidate, cell);
	slot = (uint32_t)value & (expected->phase_hash_capacity - 1U);
	while (expected->phase_hash[slot] != SG_PHASE_CATALOG_INDEX_NONE)
	{
		uint32_t phase = expected->phase_hash[slot];

		if (phase < expected->phase_count &&
			expected->phase_region_by_phase[phase] == region &&
			expected->phases[phase].order.source_index == cell &&
			PhaseEquivalent(&expected->phases[phase], candidate))
		{
			if (phase_out)
				*phase_out = phase;
			return 1;
		}
		slot = (slot + 1U) & (expected->phase_hash_capacity - 1U);
	}
	return 0;
}

static int PhaseNeutralEquivalent(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		StableIdEqual(&left->mover.value, &right->mover.value) &&
		Interval3Equal(&left->velocity, &right->velocity) &&
		IntervalEqual(&left->elapsed_ms, &right->elapsed_ms) &&
		left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int PhaseNeutralFind(const sg_phase_catalog_expected_t *expected,
	uint32_t cell, const sg_rune_phase_basis_t *candidate,
	sg_rune_stance_t stance, uint32_t *phase_out)
{
	uint64_t value;
	uint32_t slot;

	if (!expected || !candidate || !expected->phase_neutral_hash ||
		expected->phase_neutral_hash_capacity == 0U)
		return 0;
	value = NeutralPhaseHash(candidate, cell);
	slot = (uint32_t)value & (expected->phase_neutral_hash_capacity - 1U);
	while (expected->phase_neutral_hash[slot] != SG_PHASE_CATALOG_INDEX_NONE)
	{
		uint32_t phase = expected->phase_neutral_hash[slot];

		if (phase < expected->phase_count &&
			expected->phases[phase].order.source_index == cell &&
			expected->phases[phase].stance == stance &&
			PhaseNeutralEquivalent(&expected->phases[phase], candidate))
		{
			if (phase_out)
				*phase_out = phase;
			return 1;
		}
		slot = (slot + 1U) & (expected->phase_neutral_hash_capacity - 1U);
	}
	return 0;
}

static int ExpectedReservePhases(sg_phase_catalog_expected_t *expected,
	uint32_t required, sg_phase_catalog_error_t *error_out)
{
	uint32_t capacity;
	uint32_t previous_capacity;
	void *grown;
	void *grown_regions;

	if (required > SG_RUNE_MODEL_MAX_PHASES)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	if (required <= expected->phase_capacity &&
		expected->phase_region_by_phase != NULL &&
		expected->phase_region_capacity >= expected->phase_capacity)
		return 1;
	capacity = expected->phase_capacity == 0U ? 16U : expected->phase_capacity;
	previous_capacity = expected->phase_capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_MODEL_MAX_PHASES / 2U)
			capacity = SG_RUNE_MODEL_MAX_PHASES;
		else
			capacity *= 2U;
	}
	if (!AllocationFits((size_t)capacity, sizeof(*expected->phases)))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	grown = realloc(expected->phases,
		(size_t)capacity * sizeof(*expected->phases));
	if (!grown)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, required);
		return 0;
	}
	expected->phases = grown;
	expected->phase_capacity = capacity;
	if (!AllocationFits((size_t)capacity, sizeof(*expected->phase_region_by_phase)))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	grown_regions = realloc(expected->phase_region_by_phase,
		(size_t)capacity * sizeof(*expected->phase_region_by_phase));
	if (!grown_regions)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, required);
		return 0;
	}
	expected->phase_region_by_phase = grown_regions;
	for (capacity = previous_capacity; capacity < expected->phase_capacity;
		capacity++)
		expected->phase_region_by_phase[capacity] = SG_PHASE_CATALOG_INDEX_NONE;
	expected->phase_region_capacity = expected->phase_capacity;
	return 1;
}

static int ExpectedReserveBindings(sg_phase_catalog_expected_t *expected,
	uint32_t required, sg_phase_catalog_error_t *error_out)
{
	uint32_t capacity;
	void *grown;

	if (required == 0U || required > SG_PHASE_CATALOG_MAX_BINDINGS)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	if (required <= expected->binding_capacity)
		return 1;
	capacity = expected->binding_capacity == 0U ? 16U : expected->binding_capacity;
	while (capacity < required)
	{
		if (capacity > SG_PHASE_CATALOG_MAX_BINDINGS / 2U)
			capacity = SG_PHASE_CATALOG_MAX_BINDINGS;
		else
			capacity *= 2U;
	}
	if (!AllocationFits((size_t)capacity, sizeof(*expected->bindings)))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	grown = realloc(expected->bindings,
		(size_t)capacity * sizeof(*expected->bindings));
	if (!grown)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, required);
		return 0;
	}
	expected->bindings = grown;
	expected->binding_capacity = capacity;
	return 1;
}

static int ExpectedReservePairs(sg_phase_catalog_expected_t *expected,
	uint32_t required, sg_phase_catalog_error_t *error_out)
{
	uint32_t capacity;
	void *grown;

	/* This is construction scratch.  Do not reject source rows before exact
	 * transition deduplication; only uint32/size_t representation limits are
	 * arithmetic bounds here. */
	if (required == 0U)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	if (required <= expected->transition_pair_capacity)
		return 1;
	capacity = expected->transition_pair_capacity == 0U ? 16U :
		expected->transition_pair_capacity;
	while (capacity < required)
	{
		if (capacity > UINT32_MAX / 2U)
			capacity = UINT32_MAX;
		else
			capacity *= 2U;
	}
	if (!AllocationFits((size_t)capacity, sizeof(*expected->transition_pairs)))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW, required);
		return 0;
	}
	grown = realloc(expected->transition_pairs,
		(size_t)capacity * sizeof(*expected->transition_pairs));
	if (!grown)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, required);
		return 0;
	}
	expected->transition_pairs = grown;
	expected->transition_pair_capacity = capacity;
	return 1;
}

static int AppendPhase(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected, const sg_rune_phase_basis_t *candidate,
	uint32_t region, uint32_t cell, uint32_t *cell_ordinals, uint32_t *phase_out,
	sg_phase_catalog_error_t *error_out)
{
	uint32_t phase;
	sg_rune_phase_basis_t record;

	if (PhaseFindForRegion(expected, cell, region, candidate, &phase))
	{
		if (phase_out)
			*phase_out = phase;
		return 1;
	}
	if (!source || !source->configuration || !source->semantics ||
		!cell_ordinals || region >= source->semantics->region_count ||
		cell >= source->configuration->cell_count ||
		source->semantics->regions[region].cell != cell)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, cell);
		return 0;
	}
	/* A cell carries one phase per distinct basis in each of its semantic
	 * regions, so its phase count follows from the BSP partition.  Only the
	 * global phase bound and the allocation below may stop construction. */
	if (expected->phase_count >= SG_RUNE_MODEL_MAX_PHASES ||
		!ExpectedReservePhases(expected, expected->phase_count + 1U, error_out))
		return 0;
	record = *candidate;
	record.order.source_index = cell;
	record.order.local_ordinal = cell_ordinals[cell]++;
	record.id.value = SG_RuneModelStableIdFromOrderKey(&record.order);
	if (!SG_RuneModelPhaseValid(&record))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_PHASE,
			expected->phase_count);
		return 0;
	}
	phase = expected->phase_count++;
	expected->phases[phase] = record;
	expected->phase_region_by_phase[phase] = region;
	if (!HashEnsure(expected, phase) ||
		!HashInsert(&record, phase, expected->phase_hash,
			expected->phase_hash_capacity, 0) ||
		!HashInsert(&record, phase, expected->phase_neutral_hash,
			expected->phase_neutral_hash_capacity, 1))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, phase);
		return 0;
	}
	(void)source;
	if (phase_out)
		*phase_out = phase;
	return 1;
}

static int AppendBinding(sg_phase_catalog_expected_t *expected,
	const sg_configuration_semantic_region_t *region, uint32_t cell,
	uint32_t phase, sg_phase_mechanism_state_mask_t state_mask,
	sg_phase_catalog_error_t *error_out)
{
	sg_phase_catalog_binding_t *binding;
	uint32_t index;
	uint32_t insertion;

	if (!expected || !region || phase >= expected->phase_count)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, phase);
		return 0;
	}
	insertion = expected->binding_count;
	for (index = 0U; index < expected->binding_count; index++)
	{
		binding = &expected->bindings[index];
		if (binding->semantic_region_id == region->id &&
			binding->configuration_cell == cell &&
			StableIdEqual(&binding->phase.value,
				&expected->phases[phase].id.value))
		{
			binding->mechanism_state_mask |= state_mask;
			return 1;
		}
		if (insertion == expected->binding_count &&
			binding->semantic_region_id > region->id)
			insertion = index;
	}
	if (expected->binding_count >= SG_PHASE_CATALOG_MAX_BINDINGS)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			expected->binding_count);
		return 0;
	}
	if (!ExpectedReserveBindings(expected, expected->binding_count + 1U,
		error_out))
		return 0;
	if (insertion < expected->binding_count)
		memmove(&expected->bindings[insertion + 1U],
			&expected->bindings[insertion],
			(size_t)(expected->binding_count - insertion) *
				sizeof(*expected->bindings));
	binding = &expected->bindings[insertion];
	expected->binding_count++;
	memset(binding, 0, sizeof(*binding));
	binding->semantic_region_id = region->id;
	binding->configuration_cell = cell;
	binding->phase = expected->phases[phase].id;
	binding->mechanism_state_mask = state_mask;
	return 1;
}

static void FillPhase(const sg_phase_catalog_source_t *source,
	const sg_configuration_semantic_region_t *region, int mover,
	const sg_rune_mechanism_ref_t *mechanism, uint32_t variant,
	sg_rune_phase_basis_t *phase_out)
{
	sg_rune_medium_t medium = SG_RUNE_MEDIUM_DRY;
	float speed = source->authority->identity.physics.max_velocity;

	memset(phase_out, 0, sizeof(*phase_out));
	phase_out->order.source_set_identity =
		source->authority->identity.source_set_identity;
	phase_out->order.domain = SG_RUNE_ORDER_PHASE;
	phase_out->order.source_index = region->cell;
	phase_out->order.variant = variant;
	phase_out->stance = source->configuration->cells[region->cell].stance;
	(void)RegionMedium(region, &medium);
	phase_out->motion = mover ? SG_RUNE_MOTION_SUPPORTED :
		(region->water_level >= 2U ? SG_RUNE_MOTION_SWIMMING :
			((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U ?
				SG_RUNE_MOTION_SUPPORTED : SG_RUNE_MOTION_AIRBORNE));
	phase_out->support = mover ? SG_RUNE_SUPPORT_MOVER :
		(phase_out->motion == SG_RUNE_MOTION_SUPPORTED ?
			SG_RUNE_SUPPORT_SUPPORTED : SG_RUNE_SUPPORT_NONE);
	phase_out->medium = medium;
	phase_out->void_relation =
		(region->flags & SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) != 0U ?
			SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR;
	phase_out->reference_frame = mover ? SG_RUNE_FRAME_MOVER_RELATIVE :
		SG_RUNE_FRAME_WORLD;
	phase_out->mover = mover && mechanism ? *mechanism :
		SG_RUNE_MECHANISM_REF_NONE;
	phase_out->velocity.x.min_value = -speed;
	phase_out->velocity.x.max_value = speed;
	phase_out->velocity.y = phase_out->velocity.x;
	phase_out->velocity.z = phase_out->velocity.x;
	phase_out->elapsed_ms.min_value = 0.0f;
	phase_out->elapsed_ms.max_value = (float)
		source->authority->identity.physics.frame_ms;
	phase_out->time_quantum_ms = source->authority->identity.physics.substep_ms;
	phase_out->time_horizon_ms = source->authority->identity.physics.frame_ms;
}

static uint64_t PairHashU32(uint64_t hash, uint32_t value)
{
	uint32_t shift;
	for (shift = 0U; shift != 32U; shift += 8U)
		hash = (hash ^ (uint8_t)(value >> shift)) * UINT64_C(1099511628211);
	return hash;
}

static uint64_t PairHashU64(uint64_t hash, uint64_t value)
{
	uint32_t shift;
	for (shift = 0U; shift != 64U; shift += 8U)
		hash = (hash ^ (uint8_t)(value >> shift)) * UINT64_C(1099511628211);
	return hash;
}

static uint64_t PairHashFloat(uint64_t hash, float value)
{
	uint32_t bits;
	if (value == 0.0f) value = 0.0f;
	memcpy(&bits, &value, sizeof(bits));
	return PairHashU32(hash, bits);
}

static uint64_t PairHashStable(uint64_t hash,
	const sg_rune_stable_id_t *value)
{
	hash = PairHashU64(hash, value->source_set_identity);
	hash = PairHashU64(hash, value->high);
	return PairHashU64(hash, value->low);
}

static uint64_t TransitionPairHash(
	const sg_phase_catalog_transition_pair_t *pair)
{
	const sg_rune_phase_transition_t *t = &pair->transition;
	uint64_t hash = UINT64_C(1469598103934665603);

	hash = PairHashStable(hash, &t->cell.value);
	hash = PairHashStable(hash, &t->source_phase.value);
	hash = PairHashStable(hash, &t->destination_phase.value);
	hash = PairHashU32(hash, (uint32_t)t->kind);
	hash = PairHashFloat(hash, t->duration_ms.min_value);
	hash = PairHashFloat(hash, t->duration_ms.max_value);
	hash = PairHashU32(hash, t->flags);
	return PairHashStable(hash, &t->destination_cell.value);
}

static int TransitionPairFieldsEqual(
	const sg_phase_catalog_transition_pair_t *left,
	const sg_phase_catalog_transition_pair_t *right)
{
	const sg_rune_phase_transition_t *lt = &left->transition;
	const sg_rune_phase_transition_t *rt = &right->transition;

	return SG_RuneModelStableIdEqual(&lt->cell.value, &rt->cell.value) &&
		SG_RuneModelStableIdEqual(&lt->source_phase.value,
			&rt->source_phase.value) &&
		SG_RuneModelStableIdEqual(&lt->destination_phase.value,
			&rt->destination_phase.value) && lt->kind == rt->kind &&
		lt->duration_ms.min_value == rt->duration_ms.min_value &&
		lt->duration_ms.max_value == rt->duration_ms.max_value &&
		lt->flags == rt->flags && SG_RuneModelStableIdEqual(
			&lt->destination_cell.value, &rt->destination_cell.value);
}

static int AppendPair(sg_phase_catalog_expected_t *expected,
	const sg_rune_phase_transition_t *transition,
	const sg_phase_catalog_transition_evidence_t *evidence,
	sg_phase_catalog_error_t *error_out)
{
	uint64_t hash;
	uint32_t slot;
	uint32_t index;
	uint32_t needed_hash_capacity;
	uint32_t *grown_hash;
	sg_phase_catalog_transition_pair_t candidate;

	memset(&candidate, 0, sizeof(candidate));
	candidate.transition = *transition;
	candidate.evidence = *evidence;
	hash = TransitionPairHash(&candidate);
	if (expected->transition_pair_hash_capacity == 0U)
	{
		expected->transition_pair_hash_capacity = 32U;
		expected->transition_pair_hash = calloc(
			expected->transition_pair_hash_capacity,
			sizeof(*expected->transition_pair_hash));
		if (!expected->transition_pair_hash)
		{
			expected->transition_pair_hash_capacity = 0U;
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
	}
	slot = (uint32_t)hash & (expected->transition_pair_hash_capacity - 1U);
	while (expected->transition_pair_hash[slot] != 0U)
	{
		index = expected->transition_pair_hash[slot] - 1U;
		if (TransitionPairFieldsEqual(&expected->transition_pairs[index],
				&candidate))
			return 1;
		slot = (slot + 1U) & (expected->transition_pair_hash_capacity - 1U);
	}
	if (expected->transition_pair_count >=
		SG_PHASE_CATALOG_TRANSITION_APPEND_LIMIT)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			expected->transition_pair_count);
		return 0;
	}
	if (expected->transition_pair_hash_capacity / 2U <=
		expected->transition_pair_count)
	{
		if (expected->transition_pair_hash_capacity > UINT32_MAX / 2U)
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
				expected->transition_pair_count);
			return 0;
		}
		needed_hash_capacity = expected->transition_pair_hash_capacity * 2U;
		grown_hash = calloc(needed_hash_capacity, sizeof(*grown_hash));
		if (!grown_hash)
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		for (index = 0U; index < expected->transition_pair_count; index++)
		{
			uint64_t existing_hash = TransitionPairHash(
				&expected->transition_pairs[index]);
			slot = (uint32_t)existing_hash & (needed_hash_capacity - 1U);
			while (grown_hash[slot] != 0U)
				slot = (slot + 1U) & (needed_hash_capacity - 1U);
			grown_hash[slot] = index + 1U;
		}
		free(expected->transition_pair_hash);
		expected->transition_pair_hash = grown_hash;
		expected->transition_pair_hash_capacity = needed_hash_capacity;
		slot = (uint32_t)hash & (needed_hash_capacity - 1U);
		while (expected->transition_pair_hash[slot] != 0U)
			slot = (slot + 1U) & (needed_hash_capacity - 1U);
	}
	if (!ExpectedReservePairs(expected, expected->transition_pair_count + 1U,
		error_out))
		return 0;
	expected->transition_pairs[expected->transition_pair_count] = candidate;
	expected->transition_pair_hash[slot] = expected->transition_pair_count + 1U;
	expected->transition_pair_count++;
	return 1;
}

static int PhaseCellRange(const sg_phase_catalog_expected_t *expected,
	uint32_t cell, uint32_t *first_out, uint32_t *last_out)
{
	uint32_t first = 0U;
	uint32_t last = expected->phase_count;
	uint32_t middle;

	while (first < last)
	{
		middle = first + (last - first) / 2U;
		if (expected->phases[middle].order.source_index < cell)
			first = middle + 1U;
		else
			last = middle;
	}
	*first_out = first;
	last = expected->phase_count;
	while (first < last)
	{
		middle = first + (last - first) / 2U;
		if (expected->phases[middle].order.source_index <= cell)
			first = middle + 1U;
		else
			last = middle;
	}
	*last_out = first;
	return *first_out != *last_out;
}

static int AppendStanceTransition(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected, uint32_t overlap_index,
	sg_phase_catalog_error_t *error_out)
{
	const sg_configuration_stance_overlap_t *overlap =
		&source->configuration->stance_overlaps[overlap_index];
	uint32_t source_first, source_last, destination_first, destination_last;
	uint32_t phase;
	uint32_t direction;

	if (!PhaseCellRange(expected, overlap->standing_cell, &source_first,
		&source_last) || !PhaseCellRange(expected, overlap->crouching_cell,
		&destination_first, &destination_last))
		return 0;
	/* The overlap is bidirectional.  Walk each source stance independently and
	 * use the neutral fingerprint index for the destination; there is no
	 * first-match shortcut, so every compatible pair is represented. */
	for (direction = 0U; direction < 2U; direction++)
	{
		uint32_t source_cell = direction == 0U ? overlap->standing_cell :
			overlap->crouching_cell;
		uint32_t destination_cell = direction == 0U ? overlap->crouching_cell :
			overlap->standing_cell;
		sg_rune_stance_t source_stance = direction == 0U ?
			SG_RUNE_STANCE_STANDING : SG_RUNE_STANCE_CROUCHING;
		sg_rune_stance_t destination_stance = direction == 0U ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
		uint32_t first = direction == 0U ? source_first : destination_first;
		uint32_t last = direction == 0U ? source_last : destination_last;

		for (phase = first; phase < last; phase++)
		{
			uint32_t candidate;
			sg_rune_phase_transition_t transition;
			sg_phase_catalog_transition_evidence_t evidence;
			uint32_t frame = source->authority->identity.physics.frame_ms;
			uint32_t quantum = source->authority->identity.physics.substep_ms;

			if (expected->phases[phase].stance != source_stance ||
				expected->phases[phase].reference_frame != SG_RUNE_FRAME_WORLD ||
				!PhaseNeutralFind(expected, destination_cell,
					&expected->phases[phase], destination_stance, &candidate))
				continue;
			memset(&transition, 0, sizeof(transition));
			memset(&evidence, 0, sizeof(evidence));
			transition.cell = source->configuration->cells[source_cell].id;
			transition.destination_cell =
				source->configuration->cells[destination_cell].id;
			transition.source_phase = expected->phases[phase].id;
			transition.destination_phase = expected->phases[candidate].id;
			transition.kind = SG_RUNE_PHASE_TRANSITION_STANCE;
			transition.flags = SG_RUNE_PHASE_TRANSITION_CROSS_CELL;
			transition.duration_ms = (sg_rune_interval_t){ (float)quantum,
				(float)frame };
			evidence.origin = SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP;
			evidence.source_record = overlap_index;
			evidence.destination_record = SG_PHASE_CATALOG_INDEX_NONE;
			evidence.source_cell = source_cell;
			evidence.destination_cell = destination_cell;
			evidence.source_region_id = source->semantics->regions[
				expected->phase_region_by_phase[phase]].id;
			evidence.destination_region_id = source->semantics->regions[
				expected->phase_region_by_phase[candidate]].id;
			if (!AppendPair(expected, &transition, &evidence, error_out))
				return 0;
		}
	}
	return 1;
}

static int AppendPortalTransition(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected, uint32_t portal_index, int reverse,
	sg_phase_catalog_error_t *error_out)
{
	const sg_configuration_portal_t *portal =
		&source->configuration->portals[portal_index];
	uint32_t source_cell = reverse ? portal->to_cell : portal->from_cell;
	uint32_t destination_cell = reverse ? portal->from_cell : portal->to_cell;
	uint32_t source_first;
	uint32_t source_last;
	uint32_t destination_first;
	uint32_t destination_last;
	uint32_t source_phase;

	if (!PhaseCellRange(expected, source_cell, &source_first, &source_last) ||
		!PhaseCellRange(expected, destination_cell, &destination_first,
			&destination_last))
		return 0;
	/* A portal owns geometric adjacency, not phase equivalence.  Enumerate each
	 * region-owned phase pair and admit world-relative changes in medium,
	 * support, or motion.  Mover-relative pairs require the same accepted mover
	 * and at least one common accepted mechanism state. */
	for (source_phase = source_first; source_phase < source_last; source_phase++)
	{
		uint32_t destination_phase;

		for (destination_phase = destination_first;
			destination_phase < destination_last; destination_phase++)
		{
			const sg_rune_phase_basis_t *source_basis =
				&expected->phases[source_phase];
			const sg_rune_phase_basis_t *destination_basis =
				&expected->phases[destination_phase];
			sg_phase_mechanism_state_mask_t source_states = 0U;
			sg_phase_mechanism_state_mask_t destination_states = 0U;
			sg_rune_phase_transition_t transition;
			sg_phase_catalog_transition_evidence_t evidence;
			uint32_t binding;
			int legal;

			if (source_basis->stance != portal->stance ||
				destination_basis->stance != portal->stance)
				continue;
			legal = source_basis->reference_frame == SG_RUNE_FRAME_WORLD &&
				destination_basis->reference_frame == SG_RUNE_FRAME_WORLD;
			if (!legal && source_basis->reference_frame ==
					SG_RUNE_FRAME_MOVER_RELATIVE &&
				destination_basis->reference_frame ==
					SG_RUNE_FRAME_MOVER_RELATIVE &&
				StableIdEqual(&source_basis->mover.value,
					&destination_basis->mover.value))
			{
				for (binding = 0U; binding < expected->binding_count; binding++)
				{
					const sg_phase_catalog_binding_t *record =
						&expected->bindings[binding];

					if (StableIdEqual(&record->phase.value,
						&source_basis->id.value))
						source_states |= record->mechanism_state_mask;
					if (StableIdEqual(&record->phase.value,
						&destination_basis->id.value))
						destination_states |= record->mechanism_state_mask;
				}
				legal = (source_states & destination_states) != 0U;
			}
			if (!legal)
				continue;
			memset(&transition, 0, sizeof(transition));
			memset(&evidence, 0, sizeof(evidence));
			transition.cell = source->configuration->cells[source_cell].id;
			transition.destination_cell =
				source->configuration->cells[destination_cell].id;
			transition.source_phase = source_basis->id;
			transition.destination_phase = destination_basis->id;
			transition.kind = SG_RUNE_PHASE_TRANSITION_PORTAL;
			transition.flags = SG_RUNE_PHASE_TRANSITION_CROSS_CELL;
			transition.duration_ms = (sg_rune_interval_t){ 0.0f, 0.0f };
			evidence.origin = SG_PHASE_CATALOG_TRANSITION_PORTAL;
			evidence.source_record = portal_index;
			evidence.destination_record = reverse ? portal->from_cell :
				portal->to_cell;
			evidence.source_cell = source_cell;
			evidence.destination_cell = destination_cell;
			evidence.source_region_id = source->semantics->regions[
				expected->phase_region_by_phase[source_phase]].id;
			evidence.destination_region_id = source->semantics->regions[
				expected->phase_region_by_phase[destination_phase]].id;
			evidence.portal = portal->id;
			evidence.source_state_mask = source_states;
			evidence.destination_state_mask = destination_states;
			evidence.portal_duration_ms = 0U;
			if (!AppendPair(expected, &transition, &evidence, error_out))
				return 0;
		}
	}
	return 1;
}

static int AirbornePhaseForSupport(const sg_rune_phase_basis_t *airborne)
{
	return airborne &&
		airborne->motion == SG_RUNE_MOTION_AIRBORNE &&
		airborne->support == SG_RUNE_SUPPORT_NONE &&
		airborne->reference_frame == SG_RUNE_FRAME_WORLD &&
		!SG_RuneModelStableIdValid(&airborne->mover.value);
}

static int AppendSupportTransition(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected, uint32_t cell,
	uint32_t airborne_phase, uint32_t supported_phase,
	sg_phase_catalog_error_t *error_out)
{
	sg_rune_phase_transition_t transition;
	sg_phase_catalog_transition_evidence_t evidence;
	uint32_t frame = source->authority->identity.physics.frame_ms;
	uint32_t quantum = source->authority->identity.physics.substep_ms;
	uint32_t airborne_region = expected->phase_region_by_phase[airborne_phase];
	uint32_t supported_region = expected->phase_region_by_phase[supported_phase];

	if (airborne_phase == supported_phase ||
		airborne_region == SG_PHASE_CATALOG_INDEX_NONE ||
		supported_region == SG_PHASE_CATALOG_INDEX_NONE)
		return 1;
	memset(&transition, 0, sizeof(transition));
	memset(&evidence, 0, sizeof(evidence));
	transition.cell = source->configuration->cells[cell].id;
	transition.destination_cell = source->configuration->cells[cell].id;
	transition.source_phase = expected->phases[airborne_phase].id;
	transition.destination_phase = expected->phases[supported_phase].id;
	transition.kind = SG_RUNE_PHASE_TRANSITION_SUPPORT;
	transition.duration_ms = (sg_rune_interval_t){ (float)quantum,
		(float)frame };
	evidence.origin = SG_PHASE_CATALOG_TRANSITION_SUPPORT_CHANGE;
	evidence.source_record = airborne_region;
	evidence.destination_record = supported_region;
	evidence.source_cell = cell;
	evidence.destination_cell = cell;
	evidence.source_region_id = source->semantics->regions[airborne_region].id;
	evidence.destination_region_id = source->semantics->regions[
		supported_region].id;
	return AppendPair(expected, &transition, &evidence, error_out);
}

static uint32_t StateBit(sg_mechanism_state_t state)
{
	if (state < SG_MECHANISM_STATE_INACTIVE ||
		state >= SG_MECHANISM_STATE_COUNT)
		return 0U;
	return UINT32_C(1) << (uint32_t)state;
}

static uint32_t TimingSpan(const sg_mechanism_capability_fact_t *fact)
{
	uint64_t total = (uint64_t)fact->delay_ms + fact->dwell_ms +
		fact->travel_ms + fact->wait_ms + fact->reset_ms;

	if (total == 0U)
		return 1U;
	return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static void FillMechanismDestinationPhase(
	const sg_phase_catalog_source_t *source,
	const sg_mechanism_capability_fact_t *fact,
	sg_rune_phase_basis_t *phase_out)
{
	uint32_t elapsed;
	uint32_t frame = source->authority->identity.physics.frame_ms;

	FillPhase(source, &source->semantics->regions[fact->destination_region], 1,
		&fact->mechanism_id, 1U, phase_out);
	elapsed = TimingSpan(fact);
	if (elapsed > frame)
		elapsed = frame;
	if (elapsed == 0U)
		elapsed = source->authority->identity.physics.substep_ms;
	phase_out->order.variant = 3U;
	phase_out->elapsed_ms.min_value = (float)elapsed;
	phase_out->elapsed_ms.max_value = (float)frame;
}

static int AppendMechanismTransition(const sg_phase_catalog_source_t *source,
	const sg_phase_mover_support_provider_payload_t *provider,
	sg_phase_catalog_expected_t *expected, uint32_t fact_index,
	sg_phase_catalog_error_t *error_out)
{
	const sg_mechanism_capability_fact_t *fact =
		&provider->facts[fact_index];
	const sg_configuration_semantic_region_t *source_region =
		&source->semantics->regions[fact->source_region];
	const sg_configuration_semantic_region_t *destination_region =
		&source->semantics->regions[fact->destination_region];
	sg_rune_mechanism_ref_t mechanism = fact->mechanism_id;
	sg_rune_phase_basis_t source_candidate;
	sg_rune_phase_basis_t destination_candidate;
	uint32_t source_phase;
	uint32_t destination_phase;
	uint32_t source_cell = source_region->cell;
	uint32_t destination_cell = destination_region->cell;
	sg_rune_phase_transition_t transition;
	sg_phase_catalog_transition_evidence_t evidence;

	FillPhase(source, source_region, 1, &mechanism, 1U, &source_candidate);
	FillPhase(source, destination_region, 1, &mechanism, 1U,
		&destination_candidate);
	if (!PhaseFindForRegion(expected, source_cell, fact->source_region,
		&source_candidate, &source_phase) ||
		!PhaseFindForRegion(expected, destination_cell, fact->destination_region,
			&destination_candidate,
			&destination_phase))
		return 0;
	FillMechanismDestinationPhase(source, fact, &destination_candidate);
	if (!PhaseFindForRegion(expected, destination_cell,
		fact->destination_region, &destination_candidate, &destination_phase))
		return 0;
	if (!AppendBinding(expected, destination_region, destination_cell,
		destination_phase, (sg_phase_mechanism_state_mask_t)
			StateBit(fact->destination_state), error_out))
		return 0;
	if (source_phase == destination_phase)
		return 0;
	memset(&transition, 0, sizeof(transition));
	memset(&evidence, 0, sizeof(evidence));
	transition.cell = source->configuration->cells[source_cell].id;
	transition.destination_cell =
		source->configuration->cells[destination_cell].id;
	transition.source_phase = expected->phases[source_phase].id;
	transition.destination_phase = expected->phases[destination_phase].id;
	transition.flags = source_cell == destination_cell ? 0U :
		SG_RUNE_PHASE_TRANSITION_CROSS_CELL;
	transition.kind = fact->kind == SG_MECHANISM_CAPABILITY_DWELL ?
		SG_RUNE_PHASE_TRANSITION_MOVER_DWELL : SG_RUNE_PHASE_TRANSITION_TIME;
	transition.duration_ms = (sg_rune_interval_t){ (float)TimingSpan(fact),
		(float)TimingSpan(fact) };
	evidence.origin = SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING;
	evidence.source_record = fact_index;
	evidence.destination_record = SG_PHASE_CATALOG_INDEX_NONE;
	evidence.source_cell = source_cell;
	evidence.destination_cell = destination_cell;
	evidence.source_region_id = source_region->id;
	evidence.destination_region_id = destination_region->id;
	evidence.mechanism = mechanism;
	evidence.source_state_mask = (sg_phase_mechanism_state_mask_t)
		StateBit(fact->source_state);
	evidence.destination_state_mask = (sg_phase_mechanism_state_mask_t)
		StateBit(fact->destination_state);
	evidence.provider_verifier_identity =
		provider->verifier_identity;
	evidence.delay_ms = fact->delay_ms;
	evidence.dwell_ms = fact->dwell_ms;
	evidence.travel_ms = fact->travel_ms;
	evidence.wait_ms = fact->wait_ms;
	evidence.reset_ms = fact->reset_ms;
	evidence.activation_time_ms = fact->activation_time_ms;
	evidence.active_time_ms = fact->active_time_ms;
	evidence.exit_time_ms = fact->exit_time_ms;
	evidence.reset_time_ms = fact->reset_time_ms;
	return AppendPair(expected, &transition, &evidence, error_out);
}

static int FloatCompare(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	if (left == right)
		return 0;
	if (!isnan(left) && !isnan(right))
		return left < right ? -1 : 1;
	if (isnan(left) != isnan(right))
		return isnan(left) ? 1 : -1;
	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits ? 0 : (left_bits < right_bits ? -1 : 1);
}

static int TransitionPairCompare(const void *left_value,
	const void *right_value)
{
	const sg_phase_catalog_transition_pair_t *left = left_value;
	const sg_phase_catalog_transition_pair_t *right = right_value;
	const sg_rune_phase_transition_t *lt = &left->transition;
	const sg_rune_phase_transition_t *rt = &right->transition;
	int comparison;

	comparison = StableIdCompare(&lt->cell.value, &rt->cell.value);
	if (comparison != 0)
		return comparison;
	comparison = StableIdCompare(&lt->source_phase.value,
		&rt->source_phase.value);
	if (comparison != 0)
		return comparison;
	comparison = StableIdCompare(&lt->destination_phase.value,
		&rt->destination_phase.value);
	if (comparison != 0)
		return comparison;
	if (lt->kind != rt->kind)
		return lt->kind < rt->kind ? -1 : 1;
	comparison = FloatCompare(lt->duration_ms.min_value,
		rt->duration_ms.min_value);
	if (comparison != 0)
		return comparison;
	comparison = FloatCompare(lt->duration_ms.max_value,
		rt->duration_ms.max_value);
	if (comparison != 0)
		return comparison;
	if (lt->flags != rt->flags)
		return lt->flags < rt->flags ? -1 : 1;
	return StableIdCompare(&lt->destination_cell.value,
		&rt->destination_cell.value);
}

static int PhaseOrderCompare(const void *left_value, const void *right_value)
{
	const sg_rune_phase_basis_t *left = left_value;
	const sg_rune_phase_basis_t *right = right_value;

	return SG_RuneModelOrderKeyCompare(&left->order, &right->order);
}

static int FinalizeTransitions(sg_phase_catalog_expected_t *expected,
	const sg_rune_model_identity_t *identity,
	sg_phase_catalog_error_t *error_out)
{
	uint32_t index;

	if (expected->transition_pair_count != 0U)
	{
		uint32_t output = 1U;

		qsort(expected->transition_pairs, expected->transition_pair_count,
			sizeof(*expected->transition_pairs), TransitionPairCompare);
		for (index = 1U; index < expected->transition_pair_count; index++)
			if (TransitionPairCompare(
				&expected->transition_pairs[index - 1U],
				&expected->transition_pairs[index]) != 0)
				expected->transition_pairs[output++] =
					expected->transition_pairs[index];
		expected->transition_pair_count = output;
	}
	expected->transition_count = expected->transition_pair_count;
	if (expected->transition_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			expected->transition_count);
		return 0;
	}
	expected->transition_completion = expected->transition_count == 0U ?
		SG_PHASE_CATALOG_PROVEN_EMPTY : SG_PHASE_CATALOG_COMPLETE;
	if (expected->transition_count != 0U)
	{
		if (!AllocationFits((size_t)expected->transition_count,
				sizeof(*expected->transitions)) ||
			!AllocationFits((size_t)expected->transition_count,
				sizeof(*expected->transition_evidence)))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
				expected->transition_count);
			return 0;
		}
		expected->transitions = malloc((size_t)expected->transition_count *
			sizeof(*expected->transitions));
		expected->transition_evidence = malloc(
			(size_t)expected->transition_count *
			sizeof(*expected->transition_evidence));
		if (!expected->transitions || !expected->transition_evidence)
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY,
				expected->transition_count);
			return 0;
		}
	}
	for (index = 0U; index < expected->transition_count; index++)
	{
		sg_rune_order_key_t order;

		memset(&order, 0, sizeof(order));
		order.source_set_identity = identity->source_set_identity;
		order.domain = SG_RUNE_ORDER_PHASE_TRANSITION;
		order.source_index = index;
		order.variant = (uint32_t)expected->transition_pairs[index].evidence.origin;
		expected->transition_pairs[index].transition.order = order;
		expected->transition_pairs[index].transition.id.value =
			SG_RuneModelStableIdFromOrderKey(&order);
		expected->transitions[index] =
			expected->transition_pairs[index].transition;
		expected->transition_evidence[index] =
			expected->transition_pairs[index].evidence;
	}
	return 1;
}

static int BuildExpectedValidated(const sg_phase_catalog_source_t *source,
	const sg_phase_mover_support_provider_payload_t *provider,
	sg_phase_catalog_expected_t *expected, sg_phase_catalog_error_t *error_out)
{
	uint32_t *cell_ordinals = NULL;
	uint32_t region;
	uint32_t cell;
	uint32_t support_cursor = 0U;

	expected->completion = source->configuration->cell_count == 0U ?
		SG_PHASE_CATALOG_PROVEN_EMPTY : SG_PHASE_CATALOG_COMPLETE;
	expected->mover_support_verifier_identity =
		provider->verifier_identity;
	if (source->configuration->cell_count == 0U)
	{
		expected->transition_completion = SG_PHASE_CATALOG_PROVEN_EMPTY;
		return 1;
	}
	if (!AllocationFits((size_t)source->configuration->cell_count,
		sizeof(*cell_ordinals)))
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OVERFLOW,
			source->configuration->cell_count);
		return 0;
	}
	cell_ordinals = calloc((size_t)source->configuration->cell_count,
		sizeof(*cell_ordinals));
	if (!cell_ordinals)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		goto failure;
	}
	for (region = 0U; region < source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&source->semantics->regions[region];
		sg_rune_phase_basis_t candidate;
		uint32_t phase;

		FillPhase(source, record, 0, NULL, 0U, &candidate);
		if (!AppendPhase(source, expected, &candidate, region, record->cell,
			cell_ordinals, &phase, error_out) ||
			!AppendBinding(expected, record, record->cell, phase, 0U,
				error_out))
			goto failure;
		while (support_cursor < provider->support_count &&
			provider->supports[support_cursor].
				semantic_region_id < record->id)
			support_cursor++;
		while (support_cursor < provider->support_count &&
			provider->supports[support_cursor].
				semantic_region_id == record->id)
		{
			const sg_phase_mover_support_t *support =
				&provider->supports[support_cursor];
			sg_rune_mechanism_ref_t mechanism = support->mechanism;

			FillPhase(source, record, 1, &mechanism, 1U, &candidate);
			if (!AppendPhase(source, expected, &candidate, region, record->cell,
				cell_ordinals, &phase, error_out) ||
				!AppendBinding(expected, record, record->cell, phase,
					support->mechanism_state_mask, error_out))
				goto failure;
			support_cursor++;
		}
		/* Keep every phase for one configuration cell contiguous while the
		 * geometric relation builders use cell ranges. */
		for (cell = 0U; cell < provider->fact_count; cell++)
			if (provider->facts[cell].destination_region == region)
			{
				FillMechanismDestinationPhase(source, &provider->facts[cell],
					&candidate);
				if (!AppendPhase(source, expected, &candidate, region,
					record->cell, cell_ordinals, &phase, error_out))
					goto failure;
			}
	}
	if (support_cursor != provider->support_count)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
			support_cursor);
		goto failure;
	}
	/* Mechanism facts can add elapsed mover-relative phases.  Materialize those
	 * phases before deriving geometric relations so portal enumeration covers
	 * the complete accepted phase set. */
	for (region = 0U; region < provider->fact_count; region++)
		if (!AppendMechanismTransition(source, provider, expected, region,
			error_out))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
			goto failure;
		}
	for (cell = 0U; cell < source->configuration->cell_count; cell++)
	{
		uint32_t first;
		uint32_t last;
		uint32_t airborne_phase;

		if (!PhaseCellRange(expected, cell, &first, &last))
			continue;
		for (airborne_phase = first; airborne_phase < last; airborne_phase++)
		{
			sg_rune_phase_basis_t candidate;
			uint32_t supported_phase;

			if (!AirbornePhaseForSupport(&expected->phases[airborne_phase]))
				continue;
			candidate = expected->phases[airborne_phase];
			candidate.motion = SG_RUNE_MOTION_SUPPORTED;
			candidate.support = SG_RUNE_SUPPORT_SUPPORTED;
			for (supported_phase = first; supported_phase < last;
				supported_phase++)
				if (PhaseEquivalent(&expected->phases[supported_phase],
						&candidate) &&
					!AppendSupportTransition(source, expected, cell,
						airborne_phase, supported_phase, error_out))
					goto failure;
		}
	}
	for (region = 0U; region < source->configuration->stance_overlap_count;
		region++)
		if (!AppendStanceTransition(source, expected, region, error_out))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
			goto failure;
		}
	for (region = 0U; region < source->configuration->portal_count; region++)
	{
		if (!AppendPortalTransition(source, expected, region, 0, error_out))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
			goto failure;
		}
		if (!AppendPortalTransition(source, expected, region, 1, error_out))
		{
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
			goto failure;
		}
	}
	if (!FinalizeTransitions(expected, &source->authority->identity, error_out))
		goto failure;
	qsort(expected->phases, expected->phase_count, sizeof(*expected->phases),
		PhaseOrderCompare);
	free(cell_ordinals);
	return 1;

failure:
	free(cell_ordinals);
	SG_PhaseCatalogExpectedDestroy(expected);
	return 0;
}

int SG_PhaseCatalogBuildExpected(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected, sg_phase_catalog_error_t *error_out)
{
	const sg_phase_mover_support_provider_payload_t *provider;

	if (!expected)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	memset(expected, 0, sizeof(*expected));
	if (!SG_PhaseCatalogSourceValidate(source, error_out))
		return 0;
	provider = SG_PHASE_SOURCE_PROVIDER(source);
	return BuildExpectedValidated(source, provider, expected, error_out);
}

int SG_PhaseCatalogDeriveExpectedNonAuthoritative(
	const sg_phase_catalog_non_authoritative_source_t *source,
	sg_phase_catalog_expected_t *expected, sg_phase_catalog_error_t *error_out)
{
	sg_phase_mover_support_provider_payload_t provider;
	sg_phase_catalog_source_t derivation_source;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!expected)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	memset(expected, 0, sizeof(*expected));
	if (!source || !source->authority || !source->configuration ||
		!source->semantics || source->verifier_identity == 0U ||
		source->verifier_identity == UINT64_MAX)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	memset(&provider, 0, sizeof(provider));
	provider.identity = source->authority->identity;
	provider.completion = source->completion;
	provider.supports = source->supports;
	provider.support_count = source->support_count;
	provider.facts = source->facts;
	provider.fact_count = source->fact_count;
	provider.verifier_identity = source->verifier_identity;
	memset(&derivation_source, 0, sizeof(derivation_source));
	derivation_source.authority = source->authority;
	derivation_source.configuration = source->configuration;
	derivation_source.semantics = source->semantics;
	if (!SourceValidateWithProvider(&derivation_source, &provider, error_out))
		return 0;
	return BuildExpectedValidated(&derivation_source, &provider, expected,
		error_out);
}

void SG_PhaseCatalogExpectedDestroy(sg_phase_catalog_expected_t *expected)
{
	if (!expected)
		return;
	free(expected->phases);
	free(expected->bindings);
	free(expected->transitions);
	free(expected->transition_evidence);
	free(expected->transition_pairs);
	free(expected->transition_pair_hash);
	free(expected->phase_hash);
	free(expected->phase_neutral_hash);
	free(expected->phase_region_by_phase);
	memset(expected, 0, sizeof(*expected));
}

int SG_PhaseCatalogHeaderValid(const sg_phase_catalog_t *catalog)
{
	return catalog && catalog->magic == SG_PHASE_CATALOG_MAGIC &&
		catalog->magic_inverse == ~SG_PHASE_CATALOG_MAGIC &&
		catalog->self == catalog;
}

static int CatalogStorageShapeValid(const sg_phase_catalog_t *catalog)
{
	return catalog && catalog->phase_capacity <= SG_RUNE_MODEL_MAX_PHASES &&
		catalog->binding_capacity <= SG_PHASE_CATALOG_MAX_BINDINGS &&
		catalog->transition_capacity <= SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS &&
		AllocationFits((size_t)catalog->phase_capacity,
			sizeof(*catalog->phases)) &&
		AllocationFits((size_t)catalog->binding_capacity,
			sizeof(*catalog->bindings)) &&
		AllocationFits((size_t)catalog->transition_capacity,
			sizeof(*catalog->transitions)) &&
		AllocationFits((size_t)catalog->transition_capacity,
			sizeof(*catalog->transition_evidence)) &&
		catalog->phase_count <= catalog->phase_capacity &&
		catalog->binding_count <= catalog->binding_capacity &&
		catalog->transition_count <= catalog->transition_capacity &&
		(catalog->phase_count == 0U || catalog->phases) &&
		(catalog->binding_count == 0U || catalog->bindings) &&
		(catalog->transition_count == 0U ||
			(catalog->transitions && catalog->transition_evidence));
}

/* Linear structural acceptance.  These checks read the published catalog and
 * its binding source identity only.  They never re-derive the expected model,
 * so a defect is reported against a real record instead of against a second
 * implementation that has to be kept in step with this one. */

typedef struct sg_catalog_id_ref_s
{
	sg_rune_stable_id_t id;
	uint32_t index;
} sg_catalog_id_ref_t;

typedef struct sg_catalog_binding_ref_s
{
	uint64_t region;
	uint32_t cell;
	sg_rune_stable_id_t phase;
	uint32_t index;
} sg_catalog_binding_ref_t;

static void SetCatalogReport(sg_phase_catalog_check_result_t *report_out,
	sg_phase_catalog_check_code_t code, uint32_t record)
{
	if (report_out)
	{
		report_out->code = code;
		report_out->record = record;
	}
}

static int CatalogIdRefCompare(const void *left_value, const void *right_value)
{
	const sg_catalog_id_ref_t *left = left_value;
	const sg_catalog_id_ref_t *right = right_value;
	int comparison = StableIdCompare(&left->id, &right->id);

	if (comparison != 0)
		return comparison;
	return left->index == right->index ? 0 :
		(left->index < right->index ? -1 : 1);
}

static int CatalogBindingRefCompare(const void *left_value,
	const void *right_value)
{
	const sg_catalog_binding_ref_t *left = left_value;
	const sg_catalog_binding_ref_t *right = right_value;
	int comparison;

	if (left->region != right->region)
		return left->region < right->region ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	comparison = StableIdCompare(&left->phase, &right->phase);
	if (comparison != 0)
		return comparison;
	return left->index == right->index ? 0 :
		(left->index < right->index ? -1 : 1);
}

/* Returns 1 when a duplicate exists, -1 on allocation failure, 0 otherwise.
 * The caller fills refs; this owns only the sort and the adjacent compare. */
static int CatalogDuplicateRefs(sg_catalog_id_ref_t *refs, uint32_t count,
	sg_phase_catalog_check_code_t code,
	sg_phase_catalog_check_result_t *report_out)
{
	uint32_t index;

	qsort(refs, count, sizeof(*refs), CatalogIdRefCompare);
	for (index = 1U; index < count; index++)
		if (StableIdCompare(&refs[index - 1U].id, &refs[index].id) == 0)
		{
			SetCatalogReport(report_out, code, refs[index].index);
			return 1;
		}
	return 0;
}

static sg_catalog_id_ref_t *CatalogAllocRefs(uint32_t count,
	sg_phase_catalog_check_result_t *report_out)
{
	sg_catalog_id_ref_t *refs;

	if (!AllocationFits((size_t)count, sizeof(*refs)))
	{
		SetCatalogReport(report_out,
			SG_PHASE_CATALOG_CHECK_STORAGE_INVALID, 0U);
		return NULL;
	}
	refs = malloc((size_t)count * sizeof(*refs));
	if (!refs)
		SetCatalogReport(report_out,
			SG_PHASE_CATALOG_CHECK_STORAGE_INVALID, 0U);
	return refs;
}

static int CatalogDuplicatePhases(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_check_result_t *report_out)
{
	sg_catalog_id_ref_t *refs;
	uint32_t index;
	int duplicate;

	if (catalog->phase_count < 2U)
		return 0;
	refs = CatalogAllocRefs(catalog->phase_count, report_out);
	if (!refs)
		return -1;
	for (index = 0U; index < catalog->phase_count; index++)
	{
		refs[index].id = catalog->phases[index].id.value;
		refs[index].index = index;
	}
	duplicate = CatalogDuplicateRefs(refs, catalog->phase_count,
		SG_PHASE_CATALOG_CHECK_DUPLICATE_PHASE, report_out);
	free(refs);
	return duplicate;
}

static int CatalogDuplicateTransitions(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_check_result_t *report_out)
{
	sg_catalog_id_ref_t *refs;
	uint32_t index;
	int duplicate;

	if (catalog->transition_count < 2U)
		return 0;
	refs = CatalogAllocRefs(catalog->transition_count, report_out);
	if (!refs)
		return -1;
	for (index = 0U; index < catalog->transition_count; index++)
	{
		refs[index].id = catalog->transitions[index].id.value;
		refs[index].index = index;
	}
	duplicate = CatalogDuplicateRefs(refs, catalog->transition_count,
		SG_PHASE_CATALOG_CHECK_DUPLICATE_TRANSITION, report_out);
	free(refs);
	return duplicate;
}

static int CatalogDuplicateBindings(const sg_phase_catalog_t *catalog,
	sg_phase_catalog_check_result_t *report_out)
{
	sg_catalog_binding_ref_t *refs;
	uint32_t index;
	int duplicate = 0;

	if (catalog->binding_count < 2U)
		return 0;
	if (!AllocationFits((size_t)catalog->binding_count, sizeof(*refs)))
	{
		SetCatalogReport(report_out,
			SG_PHASE_CATALOG_CHECK_STORAGE_INVALID, 0U);
		return -1;
	}
	refs = malloc((size_t)catalog->binding_count * sizeof(*refs));
	if (!refs)
	{
		SetCatalogReport(report_out,
			SG_PHASE_CATALOG_CHECK_STORAGE_INVALID, 0U);
		return -1;
	}
	for (index = 0U; index < catalog->binding_count; index++)
	{
		refs[index].region = catalog->bindings[index].semantic_region_id;
		refs[index].cell = catalog->bindings[index].configuration_cell;
		refs[index].phase = catalog->bindings[index].phase.value;
		refs[index].index = index;
	}
	qsort(refs, catalog->binding_count, sizeof(*refs),
		CatalogBindingRefCompare);
	for (index = 1U; index < catalog->binding_count; index++)
		if (refs[index - 1U].region == refs[index].region &&
			refs[index - 1U].cell == refs[index].cell &&
			StableIdCompare(&refs[index - 1U].phase, &refs[index].phase) == 0)
		{
			SetCatalogReport(report_out,
				SG_PHASE_CATALOG_CHECK_DUPLICATE_BINDING, refs[index].index);
			duplicate = 1;
			break;
		}
	free(refs);
	return duplicate;
}

int SG_PhaseCatalogValidate(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_check_result_t *result_out)
{
	uint32_t index;

	SetCatalogReport(result_out, SG_PHASE_CATALOG_CHECK_OK_COMPLETE, 0U);
	if (!source || !source->authority || !catalog)
	{
		SetCatalogReport(result_out,
			SG_PHASE_CATALOG_CHECK_INVALID_ARGUMENT, 0U);
		return 0;
	}
	if (!SG_PhaseCatalogHeaderValid(catalog) ||
		!CatalogStorageShapeValid(catalog))
	{
		SetCatalogReport(result_out,
			SG_PHASE_CATALOG_CHECK_STORAGE_INVALID, 0U);
		return 0;
	}
	if (!SG_PhaseCatalogIdentityEqual(&catalog->identity,
		&source->authority->identity))
	{
		SetCatalogReport(result_out,
			SG_PHASE_CATALOG_CHECK_SOURCE_MISMATCH, 0U);
		return 0;
	}
	/* Deterministic order is an artifact property: equal inputs must produce
	 * the same byte order, so phases carry strictly increasing order keys. */
	for (index = 1U; index < catalog->phase_count; index++)
		if (SG_RuneModelOrderKeyCompare(&catalog->phases[index - 1U].order,
			&catalog->phases[index].order) >= 0)
		{
			SetCatalogReport(result_out,
				SG_PHASE_CATALOG_CHECK_NONDETERMINISTIC_ORDER, index);
			return 0;
		}
	if (CatalogDuplicatePhases(catalog, result_out) != 0 ||
		CatalogDuplicateBindings(catalog, result_out) != 0 ||
		CatalogDuplicateTransitions(catalog, result_out) != 0)
		return 0;
	return 1;
}

int SG_PhaseCatalogBuild(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_t **catalog_out, sg_phase_catalog_error_t *error_out)
{
	sg_phase_catalog_expected_t expected;
	sg_phase_catalog_t *catalog;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!catalog_out || *catalog_out)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	*catalog_out = NULL;
	memset(&expected, 0, sizeof(expected));
	if (!SG_PhaseCatalogBuildExpected(source, &expected, error_out))
		return 0;
	catalog = calloc(1U, sizeof(*catalog));
	if (!catalog)
	{
		SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	catalog->magic = SG_PHASE_CATALOG_MAGIC;
	catalog->magic_inverse = ~SG_PHASE_CATALOG_MAGIC;
	catalog->self = catalog;
	catalog->identity = source->authority->identity;
	catalog->completion = expected.completion;
	catalog->transition_completion = expected.transition_completion;
	catalog->mover_support_verifier_identity =
		expected.mover_support_verifier_identity;
	catalog->phases = expected.phases;
	catalog->phase_count = expected.phase_count;
	catalog->phase_capacity = expected.phase_count;
	catalog->bindings = expected.bindings;
	catalog->binding_count = expected.binding_count;
	catalog->binding_capacity = expected.binding_count;
	catalog->transitions = expected.transitions;
	catalog->transition_evidence = expected.transition_evidence;
	catalog->transition_count = expected.transition_count;
	catalog->transition_capacity = expected.transition_count;
	/* The catalog owns the published arrays.  Construction-only pair and
	 * lookup storage must be released before the expected value is reset. */
	free(expected.transition_pairs);
	free(expected.transition_pair_hash);
	free(expected.phase_hash);
	free(expected.phase_neutral_hash);
	free(expected.phase_region_by_phase);
	memset(&expected, 0, sizeof(expected));
	{
		sg_phase_catalog_check_result_t acceptance = { 0 };

		if (!SG_PhaseCatalogValidate(source, catalog, &acceptance))
		{
			/* Report the failing record, not a bare rejection. */
			SetErrorOnce(error_out, SG_PHASE_CATALOG_ERROR_CHECK_REJECTED,
				acceptance.record);
			SG_PhaseCatalogDestroy(catalog);
			return 0;
		}
	}
	*catalog_out = catalog;
	return 1;
}

void SG_PhaseCatalogDestroy(sg_phase_catalog_t *catalog)
{
	if (!catalog)
		return;
	free(catalog->phases);
	free(catalog->bindings);
	free(catalog->transitions);
	free(catalog->transition_evidence);
	memset(catalog, 0, sizeof(*catalog));
	free(catalog);
}

int SG_PhaseCatalogBindingsForRegion(const sg_phase_catalog_t *catalog,
	uint64_t semantic_region_id, const sg_phase_catalog_binding_t **bindings_out,
	uint32_t *binding_count_out)
{
	uint32_t first = 0U;
	uint32_t last;
	uint32_t middle;

	if (bindings_out)
		*bindings_out = NULL;
	if (binding_count_out)
		*binding_count_out = 0U;
	if (!SG_PhaseCatalogHeaderValid(catalog) ||
		!CatalogStorageShapeValid(catalog) || !bindings_out ||
		!binding_count_out)
		return 0;
	last = catalog->binding_count;
	while (first < last)
	{
		middle = first + (last - first) / 2U;
		if (catalog->bindings[middle].semantic_region_id < semantic_region_id)
			first = middle + 1U;
		else
			last = middle;
	}
	if (first == catalog->binding_count ||
		catalog->bindings[first].semantic_region_id != semantic_region_id)
		return 0;
	last = first;
	while (last < catalog->binding_count &&
		catalog->bindings[last].semantic_region_id == semantic_region_id)
		last++;
	*bindings_out = &catalog->bindings[first];
	*binding_count_out = last - first;
	return 1;
}

const char *SG_PhaseCatalogErrorString(sg_phase_catalog_error_code_t code)
{
	switch (code)
	{
	case SG_PHASE_CATALOG_ERROR_NONE: return "none";
	case SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_PHASE_CATALOG_ERROR_IDENTITY_MISMATCH: return "identity mismatch";
	case SG_PHASE_CATALOG_ERROR_INVALID_SOURCE: return "invalid source";
	case SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE: return "incomplete source";
	case SG_PHASE_CATALOG_ERROR_INVALID_PHASE: return "invalid phase";
	case SG_PHASE_CATALOG_ERROR_INVALID_BINDING: return "invalid binding";
	case SG_PHASE_CATALOG_ERROR_DUPLICATE_SOURCE: return "duplicate source";
	case SG_PHASE_CATALOG_ERROR_OVERFLOW: return "representation overflow";
	case SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY: return "out of memory";
	case SG_PHASE_CATALOG_ERROR_CHECK_REJECTED: return "check rejected";
	case SG_PHASE_CATALOG_ERROR_ERROR_COUNT: break;
	}
	return "unknown phase catalog error";
}
