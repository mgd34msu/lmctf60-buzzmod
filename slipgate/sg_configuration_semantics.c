#include "sg_configuration_semantics.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SEMANTICS_POINT_EPSILON 0.00001f
#define SEMANTICS_CONTENTS_DEADMONSTER UINT32_C(0x04000000)
#define SEMANTICS_GROUND_PROBE 0.25f
#define SEMANTICS_TRACE_EPSILON 0.03125f

typedef struct semantic_constraint_s
{
	sg_configuration_lattice_halfspace_t halfspace;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint8_t sample_index;
	uint8_t reversed;
} semantic_constraint_t;

typedef struct semantic_constraints_s
{
	semantic_constraint_t *items;
	uint32_t count;
	uint32_t capacity;
} semantic_constraints_t;

typedef struct semantic_support_decision_s
{
	float normal[3];
	float distance;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
} semantic_support_decision_t;

typedef struct semantic_build_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_t *output;
	uint32_t region_capacity;
	uint32_t face_capacity;
	uint32_t vertex_capacity;
	uint32_t boundary_capacity;
	uint32_t hook_surface_capacity;
	uint32_t hook_vertex_capacity;
	semantic_support_decision_t *support_decisions;
	uint32_t support_decision_count;
	uint32_t support_decision_capacity;
	sg_configuration_semantics_error_t error;
} semantic_build_t;

static float Dot(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Copy3(float out[3], const float in[3])
{
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
}

static int Finite3(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static void SetError(semantic_build_t *build,
	sg_configuration_semantics_error_code_t code, uint32_t source)
{
	if (build->error.code == SG_CONFIGURATION_SEMANTICS_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source;
	}
}

static int Grow(void **array, uint32_t *capacity, uint32_t required,
	uint32_t limit, size_t size)
{
	uint32_t next;
	void *grown;

	if (required <= *capacity)
		return 1;
	if (required > limit || (size_t)required > SIZE_MAX / size)
		return 0;
	next = *capacity ? *capacity : 64U;
	if (next > limit)
		next = limit;
	while (next < required)
	{
		if (next > limit / 2U)
		{
			next = limit;
			break;
		}
		next *= 2U;
	}
	grown = realloc(*array, (size_t)next * size);
	if (!grown)
		return 0;
	*array = grown;
	*capacity = next;
	return 1;
}

static int AppendConstraint(semantic_constraints_t *constraints,
	const semantic_constraint_t *constraint)
{
	semantic_constraint_t *grown;
	uint32_t next;

	if (constraints->count == UINT32_MAX)
		return 0;
	if (constraints->count == constraints->capacity)
	{
		next = constraints->capacity ? constraints->capacity * 2U : 32U;
		if (next < constraints->capacity)
			return 0;
		grown = realloc(constraints->items, (size_t)next * sizeof(*grown));
		if (!grown)
			return 0;
		constraints->items = grown;
		constraints->capacity = next;
	}
	constraints->items[constraints->count++] = *constraint;
	return 1;
}

static int CopyConstraints(const semantic_constraints_t *source,
	semantic_constraints_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	if (!source->count)
		return 1;
	destination->items = malloc((size_t)source->count *
		sizeof(*destination->items));
	if (!destination->items)
		return 0;
	memcpy(destination->items, source->items,
		(size_t)source->count * sizeof(*destination->items));
	destination->count = source->count;
	destination->capacity = source->count;
	return 1;
}

static void FreeConstraints(semantic_constraints_t *constraints)
{
	free(constraints->items);
	memset(constraints, 0, sizeof(*constraints));
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
		memcmp(&left->physics, &right->physics,
			sizeof(left->physics)) == 0;
}

static float HullMinimum(const sg_rune_hull_profile_t *hull,
	const float normal[3]);
static int MarkModelBrushes(const sg_bsp_world_t *world, uint32_t model_index,
	uint8_t *brush_marks);
static int AuditReachableBrushes(const sg_bsp_world_t *world,
	uint32_t model, uint8_t *brush_marks);
static int HostLeafAtPoint(const sg_bsp_world_t *world, const float point[3],
	uint32_t *leaf_out);
static int ValidateCellMesh(const sg_configuration_space_t *configuration,
	uint32_t cell_index);

static int ValidateSource(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	sg_configuration_semantics_error_t *error)
{
	uint32_t cell_index;

	if (!authority->world->models || !authority->world->model_count ||
		(configuration->cell_count && !configuration->cells) ||
		(configuration->face_count && !configuration->faces) ||
		(configuration->vertex_count && !configuration->vertices))
		return 0;
	if (configuration->domain.mins.value[0] !=
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
		configuration->domain.mins.value[1] !=
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
		configuration->domain.mins.value[2] !=
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
		configuration->domain.maxs.value[0] !=
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX ||
		configuration->domain.maxs.value[1] !=
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX ||
		configuration->domain.maxs.value[2] !=
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX)
		return 0;
	for (cell_index = 0; cell_index < configuration->cell_count; cell_index++)
	{
		const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
		const sg_bsp_leaf_t *leaf;
		uint32_t local;
		float vertex_mins[3] = { INFINITY, INFINITY, INFINITY };
		float vertex_maxs[3] = { -INFINITY, -INFINITY, -INFINITY };

		if (cell->stance >= SG_RUNE_STANCE_COUNT || cell->face_count < 4U ||
			cell->first_face > configuration->face_count ||
			cell->face_count > configuration->face_count - cell->first_face ||
			cell->bsp_leaf.index >= authority->world->leaf_count ||
			!Finite3(cell->bounds.mins.value) ||
			!Finite3(cell->bounds.maxs.value) ||
			!Finite3(cell->interior_witness.value))
		{
			error->source_index = cell_index;
			return 0;
		}
		leaf = &authority->world->leaves[cell->bsp_leaf.index];
		if (cell->bsp_area.index != leaf->area ||
			cell->bsp_cluster.index !=
				(leaf->cluster < 0 ? UINT32_MAX : (uint32_t)leaf->cluster) ||
			cell->contents != SG_HostCollisionRuneContents(
				(sg_host_collision_contents_t)leaf->contents) ||
			!HostLeafAtPoint(authority->world, cell->interior_witness.value,
				&local) || local != cell->bsp_leaf.index)
		{
			error->source_index = cell_index;
			return 0;
		}
		for (local = 0; local < cell->face_count; local++)
		{
			uint32_t face_index = cell->first_face + local;
			const sg_configuration_face_t *face =
				&configuration->faces[face_index];
			uint32_t vertex;
			if (!Finite3(face->plane.normal) ||
				!isfinite(face->plane.distance) ||
				Dot(face->plane.normal, face->plane.normal) <= 0.0f ||
				face->vertex_count < 3U ||
				face->first_vertex > configuration->vertex_count ||
				face->vertex_count > configuration->vertex_count -
					face->first_vertex ||
				face->plane.reversed > 1U ||
				face->plane.source_kind >
					SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			{
				error->source_index = face_index;
				return 0;
			}
			if (face->plane.source_kind == SG_CONFIGURATION_PLANE_BSP)
			{
				const sg_bsp_plane_t *source;
				float sign = face->plane.reversed ? -1.0f : 1.0f;
				if (face->plane.source_index >= authority->world->plane_count ||
					face->plane.source_variant != 0U)
					return 0;
				source = &authority->world->planes[face->plane.source_index];
				if (face->plane.normal[0] != sign * source->normal.value[0] ||
					face->plane.normal[1] != sign * source->normal.value[1] ||
					face->plane.normal[2] != sign * source->normal.value[2] ||
					face->plane.distance != sign * source->distance)
					return 0;
			}
			else if (face->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
			{
				uint32_t axis = face->plane.source_index;
				uint32_t variant = face->plane.source_variant;
				float normal[3] = { 0, 0, 0 };
				float distance;
				if (axis >= 3U || variant != axis * 2U +
					(face->plane.reversed ? 1U : 0U))
					return 0;
				normal[axis] = face->plane.reversed ? -1.0f : 1.0f;
				distance = face->plane.reversed ?
					-configuration->domain.mins.value[axis] :
					configuration->domain.maxs.value[axis];
				if (face->plane.normal[0] != normal[0] ||
					face->plane.normal[1] != normal[1] ||
					face->plane.normal[2] != normal[2] ||
					face->plane.distance != distance)
					return 0;
			}
			else if (face->plane.source_kind ==
				SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			{
				const sg_rune_hull_profile_t *hull =
					cell->stance == SG_RUNE_STANCE_STANDING ?
					&authority->identity.standing_hull :
					&authority->identity.crouching_hull;
				const sg_bsp_brush_side_t *side;
				const sg_bsp_plane_t *source;
				float distance, sign = face->plane.reversed ? -1.0f : 1.0f;
				if (face->plane.source_index >= authority->world->brush_side_count ||
					face->plane.source_variant != (uint32_t)cell->stance)
					return 0;
				side = &authority->world->brush_sides[face->plane.source_index];
				if (side->plane >= authority->world->plane_count)
					return 0;
				source = &authority->world->planes[side->plane];
				distance = source->distance - HullMinimum(hull,
					source->normal.value);
				if (face->plane.normal[0] != sign * source->normal.value[0] ||
					face->plane.normal[1] != sign * source->normal.value[1] ||
					face->plane.normal[2] != sign * source->normal.value[2] ||
					face->plane.distance != sign * distance)
					return 0;
			}
			for (vertex = face->first_vertex;
				vertex < face->first_vertex + face->vertex_count; vertex++)
				if (!Finite3(configuration->vertices[vertex].value))
				{
					error->source_index = face_index;
					return 0;
				}
				else
				{
					uint32_t axis;
					for (axis = 0; axis < 3U; axis++)
					{
						float value = configuration->vertices[vertex].value[axis];
						if (value < vertex_mins[axis])
							vertex_mins[axis] = value;
						if (value > vertex_maxs[axis])
							vertex_maxs[axis] = value;
					}
				}
		}
		for (local = 0; local < cell->face_count; local++)
		{
			const sg_configuration_face_t *face =
				&configuration->faces[cell->first_face + local];
			uint32_t vertex;
			if (Dot(cell->interior_witness.value, face->plane.normal) >=
				face->plane.distance)
				return 0;
			for (vertex = face->first_vertex;
				vertex < face->first_vertex + face->vertex_count; vertex++)
			{
				const float *point = configuration->vertices[vertex].value;
				uint32_t other, prior;
				if (fabsf(Dot(point, face->plane.normal) - face->plane.distance) >
					SEMANTICS_POINT_EPSILON * 4.0f)
					return 0;
				for (other = 0; other < cell->face_count; other++)
				{
					const sg_configuration_face_t *halfspace =
						&configuration->faces[cell->first_face + other];
					if (Dot(point, halfspace->plane.normal) -
						halfspace->plane.distance > SEMANTICS_POINT_EPSILON)
						return 0;
				}
				for (prior = face->first_vertex; prior < vertex; prior++)
					if (fabsf(configuration->vertices[prior].value[0] - point[0]) <=
							SEMANTICS_POINT_EPSILON &&
						fabsf(configuration->vertices[prior].value[1] - point[1]) <=
							SEMANTICS_POINT_EPSILON &&
						fabsf(configuration->vertices[prior].value[2] - point[2]) <=
							SEMANTICS_POINT_EPSILON)
						return 0;
			}
		}
		for (local = 0; local < 3U; local++)
			if (fabsf(cell->bounds.mins.value[local] - vertex_mins[local]) >
					SEMANTICS_POINT_EPSILON ||
				fabsf(cell->bounds.maxs.value[local] - vertex_maxs[local]) >
					SEMANTICS_POINT_EPSILON)
			{
				error->source_index = cell_index;
				return 0;
			}
		if (!ValidateCellMesh(configuration, cell_index))
			return 0;
	}
	return 1;
}

static int SolveInterior(const semantic_constraints_t *constraints,
	int32_t q8[3], sg_configuration_lattice_stats_t *stats)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	uint32_t index;
	int positive = 0;
	int result;

	if (!constraints->count)
		return 0;
	halfspaces = malloc((size_t)constraints->count * sizeof(*halfspaces));
	clearance = malloc(constraints->count);
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		return -2;
	}
	for (index = 0; index < constraints->count; index++)
	{
		halfspaces[index] = constraints->items[index].halfspace;
		clearance[index] = 1;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraints->count, NULL, q8, &positive, stats);
	if (result > 0 && !positive)
	{
		for (index = 0; index < constraints->count; index++)
			halfspaces[index].open = 1;
		result = SG_ConfigurationLatticeFind(halfspaces, constraints->count,
			NULL, q8, stats);
	}
	free(clearance);
	free(halfspaces);
	return result;
}

static int BuildSolveInterior(semantic_build_t *build,
	const semantic_constraints_t *constraints, int32_t q8[3])
{
	sg_configuration_lattice_stats_t stats = { 0 };
	int result = SolveInterior(constraints, q8, &stats);

	build->output->lattice_solve_calls += stats.solve_calls;
	build->output->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		build->output->lattice_maximum_binary_shift)
		build->output->lattice_maximum_binary_shift =
			stats.maximum_binary_shift;
	return result;
}

static int AddCellConstraints(const sg_configuration_space_t *configuration,
	uint32_t cell_index, semantic_constraints_t *constraints)
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	uint32_t local;

	for (local = 0; local < cell->face_count; local++)
	{
		uint32_t face_index = cell->first_face + local;
		const sg_configuration_face_t *face = &configuration->faces[face_index];
		semantic_constraint_t constraint;

		memset(&constraint, 0, sizeof(constraint));
		Copy3(constraint.halfspace.normal, face->plane.normal);
		constraint.halfspace.distance = face->plane.distance;
		constraint.source_kind = SG_CONFIGURATION_SEMANTIC_PLANE_CELL;
		constraint.source_index = face_index;
		constraint.reversed = (uint8_t)(face->plane.reversed != 0U);
		if (!AppendConstraint(constraints, &constraint))
			return 0;
	}
	return 1;
}

static int SampleOffsets(const sg_host_collision_authority_t *authority,
	sg_rune_stance_t stance, float offsets[3])
{
	const sg_rune_hull_profile_t *hull;
	float view_height, sample_height;
	int sample2;

	if (stance == SG_RUNE_STANCE_STANDING)
	{
		hull = &authority->identity.standing_hull;
		view_height = 22.0f;
	}
	else if (stance == SG_RUNE_STANCE_CROUCHING)
	{
		hull = &authority->identity.crouching_hull;
		view_height = -2.0f;
	}
	else
		return 0;
	sample_height = view_height - hull->mins.value[2];
	if (!isfinite(sample_height) || sample_height < (float)INT32_MIN ||
		sample_height >= (float)INT32_MAX)
		return 0;
	sample2 = (int)sample_height;
	offsets[0] = hull->mins.value[2] + 1.0f;
	offsets[1] = hull->mins.value[2] + (float)(sample2 / 2);
	offsets[2] = hull->mins.value[2] + (float)sample2;
	return 1;
}

static float HullMinimum(const sg_rune_hull_profile_t *hull,
	const float normal[3])
{
	float result = 0.0f;
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		result += normal[axis] < 0.0f ?
			normal[axis] * hull->maxs.value[axis] :
			normal[axis] * hull->mins.value[axis];
	return result;
}

static int AppendSupportDecision(semantic_build_t *build,
	const float normal[3], float distance,
	uint32_t source_kind, uint32_t source_index, uint32_t source_variant)
{
	semantic_support_decision_t *decisions = build->support_decisions;
	uint32_t count = build->support_decision_count;
	uint32_t index;

	if (!Finite3(normal) || !isfinite(distance) ||
		Dot(normal, normal) <= FLT_EPSILON)
		return 1;
	for (index = 0; index < count; index++)
		if (decisions[index].normal[0] == normal[0] &&
			decisions[index].normal[1] == normal[1] &&
			decisions[index].normal[2] == normal[2] &&
			decisions[index].distance == distance)
			return 1;
	if (count == UINT32_MAX)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW,
			source_index);
		return 0;
	}
	if (!Grow((void **)&build->support_decisions,
		&build->support_decision_capacity, count + 1U, UINT32_MAX,
		sizeof(*build->support_decisions)))
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
			source_index);
		return 0;
	}
	decisions = build->support_decisions;
	Copy3(decisions[count].normal, normal);
	decisions[count].distance = distance;
	decisions[count].source_kind = source_kind;
	decisions[count].source_index = source_index;
	decisions[count].source_variant = source_variant;
	build->support_decision_count++;
	return 1;
}

