#include "sg_phase_catalog_internal.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SG_PHASE_CATALOG_MAGIC UINT64_C(0x50434154414c4f47)

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

typedef struct sg_phase_support_ref_s
{
	uint32_t source_index;
	uint64_t region_id;
	sg_rune_stable_id_t mechanism;
} sg_phase_support_ref_t;

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
		truncf(physics->gravity) == physics->gravity &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
		physics->substep_ms <= UCHAR_MAX &&
		physics->substep_ms <= physics->frame_ms &&
		physics->frame_ms % physics->substep_ms == 0U;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	return identity && identity->bsp_content_id != 0U &&
		identity->entity_semantics_id != 0U &&
		identity->physics_abi_id != 0U && identity->source_set_identity != 0U &&
		identity->source_set_identity != UINT64_MAX && identity->schema_id != 0U &&
		identity->producer_identity != 0U && HullValid(&identity->standing_hull) &&
		HullValid(&identity->crouching_hull) && PhysicsValid(&identity->physics);
}

int SG_PhaseCatalogIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		memcmp(&left->standing_hull, &right->standing_hull,
			sizeof(left->standing_hull)) == 0 &&
		memcmp(&left->crouching_hull, &right->crouching_hull,
			sizeof(left->crouching_hull)) == 0 &&
		memcmp(&left->physics, &right->physics, sizeof(left->physics)) == 0;
}

