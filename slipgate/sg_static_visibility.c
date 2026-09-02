#include "sg_static_visibility.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VISIBILITY_GEOMETRY_EPSILON 0.00001f
#define VISIBILITY_SURFACE_PROBE (1.0f / 32.0f)

typedef struct visibility_counts_s
{
	uint32_t occluder_count;
} visibility_counts_t;

static void SetError(sg_static_visibility_error_t *error,
	sg_static_visibility_error_code_t code, uint32_t source_index)
{
	if (!error)
		return;
	error->code = code;
	error->source_index = source_index;
}

static int Finite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static float Dot3(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
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

static int ChildValid(int32_t child, uint32_t node_count,
	uint32_t leaf_count)
{
	if (child >= 0)
		return (uint32_t)child < node_count;
	if (child == INT32_MIN)
		return 0;
	return (uint32_t)(-1 - child) < leaf_count;
}

static int SpanValid(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int VisibilityStreamValid(const sg_bsp_visibility_t *visibility,
	uint32_t offset, uint32_t row_bytes)
{
	uint32_t produced = 0;

	while (produced < row_bytes)
	{
		uint8_t value;

		if (offset >= visibility->byte_count)
			return 0;
		value = visibility->bytes[offset++];
		if (value)
		{
			produced++;
			continue;
		}
		if (offset >= visibility->byte_count || !visibility->bytes[offset] ||
			visibility->bytes[offset] > row_bytes - produced)
			return 0;
		produced += visibility->bytes[offset++];
	}
	return 1;
}

static uint32_t ReadU32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int ValidateWorld(const sg_bsp_world_t *world)
{
	uint32_t index, local, row_bytes, table_bytes;

	if (!world || !world->planes || !world->plane_count || !world->nodes ||
		!world->node_count || !world->leaves || !world->leaf_count ||
		!world->models || !world->model_count || !world->areas ||
		!world->area_count || (world->brush_count && !world->brushes) ||
		(world->brush_side_count && !world->brush_sides) ||
		(world->leaf_brush_count && !world->leaf_brushes) ||
		(world->texinfo_count && !world->texinfos) ||
		(world->areaportal_count && !world->areaportals))
		return 0;
	for (index = 0; index < world->node_count; index++)
		if (world->nodes[index].plane >= world->plane_count ||
			!ChildValid(world->nodes[index].children[0], world->node_count,
				world->leaf_count) ||
			!ChildValid(world->nodes[index].children[1], world->node_count,
				world->leaf_count))
			return 0;
	for (index = 0; index < world->leaf_count; index++)
		if (world->leaves[index].cluster < -1 ||
			(world->visibility.byte_count && world->leaves[index].cluster >= 0 &&
			 (uint32_t)world->leaves[index].cluster >=
				world->visibility.cluster_count) ||
			world->leaves[index].area >= world->area_count ||
			!SpanValid(world->leaves[index].first_leaf_brush,
				world->leaves[index].leaf_brush_count,
				world->leaf_brush_count))
			return 0;
	for (index = 0; index < world->leaf_brush_count; index++)
		if (world->leaf_brushes[index] >= world->brush_count)
			return 0;
	for (index = 0; index < world->brush_count; index++)
		if (!SpanValid(world->brushes[index].first_side,
				world->brushes[index].side_count, world->brush_side_count))
			return 0;
	for (index = 0; index < world->brush_side_count; index++)
		if (world->brush_sides[index].plane >= world->plane_count ||
			world->brush_sides[index].texinfo < -1 ||
			(world->brush_sides[index].texinfo >= 0 &&
			 (uint32_t)world->brush_sides[index].texinfo >=
				world->texinfo_count))
			return 0;
	for (index = 0; index < world->model_count; index++)
		if (!ChildValid(world->models[index].headnode, world->node_count,
				world->leaf_count))
			return 0;
	for (index = 0; index < world->area_count; index++)
		if (!SpanValid(world->areas[index].first_areaportal,
				world->areas[index].areaportal_count, world->areaportal_count))
			return 0;
	for (index = 0; index < world->areaportal_count; index++)
		if (world->areaportals[index].other_area >= world->area_count)
			return 0;
	if (!world->visibility.byte_count)
		return !world->visibility.cluster_count &&
			!world->visibility.bit_offsets && !world->visibility.bytes;
	if (!world->visibility.bytes ||
		world->visibility.cluster_count > SG_BSP_MAX_CLUSTERS ||
		(world->visibility.cluster_count &&
		 !world->visibility.bit_offsets) ||
		(!world->visibility.cluster_count &&
		 world->visibility.bit_offsets))
		return 0;
	if (world->visibility.byte_count < 4U ||
		world->visibility.cluster_count > (UINT32_MAX - 4U) / 8U)
		return 0;
	table_bytes = 4U + world->visibility.cluster_count * 8U;
	if (table_bytes > world->visibility.byte_count ||
		ReadU32(world->visibility.bytes) != world->visibility.cluster_count)
		return 0;
	row_bytes = (world->visibility.cluster_count + 7U) >> 3;
	for (index = 0; index < world->visibility.cluster_count; index++)
		for (local = 0; local < SG_BSP_VISIBILITY_SET_COUNT; local++)
		{
			uint32_t offset = world->visibility.bit_offsets[index][local];

			if (offset != ReadU32(world->visibility.bytes + 4U + index * 8U +
					local * 4U) || offset < table_bytes ||
				offset >= world->visibility.byte_count ||
				!VisibilityStreamValid(&world->visibility, offset, row_bytes))
				return 0;
		}
	return 1;
}

static int MarkModelBrushes(const sg_bsp_world_t *world, uint32_t model,
	uint8_t *brush_marks)
{
	uint8_t *node_marks;
	uint8_t *leaf_marks;
	int32_t *pending;
	uint32_t pending_count = 0;
	uint32_t pending_capacity;
	int result = 0;

	if (model >= world->model_count || world->node_count >
			UINT32_MAX - world->leaf_count)
		return 0;
	pending_capacity = world->node_count + world->leaf_count;
	if (!pending_capacity)
		return 0;
	node_marks = calloc(world->node_count, 1);
	leaf_marks = calloc(world->leaf_count, 1);
	pending = calloc(pending_capacity, sizeof(*pending));
	if (!node_marks || !leaf_marks || !pending)
		goto done;
	pending[pending_count++] = world->models[model].headnode;
	while (pending_count)
	{
		int32_t child = pending[--pending_count];
		uint32_t index, offset;

		if (child >= 0)
		{
			index = (uint32_t)child;
			if (index >= world->node_count)
				goto done;
			if (node_marks[index])
				continue;
			node_marks[index] = 1;
			if (pending_count > pending_capacity - 2U)
				goto done;
			pending[pending_count++] = world->nodes[index].children[1];
			pending[pending_count++] = world->nodes[index].children[0];
			continue;
		}
		if (child == INT32_MIN)
			goto done;
		index = (uint32_t)(-1 - child);
		if (index >= world->leaf_count)
			goto done;
		if (leaf_marks[index])
			continue;
		leaf_marks[index] = 1;
		for (offset = 0; offset < world->leaves[index].leaf_brush_count;
			offset++)
		{
			uint32_t brush = world->leaf_brushes[
				world->leaves[index].first_leaf_brush + offset];
			if (brush >= world->brush_count)
				goto done;
			brush_marks[brush] = 1;
		}
	}
	result = 1;

done:
	free(pending);
	free(leaf_marks);
	free(node_marks);
	return result;
}

static int OccludingBrush(const sg_bsp_brush_t *brush)
{
	uint32_t contents = (uint32_t)brush->contents;

	return brush->side_count &&
		(contents & (SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW)) != 0U;
}

static int ComputeCounts(const sg_bsp_world_t *world,
	visibility_counts_t *counts)
{
	uint8_t *marks;
	uint32_t model, brush;

	memset(counts, 0, sizeof(*counts));
	marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	if (!marks)
		return -1;
	for (model = 0; model < world->model_count; model++)
	{
		memset(marks, 0, world->brush_count);
		if (!MarkModelBrushes(world, model, marks))
		{
			free(marks);
			return -1;
		}
		for (brush = 0; brush < world->brush_count; brush++)
			if (marks[brush] && OccludingBrush(&world->brushes[brush]))
			{
				if (counts->occluder_count == UINT32_MAX)
				{
					free(marks);
					return 0;
				}
				counts->occluder_count++;
			}
	}
	free(marks);
	return 1;
}

static int HostLeafAtPoint(const sg_bsp_world_t *world, const float point[3],
	uint32_t *leaf_out)
{
	int32_t child = world->models[0].headnode;
	uint32_t traversed = 0;

	if (!Finite3(point))
		return 0;
	while (child >= 0)
	{
		const sg_bsp_node_t *node;
		const sg_bsp_plane_t *plane;
		float distance;

		if ((uint32_t)child >= world->node_count ||
			traversed++ >= world->node_count)
			return 0;
		node = &world->nodes[(uint32_t)child];
		if (node->plane >= world->plane_count)
			return 0;
		plane = &world->planes[node->plane];
		distance = Dot3(point, plane->normal.value) - plane->distance;
		child = node->children[distance < 0.0f];
	}
	if (child == INT32_MIN || (uint32_t)(-1 - child) >= world->leaf_count)
		return 0;
	*leaf_out = (uint32_t)(-1 - child);
	return 1;
}

static int ValidateSources(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics)
{

	if (!authority || !configuration || !semantics || !authority->world ||
		!IdentityEqual(&authority->identity, &configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		!ValidateWorld(authority->world))
		return 0;
	return 1;
}

static int AllocateArray(void **pointer, uint32_t count, size_t element_size)
{
	if (!count)
	{
		*pointer = NULL;
		return 1;
	}
	if ((size_t)count > SIZE_MAX / element_size)
		return 0;
	*pointer = calloc((size_t)count, element_size);
	return *pointer != NULL;
}

static int FillOccluders(const sg_bsp_world_t *world,
	sg_static_visibility_t *visibility)
{
	uint8_t *marks;
	uint32_t model, brush, occluder = 0;

	marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	if (!marks)
		return 0;
	for (model = 0; model < world->model_count; model++)
	{
		memset(marks, 0, world->brush_count);
		if (!MarkModelBrushes(world, model, marks))
		{
			free(marks);
			return 0;
		}
		for (brush = 0; brush < world->brush_count; brush++)
			if (marks[brush] && OccludingBrush(&world->brushes[brush]))
			{
				sg_static_visibility_occluder_t *destination =
					&visibility->occluders[occluder++];

				destination->model = model;
				destination->brush = brush;
				destination->contents =
					(uint32_t)world->brushes[brush].contents;
				destination->conditional = model != 0;
			}
	}
	free(marks);
	return occluder == visibility->occluder_count;
}

static uint32_t ComponentRoot(uint32_t *parents, uint32_t area)
{
	uint32_t root = area;

	while (parents[root] != root)
		root = parents[root];
	while (parents[area] != area)
	{
		uint32_t next = parents[area];
		parents[area] = root;
		area = next;
	}
	return root;
}

static int BuildAreaComponents(const sg_bsp_world_t *world,
	uint32_t *components)
{
	uint32_t *parents;
	uint32_t area, local;

	parents = calloc(world->area_count ? world->area_count : 1U,
		sizeof(*parents));
	if (!parents)
		return 0;
	for (area = 0; area < world->area_count; area++)
		parents[area] = area;
	for (area = 0; area < world->area_count; area++)
		for (local = 0; local < world->areas[area].areaportal_count; local++)
		{
			uint32_t other = world->areaportals[
				world->areas[area].first_areaportal + local].other_area;
			uint32_t left = ComponentRoot(parents, area);
			uint32_t right = ComponentRoot(parents, other);
			if (left != right)
			{
				uint32_t lower = left < right ? left : right;
				uint32_t higher = left < right ? right : left;
				parents[higher] = lower;
			}
		}
	for (area = 0; area < world->area_count; area++)
		components[area] = ComponentRoot(parents, area);
	free(parents);
	return 1;
}

static int SurfaceTargetLocation(const sg_bsp_world_t *world,
	const sg_configuration_hook_surface_t *surface, const float target[3],
	uint32_t *leaf_out, uint32_t *area_out, uint32_t *cluster_out)
{
	float point[3];
	uint32_t axis, leaf;

	for (axis = 0; axis < 3; axis++)
		point[axis] = target[axis] +
			surface->normal[axis] * VISIBILITY_SURFACE_PROBE;
	if (!HostLeafAtPoint(world, point, &leaf))
		return 0;
	if (((uint32_t)world->leaves[leaf].contents &
		(SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW)) != 0U)
		return 0;
	if (world->leaves[leaf].area >= world->area_count)
		return 0;
	*leaf_out = leaf;
	*area_out = world->leaves[leaf].area;
	*cluster_out = SG_STATIC_VISIBILITY_INDEX_NONE;
	if (world->leaves[leaf].cluster >= 0)
		*cluster_out = (uint32_t)world->leaves[leaf].cluster;
	if (world->visibility.byte_count &&
		*cluster_out != SG_STATIC_VISIBILITY_INDEX_NONE &&
		*cluster_out >= world->visibility.cluster_count)
		return 0;
	return 1;
}

void SG_StaticVisibilityDefaultLimits(
	sg_static_visibility_limits_t *limits_out)
{
	if (!limits_out)
		return;
	limits_out->max_partitions = UINT32_MAX;
	limits_out->max_areas = SG_BSP_MAX_AREAS;
	limits_out->max_occluders = UINT32_MAX;
	limits_out->max_surfaces = UINT32_MAX;
}

int SG_StaticVisibilityBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_limits_t *limits,
	sg_static_visibility_t **visibility_out,
	sg_static_visibility_error_t *error_out)
{
	const sg_bsp_world_t *world;
	sg_static_visibility_t *output = NULL;
	visibility_counts_t counts;
	int count_status;
	uint32_t index;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!authority || !configuration || !semantics || !limits ||
		!visibility_out || *visibility_out || !limits->max_partitions ||
		!limits->max_areas || !limits->max_occluders || !limits->max_surfaces)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!ValidateSources(authority, configuration, semantics))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	world = authority->world;
	count_status = ComputeCounts(world, &counts);
	if (count_status <= 0)
	{
		SetError(error_out, count_status < 0 ?
			SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY :
			SG_STATIC_VISIBILITY_ERROR_OVERFLOW,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (semantics->region_count > limits->max_partitions ||
		world->area_count > limits->max_areas ||
		counts.occluder_count > limits->max_occluders ||
		semantics->hook_surface_count > limits->max_surfaces)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_OVERFLOW,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	output = calloc(1, sizeof(*output));
	if (!output || !AllocateArray((void **)&output->partitions,
			semantics->region_count, sizeof(*output->partitions)) ||
		!AllocateArray((void **)&output->area_components, world->area_count,
			sizeof(*output->area_components)) ||
		!AllocateArray((void **)&output->occluders, counts.occluder_count,
			sizeof(*output->occluders)) ||
		!AllocateArray((void **)&output->surfaces,
			semantics->hook_surface_count, sizeof(*output->surfaces)))
	{
		SG_StaticVisibilityDestroy(output);
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	output->identity = authority->identity;
	output->partition_count = semantics->region_count;
	output->area_count = world->area_count;
	output->occluder_count = counts.occluder_count;
	output->surface_count = semantics->hook_surface_count;
	for (index = 0; index < output->partition_count; index++)
	{
		const sg_configuration_semantic_region_t *region =
			&semantics->regions[index];
		const sg_configuration_cell_t *cell =
			&configuration->cells[region->cell];
		sg_static_visibility_partition_t *partition =
			&output->partitions[index];

		partition->id = region->id;
		partition->configuration_region = index;
		partition->configuration_cell = region->cell;
		partition->bsp_leaf = cell->bsp_leaf.index;
		partition->bsp_area = cell->bsp_area.index;
		partition->bsp_cluster = cell->bsp_cluster.index;
	}
	if (!BuildAreaComponents(world, output->area_components))
	{
		SG_StaticVisibilityDestroy(output);
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!FillOccluders(world, output))
	{
		SG_StaticVisibilityDestroy(output);
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	for (index = 0; index < output->surface_count; index++)
	{
		const sg_configuration_hook_surface_t *source =
			&semantics->hook_surfaces[index];
		sg_static_visibility_surface_t *destination =
			&output->surfaces[index];

		destination->id = source->id;
		destination->semantic_surface = index;
		destination->model = source->model;
		destination->brush = source->brush;
		destination->brush_side = source->brush_side;
		destination->flags = source->flags;
	}
	*visibility_out = output;
	return 1;
}

static int AuditFailure(sg_static_visibility_audit_result_t *result,
	sg_static_visibility_audit_code_t code, uint32_t record)
{
	result->code = code;
	result->record = record;
	return 0;
}

static int AuditOccluders(const sg_bsp_world_t *world,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result)
{
	uint8_t *marks;
	uint32_t model, brush, record = 0;

	marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	if (!marks)
		return AuditFailure(result,
			SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
	for (model = 0; model < world->model_count; model++)
	{
		memset(marks, 0, world->brush_count);
		if (!MarkModelBrushes(world, model, marks))
		{
			free(marks);
			return AuditFailure(result,
				SG_STATIC_VISIBILITY_AUDIT_SOURCE_MISMATCH, model);
		}
		for (brush = 0; brush < world->brush_count; brush++)
			if (marks[brush] && OccludingBrush(&world->brushes[brush]))
			{
				const sg_static_visibility_occluder_t *actual =
					&visibility->occluders[record];

				if (actual->model != model || actual->brush != brush ||
					actual->contents != (uint32_t)world->brushes[brush].contents ||
					actual->conditional != (uint32_t)(model != 0))
				{
					free(marks);
					return AuditFailure(result,
						SG_STATIC_VISIBILITY_AUDIT_OCCLUDER_DISAGREEMENT,
						record);
				}
				record++;
			}
	}
	free(marks);
	return 1;
}

static int AuditAreaComponents(const sg_bsp_world_t *world,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result)
{
	uint8_t *visited;
	uint32_t *pending;
	uint32_t source;

	visited = calloc(world->area_count ? world->area_count : 1U, 1);
	pending = calloc(world->area_count ? world->area_count : 1U,
		sizeof(*pending));
	if (!visited || !pending)
	{
		free(pending);
		free(visited);
		return AuditFailure(result,
			SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
	}
	for (source = 0; source < world->area_count; source++)
	{
		uint32_t first = 0, count = 1, minimum = source;

		memset(visited, 0, world->area_count);
		visited[source] = 1;
		pending[0] = source;
		while (first < count)
		{
			uint32_t area = pending[first++];
			uint32_t owner, local;

			if (area < minimum)
				minimum = area;
			for (local = 0; local < world->areas[area].areaportal_count;
				local++)
			{
				uint32_t next = world->areaportals[
					world->areas[area].first_areaportal + local].other_area;
				if (!visited[next])
				{
					visited[next] = 1;
					pending[count++] = next;
				}
			}
			for (owner = 0; owner < world->area_count; owner++)
				for (local = 0;
					local < world->areas[owner].areaportal_count; local++)
					if (world->areaportals[
						world->areas[owner].first_areaportal + local].other_area ==
						area && !visited[owner])
					{
						visited[owner] = 1;
						pending[count++] = owner;
					}
		}
		if (visibility->area_components[source] != minimum)
		{
			free(pending);
			free(visited);
			return AuditFailure(result,
				SG_STATIC_VISIBILITY_AUDIT_OUTPUT_MUTATED, source);
		}
	}
	free(pending);
	free(visited);
	return 1;
}

int SG_StaticVisibilityAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result_out)
{
	const sg_bsp_world_t *world;
	visibility_counts_t counts;
	int count_status;
	uint32_t index;

	if (!result_out)
		return 0;
	memset(result_out, 0, sizeof(*result_out));
	result_out->record = SG_STATIC_VISIBILITY_INDEX_NONE;
	if (!authority || !configuration || !semantics || !visibility)
		return AuditFailure(result_out,
			SG_STATIC_VISIBILITY_AUDIT_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
	if (!ValidateSources(authority, configuration, semantics) ||
		!IdentityEqual(&authority->identity, &visibility->identity))
		return AuditFailure(result_out,
			SG_STATIC_VISIBILITY_AUDIT_SOURCE_MISMATCH,
			SG_STATIC_VISIBILITY_INDEX_NONE);
	world = authority->world;
	count_status = ComputeCounts(world, &counts);
	if (count_status <= 0)
		return AuditFailure(result_out,
			count_status < 0 ? SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY :
			SG_STATIC_VISIBILITY_AUDIT_SOURCE_MISMATCH,
			SG_STATIC_VISIBILITY_INDEX_NONE);
	result_out->reconstructed_partitions = semantics->region_count;
	result_out->reconstructed_areas = world->area_count;
	result_out->reconstructed_occluders = counts.occluder_count;
	result_out->reconstructed_surfaces = semantics->hook_surface_count;
	if (visibility->partition_count != semantics->region_count ||
		visibility->area_count != world->area_count ||
		visibility->occluder_count != counts.occluder_count ||
		visibility->surface_count != semantics->hook_surface_count ||
		(visibility->partition_count && !visibility->partitions) ||
		(visibility->area_count && !visibility->area_components) ||
		(visibility->occluder_count && !visibility->occluders) ||
		(visibility->surface_count && !visibility->surfaces))
		return AuditFailure(result_out,
			SG_STATIC_VISIBILITY_AUDIT_OUTPUT_MUTATED,
			SG_STATIC_VISIBILITY_INDEX_NONE);
	for (index = 0; index < visibility->partition_count; index++)
	{
		const sg_configuration_semantic_region_t *region =
			&semantics->regions[index];
		const sg_configuration_cell_t *cell =
			&configuration->cells[region->cell];
		const sg_static_visibility_partition_t *actual =
			&visibility->partitions[index];

		if (actual->id != region->id ||
			actual->configuration_region != index ||
			actual->configuration_cell != region->cell ||
			actual->bsp_leaf != cell->bsp_leaf.index ||
			actual->bsp_area != cell->bsp_area.index ||
			actual->bsp_cluster != cell->bsp_cluster.index)
			return AuditFailure(result_out,
				SG_STATIC_VISIBILITY_AUDIT_PARTITION_DISAGREEMENT, index);
	}
	if (!AuditAreaComponents(world, visibility, result_out))
		return 0;
	if (!AuditOccluders(world, visibility, result_out))
		return 0;
	for (index = 0; index < visibility->surface_count; index++)
	{
		const sg_configuration_hook_surface_t *source =
			&semantics->hook_surfaces[index];
		const sg_static_visibility_surface_t *actual =
			&visibility->surfaces[index];
		if (actual->id != source->id || actual->semantic_surface != index ||
			actual->model != source->model || actual->brush != source->brush ||
			actual->brush_side != source->brush_side ||
			actual->flags != source->flags)
			return AuditFailure(result_out,
				SG_STATIC_VISIBILITY_AUDIT_SURFACE_DISAGREEMENT, index);
	}
	result_out->code = SG_STATIC_VISIBILITY_AUDIT_OK;
	return 1;
}

static int BindingValidForQuery(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_error_t *error)
{
	if (!authority || !authority->world || !configuration || !semantics ||
		!visibility || !IdentityEqual(&authority->identity,
			&configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		!IdentityEqual(&authority->identity, &visibility->identity) ||
		visibility->partition_count != semantics->region_count ||
		visibility->area_count != authority->world->area_count ||
		visibility->surface_count != semantics->hook_surface_count ||
		(visibility->partition_count && !visibility->partitions) ||
		(visibility->area_count && !visibility->area_components) ||
		(visibility->surface_count && !visibility->surfaces))
	{
		SetError(error, SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	return 1;
}

static int PartitionValidForQuery(const sg_bsp_world_t *world,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t index,
	sg_static_visibility_error_t *error)
{
	const sg_static_visibility_partition_t *partition;
	const sg_configuration_semantic_region_t *region;
	const sg_configuration_cell_t *cell;
	const sg_bsp_leaf_t *leaf;
	uint32_t cluster;

	if (index >= visibility->partition_count)
		goto invalid;
	partition = &visibility->partitions[index];
	if (partition->configuration_region != index ||
		partition->configuration_cell >= configuration->cell_count ||
		partition->bsp_leaf >= world->leaf_count ||
		partition->bsp_area >= world->area_count)
		goto invalid;
	region = &semantics->regions[index];
	if (region->cell != partition->configuration_cell)
		goto invalid;
	cell = &configuration->cells[partition->configuration_cell];
	leaf = &world->leaves[partition->bsp_leaf];
	cluster = leaf->cluster < 0 ? SG_STATIC_VISIBILITY_INDEX_NONE :
		(uint32_t)leaf->cluster;
	if (partition->bsp_leaf != cell->bsp_leaf.index ||
		partition->bsp_area != cell->bsp_area.index ||
		partition->bsp_cluster != cell->bsp_cluster.index ||
		partition->bsp_area != leaf->area ||
		partition->bsp_cluster != cluster ||
		(world->visibility.byte_count &&
		 cluster != SG_STATIC_VISIBILITY_INDEX_NONE &&
		 cluster >= world->visibility.cluster_count) ||
		visibility->area_components[partition->bsp_area] >= world->area_count)
		goto invalid;
	return 1;

invalid:
	SetError(error, SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH, index);
	return 0;
}

static int SurfaceValidForQuery(
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t index,
	sg_static_visibility_error_t *error)
{
	const sg_configuration_hook_surface_t *source =
		&semantics->hook_surfaces[index];
	const sg_static_visibility_surface_t *surface =
		&visibility->surfaces[index];

	if (surface->id == source->id && surface->semantic_surface == index &&
		surface->model == source->model && surface->brush == source->brush &&
		surface->brush_side == source->brush_side &&
		surface->flags == source->flags)
		return 1;
	SetError(error, SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH, index);
	return 0;
}

static int PvsAllows(const sg_bsp_world_t *world,
	uint32_t source_cluster, uint32_t destination_cluster)
{
	uint32_t input, output = 0;
	uint32_t target_byte;
	uint8_t value = 0;

	if (!world->visibility.byte_count ||
		source_cluster == SG_STATIC_VISIBILITY_INDEX_NONE ||
		destination_cluster == SG_STATIC_VISIBILITY_INDEX_NONE)
		return 1;
	target_byte = destination_cluster >> 3;
	input = world->visibility.bit_offsets[source_cluster][0];
	while (output <= target_byte)
	{
		value = world->visibility.bytes[input++];
		if (value)
		{
			if (output++ == target_byte)
				break;
		}
		else
		{
			uint8_t run = world->visibility.bytes[input++];
			if (target_byte < output + run)
			{
				value = 0;
				break;
			}
			output += run;
		}
	}
	return (value &
		(UINT32_C(1) << (destination_cluster & 7U))) != 0U;
}

/* Returns 1 for the same area, 2 for an all-open conditional component, and 0
 * for areas in disconnected BSP components. */
static int AreaRelation(const sg_bsp_world_t *world,
	const sg_static_visibility_t *visibility, uint32_t source,
	uint32_t destination)
{
	if (source == destination)
		return 1;
	if (source >= world->area_count || destination >= world->area_count)
		return 0;
	return visibility->area_components[source] ==
		visibility->area_components[destination] ? 2 : 0;
}

static void InitResult(sg_static_visibility_result_t *result,
	uint32_t source, uint32_t destination)
{
	memset(result, 0, sizeof(*result));
	result->source_partition = source;
	result->destination_partition = destination;
	result->surface = SG_STATIC_VISIBILITY_INDEX_NONE;
	result->trace.fraction = 1.0f;
}

static int ClassifyRegions(const sg_bsp_world_t *world,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	uint32_t destination_partition, sg_static_visibility_result_t *result)
{
	const sg_static_visibility_partition_t *source =
		&visibility->partitions[source_partition];
	const sg_static_visibility_partition_t *destination =
		&visibility->partitions[destination_partition];
	int area_relation;

	InitResult(result, source_partition, destination_partition);
	if (!PvsAllows(world, source->bsp_cluster,
			destination->bsp_cluster))
	{
		result->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result->reason = SG_STATIC_VISIBILITY_REASON_PVS;
		return 1;
	}
	area_relation = AreaRelation(world, visibility, source->bsp_area,
		destination->bsp_area);
	if (!area_relation)
	{
		result->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result->reason = SG_STATIC_VISIBILITY_REASON_AREA_GRAPH;
		return 1;
	}
	if (source_partition == destination_partition)
	{
		result->classification = SG_STATIC_VISIBILITY_VISIBLE;
		return 1;
	}
	result->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
	result->reason = area_relation == 2 ?
		SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE :
		SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	result->requires_exact_ray = 1;
	result->requires_area_state = (uint32_t)(area_relation == 2);
	return 1;
}

int SG_StaticVisibilityQueryRegions(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	uint32_t destination_partition, sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!result_out || !visibility ||
		source_partition >= visibility->partition_count ||
		destination_partition >= visibility->partition_count)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!BindingValidForQuery(authority, configuration, semantics, visibility,
			error_out))
		return 0;
	if (!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, source_partition, error_out) ||
		!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, destination_partition, error_out))
		return 0;
	return ClassifyRegions(authority->world, visibility, source_partition,
		destination_partition, result_out);
}

static int PointInRegion(const sg_configuration_semantics_t *semantics,
	const sg_configuration_semantic_region_t *region, const float point[3])
{
	uint32_t axis, face;

	for (axis = 0; axis < 3; axis++)
		if (point[axis] < region->bounds.mins.value[axis] -
				VISIBILITY_GEOMETRY_EPSILON ||
			point[axis] > region->bounds.maxs.value[axis] +
				VISIBILITY_GEOMETRY_EPSILON)
			return 0;
	for (face = 0; face < region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *plane =
			&semantics->faces[region->first_face + face];
		if (!SG_ConfigurationSemanticFaceContainsPoint(plane, point))
			return 0;
	}
	return 1;
}

static int PartitionAt(const sg_configuration_semantics_t *semantics,
	const float point[3], uint32_t *partition_out)
{
	uint32_t partition;

	if (!Finite3(point))
		return 0;
	for (partition = 0; partition < semantics->region_count; partition++)
		if (PointInRegion(semantics, &semantics->regions[partition], point))
		{
			*partition_out = partition;
			return 1;
		}
	return 0;
}

int SG_StaticVisibilityPointInPartition(
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t partition_index,
	const float point[3], uint32_t *face_tests_out)
{
	const sg_static_visibility_partition_t *partition;
	const sg_configuration_semantic_region_t *region;

	if (face_tests_out)
		*face_tests_out = 0U;
	if (!semantics || !visibility || !point ||
		partition_index >= visibility->partition_count)
		return 0;
	partition = &visibility->partitions[partition_index];
	if (partition->configuration_region >= semantics->region_count)
		return 0;
	region = &semantics->regions[partition->configuration_region];
	if (face_tests_out)
		*face_tests_out = region->face_count;
	return Finite3(point) && PointInRegion(semantics, region, point);
}

static int TraceBlocked(const sg_host_collision_trace_t *trace)
{
	return trace->startsolid || trace->allsolid || trace->fraction < 1.0f;
}

static int ExactPointRay(const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const float source[3], const float destination[3],
	sg_static_visibility_result_t *result,
	sg_static_visibility_error_t *error)
{
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	sg_host_collision_trace_t world_trace;
	int needs_area = result->requires_area_state != 0U;

	if (!SG_HostCollisionTraceModel(authority, SG_HOST_COLLISION_MODEL_WORLD,
			NULL, source, zero, zero, destination,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &world_trace))
	{
		SetError(error, SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	result->trace = world_trace;
	result->requires_exact_ray = 0;
	if (TraceBlocked(&world_trace))
	{
		result->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result->reason = SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
		result->requires_area_state = 0;
		return 1;
	}
	if (scene->instance_count &&
		!SG_HostCollisionTrace(authority, scene, source, zero, zero,
			destination, SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW,
			&result->trace))
	{
		SetError(error, SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (TraceBlocked(&result->trace) &&
		(result->trace.model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		 result->trace.instance_id != 0U))
	{
		result->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
		result->reason = SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
		result->requires_area_state = (uint32_t)needs_area;
	}
	else if (TraceBlocked(&result->trace))
	{
		result->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result->reason = SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
		result->requires_area_state = 0;
	}
	else if (needs_area)
	{
		result->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
		result->reason = SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE;
	}
	else
	{
		result->classification = SG_STATIC_VISIBILITY_VISIBLE;
		result->reason = SG_STATIC_VISIBILITY_REASON_NONE;
	}
	return 1;
}

int SG_StaticVisibilityQueryPoints(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, const float source[3],
	const float destination[3], sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	uint32_t source_partition, destination_partition;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!scene || !source || !destination || !result_out || !visibility)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!BindingValidForQuery(authority, configuration, semantics, visibility,
			error_out))
		return 0;
	if (!PartitionAt(semantics, source, &source_partition) ||
		!PartitionAt(semantics, destination, &destination_partition))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, source_partition, error_out) ||
		!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, destination_partition, error_out))
		return 0;
	if (!ClassifyRegions(authority->world, visibility, source_partition,
			destination_partition, result_out))
		return 0;
	if (!result_out->requires_exact_ray &&
		(result_out->classification != SG_STATIC_VISIBILITY_VISIBLE ||
		 scene->instance_count == 0U))
		return 1;
	return ExactPointRay(authority, scene, source, destination, result_out,
		error_out);
}

int SG_StaticVisibilityQueryBoundPoints(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	uint32_t destination_partition, const float source[3],
	const float destination[3], sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!scene || !source || !destination || !result_out || !visibility ||
		source_partition >= visibility->partition_count ||
		destination_partition >= visibility->partition_count)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!BindingValidForQuery(authority, configuration, semantics, visibility,
			error_out))
		return 0;
	if (!SG_StaticVisibilityPointInPartition(semantics, visibility,
			source_partition, source, NULL) ||
		!SG_StaticVisibilityPointInPartition(semantics, visibility,
			destination_partition, destination, NULL))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, source_partition, error_out) ||
		!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, destination_partition, error_out))
		return 0;
	if (!ClassifyRegions(authority->world, visibility, source_partition,
			destination_partition, result_out))
		return 0;
	if (!result_out->requires_exact_ray &&
		(result_out->classification != SG_STATIC_VISIBILITY_VISIBLE ||
		 scene->instance_count == 0U))
		return 1;
	return ExactPointRay(authority, scene, source, destination, result_out,
		error_out);
}