static float BoundsMinimum(const sg_rune_bounds_t *bounds,
	const float normal[3])
{
	float value = 0.0f;
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
		value += normal[axis] < 0.0f ?
			normal[axis] * bounds->maxs.value[axis] :
			normal[axis] * bounds->mins.value[axis];
	return value;
}

static int BuildSupportDecisions(semantic_build_t *build,
	sg_rune_stance_t stance, uint32_t cell_index)
{
	const sg_bsp_world_t *world = build->authority->world;
	const sg_rune_bounds_t *bounds =
		&build->configuration->cells[cell_index].bounds;
	const sg_rune_hull_profile_t *hull =
		stance == SG_RUNE_STANCE_STANDING ?
		&build->authority->identity.standing_hull :
		&build->authority->identity.crouching_hull;
	semantic_support_decision_t *entering = NULL;
	semantic_support_decision_t *leaving = NULL;
	uint8_t *brush_marks = NULL;
	uint32_t entering_count = 0, entering_capacity = 0;
	uint32_t leaving_count = 0, leaving_capacity = 0;
	uint32_t brush, side_index, first, second;

	free(build->support_decisions);
	build->support_decisions = NULL;
	build->support_decision_count = 0;
	build->support_decision_capacity = 0;
	brush_marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	if (!brush_marks)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
			cell_index);
		return 0;
	}
	if (!MarkModelBrushes(world, 0, brush_marks))
		goto invalid_source;
	for (brush = 0; brush < world->brush_count; brush++)
	{
		const sg_bsp_brush_t *record = &world->brushes[brush];
		uint32_t contents = (uint32_t)record->contents;
		uint32_t side_offset;
		int potentially_reachable = 1;

		if (!brush_marks[brush] || !record->side_count ||
			!(contents & SG_HOST_MASK_PLAYER_SOLID))
			continue;
		for (side_offset = 0; side_offset < record->side_count; side_offset++)
		{
			const sg_bsp_brush_side_t *side;
			const sg_bsp_plane_t *plane;
			float expanded_distance, reachable_minimum;
			side_index = record->first_side + side_offset;
			if (side_index >= world->brush_side_count)
				goto invalid_source;
			side = &world->brush_sides[side_index];
			if (side->plane >= world->plane_count)
				goto invalid_source;
			plane = &world->planes[side->plane];
			expanded_distance = plane->distance -
				HullMinimum(hull, plane->normal.value);
			reachable_minimum = BoundsMinimum(bounds, plane->normal.value);
			if (plane->normal.value[2] > 0.0f)
				reachable_minimum -= plane->normal.value[2] *
					SEMANTICS_GROUND_PROBE;
			if (reachable_minimum > expanded_distance +
				SEMANTICS_TRACE_EPSILON)
			{
				potentially_reachable = 0;
				break;
			}
		}
		if (!potentially_reachable)
			continue;
		for (side_offset = 0; side_offset < record->side_count; side_offset++)
		{
			const sg_bsp_brush_side_t *side;
			const sg_bsp_plane_t *plane;
			float expanded_distance;
			semantic_support_decision_t candidate;

			side_index = record->first_side + side_offset;
			if (side_index >= world->brush_side_count)
				goto invalid_source;
			side = &world->brush_sides[side_index];
			if (side->plane >= world->plane_count)
				goto invalid_source;
			plane = &world->planes[side->plane];
			expanded_distance = plane->distance -
				HullMinimum(hull, plane->normal.value);
			if (!AppendSupportDecision(build, plane->normal.value,
				expanded_distance, SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_START,
				side_index, brush))
				goto failure;
			if (plane->normal.value[2] < 0.0f)
			{
				if (!AppendSupportDecision(build, plane->normal.value,
					expanded_distance - SEMANTICS_TRACE_EPSILON,
					SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_LEAVE_ZERO,
					side_index, brush) ||
					!AppendSupportDecision(build, plane->normal.value,
						expanded_distance + plane->normal.value[2] *
							SEMANTICS_GROUND_PROBE,
						SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_LEAVE_END,
						side_index, brush) || leaving_count == UINT32_MAX ||
					!Grow((void **)&leaving, &leaving_capacity,
						leaving_count + 1U, UINT32_MAX, sizeof(*leaving)))
				{
					if (build->error.code == SG_CONFIGURATION_SEMANTICS_ERROR_NONE)
						SetError(build, leaving_count == UINT32_MAX ?
							SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW :
							SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
							side_index);
					goto failure;
				}
				memset(&candidate, 0, sizeof(candidate));
				Copy3(candidate.normal, plane->normal.value);
				candidate.distance = expanded_distance;
				candidate.source_index = side_index;
				candidate.source_variant = brush;
				leaving[leaving_count++] = candidate;
				continue;
			}
			if (plane->normal.value[2] == 0.0f)
				continue;
			if (!AppendSupportDecision(build, plane->normal.value,
				expanded_distance +
					plane->normal.value[2] * SEMANTICS_GROUND_PROBE +
					SEMANTICS_TRACE_EPSILON,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_REACH,
					side_index, brush) ||
				!AppendSupportDecision(build, plane->normal.value,
					expanded_distance + SEMANTICS_TRACE_EPSILON,
					SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_ENTER_ZERO,
					side_index, brush))
				goto failure;
			if (entering_count == UINT32_MAX ||
				!Grow((void **)&entering, &entering_capacity,
					entering_count + 1U, UINT32_MAX, sizeof(*entering)))
			{
				SetError(build, entering_count == UINT32_MAX ?
					SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW :
					SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
					side_index);
				goto failure;
			}
			memset(&candidate, 0, sizeof(candidate));
			Copy3(candidate.normal, plane->normal.value);
			candidate.distance = expanded_distance;
			candidate.source_index = side_index;
			candidate.source_variant = brush;
			entering[entering_count++] = candidate;
		}
	}
	for (first = 0; first < entering_count; first++)
		for (second = first + 1U; second < entering_count; second++)
		{
			float normal[3];
			float distance;
			uint32_t axis;
			for (axis = 0; axis < 3; axis++)
				normal[axis] = entering[second].normal[2] *
					entering[first].normal[axis] -
					entering[first].normal[2] *
					entering[second].normal[axis];
			distance = entering[second].normal[2] *
				(entering[first].distance + SEMANTICS_TRACE_EPSILON) -
				entering[first].normal[2] *
				(entering[second].distance + SEMANTICS_TRACE_EPSILON);
			if (!AppendSupportDecision(build, normal, distance,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_ORDER,
				entering[first].source_index, entering[second].source_index))
				goto failure;
		}
	for (first = 0; first < entering_count; first++)
		for (second = 0; second < leaving_count; second++)
		{
			float normal[3];
			float distance;
			uint32_t axis;
			if (entering[first].source_variant != leaving[second].source_variant)
				continue;
			for (axis = 0; axis < 3U; axis++)
				normal[axis] = entering[first].normal[2] *
					leaving[second].normal[axis] - leaving[second].normal[2] *
					entering[first].normal[axis];
			distance = entering[first].normal[2] *
				(leaving[second].distance - SEMANTICS_TRACE_EPSILON) -
				leaving[second].normal[2] *
				(entering[first].distance + SEMANTICS_TRACE_EPSILON);
			if (!AppendSupportDecision(build, normal, distance,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP,
				entering[first].source_index, leaving[second].source_index))
				goto failure;
		}
	free(brush_marks);
	free(entering);
	free(leaving);
	return 1;

invalid_source:
	SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
		cell_index);
failure:
	free(brush_marks);
	free(entering);
	free(leaving);
	return 0;
}

static int HostLeafAtPoint(const sg_bsp_world_t *world, const float point[3],
	uint32_t *leaf_out)
{
	int32_t child = world->models[0].headnode;

	while (child >= 0)
	{
		const sg_bsp_node_t *node;
		const sg_bsp_plane_t *plane;
		float distance;
		if ((uint32_t)child >= world->node_count)
			return 0;
		node = &world->nodes[child];
		if (node->plane >= world->plane_count)
			return 0;
		plane = &world->planes[node->plane];
		distance = Dot(point, plane->normal.value) - plane->distance;
		child = node->children[distance < 0.0f];
	}
	*leaf_out = (uint32_t)(-1 - child);
	return *leaf_out < world->leaf_count;
}

static int AddBspSideConstraint(const sg_bsp_plane_t *plane, float offset,
	uint32_t plane_index, uint8_t sample, int front,
	semantic_constraints_t *constraints)
{
	semantic_constraint_t constraint;

	memset(&constraint, 0, sizeof(constraint));
	constraint.source_kind = SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE;
	constraint.source_index = plane_index;
	constraint.sample_index = sample;
	constraint.reversed = (uint8_t)front;
	if (front)
	{
		constraint.halfspace.normal[0] = -plane->normal.value[0];
		constraint.halfspace.normal[1] = -plane->normal.value[1];
		constraint.halfspace.normal[2] = -plane->normal.value[2];
		constraint.halfspace.distance = -plane->distance +
			plane->normal.value[2] * offset;
	}
	else
	{
		Copy3(constraint.halfspace.normal, plane->normal.value);
		constraint.halfspace.distance = plane->distance -
			plane->normal.value[2] * offset;
	}
	return AppendConstraint(constraints, &constraint);
}

static int PointInside(const float point[3],
	const semantic_constraints_t *constraints)
{
	uint32_t index;

	for (index = 0; index < constraints->count; index++)
		if (Dot(point, constraints->items[index].halfspace.normal) -
			constraints->items[index].halfspace.distance >
			SEMANTICS_POINT_EPSILON)
			return 0;
	return 1;
}

static int Intersect3(const semantic_constraint_t *a,
	const semantic_constraint_t *b, const semantic_constraint_t *c,
	float point[3])
{
	double n0[3], n1[3], n2[3];
	double cross12[3], cross20[3], cross01[3], determinant;
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
	{
		n0[axis] = a->halfspace.normal[axis];
		n1[axis] = b->halfspace.normal[axis];
		n2[axis] = c->halfspace.normal[axis];
	}
	cross12[0] = n1[1] * n2[2] - n1[2] * n2[1];
	cross12[1] = n1[2] * n2[0] - n1[0] * n2[2];
	cross12[2] = n1[0] * n2[1] - n1[1] * n2[0];
	determinant = n0[0] * cross12[0] + n0[1] * cross12[1] +
		n0[2] * cross12[2];
	if (!isfinite(determinant) || fabs(determinant) <= DBL_EPSILON)
		return 0;
	cross20[0] = n2[1] * n0[2] - n2[2] * n0[1];
	cross20[1] = n2[2] * n0[0] - n2[0] * n0[2];
	cross20[2] = n2[0] * n0[1] - n2[1] * n0[0];
	cross01[0] = n0[1] * n1[2] - n0[2] * n1[1];
	cross01[1] = n0[2] * n1[0] - n0[0] * n1[2];
	cross01[2] = n0[0] * n1[1] - n0[1] * n1[0];
	for (axis = 0; axis < 3; axis++)
		point[axis] = (float)(((double)a->halfspace.distance * cross12[axis] +
			(double)b->halfspace.distance * cross20[axis] +
			(double)c->halfspace.distance * cross01[axis]) / determinant);
	return Finite3(point);
}

static int SamePoint(const sg_rune_vec3_t *point, const float value[3])
{
	return fabsf(point->value[0] - value[0]) <= SEMANTICS_POINT_EPSILON &&
		fabsf(point->value[1] - value[1]) <= SEMANTICS_POINT_EPSILON &&
		fabsf(point->value[2] - value[2]) <= SEMANTICS_POINT_EPSILON;
}

static int SameHalfspace(const sg_configuration_lattice_halfspace_t *left,
	const sg_configuration_lattice_halfspace_t *right)
{
	return left->normal[0] == right->normal[0] &&
		left->normal[1] == right->normal[1] &&
		left->normal[2] == right->normal[2] &&
		left->distance == right->distance;
}

static int ValidateCellMesh(const sg_configuration_space_t *configuration,
	uint32_t cell_index)
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	sg_rune_vec3_t *points = NULL;
	uint32_t point_count = 0, point_capacity = 0;
	uint32_t first, second, third, local;
	int valid = 0;

	for (first = 0; first < cell->face_count; first++)
		for (second = first + 1U; second < cell->face_count; second++)
			for (third = second + 1U; third < cell->face_count; third++)
			{
				semantic_constraint_t constraints[3];
				float point[3];
				uint32_t face, existing;
				memset(constraints, 0, sizeof(constraints));
				Copy3(constraints[0].halfspace.normal,
					configuration->faces[cell->first_face + first].plane.normal);
				constraints[0].halfspace.distance =
					configuration->faces[cell->first_face + first].plane.distance;
				Copy3(constraints[1].halfspace.normal,
					configuration->faces[cell->first_face + second].plane.normal);
				constraints[1].halfspace.distance =
					configuration->faces[cell->first_face + second].plane.distance;
				Copy3(constraints[2].halfspace.normal,
					configuration->faces[cell->first_face + third].plane.normal);
				constraints[2].halfspace.distance =
					configuration->faces[cell->first_face + third].plane.distance;
				if (!Intersect3(&constraints[0], &constraints[1], &constraints[2],
					point))
					continue;
				for (face = 0; face < cell->face_count; face++)
				{
					const sg_configuration_face_t *halfspace =
						&configuration->faces[cell->first_face + face];
					if (Dot(point, halfspace->plane.normal) -
						halfspace->plane.distance > SEMANTICS_POINT_EPSILON)
						break;
				}
				if (face != cell->face_count)
					continue;
				for (existing = 0; existing < point_count; existing++)
					if (SamePoint(&points[existing], point))
						break;
				if (existing != point_count)
					continue;
				if (point_count == UINT32_MAX ||
					!Grow((void **)&points, &point_capacity, point_count + 1U,
						UINT32_MAX, sizeof(*points)))
					goto done;
				Copy3(points[point_count++].value, point);
			}
	if (point_count < 4U)
		goto done;
	for (local = 0; local < cell->face_count; local++)
	{
		const sg_configuration_face_t *face =
			&configuration->faces[cell->first_face + local];
		uint32_t expected = 0, point;
		for (point = 0; point < point_count; point++)
			if (fabsf(Dot(points[point].value, face->plane.normal) -
				face->plane.distance) <= SEMANTICS_POINT_EPSILON * 4.0f)
			{
				uint32_t actual;
				expected++;
				for (actual = face->first_vertex;
					actual < face->first_vertex + face->vertex_count; actual++)
					if (SamePoint(&configuration->vertices[actual],
						points[point].value))
						break;
				if (actual == face->first_vertex + face->vertex_count)
					goto done;
			}
		if (expected != face->vertex_count)
			goto done;
	}
	valid = 1;

