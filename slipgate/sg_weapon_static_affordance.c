#include "sg_weapon_static_affordance.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SG_WEAPON_SURFACE_EPSILON 0.03125f

typedef struct sg_weapon_static_cell_binding_s
{
	sg_rune_stable_id_t id;
	uint32_t model_cell;
	uint32_t configuration_cell;
	uint32_t first_partition;
	uint32_t partition_count;
	uint32_t partition_root;
} sg_weapon_static_cell_binding_t;

typedef struct sg_weapon_static_partition_ref_s
{
	uint32_t partition;
	sg_rune_bounds_t bounds;
} sg_weapon_static_partition_ref_t;

typedef struct sg_weapon_static_partition_node_s
{
	sg_rune_bounds_t bounds;
	uint32_t left;
	uint32_t right;
	uint32_t partition;
} sg_weapon_static_partition_node_t;

typedef struct sg_weapon_static_surface_ref_s
{
	uint32_t surface;
	sg_rune_bounds_t bounds;
} sg_weapon_static_surface_ref_t;

typedef struct sg_weapon_static_configuration_ref_s
{
	sg_rune_stable_id_t id;
	uint32_t configuration_cell;
} sg_weapon_static_configuration_ref_t;

typedef struct sg_weapon_static_bvh_node_s
{
	sg_rune_bounds_t bounds;
	uint32_t left;
	uint32_t right;
	uint32_t surface;
} sg_weapon_static_bvh_node_t;

struct sg_weapon_static_context_s
{
	sg_weapon_static_binding_t binding;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;
	const sg_rune_model_t *model;
	/* Provisional bridge lifetime/authenticity state only. */
	const sg_rune_v2_artifact_loader_t *artifact_loader;
	const sg_rune_v2_artifact_snapshot_t *artifact_snapshot;
	const sg_static_visibility_publication_t *visibility_publication;
	sg_weapon_static_cell_binding_t *cells;
	uint32_t cell_count;
	uint32_t *partition_indices;
	uint32_t partition_count;
	sg_weapon_static_partition_ref_t *partition_refs;
	sg_weapon_static_partition_node_t *partition_nodes;
	uint32_t partition_node_count;
	sg_weapon_static_surface_ref_t *surface_refs;
	sg_weapon_static_bvh_node_t *bvh_nodes;
	uint32_t bvh_node_count;
	uint32_t bvh_root;
	uint64_t binding_comparisons;
	uint64_t partition_preparation_work;
	uint64_t surface_preparation_work;
};

static const sg_weapon_static_relation_t relation_order[] = {
	SG_WEAPON_STATIC_DIRECT_VISIBILITY,
	SG_WEAPON_STATIC_PROJECTILE_CORRIDOR,
	SG_WEAPON_STATIC_IMPACT_SURFACE,
	SG_WEAPON_STATIC_BLAST_REACH,
	SG_WEAPON_STATIC_BOUNCE_SURFACE,
	SG_WEAPON_STATIC_SECONDARY_BLAST_REACH,
	SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY
};

_Static_assert(sizeof(relation_order) / sizeof(relation_order[0]) ==
	SG_WEAPON_STATIC_RELATION_COUNT, "relation table must cover every result");

static void SetError(sg_weapon_static_affordance_error_t *error,
	sg_weapon_static_affordance_error_code_t code,
	const sg_static_visibility_error_t *visibility)
{
	if (!error)
		return;
	memset(error, 0, sizeof(*error));
	error->code = code;
	if (visibility)
		error->visibility = *visibility;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left->bsp_content_id == right->bsp_content_id &&
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

static int BindingEqual(const sg_weapon_static_binding_t *left,
	const sg_weapon_static_binding_t *right)
{
	return SG_RuneV2ContentIdEqual(&left->artifact_identity,
			&right->artifact_identity) &&
		SG_RuneV2ContentIdEqual(&left->bsp_identity,
			&right->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&left->schema_identity,
			&right->schema_identity) &&
		left->source_set_identity == right->source_set_identity &&
		left->visibility_revision == right->visibility_revision;
}

static int PreparedQueryValid(const sg_weapon_static_query_t *query)
{
	sg_weapon_static_query_input_t input;
	sg_weapon_static_query_t prepared;

	if (!query || query->exact_live_prefire_trace_required != 1U)
		return 0;
	memset(&input, 0, sizeof(input));
	input.binding = query->binding;
	input.source_cell = query->source_cell;
	input.target_cell = query->target_cell;
	input.source_phase = query->source_phase;
	input.target_phase = query->target_phase;
	input.source_origin = query->source_origin;
	input.target_origin = query->target_origin;
	input.target_bounds = query->target_bounds;
	input.requested_relations = query->requested_relations;
	if (!SG_WeaponStaticQueryPrepare(&input, &prepared))
		return 0;
	return query->target_origin.value[0] >=
			query->target_bounds.mins.value[0] &&
		query->target_origin.value[0] <= query->target_bounds.maxs.value[0] &&
		query->target_origin.value[1] >=
			query->target_bounds.mins.value[1] &&
		query->target_origin.value[1] <= query->target_bounds.maxs.value[1] &&
		query->target_origin.value[2] >=
			query->target_bounds.mins.value[2] &&
		query->target_origin.value[2] <= query->target_bounds.maxs.value[2];
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

static int ConfigurationRefLess(
	const sg_weapon_static_configuration_ref_t *left,
	const sg_weapon_static_configuration_ref_t *right,
	uint64_t *comparisons)
{
	int comparison;

	(*comparisons)++;
	comparison = StableIdCompare(&left->id, &right->id);
	if (comparison != 0)
		return comparison < 0;
	return left->configuration_cell < right->configuration_cell;
}

static void ConfigurationRefSift(
	sg_weapon_static_configuration_ref_t *items, uint32_t count,
	uint32_t root, uint64_t *comparisons)
{
	for (;;)
	{
		uint32_t child = root * 2U + 1U;
		uint32_t selected = root;
		sg_weapon_static_configuration_ref_t swap;

		if (child < count && ConfigurationRefLess(&items[selected],
			&items[child], comparisons))
			selected = child;
		if (child + 1U < count && ConfigurationRefLess(&items[selected],
			&items[child + 1U], comparisons))
			selected = child + 1U;
		if (selected == root)
			return;
		swap = items[root];
		items[root] = items[selected];
		items[selected] = swap;
		root = selected;
	}
}

static void ConfigurationRefSort(
	sg_weapon_static_configuration_ref_t *items, uint32_t count,
	uint64_t *comparisons)
{
	uint32_t index;

	for (index = count / 2U; index > 0U; index--)
		ConfigurationRefSift(items, count, index - 1U, comparisons);
	for (index = count; index > 1U; index--)
	{
		sg_weapon_static_configuration_ref_t swap = items[0];

		items[0] = items[index - 1U];
		items[index - 1U] = swap;
		ConfigurationRefSift(items, index - 1U, 0U, comparisons);
	}
}

static int FindConfigurationRef(
	const sg_weapon_static_configuration_ref_t *items, uint32_t count,
	const sg_rune_stable_id_t *id, uint64_t *comparisons,
	uint32_t *configuration_cell_out)
{
	uint32_t low = 0U, high = count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison;

		(*comparisons)++;
		comparison = StableIdCompare(&items[middle].id, id);
		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= count)
		return 0;
	(*comparisons)++;
	if (StableIdCompare(&items[low].id, id) != 0)
		return 0;
	*configuration_cell_out = items[low].configuration_cell;
	return 1;
}

static int ConfigurationModelCellEqual(
	const sg_configuration_cell_t *configuration,
	const sg_rune_cell_t *model)
{
	return configuration->bsp_leaf.index == model->bsp_leaf.index &&
		configuration->bsp_area.index == model->bsp_area.index &&
		configuration->bsp_cluster.index == model->bsp_cluster.index &&
		configuration->contents == model->contents &&
		memcmp(&configuration->bounds, &model->bounds,
			sizeof(configuration->bounds)) == 0;
}

static int FindModelPhase(const sg_rune_model_t *model,
	const sg_rune_phase_ref_t *reference, uint32_t *index_out)
{
	uint32_t low = 0U;
	uint32_t high = model->phase_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&model->phases[middle].id.value,
			&reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->phase_count ||
		StableIdCompare(&model->phases[low].id.value, &reference->value) != 0)
		return 0;
	*index_out = low;
	return 1;
}

