#include "sg_ground_capability_publication.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"
#include "sg_mechanism_capability_internal.h"

typedef struct sg_ground_phase_ref_index_s
{
	sg_rune_phase_ref_t reference;
	uint32_t index;
} sg_ground_phase_ref_index_t;

typedef struct sg_ground_cell_ref_index_s
{
	sg_rune_cell_ref_t reference;
	uint32_t index;
} sg_ground_cell_ref_index_t;

typedef struct sg_ground_catalog_binding_key_s
{
	uint64_t region;
	uint32_t cell;
	sg_rune_phase_ref_t phase;
	uint32_t index;
} sg_ground_catalog_binding_key_t;

typedef struct sg_ground_ordered_index_s
{
	sg_rune_order_key_t order;
	uint32_t source;
} sg_ground_ordered_index_t;

typedef struct sg_ground_region_order_s
{
	uint64_t id;
	uint32_t cell;
	uint32_t source;
} sg_ground_region_order_t;

/* The accepted configuration builder groups records by stance, while the
 * canonical ground constructor consumes records in stable-order-key order.
 * This private, bijectively indexed copy reconciles those representations;
 * published facts are mapped back to the accepted source indices and IDs. */
typedef struct sg_ground_reconstruction_source_s
{
	sg_configuration_space_t configuration;
	sg_configuration_semantics_t semantics;
	sg_ground_phase_binding_t *bindings;
	uint32_t *cell_to_source;
	uint32_t *source_to_cell;
	uint32_t *portal_to_source;
	uint32_t *region_to_source;
} sg_ground_reconstruction_source_t;

typedef struct sg_ground_normalized_source_s
{
	const sg_host_law_publication_t *engine_authority;
	const sg_host_collision_authority_t *collision_authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_phase_catalog_publication_owner_t *phase_catalog_owner;
	const sg_phase_catalog_publication_t *phase_catalog;
	sg_host_law_view_t engine_view;
	sg_phase_catalog_completion_t phase_completion;
	sg_phase_catalog_completion_t transition_completion;
	uint64_t mover_support_verifier_identity;
	uint64_t configuration_fingerprint;
	uint64_t semantics_fingerprint;
	sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	sg_phase_catalog_binding_t *catalog_bindings;
	uint32_t catalog_binding_count;
	sg_rune_phase_transition_t *phase_transitions;
	sg_phase_catalog_transition_evidence_t *transition_evidence;
	uint32_t phase_transition_count;
	sg_ground_phase_binding_t *ground_bindings;
	uint32_t ground_binding_count;
} sg_ground_normalized_source_t;

typedef struct sg_ground_publication_payload_s
{
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_publication_fact_t *facts;
	uint64_t digest;
} sg_ground_publication_payload_t;

typedef struct sg_ground_publication_record_s
{
	sg_ground_capability_publication_t *token;
	sg_ground_publication_payload_t *payload;
	struct sg_ground_publication_record_s *next;
} sg_ground_publication_record_t;

struct sg_ground_capability_publication_owner_s
{
	sg_ground_publication_record_t *live;
	uint32_t live_count;
};

static _Thread_local int sg_ground_engine_pmove_failed;

static int CapabilityOrderCompare(
	const sg_ground_capability_t *left,
	const sg_ground_capability_t *right);

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int FloatBitsEqual(float left, float right)
{
	return FloatBits(left) == FloatBits(right);
}

static int Vec3BitsEqual(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	return FloatBitsEqual(left->value[0], right->value[0]) &&
		FloatBitsEqual(left->value[1], right->value[1]) &&
		FloatBitsEqual(left->value[2], right->value[2]);
}

static int IntervalBitsEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return FloatBitsEqual(left->min_value, right->min_value) &&
		FloatBitsEqual(left->max_value, right->max_value);
}

static int Interval3BitsEqual(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return IntervalBitsEqual(&left->x, &right->x) &&
		IntervalBitsEqual(&left->y, &right->y) &&
		IntervalBitsEqual(&left->z, &right->z);
}

int SG_GroundCapabilityFactBitsEqual(
	const sg_ground_capability_t *left,
	const sg_ground_capability_t *right)
{
	return left && right && left->source_cell == right->source_cell &&
		left->destination_cell == right->destination_cell &&
		left->source_region == right->source_region &&
		left->destination_region == right->destination_region &&
		left->portal == right->portal &&
		left->source_phase == right->source_phase &&
		left->destination_phase == right->destination_phase &&
		left->kind == right->kind &&
		Vec3BitsEqual(&left->source_witness, &right->source_witness) &&
		Vec3BitsEqual(&left->destination_witness,
			&right->destination_witness) &&
		Vec3BitsEqual(&left->initial_velocity, &right->initial_velocity) &&
		Vec3BitsEqual(&left->observed_velocity, &right->observed_velocity) &&
		Interval3BitsEqual(&left->displacement, &right->displacement) &&
		IntervalBitsEqual(&left->duration_ms, &right->duration_ms) &&
		FloatBitsEqual(left->acceleration, right->acceleration) &&
		FloatBitsEqual(left->gravity, right->gravity) &&
		left->physics_abi_id == right->physics_abi_id &&
		left->flags == right->flags;
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return Vec3BitsEqual(&left->mins, &right->mins) &&
		Vec3BitsEqual(&left->maxs, &right->maxs);
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return FloatBitsEqual(left->gravity, right->gravity) &&
		FloatBitsEqual(left->ground_acceleration, right->ground_acceleration) &&
		FloatBitsEqual(left->air_acceleration, right->air_acceleration) &&
		FloatBitsEqual(left->water_acceleration, right->water_acceleration) &&
		FloatBitsEqual(left->hook_acceleration, right->hook_acceleration) &&
		FloatBitsEqual(left->external_acceleration,
			right->external_acceleration) &&
		FloatBitsEqual(left->water_drag, right->water_drag) &&
		FloatBitsEqual(left->max_velocity, right->max_velocity) &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
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

static int PhaseRefIndexCompare(const void *left_value,
	const void *right_value)
{
	const sg_ground_phase_ref_index_t *left = left_value;
	const sg_ground_phase_ref_index_t *right = right_value;

	return StableIdCompare(&left->reference.value, &right->reference.value);
}

static int CellRefIndexCompare(const void *left_value,
	const void *right_value)
{
	const sg_ground_cell_ref_index_t *left = left_value;
	const sg_ground_cell_ref_index_t *right = right_value;

	return StableIdCompare(&left->reference.value, &right->reference.value);
}

static int GroundBindingCompare(const void *left_value,
	const void *right_value)
{
	const sg_ground_phase_binding_t *left = left_value;
	const sg_ground_phase_binding_t *right = right_value;

	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	if (left->phase != right->phase)
		return left->phase < right->phase ? -1 : 1;
	return 0;
}

static int CatalogBindingKeyCompare(const void *left_value,
	const void *right_value)
{
	const sg_ground_catalog_binding_key_t *left = left_value;
	const sg_ground_catalog_binding_key_t *right = right_value;

	if (left->region != right->region)
		return left->region < right->region ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	return StableIdCompare(&left->phase.value, &right->phase.value);
}

static int FindPhaseIndex(const sg_ground_phase_ref_index_t *index,
	uint32_t count, sg_rune_phase_ref_t reference, uint32_t *phase_out)
{
	uint32_t low = 0U;
	uint32_t high = count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&index[middle].reference.value,
			&reference.value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= count || StableIdCompare(&index[low].reference.value,
		&reference.value) != 0)
		return 0;
	*phase_out = index[low].index;
	return 1;
}

static int FindCellIndex(const sg_ground_cell_ref_index_t *index,
	uint32_t count, sg_rune_cell_ref_t reference, uint32_t *cell_out)
{
	uint32_t low = 0U;
	uint32_t high = count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&index[middle].reference.value,
			&reference.value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= count || StableIdCompare(&index[low].reference.value,
		&reference.value) != 0)
		return 0;
	*cell_out = index[low].index;
	return 1;
}

static int FindRegionIndex(const sg_configuration_semantics_t *semantics,
	uint64_t id, uint32_t *region_out)
{
	uint32_t low = 0U;
	uint32_t high = semantics->region_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		uint64_t candidate = semantics->regions[middle].id;

		if (candidate < id)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= semantics->region_count || semantics->regions[low].id != id)
		return 0;
	*region_out = low;
	return 1;
}

static void SetResult(sg_ground_capability_audit_result_t *result,
	sg_ground_capability_audit_code_t code, uint32_t record)
{
	result->code = code;
	result->record = record;
}

static void NormalizedSourceDestroy(sg_ground_normalized_source_t *source)
{
	if (!source)
		return;
	free(source->phases);
	free(source->catalog_bindings);
	free(source->phase_transitions);
	free(source->transition_evidence);
	free(source->ground_bindings);
	memset(source, 0, sizeof(*source));
}

static int AuditAcceptedGeometry(
	const sg_ground_normalized_source_t *source,
	sg_ground_capability_audit_result_t *result)
{
	sg_bsp_completeness_result_t completeness;
	sg_configuration_audit_result_t configuration;
	sg_configuration_semantics_audit_result_t semantics;

	memset(&completeness, 0, sizeof(completeness));
	memset(&configuration, 0, sizeof(configuration));
	memset(&semantics, 0, sizeof(semantics));
	if (!SG_BspCompletenessProve(source->collision_authority,
			source->configuration, &completeness) ||
		completeness.code != SG_BSP_COMPLETENESS_OK ||
		completeness.represented_cells != source->configuration->cell_count ||
		completeness.proved_cells != source->configuration->cell_count ||
		completeness.expected_portals != source->configuration->portal_count ||
		completeness.represented_portals !=
			source->configuration->portal_count ||
		completeness.proved_portals != source->configuration->portal_count ||
		completeness.omitted_cells != 0U ||
		completeness.invented_cells != 0U ||
		completeness.omitted_portals != 0U ||
		completeness.invented_portals != 0U)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
			completeness.record);
		return 0;
	}
	if (!SG_ConfigurationAudit(source->collision_authority,
			source->configuration, &configuration) ||
		configuration.code != SG_CONFIGURATION_AUDIT_OK ||
		configuration.proved_cells != source->configuration->cell_count ||
		configuration.proved_portals != source->configuration->portal_count ||
		configuration.omitted_cells != 0U ||
		configuration.omitted_portals != 0U ||
		configuration.invented_portals != 0U)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
			configuration.record);
		return 0;
	}
	if (!SG_ConfigurationSemanticsAudit(source->collision_authority,
		source->configuration, source->semantics, &semantics) ||
		semantics.code != SG_CONFIGURATION_SEMANTICS_AUDIT_OK)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SEMANTICS_REJECTED,
			semantics.record);
		return 0;
	}
	return 1;
}