done:
	free(points);
	return valid;
}

static uint32_t DominantAxis(const float normal[3])
{
	uint32_t axis = 0, candidate;
	for (candidate = 1; candidate < 3; candidate++)
		if (fabsf(normal[candidate]) > fabsf(normal[axis]))
			axis = candidate;
	return axis;
}

static float FaceAngle(const sg_rune_vec3_t *point, uint32_t axis,
	const float center[3])
{
	uint32_t u = (axis + 1U) % 3U;
	uint32_t v = (axis + 2U) % 3U;
	return atan2f(point->value[v] - center[v],
		point->value[u] - center[u]);
}

static int BuildMesh(semantic_build_t *build,
	const semantic_constraints_t *constraints,
	sg_configuration_semantic_region_t *region)
{
	sg_rune_vec3_t *all = NULL;
	uint32_t all_count = 0, all_capacity = 0;
	uint32_t i, j, k;
	sg_rune_vec3_t *face_points = NULL;
	float bounds_min[3] = { INFINITY, INFINITY, INFINITY };
	float bounds_max[3] = { -INFINITY, -INFINITY, -INFINITY };

	for (i = 0; i < constraints->count; i++)
		for (j = i + 1U; j < constraints->count; j++)
			for (k = j + 1U; k < constraints->count; k++)
			{
				float point[3];
				uint32_t seen, axis;

				if (!Intersect3(&constraints->items[i], &constraints->items[j],
						&constraints->items[k], point) ||
					!PointInside(point, constraints))
					continue;
				for (seen = 0; seen < all_count; seen++)
					if (SamePoint(&all[seen], point))
						break;
				if (seen != all_count)
					continue;
				if (all_count == UINT32_MAX ||
					!Grow((void **)&all, &all_capacity, all_count + 1U,
					build->limits.max_vertices, sizeof(*all)))
				{
					free(all);
					SetError(build, all_count >= build->limits.max_vertices ?
						SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW :
						SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, region->cell);
					return 0;
				}
				Copy3(all[all_count++].value, point);
				for (axis = 0; axis < 3; axis++)
				{
					if (point[axis] < bounds_min[axis]) bounds_min[axis] = point[axis];
					if (point[axis] > bounds_max[axis]) bounds_max[axis] = point[axis];
				}
			}
	if (all_count < 4U)
	{
		free(all);
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_NONFINITE_GEOMETRY,
			region->cell);
		return 0;
	}
	Copy3(region->bounds.mins.value, bounds_min);
	Copy3(region->bounds.maxs.value, bounds_max);
	region->first_face = build->output->face_count;
	for (i = 0; i < constraints->count; i++)
	{
		uint32_t point_count = 0, point_capacity = 0, v, axis;
		float center[3] = { 0, 0, 0 };
		sg_configuration_semantic_face_t *face;
		uint32_t prior;

		for (prior = 0; prior < i; prior++)
			if (SameHalfspace(&constraints->items[prior].halfspace,
				&constraints->items[i].halfspace))
				break;
		if (prior != i)
			continue;

		for (v = 0; v < all_count; v++)
			if (fabsf(Dot(all[v].value,
				constraints->items[i].halfspace.normal) -
				constraints->items[i].halfspace.distance) <=
				SEMANTICS_POINT_EPSILON * 4.0f)
			{
				if (point_count == UINT32_MAX ||
					!Grow((void **)&face_points, &point_capacity, point_count + 1U,
					build->limits.max_vertices, sizeof(*face_points)))
				{
					SetError(build,
						point_count >= build->limits.max_vertices ?
						SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW :
						SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
						region->cell);
					goto mesh_failure;
				}
				face_points[point_count++] = all[v];
			}
		if (point_count < 3U)
		{
			free(face_points);
			face_points = NULL;
			continue;
		}
		for (v = 0; v < point_count; v++)
			for (axis = 0; axis < 3; axis++) center[axis] += face_points[v].value[axis];
		for (axis = 0; axis < 3; axis++) center[axis] /= (float)point_count;
		axis = DominantAxis(constraints->items[i].halfspace.normal);
		for (v = 1; v < point_count; v++)
		{
			sg_rune_vec3_t value = face_points[v];
			float angle = FaceAngle(&value, axis, center);
			uint32_t insert = v;
			while (insert && FaceAngle(&face_points[insert - 1U], axis, center) > angle)
			{
				face_points[insert] = face_points[insert - 1U];
				insert--;
			}
			face_points[insert] = value;
		}
		if (build->output->face_count >= build->limits.max_faces ||
			point_count > build->limits.max_vertices -
				build->output->vertex_count ||
			point_count > UINT32_MAX - build->output->vertex_count)
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW,
				region->cell);
			goto mesh_failure;
		}
		if (
			!Grow((void **)&build->output->faces, &build->face_capacity,
			build->output->face_count + 1U, build->limits.max_faces,
			sizeof(*build->output->faces)) ||
			!Grow((void **)&build->output->vertices, &build->vertex_capacity,
			build->output->vertex_count + point_count,
			build->limits.max_vertices, sizeof(*build->output->vertices)))
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
				region->cell);
			goto mesh_failure;
		}
		face = &build->output->faces[build->output->face_count++];
		memset(face, 0, sizeof(*face));
		Copy3(face->normal, constraints->items[i].halfspace.normal);
		face->distance = constraints->items[i].halfspace.distance;
		face->first_vertex = build->output->vertex_count;
		face->vertex_count = point_count;
		face->source_kind = constraints->items[i].source_kind;
		face->source_index = constraints->items[i].source_index;
		face->source_variant = constraints->items[i].source_variant;
		face->sample_index = constraints->items[i].sample_index;
		face->reversed = constraints->items[i].reversed;
		memcpy(&build->output->vertices[build->output->vertex_count], face_points,
			(size_t)point_count * sizeof(*face_points));
		build->output->vertex_count += point_count;
		free(face_points);
		face_points = NULL;
	}
	region->face_count = build->output->face_count - region->first_face;
	free(all);
	return region->face_count >= 4U;

mesh_failure:
	free(face_points);
	free(all);
	if (build->error.code == SG_CONFIGURATION_SEMANTICS_ERROR_NONE)
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
			region->cell);
	return 0;
}

static int AppendRegion(semantic_build_t *build, uint32_t cell_index,
	const semantic_constraints_t *constraints, const uint32_t leaves[3],
	const int32_t witness_q8[3])
{
	const sg_bsp_world_t *world = build->authority->world;
	const sg_configuration_cell_t *cell = &build->configuration->cells[cell_index];
	sg_configuration_semantic_region_t *region;
	sg_host_collision_pose_t pose;
	float witness[3];
	float offsets[3];
	uint32_t sample;

	if (build->output->region_count >= build->limits.max_regions)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW, cell_index);
		return 0;
	}
	if (!Grow((void **)&build->output->regions, &build->region_capacity,
		build->output->region_count + 1U, build->limits.max_regions,
		sizeof(*build->output->regions)))
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, cell_index);
		return 0;
	}
	region = &build->output->regions[build->output->region_count];
	memset(region, 0, sizeof(*region));
	region->id = ((uint64_t)cell_index << 32) | build->output->region_count;
	region->cell = cell_index;
	if (cell->bsp_leaf.index >= world->leaf_count)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
			cell_index);
		return 0;
	}
	region->origin_contents =
		(sg_host_collision_contents_t)world->leaves[cell->bsp_leaf.index].contents;
	region->origin_rune_contents = cell->contents;
	for (sample = 0; sample < 3; sample++)
	{
		region->sample_leaves[sample] = leaves[sample];
		region->sample_contents[sample] =
			(sg_host_collision_contents_t)world->leaves[leaves[sample]].contents;
		region->sample_areas[sample] = world->leaves[leaves[sample]].area;
		region->sample_clusters[sample] = world->leaves[leaves[sample]].cluster;
	}
	if (region->sample_contents[0] & SG_HOST_MASK_WATER)
	{
		region->water_level = 1;
		region->water_type = region->sample_contents[0];
		if (region->sample_contents[1] & SG_HOST_MASK_WATER)
		{
			region->water_level = 2;
			if (region->sample_contents[2] & SG_HOST_MASK_WATER)
				region->water_level = 3;
		}
	}
	if (region->water_type & SG_HOST_CONTENTS_WATER)
		region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_WATER;
	if (region->water_type & SG_HOST_CONTENTS_LAVA)
		region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	if (region->water_type & SG_HOST_CONTENTS_SLIME)
		region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_SLIME |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	for (sample = 0; sample < 3; sample++)
		witness[sample] = (float)witness_q8[sample] * 0.125f;
	Copy3(region->interior_witness.value, witness);
	if (!SampleOffsets(build->authority, cell->stance, offsets))
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
			cell_index);
		return 0;
	}
	for (sample = 0; sample < 3; sample++)
	{
		float point[3];
		uint32_t host_leaf;
		Copy3(point, witness);
		point[2] += offsets[sample];
		if (!HostLeafAtPoint(world, point, &host_leaf) ||
			host_leaf != leaves[sample])
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_HOST_DISAGREEMENT,
				cell_index);
			return 0;
		}
	}
	if (!SG_HostCollisionClassifyPose(build->authority, NULL, witness,
		cell->stance, &pose) || !pose.valid ||
		pose.water_level != region->water_level ||
		pose.water_type != region->water_type)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_HOST_DISAGREEMENT,
			cell_index);
		return 0;
	}
	region->flags |= pose.supported ?
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED :
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	if (!BuildMesh(build, constraints, region))
		return 0;
	for (sample = 0; sample < region->face_count; sample++)
	{
		const sg_configuration_semantic_face_t *semantic_face =
			&build->output->faces[region->first_face + sample];
		if (semantic_face->source_kind ==
			SG_CONFIGURATION_SEMANTIC_PLANE_CELL &&
			semantic_face->source_index < build->configuration->face_count &&
			build->configuration->faces[semantic_face->source_index].plane.source_kind ==
				SG_CONFIGURATION_PLANE_DOMAIN)
			region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT;
	}
	build->output->region_count++;
	return 1;
}

static int PartitionSupport(semantic_build_t *build, uint32_t cell_index,
	uint32_t decision_index, semantic_constraints_t *constraints,
	const uint32_t leaves[3])
{
	const semantic_support_decision_t *decision;
	int side;

	if (decision_index == build->support_decision_count)
	{
		int32_t witness[3];
		int feasible = BuildSolveInterior(build, constraints, witness);
		if (feasible < 0)
		{
			SetError(build, feasible == -2 ?
				SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY :
				SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER, cell_index);
			return 0;
		}
		return !feasible || AppendRegion(build, cell_index, constraints,
			leaves, witness);
	}
	decision = &build->support_decisions[decision_index];
	for (side = 0; side < 2; side++)
	{
		semantic_constraints_t branch;
		semantic_constraint_t constraint;
		int32_t witness[3];
		int feasible;

		if (!CopyConstraints(constraints, &branch))
			goto allocation_failure;
		memset(&constraint, 0, sizeof(constraint));
		constraint.source_kind = decision->source_kind;
		constraint.source_index = decision->source_index;
		constraint.source_variant = decision->source_variant;
		constraint.reversed = (uint8_t)(side == 0);
		if (side == 0)
		{
			constraint.halfspace.normal[0] = -decision->normal[0];
			constraint.halfspace.normal[1] = -decision->normal[1];
			constraint.halfspace.normal[2] = -decision->normal[2];
			constraint.halfspace.distance = -decision->distance;
		}
		else
		{
			Copy3(constraint.halfspace.normal, decision->normal);
			constraint.halfspace.distance = decision->distance;
		}
		if (!AppendConstraint(&branch, &constraint))
		{
			FreeConstraints(&branch);
			goto allocation_failure;
		}
		feasible = BuildSolveInterior(build, &branch, witness);
		if (feasible < 0)
		{
			FreeConstraints(&branch);
			SetError(build, feasible == -2 ?
				SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY :
				SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER, cell_index);
			return 0;
		}
		if (feasible && !PartitionSupport(build, cell_index,
			decision_index + 1U, &branch, leaves))
		{
			FreeConstraints(&branch);
			return 0;
		}
		FreeConstraints(&branch);
	}
	return 1;

allocation_failure:
	SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, cell_index);
	return 0;
}

static int PartitionSample(semantic_build_t *build, uint32_t cell_index,
	uint8_t sample, int32_t node, const float offsets[3],
	semantic_constraints_t *constraints, uint32_t leaves[3]);

static int ContinueSamples(semantic_build_t *build, uint32_t cell_index,
	uint8_t sample, semantic_constraints_t *constraints, uint32_t leaves[3],
	const float offsets[3])
{
	int32_t witness[3];
	int feasible;

	if (sample < 3U)
		return PartitionSample(build, cell_index, sample,
			build->authority->world->models[0].headnode, offsets,
			constraints, leaves);
	feasible = BuildSolveInterior(build, constraints, witness);
	if (feasible < 0)
	{
		SetError(build, feasible == -2 ?
			SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY :
			SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER, cell_index);
		return 0;
	}
	return !feasible || PartitionSupport(build, cell_index, 0,
		constraints, leaves);
}

static int PartitionSample(semantic_build_t *build, uint32_t cell_index,
	uint8_t sample, int32_t node, const float offsets[3],
	semantic_constraints_t *constraints, uint32_t leaves[3])
{
	const sg_bsp_world_t *world = build->authority->world;
	const sg_bsp_node_t *record;
	const sg_bsp_plane_t *plane;
	int side;

	if (node < 0)
	{
		uint32_t leaf = (uint32_t)(-1 - node);
		if (leaf >= world->leaf_count)
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE, leaf);
			return 0;
		}
		leaves[sample] = leaf;
		return ContinueSamples(build, cell_index, (uint8_t)(sample + 1U),
			constraints, leaves, offsets);
	}
	if ((uint32_t)node >= world->node_count)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
			(uint32_t)node);
		return 0;
	}
	record = &world->nodes[node];
	if (record->plane >= world->plane_count)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
			record->plane);
		return 0;
	}
	plane = &world->planes[record->plane];
	for (side = 0; side < 2; side++)
	{
		semantic_constraints_t branch;
		int32_t witness[3];
		int feasible;

		if (!CopyConstraints(constraints, &branch) ||
			!AddBspSideConstraint(plane, offsets[sample], record->plane,
				sample, side == 0, &branch))
		{
			FreeConstraints(&branch);
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
				cell_index);
			return 0;
		}
		feasible = BuildSolveInterior(build, &branch, witness);
		if (feasible < 0)
		{
			FreeConstraints(&branch);
			SetError(build, feasible == -2 ?
				SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY :
				SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER, cell_index);
			return 0;
		}
		if (feasible && !PartitionSample(build, cell_index, sample,
			record->children[side], offsets, &branch, leaves))
		{
			FreeConstraints(&branch);
			return 0;
		}
		FreeConstraints(&branch);
	}
	return 1;
}