static int BoundsCenterLess(const sg_rune_bounds_t *left,
	const sg_rune_bounds_t *right, uint32_t left_index,
	uint32_t right_index)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		double left_center = (double)left->mins.value[axis] +
			left->maxs.value[axis];
		double right_center = (double)right->mins.value[axis] +
			right->maxs.value[axis];

		if (left_center != right_center)
			return left_center < right_center;
	}
	return left_index < right_index;
}

static int SurfaceRefLess(const sg_weapon_static_surface_ref_t *left,
	const sg_weapon_static_surface_ref_t *right, uint64_t *work)
{
	(*work)++;
	return BoundsCenterLess(&left->bounds, &right->bounds, left->surface,
		right->surface);
}

static void SurfaceRefSift(sg_weapon_static_surface_ref_t *items,
	uint32_t count, uint32_t root, uint64_t *work)
{
	for (;;)
	{
		uint32_t child = root * 2U + 1U;
		uint32_t selected = root;
		sg_weapon_static_surface_ref_t swap;

		if (child < count && SurfaceRefLess(&items[selected], &items[child],
			work))
			selected = child;
		if (child + 1U < count &&
			SurfaceRefLess(&items[selected], &items[child + 1U], work))
			selected = child + 1U;
		if (selected == root)
			return;
		swap = items[root];
		items[root] = items[selected];
		items[selected] = swap;
		root = selected;
	}
}

static void SurfaceRefSort(sg_weapon_static_surface_ref_t *items,
	uint32_t count, uint64_t *work)
{
	uint32_t index;

	for (index = count / 2U; index > 0U; index--)
		SurfaceRefSift(items, count, index - 1U, work);
	for (index = count; index > 1U; index--)
	{
		sg_weapon_static_surface_ref_t swap = items[0];

		items[0] = items[index - 1U];
		items[index - 1U] = swap;
		SurfaceRefSift(items, index - 1U, 0U, work);
	}
}

static int PartitionRefLess(const sg_weapon_static_partition_ref_t *left,
	const sg_weapon_static_partition_ref_t *right, uint64_t *work)
{
	(*work)++;
	return BoundsCenterLess(&left->bounds, &right->bounds, left->partition,
		right->partition);
}

static void PartitionRefSift(sg_weapon_static_partition_ref_t *items,
	uint32_t count, uint32_t root, uint64_t *work)
{
	for (;;)
	{
		uint32_t child = root * 2U + 1U;
		uint32_t selected = root;
		sg_weapon_static_partition_ref_t swap;

		if (child < count && PartitionRefLess(&items[selected], &items[child],
			work))
			selected = child;
		if (child + 1U < count && PartitionRefLess(&items[selected],
			&items[child + 1U], work))
			selected = child + 1U;
		if (selected == root)
			return;
		swap = items[root];
		items[root] = items[selected];
		items[selected] = swap;
		root = selected;
	}
}

static void PartitionRefSort(sg_weapon_static_partition_ref_t *items,
	uint32_t count, uint64_t *work)
{
	uint32_t index;

	for (index = count / 2U; index > 0U; index--)
		PartitionRefSift(items, count, index - 1U, work);
	for (index = count; index > 1U; index--)
	{
		sg_weapon_static_partition_ref_t swap = items[0];

		items[0] = items[index - 1U];
		items[index - 1U] = swap;
		PartitionRefSift(items, index - 1U, 0U, work);
	}
}

static void BoundsInclude(sg_rune_bounds_t *bounds,
	const sg_rune_bounds_t *other)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (other->mins.value[axis] < bounds->mins.value[axis])
			bounds->mins.value[axis] = other->mins.value[axis];
		if (other->maxs.value[axis] > bounds->maxs.value[axis])
			bounds->maxs.value[axis] = other->maxs.value[axis];
	}
}

static uint32_t BuildBvhNode(sg_weapon_static_context_t *context,
	uint32_t first, uint32_t count)
{
	uint32_t node_index = context->bvh_node_count++;
	sg_weapon_static_bvh_node_t *node = &context->bvh_nodes[node_index];
	uint32_t index;

	context->surface_preparation_work++;
	node->left = SG_STATIC_VISIBILITY_INDEX_NONE;
	node->right = SG_STATIC_VISIBILITY_INDEX_NONE;
	node->surface = SG_STATIC_VISIBILITY_INDEX_NONE;
	node->bounds = context->surface_refs[first].bounds;
	for (index = 1U; index < count; index++)
	{
		BoundsInclude(&node->bounds, &context->surface_refs[first + index].bounds);
		context->surface_preparation_work++;
	}
	if (count == 1U)
	{
		node->surface = context->surface_refs[first].surface;
		return node_index;
	}
	index = count / 2U;
	node->left = BuildBvhNode(context, first, index);
	node->right = BuildBvhNode(context, first + index, count - index);
	return node_index;
}

static uint32_t BuildPartitionNode(sg_weapon_static_context_t *context,
	uint32_t first, uint32_t count)
{
	uint32_t node_index = context->partition_node_count++;
	sg_weapon_static_partition_node_t *node =
		&context->partition_nodes[node_index];
	uint32_t index;

	context->partition_preparation_work++;
	node->left = SG_STATIC_VISIBILITY_INDEX_NONE;
	node->right = SG_STATIC_VISIBILITY_INDEX_NONE;
	node->partition = SG_STATIC_VISIBILITY_INDEX_NONE;
	node->bounds = context->partition_refs[first].bounds;
	for (index = 1U; index < count; index++)
	{
		BoundsInclude(&node->bounds,
			&context->partition_refs[first + index].bounds);
		context->partition_preparation_work++;
	}
	if (count == 1U)
	{
		node->partition = context->partition_refs[first].partition;
		return node_index;
	}
	index = count / 2U;
	node->left = BuildPartitionNode(context, first, index);
	node->right = BuildPartitionNode(context, first + index, count - index);
	return node_index;
}

static void SetPrepareError(sg_weapon_static_prepare_error_t *error,
	sg_weapon_static_prepare_error_code_t code)
{
	if (!error)
		return;
	memset(error, 0, sizeof(*error));
	error->code = code;
	error->record = SG_STATIC_VISIBILITY_INDEX_NONE;
}

static const sg_rune_v2_artifact_snapshot_t *ReadArtifactLoaderBridge(
	const sg_weapon_static_artifact_loader_bridge_t *bridge)
{
	const sg_rune_v2_artifact_snapshot_t *snapshot;

	if (!bridge || !bridge->loader || !bridge->snapshot)
		return NULL;
	snapshot = SG_RuneV2ArtifactLoaderSnapshot(bridge->loader);
	return snapshot == bridge->snapshot ? snapshot : NULL;
}

void SG_WeaponStaticContextDestroy(sg_weapon_static_context_t *context)
{
	if (!context)
		return;
	free(context->bvh_nodes);
	free(context->surface_refs);
	free(context->partition_nodes);
	free(context->partition_refs);
	free(context->partition_indices);
	free(context->cells);
	free(context);
}

uint64_t SG_WeaponStaticContextBindingComparisons(
	const sg_weapon_static_context_t *context)
{
	return context ? context->binding_comparisons : 0U;
}

uint64_t SG_WeaponStaticContextPartitionPreparationWork(
	const sg_weapon_static_context_t *context)
{
	return context ? context->partition_preparation_work : 0U;
}

uint64_t SG_WeaponStaticContextSurfacePreparationWork(
	const sg_weapon_static_context_t *context)
{
	return context ? context->surface_preparation_work : 0U;
}