static int EngineViewValid(const sg_ground_normalized_source_t *source)
{
	const sg_host_law_view_t *view = &source->engine_view;
	const sg_rune_model_identity_t *identity =
		&source->collision_authority->identity;

	return view->version == SG_HOST_LAW_PUBLICATION_VERSION &&
		view->reserved == 0U && view->collision_law_id != 0U &&
		view->pmove_law_id != 0U && view->gravity_law_id != 0U &&
		view->collision_law_id != view->pmove_law_id &&
		view->collision_law_id != view->gravity_law_id &&
		view->pmove_law_id != view->gravity_law_id &&
		view->pmove_abi.version == SG_HOST_ENGINE_PMOVE_ABI_VERSION &&
		view->pmove_abi.identity != 0U &&
		view->pmove_behavior_fingerprint == view->pmove_abi.identity &&
		view->pmove_abi.identity == identity->physics_abi_id &&
		view->pmove_abi.substep_ms == identity->physics.substep_ms &&
		FloatBitsEqual(view->airaccelerate, 0.0f) &&
		FloatBitsEqual(view->maxvelocity, identity->physics.max_velocity) &&
		view->movement_flags == 0U &&
		view->physics_flags == SG_HOST_ENGINE_PHYSICS_FLAGS &&
		IdentityEqual(&view->identity, identity);
}

static int CopyArray(void **destination, const void *source, size_t count,
	size_t element_size)
{
	size_t bytes;

	*destination = NULL;
	if (count == 0U)
		return 1;
	if (!source || !AllocationFits(count, element_size))
		return 0;
	bytes = count * element_size;
	*destination = malloc(bytes);
	if (!*destination)
		return 0;
	memcpy(*destination, source, bytes);
	return 1;
}

static int OrderedIndexCompare(const void *left_value,
	const void *right_value)
{
	const sg_ground_ordered_index_t *left = left_value;
	const sg_ground_ordered_index_t *right = right_value;
	int order = SG_RuneModelOrderKeyCompare(&left->order, &right->order);

	if (order != 0)
		return order;
	return left->source < right->source ? -1 : left->source > right->source;
}

static int RegionOrderCompare(const void *left_value,
	const void *right_value)
{
	const sg_ground_region_order_t *left = left_value;
	const sg_ground_region_order_t *right = right_value;

	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	if (left->id != right->id)
		return left->id < right->id ? -1 : 1;
	return left->source < right->source ? -1 : left->source > right->source;
}

static int CapabilityQsortCompare(const void *left_value,
	const void *right_value)
{
	return CapabilityOrderCompare(left_value, right_value);
}

static void ReconstructionSourceDestroy(
	sg_ground_reconstruction_source_t *source)
{
	if (!source)
		return;
	free(source->configuration.cells);
	free(source->configuration.faces);
	free(source->configuration.vertices);
	free(source->configuration.portals);
	free(source->configuration.stance_overlaps);
	free(source->configuration.certificate_nodes);
	free(source->semantics.regions);
	free(source->semantics.faces);
	free(source->semantics.vertices);
	free(source->semantics.boundaries);
	free(source->semantics.hook_surfaces);
	free(source->semantics.hook_vertices);
	free(source->bindings);
	free(source->cell_to_source);
	free(source->source_to_cell);
	free(source->portal_to_source);
	free(source->region_to_source);
	memset(source, 0, sizeof(*source));
}

static int ReconstructionSizesFit(
	const sg_ground_normalized_source_t *source)
{
	const sg_configuration_space_t *configuration = source->configuration;
	const sg_configuration_semantics_t *semantics = source->semantics;

#define SG_GROUND_FITS(count, type) AllocationFits((count), sizeof(type))
	return SG_GROUND_FITS(configuration->cell_count,
			sg_configuration_cell_t) &&
		SG_GROUND_FITS(configuration->face_count,
			sg_configuration_face_t) &&
		SG_GROUND_FITS(configuration->vertex_count, sg_rune_vec3_t) &&
		SG_GROUND_FITS(configuration->portal_count,
			sg_configuration_portal_t) &&
		SG_GROUND_FITS(configuration->stance_overlap_count,
			sg_configuration_stance_overlap_t) &&
		SG_GROUND_FITS(configuration->certificate_node_count,
			sg_configuration_certificate_node_t) &&
		SG_GROUND_FITS(semantics->region_count,
			sg_configuration_semantic_region_t) &&
		SG_GROUND_FITS(semantics->face_count,
			sg_configuration_semantic_face_t) &&
		SG_GROUND_FITS(semantics->vertex_count, sg_rune_vec3_t) &&
		SG_GROUND_FITS(semantics->boundary_count,
			sg_configuration_boundary_t) &&
		SG_GROUND_FITS(semantics->hook_surface_count,
			sg_configuration_hook_surface_t) &&
		SG_GROUND_FITS(semantics->hook_vertex_count, sg_rune_vec3_t) &&
		SG_GROUND_FITS(source->ground_binding_count,
			sg_ground_phase_binding_t) &&
		SG_GROUND_FITS(configuration->cell_count,
			sg_ground_ordered_index_t) &&
		SG_GROUND_FITS(configuration->cell_count, uint32_t) &&
		SG_GROUND_FITS(configuration->portal_count,
			sg_ground_ordered_index_t) &&
		SG_GROUND_FITS(configuration->portal_count, uint32_t) &&
		SG_GROUND_FITS(semantics->region_count,
			sg_ground_region_order_t) &&
		SG_GROUND_FITS(semantics->region_count, uint32_t);
#undef SG_GROUND_FITS
}

static int ReconstructionSourceBuild(
	const sg_ground_normalized_source_t *source,
	sg_ground_reconstruction_source_t *reconstruction,
	sg_ground_capability_audit_result_t *result)
{
	const sg_configuration_space_t *configuration = source->configuration;
	const sg_configuration_semantics_t *semantics = source->semantics;
	sg_ground_ordered_index_t *cell_order = NULL;
	sg_ground_ordered_index_t *portal_order = NULL;
	sg_ground_region_order_t *region_order = NULL;
	uint32_t index;
	int ok = 0;

	memset(reconstruction, 0, sizeof(*reconstruction));
	if (!ReconstructionSizesFit(source))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OVERFLOW, 0U);
		return 0;
	}
	reconstruction->configuration = *configuration;
	reconstruction->configuration.cells = NULL;
	reconstruction->configuration.faces = NULL;
	reconstruction->configuration.vertices = NULL;
	reconstruction->configuration.portals = NULL;
	reconstruction->configuration.stance_overlaps = NULL;
	reconstruction->configuration.certificate_nodes = NULL;
	reconstruction->semantics = *semantics;
	reconstruction->semantics.regions = NULL;
	reconstruction->semantics.faces = NULL;
	reconstruction->semantics.vertices = NULL;
	reconstruction->semantics.boundaries = NULL;
	reconstruction->semantics.hook_surfaces = NULL;
	reconstruction->semantics.hook_vertices = NULL;
	if (!CopyArray((void **)&reconstruction->configuration.cells,
			configuration->cells, configuration->cell_count,
			sizeof(*configuration->cells)) ||
		!CopyArray((void **)&reconstruction->configuration.faces,
			configuration->faces, configuration->face_count,
			sizeof(*configuration->faces)) ||
		!CopyArray((void **)&reconstruction->configuration.vertices,
			configuration->vertices, configuration->vertex_count,
			sizeof(*configuration->vertices)) ||
		!CopyArray((void **)&reconstruction->configuration.portals,
			configuration->portals, configuration->portal_count,
			sizeof(*configuration->portals)) ||
		!CopyArray((void **)&reconstruction->configuration.stance_overlaps,
			configuration->stance_overlaps, configuration->stance_overlap_count,
			sizeof(*configuration->stance_overlaps)) ||
		!CopyArray((void **)&reconstruction->configuration.certificate_nodes,
			configuration->certificate_nodes, configuration->certificate_node_count,
			sizeof(*configuration->certificate_nodes)) ||
		!CopyArray((void **)&reconstruction->semantics.regions,
			semantics->regions, semantics->region_count,
			sizeof(*semantics->regions)) ||
		!CopyArray((void **)&reconstruction->semantics.faces,
			semantics->faces, semantics->face_count,
			sizeof(*semantics->faces)) ||
		!CopyArray((void **)&reconstruction->semantics.vertices,
			semantics->vertices, semantics->vertex_count,
			sizeof(*semantics->vertices)) ||
		!CopyArray((void **)&reconstruction->semantics.boundaries,
			semantics->boundaries, semantics->boundary_count,
			sizeof(*semantics->boundaries)) ||
		!CopyArray((void **)&reconstruction->semantics.hook_surfaces,
			semantics->hook_surfaces, semantics->hook_surface_count,
			sizeof(*semantics->hook_surfaces)) ||
		!CopyArray((void **)&reconstruction->semantics.hook_vertices,
			semantics->hook_vertices, semantics->hook_vertex_count,
			sizeof(*semantics->hook_vertices)) ||
		!CopyArray((void **)&reconstruction->bindings,
			source->ground_bindings, source->ground_binding_count,
			sizeof(*source->ground_bindings)))
		goto out_of_memory;
	cell_order = malloc((size_t)configuration->cell_count *
		sizeof(*cell_order));
	reconstruction->cell_to_source = malloc(
		(size_t)configuration->cell_count *
		sizeof(*reconstruction->cell_to_source));
	reconstruction->source_to_cell = malloc(
		(size_t)configuration->cell_count *
		sizeof(*reconstruction->source_to_cell));
	region_order = malloc((size_t)semantics->region_count *
		sizeof(*region_order));
	reconstruction->region_to_source = malloc(
		(size_t)semantics->region_count *
		sizeof(*reconstruction->region_to_source));
	if (!cell_order || !reconstruction->cell_to_source ||
		!reconstruction->source_to_cell || !region_order ||
		!reconstruction->region_to_source)
		goto out_of_memory;
	if (configuration->portal_count != 0U)
	{
		portal_order = malloc((size_t)configuration->portal_count *
			sizeof(*portal_order));
		reconstruction->portal_to_source = malloc(
			(size_t)configuration->portal_count *
			sizeof(*reconstruction->portal_to_source));
		if (!portal_order || !reconstruction->portal_to_source)
			goto out_of_memory;
	}
	for (index = 0U; index < configuration->cell_count; index++)
	{
		cell_order[index].order = configuration->cells[index].order;
		cell_order[index].source = index;
	}
	qsort(cell_order, configuration->cell_count, sizeof(*cell_order),
		OrderedIndexCompare);
	for (index = 0U; index < configuration->cell_count; index++)
	{
		uint32_t original = cell_order[index].source;

		if (index != 0U && SG_RuneModelOrderKeyCompare(
				&cell_order[index - 1U].order, &cell_order[index].order) >= 0)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
				original);
			goto done;
		}
		reconstruction->configuration.cells[index] =
			configuration->cells[original];
		reconstruction->cell_to_source[index] = original;
		reconstruction->source_to_cell[original] = index;
	}
	for (index = 0U; index < configuration->portal_count; index++)
	{
		portal_order[index].order = configuration->portals[index].order;
		portal_order[index].source = index;
	}
	if (configuration->portal_count != 0U)
		qsort(portal_order, configuration->portal_count, sizeof(*portal_order),
			OrderedIndexCompare);
	for (index = 0U; index < configuration->portal_count; index++)
	{
		uint32_t original = portal_order[index].source;
		sg_configuration_portal_t *portal =
			&reconstruction->configuration.portals[index];

		if (index != 0U && SG_RuneModelOrderKeyCompare(
				&portal_order[index - 1U].order,
				&portal_order[index].order) >= 0)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
				original);
			goto done;
		}
		*portal = configuration->portals[original];
		portal->from_cell = reconstruction->source_to_cell[portal->from_cell];
		portal->to_cell = reconstruction->source_to_cell[portal->to_cell];
		reconstruction->portal_to_source[index] = original;
	}
	for (index = 0U; index < configuration->stance_overlap_count; index++)
	{
		sg_configuration_stance_overlap_t *overlap =
			&reconstruction->configuration.stance_overlaps[index];

		overlap->standing_cell =
			reconstruction->source_to_cell[overlap->standing_cell];
		overlap->crouching_cell =
			reconstruction->source_to_cell[overlap->crouching_cell];
	}
	for (index = 0U; index < configuration->certificate_node_count; index++)
	{
		sg_configuration_certificate_node_t *node =
			&reconstruction->configuration.certificate_nodes[index];

		if (node->cell != SG_CONFIGURATION_INDEX_NONE)
			node->cell = reconstruction->source_to_cell[node->cell];
	}
	for (index = 0U; index < semantics->region_count; index++)
	{
		region_order[index].id = semantics->regions[index].id;
		region_order[index].cell = reconstruction->source_to_cell[
			semantics->regions[index].cell];
		region_order[index].source = index;
	}
	qsort(region_order, semantics->region_count, sizeof(*region_order),
		RegionOrderCompare);
	for (index = 0U; index < semantics->region_count; index++)
	{
		uint32_t original = region_order[index].source;
		sg_configuration_semantic_region_t *region =
			&reconstruction->semantics.regions[index];

		*region = semantics->regions[original];
		region->cell = region_order[index].cell;
		/* Region IDs are opaque to construction, but its source guard
		 * requires global monotonicity in addition to cell grouping. */
		region->id = index;
		reconstruction->region_to_source[index] = original;
	}
	for (index = 0U; index < semantics->boundary_count; index++)
		reconstruction->semantics.boundaries[index].cell =
			reconstruction->source_to_cell[
				semantics->boundaries[index].cell];
	for (index = 0U; index < source->ground_binding_count; index++)
		reconstruction->bindings[index].cell =
			reconstruction->source_to_cell[
				reconstruction->bindings[index].cell];
	qsort(reconstruction->bindings, source->ground_binding_count,
		sizeof(*reconstruction->bindings), GroundBindingCompare);
	ok = 1;
	goto done;