static int PointOnSurface(const sg_configuration_semantics_t *semantics,
	const sg_configuration_hook_surface_t *surface, const float point[3])
{
	float sign = 0.0f;
	uint32_t vertex;

	if (!Finite3(point) || fabsf(Dot3(point, surface->normal) -
			surface->distance) > VISIBILITY_GEOMETRY_EPSILON)
		return 0;
	for (vertex = 0; vertex < surface->vertex_count; vertex++)
	{
		const float *first = semantics->hook_vertices[
			surface->first_vertex + vertex].value;
		const float *second = semantics->hook_vertices[surface->first_vertex +
			(vertex + 1U) % surface->vertex_count].value;
		float edge[3], relative[3], cross[3], side;

		edge[0] = second[0] - first[0];
		edge[1] = second[1] - first[1];
		edge[2] = second[2] - first[2];
		relative[0] = point[0] - first[0];
		relative[1] = point[1] - first[1];
		relative[2] = point[2] - first[2];
		cross[0] = edge[1] * relative[2] - edge[2] * relative[1];
		cross[1] = edge[2] * relative[0] - edge[0] * relative[2];
		cross[2] = edge[0] * relative[1] - edge[1] * relative[0];
		side = Dot3(cross, surface->normal);
		if (fabsf(side) <= VISIBILITY_GEOMETRY_EPSILON)
			continue;
		if (sign == 0.0f)
			sign = side;
		else if ((sign < 0.0f) != (side < 0.0f))
			return 0;
	}
	return 1;
}