int SG_WeaponStaticContextPrepare(
	const sg_weapon_static_prepare_input_t *input,
	sg_weapon_static_context_t **context_out,
	sg_weapon_static_prepare_error_t *error_out)
{
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_audit_result_t semantics_audit;
	sg_static_visibility_audit_result_t visibility_audit;
	sg_rune_failure_reason_t model_reason;
	sg_weapon_static_context_t *context = NULL;
	const sg_rune_v2_artifact_snapshot_t *artifact_snapshot;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;
	const sg_rune_model_t *model;
	const sg_rune_validation_evidence_t *model_evidence;
	sg_weapon_static_binding_t binding;
	uint64_t visibility_revision;
	sg_weapon_static_configuration_ref_t *configuration_refs = NULL;
	uint32_t *counts = NULL, *positions = NULL, *binding_by_configuration = NULL;
	uint32_t index, total = 0U;

	SetPrepareError(error_out, SG_WEAPON_STATIC_PREPARE_ERROR_NONE);
	if (context_out)
		*context_out = NULL;
	if (!input || !context_out || !input->visibility_publication)
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	artifact_snapshot = ReadArtifactLoaderBridge(&input->artifact);
	if (!artifact_snapshot ||
		!SG_StaticVisibilityPublicationRead(input->visibility_publication,
			&authority, &configuration, &semantics, &visibility,
			&visibility_revision))
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		return 0;
	}
	model = &artifact_snapshot->model;
	model_evidence = &artifact_snapshot->evidence;
	if (visibility_revision != artifact_snapshot->binding.generation)
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		return 0;
	}
	memset(&binding, 0, sizeof(binding));
	binding.artifact_identity = artifact_snapshot->binding.artifact_identity;
	binding.bsp_identity = artifact_snapshot->binding.bsp_identity;
	binding.schema_identity = artifact_snapshot->binding.schema_identity;
	binding.source_set_identity = model->identity.source_set_identity;
	binding.visibility_revision = visibility_revision;
	if (!SG_WeaponStaticBindingValid(&binding) ||
		binding.source_set_identity != authority->identity.source_set_identity)
	{
		SetPrepareError(error_out, SG_WEAPON_STATIC_PREPARE_ERROR_BINDING);
		return 0;
	}
	if (!IdentityEqual(&authority->identity,
			&configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		!IdentityEqual(&authority->identity, &visibility->identity) ||
		!IdentityEqual(&authority->identity, &model->identity))
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		return 0;
	}
	model_reason = SG_RuneModelValidate(model, model_evidence);
	if (model_reason != SG_RUNE_FAILURE_NONE)
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_MODEL_VALIDATION);
		if (error_out)
			error_out->model = model_reason;
		return 0;
	}
	if (!SG_ConfigurationAudit(authority, configuration,
			&configuration_audit))
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_CONFIGURATION_AUDIT);
		if (error_out)
		{
			error_out->configuration = configuration_audit.code;
			error_out->record = configuration_audit.record;
		}
		return 0;
	}
	if (!SG_ConfigurationSemanticsAudit(authority, configuration,
			semantics, &semantics_audit))
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_SEMANTICS_AUDIT);
		if (error_out)
		{
			error_out->semantics = semantics_audit.code;
			error_out->record = semantics_audit.record;
		}
		return 0;
	}
	if (!SG_StaticVisibilityAudit(authority, configuration,
			semantics, visibility, &visibility_audit))
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_VISIBILITY_AUDIT);
		if (error_out)
		{
			error_out->visibility = visibility_audit.code;
			error_out->record = visibility_audit.record;
		}
		return 0;
	}
	if (model->cell_count == 0U ||
		visibility->partition_count == 0U ||
		(uint64_t)visibility->partition_count * 2U - model->cell_count >
			UINT32_MAX ||
		visibility->surface_count > (UINT32_MAX + UINT64_C(1)) / 2U)
	{
		SetPrepareError(error_out, SG_WEAPON_STATIC_PREPARE_ERROR_OVERFLOW);
		return 0;
	}
	context = calloc(1U, sizeof(*context));
	if (!context)
		goto out_of_memory;
	context->binding = binding;
	context->authority = authority;
	context->configuration = configuration;
	context->semantics = semantics;
	context->visibility = visibility;
	context->model = model;
	context->artifact_loader = input->artifact.loader;
	context->artifact_snapshot = artifact_snapshot;
	context->visibility_publication = input->visibility_publication;
	context->cell_count = model->cell_count;
	context->partition_count = visibility->partition_count;
	context->cells = calloc(context->cell_count, sizeof(*context->cells));
	context->partition_indices = calloc(context->partition_count,
		sizeof(*context->partition_indices));
	configuration_refs = calloc(configuration->cell_count,
		sizeof(*configuration_refs));
	counts = calloc(configuration->cell_count, sizeof(*counts));
	positions = calloc(configuration->cell_count, sizeof(*positions));
	binding_by_configuration = calloc(configuration->cell_count,
		sizeof(*binding_by_configuration));
	if (!context->cells || !context->partition_indices ||
		!configuration_refs || !counts || !positions ||
		!binding_by_configuration)
		goto out_of_memory;
	for (index = 0U; index < configuration->cell_count; index++)
	{
		binding_by_configuration[index] = UINT32_MAX;
		configuration_refs[index].id =
			configuration->cells[index].id.value;
		configuration_refs[index].configuration_cell = index;
	}
	ConfigurationRefSort(configuration_refs, configuration->cell_count,
		&context->binding_comparisons);
	for (index = 1U; index < configuration->cell_count; index++)
	{
		context->binding_comparisons++;
		if (StableIdCompare(&configuration_refs[index - 1U].id,
			&configuration_refs[index].id) == 0)
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
	}
	for (index = 0U; index < context->partition_count; index++)
	{
		uint32_t cell = visibility->partitions[index].configuration_cell;

		if (cell >= configuration->cell_count || counts[cell] == UINT32_MAX)
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
		counts[cell]++;
	}
	if (configuration->cell_count != context->cell_count)
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		goto failure;
	}
	for (index = 0U; index < context->cell_count; index++)
	{
		uint32_t configuration_cell;

		context->cells[index].id = model->cells[index].id.value;
		context->cells[index].model_cell = index;
		if (!FindConfigurationRef(configuration_refs,
			configuration->cell_count, &context->cells[index].id,
			&context->binding_comparisons, &configuration_cell) ||
			counts[configuration_cell] == 0U ||
			binding_by_configuration[configuration_cell] != UINT32_MAX ||
			!ConfigurationModelCellEqual(
				&configuration->cells[configuration_cell],
				&model->cells[index]))
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
		context->cells[index].configuration_cell = configuration_cell;
		binding_by_configuration[configuration_cell] = index;
	}
	for (index = 0U; index < configuration->cell_count; index++)
	{
		if (counts[index] == 0U)
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
		if (binding_by_configuration[index] == UINT32_MAX)
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
	}
	for (index = 0U; index < configuration->cell_count; index++)
	{
		uint32_t cell_binding = binding_by_configuration[index];

		if (cell_binding == UINT32_MAX)
			continue;
		if (counts[index] == 0U || total > UINT32_MAX - counts[index])
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
		context->cells[cell_binding].first_partition = total;
		context->cells[cell_binding].partition_count = counts[index];
		positions[index] = total;
		total += counts[index];
	}
	for (index = 0U; index < context->partition_count; index++)
	{
		uint32_t cell = visibility->partitions[index].configuration_cell;

		context->partition_indices[positions[cell]++] = index;
	}
	if (total != visibility->partition_count)
	{
		SetPrepareError(error_out,
			SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		goto failure;
	}
	context->partition_count = total;
	{
		uint64_t partition_nodes = (uint64_t)total * 2U -
			context->cell_count;

		context->partition_refs = calloc(total,
			sizeof(*context->partition_refs));
		context->partition_nodes = calloc((size_t)partition_nodes,
			sizeof(*context->partition_nodes));
		if (!context->partition_refs || !context->partition_nodes)
			goto out_of_memory;
		for (index = 0U; index < context->cell_count; index++)
		{
			sg_weapon_static_cell_binding_t *cell = &context->cells[index];
			uint32_t local;

			for (local = 0U; local < cell->partition_count; local++)
			{
				uint32_t offset = cell->first_partition + local;
				uint32_t partition = context->partition_indices[offset];
				uint32_t region = visibility->partitions[
					partition].configuration_region;

				context->partition_refs[offset].partition = partition;
				context->partition_refs[offset].bounds =
					semantics->regions[region].bounds;
				context->partition_preparation_work++;
			}
			PartitionRefSort(&context->partition_refs[cell->first_partition],
				cell->partition_count, &context->partition_preparation_work);
			cell->partition_root = BuildPartitionNode(context,
				cell->first_partition, cell->partition_count);
		}
		if (context->partition_node_count != (uint32_t)partition_nodes)
		{
			SetPrepareError(error_out,
				SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
			goto failure;
		}
	}
	if (visibility->surface_count != 0U)
	{
		uint64_t nodes = (uint64_t)visibility->surface_count * 2U - 1U;

		context->surface_refs = calloc(visibility->surface_count,
			sizeof(*context->surface_refs));
		context->bvh_nodes = calloc((size_t)nodes,
			sizeof(*context->bvh_nodes));
		if (!context->surface_refs || !context->bvh_nodes)
			goto out_of_memory;
		for (index = 0U; index < visibility->surface_count; index++)
		{
			context->surface_refs[index].surface = index;
			context->surface_refs[index].bounds =
				semantics->hook_surfaces[index].bounds;
			context->surface_preparation_work++;
		}
		SurfaceRefSort(context->surface_refs, visibility->surface_count,
			&context->surface_preparation_work);
		context->bvh_root = BuildBvhNode(context, 0U,
			visibility->surface_count);
	}
	else
		context->bvh_root = SG_STATIC_VISIBILITY_INDEX_NONE;
	free(binding_by_configuration);
	free(positions);
	free(counts);
	free(configuration_refs);
	*context_out = context;
	return 1;

out_of_memory:
	SetPrepareError(error_out, SG_WEAPON_STATIC_PREPARE_ERROR_OUT_OF_MEMORY);
failure:
	free(binding_by_configuration);
	free(positions);
	free(counts);
	free(configuration_refs);
	SG_WeaponStaticContextDestroy(context);
	return 0;
}

static const sg_weapon_static_cell_binding_t *FindCellBinding(
	const sg_weapon_static_context_t *context,
	const sg_rune_cell_ref_t *reference)
{
	uint32_t low = 0U, high = context->cell_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&context->cells[middle].id,
			&reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= context->cell_count ||
		StableIdCompare(&context->cells[low].id, &reference->value) != 0)
		return NULL;
	return &context->cells[low];
}