static int FindBrushForSide(const sg_bsp_world_t *world, uint32_t side,
	uint32_t *brush_out)
{
	uint32_t brush, found = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;

	for (brush = 0; brush < world->brush_count; brush++)
	{
		uint32_t first = world->brushes[brush].first_side;
		uint32_t count = world->brushes[brush].side_count;
		if (side >= first && side - first < count)
		{
			if (found != SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
				return 0;
			found = brush;
		}
	}
	if (found == SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
		return 0;
	*brush_out = found;
	return 1;
}

static int BuildBoundaries(semantic_build_t *build)
{
	const sg_bsp_world_t *world = build->authority->world;
	uint32_t cell_index;

	for (cell_index = 0; cell_index < build->configuration->cell_count;
		cell_index++)
	{
		const sg_configuration_cell_t *cell =
			&build->configuration->cells[cell_index];
		uint32_t local;

		for (local = 0; local < cell->face_count; local++)
		{
			uint32_t face_index = cell->first_face + local;
			const sg_configuration_face_t *face =
				&build->configuration->faces[face_index];
			sg_configuration_boundary_t boundary;

			memset(&boundary, 0, sizeof(boundary));
			boundary.id = ((uint64_t)cell_index << 32) | local;
			boundary.cell = cell_index;
			boundary.configuration_face = face_index;
			boundary.brush = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
			boundary.brush_side = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
			boundary.texinfo = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
			Copy3(boundary.origin_normal, face->plane.normal);
			boundary.origin_distance = face->plane.distance;
			if (face->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
				boundary.flags = SG_CONFIGURATION_BOUNDARY_VOID;
			else if (face->plane.source_kind ==
				SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			{
				uint32_t brush;
				const sg_bsp_brush_side_t *side;

				if (face->plane.source_index >= world->brush_side_count ||
					!FindBrushForSide(world, face->plane.source_index, &brush))
				{
					SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
						face_index);
					return 0;
				}
				side = &world->brush_sides[face->plane.source_index];
				if (side->plane >= world->plane_count)
				{
					SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
						face->plane.source_index);
					return 0;
				}
				Copy3(boundary.surface_normal,
					world->planes[side->plane].normal.value);
				boundary.surface_distance = world->planes[side->plane].distance;
				boundary.brush = brush;
				boundary.brush_side = face->plane.source_index;
				boundary.flags = SG_CONFIGURATION_BOUNDARY_PHYSICAL;
				if (boundary.surface_normal[2] >= 0.7f)
					boundary.flags |=
						SG_CONFIGURATION_BOUNDARY_SUPPORT_CANDIDATE;
				if (side->texinfo >= 0)
				{
					if ((uint32_t)side->texinfo >= world->texinfo_count)
					{
						SetError(build,
							SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
							face->plane.source_index);
						return 0;
					}
					boundary.texinfo = (uint32_t)side->texinfo;
					boundary.surface_flags =
						world->texinfos[side->texinfo].flags;
				}
			}
			else
				continue;
			if (build->output->boundary_count >= build->limits.max_boundaries)
			{
				SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW,
					face_index);
				return 0;
			}
			if (!Grow((void **)&build->output->boundaries,
				&build->boundary_capacity, build->output->boundary_count + 1U,
				build->limits.max_boundaries,
				sizeof(*build->output->boundaries)))
			{
				SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
					face_index);
				return 0;
			}
			build->output->boundaries[build->output->boundary_count++] = boundary;
		}
	}
	return 1;
}

static int MarkModelBrushes(const sg_bsp_world_t *world, uint32_t model_index,
	uint8_t *brush_marks)
{
	int32_t *stack = NULL;
	uint32_t stack_count = 0, stack_capacity = 0;
	uint8_t *node_marks = NULL;
	int result = 0;

	node_marks = calloc(world->node_count ? world->node_count : 1U, 1);
	if (!node_marks || !Grow((void **)&stack, &stack_capacity, 1U,
		UINT32_MAX, sizeof(*stack)))
		goto done;
	stack[stack_count++] = world->models[model_index].headnode;
	while (stack_count)
	{
		int32_t child = stack[--stack_count];
		if (child < 0)
		{
			uint32_t leaf_index = (uint32_t)(-1 - child);
			const sg_bsp_leaf_t *leaf;
			uint32_t offset;
			if (leaf_index >= world->leaf_count)
				goto done;
			leaf = &world->leaves[leaf_index];
			if (leaf->first_leaf_brush > world->leaf_brush_count ||
				leaf->leaf_brush_count > world->leaf_brush_count -
					leaf->first_leaf_brush)
				goto done;
			for (offset = 0; offset < leaf->leaf_brush_count; offset++)
			{
				uint32_t brush = world->leaf_brushes[
					leaf->first_leaf_brush + offset];
				if (brush >= world->brush_count)
					goto done;
				brush_marks[brush] = 1;
			}
			continue;
		}
		if ((uint32_t)child >= world->node_count)
			goto done;
		if (node_marks[child])
			continue;
		node_marks[child] = 1;
		if (stack_count > UINT32_MAX - 2U ||
			!Grow((void **)&stack, &stack_capacity, stack_count + 2U,
				UINT32_MAX, sizeof(*stack)))
			goto done;
		stack[stack_count++] = world->nodes[child].children[1];
		stack[stack_count++] = world->nodes[child].children[0];
	}
	result = 1;

done:
	free(node_marks);
	free(stack);
	return result;
}

static int BuildHookPolygon(const sg_bsp_world_t *world,
	const sg_bsp_brush_t *brush, uint32_t target_side,
	sg_rune_vec3_t **points_out, uint32_t *point_count_out)
{
	semantic_constraints_t constraints = { 0 };
	sg_rune_vec3_t *points = NULL;
	uint32_t point_count = 0, point_capacity = 0;
	uint32_t first, second, third, side_offset, axis;
	float center[3] = { 0, 0, 0 };
	uint32_t dominant;

	*points_out = NULL;
	*point_count_out = 0;
	for (side_offset = 0; side_offset < brush->side_count; side_offset++)
	{
		uint32_t side_index = brush->first_side + side_offset;
		const sg_bsp_brush_side_t *side;
		const sg_bsp_plane_t *plane;
		semantic_constraint_t constraint;
		if (side_index >= world->brush_side_count)
			goto failure;
		side = &world->brush_sides[side_index];
		if (side->plane >= world->plane_count)
			goto failure;
		plane = &world->planes[side->plane];
		memset(&constraint, 0, sizeof(constraint));
		Copy3(constraint.halfspace.normal, plane->normal.value);
		constraint.halfspace.distance = plane->distance;
		if (!AppendConstraint(&constraints, &constraint))
			goto failure;
	}
	for (first = 0; first < constraints.count; first++)
		for (second = first + 1U; second < constraints.count; second++)
			for (third = second + 1U; third < constraints.count; third++)
			{
				float point[3];
				uint32_t existing;
				if (!Intersect3(&constraints.items[first],
					&constraints.items[second], &constraints.items[third], point) ||
					!PointInside(point, &constraints) ||
					fabsf(Dot(point, constraints.items[target_side].halfspace.normal) -
						constraints.items[target_side].halfspace.distance) >
						SEMANTICS_POINT_EPSILON * 4.0f)
					continue;
				for (existing = 0; existing < point_count; existing++)
					if (SamePoint(&points[existing], point))
						break;
				if (existing != point_count)
					continue;
				if (point_count == UINT32_MAX)
					goto overflow;
				if (!Grow((void **)&points, &point_capacity, point_count + 1U,
					UINT32_MAX, sizeof(*points)))
					goto failure;
				Copy3(points[point_count++].value, point);
			}
	if (point_count < 3U)
	{
		free(points);
		FreeConstraints(&constraints);
		return 1;
	}
	for (first = 0; first < point_count; first++)
		for (axis = 0; axis < 3; axis++)
			center[axis] += points[first].value[axis];
	for (axis = 0; axis < 3; axis++)
		center[axis] /= (float)point_count;
	dominant = DominantAxis(constraints.items[target_side].halfspace.normal);
	for (first = 1; first < point_count; first++)
	{
		sg_rune_vec3_t value = points[first];
		float angle = FaceAngle(&value, dominant, center);
		uint32_t insert = first;
		while (insert && FaceAngle(&points[insert - 1U], dominant, center) > angle)
		{
			points[insert] = points[insert - 1U];
			insert--;
		}
		points[insert] = value;
	}
	FreeConstraints(&constraints);
	*points_out = points;
	*point_count_out = point_count;
	return 1;

failure:
	free(points);
	FreeConstraints(&constraints);
	return 0;

overflow:
	free(points);
	FreeConstraints(&constraints);
	return -1;
}

static int AppendHookSurface(semantic_build_t *build, uint32_t model,
	uint32_t brush_index, uint32_t side_offset)
{
	const sg_bsp_world_t *world = build->authority->world;
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	uint32_t side_index = brush->first_side + side_offset;
	const sg_bsp_brush_side_t *side = &world->brush_sides[side_index];
	const sg_bsp_plane_t *plane = &world->planes[side->plane];
	sg_rune_vec3_t *points = NULL;
	uint32_t point_count = 0, vertex, axis;
	sg_configuration_hook_surface_t *surface;

	{
		int polygon = BuildHookPolygon(world, brush, side_offset,
			&points, &point_count);
		if (polygon <= 0)
		{
			SetError(build, polygon < 0 ?
				SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW :
				SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, side_index);
			return 0;
		}
	}
	if (!point_count)
		return 1;
	if (build->output->hook_surface_count >= build->limits.max_hook_surfaces ||
		point_count > build->limits.max_hook_vertices -
			build->output->hook_vertex_count)
	{
		free(points);
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW, side_index);
		return 0;
	}
	if (!Grow((void **)&build->output->hook_surfaces,
		&build->hook_surface_capacity, build->output->hook_surface_count + 1U,
		build->limits.max_hook_surfaces,
		sizeof(*build->output->hook_surfaces)) ||
		!Grow((void **)&build->output->hook_vertices,
		&build->hook_vertex_capacity,
		build->output->hook_vertex_count + point_count,
		build->limits.max_hook_vertices, sizeof(*build->output->hook_vertices)))
	{
		free(points);
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
			side_index);
		return 0;
	}
	surface = &build->output->hook_surfaces[build->output->hook_surface_count];
	memset(surface, 0, sizeof(*surface));
	surface->id = build->output->hook_surface_count;
	surface->model = model;
	surface->brush = brush_index;
	surface->brush_side = side_index;
	surface->texinfo = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
	Copy3(surface->normal, plane->normal.value);
	surface->distance = plane->distance;
	surface->first_vertex = build->output->hook_vertex_count;
	surface->vertex_count = point_count;
	for (axis = 0; axis < 3; axis++)
	{
		surface->bounds.mins.value[axis] = INFINITY;
		surface->bounds.maxs.value[axis] = -INFINITY;
	}
	for (vertex = 0; vertex < point_count; vertex++)
		for (axis = 0; axis < 3; axis++)
		{
			float value = points[vertex].value[axis];
			if (value < surface->bounds.mins.value[axis])
				surface->bounds.mins.value[axis] = value;
			if (value > surface->bounds.maxs.value[axis])
				surface->bounds.maxs.value[axis] = value;
		}
	if (side->texinfo >= 0)
	{
		if ((uint32_t)side->texinfo >= world->texinfo_count)
		{
			free(points);
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
				side_index);
			return 0;
		}
		surface->texinfo = (uint32_t)side->texinfo;
		surface->surface_flags = world->texinfos[side->texinfo].flags;
	}
	if (surface->surface_flags & SG_HOST_SURFACE_SKY)
		surface->flags |= SG_CONFIGURATION_HOOK_SURFACE_SKY;
	else
		surface->flags |= SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
	if (model != 0U)
		surface->flags |= SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	memcpy(&build->output->hook_vertices[build->output->hook_vertex_count],
		points, (size_t)point_count * sizeof(*points));
	build->output->hook_vertex_count += point_count;
	build->output->hook_surface_count++;
	free(points);
	return 1;
}

static int BuildHookSurfaces(semantic_build_t *build)
{
	const sg_bsp_world_t *world = build->authority->world;
	uint8_t *brush_marks;
	uint32_t model;

	brush_marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	if (!brush_marks)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, 0);
		return 0;
	}
	for (model = 0; model < world->model_count; model++)
	{
		uint32_t brush;
		memset(brush_marks, 0, world->brush_count);
		if (!MarkModelBrushes(world, model, brush_marks))
		{
			free(brush_marks);
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
				model);
			return 0;
		}
		for (brush = 0; brush < world->brush_count; brush++)
		{
			uint32_t contents = (uint32_t)world->brushes[brush].contents;
			uint32_t side;
			if (!brush_marks[brush] || !(contents & (SG_HOST_CONTENTS_SOLID |
				SG_HOST_CONTENTS_WINDOW | SG_HOST_CONTENTS_MONSTER |
				SEMANTICS_CONTENTS_DEADMONSTER)))
				continue;
			for (side = 0; side < world->brushes[brush].side_count; side++)
				if (!AppendHookSurface(build, model, brush, side))
				{
					free(brush_marks);
					return 0;
				}
		}
	}
	free(brush_marks);
	return 1;
}

void SG_ConfigurationSemanticsDefaultLimits(
	sg_configuration_semantics_limits_t *limits_out)
{
	if (!limits_out)
		return;
	limits_out->max_regions = UINT32_MAX;
	limits_out->max_faces = UINT32_MAX;
	limits_out->max_vertices = UINT32_MAX;
	limits_out->max_boundaries = UINT32_MAX;
	limits_out->max_hook_surfaces = UINT32_MAX;
	limits_out->max_hook_vertices = UINT32_MAX;
}

int SG_ConfigurationSemanticsBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_limits_t *limits,
	sg_configuration_semantics_t **semantics_out,
	sg_configuration_semantics_error_t *error_out)
{
	semantic_build_t build;
	uint32_t cell;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!authority || !authority->world || !configuration || !limits ||
		!semantics_out || *semantics_out || !limits->max_regions ||
		!limits->max_faces || !limits->max_vertices ||
		!limits->max_boundaries || !limits->max_hook_surfaces ||
		!limits->max_hook_vertices)
	{
		if (error_out)
			error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (!IdentityEqual(&authority->identity, &configuration->identity) ||
		!ValidateSource(authority, configuration,
			error_out ? error_out : &(sg_configuration_semantics_error_t){ 0 }))
	{
		if (error_out)
			error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE;
		return 0;
	}
	memset(&build, 0, sizeof(build));
	build.authority = authority;
	build.configuration = configuration;
	build.limits = *limits;
	build.output = calloc(1, sizeof(*build.output));
	if (!build.output)
	{
		if (error_out)
			error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	build.output->identity = configuration->identity;
	for (cell = 0; cell < configuration->cell_count; cell++)
	{
		semantic_constraints_t constraints = { 0 };
		float offsets[3];
		uint32_t leaves[3] = { 0, 0, 0 };
		sg_rune_stance_t stance = configuration->cells[cell].stance;

		if (!BuildSupportDecisions(&build, stance, cell) ||
			!SampleOffsets(authority, stance,
				offsets) || !AddCellConstraints(configuration, cell, &constraints))
		{
			FreeConstraints(&constraints);
			SetError(&build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, cell);
			goto failure;
		}
		if (!PartitionSample(&build, cell, 0,
			authority->world->models[0].headnode, offsets, &constraints, leaves))
		{
			FreeConstraints(&constraints);
			goto failure;
		}
		FreeConstraints(&constraints);
	}
	if (!BuildBoundaries(&build) || !BuildHookSurfaces(&build))
		goto failure;
	free(build.support_decisions);
	*semantics_out = build.output;
	return 1;

failure:
	free(build.support_decisions);
	if (error_out)
		*error_out = build.error;
	SG_ConfigurationSemanticsDestroy(build.output);
	return 0;
}

typedef struct semantic_audit_constraint_s
{
	sg_configuration_lattice_halfspace_t halfspace;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint8_t sample_index;
	uint8_t reversed;
} semantic_audit_constraint_t;

typedef struct semantic_audit_constraints_s
{
	semantic_audit_constraint_t *items;
	uint32_t count;
	uint32_t capacity;
} semantic_audit_constraints_t;

typedef struct semantic_audit_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	sg_configuration_semantics_audit_result_t *result;
	uint8_t *seen_regions;
	uint32_t cell;
	float offsets[3];
	uint32_t leaves[3];
	semantic_support_decision_t *support_decisions;
	uint32_t support_decision_count;
	uint32_t support_decision_capacity;
	uint32_t expected_region;
} semantic_audit_t;