out_of_memory:
	SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U);
done:
	free(cell_order);
	free(portal_order);
	free(region_order);
	if (!ok)
		ReconstructionSourceDestroy(reconstruction);
	return ok;
}

static int RemapReconstructedFacts(
	const sg_ground_reconstruction_source_t *reconstruction,
	sg_ground_capability_set_t *set,
	sg_ground_capability_audit_result_t *result)
{
	uint32_t index;

	for (index = 0U; index < set->capability_count; index++)
	{
		sg_ground_capability_t *fact = &set->capabilities[index];

		if (fact->source_cell >= reconstruction->configuration.cell_count ||
			fact->destination_cell >= reconstruction->configuration.cell_count ||
			fact->source_region >= reconstruction->semantics.region_count ||
			fact->destination_region >= reconstruction->semantics.region_count ||
			(fact->portal != SG_GROUND_CAPABILITY_INDEX_NONE &&
			 fact->portal >= reconstruction->configuration.portal_count))
		{
			SetResult(result,
				SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED, index);
			return 0;
		}
		fact->source_cell = reconstruction->cell_to_source[fact->source_cell];
		fact->destination_cell =
			reconstruction->cell_to_source[fact->destination_cell];
		fact->source_region =
			reconstruction->region_to_source[fact->source_region];
		fact->destination_region =
			reconstruction->region_to_source[fact->destination_region];
		if (fact->portal != SG_GROUND_CAPABILITY_INDEX_NONE)
			fact->portal = reconstruction->portal_to_source[fact->portal];
	}
	if (set->capability_count != 0U)
		qsort(set->capabilities, set->capability_count,
			sizeof(*set->capabilities), CapabilityQsortCompare);
	return 1;
}

static uint64_t HashBytes(uint64_t hash, const void *bytes, size_t size)
{
	const unsigned char *cursor = bytes;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		hash ^= cursor[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static uint64_t ConfigurationFingerprint(
	const sg_configuration_space_t *configuration)
{
	uint64_t hash = UINT64_C(1469598103934665603);

	hash = HashBytes(hash, configuration, sizeof(*configuration));
#define SG_GROUND_HASH_CONFIGURATION(member, count) \
	hash = HashBytes(hash, configuration->member, \
		(size_t)configuration->count * sizeof(*configuration->member))
	SG_GROUND_HASH_CONFIGURATION(cells, cell_count);
	SG_GROUND_HASH_CONFIGURATION(faces, face_count);
	SG_GROUND_HASH_CONFIGURATION(vertices, vertex_count);
	SG_GROUND_HASH_CONFIGURATION(portals, portal_count);
	SG_GROUND_HASH_CONFIGURATION(stance_overlaps, stance_overlap_count);
	SG_GROUND_HASH_CONFIGURATION(certificate_nodes, certificate_node_count);
#undef SG_GROUND_HASH_CONFIGURATION
	return hash;
}

static uint64_t SemanticsFingerprint(
	const sg_configuration_semantics_t *semantics)
{
	uint64_t hash = UINT64_C(1469598103934665603);

	hash = HashBytes(hash, semantics, sizeof(*semantics));
#define SG_GROUND_HASH_SEMANTICS(member, count) \
	hash = HashBytes(hash, semantics->member, \
		(size_t)semantics->count * sizeof(*semantics->member))
	SG_GROUND_HASH_SEMANTICS(regions, region_count);
	SG_GROUND_HASH_SEMANTICS(faces, face_count);
	SG_GROUND_HASH_SEMANTICS(vertices, vertex_count);
	SG_GROUND_HASH_SEMANTICS(boundaries, boundary_count);
	SG_GROUND_HASH_SEMANTICS(hook_surfaces, hook_surface_count);
	SG_GROUND_HASH_SEMANTICS(hook_vertices, hook_vertex_count);
#undef SG_GROUND_HASH_SEMANTICS
	return hash;
}

static int RecordIdValid(const sg_rune_stable_id_t *id,
	const sg_rune_order_key_t *order, sg_rune_order_domain_t domain,
	uint64_t source_set_identity)
{
	sg_rune_stable_id_t expected;

	if (!SG_RuneModelStableIdValid(id) ||
		!SG_RuneModelOrderKeyValid(order) || order->domain != domain ||
		order->source_set_identity != source_set_identity)
		return 0;
	expected = SG_RuneModelStableIdFromOrderKey(order);
	return SG_RuneModelStableIdEqual(id, &expected);
}

static int ReferenceIsNone(const sg_rune_stable_id_t *reference)
{
	return !SG_RuneModelStableIdValid(reference) &&
		StableIdCompare(reference, &SG_RUNE_STABLE_ID_NONE) == 0;
}

static int ReferenceIsAbsent(const sg_rune_stable_id_t *reference)
{
	return reference &&
		((reference->source_set_identity == 0U && reference->high == 0U &&
		  reference->low == 0U) || ReferenceIsNone(reference));
}

static int TransitionTimingFieldsZero(
	const sg_phase_catalog_transition_evidence_t *evidence)
{
	return evidence->source_state_mask == 0U &&
		evidence->destination_state_mask == 0U &&
		evidence->provider_verifier_identity == 0U &&
		evidence->delay_ms == 0U && evidence->dwell_ms == 0U &&
		evidence->travel_ms == 0U && evidence->wait_ms == 0U &&
		evidence->reset_ms == 0U && evidence->activation_time_ms == 0U &&
		evidence->active_time_ms == 0U && evidence->exit_time_ms == 0U &&
		evidence->reset_time_ms == 0U;
}

static int PhaseClockEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int PhaseDiscreteEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left->stance == right->stance &&
		left->motion == right->motion && left->support == right->support &&
		left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value);
}

static int PhaseEqualExceptStance(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left->motion == right->motion && left->support == right->support &&
		left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value) &&
		Interval3BitsEqual(&left->velocity, &right->velocity) &&
		IntervalBitsEqual(&left->elapsed_ms, &right->elapsed_ms) &&
		PhaseClockEqual(left, right);
}

static int TransitionPhaseSemanticsValid(
	const sg_rune_phase_transition_t *transition,
	const sg_rune_phase_basis_t *source,
	const sg_rune_phase_basis_t *destination)
{
	if (source->medium != destination->medium)
		return 0;
	switch (transition->kind)
	{
	case SG_RUNE_PHASE_TRANSITION_STANCE:
		return source->stance != destination->stance &&
			PhaseEqualExceptStance(source, destination);
	case SG_RUNE_PHASE_TRANSITION_PORTAL:
		return PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) &&
			Interval3BitsEqual(&source->velocity, &destination->velocity) &&
			IntervalBitsEqual(&source->elapsed_ms, &destination->elapsed_ms);
	case SG_RUNE_PHASE_TRANSITION_SUPPORT:
		return source->motion == SG_RUNE_MOTION_AIRBORNE &&
			source->support == SG_RUNE_SUPPORT_NONE &&
			destination->motion == SG_RUNE_MOTION_SUPPORTED &&
			destination->support != SG_RUNE_SUPPORT_NONE &&
			source->stance == destination->stance &&
			source->void_relation == destination->void_relation &&
			PhaseClockEqual(source, destination);
	case SG_RUNE_PHASE_TRANSITION_TIME:
		return PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) &&
			Interval3BitsEqual(&source->velocity, &destination->velocity) &&
			!IntervalBitsEqual(&source->elapsed_ms, &destination->elapsed_ms);
	case SG_RUNE_PHASE_TRANSITION_MOVER_DWELL:
		return PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) &&
			source->support == SG_RUNE_SUPPORT_MOVER &&
			Interval3BitsEqual(&source->velocity, &destination->velocity) &&
			!IntervalBitsEqual(&source->elapsed_ms, &destination->elapsed_ms);
	case SG_RUNE_PHASE_TRANSITION_NONE:
	case SG_RUNE_PHASE_TRANSITION_ACCELERATION:
	case SG_RUNE_PHASE_TRANSITION_TAKEOFF:
	case SG_RUNE_PHASE_TRANSITION_RELAUNCH:
	case SG_RUNE_PHASE_TRANSITION_KIND_COUNT:
		return 0;
	}
	return 0;
}