static int PointInsideBounds(const sg_rune_bounds_t *bounds,
	const float point[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < bounds->mins.value[axis] -
				SG_WEAPON_SURFACE_EPSILON ||
			point[axis] > bounds->maxs.value[axis] +
				SG_WEAPON_SURFACE_EPSILON)
			return 0;
	return 1;
}

static void FindPointPartitionNode(
	const sg_weapon_static_context_t *context, uint32_t node_index,
	const float point[3], uint32_t *best_partition,
	uint32_t *nodes_visited, uint32_t *bounds_overlaps,
	uint32_t *face_tests)
{
	const sg_weapon_static_partition_node_t *node;

	(*nodes_visited)++;
	node = &context->partition_nodes[node_index];
	if (!PointInsideBounds(&node->bounds, point))
		return;
	(*bounds_overlaps)++;
	if (node->partition != SG_STATIC_VISIBILITY_INDEX_NONE)
	{
		uint32_t tested = 0U;

		if (SG_StaticVisibilityPointInPartition(context->semantics,
				context->visibility, node->partition, point, &tested) &&
			node->partition < *best_partition)
			*best_partition = node->partition;
		*face_tests += tested;
		return;
	}
	FindPointPartitionNode(context, node->left, point, best_partition,
		nodes_visited, bounds_overlaps, face_tests);
	FindPointPartitionNode(context, node->right, point, best_partition,
		nodes_visited, bounds_overlaps, face_tests);
}

static int FindPointPartition(const sg_weapon_static_context_t *context,
	const sg_weapon_static_cell_binding_t *binding, const float point[3],
	uint32_t *partition_out, uint32_t *nodes_visited,
	uint32_t *bounds_overlaps, uint32_t *face_tests)
{
	uint32_t best = SG_STATIC_VISIBILITY_INDEX_NONE;

	FindPointPartitionNode(context, binding->partition_root, point, &best,
		nodes_visited, bounds_overlaps, face_tests);
	if (best == SG_STATIC_VISIBILITY_INDEX_NONE)
		return 0;
	*partition_out = best;
	return 1;
}

static int LocatePose(const sg_weapon_static_context_t *context,
	const sg_rune_cell_ref_t *cell_reference,
	const sg_rune_phase_ref_t *phase_reference, const float point[3],
	uint32_t *partition_out, uint32_t *nodes_visited_out,
	uint32_t *bounds_overlaps_out, uint32_t *face_tests_out)
{
	const sg_weapon_static_cell_binding_t *binding =
		FindCellBinding(context, cell_reference);
	const sg_rune_phase_span_t *span;
	uint32_t phase;

	if (!binding || !FindModelPhase(context->model, phase_reference, &phase))
		return 0;
	span = &context->model->cells[binding->model_cell].phases;
	if (span->first > context->model->phase_count ||
		span->count > context->model->phase_count - span->first ||
		phase < span->first || phase - span->first >= span->count)
		return 0;
	return FindPointPartition(context, binding, point, partition_out,
		nodes_visited_out, bounds_overlaps_out, face_tests_out);
}

static sg_weapon_static_relation_t AllowedRelations(
	const sg_weapon_profile_t *profile)
{
	sg_weapon_static_relation_t allowed = 0U;

	if ((profile->effects & SG_WEAPON_EFFECT_HITSCAN) != 0U)
		allowed |= SG_WEAPON_STATIC_DIRECT_VISIBILITY;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U)
		allowed |= SG_WEAPON_STATIC_PROJECTILE_CORRIDOR;
	if (profile->supports_occluded_impact != 0U)
		allowed |= SG_WEAPON_STATIC_IMPACT_SURFACE;
	if ((profile->effects & SG_WEAPON_EFFECT_SPLASH) != 0U)
		allowed |= SG_WEAPON_STATIC_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_SECONDARY_AREA) != 0U)
		allowed |= SG_WEAPON_STATIC_SECONDARY_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_BOUNCE) != 0U)
		allowed |= SG_WEAPON_STATIC_BOUNCE_SURFACE;
	if ((profile->effects & SG_WEAPON_EFFECT_PERIODIC_RAY) != 0U)
		allowed |= SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY;
	return allowed;
}

static sg_weapon_static_status_t VisibilityStatus(
	const sg_static_visibility_result_t *visibility)
{
	switch (visibility->classification)
	{
	case SG_STATIC_VISIBILITY_VISIBLE:
		return SG_WEAPON_STATIC_PROVEN;
	case SG_STATIC_VISIBILITY_CONDITIONAL:
		return SG_WEAPON_STATIC_CONDITIONAL;
	case SG_STATIC_VISIBILITY_OCCLUDED:
		return SG_WEAPON_STATIC_REJECTED;
	}
	return SG_WEAPON_STATIC_REJECTED;
}

static void InitAffordance(sg_weapon_static_affordance_t *affordance,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile)
{
	size_t index;

	memset(affordance, 0, sizeof(*affordance));
	affordance->binding = query->binding;
	affordance->profile_id = profile->id;
	affordance->family = profile->family;
	affordance->requested_relations = query->requested_relations;
	affordance->allowed_relations = AllowedRelations(profile);
	affordance->exact_authenticated_live_prefire_trace_required = 1U;
	for (index = 0U; index < SG_WEAPON_STATIC_RELATION_COUNT; index++)
	{
		sg_weapon_static_relation_result_t *relation =
			&affordance->relations[index];

		relation->relation = relation_order[index];
		relation->visibility.surface = SG_STATIC_VISIBILITY_INDEX_NONE;
		relation->visibility.source_partition =
			SG_STATIC_VISIBILITY_INDEX_NONE;
		relation->visibility.destination_partition =
			SG_STATIC_VISIBILITY_INDEX_NONE;
		relation->visibility.trace.fraction = 1.0f;
		if ((query->requested_relations & relation->relation) != 0U &&
			(affordance->allowed_relations & relation->relation) == 0U)
		{
			relation->status = SG_WEAPON_STATIC_REJECTED;
			relation->reason = SG_WEAPON_STATIC_REASON_PROFILE_UNSUPPORTED;
		}
		else if (relation->relation ==
				SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY &&
			(query->requested_relations & relation->relation) != 0U)
		{
			relation->status = SG_WEAPON_STATIC_CONDITIONAL;
			relation->reason =
				SG_WEAPON_STATIC_REASON_RUNTIME_PROJECTILE_ORIGIN;
		}
	}
}