void SG_PhaseCatalogSetError(sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_error_code_t code, uint32_t source_index)
{
	if (error_out && error_out->code == SG_PHASE_CATALOG_ERROR_NONE)
	{
		error_out->code = code;
		error_out->source_index = source_index;
	}
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

static int SupportRefCompare(const void *left_value, const void *right_value)
{
	const sg_phase_support_ref_t *left = left_value;
	const sg_phase_support_ref_t *right = right_value;
	int comparison;

	if (left->region_id != right->region_id)
		return left->region_id < right->region_id ? -1 : 1;
	comparison = StableIdCompare(&left->mechanism, &right->mechanism);
	if (comparison != 0)
		return comparison;
	if (left->source_index != right->source_index)
		return left->source_index < right->source_index ? -1 : 1;
	return 0;
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
	const sg_configuration_semantic_region_t *region;
	const sg_configuration_space_t *configuration = source->configuration;
	uint32_t flags;
	uint32_t water_flags;
	sg_rune_medium_t medium;

	region = &source->semantics->regions[region_index];
	flags = region->flags;
	water_flags = region->water_type & SG_HOST_MASK_WATER;
	if (region->id == 0U || region->cell >= configuration->cell_count ||
		!Finite3(region->interior_witness.value) ||
		!Finite3(region->bounds.mins.value) ||
		!Finite3(region->bounds.maxs.value) ||
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
		((flags & (SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)) == 0U) ||
		((flags & (SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)) ==
			(SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)) ||
		region->water_level > 3U || !RegionMedium(region, &medium))
		return 0;
	if ((water_flags & (water_flags - 1U)) != 0U)
		return 0;
	if (region->water_level == 0U)
	{
		if (medium != SG_RUNE_MEDIUM_DRY ||
			(region->water_type & SG_HOST_MASK_WATER) != 0U)
			return 0;
	}
	else
	{
		if (medium == SG_RUNE_MEDIUM_DRY ||
			(region->water_type & SG_HOST_MASK_WATER) == 0U)
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
	if ((medium == SG_RUNE_MEDIUM_LAVA || medium == SG_RUNE_MEDIUM_SLIME) !=
		((flags & SG_CONFIGURATION_SEMANTIC_REGION_HAZARD) != 0U))
		return 0;
	return 1;
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

static int SupportRefsValid(const sg_phase_catalog_source_t *source,
	sg_phase_support_ref_t *refs)
{
	uint32_t index;
	uint32_t region_index;

	for (index = 0U; index < source->mover_support_count; index++)
	{
		const sg_phase_mover_support_t *support =
			&source->mover_supports[index];
		sg_rune_order_key_t order;

		if (support->semantic_region_id == 0U ||
			!FindRegion(source, support->semantic_region_id, &region_index) ||
			support->mechanism.value.source_set_identity !=
				source->authority->identity.source_set_identity ||
			!SG_RuneModelStableIdValid(&support->mechanism.value) ||
			!SG_RuneModelStableIdToOrderKey(&support->mechanism.value, &order) ||
			order.domain != SG_RUNE_ORDER_MECHANISM ||
			order.source_index >= SG_RUNE_MODEL_MAX_MECHANISMS ||
			support->mechanism_state_mask == 0U ||
			(support->mechanism_state_mask &
				~(sg_phase_mechanism_state_mask_t)
				SG_PHASE_MECHANISM_STATE_KNOWN) != 0U)
		{
			(void)region_index;
			return 0;
		}
		refs[index].source_index = index;
		refs[index].region_id = support->semantic_region_id;
		refs[index].mechanism = support->mechanism.value;
	}
	qsort(refs, source->mover_support_count, sizeof(*refs),
		SupportRefCompare);
	for (index = 1U; index < source->mover_support_count; index++)
		if (refs[index - 1U].region_id == refs[index].region_id &&
			StableIdEqual(&refs[index - 1U].mechanism, &refs[index].mechanism))
			return 0;
	return 1;
}

int SG_PhaseCatalogSourceValidate(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_error_t *error_out)
{
	const sg_rune_model_identity_t *identity;
	uint32_t region;
	uint32_t cell;
	uint32_t support_capacity;
	sg_phase_support_ref_t *refs = NULL;
	int valid = 0;

	if (!source || !source->authority || !source->configuration ||
		!source->semantics)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	identity = &source->authority->identity;
	if (!IdentityValid(identity))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	if (!SG_PhaseCatalogIdentityEqual(identity,
		&source->configuration->identity) ||
		!SG_PhaseCatalogIdentityEqual(identity, &source->semantics->identity))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_IDENTITY_MISMATCH, 0U);
		return 0;
	}
	if (source->configuration->cell_count > SG_RUNE_MODEL_MAX_CELLS ||
		source->semantics->region_count > SG_RUNE_MODEL_MAX_PHASES ||
		source->mover_support_count > SG_RUNE_MODEL_MAX_PHASES)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
		return 0;
	}
	if (source->mover_support_count != 0U && !source->mover_supports)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		return 0;
	}
	if (source->semantics->region_count != 0U &&
		(!source->semantics->regions ||
		 (source->semantics->face_count != 0U && !source->semantics->faces)))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		return 0;
	}
	if (source->configuration->cell_count != 0U &&
		!source->configuration->cells)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		return 0;
	}
	if (source->mover_support_verifier_identity == 0U ||
		source->mover_support_verifier_identity == UINT64_MAX)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	if (source->mover_support_completion < SG_PHASE_CATALOG_COMPLETE ||
		source->mover_support_completion >= SG_PHASE_CATALOG_COMPLETION_COUNT)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		return 0;
	}
	if (source->configuration->cell_count == 0U)
	{
		if (source->semantics->region_count != 0U ||
			source->semantics->face_count != 0U ||
			source->mover_support_count != 0U ||
			source->mover_support_completion != SG_PHASE_CATALOG_PROVEN_EMPTY)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
			return 0;
		}
		return 1;
	}
	if (source->semantics->region_count == 0U)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		return 0;
	}
	for (cell = 0U; cell < source->configuration->cell_count; cell++)
		if (source->configuration->cells[cell].stance >= SG_RUNE_STANCE_COUNT)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, cell);
			return 0;
		}
	for (region = 0U; region < source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&source->semantics->regions[region];

		if (!RegionFactsValid(source, region) ||
			(region != 0U &&
				((source->semantics->regions[region - 1U].cell > record->cell) ||
				 (source->semantics->regions[region - 1U].cell == record->cell &&
					source->semantics->regions[region - 1U].id >= record->id) ||
				 source->semantics->regions[region - 1U].id >= record->id)))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
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
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, cell);
			return 0;
		}
	}
	if (region != source->semantics->region_count)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, region);
		return 0;
	}
	{
		const sg_configuration_semantic_region_t *last =
			&source->semantics->regions[source->semantics->region_count - 1U];

		if (last->first_face > source->semantics->face_count ||
			last->face_count > source->semantics->face_count - last->first_face ||
			last->first_face + last->face_count != source->semantics->face_count)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
				source->semantics->region_count - 1U);
			return 0;
		}
	}
	if (source->mover_support_count > UINT32_MAX -
		source->semantics->region_count)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
		return 0;
	}
	support_capacity = source->mover_support_count;
	if (support_capacity != 0U)
	{
		if (!AllocationFits((size_t)support_capacity, sizeof(*refs)))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OVERFLOW, support_capacity);
			return 0;
		}
		refs = malloc((size_t)support_capacity * sizeof(*refs));
		if (!refs)
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, support_capacity);
			return 0;
		}
		if (!SupportRefsValid(source, refs))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
			goto done;
		}
	}
	if (source->mover_support_count == 0U &&
		source->mover_support_completion != SG_PHASE_CATALOG_PROVEN_EMPTY)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE, 0U);
		goto done;
	}
	if (source->mover_support_count != 0U &&
		source->mover_support_completion != SG_PHASE_CATALOG_COMPLETE)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		goto done;
	}
	valid = 1;