static int AuditSourceCellMesh(
	const sg_configuration_space_t *configuration, uint32_t cell_index);

static int AuditLeafAtPoint(const sg_bsp_world_t *world,
	const float point[3], uint32_t *leaf_out)
{
	int32_t child = world->models[0].headnode;

	while (child >= 0)
	{
		const sg_bsp_node_t *node;
		const sg_bsp_plane_t *plane;
		float side;
		if ((uint32_t)child >= world->node_count)
			return 0;
		node = &world->nodes[child];
		if (node->plane >= world->plane_count)
			return 0;
		plane = &world->planes[node->plane];
		side = point[0] * plane->normal.value[0] +
			point[1] * plane->normal.value[1] +
			point[2] * plane->normal.value[2] - plane->distance;
		child = node->children[side < 0.0];
	}
	*leaf_out = (uint32_t)(-1 - child);
	return *leaf_out < world->leaf_count;
}

static int AuditValidateSource(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration, uint32_t *source_out)
{
	const sg_bsp_world_t *world = authority->world;
	uint32_t cell_index;

	if (!world->models || !world->model_count ||
		(configuration->cell_count && !configuration->cells) ||
		(configuration->face_count && !configuration->faces) ||
		(configuration->vertex_count && !configuration->vertices) ||
		configuration->domain.mins.value[0] != SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
		configuration->domain.mins.value[1] != SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
		configuration->domain.mins.value[2] != SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
		configuration->domain.maxs.value[0] != SG_CONFIGURATION_PMOVE_ORIGIN_MAX ||
		configuration->domain.maxs.value[1] != SG_CONFIGURATION_PMOVE_ORIGIN_MAX ||
		configuration->domain.maxs.value[2] != SG_CONFIGURATION_PMOVE_ORIGIN_MAX)
		return 0;
	for (cell_index = 0; cell_index < configuration->cell_count; cell_index++)
	{
		const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
		const sg_bsp_leaf_t *leaf;
		float mins[3] = { INFINITY, INFINITY, INFINITY };
		float maxs[3] = { -INFINITY, -INFINITY, -INFINITY };
		uint32_t local, witness_leaf;

		*source_out = cell_index;
		if (cell->stance >= SG_RUNE_STANCE_COUNT || cell->face_count < 4U ||
			cell->first_face > configuration->face_count ||
			cell->face_count > configuration->face_count - cell->first_face ||
			cell->bsp_leaf.index >= world->leaf_count ||
			!Finite3(cell->bounds.mins.value) || !Finite3(cell->bounds.maxs.value) ||
			!Finite3(cell->interior_witness.value))
			return 0;
		leaf = &world->leaves[cell->bsp_leaf.index];
		if (cell->bsp_area.index != leaf->area ||
			cell->bsp_cluster.index !=
				(leaf->cluster < 0 ? UINT32_MAX : (uint32_t)leaf->cluster) ||
			cell->contents != SG_HostCollisionRuneContents(
				(sg_host_collision_contents_t)leaf->contents) ||
			!AuditLeafAtPoint(world, cell->interior_witness.value,
				&witness_leaf) || witness_leaf != cell->bsp_leaf.index)
			return 0;
		for (local = 0; local < cell->face_count; local++)
		{
			uint32_t face_index = cell->first_face + local;
			const sg_configuration_face_t *face =
				&configuration->faces[face_index];
			float source_normal[3], source_distance;
			uint32_t vertex, axis;
			float sign = face->plane.reversed ? -1.0f : 1.0f;

			*source_out = face_index;
			if (!Finite3(face->plane.normal) || !isfinite(face->plane.distance) ||
				Dot(face->plane.normal, face->plane.normal) <= 0.0f ||
				face->vertex_count < 3U ||
				face->first_vertex > configuration->vertex_count ||
				face->vertex_count > configuration->vertex_count -
					face->first_vertex ||
				face->plane.reversed > 1U ||
				face->plane.source_kind > SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
				return 0;
			if (face->plane.source_kind == SG_CONFIGURATION_PLANE_BSP)
			{
				const sg_bsp_plane_t *plane;
				if (face->plane.source_index >= world->plane_count ||
					face->plane.source_variant != 0U)
					return 0;
				plane = &world->planes[face->plane.source_index];
				for (axis = 0; axis < 3U; axis++)
					source_normal[axis] = sign * plane->normal.value[axis];
				source_distance = sign * plane->distance;
			}
			else if (face->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
			{
				uint32_t source_axis = face->plane.source_index;
				if (source_axis >= 3U || face->plane.source_variant !=
					source_axis * 2U + (face->plane.reversed ? 1U : 0U))
					return 0;
				memset(source_normal, 0, sizeof(source_normal));
				source_normal[source_axis] = face->plane.reversed ? -1.0f : 1.0f;
				source_distance = face->plane.reversed ?
					-configuration->domain.mins.value[source_axis] :
					configuration->domain.maxs.value[source_axis];
			}
			else
			{
				const sg_rune_hull_profile_t *hull =
					cell->stance == SG_RUNE_STANCE_STANDING ?
					&authority->identity.standing_hull :
					&authority->identity.crouching_hull;
				const sg_bsp_brush_side_t *side;
				const sg_bsp_plane_t *plane;
				float hull_minimum = 0.0f;
				if (face->plane.source_index >= world->brush_side_count ||
					face->plane.source_variant != (uint32_t)cell->stance)
					return 0;
				side = &world->brush_sides[face->plane.source_index];
				if (side->plane >= world->plane_count)
					return 0;
				plane = &world->planes[side->plane];
				for (axis = 0; axis < 3U; axis++)
				{
					source_normal[axis] = sign * plane->normal.value[axis];
					hull_minimum += plane->normal.value[axis] < 0.0f ?
						plane->normal.value[axis] * hull->maxs.value[axis] :
						plane->normal.value[axis] * hull->mins.value[axis];
				}
				source_distance = sign * (plane->distance - hull_minimum);
			}
			if (face->plane.normal[0] != source_normal[0] ||
				face->plane.normal[1] != source_normal[1] ||
				face->plane.normal[2] != source_normal[2] ||
				face->plane.distance != source_distance)
				return 0;
			for (vertex = face->first_vertex;
				vertex < face->first_vertex + face->vertex_count; vertex++)
			{
				if (!Finite3(configuration->vertices[vertex].value))
					return 0;
				for (axis = 0; axis < 3U; axis++)
				{
					float value = configuration->vertices[vertex].value[axis];
					if (value < mins[axis]) mins[axis] = value;
					if (value > maxs[axis]) maxs[axis] = value;
				}
			}
		}
		for (local = 0; local < cell->face_count; local++)
		{
			const sg_configuration_face_t *face =
				&configuration->faces[cell->first_face + local];
			uint32_t vertex;
			float witness_side =
				cell->interior_witness.value[0] * face->plane.normal[0] +
				cell->interior_witness.value[1] * face->plane.normal[1] +
				cell->interior_witness.value[2] * face->plane.normal[2] -
				face->plane.distance;
			if (witness_side >= 0.0f)
				return 0;
			for (vertex = face->first_vertex;
				vertex < face->first_vertex + face->vertex_count; vertex++)
			{
				const float *point = configuration->vertices[vertex].value;
				uint32_t other, prior;
				float own_distance = point[0] * face->plane.normal[0] +
					point[1] * face->plane.normal[1] +
					point[2] * face->plane.normal[2] - face->plane.distance;
				if (fabsf(own_distance) > SEMANTICS_POINT_EPSILON * 4.0f)
					return 0;
				for (other = 0; other < cell->face_count; other++)
				{
					const sg_configuration_face_t *halfspace =
						&configuration->faces[cell->first_face + other];
					float side = point[0] * halfspace->plane.normal[0] +
						point[1] * halfspace->plane.normal[1] +
						point[2] * halfspace->plane.normal[2] -
						halfspace->plane.distance;
					if (side > SEMANTICS_POINT_EPSILON)
						return 0;
				}
				for (prior = face->first_vertex; prior < vertex; prior++)
					if (fabsf(configuration->vertices[prior].value[0] - point[0]) <=
							SEMANTICS_POINT_EPSILON &&
						fabsf(configuration->vertices[prior].value[1] - point[1]) <=
							SEMANTICS_POINT_EPSILON &&
						fabsf(configuration->vertices[prior].value[2] - point[2]) <=
							SEMANTICS_POINT_EPSILON)
						return 0;
			}
		}
		for (local = 0; local < 3U; local++)
			if (fabsf(cell->bounds.mins.value[local] - mins[local]) >
					SEMANTICS_POINT_EPSILON ||
				fabsf(cell->bounds.maxs.value[local] - maxs[local]) >
					SEMANTICS_POINT_EPSILON)
				return 0;
		if (!AuditSourceCellMesh(configuration, cell_index))
			return 0;
	}
	return 1;
}

static int AuditAddDecision(semantic_audit_t *audit,
	const float normal[3], float distance, uint32_t source_kind,
	uint32_t source_index, uint32_t source_variant)
{
	semantic_support_decision_t *items = audit->support_decisions;
	uint32_t count = audit->support_decision_count;
	uint32_t index;

	if (Dot(normal, normal) <= FLT_EPSILON)
		return 1;
	for (index = 0; index < count; index++)
		if (items[index].normal[0] == normal[0] &&
			items[index].normal[1] == normal[1] &&
			items[index].normal[2] == normal[2] &&
			items[index].distance == distance)
			return 1;
	if (!Grow((void **)&audit->support_decisions,
		&audit->support_decision_capacity, count + 1U, UINT32_MAX,
		sizeof(*items)))
		return 0;
	items = audit->support_decisions;
	Copy3(items[count].normal, normal);
	items[count].distance = distance;
	items[count].source_kind = source_kind;
	items[count].source_index = source_index;
	items[count].source_variant = source_variant;
	audit->support_decision_count++;
	return 1;
}

static int AuditBuildSupportDecisions(semantic_audit_t *audit,
	sg_rune_stance_t stance, uint32_t cell_index)
{
	const sg_bsp_world_t *world = audit->authority->world;
	const sg_rune_bounds_t *bounds =
		&audit->configuration->cells[cell_index].bounds;
	const sg_rune_hull_profile_t *hull = stance == SG_RUNE_STANCE_STANDING ?
		&audit->authority->identity.standing_hull :
		&audit->authority->identity.crouching_hull;
	semantic_support_decision_t *entries = NULL;
	semantic_support_decision_t *leaves = NULL;
	uint8_t *brush_marks = NULL;
	uint32_t entry_count = 0, entry_capacity = 0;
	uint32_t leave_count = 0, leave_capacity = 0;
	uint32_t brush, first, second;

	free(audit->support_decisions);
	audit->support_decisions = NULL;
	audit->support_decision_count = 0;
	audit->support_decision_capacity = 0;
	brush_marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	if (!brush_marks || !AuditReachableBrushes(world, 0, brush_marks))
		goto failure;
	for (brush = 0; brush < world->brush_count; brush++)
	{
		const sg_bsp_brush_t *record = &world->brushes[brush];
		uint32_t offset;
		int potentially_reachable = 1;
		if (!brush_marks[brush] || !record->side_count ||
			!((uint32_t)record->contents & SG_HOST_MASK_PLAYER_SOLID))
			continue;
		for (offset = 0; offset < record->side_count; offset++)
		{
			uint32_t side_index = record->first_side + offset;
			const sg_bsp_brush_side_t *side;
			const sg_bsp_plane_t *plane;
			float expanded, minimum = 0.0f;
			uint32_t axis;
			if (side_index >= world->brush_side_count)
				goto failure;
			side = &world->brush_sides[side_index];
			if (side->plane >= world->plane_count)
				goto failure;
			plane = &world->planes[side->plane];
			expanded = plane->distance - HullMinimum(hull, plane->normal.value);
			for (axis = 0; axis < 3U; axis++)
				minimum += plane->normal.value[axis] < 0.0f ?
					plane->normal.value[axis] * bounds->maxs.value[axis] :
					plane->normal.value[axis] * bounds->mins.value[axis];
			if (plane->normal.value[2] > 0.0f)
				minimum -= plane->normal.value[2] * SEMANTICS_GROUND_PROBE;
			if (minimum > expanded + SEMANTICS_TRACE_EPSILON)
			{
				potentially_reachable = 0;
				break;
			}
		}
		if (!potentially_reachable)
			continue;
		for (offset = 0; offset < record->side_count; offset++)
		{
			uint32_t side_index = record->first_side + offset;
			const sg_bsp_brush_side_t *side;
			const sg_bsp_plane_t *plane;
			float expanded;
			if (side_index >= world->brush_side_count)
				goto failure;
			side = &world->brush_sides[side_index];
			if (side->plane >= world->plane_count)
				goto failure;
			plane = &world->planes[side->plane];
			expanded = plane->distance - HullMinimum(hull, plane->normal.value);
			if (!AuditAddDecision(audit, plane->normal.value, expanded,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_START,
				side_index, brush))
				goto failure;
			if (plane->normal.value[2] < 0.0f)
			{
				if (!AuditAddDecision(audit, plane->normal.value,
					expanded - SEMANTICS_TRACE_EPSILON,
					SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_LEAVE_ZERO,
					side_index, brush) ||
					!AuditAddDecision(audit, plane->normal.value,
						expanded + plane->normal.value[2] *
							SEMANTICS_GROUND_PROBE,
						SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_LEAVE_END,
						side_index, brush) || leave_count == UINT32_MAX ||
					!Grow((void **)&leaves, &leave_capacity, leave_count + 1U,
						UINT32_MAX, sizeof(*leaves)))
					goto failure;
				memset(&leaves[leave_count], 0, sizeof(*leaves));
				Copy3(leaves[leave_count].normal, plane->normal.value);
				leaves[leave_count].distance = expanded;
				leaves[leave_count].source_index = side_index;
				leaves[leave_count].source_variant = brush;
				leave_count++;
				continue;
			}
			if (plane->normal.value[2] == 0.0f)
				continue;
			if (!AuditAddDecision(audit, plane->normal.value,
				expanded + plane->normal.value[2] * SEMANTICS_GROUND_PROBE +
					SEMANTICS_TRACE_EPSILON,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_REACH,
					side_index, brush) ||
				!AuditAddDecision(audit, plane->normal.value,
					expanded + SEMANTICS_TRACE_EPSILON,
					SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_ENTER_ZERO,
					side_index, brush) ||
				!Grow((void **)&entries, &entry_capacity, entry_count + 1U,
					UINT32_MAX, sizeof(*entries)))
				goto failure;
			memset(&entries[entry_count], 0, sizeof(*entries));
			Copy3(entries[entry_count].normal, plane->normal.value);
			entries[entry_count].distance = expanded;
			entries[entry_count].source_index = side_index;
			entries[entry_count].source_variant = brush;
			entry_count++;
		}
	}
	for (first = 0; first < entry_count; first++)
		for (second = first + 1U; second < entry_count; second++)
		{
			float normal[3];
			float distance;
			uint32_t axis;
			for (axis = 0; axis < 3; axis++)
				normal[axis] = entries[second].normal[2] *
					entries[first].normal[axis] - entries[first].normal[2] *
					entries[second].normal[axis];
			distance = entries[second].normal[2] *
				(entries[first].distance + SEMANTICS_TRACE_EPSILON) -
				entries[first].normal[2] *
				(entries[second].distance + SEMANTICS_TRACE_EPSILON);
			if (!AuditAddDecision(audit, normal, distance,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_ORDER,
				entries[first].source_index, entries[second].source_index))
				goto failure;
		}
	for (first = 0; first < entry_count; first++)
		for (second = 0; second < leave_count; second++)
		{
			float normal[3], distance;
			uint32_t axis;
			if (entries[first].source_variant != leaves[second].source_variant)
				continue;
			for (axis = 0; axis < 3U; axis++)
				normal[axis] = entries[first].normal[2] *
					leaves[second].normal[axis] - leaves[second].normal[2] *
					entries[first].normal[axis];
			distance = entries[first].normal[2] *
				(leaves[second].distance - SEMANTICS_TRACE_EPSILON) -
				leaves[second].normal[2] *
				(entries[first].distance + SEMANTICS_TRACE_EPSILON);
			if (!AuditAddDecision(audit, normal, distance,
				SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP,
				entries[first].source_index, leaves[second].source_index))
				goto failure;
		}
	free(brush_marks);
	free(entries);
	free(leaves);
	return 1;

failure:
	free(brush_marks);
	free(entries);
	free(leaves);
	return 0;
}

static int AuditAppendSource(semantic_audit_constraints_t *constraints,
	const float normal[3], float distance, uint32_t source_kind,
	uint32_t source_index, uint32_t source_variant, uint8_t sample_index,
	uint8_t reversed)
{
	semantic_audit_constraint_t *grown;
	uint32_t next;

	if (constraints->count == constraints->capacity)
	{
		next = constraints->capacity ? constraints->capacity * 2U : 32U;
		if (next < constraints->capacity)
			return 0;
		grown = realloc(constraints->items, (size_t)next * sizeof(*grown));
		if (!grown)
			return 0;
		constraints->items = grown;
		constraints->capacity = next;
	}
	memset(&constraints->items[constraints->count], 0,
		sizeof(constraints->items[constraints->count]));
	Copy3(constraints->items[constraints->count].halfspace.normal, normal);
	constraints->items[constraints->count].halfspace.distance = distance;
	constraints->items[constraints->count].source_kind = source_kind;
	constraints->items[constraints->count].source_index = source_index;
	constraints->items[constraints->count].source_variant = source_variant;
	constraints->items[constraints->count].sample_index = sample_index;
	constraints->items[constraints->count].reversed = reversed;
	constraints->count++;
	return 1;
}

static int AuditAppend(semantic_audit_constraints_t *constraints,
	const float normal[3], float distance)
{
	return AuditAppendSource(constraints, normal, distance, 0, 0, 0, 0, 0);
}

static int AuditCopy(const semantic_audit_constraints_t *source,
	semantic_audit_constraints_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	if (!source->count)
		return 1;
	destination->items = malloc((size_t)source->count *
		sizeof(*destination->items));
	if (!destination->items)
		return 0;
	memcpy(destination->items, source->items,
		(size_t)source->count * sizeof(*destination->items));
	destination->count = source->count;
	destination->capacity = source->count;
	return 1;
}

static int AuditInterior(semantic_audit_t *audit,
	const semantic_audit_constraints_t *constraints, int32_t q8[3])
{
	uint8_t *clearance;
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_configuration_lattice_stats_t stats = { 0 };
	uint32_t index;
	int positive = 0;
	int answer;

	clearance = malloc(constraints->count);
	halfspaces = malloc((size_t)constraints->count * sizeof(*halfspaces));
	if (!clearance || !halfspaces)
	{
		free(clearance);
		free(halfspaces);
		return -2;
	}
	memset(clearance, 1, constraints->count);
	for (index = 0; index < constraints->count; index++)
		halfspaces[index] = constraints->items[index].halfspace;
	answer = SG_ConfigurationLatticeFindMaxClearance(halfspaces,
		clearance, constraints->count, NULL, q8, &positive, &stats);
	if (answer > 0 && !positive)
	{
		for (index = 0; index < constraints->count; index++)
			halfspaces[index].open = 1;
		answer = SG_ConfigurationLatticeFind(halfspaces, constraints->count,
			NULL, q8, &stats);
	}
	free(clearance);
	free(halfspaces);
	audit->result->lattice_solve_calls += stats.solve_calls;
	audit->result->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		audit->result->lattice_maximum_binary_shift)
		audit->result->lattice_maximum_binary_shift =
			stats.maximum_binary_shift;
	return answer;
}

static int AuditPointInside(const float point[3],
	const semantic_audit_constraints_t *constraints)
{
	uint32_t constraint;

	for (constraint = 0; constraint < constraints->count; constraint++)
		if (Dot(point, constraints->items[constraint].halfspace.normal) >
			constraints->items[constraint].halfspace.distance +
			SEMANTICS_POINT_EPSILON)
			return 0;
	return 1;
}

static int AuditIntersection(
	const sg_configuration_lattice_halfspace_t *a,
	const sg_configuration_lattice_halfspace_t *b,
	const sg_configuration_lattice_halfspace_t *c, float point[3])
{
	double n0[3], n1[3], n2[3], cross12[3], cross20[3], cross01[3];
	double determinant;
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
	{
		n0[axis] = a->normal[axis];
		n1[axis] = b->normal[axis];
		n2[axis] = c->normal[axis];
	}
	cross12[0] = n1[1] * n2[2] - n1[2] * n2[1];
	cross12[1] = n1[2] * n2[0] - n1[0] * n2[2];
	cross12[2] = n1[0] * n2[1] - n1[1] * n2[0];
	determinant = n0[0] * cross12[0] + n0[1] * cross12[1] +
		n0[2] * cross12[2];
	if (!isfinite(determinant) || fabs(determinant) <= DBL_EPSILON)
		return 0;
	cross20[0] = n2[1] * n0[2] - n2[2] * n0[1];
	cross20[1] = n2[2] * n0[0] - n2[0] * n0[2];
	cross20[2] = n2[0] * n0[1] - n2[1] * n0[0];
	cross01[0] = n0[1] * n1[2] - n0[2] * n1[1];
	cross01[1] = n0[2] * n1[0] - n0[0] * n1[2];
	cross01[2] = n0[0] * n1[1] - n0[1] * n1[0];
	for (axis = 0; axis < 3; axis++)
		point[axis] = (float)(((double)a->distance * cross12[axis] +
			(double)b->distance * cross20[axis] +
			(double)c->distance * cross01[axis]) / determinant);
	return Finite3(point);
}

static int AuditSourceCellMesh(
	const sg_configuration_space_t *configuration, uint32_t cell_index)
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	sg_rune_vec3_t *vertices = NULL;
	uint32_t count = 0, capacity = 0;
	uint32_t i, j, k, face_index;
	int answer = 0;

	for (i = 0; i < cell->face_count; i++)
		for (j = i + 1U; j < cell->face_count; j++)
			for (k = j + 1U; k < cell->face_count; k++)
			{
				sg_configuration_lattice_halfspace_t planes[3];
				float point[3];
				uint32_t plane, seen;
				memset(planes, 0, sizeof(planes));
				Copy3(planes[0].normal,
					configuration->faces[cell->first_face + i].plane.normal);
				planes[0].distance =
					configuration->faces[cell->first_face + i].plane.distance;
				Copy3(planes[1].normal,
					configuration->faces[cell->first_face + j].plane.normal);
				planes[1].distance =
					configuration->faces[cell->first_face + j].plane.distance;
				Copy3(planes[2].normal,
					configuration->faces[cell->first_face + k].plane.normal);
				planes[2].distance =
					configuration->faces[cell->first_face + k].plane.distance;
				if (!AuditIntersection(&planes[0], &planes[1], &planes[2], point))
					continue;
				for (plane = 0; plane < cell->face_count; plane++)
				{
					const sg_configuration_face_t *halfspace =
						&configuration->faces[cell->first_face + plane];
					float side = point[0] * halfspace->plane.normal[0] +
						point[1] * halfspace->plane.normal[1] +
						point[2] * halfspace->plane.normal[2] -
						halfspace->plane.distance;
					if (side > SEMANTICS_POINT_EPSILON)
						break;
				}
				if (plane != cell->face_count)
					continue;
				for (seen = 0; seen < count; seen++)
					if (fabsf(vertices[seen].value[0] - point[0]) <=
							SEMANTICS_POINT_EPSILON &&
						fabsf(vertices[seen].value[1] - point[1]) <=
							SEMANTICS_POINT_EPSILON &&
						fabsf(vertices[seen].value[2] - point[2]) <=
							SEMANTICS_POINT_EPSILON)
						break;
				if (seen != count)
					continue;
				if (count == UINT32_MAX || !Grow((void **)&vertices, &capacity,
					count + 1U, UINT32_MAX, sizeof(*vertices)))
					goto done;
				Copy3(vertices[count++].value, point);
			}
	if (count < 4U)
		goto done;
	for (face_index = 0; face_index < cell->face_count; face_index++)
	{
		const sg_configuration_face_t *face =
			&configuration->faces[cell->first_face + face_index];
		uint32_t expected = 0, vertex;
		for (vertex = 0; vertex < count; vertex++)
		{
			float side = vertices[vertex].value[0] * face->plane.normal[0] +
				vertices[vertex].value[1] * face->plane.normal[1] +
				vertices[vertex].value[2] * face->plane.normal[2] -
				face->plane.distance;
			if (fabsf(side) <= SEMANTICS_POINT_EPSILON * 4.0f)
			{
				uint32_t actual;
				expected++;
				for (actual = face->first_vertex;
					actual < face->first_vertex + face->vertex_count; actual++)
					if (fabsf(configuration->vertices[actual].value[0] -
							vertices[vertex].value[0]) <= SEMANTICS_POINT_EPSILON &&
						fabsf(configuration->vertices[actual].value[1] -
							vertices[vertex].value[1]) <= SEMANTICS_POINT_EPSILON &&
						fabsf(configuration->vertices[actual].value[2] -
							vertices[vertex].value[2]) <= SEMANTICS_POINT_EPSILON)
						break;
				if (actual == face->first_vertex + face->vertex_count)
					goto done;
			}
		}
		if (expected != face->vertex_count)
			goto done;
	}
	answer = 1;

done:
	free(vertices);
	return answer;
}