static uint32_t TransitionTimingSpan(
	const sg_phase_catalog_transition_evidence_t *evidence)
{
	uint64_t total = (uint64_t)evidence->delay_ms + evidence->dwell_ms +
		evidence->travel_ms + evidence->wait_ms + evidence->reset_ms;

	if (total == 0U)
		return 1U;
	return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static int TransitionValid(
	const sg_ground_normalized_source_t *source,
	const sg_ground_phase_ref_index_t *phase_index,
	const sg_ground_cell_ref_index_t *cell_index,
	const uint32_t *phase_cells, uint32_t index)
{
	const sg_rune_phase_transition_t *transition =
		&source->phase_transitions[index];
	const sg_phase_catalog_transition_evidence_t *evidence =
		&source->transition_evidence[index];
	uint32_t source_phase;
	uint32_t destination_phase;
	uint32_t source_cell;
	uint32_t destination_cell;
	uint32_t source_region;
	uint32_t destination_region;
	int cross_cell;

	if (!RecordIdValid(&transition->id.value, &transition->order,
			SG_RUNE_ORDER_PHASE_TRANSITION,
			source->configuration->identity.source_set_identity) ||
		transition->order.source_index != index ||
		transition->order.local_ordinal != 0U ||
		transition->order.variant != (uint32_t)evidence->origin ||
		(index != 0U && SG_RuneModelOrderKeyCompare(
			&source->phase_transitions[index - 1U].order,
			&transition->order) >= 0) ||
		!FindPhaseIndex(phase_index, source->phase_count,
			transition->source_phase, &source_phase) ||
		!FindPhaseIndex(phase_index, source->phase_count,
			transition->destination_phase, &destination_phase) ||
		source_phase == destination_phase ||
		!FindCellIndex(cell_index, source->configuration->cell_count,
			transition->cell, &source_cell) ||
		!FindCellIndex(cell_index, source->configuration->cell_count,
			transition->destination_cell, &destination_cell) ||
		phase_cells[source_phase] != source_cell ||
		phase_cells[destination_phase] != destination_cell ||
		transition->kind <= SG_RUNE_PHASE_TRANSITION_NONE ||
		transition->kind >= SG_RUNE_PHASE_TRANSITION_KIND_COUNT ||
		!isfinite(transition->duration_ms.min_value) ||
		!isfinite(transition->duration_ms.max_value) ||
		transition->duration_ms.min_value < 0.0f ||
		transition->duration_ms.min_value >
			transition->duration_ms.max_value ||
		(transition->kind != SG_RUNE_PHASE_TRANSITION_PORTAL &&
		 transition->duration_ms.max_value <= 0.0f) ||
		(transition->flags & ~(sg_rune_phase_transition_flags_t)
			SG_RUNE_PHASE_TRANSITION_FLAGS_KNOWN) != 0U ||
		evidence->origin < SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP ||
		evidence->origin >= SG_PHASE_CATALOG_TRANSITION_ORIGIN_COUNT ||
		evidence->source_cell != source_cell ||
		evidence->destination_cell != destination_cell ||
		!FindRegionIndex(source->semantics, evidence->source_region_id,
			&source_region) ||
		!FindRegionIndex(source->semantics, evidence->destination_region_id,
			&destination_region) ||
		source->semantics->regions[source_region].cell != source_cell ||
		source->semantics->regions[destination_region].cell !=
			destination_cell ||
		!TransitionPhaseSemanticsValid(transition,
			&source->phases[source_phase],
			&source->phases[destination_phase]))
		return 0;
	cross_cell = source_cell != destination_cell;
	if (((transition->flags & SG_RUNE_PHASE_TRANSITION_CROSS_CELL) != 0U) !=
		cross_cell)
		return 0;

	switch (evidence->origin)
	{
	case SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP:
		if (evidence->source_record >=
				source->configuration->stance_overlap_count ||
			evidence->destination_record != SG_PHASE_CATALOG_INDEX_NONE ||
			transition->kind != SG_RUNE_PHASE_TRANSITION_STANCE ||
			!cross_cell || !ReferenceIsAbsent(&evidence->portal.value) ||
			!ReferenceIsAbsent(&evidence->mechanism.value) ||
			!TransitionTimingFieldsZero(evidence) ||
			evidence->portal_duration_ms != 0U ||
			!FloatBitsEqual(transition->duration_ms.min_value,
				(float)source->configuration->identity.physics.substep_ms) ||
			!FloatBitsEqual(transition->duration_ms.max_value,
				(float)source->configuration->identity.physics.frame_ms))
			return 0;
		{
			const sg_configuration_stance_overlap_t *overlap =
				&source->configuration->stance_overlaps[
					evidence->source_record];
			if (!((source_cell == overlap->standing_cell &&
					destination_cell == overlap->crouching_cell) ||
				(source_cell == overlap->crouching_cell &&
					destination_cell == overlap->standing_cell)))
				return 0;
		}
		break;
	case SG_PHASE_CATALOG_TRANSITION_PORTAL:
		if (evidence->source_record >= source->configuration->portal_count ||
			transition->kind != SG_RUNE_PHASE_TRANSITION_PORTAL ||
			!cross_cell || !ReferenceIsAbsent(&evidence->mechanism.value) ||
			!TransitionTimingFieldsZero(evidence) ||
			evidence->portal_duration_ms != 0U ||
			!FloatBitsEqual(transition->duration_ms.min_value, 0.0f) ||
			!FloatBitsEqual(transition->duration_ms.max_value, 0.0f))
			return 0;
		{
			const sg_configuration_portal_t *portal =
				&source->configuration->portals[evidence->source_record];
			if (!SG_RuneModelStableIdEqual(&evidence->portal.value,
					&portal->id.value) ||
				evidence->destination_record != destination_cell ||
				!((source_cell == portal->from_cell &&
					destination_cell == portal->to_cell) ||
					(source_cell == portal->to_cell &&
					destination_cell == portal->from_cell)))
				return 0;
		}
		break;
	case SG_PHASE_CATALOG_TRANSITION_SUPPORT_CHANGE:
		if (evidence->source_record != source_region ||
			evidence->destination_record != destination_region || cross_cell ||
			transition->kind != SG_RUNE_PHASE_TRANSITION_SUPPORT ||
			!ReferenceIsAbsent(&evidence->portal.value) ||
			!ReferenceIsAbsent(&evidence->mechanism.value) ||
			!TransitionTimingFieldsZero(evidence) ||
			evidence->portal_duration_ms != 0U ||
			!FloatBitsEqual(transition->duration_ms.min_value,
				(float)source->configuration->identity.physics.substep_ms) ||
			!FloatBitsEqual(transition->duration_ms.max_value,
				(float)source->configuration->identity.physics.frame_ms))
			return 0;
		break;
	case SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING:
		if (evidence->destination_record != SG_PHASE_CATALOG_INDEX_NONE ||
			(transition->kind != SG_RUNE_PHASE_TRANSITION_TIME &&
			 transition->kind != SG_RUNE_PHASE_TRANSITION_MOVER_DWELL) ||
			!ReferenceIsAbsent(&evidence->portal.value) ||
			!SG_RuneModelStableIdValid(&evidence->mechanism.value) ||
			evidence->mechanism.value.source_set_identity !=
				source->configuration->identity.source_set_identity ||
			evidence->provider_verifier_identity == 0U ||
			evidence->provider_verifier_identity !=
				source->mover_support_verifier_identity ||
			evidence->source_state_mask == 0U ||
			evidence->destination_state_mask == 0U ||
			(evidence->source_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			(evidence->destination_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			evidence->portal_duration_ms != 0U ||
			!FloatBitsEqual(transition->duration_ms.min_value,
				(float)TransitionTimingSpan(evidence)) ||
			!FloatBitsEqual(transition->duration_ms.max_value,
				(float)TransitionTimingSpan(evidence)))
			return 0;
		break;
	case SG_PHASE_CATALOG_TRANSITION_ORIGIN_COUNT:
		return 0;
	}
	return 1;
}

static int NormalizePhaseCatalog(
	const sg_phase_catalog_view_t *view,
	sg_ground_normalized_source_t *source,
	sg_ground_capability_audit_result_t *result)
{
	sg_ground_phase_ref_index_t *phase_index = NULL;
	sg_ground_cell_ref_index_t *cell_index = NULL;
	sg_ground_catalog_binding_key_t *binding_keys = NULL;
	uint32_t *phase_cells = NULL;
	uint8_t *region_seen = NULL;
	uint32_t index;
	int ok = 0;

	if ((view->completion != SG_PHASE_CATALOG_COMPLETE &&
		 view->completion != SG_PHASE_CATALOG_PROVEN_EMPTY) ||
		(view->transition_completion != SG_PHASE_CATALOG_COMPLETE &&
		 view->transition_completion != SG_PHASE_CATALOG_PROVEN_EMPTY) ||
		view->phase_count > SG_RUNE_MODEL_MAX_PHASES ||
		view->binding_count > SG_PHASE_CATALOG_MAX_BINDINGS ||
		view->transition_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		(view->completion == SG_PHASE_CATALOG_PROVEN_EMPTY &&
		 (view->phase_count != 0U || view->binding_count != 0U)) ||
		(view->completion == SG_PHASE_CATALOG_COMPLETE &&
		 (view->phase_count == 0U || view->binding_count == 0U)) ||
		(view->transition_completion == SG_PHASE_CATALOG_PROVEN_EMPTY &&
		 view->transition_count != 0U) ||
		(view->transition_completion == SG_PHASE_CATALOG_COMPLETE &&
		 view->transition_count == 0U) ||
		(view->phase_count != 0U && !view->phases) ||
		(view->binding_count != 0U && !view->bindings) ||
		(view->transition_count != 0U &&
		 (!view->transitions || !view->transition_evidence)))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED,
			0U);
		return 0;
	}
	source->phase_completion = view->completion;
	source->transition_completion = view->transition_completion;
	source->mover_support_verifier_identity =
		view->mover_support_verifier_identity;
	source->phase_count = view->phase_count;
	source->catalog_binding_count = view->binding_count;
	source->phase_transition_count = view->transition_count;
	if (!CopyArray((void **)&source->phases, view->phases, view->phase_count,
			sizeof(*source->phases)) ||
		!CopyArray((void **)&source->catalog_bindings, view->bindings,
			view->binding_count, sizeof(*source->catalog_bindings)) ||
		!CopyArray((void **)&source->phase_transitions, view->transitions,
			view->transition_count, sizeof(*source->phase_transitions)) ||
		!CopyArray((void **)&source->transition_evidence,
			view->transition_evidence, view->transition_count,
			sizeof(*source->transition_evidence)))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U);
		return 0;
	}
	if (view->completion == SG_PHASE_CATALOG_PROVEN_EMPTY)
	{
		if (source->configuration->cell_count != 0U ||
			source->semantics->region_count != 0U)
		{
			SetResult(result,
				SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED, 0U);
			return 0;
		}
		ok = 1;
		goto done;
	}
	if (!AllocationFits(view->phase_count, sizeof(*phase_index)) ||
		!AllocationFits(source->configuration->cell_count,
			sizeof(*cell_index)) ||
		!AllocationFits(view->binding_count, sizeof(*binding_keys)) ||
		!AllocationFits(view->phase_count, sizeof(*phase_cells)) ||
		!AllocationFits(source->semantics->region_count,
			sizeof(*region_seen)))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OVERFLOW, 0U);
		goto done;
	}
	phase_index = malloc((size_t)view->phase_count * sizeof(*phase_index));
	cell_index = malloc((size_t)source->configuration->cell_count *
		sizeof(*cell_index));
	binding_keys = malloc((size_t)view->binding_count *
		sizeof(*binding_keys));
	phase_cells = malloc((size_t)view->phase_count * sizeof(*phase_cells));
	region_seen = calloc((size_t)source->semantics->region_count,
		sizeof(*region_seen));
	source->ground_bindings = malloc((size_t)view->phase_count *
		sizeof(*source->ground_bindings));
	if (!phase_index || !cell_index || !binding_keys || !phase_cells ||
		!region_seen || !source->ground_bindings)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U);
		goto done;
	}
	for (index = 0U; index < view->phase_count; index++)
	{
		if (!SG_RuneModelPhaseValid(&source->phases[index]) ||
			source->phases[index].order.source_set_identity !=
				source->configuration->identity.source_set_identity ||
			(index != 0U && SG_RuneModelOrderKeyCompare(
				&source->phases[index - 1U].order,
				&source->phases[index].order) >= 0))
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED,
				index);
			goto done;
		}
		phase_index[index].reference = source->phases[index].id;
		phase_index[index].index = index;
		phase_cells[index] = SG_GROUND_CAPABILITY_INDEX_NONE;
	}
	qsort(phase_index, view->phase_count, sizeof(*phase_index),
		PhaseRefIndexCompare);
	for (index = 1U; index < view->phase_count; index++)
		if (StableIdCompare(&phase_index[index - 1U].reference.value,
			&phase_index[index].reference.value) == 0)
		{
			SetResult(result,
				SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED,
				phase_index[index].index);
			goto done;
		}
	for (index = 0U; index < source->configuration->cell_count; index++)
	{
		const sg_configuration_cell_t *cell =
			&source->configuration->cells[index];

		if (!RecordIdValid(&cell->id.value, &cell->order,
				SG_RUNE_ORDER_CELL,
				source->configuration->identity.source_set_identity))
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
				index);
			goto done;
		}
		cell_index[index].reference = cell->id;
		cell_index[index].index = index;
	}
	qsort(cell_index, source->configuration->cell_count, sizeof(*cell_index),
		CellRefIndexCompare);
	for (index = 1U; index < source->configuration->cell_count; index++)
		if (StableIdCompare(&cell_index[index - 1U].reference.value,
			&cell_index[index].reference.value) == 0)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
				cell_index[index].index);
			goto done;
		}
	for (index = 0U; index < view->binding_count; index++)
	{
		const sg_phase_catalog_binding_t *binding =
			&source->catalog_bindings[index];
		uint32_t phase;
		uint32_t region;

		if (!FindPhaseIndex(phase_index, view->phase_count, binding->phase,
				&phase) ||
			!FindRegionIndex(source->semantics, binding->semantic_region_id,
				&region) ||
			binding->configuration_cell >=
				source->configuration->cell_count ||
			source->semantics->regions[region].cell !=
				binding->configuration_cell ||
			source->phases[phase].stance != source->configuration->cells[
				binding->configuration_cell].stance ||
			(binding->mechanism_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) != 0U ||
			(SG_RuneModelStableIdValid(&source->phases[phase].mover.value) ?
				binding->mechanism_state_mask == 0U :
				binding->mechanism_state_mask != 0U) ||
			(phase_cells[phase] != SG_GROUND_CAPABILITY_INDEX_NONE &&
			 phase_cells[phase] != binding->configuration_cell))
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_BINDING_REJECTED,
				index);
			goto done;
		}
		phase_cells[phase] = binding->configuration_cell;
		region_seen[region] = 1U;
		binding_keys[index].region = binding->semantic_region_id;
		binding_keys[index].cell = binding->configuration_cell;
		binding_keys[index].phase = binding->phase;
		binding_keys[index].index = index;
		if (index != 0U && source->catalog_bindings[index - 1U].
				semantic_region_id > binding->semantic_region_id)
		{
			SetResult(result,
				SG_GROUND_CAPABILITY_AUDIT_PHASE_BINDING_REJECTED, index);
			goto done;
		}
	}
	qsort(binding_keys, view->binding_count, sizeof(*binding_keys),
		CatalogBindingKeyCompare);
	for (index = 1U; index < view->binding_count; index++)
		if (CatalogBindingKeyCompare(&binding_keys[index - 1U],
			&binding_keys[index]) == 0)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_BINDING_REJECTED,
				binding_keys[index].index);
			goto done;
		}
	for (index = 0U; index < view->phase_count; index++)
	{
		if (phase_cells[index] == SG_GROUND_CAPABILITY_INDEX_NONE ||
			source->phases[index].order.source_index != phase_cells[index] ||
			source->phases[index].time_quantum_ms !=
				source->configuration->identity.physics.substep_ms)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_BINDING_REJECTED,
				index);
			goto done;
		}
		source->ground_bindings[index].cell = phase_cells[index];
		source->ground_bindings[index].phase = index;
	}
	for (index = 0U; index < source->semantics->region_count; index++)
		if (!region_seen[index])
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_BINDING_REJECTED,
				index);
			goto done;
		}
	source->ground_binding_count = view->phase_count;
	qsort(source->ground_bindings, source->ground_binding_count,
		sizeof(*source->ground_bindings), GroundBindingCompare);
	for (index = 0U; index < view->transition_count; index++)
		if (!TransitionValid(source, phase_index, cell_index, phase_cells,
			index))
		{
			SetResult(result,
				SG_GROUND_CAPABILITY_AUDIT_PHASE_TRANSITION_REJECTED, index);
			goto done;
		}
	ok = 1;