static int TraceReachedSurface(const sg_host_collision_trace_t *trace,
	const sg_configuration_hook_surface_t *surface, const float target[3])
{
	float delta[3];
	float plane_dot = Dot3(trace->plane.normal, surface->normal);

	if (trace->fraction == 1.0f && !trace->startsolid && !trace->allsolid)
		return 1;
	delta[0] = trace->end[0] - target[0];
	delta[1] = trace->end[1] - target[1];
	delta[2] = trace->end[2] - target[2];
	return !trace->startsolid && !trace->allsolid && trace->model_index == 0 &&
		plane_dot > 0.9999f &&
		fabsf(trace->plane.distance - surface->distance) <=
			VISIBILITY_SURFACE_PROBE &&
		Dot3(delta, delta) <= 0.01f;
}

static int QuerySurfaceFromPartition(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	const float source[3], uint32_t surface_index, const float target[3],
	sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	const sg_configuration_hook_surface_t *surface;
	const sg_static_visibility_partition_t *source_record;
	sg_host_collision_trace_t world_trace, scene_trace;
	uint32_t target_leaf, target_area, target_cluster;
	int area_relation;

	if (!SurfaceValidForQuery(semantics, visibility, surface_index, error_out))
		return 0;
	surface = &semantics->hook_surfaces[surface_index];
	if (!PointOnSurface(semantics, surface, target))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY,
			surface_index);
		return 0;
	}
	InitResult(result_out, source_partition,
		SG_STATIC_VISIBILITY_INDEX_NONE);
	result_out->surface = surface_index;
	source_record = &visibility->partitions[source_partition];
	if (!PartitionValidForQuery(authority->world, configuration, semantics,
			visibility, source_partition, error_out))
		return 0;
	if (surface->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY)
	{
		result_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_SKY;
		return 1;
	}
	if (surface->flags & SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL)
	{
		result_out->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
		return 1;
	}
	if (!SurfaceTargetLocation(authority->world, surface, target, &target_leaf,
			&target_area, &target_cluster))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH,
			surface_index);
		return 0;
	}
	if (visibility->area_components[target_area] >= authority->world->area_count)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH,
			target_area);
		return 0;
	}
	if (!PvsAllows(authority->world, source_record->bsp_cluster,
			target_cluster))
	{
		result_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_PVS;
		return 1;
	}
	area_relation = AreaRelation(authority->world, visibility,
		source_record->bsp_area, target_area);
	if (!area_relation)
	{
		result_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_AREA_GRAPH;
		return 1;
	}
	if (!SG_HostCollisionTraceModel(authority, SG_HOST_COLLISION_MODEL_WORLD,
			NULL, source, zero, zero, target,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW,
			&world_trace))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	result_out->trace = world_trace;
	if (!TraceReachedSurface(&world_trace, surface, target))
	{
		result_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
		return 1;
	}
	if (scene->instance_count &&
		!SG_HostCollisionTrace(authority, scene, source, zero, zero, target,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &scene_trace))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (scene->instance_count && TraceBlocked(&scene_trace) &&
		(scene_trace.model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		 scene_trace.instance_id != 0U))
	{
		result_out->trace = scene_trace;
		result_out->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
		result_out->requires_area_state = (uint32_t)(area_relation == 2);
	}
	else if (area_relation == 2)
	{
		result_out->classification = SG_STATIC_VISIBILITY_CONDITIONAL;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE;
		result_out->requires_area_state = 1;
	}
	else
		result_out->classification = SG_STATIC_VISIBILITY_VISIBLE;
	return 1;
}