static int AuditMesh(const sg_configuration_semantics_t *semantics,
	const sg_configuration_semantic_region_t *region,
	const semantic_audit_constraints_t *constraints)
{
	sg_rune_vec3_t *vertices = NULL;
	uint32_t vertex_count = 0, vertex_capacity = 0;
	uint8_t *active = NULL;
	uint32_t a, b, c, constraint, active_count = 0, face;
	uint32_t next_constraint = 0;
	float bounds_min[3] = { INFINITY, INFINITY, INFINITY };
	float bounds_max[3] = { -INFINITY, -INFINITY, -INFINITY };
	int valid = 0;

	for (a = 0; a < constraints->count; a++)
		for (b = a + 1U; b < constraints->count; b++)
			for (c = b + 1U; c < constraints->count; c++)
			{
				float point[3];
				uint32_t existing;
				if (!AuditIntersection(&constraints->items[a].halfspace,
					&constraints->items[b].halfspace,
					&constraints->items[c].halfspace, point) ||
					!AuditPointInside(point, constraints))
					continue;
				for (existing = 0; existing < vertex_count; existing++)
					if (SamePoint(&vertices[existing], point))
						break;
				if (existing != vertex_count)
					continue;
				if (vertex_count == vertex_capacity)
				{
					uint32_t next = vertex_capacity ? vertex_capacity * 2U : 32U;
					sg_rune_vec3_t *grown;
					if (next < vertex_capacity)
						goto done;
					grown = realloc(vertices, (size_t)next * sizeof(*grown));
					if (!grown)
						goto done;
					vertices = grown;
					vertex_capacity = next;
				}
				Copy3(vertices[vertex_count++].value, point);
			}
	active = calloc(constraints->count, 1);
	if (!active)
		goto done;
	if (vertex_count < 4U)
		goto done;
	if (!Finite3(region->bounds.mins.value) ||
		!Finite3(region->bounds.maxs.value))
		goto done;
	for (a = 0; a < vertex_count; a++)
		for (b = 0; b < 3U; b++)
		{
			float value = vertices[a].value[b];
			if (value < bounds_min[b])
				bounds_min[b] = value;
			if (value > bounds_max[b])
				bounds_max[b] = value;
		}
	for (a = 0; a < 3U; a++)
		if (fabsf(region->bounds.mins.value[a] - bounds_min[a]) >
				SEMANTICS_POINT_EPSILON ||
			fabsf(region->bounds.maxs.value[a] - bounds_max[a]) >
				SEMANTICS_POINT_EPSILON)
			goto done;
	for (constraint = 0; constraint < constraints->count; constraint++)
	{
		uint32_t on_plane = 0, vertex;
		uint32_t prior;
		for (prior = 0; prior < constraint; prior++)
			if (SameHalfspace(&constraints->items[prior].halfspace,
				&constraints->items[constraint].halfspace))
				break;
		if (prior != constraint)
			continue;
		for (vertex = 0; vertex < vertex_count; vertex++)
			if (fabsf(Dot(vertices[vertex].value,
				constraints->items[constraint].halfspace.normal) -
				constraints->items[constraint].halfspace.distance) <=
				SEMANTICS_POINT_EPSILON * 4.0f)
				on_plane++;
		if (on_plane >= 3U)
		{
			active[constraint] = 1;
			active_count++;
		}
	}
	if (region->face_count != active_count ||
		region->first_face > semantics->face_count ||
		region->face_count > semantics->face_count - region->first_face)
		goto done;
	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *actual =
			&semantics->faces[face];
		const semantic_audit_constraint_t *expected;
		uint32_t expected_vertex_count = 0;
		uint32_t expected_first_vertex;
		uint32_t vertex;

		if (actual->vertex_count < 3U ||
			actual->first_vertex > semantics->vertex_count ||
			actual->vertex_count > semantics->vertex_count - actual->first_vertex)
			goto done;
		while (next_constraint < constraints->count &&
			!active[next_constraint])
			next_constraint++;
		if (next_constraint == constraints->count)
			goto done;
		expected = &constraints->items[next_constraint++];
		if (actual->normal[0] != expected->halfspace.normal[0] ||
			actual->normal[1] != expected->halfspace.normal[1] ||
			actual->normal[2] != expected->halfspace.normal[2] ||
			actual->distance != expected->halfspace.distance ||
			actual->source_kind != expected->source_kind ||
			actual->source_index != expected->source_index ||
			actual->source_variant != expected->source_variant ||
			actual->sample_index != expected->sample_index ||
			actual->reversed != expected->reversed ||
			actual->reserved[0] != 0U || actual->reserved[1] != 0U)
			goto done;
		expected_first_vertex = face ?
			semantics->faces[face - 1U].first_vertex +
				semantics->faces[face - 1U].vertex_count : 0U;
		if (actual->first_vertex != expected_first_vertex)
			goto done;
		for (vertex = 0; vertex < vertex_count; vertex++)
			if (fabsf(Dot(vertices[vertex].value,
				expected->halfspace.normal) - expected->halfspace.distance) <=
				SEMANTICS_POINT_EPSILON * 4.0f)
				expected_vertex_count++;
		if (actual->vertex_count != expected_vertex_count)
			goto done;
		for (vertex = actual->first_vertex;
			vertex < actual->first_vertex + actual->vertex_count; vertex++)
		{
			uint32_t prior;
			if (!Finite3(semantics->vertices[vertex].value) ||
				fabsf(Dot(semantics->vertices[vertex].value, actual->normal) -
					actual->distance) > SEMANTICS_POINT_EPSILON * 4.0f ||
				!AuditPointInside(semantics->vertices[vertex].value, constraints))
				goto done;
			for (prior = actual->first_vertex; prior < vertex; prior++)
				if (SamePoint(&semantics->vertices[prior],
					semantics->vertices[vertex].value))
					goto done;
		}
		for (vertex = 0; vertex < vertex_count; vertex++)
			if (fabsf(Dot(vertices[vertex].value,
				expected->halfspace.normal) - expected->halfspace.distance) <=
				SEMANTICS_POINT_EPSILON * 4.0f)
			{
				uint32_t actual_vertex;
				for (actual_vertex = actual->first_vertex;
					actual_vertex < actual->first_vertex + actual->vertex_count;
					actual_vertex++)
					if (SamePoint(&semantics->vertices[actual_vertex],
						vertices[vertex].value))
						break;
				if (actual_vertex == actual->first_vertex + actual->vertex_count)
					goto done;
			}
	}
	while (next_constraint < constraints->count && !active[next_constraint])
		next_constraint++;
	if (next_constraint != constraints->count)
		goto done;
	valid = 1;