done:
	free(refs);
	return valid;
}

static int PhaseEquivalent(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left->stance == right->stance && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		StableIdEqual(&left->mover.value, &right->mover.value);
}

int SG_PhaseCatalogPhaseEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left && right && memcmp(left, right, sizeof(*left)) == 0;
}

int SG_PhaseCatalogBindingEqual(const sg_phase_catalog_binding_t *left,
	const sg_phase_catalog_binding_t *right)
{
	return left && right && memcmp(left, right, sizeof(*left)) == 0;
}

static void FillPhase(const sg_phase_catalog_source_t *source,
	const sg_configuration_semantic_region_t *region,
	int mover, const sg_rune_mechanism_ref_t *mechanism,
	uint32_t source_index, uint32_t local_ordinal,
	sg_rune_phase_basis_t *phase_out)
{
	sg_rune_medium_t medium = SG_RUNE_MEDIUM_DRY;
	float speed = source->authority->identity.physics.max_velocity;

	memset(phase_out, 0, sizeof(*phase_out));
	phase_out->order.source_set_identity =
		source->authority->identity.source_set_identity;
	phase_out->order.domain = SG_RUNE_ORDER_PHASE;
	phase_out->order.source_index = source_index;
	phase_out->order.local_ordinal = local_ordinal;
	phase_out->order.variant = mover ? 1U : 0U;
	phase_out->id.value = SG_RuneModelStableIdFromOrderKey(
		&phase_out->order);
	phase_out->stance = source->configuration->cells[region->cell].stance;
	(void)RegionMedium(region, &medium);
	/* A mover binding is itself the support relation.  The phase model
	 * represents that relation as supported motion even when the static
	 * semantic sample is airborne (for example, a platform crossing the
	 * sample while the player is in flight).  This keeps the phase valid
	 * under the model's support/motion invariants. */
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

static int PhaseFind(const sg_phase_catalog_expected_t *expected,
	uint32_t cell, const sg_rune_phase_basis_t *candidate,
	uint32_t *phase_out)
{
	uint32_t phase;

	for (phase = 0U; phase < expected->phase_count; phase++)
	{
		const sg_rune_phase_basis_t *record = &expected->phases[phase];

		if (record->order.source_index == cell &&
			PhaseEquivalent(record, candidate))
		{
			if (phase_out)
				*phase_out = phase;
			return 1;
		}
	}
	return 0;
}

static int AppendPhase(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected,
	const sg_rune_phase_basis_t *candidate, uint32_t cell,
	uint32_t *cell_ordinals, uint32_t *phase_out,
	sg_phase_catalog_error_t *error_out)
{
	uint32_t phase;
	sg_rune_phase_basis_t record;

	if (PhaseFind(expected, cell, candidate, &phase))
	{
		if (phase_out)
			*phase_out = phase;
		return 1;
	}
	if (expected->phase_count >= SG_RUNE_MODEL_MAX_PHASES)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OVERFLOW, expected->phase_count);
		return 0;
	}
	phase = expected->phase_count++;
	record = *candidate;
	record.order.local_ordinal = cell_ordinals[cell]++;
	/* The ID is derived after the per-cell ordinal is known. */
	expected->phases[phase] = record;
	expected->phases[phase].id.value = SG_RuneModelStableIdFromOrderKey(
		&expected->phases[phase].order);
	if (!SG_RuneModelPhaseValid(&expected->phases[phase]))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_PHASE, phase);
		return 0;
	}
	(void)source;
	if (phase_out)
		*phase_out = phase;
	return 1;
}