done:
	free(phase_index);
	free(cell_index);
	free(binding_keys);
	free(phase_cells);
	free(region_seen);
	if (ok)
	{
		result->phase_count = view->phase_count;
		result->binding_count = view->binding_count;
		result->phase_transition_count = view->transition_count;
	}
	return ok;
}

static int PrepareSource(
	const sg_ground_capability_publication_source_t *input,
	sg_ground_normalized_source_t *source,
	sg_ground_capability_audit_result_t *result)
{
	const sg_phase_catalog_view_t *phase_view = NULL;
	sg_host_law_result_t host_result;

	if (!input || !source || !result || !input->engine_authority ||
		!input->configuration || !input->semantics ||
		!input->phase_catalog_owner || !input->phase_catalog)
		return 0;
	memset(source, 0, sizeof(*source));
	source->engine_authority = input->engine_authority;
	source->configuration = input->configuration;
	source->semantics = input->semantics;
	source->phase_catalog_owner = input->phase_catalog_owner;
	source->phase_catalog = input->phase_catalog;
	host_result = SG_HostLawPublicationRevalidateProduction(
		input->engine_authority);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_ENGINE_AUTHORITY_REJECTED,
			host_result.element);
		return 0;
	}
	host_result = SG_HostLawPublicationRead(input->engine_authority,
		&source->engine_view);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_ENGINE_AUTHORITY_REJECTED,
			host_result.element);
		return 0;
	}
	host_result = SG_HostLawPublicationCollisionAuthority(
		input->engine_authority, &source->collision_authority);
	if (host_result.status != SG_HOST_LAW_OK || !source->collision_authority ||
		!source->collision_authority->world || !EngineViewValid(source))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_ENGINE_AUTHORITY_REJECTED,
			host_result.element);
		return 0;
	}
	if (!IdentityEqual(&source->collision_authority->identity,
			&input->configuration->identity) ||
		!IdentityEqual(&source->collision_authority->identity,
			&input->semantics->identity))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (!AuditAcceptedGeometry(source, result))
		return 0;
	if (!SG_PhaseCatalogPublicationRead(input->phase_catalog_owner,
			input->phase_catalog, &phase_view))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED,
			0U);
		return 0;
	}
	if (!IdentityEqual(&phase_view->identity,
			&source->collision_authority->identity))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (!NormalizePhaseCatalog(phase_view, source, result))
		return 0;
	source->configuration_fingerprint =
		ConfigurationFingerprint(input->configuration);
	source->semantics_fingerprint = SemanticsFingerprint(input->semantics);
	return 1;
}

static int SamePhaseView(const sg_ground_normalized_source_t *source,
	const sg_phase_catalog_view_t *view)
{
	return view && IdentityEqual(&view->identity,
			&source->collision_authority->identity) &&
		view->completion == source->phase_completion &&
		view->transition_completion == source->transition_completion &&
		view->mover_support_verifier_identity ==
			source->mover_support_verifier_identity &&
		view->phase_count == source->phase_count &&
		view->binding_count == source->catalog_binding_count &&
		view->transition_count == source->phase_transition_count &&
		(view->phase_count == 0U ||
		 memcmp(view->phases, source->phases,
			(size_t)view->phase_count * sizeof(*view->phases)) == 0) &&
		(view->binding_count == 0U ||
		 memcmp(view->bindings, source->catalog_bindings,
			(size_t)view->binding_count * sizeof(*view->bindings)) == 0) &&
		(view->transition_count == 0U ||
		 (memcmp(view->transitions, source->phase_transitions,
			(size_t)view->transition_count * sizeof(*view->transitions)) == 0 &&
		  memcmp(view->transition_evidence, source->transition_evidence,
			(size_t)view->transition_count *
				sizeof(*view->transition_evidence)) == 0));
}