int SG_StaticVisibilityQuerySurface(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, const float source[3],
	uint32_t surface_index, const float target[3],
	sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	uint32_t source_partition;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!scene || !source || !target || !result_out || !visibility ||
		surface_index >= visibility->surface_count)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!BindingValidForQuery(authority, configuration, semantics, visibility,
			error_out))
		return 0;
	if (!PartitionAt(semantics, source, &source_partition))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY,
			surface_index);
		return 0;
	}
	return QuerySurfaceFromPartition(authority, scene, configuration,
		semantics, visibility, source_partition, source, surface_index, target,
		result_out, error_out);
}

int SG_StaticVisibilityQueryBoundSurface(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	const float source[3], uint32_t surface_index, const float target[3],
	sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!scene || !source || !target || !result_out || !visibility ||
		source_partition >= visibility->partition_count ||
		surface_index >= visibility->surface_count)
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
			SG_STATIC_VISIBILITY_INDEX_NONE);
		return 0;
	}
	if (!BindingValidForQuery(authority, configuration, semantics, visibility,
			error_out))
		return 0;
	if (!SG_StaticVisibilityPointInPartition(semantics, visibility,
			source_partition, source, NULL))
	{
		SetError(error_out, SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY,
			surface_index);
		return 0;
	}
	return QuerySurfaceFromPartition(authority, scene, configuration,
		semantics, visibility, source_partition, source, surface_index, target,
		result_out, error_out);
}