static int AppendBinding(sg_phase_catalog_expected_t *expected,
	const sg_configuration_semantic_region_t *region, uint32_t cell,
	uint32_t phase, sg_phase_catalog_error_t *error_out)
{
	sg_phase_catalog_binding_t *binding;

	if (expected->binding_count >= SG_PHASE_CATALOG_MAX_BINDINGS)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OVERFLOW, expected->binding_count);
		return 0;
	}
	binding = &expected->bindings[expected->binding_count++];
	memset(binding, 0, sizeof(*binding));
	binding->semantic_region_id = region->id;
	binding->configuration_cell = cell;
	binding->phase = expected->phases[phase].id;
	return 1;
}

int SG_PhaseCatalogBuildExpected(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected,
	sg_phase_catalog_error_t *error_out)
{
	sg_phase_support_ref_t *support_refs = NULL;
	uint32_t *cell_ordinals = NULL;
	uint32_t allocation_count;
	uint32_t region;
	uint32_t support_cursor = 0U;

	if (!expected)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	memset(expected, 0, sizeof(*expected));
	if (!SG_PhaseCatalogSourceValidate(source, error_out))
		return 0;
	/* The mover source has its own empty proof, but a non-empty accepted
	 * configuration still yields a complete phase catalog even when that
	 * proof contains zero mover rows. */
	expected->completion = source->configuration->cell_count == 0U ?
		SG_PHASE_CATALOG_PROVEN_EMPTY : SG_PHASE_CATALOG_COMPLETE;
	expected->mover_support_verifier_identity =
		source->mover_support_verifier_identity;
	if (source->configuration->cell_count == 0U)
		return 1;
	if (source->semantics->region_count > UINT32_MAX -
		source->mover_support_count)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OVERFLOW, 0U);
		return 0;
	}
	allocation_count = source->semantics->region_count +
		source->mover_support_count;
	if (allocation_count > SG_PHASE_CATALOG_MAX_BINDINGS ||
		!AllocationFits((size_t)allocation_count, sizeof(*expected->phases)) ||
		!AllocationFits((size_t)allocation_count, sizeof(*expected->bindings)) ||
		!AllocationFits((size_t)source->configuration->cell_count,
			sizeof(*cell_ordinals)))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OVERFLOW, allocation_count);
		return 0;
	}
	expected->phases = calloc((size_t)allocation_count,
		sizeof(*expected->phases));
	expected->bindings = calloc((size_t)allocation_count,
		sizeof(*expected->bindings));
	cell_ordinals = calloc((size_t)source->configuration->cell_count,
		sizeof(*cell_ordinals));
	if (!expected->phases || !expected->bindings || !cell_ordinals)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		free(cell_ordinals);
		SG_PhaseCatalogExpectedDestroy(expected);
		return 0;
	}
	if (source->mover_support_count != 0U)
	{
		support_refs = malloc((size_t)source->mover_support_count *
			sizeof(*support_refs));
		if (!support_refs || !SupportRefsValid(source, support_refs))
		{
			SG_PhaseCatalogSetError(error_out,
				SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
			free(support_refs);
			free(cell_ordinals);
			SG_PhaseCatalogExpectedDestroy(expected);
			return 0;
		}
	}
	for (region = 0U; region < source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&source->semantics->regions[region];
		sg_rune_phase_basis_t candidate;
		uint32_t phase;

		FillPhase(source, record, 0, NULL, record->cell,
			cell_ordinals[record->cell], &candidate);
		if (!AppendPhase(source, expected, &candidate, record->cell,
			cell_ordinals, &phase, error_out) ||
			!AppendBinding(expected, record, record->cell, phase, error_out))
			goto failure;
		while (support_cursor < source->mover_support_count &&
			support_refs[support_cursor].region_id < record->id)
			support_cursor++;
		{
			uint32_t cursor = support_cursor;

			while (cursor < source->mover_support_count &&
				support_refs[cursor].region_id == record->id)
			{
				sg_rune_mechanism_ref_t mechanism;

				mechanism.value = support_refs[cursor].mechanism;
				FillPhase(source, record, 1, &mechanism, record->cell,
					cell_ordinals[record->cell], &candidate);
				if (!AppendPhase(source, expected, &candidate, record->cell,
					cell_ordinals, &phase, error_out) ||
					!AppendBinding(expected, record, record->cell, phase,
						error_out))
					goto failure;
				cursor++;
			}
			support_cursor = cursor;
		}
	}
	if (support_cursor != source->mover_support_count)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, support_cursor);
		goto failure;
	}
	free(support_refs);
	free(cell_ordinals);
	return 1;