done:
	free(active);
	free(vertices);
	return valid;
}

static int AuditTerminal(semantic_audit_t *audit,
	const semantic_audit_constraints_t *constraints)
{
	int32_t q8[3];
	int feasible = AuditInterior(audit, constraints, q8);
	uint32_t region, match = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;

	if (feasible < 0)
	{
		audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
		return 0;
	}
	if (!feasible)
		return 1;
	for (region = 0; region < audit->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *candidate =
			&audit->semantics->regions[region];
		if (candidate->cell != audit->cell ||
			candidate->sample_leaves[0] != audit->leaves[0] ||
			candidate->sample_leaves[1] != audit->leaves[1] ||
			candidate->sample_leaves[2] != audit->leaves[2] ||
			!AuditPointInside(candidate->interior_witness.value, constraints))
			continue;
		if (match != SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
		{
			audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_REGION;
			audit->result->record = region;
			return 0;
		}
		match = region;
	}
	if (match == SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
	{
		audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_REGION;
		audit->result->record = audit->cell;
		return 0;
	}
	if (match != audit->expected_region ||
		audit->semantics->regions[match].id !=
			(((uint64_t)audit->cell << 32) | match) ||
		audit->semantics->regions[match].first_face != (match ?
			audit->semantics->regions[match - 1U].first_face +
				audit->semantics->regions[match - 1U].face_count : 0U) ||
		audit->semantics->regions[match].interior_witness.value[0] !=
			(float)q8[0] * 0.125f ||
		audit->semantics->regions[match].interior_witness.value[1] !=
			(float)q8[1] * 0.125f ||
		audit->semantics->regions[match].interior_witness.value[2] !=
			(float)q8[2] * 0.125f)
	{
		audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
		audit->result->record = match;
		return 0;
	}
	if (!AuditMesh(audit->semantics, &audit->semantics->regions[match],
		constraints))
	{
		audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
		audit->result->record = match;
		return 0;
	}
	audit->seen_regions[match] = 1;
	audit->expected_region++;
	return 1;
}

static int AuditTree(semantic_audit_t *audit, uint8_t sample, int32_t node,
	const semantic_audit_constraints_t *constraints);

static int AuditSupport(semantic_audit_t *audit, uint32_t decision_index,
	const semantic_audit_constraints_t *constraints)
{
	const semantic_support_decision_t *decision;
	int side;

	if (decision_index == audit->support_decision_count)
		return AuditTerminal(audit, constraints);
	decision = &audit->support_decisions[decision_index];
	for (side = 0; side < 2; side++)
	{
		semantic_audit_constraints_t branch;
		float normal[3], distance;
		int32_t q8[3];
		int feasible;

		if (!AuditCopy(constraints, &branch))
			goto failure;
		if (side == 0)
		{
			normal[0] = -decision->normal[0];
			normal[1] = -decision->normal[1];
			normal[2] = -decision->normal[2];
			distance = -decision->distance;
		}
		else
		{
			Copy3(normal, decision->normal);
			distance = decision->distance;
		}
		if (!AuditAppendSource(&branch, normal, distance,
			decision->source_kind, decision->source_index,
			decision->source_variant, 0, (uint8_t)(side == 0)))
		{
			free(branch.items);
			goto failure;
		}
		feasible = AuditInterior(audit, &branch, q8);
		if (feasible < 0)
		{
			free(branch.items);
			goto failure;
		}
		if (feasible && !AuditSupport(audit, decision_index + 1U, &branch))
		{
			free(branch.items);
			return 0;
		}
		free(branch.items);
	}
	return 1;

failure:
	audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
	return 0;
}

static int AuditNextSample(semantic_audit_t *audit, uint8_t sample,
	const semantic_audit_constraints_t *constraints)
{
	if (sample == 3U)
	{
		int32_t q8[3];
		int feasible = AuditInterior(audit, constraints, q8);
		if (feasible < 0)
		{
			audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
			return 0;
		}
		return !feasible || AuditSupport(audit, 0, constraints);
	}
	return AuditTree(audit, sample,
		audit->authority->world->models[0].headnode, constraints);
}

static int AuditTree(semantic_audit_t *audit, uint8_t sample, int32_t node,
	const semantic_audit_constraints_t *constraints)
{
	const sg_bsp_world_t *world = audit->authority->world;
	const sg_bsp_node_t *record;
	const sg_bsp_plane_t *plane;
	int side;

	if (node < 0)
	{
		uint32_t leaf = (uint32_t)(-1 - node);
		if (leaf >= world->leaf_count)
		{
			audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH;
			audit->result->record = leaf;
			return 0;
		}
		audit->leaves[sample] = leaf;
		return AuditNextSample(audit, (uint8_t)(sample + 1U), constraints);
	}
	if ((uint32_t)node >= world->node_count ||
		world->nodes[node].plane >= world->plane_count)
	{
		audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH;
		audit->result->record = (uint32_t)node;
		return 0;
	}
	record = &world->nodes[node];
	plane = &world->planes[record->plane];
	for (side = 0; side < 2; side++)
	{
		semantic_audit_constraints_t branch;
		float normal[3], distance;
		int32_t q8[3];
		int feasible;

		if (!AuditCopy(constraints, &branch))
			goto solver_failure;
		if (side == 0)
		{
			normal[0] = -plane->normal.value[0];
			normal[1] = -plane->normal.value[1];
			normal[2] = -plane->normal.value[2];
			distance = -plane->distance +
				plane->normal.value[2] * audit->offsets[sample];
		}
		else
		{
			Copy3(normal, plane->normal.value);
			distance = plane->distance -
				plane->normal.value[2] * audit->offsets[sample];
		}
		if (!AuditAppendSource(&branch, normal, distance,
			SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE,
			record->plane, 0, sample, (uint8_t)(side == 0)))
		{
			free(branch.items);
			goto solver_failure;
		}
		feasible = AuditInterior(audit, &branch, q8);
		if (feasible < 0)
		{
			free(branch.items);
			goto solver_failure;
		}
		if (feasible && !AuditTree(audit, sample, record->children[side],
			&branch))
		{
			free(branch.items);
			return 0;
		}
		free(branch.items);
	}
	return 1;

solver_failure:
	audit->result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
	return 0;
}

static int AuditOffsets(const sg_host_collision_authority_t *authority,
	sg_rune_stance_t stance, float offsets[3])
{
	const sg_rune_hull_profile_t *hull;
	float view, height;
	int top;

	if (stance == SG_RUNE_STANCE_STANDING)
	{
		hull = &authority->identity.standing_hull;
		view = 22.0f;
	}
	else if (stance == SG_RUNE_STANCE_CROUCHING)
	{
		hull = &authority->identity.crouching_hull;
		view = -2.0f;
	}
	else
		return 0;
	height = view - hull->mins.value[2];
	if (!isfinite(height) || height < (float)INT32_MIN ||
		height >= (float)INT32_MAX)
		return 0;
	top = (int)height;
	offsets[2] = hull->mins.value[2] + (float)top;
	offsets[1] = hull->mins.value[2] + (float)(top / 2);
	offsets[0] = hull->mins.value[2] + 1.0f;
	return 1;
}

static int AuditBoundaries(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *result)
{
	const sg_bsp_world_t *world = authority->world;
	uint8_t *seen;
	uint32_t cell, expected_record = 0;

	seen = calloc(semantics->boundary_count ? semantics->boundary_count : 1U, 1);
	if (!seen)
	{
		result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
		return 0;
	}
	for (cell = 0; cell < configuration->cell_count; cell++)
	{
		const sg_configuration_cell_t *owner = &configuration->cells[cell];
		uint32_t local;

		for (local = 0; local < owner->face_count; local++)
		{
			uint32_t face_index = owner->first_face + local;
			const sg_configuration_face_t *face = &configuration->faces[face_index];
			sg_configuration_boundary_flags_t flags = 0;
			uint32_t expected_brush = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
			uint32_t expected_brush_side =
				SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
			uint32_t expected_texinfo = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
			int32_t expected_surface_flags = 0;
			float expected_surface_normal[3] = { 0, 0, 0 };
			float expected_surface_distance = 0.0f;
			uint32_t record, match_record = 0, matches = 0;

			if (face->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
				flags = SG_CONFIGURATION_BOUNDARY_VOID;
			else if (face->plane.source_kind ==
				SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			{
				uint32_t brush;
				const sg_bsp_brush_side_t *side;

				if (face->plane.source_index >= world->brush_side_count)
					goto boundary_source_failure;
				for (brush = 0; brush < world->brush_count; brush++)
				{
					uint32_t first = world->brushes[brush].first_side;
					if (face->plane.source_index >= first &&
						face->plane.source_index - first <
						world->brushes[brush].side_count)
					{
						if (expected_brush !=
							SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
							goto boundary_source_failure;
						expected_brush = brush;
					}
				}
				if (expected_brush == SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
					goto boundary_source_failure;
				side = &world->brush_sides[face->plane.source_index];
				if (side->plane >= world->plane_count)
					goto boundary_source_failure;
				Copy3(expected_surface_normal,
					world->planes[side->plane].normal.value);
				expected_surface_distance = world->planes[side->plane].distance;
				expected_brush_side = face->plane.source_index;
				if (side->texinfo >= 0)
				{
					if ((uint32_t)side->texinfo >= world->texinfo_count)
						goto boundary_source_failure;
					expected_texinfo = (uint32_t)side->texinfo;
					expected_surface_flags = world->texinfos[side->texinfo].flags;
				}
				flags = SG_CONFIGURATION_BOUNDARY_PHYSICAL;
				if (expected_surface_normal[2] >= 0.7f)
					flags |= SG_CONFIGURATION_BOUNDARY_SUPPORT_CANDIDATE;
			}
			else
				continue;
			for (record = 0; record < semantics->boundary_count; record++)
				if (semantics->boundaries[record].cell == cell &&
					semantics->boundaries[record].configuration_face == face_index)
				{
					match_record = record;
					matches++;
				}
			if (!matches)
			{
				free(seen);
				result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_BOUNDARY;
				result->record = face_index;
				return 0;
			}
			if (matches > 1U)
			{
				free(seen);
				result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_BOUNDARY;
				result->record = face_index;
				return 0;
			}
			{
				const sg_configuration_boundary_t *actual =
					&semantics->boundaries[match_record];
				if (match_record != expected_record ||
					actual->id != (((uint64_t)cell << 32) | local) ||
					actual->flags != flags || actual->brush != expected_brush ||
					actual->brush_side != expected_brush_side ||
					actual->texinfo != expected_texinfo ||
					actual->surface_flags != expected_surface_flags ||
					actual->origin_normal[0] != face->plane.normal[0] ||
					actual->origin_normal[1] != face->plane.normal[1] ||
					actual->origin_normal[2] != face->plane.normal[2] ||
					actual->origin_distance != face->plane.distance ||
					actual->surface_normal[0] != expected_surface_normal[0] ||
					actual->surface_normal[1] != expected_surface_normal[1] ||
					actual->surface_normal[2] != expected_surface_normal[2] ||
					actual->surface_distance != expected_surface_distance)
				{
					free(seen);
					result->code =
						SG_CONFIGURATION_SEMANTICS_AUDIT_BOUNDARY_DISAGREEMENT;
					result->record = match_record;
					return 0;
				}
				seen[match_record] = 1;
			}
			expected_record++;
		}
	}
	if (expected_record != semantics->boundary_count)
	{
		free(seen);
		result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_BOUNDARY;
		result->record = expected_record;
		return 0;
	}
	for (cell = 0; cell < semantics->boundary_count; cell++)
		if (!seen[cell])
		{
			free(seen);
			result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_BOUNDARY;
			result->record = cell;
			return 0;
		}
	free(seen);
	return 1;

boundary_source_failure:
	free(seen);
	result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH;
	return 0;
}

static int AuditReachableBrushes(const sg_bsp_world_t *world,
	uint32_t model, uint8_t *brush_marks)
{
	int32_t *pending = NULL;
	uint32_t pending_count = 0, pending_capacity = 0;
	uint8_t *visited = calloc(world->node_count ? world->node_count : 1U, 1);
	int result = 0;

	if (!visited || !Grow((void **)&pending, &pending_capacity, 1U,
		UINT32_MAX, sizeof(*pending)))
		goto done;
	pending[pending_count++] = world->models[model].headnode;
	while (pending_count)
	{
		int32_t child = pending[0];
		uint32_t move;
		for (move = 1; move < pending_count; move++)
			pending[move - 1U] = pending[move];
		pending_count--;
		if (child < 0)
		{
			uint32_t leaf_index = (uint32_t)(-1 - child);
			const sg_bsp_leaf_t *leaf;
			uint32_t offset;
			if (leaf_index >= world->leaf_count)
				goto done;
			leaf = &world->leaves[leaf_index];
			if (leaf->first_leaf_brush > world->leaf_brush_count ||
				leaf->leaf_brush_count > world->leaf_brush_count -
					leaf->first_leaf_brush)
				goto done;
			for (offset = 0; offset < leaf->leaf_brush_count; offset++)
			{
				uint32_t brush = world->leaf_brushes[
					leaf->first_leaf_brush + offset];
				if (brush >= world->brush_count)
					goto done;
				brush_marks[brush] = 1;
			}
			continue;
		}
		if ((uint32_t)child >= world->node_count)
			goto done;
		if (visited[child])
			continue;
		visited[child] = 1;
		if (!Grow((void **)&pending, &pending_capacity, pending_count + 2U,
			UINT32_MAX, sizeof(*pending)))
			goto done;
		pending[pending_count++] = world->nodes[child].children[0];
		pending[pending_count++] = world->nodes[child].children[1];
	}
	result = 1;

done:
	free(visited);
	free(pending);
	return result;
}

static int AuditHookVertices(const sg_bsp_world_t *world,
	const sg_bsp_brush_t *brush, uint32_t target,
	sg_rune_vec3_t **vertices_out, uint32_t *count_out)
{
	semantic_audit_constraints_t constraints = { 0 };
	sg_rune_vec3_t *vertices = NULL;
	uint32_t count = 0, capacity = 0;
	uint32_t side, a, b, c;

	*vertices_out = NULL;
	*count_out = 0;
	for (side = 0; side < brush->side_count; side++)
	{
		uint32_t side_index = brush->first_side + side;
		const sg_bsp_brush_side_t *record;
		const sg_bsp_plane_t *plane;
		if (side_index >= world->brush_side_count)
			goto failure;
		record = &world->brush_sides[side_index];
		if (record->plane >= world->plane_count)
			goto failure;
		plane = &world->planes[record->plane];
		if (!AuditAppend(&constraints, plane->normal.value, plane->distance))
			goto failure;
	}
	for (a = 0; a < constraints.count; a++)
		for (b = a + 1U; b < constraints.count; b++)
			for (c = b + 1U; c < constraints.count; c++)
			{
				float point[3];
				uint32_t prior;
				if (!AuditIntersection(&constraints.items[a].halfspace,
					&constraints.items[b].halfspace,
					&constraints.items[c].halfspace, point) ||
					!AuditPointInside(point, &constraints) ||
					fabsf(Dot(point,
						constraints.items[target].halfspace.normal) -
						constraints.items[target].halfspace.distance) >
						SEMANTICS_POINT_EPSILON * 4.0f)
					continue;
				for (prior = 0; prior < count; prior++)
					if (SamePoint(&vertices[prior], point))
						break;
				if (prior != count)
					continue;
				if (!Grow((void **)&vertices, &capacity, count + 1U,
					UINT32_MAX, sizeof(*vertices)))
					goto failure;
				Copy3(vertices[count++].value, point);
			}
	free(constraints.items);
	*vertices_out = vertices;
	*count_out = count;
	return 1;

failure:
	free(vertices);
	free(constraints.items);
	return 0;
}

static int AuditHookSurfaces(const sg_host_collision_authority_t *authority,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *result)
{
	const sg_bsp_world_t *world = authority->world;
	uint8_t *marks = calloc(world->brush_count ? world->brush_count : 1U, 1);
	uint32_t expected = 0, expected_vertex = 0, model;

	if (!marks)
		goto solver_failure;
	for (model = 0; model < world->model_count; model++)
	{
		uint32_t brush;
		memset(marks, 0, world->brush_count);
		if (!AuditReachableBrushes(world, model, marks))
			goto solver_failure;
		for (brush = 0; brush < world->brush_count; brush++)
		{
			const sg_bsp_brush_t *brush_record = &world->brushes[brush];
			uint32_t contents = (uint32_t)brush_record->contents;
			uint32_t side;
			if (!marks[brush] || !(contents & (SG_HOST_CONTENTS_SOLID |
				SG_HOST_CONTENTS_WINDOW | SG_HOST_CONTENTS_MONSTER |
				SEMANTICS_CONTENTS_DEADMONSTER)))
				continue;
			for (side = 0; side < brush_record->side_count; side++)
			{
				uint32_t side_index = brush_record->first_side + side;
				const sg_bsp_brush_side_t *source_side;
				const sg_bsp_plane_t *plane;
				sg_rune_vec3_t *vertices = NULL;
				uint32_t vertex_count = 0, vertex, axis;
				float mins[3] = { INFINITY, INFINITY, INFINITY };
				float maxs[3] = { -INFINITY, -INFINITY, -INFINITY };
				uint32_t texinfo = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
				int32_t surface_flags = 0;
				sg_configuration_hook_surface_flags_t flags;
				const sg_configuration_hook_surface_t *actual;

				if (!AuditHookVertices(world, brush_record, side, &vertices,
					&vertex_count))
					goto solver_failure;
				if (!vertex_count)
				{
					free(vertices);
					continue;
				}
				if (expected >= semantics->hook_surface_count)
				{
					free(vertices);
					free(marks);
					result->code =
						SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_HOOK_SURFACE;
					result->record = expected;
					return 0;
				}
				if (side_index >= world->brush_side_count)
				{
					free(vertices);
					goto solver_failure;
				}
				source_side = &world->brush_sides[side_index];
				if (source_side->plane >= world->plane_count)
				{
					free(vertices);
					goto solver_failure;
				}
				plane = &world->planes[source_side->plane];
				if (source_side->texinfo >= 0)
				{
					if ((uint32_t)source_side->texinfo >= world->texinfo_count)
					{
						free(vertices);
						goto solver_failure;
					}
					texinfo = (uint32_t)source_side->texinfo;
					surface_flags = world->texinfos[source_side->texinfo].flags;
				}
				flags = (surface_flags & SG_HOST_SURFACE_SKY) ?
					SG_CONFIGURATION_HOOK_SURFACE_SKY :
					SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
				if (model)
					flags |= SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
				for (vertex = 0; vertex < vertex_count; vertex++)
					for (axis = 0; axis < 3; axis++)
					{
						float value = vertices[vertex].value[axis];
						if (value < mins[axis]) mins[axis] = value;
						if (value > maxs[axis]) maxs[axis] = value;
					}
				actual = &semantics->hook_surfaces[expected];
				if (actual->id != expected || actual->model != model ||
					actual->brush != brush || actual->brush_side != side_index ||
					actual->texinfo != texinfo ||
					actual->surface_flags != surface_flags || actual->flags != flags ||
					actual->normal[0] != plane->normal.value[0] ||
					actual->normal[1] != plane->normal.value[1] ||
					actual->normal[2] != plane->normal.value[2] ||
					actual->distance != plane->distance ||
					actual->first_vertex != expected_vertex ||
					actual->vertex_count != vertex_count ||
					!Finite3(actual->bounds.mins.value) ||
					!Finite3(actual->bounds.maxs.value) ||
					actual->first_vertex > semantics->hook_vertex_count ||
					actual->vertex_count > semantics->hook_vertex_count -
						actual->first_vertex)
					goto disagreement;
				for (axis = 0; axis < 3; axis++)
					if (fabsf(actual->bounds.mins.value[axis] - mins[axis]) >
							SEMANTICS_POINT_EPSILON ||
						fabsf(actual->bounds.maxs.value[axis] - maxs[axis]) >
							SEMANTICS_POINT_EPSILON)
						goto disagreement;
				for (vertex = 0; vertex < vertex_count; vertex++)
				{
					uint32_t candidate;
					for (candidate = 0; candidate < actual->vertex_count; candidate++)
						if (SamePoint(&vertices[vertex],
							&semantics->hook_vertices[
								actual->first_vertex + candidate].value[0]))
							break;
					if (candidate == actual->vertex_count)
						goto disagreement;
				}
				expected_vertex += vertex_count;
				expected++;
				free(vertices);
				continue;

disagreement:
				free(vertices);
				free(marks);
				result->code =
					SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT;
				result->record = expected;
				return 0;
			}
		}
	}
	free(marks);
	if (expected != semantics->hook_surface_count ||
		expected_vertex != semantics->hook_vertex_count)
	{
		result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_HOOK_SURFACE;
		result->record = expected;
		return 0;
	}
	return 1;

solver_failure:
	free(marks);
	result->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
	return 0;
}

int SG_ConfigurationSemanticsAudit(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *result_out)
{
	semantic_audit_t audit;
	uint32_t region, cell, source = 0;

	if (result_out)
		memset(result_out, 0, sizeof(*result_out));
	if (!authority || !authority->world || !configuration || !semantics ||
		!result_out || (semantics->region_count && !semantics->regions) ||
		(semantics->face_count && !semantics->faces) ||
		(semantics->vertex_count && !semantics->vertices) ||
		(semantics->boundary_count && !semantics->boundaries) ||
		(semantics->hook_surface_count && !semantics->hook_surfaces) ||
		(semantics->hook_vertex_count && !semantics->hook_vertices))
	{
		if (result_out)
			result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	if (!IdentityEqual(&authority->identity, &semantics->identity) ||
		!IdentityEqual(&configuration->identity, &semantics->identity) ||
		!AuditValidateSource(authority, configuration, &source))
	{
		result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH;
		result_out->record = source;
		return 0;
	}
	memset(&audit, 0, sizeof(audit));
	audit.authority = authority;
	audit.configuration = configuration;
	audit.semantics = semantics;
	audit.result = result_out;
	audit.seen_regions = calloc(semantics->region_count ?
		semantics->region_count : 1U, 1);
	if (!audit.seen_regions)
	{
		free(audit.support_decisions);
		result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
		return 0;
	}
	for (cell = 0; cell < configuration->cell_count; cell++)
	{
		semantic_audit_constraints_t constraints = { 0 };
		const sg_configuration_cell_t *owner = &configuration->cells[cell];
		uint32_t local;

		audit.cell = cell;
		if (!AuditBuildSupportDecisions(&audit, owner->stance, cell) ||
			!AuditOffsets(authority, owner->stance, audit.offsets))
			goto audit_source_failure;
		for (local = 0; local < owner->face_count; local++)
		{
			const sg_configuration_face_t *face =
				&configuration->faces[owner->first_face + local];
			if (!AuditAppendSource(&constraints, face->plane.normal,
				face->plane.distance, SG_CONFIGURATION_SEMANTIC_PLANE_CELL,
				owner->first_face + local, 0, 0,
				(uint8_t)(face->plane.reversed != 0U)))
			{
				free(constraints.items);
				goto audit_solver_failure;
			}
		}
		if (!AuditTree(&audit, 0, authority->world->models[0].headnode,
			&constraints))
		{
			free(constraints.items);
			free(audit.seen_regions);
			free(audit.support_decisions);
			return 0;
		}
		free(constraints.items);
	}
	for (region = 0; region < semantics->region_count; region++)
		if (!audit.seen_regions[region])
		{
			free(audit.seen_regions);
			free(audit.support_decisions);
			result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_REGION;
			result_out->record = region;
			return 0;
		}
	if (audit.expected_region != semantics->region_count)
	{
		free(audit.seen_regions);
		free(audit.support_decisions);
		result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_REGION;
		result_out->record = audit.expected_region;
		return 0;
	}
	if ((semantics->region_count ?
		semantics->regions[semantics->region_count - 1U].first_face +
			semantics->regions[semantics->region_count - 1U].face_count : 0U) !=
			semantics->face_count ||
		(semantics->face_count ?
		semantics->faces[semantics->face_count - 1U].first_vertex +
			semantics->faces[semantics->face_count - 1U].vertex_count : 0U) !=
			semantics->vertex_count ||
		semantics->lattice_solve_calls != result_out->lattice_solve_calls ||
		semantics->lattice_constraints != result_out->lattice_constraints ||
		semantics->lattice_maximum_binary_shift !=
			result_out->lattice_maximum_binary_shift)
	{
		free(audit.seen_regions);
		free(audit.support_decisions);
		result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
		result_out->record = semantics->region_count;
		return 0;
	}
	free(audit.seen_regions);
	free(audit.support_decisions);
	for (region = 0; region < semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&semantics->regions[region];
		sg_host_collision_pose_t pose;
		sg_configuration_semantic_region_flags_t expected_flags = 0;
		const sg_configuration_cell_t *owner;
		uint32_t sample;
		int samples_valid = 1;

		if (record->cell >= configuration->cell_count)
		{
			result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
			result_out->record = region;
			return 0;
		}
		owner = &configuration->cells[record->cell];
		if (record->first_face > semantics->face_count ||
			record->face_count > semantics->face_count - record->first_face)
		{
			result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
			result_out->record = region;
			return 0;
		}
		if (record->water_type & SG_HOST_CONTENTS_WATER)
			expected_flags |= SG_CONFIGURATION_SEMANTIC_REGION_WATER;
		if (record->water_type & SG_HOST_CONTENTS_LAVA)
			expected_flags |= SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
				SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
		if (record->water_type & SG_HOST_CONTENTS_SLIME)
			expected_flags |= SG_CONFIGURATION_SEMANTIC_REGION_SLIME |
				SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
		if (!SG_HostCollisionClassifyPose(authority, NULL,
			record->interior_witness.value, owner->stance, &pose) || !pose.valid)
		{
			result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
			result_out->record = region;
			return 0;
		}
		expected_flags |= pose.supported ?
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED :
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
		for (sample = 0; sample < record->face_count; sample++)
		{
			const sg_configuration_semantic_face_t *face =
				&semantics->faces[record->first_face + sample];
			if (face->source_kind == SG_CONFIGURATION_SEMANTIC_PLANE_CELL &&
				face->source_index < configuration->face_count &&
				configuration->faces[face->source_index].plane.source_kind ==
					SG_CONFIGURATION_PLANE_DOMAIN)
				expected_flags |= SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT;
		}
		for (sample = 0; sample < 3; sample++)
			if (record->sample_leaves[sample] >= authority->world->leaf_count ||
				record->sample_contents[sample] !=
				(sg_host_collision_contents_t)authority->world->leaves[
					record->sample_leaves[sample]].contents ||
				record->sample_areas[sample] != authority->world->leaves[
					record->sample_leaves[sample]].area ||
				record->sample_clusters[sample] != authority->world->leaves[
					record->sample_leaves[sample]].cluster)
				samples_valid = 0;
		if (owner->bsp_leaf.index >= authority->world->leaf_count ||
			record->origin_contents != (sg_host_collision_contents_t)
				authority->world->leaves[owner->bsp_leaf.index].contents ||
			record->origin_rune_contents != owner->contents ||
			record->flags != expected_flags || !samples_valid ||
			record->reserved[0] != 0U || record->reserved[1] != 0U ||
			record->reserved[2] != 0U ||
			record->first_face > semantics->face_count ||
			record->face_count > semantics->face_count - record->first_face ||
			pose.water_level != record->water_level ||
			pose.water_type != record->water_type)
		{
			result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT;
			result_out->record = region;
			return 0;
		}
	}
	if (!AuditBoundaries(authority, configuration, semantics, result_out))
		return 0;
	if (!AuditHookSurfaces(authority, semantics, result_out))
		return 0;
	result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_OK;
	return 1;

audit_source_failure:
	free(audit.seen_regions);
	free(audit.support_decisions);
	result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH;
	result_out->record = cell;
	return 0;
audit_solver_failure:
	free(audit.seen_regions);
	free(audit.support_decisions);
	result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER;
	result_out->record = cell;
	return 0;
}

void SG_ConfigurationSemanticsDestroy(sg_configuration_semantics_t *semantics)
{
	if (!semantics)
		return;
	free(semantics->regions);
	free(semantics->faces);
	free(semantics->vertices);
	free(semantics->boundaries);
	free(semantics->hook_surfaces);
	free(semantics->hook_vertices);
	free(semantics);
}

const char *SG_ConfigurationSemanticsErrorString(
	sg_configuration_semantics_error_code_t code)
{
	switch (code)
	{
	case SG_CONFIGURATION_SEMANTICS_ERROR_NONE: return "none";
	case SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE: return "invalid source";
	case SG_CONFIGURATION_SEMANTICS_ERROR_NONFINITE_GEOMETRY: return "nonfinite geometry";
	case SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW: return "representation overflow";
	case SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY: return "out of memory";
	case SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER: return "lattice solver failure";
	case SG_CONFIGURATION_SEMANTICS_ERROR_HOST_DISAGREEMENT: return "host disagreement";
	default: return "unknown";
	}
}

const char *SG_ConfigurationSemanticsAuditCodeString(
	sg_configuration_semantics_audit_code_t code)
{
	switch (code)
	{
	case SG_CONFIGURATION_SEMANTICS_AUDIT_OK: return "ok";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_INVALID_ARGUMENT: return "invalid argument";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH: return "source mismatch";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_REGION: return "omitted region";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_REGION: return "invented region";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT: return "region disagreement";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_BOUNDARY: return "omitted boundary";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_BOUNDARY: return "invented boundary";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_BOUNDARY_DISAGREEMENT: return "boundary disagreement";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_HOOK_SURFACE: return "omitted hook surface";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_HOOK_SURFACE: return "invented hook surface";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT: return "hook surface disagreement";
	case SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER: return "solver failure";
	default: return "unknown";
	}
}