static void SetRelation(sg_weapon_static_relation_result_t *relation,
	sg_weapon_static_status_t status, sg_weapon_static_reason_t reason,
	const sg_static_visibility_result_t *visibility)
{
	relation->status = status;
	relation->reason = reason;
	if (visibility)
		relation->visibility = *visibility;
}

static int TraceBlocked(const sg_host_collision_trace_t *trace)
{
	return trace->startsolid || trace->allsolid || trace->fraction < 1.0f;
}

static int RefineProjectileClearance(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_relation_result_t *relation)
{
	float mins[3], maxs[3];
	sg_host_collision_trace_t trace;
	uint32_t axis;

	if (relation->status != SG_WEAPON_STATIC_PROVEN ||
		profile->projectile_half_extent == 0.0f)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = -profile->projectile_half_extent;
		maxs[axis] = profile->projectile_half_extent;
	}
	if (!SG_HostCollisionTraceModel(authority, SG_HOST_COLLISION_MODEL_WORLD,
			NULL, query->source_origin.value, mins, maxs,
			query->target_origin.value,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &trace))
		return 0;
	if (TraceBlocked(&trace))
	{
		relation->status = SG_WEAPON_STATIC_REJECTED;
		relation->reason = SG_WEAPON_STATIC_REASON_PROJECTILE_CLEARANCE;
		relation->visibility.classification =
			SG_STATIC_VISIBILITY_OCCLUDED;
		relation->visibility.reason =
			SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
		relation->visibility.trace = trace;
		return 1;
	}
	if (scene->instance_count == 0U)
		return 1;
	if (!SG_HostCollisionTrace(authority, scene, query->source_origin.value,
			mins, maxs, query->target_origin.value,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &trace))
		return 0;
	if (TraceBlocked(&trace))
	{
		relation->status = SG_WEAPON_STATIC_CONDITIONAL;
		relation->reason = SG_WEAPON_STATIC_REASON_PROJECTILE_CLEARANCE;
		relation->visibility.classification =
			SG_STATIC_VISIBILITY_CONDITIONAL;
		relation->visibility.reason =
			SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
		relation->visibility.trace = trace;
	}
	return 1;
}

static float EffectRadius(const sg_weapon_profile_t *profile)
{
	return profile->secondary_splash_radius > profile->splash_radius ?
		profile->secondary_splash_radius : profile->splash_radius;
}

static float Dot3(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static void Cross3(const float left[3], const float right[3], float result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static int ClosestPointOnSurface(
	const sg_configuration_semantics_t *semantics,
	const sg_configuration_hook_surface_t *surface,
	const float point[3], float closest[3])
{
	float projected[3], signed_distance;
	float winding = 0.0f;
	float best_distance_squared = 0.0f;
	uint32_t vertex, axis;
	int projected_inside = 1;
	int have_best = 0;

	if (surface->vertex_count < 3U ||
		surface->first_vertex > semantics->hook_vertex_count ||
		surface->vertex_count >
			semantics->hook_vertex_count - surface->first_vertex)
		return 0;
	{
		float normal_length_squared = Dot3(surface->normal, surface->normal);

		if (!isfinite(normal_length_squared) || normal_length_squared <= 0.0f)
			return 0;
		signed_distance = (Dot3(point, surface->normal) - surface->distance) /
			normal_length_squared;
	}
	for (axis = 0U; axis < 3U; axis++)
		projected[axis] = point[axis] - signed_distance * surface->normal[axis];
	if (!isfinite(projected[0]) || !isfinite(projected[1]) ||
		!isfinite(projected[2]))
		return 0;
	for (vertex = 0U; vertex < surface->vertex_count; vertex++)
	{
		const float *start = semantics->hook_vertices[
			surface->first_vertex + vertex].value;
		const float *end = semantics->hook_vertices[surface->first_vertex +
			(vertex + 1U) % surface->vertex_count].value;
		float edge[3], relative[3], cross[3], side;

		for (axis = 0U; axis < 3U; axis++)
		{
			edge[axis] = end[axis] - start[axis];
			relative[axis] = projected[axis] - start[axis];
		}
		Cross3(edge, relative, cross);
		side = Dot3(cross, surface->normal);
		if (fabsf(side) <= SG_WEAPON_SURFACE_EPSILON)
			continue;
		if (winding == 0.0f)
			winding = side;
		else if ((winding < 0.0f) != (side < 0.0f))
			projected_inside = 0;
	}
	if (projected_inside)
	{
		memcpy(closest, projected, sizeof(projected));
		return 1;
	}
	for (vertex = 0U; vertex < surface->vertex_count; vertex++)
	{
		const float *start = semantics->hook_vertices[
			surface->first_vertex + vertex].value;
		const float *end = semantics->hook_vertices[surface->first_vertex +
			(vertex + 1U) % surface->vertex_count].value;
		float edge[3], relative[3], candidate[3];
		float length_squared, parameter, distance_squared = 0.0f;

		for (axis = 0U; axis < 3U; axis++)
		{
			edge[axis] = end[axis] - start[axis];
			relative[axis] = point[axis] - start[axis];
		}
		length_squared = Dot3(edge, edge);
		if (!isfinite(length_squared) || length_squared <= 0.0f)
			return 0;
		parameter = Dot3(relative, edge) / length_squared;
		if (parameter < 0.0f)
			parameter = 0.0f;
		else if (parameter > 1.0f)
			parameter = 1.0f;
		for (axis = 0U; axis < 3U; axis++)
		{
			float delta;

			candidate[axis] = start[axis] + parameter * edge[axis];
			delta = point[axis] - candidate[axis];
			distance_squared += delta * delta;
		}
		if (!isfinite(distance_squared))
			return 0;
		if (!have_best || distance_squared < best_distance_squared)
		{
			memcpy(closest, candidate, sizeof(candidate));
			best_distance_squared = distance_squared;
			have_best = 1;
		}
	}
	return have_best;
}

static int WithinRadius(const sg_weapon_static_query_t *query, float radius,
	const float impact[3], float closest[3])
{
	float distance_squared = 0.0f;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = 0.0f;
		float coordinate = impact[axis];

		if (coordinate < query->target_bounds.mins.value[axis])
		{
			closest[axis] = query->target_bounds.mins.value[axis];
			delta = closest[axis] - coordinate;
		}
		else if (coordinate > query->target_bounds.maxs.value[axis])
		{
			closest[axis] = query->target_bounds.maxs.value[axis];
			delta = coordinate - closest[axis];
		}
		else
			closest[axis] = coordinate;
		distance_squared += delta * delta;
	}
	return isfinite(distance_squared) != 0 &&
		distance_squared <= radius * radius;
}

static void PreferSurfaceEvidence(
	sg_weapon_static_relation_result_t *relation,
	sg_weapon_static_status_t status, sg_weapon_static_reason_t reason,
	const sg_static_visibility_result_t *visibility, const float witness[3])
{
	if (relation->status == SG_WEAPON_STATIC_NOT_REQUESTED ||
		status > relation->status ||
		(status == SG_WEAPON_STATIC_CONDITIONAL &&
		 ((relation->reason ==
			SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE &&
		   reason == SG_WEAPON_STATIC_REASON_VISIBILITY) ||
		  (reason == SG_WEAPON_STATIC_REASON_OWNER_DAMAGE_VISIBILITY &&
		   relation->reason !=
			SG_WEAPON_STATIC_REASON_OWNER_DAMAGE_VISIBILITY))))
	{
		SetRelation(relation, status, reason, visibility);
		if (witness)
		{
			memcpy(relation->witness_point.value, witness,
				sizeof(relation->witness_point.value));
			relation->has_witness_point = 1U;
		}
	}
}

static int SurfaceBoundsOutsideSplash(
	const sg_configuration_hook_surface_t *surface,
	const sg_weapon_static_query_t *query, float radius)
{
	float distance_squared = 0.0f;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = 0.0f;

		if (surface->bounds.maxs.value[axis] <
			query->target_bounds.mins.value[axis])
			delta = query->target_bounds.mins.value[axis] -
				surface->bounds.maxs.value[axis];
		else if (surface->bounds.mins.value[axis] >
			query->target_bounds.maxs.value[axis])
			delta = surface->bounds.mins.value[axis] -
				query->target_bounds.maxs.value[axis];
		distance_squared += delta * delta;
	}
	return isfinite(distance_squared) != 0 &&
		distance_squared > radius * radius;
}