failure:
	free(support_refs);
	free(cell_ordinals);
	SG_PhaseCatalogExpectedDestroy(expected);
	return 0;
}

void SG_PhaseCatalogExpectedDestroy(sg_phase_catalog_expected_t *expected)
{
	if (!expected)
		return;
	free(expected->phases);
	free(expected->bindings);
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
		catalog->phase_count <= catalog->phase_capacity &&
		catalog->binding_count <= catalog->binding_capacity &&
		(catalog->phase_count == 0U || catalog->phases) &&
		(catalog->binding_count == 0U || catalog->bindings);
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
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	memset(&expected, 0, sizeof(expected));
	if (!SG_PhaseCatalogBuildExpected(source, &expected, error_out))
		return 0;
	catalog = calloc(1U, sizeof(*catalog));
	if (!catalog)
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY, 0U);
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	catalog->magic = SG_PHASE_CATALOG_MAGIC;
	catalog->magic_inverse = ~SG_PHASE_CATALOG_MAGIC;
	catalog->self = catalog;
	catalog->identity = source->authority->identity;
	catalog->completion = expected.completion;
	catalog->transition_completion = expected.completion;
	catalog->mover_support_verifier_identity =
		expected.mover_support_verifier_identity;
	catalog->phases = expected.phases;
	catalog->phase_count = expected.phase_count;
	catalog->phase_capacity = expected.phase_count;
	catalog->bindings = expected.bindings;
	catalog->binding_count = expected.binding_count;
	catalog->binding_capacity = expected.binding_count;
	memset(&expected, 0, sizeof(expected));
	if (!SG_PhaseCatalogAudit(source, catalog, &(sg_phase_catalog_audit_result_t){0}))
	{
		SG_PhaseCatalogSetError(error_out,
			SG_PHASE_CATALOG_ERROR_AUDIT_REJECTED, 0U);
		SG_PhaseCatalogDestroy(catalog);
		return 0;
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
	memset(catalog, 0, sizeof(*catalog));
	free(catalog);
}

int SG_PhaseCatalogBindingsForRegion(const sg_phase_catalog_t *catalog,
	uint64_t semantic_region_id, const sg_phase_catalog_binding_t **bindings_out,
	uint32_t *binding_count_out)
{
	uint32_t first = SG_PHASE_CATALOG_INDEX_NONE;
	uint32_t index;

	if (bindings_out)
		*bindings_out = NULL;
	if (binding_count_out)
		*binding_count_out = 0U;
	if (!SG_PhaseCatalogHeaderValid(catalog) ||
		!CatalogStorageShapeValid(catalog) || semantic_region_id == 0U ||
		!bindings_out || !binding_count_out)
		return 0;
	for (index = 0U; index < catalog->binding_count; index++)
		if (catalog->bindings[index].semantic_region_id == semantic_region_id)
		{
			first = index;
			break;
		}
	if (first == SG_PHASE_CATALOG_INDEX_NONE)
		return 0;
	index = first;
	while (index < catalog->binding_count &&
		catalog->bindings[index].semantic_region_id == semantic_region_id)
		index++;
	*bindings_out = &catalog->bindings[first];
	*binding_count_out = index - first;
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
	case SG_PHASE_CATALOG_ERROR_AUDIT_REJECTED: return "audit rejected";
	case SG_PHASE_CATALOG_ERROR_ERROR_COUNT: break;
	}
	return "unknown phase catalog error";
}