static int SourceStillCurrent(const sg_ground_normalized_source_t *source,
	sg_ground_capability_audit_result_t *result)
{
	const sg_phase_catalog_view_t *phase_view = NULL;
	const sg_host_collision_authority_t *collision_authority = NULL;
	sg_host_law_view_t engine_view;
	sg_host_law_result_t host_result;

	host_result = SG_HostLawPublicationRevalidateProduction(
		source->engine_authority);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_ENGINE_AUTHORITY_REJECTED,
			host_result.element);
		return 0;
	}
	host_result = SG_HostLawPublicationRead(source->engine_authority,
		&engine_view);
	if (host_result.status != SG_HOST_LAW_OK ||
		memcmp(&engine_view, &source->engine_view, sizeof(engine_view)) != 0)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SOURCE_MUTATED,
			host_result.element);
		return 0;
	}
	host_result = SG_HostLawPublicationCollisionAuthority(
		source->engine_authority, &collision_authority);
	if (host_result.status != SG_HOST_LAW_OK ||
		collision_authority != source->collision_authority)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SOURCE_MUTATED,
			host_result.element);
		return 0;
	}
	if (ConfigurationFingerprint(source->configuration) !=
			source->configuration_fingerprint ||
		SemanticsFingerprint(source->semantics) !=
			source->semantics_fingerprint)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SOURCE_MUTATED,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (!SG_PhaseCatalogPublicationRead(source->phase_catalog_owner,
			source->phase_catalog, &phase_view) ||
		!SamePhaseView(source, phase_view))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SOURCE_MUTATED,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	return AuditAcceptedGeometry(source, result);
}

static void GroundEnginePmove(pmove_t *pmove)
{
	if (!SG_HostEnginePmove(pmove))
		sg_ground_engine_pmove_failed = 1;
}

static sg_ground_capability_audit_code_t ReconstructionCode(
	sg_ground_capability_error_code_t code)
{
	if (code == SG_GROUND_CAPABILITY_ERROR_OVERFLOW)
		return SG_GROUND_CAPABILITY_AUDIT_OVERFLOW;
	if (code == SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY)
		return SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY;
	return SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED;
}

static int Reconstruct(const sg_ground_capability_publication_source_t *input,
	sg_ground_normalized_source_t *source,
	sg_ground_capability_set_t **expected_out,
	sg_ground_capability_audit_result_t *result)
{
	sg_ground_capability_error_t error;
	sg_ground_reconstruction_source_t reconstruction;
	sg_ground_capability_set_t *expected = NULL;
	uint64_t expected_directions;

	memset(&error, 0, sizeof(error));
	memset(&reconstruction, 0, sizeof(reconstruction));
	if (!expected_out || *expected_out || !PrepareSource(input, source, result))
		return 0;
	if (source->phase_count == 0U)
	{
		expected = calloc(1U, sizeof(*expected));
		if (!expected)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U);
			return 0;
		}
		expected->identity = source->collision_authority->identity;
	}
	else
	{
		if (!ReconstructionSourceBuild(source, &reconstruction, result))
			return 0;
		sg_ground_engine_pmove_failed = 0;
		if (!SG_GroundCapabilityBuild(source->collision_authority,
				&reconstruction.configuration, &reconstruction.semantics,
				source->phases, source->phase_count, reconstruction.bindings,
				source->ground_binding_count, GroundEnginePmove, &expected,
				&error) || sg_ground_engine_pmove_failed)
		{
			ReconstructionSourceDestroy(&reconstruction);
			SG_GroundCapabilityDestroy(expected);
			SetResult(result, ReconstructionCode(error.code),
				error.source_index);
			return 0;
		}
		if (!RemapReconstructedFacts(&reconstruction, expected, result))
		{
			ReconstructionSourceDestroy(&reconstruction);
			SG_GroundCapabilityDestroy(expected);
			return 0;
		}
		ReconstructionSourceDestroy(&reconstruction);
	}
	if (!SourceStillCurrent(source, result))
	{
		SG_GroundCapabilityDestroy(expected);
		return 0;
	}
	expected_directions = (uint64_t)source->configuration->portal_count * 2U;
	if ((uint64_t)expected->proved_portals + expected->rejected_crossings !=
			source->configuration->portal_count ||
		(uint64_t)expected->proved_directions + expected->rejected_directions !=
			expected_directions)
	{
		SG_GroundCapabilityDestroy(expected);
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	*expected_out = expected;
	return 1;
}

static int CapabilityOrderCompare(
	const sg_ground_capability_t *left,
	const sg_ground_capability_t *right)
{
#define SG_GROUND_COMPARE(field) \
	if (left->field != right->field) \
		return left->field < right->field ? -1 : 1
	SG_GROUND_COMPARE(source_cell);
	SG_GROUND_COMPARE(destination_cell);
	SG_GROUND_COMPARE(kind);
	SG_GROUND_COMPARE(portal);
	SG_GROUND_COMPARE(source_phase);
	SG_GROUND_COMPARE(destination_phase);
	SG_GROUND_COMPARE(source_region);
	SG_GROUND_COMPARE(destination_region);
#undef SG_GROUND_COMPARE
	{
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			uint32_t left_bits = FloatBits(left->source_witness.value[axis]);
			uint32_t right_bits = FloatBits(right->source_witness.value[axis]);

			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
			left_bits = FloatBits(left->destination_witness.value[axis]);
			right_bits = FloatBits(right->destination_witness.value[axis]);
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
			left_bits = FloatBits(left->initial_velocity.value[axis]);
			right_bits = FloatBits(right->initial_velocity.value[axis]);
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
			left_bits = FloatBits(left->observed_velocity.value[axis]);
			right_bits = FloatBits(right->observed_velocity.value[axis]);
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
		}
	}
	return 0;
}

static int Vec3Finite(const sg_rune_vec3_t *vector)
{
	return isfinite(vector->value[0]) && isfinite(vector->value[1]) &&
		isfinite(vector->value[2]);
}

static int IntervalValid(const sg_rune_interval_t *interval)
{
	return isfinite(interval->min_value) && isfinite(interval->max_value) &&
		interval->min_value <= interval->max_value;
}

static int Interval3Valid(const sg_rune_interval3_t *interval)
{
	return IntervalValid(&interval->x) && IntervalValid(&interval->y) &&
		IntervalValid(&interval->z);
}

static int CandidateFactValid(
	const sg_ground_normalized_source_t *source,
	const sg_ground_capability_t *fact)
{
	const sg_ground_capability_flags_t known_flags =
		SG_GROUND_CAPABILITY_DIRECTIONAL |
		SG_GROUND_CAPABILITY_REQUIRES_SUPPORT |
		SG_GROUND_CAPABILITY_CHANGES_STANCE |
		SG_GROUND_CAPABILITY_CHANGES_SUPPORT |
		SG_GROUND_CAPABILITY_VOID_ADJACENT |
		SG_GROUND_CAPABILITY_PROVEN;

	return fact->source_cell < source->configuration->cell_count &&
		fact->destination_cell < source->configuration->cell_count &&
		fact->source_region < source->semantics->region_count &&
		fact->destination_region < source->semantics->region_count &&
		(fact->portal == SG_GROUND_CAPABILITY_INDEX_NONE ||
		 fact->portal < source->configuration->portal_count) &&
		fact->source_phase < source->phase_count &&
		fact->destination_phase < source->phase_count &&
		fact->kind >= SG_GROUND_CAPABILITY_WALK &&
		fact->kind < SG_GROUND_CAPABILITY_KIND_COUNT &&
		Vec3Finite(&fact->source_witness) &&
		Vec3Finite(&fact->destination_witness) &&
		Vec3Finite(&fact->initial_velocity) &&
		Vec3Finite(&fact->observed_velocity) &&
		Interval3Valid(&fact->displacement) &&
		IntervalValid(&fact->duration_ms) &&
		isfinite(fact->acceleration) && isfinite(fact->gravity) &&
		FloatBitsEqual(fact->gravity,
			source->configuration->identity.physics.gravity) &&
		fact->physics_abi_id ==
			source->configuration->identity.physics_abi_id &&
		(fact->flags & ~known_flags) == 0U &&
		(fact->flags & SG_GROUND_CAPABILITY_PROVEN) != 0U;
}

static int CompletenessEqual(const sg_ground_capability_set_t *left,
	const sg_ground_capability_set_t *right)
{
	return left->proved_portals == right->proved_portals &&
		left->rejected_crossings == right->rejected_crossings &&
		left->proved_directions == right->proved_directions &&
		left->rejected_directions == right->rejected_directions &&
		left->pmove_frames == right->pmove_frames;
}

static uint64_t CandidateFingerprint(const sg_ground_capability_set_t *set)
{
	uint64_t hash = UINT64_C(1469598103934665603);

	hash = HashBytes(hash, set, sizeof(*set));
	return HashBytes(hash, set->capabilities,
		(size_t)set->capability_count * sizeof(*set->capabilities));
}

static void PopulateExpectedResult(
	const sg_ground_normalized_source_t *source,
	const sg_ground_capability_set_t *expected,
	sg_ground_capability_audit_result_t *result)
{
	uint32_t index;

	result->expected_facts = expected->capability_count;
	result->phase_count = source->phase_count;
	result->binding_count = source->catalog_binding_count;
	result->phase_transition_count = source->phase_transition_count;
	result->proved_portals = expected->proved_portals;
	result->proven_empty_portals = expected->rejected_crossings;
	result->proved_directions = expected->proved_directions;
	result->proven_empty_directions = expected->rejected_directions;
	result->host_pmove_frames = expected->pmove_frames;
	for (index = 0U; index < expected->capability_count; index++)
		result->expected_by_kind[expected->capabilities[index].kind]++;
}