static sg_weapon_static_status_t CandidateStatus(
	const sg_static_visibility_result_t *visibility,
	sg_weapon_static_reason_t *reason_out)
{
	sg_weapon_static_status_t status = VisibilityStatus(visibility);

	*reason_out = SG_WEAPON_STATIC_REASON_VISIBILITY;
	if (status == SG_WEAPON_STATIC_REJECTED &&
		visibility->reason != SG_STATIC_VISIBILITY_REASON_SKY)
	{
		status = SG_WEAPON_STATIC_CONDITIONAL;
		*reason_out = SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE;
	}
	return status;
}

static int TraceClear(const sg_host_collision_trace_t *trace)
{
	return !trace->startsolid && !trace->allsolid && trace->fraction == 1.0f;
}

static int OwnerDamageVisibility(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_static_cell_binding_t *target_binding,
	uint32_t source_partition, sg_weapon_static_status_t *status_out,
	sg_static_visibility_result_t *evidence, uint32_t *nodes_visited,
	uint32_t *bounds_overlaps, uint32_t *face_tests)
{
	static const float offsets[5][2] = {
		{ 0.0f, 0.0f }, { 15.0f, 15.0f }, { 15.0f, -15.0f },
		{ -15.0f, 15.0f }, { -15.0f, -15.0f }
	};
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	sg_weapon_static_status_t best = SG_WEAPON_STATIC_REJECTED;
	uint32_t sample;

	memset(evidence, 0, sizeof(*evidence));
	evidence->source_partition = source_partition;
	evidence->destination_partition = SG_STATIC_VISIBILITY_INDEX_NONE;
	evidence->surface = SG_STATIC_VISIBILITY_INDEX_NONE;
	for (sample = 0U; sample < 5U; sample++)
	{
		float destination[3] = {
			query->target_origin.value[0] + offsets[sample][0],
			query->target_origin.value[1] + offsets[sample][1],
			query->target_origin.value[2]
		};
		sg_host_collision_trace_t world_trace, scene_trace;
		uint32_t destination_partition;

		if (!FindPointPartition(context, target_binding, destination,
				&destination_partition, nodes_visited, bounds_overlaps,
				face_tests))
			continue;
		if (!SG_HostCollisionTraceModel(context->authority,
				SG_HOST_COLLISION_MODEL_WORLD, NULL,
				query->source_origin.value, zero, zero, destination,
				SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &world_trace))
			return 0;
		if (!TraceClear(&world_trace))
		{
			evidence->trace = world_trace;
			continue;
		}
		if (scene->instance_count != 0U)
		{
			if (!SG_HostCollisionTrace(context->authority, scene,
					query->source_origin.value, zero, zero, destination,
					SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW,
					&scene_trace))
				return 0;
			if (!TraceClear(&scene_trace))
			{
				best = SG_WEAPON_STATIC_CONDITIONAL;
				evidence->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
				evidence->reason =
					SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
				evidence->destination_partition = destination_partition;
				evidence->trace = scene_trace;
				continue;
			}
		}
		evidence->classification = SG_STATIC_VISIBILITY_VISIBLE;
		evidence->reason = SG_STATIC_VISIBILITY_REASON_NONE;
		evidence->destination_partition = destination_partition;
		evidence->trace = world_trace;
		*status_out = SG_WEAPON_STATIC_PROVEN;
		return 1;
	}
	if (best == SG_WEAPON_STATIC_REJECTED)
	{
		evidence->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		evidence->reason = SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
	}
	*status_out = best;
	return 1;
}

static int ImpactDamageVisibility(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_static_cell_binding_t *target_binding, uint32_t surface,
	const float impact_point[3], sg_weapon_static_status_t *status_out,
	sg_static_visibility_result_t *evidence_out,
	uint32_t *nodes_visited, uint32_t *bounds_overlaps, uint32_t *face_tests,
	sg_weapon_static_affordance_error_t *error)
{
	static const float offsets[5][2] = {
		{ 0.0f, 0.0f }, { 15.0f, 15.0f }, { 15.0f, -15.0f },
		{ -15.0f, 15.0f }, { -15.0f, -15.0f }
	};
	sg_weapon_static_status_t best = SG_WEAPON_STATIC_REJECTED;
	uint32_t sample;

	memset(evidence_out, 0, sizeof(*evidence_out));
	evidence_out->surface = surface;
	evidence_out->source_partition = SG_STATIC_VISIBILITY_INDEX_NONE;
	evidence_out->destination_partition = SG_STATIC_VISIBILITY_INDEX_NONE;
	for (sample = 0U; sample < 5U; sample++)
	{
		float target[3] = {
			query->target_origin.value[0] + offsets[sample][0],
			query->target_origin.value[1] + offsets[sample][1],
			query->target_origin.value[2]
		};
		sg_static_visibility_result_t visibility;
		sg_static_visibility_error_t visibility_error;
		uint32_t partition;

		if (FindPointPartition(context, target_binding, target, &partition,
				nodes_visited, bounds_overlaps, face_tests))
		{
			if (!SG_StaticVisibilityQueryBoundSurface(context->authority, scene,
					context->configuration, context->semantics,
					context->visibility, partition, target, surface, impact_point,
					&visibility, &visibility_error))
			{
				if (visibility_error.code ==
						SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY ||
					visibility_error.code ==
						SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH)
					break;
				SetError(error, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
					&visibility_error);
				return 0;
			}
			if (VisibilityStatus(&visibility) > best)
			{
				best = VisibilityStatus(&visibility);
				*evidence_out = visibility;
			}
		}
		if (best == SG_WEAPON_STATIC_PROVEN)
			break;
	}
	if (best == SG_WEAPON_STATIC_REJECTED)
	{
		evidence_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		evidence_out->reason = SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
	}
	*status_out = best;
	return 1;
}

static int ResolveBlastCandidate(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_static_cell_binding_t *target_binding,
	uint32_t surface, const float impact_point[3], float radius,
	sg_weapon_static_status_t source_status,
	sg_weapon_static_reason_t source_reason,
	const sg_static_visibility_result_t *source_visibility,
	sg_weapon_static_status_t owner_status,
	const sg_static_visibility_result_t *owner_visibility,
	int require_owner, sg_weapon_static_relation_result_t *relation,
	sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	float closest[3];
	sg_static_visibility_result_t target_visibility;
	sg_weapon_static_status_t target_status;
	sg_weapon_static_status_t combined = source_status;
	sg_weapon_static_reason_t reason = source_reason;
	const sg_static_visibility_result_t *evidence = source_visibility;

	if (!WithinRadius(query, radius, impact_point, closest))
		return 1;
	if (source_status != SG_WEAPON_STATIC_REJECTED)
	{
		sg_weapon_static_reason_t target_reason;

		if (!ImpactDamageVisibility(context, scene, query, target_binding,
				surface, impact_point, &target_status, &target_visibility,
				&affordance->pose_partition_nodes_visited,
				&affordance->pose_partition_bounds_overlaps,
				&affordance->pose_partition_faces_tested, error))
			return 0;
		target_status = CandidateStatus(&target_visibility, &target_reason);
		if (target_status < combined)
		{
			combined = target_status;
			reason = target_reason;
			evidence = &target_visibility;
		}
	}
	if (require_owner && owner_status < combined)
	{
		combined = owner_status;
		reason = SG_WEAPON_STATIC_REASON_OWNER_DAMAGE_VISIBILITY;
		evidence = owner_visibility;
	}
	PreferSurfaceEvidence(relation, combined, reason, evidence, impact_point);
	return 1;
}