void SG_StaticVisibilityDestroy(sg_static_visibility_t *visibility)
{
	if (!visibility)
		return;
	free(visibility->partitions);
	free(visibility->area_components);
	free(visibility->occluders);
	free(visibility->surfaces);
	free(visibility);
}

const char *SG_StaticVisibilityErrorString(
	sg_static_visibility_error_code_t code)
{
	switch (code)
	{
	case SG_STATIC_VISIBILITY_ERROR_NONE: return "none";
	case SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE: return "invalid source";
	case SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH: return "source mismatch";
	case SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY: return "point outside audited regions";
	case SG_STATIC_VISIBILITY_ERROR_OVERFLOW: return "representation overflow";
	case SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY: return "out of memory";
	default: return "unknown";
	}
}

const char *SG_StaticVisibilityAuditCodeString(
	sg_static_visibility_audit_code_t code)
{
	switch (code)
	{
	case SG_STATIC_VISIBILITY_AUDIT_OK: return "ok";
	case SG_STATIC_VISIBILITY_AUDIT_INVALID_ARGUMENT: return "invalid argument";
	case SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY: return "out of memory";
	case SG_STATIC_VISIBILITY_AUDIT_SOURCE_MISMATCH: return "source mismatch";
	case SG_STATIC_VISIBILITY_AUDIT_OUTPUT_MUTATED: return "output mutated";
	case SG_STATIC_VISIBILITY_AUDIT_PARTITION_DISAGREEMENT: return "partition disagreement";
	case SG_STATIC_VISIBILITY_AUDIT_OCCLUDER_DISAGREEMENT: return "occluder disagreement";
	case SG_STATIC_VISIBILITY_AUDIT_SURFACE_DISAGREEMENT: return "surface disagreement";
	default: return "unknown";
	}
}