static int AuditCandidate(
	const sg_ground_normalized_source_t *source,
	const sg_ground_capability_set_t *expected,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_audit_result_t *result)
{
	uint64_t fingerprint;
	uint32_t expected_index = 0U;
	uint32_t candidate_index = 0U;
	uint32_t index;

	if (!candidate ||
		(candidate->capability_count != 0U && !candidate->capabilities))
		return 0;
	if (!IdentityEqual(&candidate->identity,
			&source->configuration->identity))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	result->candidate_facts = candidate->capability_count;
	fingerprint = CandidateFingerprint(candidate);
	for (index = 0U; index < candidate->capability_count; index++)
	{
		int order;

		if (!CandidateFactValid(source, &candidate->capabilities[index]))
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_INVALID_FACT, index);
			return 0;
		}
		result->candidate_by_kind[candidate->capabilities[index].kind]++;
		if (index == 0U)
			continue;
		order = CapabilityOrderCompare(&candidate->capabilities[index - 1U],
			&candidate->capabilities[index]);
		if (order == 0)
		{
			result->duplicate_facts++;
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_DUPLICATE_FACT, index);
			return 0;
		}
		if (order > 0)
		{
			SetResult(result, SG_GROUND_CAPABILITY_AUDIT_NONCANONICAL_ORDER,
				index);
			return 0;
		}
	}
	while (expected_index < expected->capability_count &&
		candidate_index < candidate->capability_count)
	{
		int order = CapabilityOrderCompare(
			&expected->capabilities[expected_index],
			&candidate->capabilities[candidate_index]);

		if (order < 0)
		{
			result->omitted_facts++;
			expected_index++;
		}
		else if (order > 0)
		{
			result->invented_facts++;
			candidate_index++;
		}
		else
		{
			if (!SG_GroundCapabilityFactBitsEqual(
					&expected->capabilities[expected_index],
					&candidate->capabilities[candidate_index]))
			{
				SetResult(result,
					SG_GROUND_CAPABILITY_AUDIT_FACT_DISAGREEMENT,
					candidate_index);
				return 0;
			}
			result->matched_facts++;
			expected_index++;
			candidate_index++;
		}
	}
	result->omitted_facts += expected->capability_count - expected_index;
	result->invented_facts += candidate->capability_count - candidate_index;
	if (result->omitted_facts != 0U)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OMITTED_FACT,
			candidate_index);
		return 0;
	}
	if (result->invented_facts != 0U)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_INVENTED_FACT,
			candidate_index);
		return 0;
	}
	if (!CompletenessEqual(expected, candidate))
	{
		SetResult(result,
			SG_GROUND_CAPABILITY_AUDIT_COMPLETENESS_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (fingerprint != CandidateFingerprint(candidate))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SOURCE_MUTATED,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	result->completeness = expected->capability_count == 0U ?
		SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY :
		SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE;
	SetResult(result, SG_GROUND_CAPABILITY_AUDIT_OK,
		SG_GROUND_CAPABILITY_INDEX_NONE);
	return 1;
}

static int AuditOwned(
	const sg_ground_capability_publication_source_t *input,
	const sg_ground_capability_set_t *candidate,
	sg_ground_normalized_source_t *source_out,
	sg_ground_capability_set_t **expected_out,
	sg_ground_capability_audit_result_t *result_out)
{
	sg_ground_capability_audit_result_t result;
	sg_ground_normalized_source_t source;
	sg_ground_capability_set_t *expected = NULL;
	int ok = 0;

	memset(&result, 0, sizeof(result));
	memset(&source, 0, sizeof(source));
	result.code = SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT;
	result.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
	result.record = SG_GROUND_CAPABILITY_INDEX_NONE;
	if (!result_out || !source_out || !expected_out || *expected_out ||
		!input || !candidate ||
		(candidate->capability_count != 0U && !candidate->capabilities))
		goto done;
	if (!Reconstruct(input, &source, &expected, &result))
		goto done;
	PopulateExpectedResult(&source, expected, &result);
	if (!AuditCandidate(&source, expected, candidate, &result))
		goto done;
	*source_out = source;
	memset(&source, 0, sizeof(source));
	*expected_out = expected;
	expected = NULL;
	ok = 1;

done:
	NormalizedSourceDestroy(&source);
	SG_GroundCapabilityDestroy(expected);
	if (result_out)
		*result_out = result;
	return ok;
}

int SG_GroundCapabilityAudit(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_audit_result_t *result_out)
{
	sg_ground_normalized_source_t normalized;
	sg_ground_capability_set_t *expected = NULL;
	int ok;

	memset(&normalized, 0, sizeof(normalized));
	ok = AuditOwned(source, candidate, &normalized, &expected, result_out);
	NormalizedSourceDestroy(&normalized);
	SG_GroundCapabilityDestroy(expected);
	return ok;
}

static void NormalizeFact(const sg_ground_normalized_source_t *source,
	const sg_ground_capability_t *input, uint32_t order,
	sg_ground_capability_publication_fact_t *output)
{
	memset(output, 0, sizeof(*output));
	output->order = order;
	output->source_cell = source->configuration->cells[input->source_cell].id;
	output->destination_cell =
		source->configuration->cells[input->destination_cell].id;
	output->source_region_id =
		source->semantics->regions[input->source_region].id;
	output->destination_region_id =
		source->semantics->regions[input->destination_region].id;
	output->portal = input->portal == SG_GROUND_CAPABILITY_INDEX_NONE ?
		SG_RUNE_PORTAL_REF_NONE :
		source->configuration->portals[input->portal].id;
	output->source_phase = source->phases[input->source_phase].id;
	output->destination_phase = source->phases[input->destination_phase].id;
	output->kind = input->kind;
	output->source_witness = input->source_witness;
	output->destination_witness = input->destination_witness;
	output->initial_velocity = input->initial_velocity;
	output->observed_velocity = input->observed_velocity;
	output->displacement = input->displacement;
	output->duration_ms = input->duration_ms;
	output->acceleration = input->acceleration;
	output->gravity = input->gravity;
	output->physics_abi_id = input->physics_abi_id;
	output->flags = input->flags;
}

static uint64_t PayloadDigest(
	const sg_ground_publication_payload_t *payload)
{
	uint64_t hash = UINT64_C(1469598103934665603);

	hash = HashBytes(hash, &payload->description,
		sizeof(payload->description));
	return HashBytes(hash, payload->facts,
		(size_t)payload->description.fact_count * sizeof(*payload->facts));
}

static int PublicationReferenceValid(const sg_rune_stable_id_t *reference,
	uint64_t source_set_identity, int allow_none)
{
	return (allow_none && ReferenceIsNone(reference)) ||
		(SG_RuneModelStableIdValid(reference) &&
		 reference->source_set_identity == source_set_identity);
}

static int PayloadValid(const sg_ground_publication_payload_t *payload)
{
	const sg_ground_capability_publication_description_t *description;
	const sg_ground_capability_flags_t known_fact_flags =
		SG_GROUND_CAPABILITY_DIRECTIONAL |
		SG_GROUND_CAPABILITY_REQUIRES_SUPPORT |
		SG_GROUND_CAPABILITY_CHANGES_STANCE |
		SG_GROUND_CAPABILITY_CHANGES_SUPPORT |
		SG_GROUND_CAPABILITY_VOID_ADJACENT |
		SG_GROUND_CAPABILITY_PROVEN;
	uint64_t portal_coverage;
	uint64_t direction_coverage;
	uint32_t by_kind[SG_GROUND_CAPABILITY_KIND_COUNT] = { 0U };
	uint32_t index;

	if (!payload)
		return 0;
	description = &payload->description;
	if (!AllocationFits(description->fact_count, sizeof(*payload->facts)) ||
		(description->fact_count == 0U ? payload->facts != NULL :
		 payload->facts == NULL) ||
		(description->completeness !=
			SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE &&
		 description->completeness !=
			SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY) ||
		((description->fact_count == 0U) !=
		 (description->completeness ==
			SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY)) ||
		description->engine_authority_version !=
			SG_HOST_LAW_PUBLICATION_VERSION || description->reserved != 0U ||
		description->collision_law_id == 0U ||
		description->pmove_law_id == 0U ||
		description->gravity_law_id == 0U ||
		description->collision_law_id == description->pmove_law_id ||
		description->collision_law_id == description->gravity_law_id ||
		description->pmove_law_id == description->gravity_law_id ||
		description->pmove_abi.version != SG_HOST_ENGINE_PMOVE_ABI_VERSION ||
		description->pmove_abi.game_api_version == 0U ||
		description->pmove_abi.import_size == 0U ||
		description->pmove_abi.pmove_offset >=
			description->pmove_abi.import_size ||
		description->pmove_abi.pmove_size != sizeof(pmove_t) ||
		description->pmove_abi.state_size != sizeof(pmove_state_t) ||
		description->pmove_abi.command_size != sizeof(usercmd_t) ||
		description->pmove_abi.fraction_bits !=
			SG_HOST_ENGINE_PMOVE_FRACTION_BITS ||
		description->pmove_abi.substep_ms !=
			SG_HOST_ENGINE_PMOVE_SUBSTEP_MS ||
		description->pmove_abi.identity == 0U ||
		description->pmove_behavior_fingerprint !=
			description->pmove_abi.identity ||
		description->pmove_abi.identity !=
			description->identity.physics_abi_id ||
		description->cell_count > SG_RUNE_MODEL_MAX_CELLS ||
		description->portal_count > SG_RUNE_MODEL_MAX_PORTALS ||
		description->phase_count > SG_RUNE_MODEL_MAX_PHASES ||
		description->binding_count > SG_PHASE_CATALOG_MAX_BINDINGS ||
		description->phase_transition_count >
			SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		((description->cell_count == 0U) !=
		 (description->phase_count == 0U)) ||
		(description->phase_count == 0U &&
		 (description->binding_count != 0U ||
		  description->phase_transition_count != 0U)) ||
		description->binding_count < description->phase_count ||
		description->binding_count < description->semantic_region_count ||
		(description->fact_count != 0U && description->phase_count == 0U))
		return 0;
	portal_coverage = (uint64_t)description->proved_portals +
		description->proven_empty_portals;
	direction_coverage = (uint64_t)description->proved_directions +
		description->proven_empty_directions;
	if (portal_coverage != description->portal_count ||
		direction_coverage != (uint64_t)description->portal_count * 2U)
		return 0;
	for (index = 0U; index < description->fact_count; index++)
	{
		const sg_ground_capability_publication_fact_t *fact =
			&payload->facts[index];

		if (fact->order != index ||
			fact->kind < SG_GROUND_CAPABILITY_WALK ||
			fact->kind >= SG_GROUND_CAPABILITY_KIND_COUNT ||
			!PublicationReferenceValid(&fact->source_cell.value,
				description->identity.source_set_identity, 0) ||
			!PublicationReferenceValid(&fact->destination_cell.value,
				description->identity.source_set_identity, 0) ||
			!PublicationReferenceValid(&fact->portal.value,
				description->identity.source_set_identity, 1) ||
			!PublicationReferenceValid(&fact->source_phase.value,
				description->identity.source_set_identity, 0) ||
			!PublicationReferenceValid(&fact->destination_phase.value,
				description->identity.source_set_identity, 0) ||
			!Vec3Finite(&fact->source_witness) ||
			!Vec3Finite(&fact->destination_witness) ||
			!Vec3Finite(&fact->initial_velocity) ||
			!Vec3Finite(&fact->observed_velocity) ||
			!Interval3Valid(&fact->displacement) ||
			!IntervalValid(&fact->duration_ms) ||
			!isfinite(fact->acceleration) || !isfinite(fact->gravity) ||
			!FloatBitsEqual(fact->gravity,
				description->identity.physics.gravity) ||
			fact->physics_abi_id != description->identity.physics_abi_id ||
			(fact->flags & ~known_fact_flags) != 0U ||
			(fact->flags & SG_GROUND_CAPABILITY_PROVEN) == 0U)
			return 0;
		by_kind[fact->kind]++;
	}
	for (index = 0U; index < SG_GROUND_CAPABILITY_KIND_COUNT; index++)
		if (by_kind[index] != description->fact_count_by_kind[index])
			return 0;
	return payload->digest == PayloadDigest(payload);
}

static int RangeEnd(const void *address, size_t size, uintptr_t *end_out)
{
	uintptr_t start;

	if (!address || size == 0U || !end_out)
		return 0;
	start = (uintptr_t)address;
	if (size > UINTPTR_MAX - start)
		return 0;
	*end_out = start + (uintptr_t)size;
	return *end_out > start;
}

static int RangesOverlap(const void *left_address, size_t left_size,
	const void *right_address, size_t right_size)
{
	uintptr_t left_end;
	uintptr_t right_end;
	uintptr_t left_start;
	uintptr_t right_start;

	if (!RangeEnd(left_address, left_size, &left_end) ||
		!RangeEnd(right_address, right_size, &right_end))
		return 0;
	left_start = (uintptr_t)left_address;
	right_start = (uintptr_t)right_address;
	return left_start < right_end && right_start < left_end;
}

static sg_ground_publication_record_t *PublicationRecord(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication)
{
	sg_ground_publication_record_t *record;

	if (!owner || !publication)
		return NULL;
	for (record = owner->live; record; record = record->next)
		if (record->token == publication)
			return record;
	return NULL;
}

static void ReleaseRecord(sg_ground_publication_record_t *record)
{
	if (!record)
		return;
	if (record->payload)
		free(record->payload->facts);
	free(record->payload);
	free(record);
}

int SG_GroundCapabilityPublicationOwnerCreate(
	sg_ground_capability_publication_owner_t **owner_out)
{
	if (!owner_out || *owner_out)
		return 0;
	*owner_out = calloc(1U, sizeof(**owner_out));
	return *owner_out != NULL;
}

void SG_GroundCapabilityPublicationOwnerDestroy(
	sg_ground_capability_publication_owner_t *owner)
{
	sg_ground_publication_record_t *record;

	if (!owner)
		return;
	while (owner->live)
	{
		record = owner->live;
		owner->live = record->next;
		ReleaseRecord(record);
	}
	free(owner);
}

int SG_GroundCapabilityPublicationIssue(
	sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_source_t *input,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_publication_t **publication_out,
	sg_ground_capability_audit_result_t *audit_out)
{
	sg_ground_capability_audit_result_t audit;
	sg_ground_normalized_source_t source;
	sg_ground_capability_set_t *expected = NULL;
	sg_ground_publication_record_t *record = NULL;
	sg_ground_publication_payload_t *payload = NULL;
	uintptr_t token;
	uint32_t index;
	int ok = 0;

	memset(&audit, 0, sizeof(audit));
	memset(&source, 0, sizeof(source));
	audit.code = SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT;
	audit.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
	audit.record = SG_GROUND_CAPABILITY_INDEX_NONE;
	if (!audit_out || !owner || !publication_out || *publication_out ||
		owner->live_count == UINT32_MAX)
		goto done;
	*publication_out = NULL;
	if (!AuditOwned(input, candidate, &source, &expected, &audit))
		goto done;
	record = calloc(1U, sizeof(*record));
	payload = calloc(1U, sizeof(*payload));
	if (!record || !payload ||
		!AllocationFits(expected->capability_count, sizeof(*payload->facts)))
	{
		SetResult(&audit, !record || !payload ?
			SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY :
			SG_GROUND_CAPABILITY_AUDIT_OVERFLOW, 0U);
		goto done;
	}
	record->payload = payload;
	payload = NULL;
	if (expected->capability_count != 0U)
	{
		record->payload->facts = calloc((size_t)expected->capability_count,
			sizeof(*record->payload->facts));
		if (!record->payload->facts)
		{
			SetResult(&audit, SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U);
			goto done;
		}
	}
	record->payload->description.identity =
		source.collision_authority->identity;
	record->payload->description.engine_authority_version =
		source.engine_view.version;
	record->payload->description.collision_law_id =
		source.engine_view.collision_law_id;
	record->payload->description.pmove_law_id = source.engine_view.pmove_law_id;
	record->payload->description.gravity_law_id =
		source.engine_view.gravity_law_id;
	record->payload->description.pmove_abi = source.engine_view.pmove_abi;
	record->payload->description.pmove_behavior_fingerprint =
		source.engine_view.pmove_behavior_fingerprint;
	record->payload->description.completeness = audit.completeness;
	record->payload->description.cell_count = source.configuration->cell_count;
	record->payload->description.portal_count =
		source.configuration->portal_count;
	record->payload->description.semantic_region_count =
		source.semantics->region_count;
	record->payload->description.phase_count = source.phase_count;
	record->payload->description.binding_count = source.catalog_binding_count;
	record->payload->description.phase_transition_count =
		source.phase_transition_count;
	record->payload->description.fact_count = expected->capability_count;
	record->payload->description.proved_portals = audit.proved_portals;
	record->payload->description.proven_empty_portals =
		audit.proven_empty_portals;
	record->payload->description.proved_directions = audit.proved_directions;
	record->payload->description.proven_empty_directions =
		audit.proven_empty_directions;
	record->payload->description.host_pmove_frames = audit.host_pmove_frames;
	for (index = 0U; index < expected->capability_count; index++)
	{
		NormalizeFact(&source, &expected->capabilities[index], index,
			&record->payload->facts[index]);
		record->payload->description.fact_count_by_kind[
			expected->capabilities[index].kind]++;
	}
	record->payload->digest = PayloadDigest(record->payload);
	if (!SourceStillCurrent(&source, &audit))
		goto done;
	if (!PayloadValid(record->payload))
	{
		SetResult(&audit, SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED,
			0U);
		goto done;
	}
	if (!SG_AuthorityTokenMint(&token))
	{
		SetResult(&audit, SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U);
		goto done;
	}
	record->token = (sg_ground_capability_publication_t *)(uintptr_t)token;
	record->next = owner->live;
	owner->live = record;
	owner->live_count++;
	*publication_out = record->token;
	record = NULL;
	ok = 1;

done:
	if (record)
		ReleaseRecord(record);
	free(payload);
	NormalizedSourceDestroy(&source);
	SG_GroundCapabilityDestroy(expected);
	if (audit_out)
		*audit_out = audit;
	return ok;
}

int SG_GroundCapabilityPublicationBuild(
	sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_source_t *source,
	sg_ground_capability_publication_t **publication_out,
	sg_ground_capability_audit_result_t *audit_out)
{
	sg_ground_capability_audit_result_t audit;
	sg_ground_normalized_source_t normalized;
	sg_ground_capability_set_t *candidate = NULL;
	int ok = 0;

	memset(&audit, 0, sizeof(audit));
	memset(&normalized, 0, sizeof(normalized));
	audit.code = SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT;
	audit.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
	audit.record = SG_GROUND_CAPABILITY_INDEX_NONE;
	if (!audit_out || !owner || !source || !publication_out ||
		*publication_out)
		goto done;
	if (!Reconstruct(source, &normalized, &candidate, &audit))
		goto done;
	NormalizedSourceDestroy(&normalized);
	ok = SG_GroundCapabilityPublicationIssue(owner, source, candidate,
		publication_out, &audit);

done:
	NormalizedSourceDestroy(&normalized);
	SG_GroundCapabilityDestroy(candidate);
	if (audit_out)
		*audit_out = audit;
	return ok;
}

int SG_GroundCapabilityPublicationDescribe(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication,
	sg_ground_capability_publication_description_t *description_out)
{
	sg_ground_publication_record_t *record = PublicationRecord(owner,
		publication);

	if (!description_out || !record || !PayloadValid(record->payload))
		return 0;
	*description_out = record->payload->description;
	return 1;
}

int SG_GroundCapabilityPublicationFact(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication, uint32_t index,
	sg_ground_capability_publication_fact_t *fact_out)
{
	sg_ground_publication_record_t *record = PublicationRecord(owner,
		publication);

	if (!fact_out || !record || !PayloadValid(record->payload) ||
		index >= record->payload->description.fact_count)
		return 0;
	*fact_out = record->payload->facts[index];
	return 1;
}

int SG_GroundCapabilityPublicationStorageOverlaps(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication,
	const void *address, size_t size)
{
	sg_ground_publication_record_t *record = PublicationRecord(owner,
		publication);

	if (!record || !PayloadValid(record->payload) || !address || size == 0U)
		return 0;
	return RangesOverlap(address, size, record->payload,
			sizeof(*record->payload)) ||
		RangesOverlap(address, size, record->payload->facts,
			(size_t)record->payload->description.fact_count *
				sizeof(*record->payload->facts));
}

void SG_GroundCapabilityPublicationDestroy(
	sg_ground_capability_publication_owner_t *owner,
	sg_ground_capability_publication_t *publication)
{
	sg_ground_publication_record_t **link;
	sg_ground_publication_record_t *record;

	if (!owner || !publication)
		return;
	for (link = &owner->live; *link; link = &(*link)->next)
		if ((*link)->token == publication)
		{
			record = *link;
			*link = record->next;
			owner->live_count--;
			ReleaseRecord(record);
			return;
		}
}

const char *SG_GroundCapabilityAuditCodeString(
	sg_ground_capability_audit_code_t code)
{
	static const char *const names[] = {
		"ok", "invalid argument", "identity mismatch",
		"engine authority rejected", "configuration rejected",
		"semantics rejected", "phase catalog rejected",
		"phase binding rejected", "phase transition rejected",
		"reconstruction rejected", "invalid fact", "duplicate fact",
		"noncanonical order", "omitted fact", "invented fact",
		"fact disagreement", "completeness disagreement",
		"source mutated", "overflow", "out of memory"
	};

	if (code < SG_GROUND_CAPABILITY_AUDIT_OK ||
		(size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}