static int EvaluateSurfaceCandidate(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile, uint32_t source_partition,
	const sg_weapon_static_cell_binding_t *target_binding,
	sg_weapon_static_status_t owner_status,
	const sg_static_visibility_result_t *owner_visibility,
	sg_weapon_static_relation_t requested, uint32_t surface,
	const float impact_point[3], sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	sg_static_visibility_result_t surface_visibility;
	sg_static_visibility_error_t visibility_error;
	sg_weapon_static_reason_t reason;
	sg_weapon_static_status_t status;

	affordance->candidate_points_queried++;
	if (!SG_StaticVisibilityQueryBoundSurface(context->authority, scene,
			context->configuration, context->semantics, context->visibility,
			source_partition, query->source_origin.value, surface, impact_point,
			&surface_visibility, &visibility_error))
	{
		if (visibility_error.code ==
				SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY ||
			visibility_error.code ==
				SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH)
			return 2;
		SetError(error, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
			&visibility_error);
		return 0;
	}
	status = CandidateStatus(&surface_visibility, &reason);
	if ((requested & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U)
		PreferSurfaceEvidence(&affordance->relations[2], status, reason,
			&surface_visibility, impact_point);
	if ((requested & SG_WEAPON_STATIC_BLAST_REACH) != 0U &&
		!ResolveBlastCandidate(context, scene, query, target_binding, surface,
			impact_point, profile->splash_radius, status, reason,
			&surface_visibility, owner_status, owner_visibility, 0,
			&affordance->relations[3], affordance, error))
		return 0;
	if ((requested & SG_WEAPON_STATIC_BOUNCE_SURFACE) != 0U)
		PreferSurfaceEvidence(&affordance->relations[4], status, reason,
			&surface_visibility, impact_point);
	if ((requested & SG_WEAPON_STATIC_SECONDARY_BLAST_REACH) != 0U &&
		!ResolveBlastCandidate(context, scene, query, target_binding, surface,
			impact_point, profile->secondary_splash_radius, status, reason,
			&surface_visibility, owner_status, owner_visibility, 1,
			&affordance->relations[5], affordance, error))
		return 0;
	return 1;
}

typedef struct sg_weapon_surface_query_state_s
{
	const sg_weapon_static_context_t *context;
	const sg_host_collision_scene_t *scene;
	const sg_weapon_static_query_t *query;
	const sg_weapon_profile_t *profile;
	sg_weapon_static_relation_t requested;
	uint32_t source_partition;
	const sg_weapon_static_cell_binding_t *target_binding;
	sg_weapon_static_status_t owner_status;
	const sg_static_visibility_result_t *owner_visibility;
	float effect_radius;
	uint8_t found_surface;
	uint8_t found_core_surface;
	uint8_t found_secondary_surface;
	sg_weapon_static_affordance_t *affordance;
	sg_weapon_static_affordance_error_t *error;
} sg_weapon_surface_query_state_t;

static int BoundsOutsideTargetRadius(const sg_rune_bounds_t *bounds,
	const sg_weapon_static_query_t *query, float radius)
{
	float distance_squared = 0.0f;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = 0.0f;

		if (bounds->maxs.value[axis] < query->target_bounds.mins.value[axis])
			delta = query->target_bounds.mins.value[axis] -
				bounds->maxs.value[axis];
		else if (bounds->mins.value[axis] > query->target_bounds.maxs.value[axis])
			delta = bounds->mins.value[axis] -
				query->target_bounds.maxs.value[axis];
		distance_squared += delta * delta;
	}
	return !isfinite(distance_squared) || distance_squared > radius * radius;
}

static int ProcessSurface(sg_weapon_surface_query_state_t *state,
	uint32_t surface)
{
	const sg_configuration_hook_surface_t *surface_record =
		&state->context->semantics->hook_surfaces[surface];
	uint32_t candidate_count, candidate;
	float nearest[3];

	if (SurfaceBoundsOutsideSplash(surface_record, state->query,
			state->effect_radius))
		return 1;
	state->affordance->candidate_surfaces_visited++;
	state->found_surface = 1U;
	if (!SurfaceBoundsOutsideSplash(surface_record, state->query,
			state->profile->splash_radius))
		state->found_core_surface = 1U;
	if (!SurfaceBoundsOutsideSplash(surface_record, state->query,
			state->profile->secondary_splash_radius))
		state->found_secondary_surface = 1U;
	if (!ClosestPointOnSurface(state->context->semantics, surface_record,
			state->query->target_origin.value, nearest))
	{
		SetError(state->error,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
		return 0;
	}
	if (surface_record->vertex_count > (UINT32_MAX - 9U) / 3U)
	{
		SetError(state->error,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
		return 0;
	}
	candidate_count = 1U + 3U * surface_record->vertex_count + 8U;
	for (candidate = 0U; candidate < candidate_count; candidate++)
	{
		float impact_point[3], seed[3], closest[3];
		int evaluated;

		if (candidate == 0U)
			memcpy(impact_point, nearest, sizeof(impact_point));
		else if (candidate <= surface_record->vertex_count)
		{
			memcpy(seed, state->context->semantics->hook_vertices[
				surface_record->first_vertex + candidate - 1U].value,
				sizeof(seed));
			if (!ClosestPointOnSurface(state->context->semantics,
					surface_record, seed, impact_point))
				goto invalid_surface;
		}
		else if (candidate <= 2U * surface_record->vertex_count)
		{
			uint32_t edge = candidate - surface_record->vertex_count - 1U;
			const float *start = state->context->semantics->hook_vertices[
				surface_record->first_vertex + edge].value;
			const float *end = state->context->semantics->hook_vertices[
				surface_record->first_vertex +
				(edge + 1U) % surface_record->vertex_count].value;
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				seed[axis] = (start[axis] + end[axis]) * 0.5f;
			if (!ClosestPointOnSurface(state->context->semantics,
					surface_record, seed, impact_point))
				goto invalid_surface;
		}
		else if (candidate <= 3U * surface_record->vertex_count)
		{
			uint32_t vertex = candidate -
				2U * surface_record->vertex_count - 1U;
			const float *point = state->context->semantics->hook_vertices[
				surface_record->first_vertex + vertex].value;
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				seed[axis] = (nearest[axis] + point[axis]) * 0.5f;
			if (!ClosestPointOnSurface(state->context->semantics,
					surface_record, seed, impact_point))
				goto invalid_surface;
		}
		else
		{
			float corner[3];
			uint32_t corner_index = candidate -
				3U * surface_record->vertex_count - 1U;
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				corner[axis] = (corner_index & (1U << axis)) != 0U ?
					state->query->target_bounds.maxs.value[axis] :
					state->query->target_bounds.mins.value[axis];
			if (!ClosestPointOnSurface(state->context->semantics,
					surface_record, corner, impact_point))
				goto invalid_surface;
		}
		if (!WithinRadius(state->query, state->effect_radius,
				impact_point, closest))
			continue;
		evaluated = EvaluateSurfaceCandidate(state->context, state->scene,
			state->query, state->profile, state->source_partition,
			state->target_binding, state->owner_status,
			state->owner_visibility, state->requested, surface, impact_point,
			state->affordance, state->error);
		if (evaluated == 0)
			return 0;
	}
	return 1;

invalid_surface:
	SetError(state->error, SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE,
		NULL);
	return 0;
}

static int VisitSurfaceBvh(sg_weapon_surface_query_state_t *state,
	uint32_t node_index)
{
	const sg_weapon_static_bvh_node_t *node;

	if (node_index == SG_STATIC_VISIBILITY_INDEX_NONE)
		return 1;
	state->affordance->spatial_nodes_visited++;
	node = &state->context->bvh_nodes[node_index];
	if (BoundsOutsideTargetRadius(&node->bounds, state->query,
			state->effect_radius))
		return 1;
	if (node->surface != SG_STATIC_VISIBILITY_INDEX_NONE)
		return ProcessSurface(state, node->surface);
	return VisitSurfaceBvh(state, node->left) &&
		VisitSurfaceBvh(state, node->right);
}

static int ResolveSurfaceRelations(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile, uint32_t source_partition,
	const sg_weapon_static_cell_binding_t *target_binding,
	sg_weapon_static_status_t owner_status,
	const sg_static_visibility_result_t *owner_visibility,
	sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	sg_weapon_surface_query_state_t state;
	size_t index;

	memset(&state, 0, sizeof(state));
	state.context = context;
	state.scene = scene;
	state.query = query;
	state.profile = profile;
	state.requested = query->requested_relations &
		affordance->allowed_relations &
		(SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH |
		 SG_WEAPON_STATIC_BOUNCE_SURFACE |
		 SG_WEAPON_STATIC_SECONDARY_BLAST_REACH);
	if (state.requested == 0U)
		return 1;
	state.source_partition = source_partition;
	state.target_binding = target_binding;
	state.owner_status = owner_status;
	state.owner_visibility = owner_visibility;
	state.effect_radius = EffectRadius(profile);
	state.affordance = affordance;
	state.error = error;
	if (!VisitSurfaceBvh(&state, context->bvh_root))
		return 0;
	for (index = 2U; index <= 5U; index++)
	{
		sg_weapon_static_relation_t relation = relation_order[index];
		sg_weapon_static_reason_t reason;

		if ((state.requested & relation) == 0U ||
			affordance->relations[index].status !=
				SG_WEAPON_STATIC_NOT_REQUESTED)
			continue;
		if (relation == SG_WEAPON_STATIC_BLAST_REACH &&
			!state.found_core_surface)
			reason = SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH;
		else if (relation == SG_WEAPON_STATIC_SECONDARY_BLAST_REACH &&
			!state.found_secondary_surface)
			reason = SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH;
		else if (!state.found_surface)
			reason = SG_WEAPON_STATIC_REASON_TARGET_NOT_SURFACE;
		else
			reason = SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE;
		SetRelation(&affordance->relations[index],
			reason == SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE ?
				SG_WEAPON_STATIC_CONDITIONAL : SG_WEAPON_STATIC_REJECTED,
			reason, NULL);
	}
	return 1;
}

static void FinalizeMasks(sg_weapon_static_affordance_t *affordance)
{
	size_t index;

	for (index = 0U; index < SG_WEAPON_STATIC_RELATION_COUNT; index++)
	{
		const sg_weapon_static_relation_result_t *relation =
			&affordance->relations[index];

		switch (relation->status)
		{
		case SG_WEAPON_STATIC_PROVEN:
			affordance->proven_relations |= relation->relation;
			break;
		case SG_WEAPON_STATIC_REJECTED:
			affordance->rejected_relations |= relation->relation;
			break;
		case SG_WEAPON_STATIC_CONDITIONAL:
			affordance->conditional_relations |= relation->relation;
			break;
		case SG_WEAPON_STATIC_NOT_REQUESTED:
			break;
		}
	}
}

int SG_WeaponStaticAffordanceResolve(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_law_input_t *law, sg_weapon_profile_id_t profile_id,
	sg_weapon_static_affordance_t *affordance_out,
	sg_weapon_static_affordance_error_t *error_out)
{
	sg_weapon_static_affordance_t affordance;
	sg_weapon_profile_t profile_storage;
	const sg_weapon_profile_t *profile = &profile_storage;
	const sg_host_collision_authority_t *published_authority;
	const sg_configuration_space_t *published_configuration;
	const sg_configuration_semantics_t *published_semantics;
	const sg_static_visibility_t *published_visibility;
	uint64_t published_revision;
	sg_static_visibility_result_t point_visibility;
	sg_static_visibility_result_t owner_visibility;
	sg_static_visibility_error_t visibility_error;
	sg_weapon_static_relation_t point_relations;
	sg_weapon_static_status_t owner_status = SG_WEAPON_STATIC_REJECTED;
	const sg_weapon_static_cell_binding_t *target_binding;
	uint32_t source_partition, target_partition;
	uint32_t pose_nodes_visited = 0U, pose_bounds_overlaps = 0U;
	uint32_t pose_face_tests = 0U;

	SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE, NULL);
	if (!context || !scene || !query || !law || !affordance_out)
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}
	if (SG_RuneV2ArtifactLoaderSnapshot(context->artifact_loader) !=
			context->artifact_snapshot ||
		!SG_StaticVisibilityPublicationRead(
			context->visibility_publication, &published_authority,
			&published_configuration, &published_semantics,
			&published_visibility, &published_revision) ||
		published_authority != context->authority ||
		published_configuration != context->configuration ||
		published_semantics != context->semantics ||
		published_visibility != context->visibility ||
		published_revision != context->binding.visibility_revision)
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH, NULL);
		return 0;
	}
	if (!BindingEqual(&context->binding, &query->binding))
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH, NULL);
		return 0;
	}
	if (!PreparedQueryValid(query))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY,
			NULL);
		return 0;
	}
	if (law->build_identity != context->model->identity.producer_identity ||
		law->physics_abi_id != context->model->identity.physics_abi_id)
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH,
			NULL);
		return 0;
	}
	if (!SG_WeaponProfileResolve(profile_id, law, &profile_storage))
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE, NULL);
		return 0;
	}
	if (!LocatePose(context, &query->source_cell, &query->source_phase,
			query->source_origin.value, &source_partition,
			&pose_nodes_visited, &pose_bounds_overlaps, &pose_face_tests) ||
		!LocatePose(context, &query->target_cell, &query->target_phase,
			query->target_origin.value, &target_partition,
			&pose_nodes_visited, &pose_bounds_overlaps, &pose_face_tests))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE,
			NULL);
		return 0;
	}
	if (!SG_StaticVisibilityQueryBoundPoints(context->authority, scene,
			context->configuration, context->semantics, context->visibility,
			source_partition, target_partition, query->source_origin.value,
			query->target_origin.value, &point_visibility, &visibility_error))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
			&visibility_error);
		return 0;
	}

	InitAffordance(&affordance, query, profile);
	point_relations = query->requested_relations &
		affordance.allowed_relations &
		(SG_WEAPON_STATIC_DIRECT_VISIBILITY |
		 SG_WEAPON_STATIC_PROJECTILE_CORRIDOR);
	if (point_relations != 0U)
	{
		sg_weapon_static_status_t status;

		status = VisibilityStatus(&point_visibility);
		if ((point_relations & SG_WEAPON_STATIC_DIRECT_VISIBILITY) != 0U)
			SetRelation(&affordance.relations[0], status,
				SG_WEAPON_STATIC_REASON_VISIBILITY, &point_visibility);
		if ((point_relations & SG_WEAPON_STATIC_PROJECTILE_CORRIDOR) != 0U)
		{
			SetRelation(&affordance.relations[1], status,
				SG_WEAPON_STATIC_REASON_VISIBILITY, &point_visibility);
			if (!RefineProjectileClearance(context->authority, scene, query,
					profile,
					&affordance.relations[1]))
			{
				SetError(error_out,
					SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
				return 0;
			}
		}
	}
	affordance.pose_partition_nodes_visited = pose_nodes_visited;
	affordance.pose_partition_bounds_overlaps = pose_bounds_overlaps;
	affordance.pose_partition_faces_tested = pose_face_tests;
	target_binding = FindCellBinding(context, &query->target_cell);
	if ((query->requested_relations & affordance.allowed_relations &
			SG_WEAPON_STATIC_SECONDARY_BLAST_REACH) != 0U)
	{
		if (!OwnerDamageVisibility(context, scene, query, target_binding,
				source_partition, &owner_status, &owner_visibility,
				&affordance.pose_partition_nodes_visited,
				&affordance.pose_partition_bounds_overlaps,
				&affordance.pose_partition_faces_tested))
		{
			SetError(error_out,
				SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
			return 0;
		}
	}
	else
		memset(&owner_visibility, 0, sizeof(owner_visibility));
	if (!ResolveSurfaceRelations(context, scene, query, profile,
			source_partition, target_binding, owner_status, &owner_visibility,
			&affordance, error_out))
		return 0;
	FinalizeMasks(&affordance);
	*affordance_out = affordance;
	return 1;
}

const char *SG_WeaponStaticAffordanceErrorString(
	sg_weapon_static_affordance_error_code_t code)
{
	switch (code)
	{
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE:
		return "none";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY:
		return "invalid query";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE:
		return "invalid profile";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE:
		return "invalid source";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY:
		return "static visibility query failed";
	}
	return "unknown static weapon-affordance error";
}
