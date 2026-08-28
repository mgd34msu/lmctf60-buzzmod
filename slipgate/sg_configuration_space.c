#include "sg_configuration_space.h"
#include "sg_configuration_lattice.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_EPSILON 0.0001f
#define CONFIG_POINT_EPSILON 0.000001f
#define CONFIG_AREA_EPSILON 0.000001f

typedef struct config_mesh_face_s
{
	sg_configuration_plane_t plane;
	sg_rune_vec3_t *vertices;
	uint32_t vertex_count;
} config_mesh_face_t;

typedef struct config_poly_s
{
	config_mesh_face_t *faces;
	uint32_t face_count;
} config_poly_t;

typedef struct config_brush_bounds_s
{
	float mins[3];
	float maxs[3];
	int usable;
} config_brush_bounds_t;

typedef struct config_build_s
{
	const sg_host_collision_authority_t *authority;
	sg_configuration_limits_t limits;
	sg_configuration_space_t *space;
	uint8_t *world_brushes;
	config_brush_bounds_t *brush_bounds[SG_RUNE_STANCE_COUNT];
	uint32_t cell_capacity;
	uint32_t face_capacity;
	uint32_t vertex_capacity;
	uint32_t portal_capacity;
	uint32_t overlap_capacity;
	uint32_t certificate_capacity;
	sg_configuration_error_t error;
} config_build_t;

typedef struct config_face_ref_s
{
	uint32_t cell;
	uint32_t face;
	float mins[3];
	float maxs[3];
	float canonical_normal[3];
	float canonical_distance;
	float sweep_min;
	float sweep_max;
} config_face_ref_t;

typedef struct config_cell_ref_s
{
	uint32_t cell;
	float sweep_min;
	float sweep_max;
} config_cell_ref_t;

static int GrowArray(void **array, uint32_t *capacity, uint32_t required,
	uint32_t limit, size_t element_size)
{
	uint32_t grown_capacity;
	void *grown;

	if (required <= *capacity)
		return 1;
	if (required > limit)
		return 0;
	grown_capacity = *capacity ? *capacity : 64U;
	while (grown_capacity < required)
	{
		if (grown_capacity > limit / 2U)
		{
			grown_capacity = limit;
			break;
		}
		grown_capacity *= 2U;
	}
	if ((size_t)grown_capacity > SIZE_MAX / element_size)
		return 0;
	grown = realloc(*array, (size_t)grown_capacity * element_size);
	if (!grown)
		return 0;
	*array = grown;
	*capacity = grown_capacity;
	return 1;
}

static void SetError(config_build_t *build, sg_configuration_error_code_t code,
	uint32_t source_index)
{
	if (build->error.code == SG_CONFIGURATION_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source_index;
	}
}

static float Dot(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static void CopyVector(float destination[3], const float source[3])
{
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
}

static void Lerp(const float start[3], const float end[3], float fraction,
	float result[3])
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		result[axis] = start[axis] + fraction * (end[axis] - start[axis]);
}

static int SamePoint(const float left[3], const float right[3])
{
	return fabsf(left[0] - right[0]) <= CONFIG_POINT_EPSILON &&
		fabsf(left[1] - right[1]) <= CONFIG_POINT_EPSILON &&
		fabsf(left[2] - right[2]) <= CONFIG_POINT_EPSILON;
}

static void FreePoly(config_poly_t *poly)
{
	uint32_t face;

	if (!poly)
		return;
	for (face = 0; face < poly->face_count; face++)
		free(poly->faces[face].vertices);
	free(poly->faces);
	memset(poly, 0, sizeof(*poly));
}

static int CopyFace(const config_mesh_face_t *source,
	config_mesh_face_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	destination->plane = source->plane;
	destination->vertices = malloc((size_t)source->vertex_count *
		sizeof(*destination->vertices));
	if (!destination->vertices)
		return 0;
	memcpy(destination->vertices, source->vertices,
		(size_t)source->vertex_count * sizeof(*destination->vertices));
	destination->vertex_count = source->vertex_count;
	return 1;
}

static int CopyPoly(const config_poly_t *source, config_poly_t *destination)
{
	uint32_t face;

	memset(destination, 0, sizeof(*destination));
	destination->faces = calloc(source->face_count,
		sizeof(*destination->faces));
	if (!destination->faces)
		return 0;
	for (face = 0; face < source->face_count; face++)
	{
		if (!CopyFace(&source->faces[face], &destination->faces[face]))
		{
			destination->face_count = face;
			FreePoly(destination);
			return 0;
		}
		destination->face_count++;
	}
	return 1;
}

static int AppendPoint(sg_rune_vec3_t **points, uint32_t *count,
	const float point[3], int unique)
{
	sg_rune_vec3_t *grown;
	uint32_t index;

	if (unique)
		for (index = 0; index < *count; index++)
			if (SamePoint((*points)[index].value, point))
				return 1;
	if (*count == UINT32_MAX)
		return 0;
	grown = realloc(*points, (size_t)(*count + 1U) * sizeof(*grown));
	if (!grown)
		return 0;
	*points = grown;
	CopyVector(grown[*count].value, point);
	(*count)++;
	return 1;
}

static int ClipPolygon(const config_mesh_face_t *face,
	const sg_configuration_plane_t *clip, int keep_back,
	config_mesh_face_t *result, sg_rune_vec3_t **cuts, uint32_t *cut_count)
{
	uint32_t edge;

	memset(result, 0, sizeof(*result));
	result->plane = face->plane;
	for (edge = 0; edge < face->vertex_count; edge++)
	{
		const float *start = face->vertices[edge].value;
		const float *end = face->vertices[(edge + 1U) % face->vertex_count].value;
		float start_distance = Dot(start, clip->normal) - clip->distance;
		float end_distance = Dot(end, clip->normal) - clip->distance;
		int start_inside = keep_back ? start_distance <= 0.0f :
			start_distance >= 0.0f;
		int end_inside = keep_back ? end_distance <= 0.0f :
			end_distance >= 0.0f;

		if (start_inside && !AppendPoint(&result->vertices,
				&result->vertex_count, start, 0))
			goto failure;
		if (start_inside != end_inside)
		{
			float denominator = start_distance - end_distance;
			float intersection[3];
			float fraction;

			if (denominator == 0.0f)
				goto failure;
			fraction = start_distance / denominator;
			if (fraction < 0.0f)
				fraction = 0.0f;
			else if (fraction > 1.0f)
				fraction = 1.0f;
			Lerp(start, end, fraction, intersection);
			if (!AppendPoint(&result->vertices, &result->vertex_count,
					intersection, 0) ||
				!AppendPoint(cuts, cut_count, intersection, 1))
				goto failure;
		}
	}
	if (result->vertex_count < 3U)
	{
		free(result->vertices);
		memset(result, 0, sizeof(*result));
	}
	return 1;

failure:
	free(result->vertices);
	memset(result, 0, sizeof(*result));
	return 0;
}

static uint32_t DominantAxis(const float normal[3])
{
	uint32_t axis = 0;
	float largest = fabsf(normal[0]);
	uint32_t candidate;

	for (candidate = 1; candidate < 3; candidate++)
		if (fabsf(normal[candidate]) > largest)
		{
			largest = fabsf(normal[candidate]);
			axis = candidate;
		}
	return axis;
}

static float CapAngle(const sg_rune_vec3_t *point, uint32_t axis,
	const float center[3])
{
	uint32_t u = (axis + 1U) % 3U;
	uint32_t v = (axis + 2U) % 3U;

	return atan2f(point->value[v] - center[v], point->value[u] - center[u]);
}

static int AddCap(config_poly_t *poly, sg_rune_vec3_t *cuts,
	uint32_t cut_count, const sg_configuration_plane_t *plane, int keep_back)
{
	config_mesh_face_t *grown;
	config_mesh_face_t *cap;
	uint32_t index;
	uint32_t axis;
	float center[3] = { 0.0f, 0.0f, 0.0f };

	if (cut_count < 3U)
		return 1;
	for (index = 0; index < cut_count; index++)
	{
		center[0] += cuts[index].value[0];
		center[1] += cuts[index].value[1];
		center[2] += cuts[index].value[2];
	}
	for (index = 0; index < 3; index++)
		center[index] /= (float)cut_count;
	axis = DominantAxis(plane->normal);
	for (index = 1; index < cut_count; index++)
	{
		sg_rune_vec3_t value = cuts[index];
		float angle = CapAngle(&value, axis, center);
		uint32_t insert = index;

		while (insert > 0U &&
			CapAngle(&cuts[insert - 1U], axis, center) > angle)
		{
			cuts[insert] = cuts[insert - 1U];
			insert--;
		}
		cuts[insert] = value;
	}
	grown = realloc(poly->faces,
		(size_t)(poly->face_count + 1U) * sizeof(*grown));
	if (!grown)
		return 0;
	poly->faces = grown;
	cap = &poly->faces[poly->face_count];
	memset(cap, 0, sizeof(*cap));
	cap->vertices = cuts;
	cap->vertex_count = cut_count;
	cap->plane = *plane;
	if (!keep_back)
	{
		for (index = 0; index < 3; index++)
			cap->plane.normal[index] = -cap->plane.normal[index];
		cap->plane.distance = -cap->plane.distance;
		cap->plane.reversed ^= 1U;
	}
	poly->face_count++;
	return 1;
}

static int ClipPoly(const config_poly_t *source,
	const sg_configuration_plane_t *plane, int keep_back,
	config_poly_t *result)
{
	sg_rune_vec3_t *cuts = NULL;
	uint32_t cut_count = 0;
	uint32_t face;

	memset(result, 0, sizeof(*result));
	result->faces = calloc(source->face_count + 1U, sizeof(*result->faces));
	if (!result->faces)
		return 0;
	for (face = 0; face < source->face_count; face++)
	{
		config_mesh_face_t clipped;

		if (!ClipPolygon(&source->faces[face], plane, keep_back, &clipped,
				&cuts, &cut_count))
			goto failure;
		if (clipped.vertex_count)
			result->faces[result->face_count++] = clipped;
	}
	if (!result->face_count)
	{
		free(cuts);
		FreePoly(result);
		return 1;
	}
	if (!AddCap(result, cuts, cut_count, plane, keep_back))
		goto failure;
	if (cut_count < 3U)
		free(cuts);
	return 1;

failure:
	free(cuts);
	FreePoly(result);
	return 0;
}

/* Returns -1 on allocation failure, 0 when unsplit, and 1 when split. */
static int SplitPoly(const config_poly_t *source,
	const sg_configuration_plane_t *plane, config_poly_t *front,
	config_poly_t *back)
{
	float minimum = INFINITY;
	float maximum = -INFINITY;
	uint32_t face, vertex;

	memset(front, 0, sizeof(*front));
	memset(back, 0, sizeof(*back));
	for (face = 0; face < source->face_count; face++)
		for (vertex = 0; vertex < source->faces[face].vertex_count; vertex++)
		{
			float distance = Dot(source->faces[face].vertices[vertex].value,
				plane->normal) - plane->distance;

			if (distance < minimum)
				minimum = distance;
			if (distance > maximum)
				maximum = distance;
		}
	if (maximum <= 0.0f)
		return CopyPoly(source, back) ? 0 : -1;
	if (minimum >= 0.0f)
		return CopyPoly(source, front) ? 0 : -1;
	if (!ClipPoly(source, plane, 0, front) ||
		!ClipPoly(source, plane, 1, back))
	{
		FreePoly(front);
		FreePoly(back);
		return -1;
	}
	return 1;
}

static int BoxPoly(const sg_rune_bounds_t *bounds, config_poly_t *poly)
{
	static const uint32_t corners[6][4] = {
		{ 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 },
		{ 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 }
	};
	float points[8][3];
	uint32_t corner, axis, face, vertex;

	memset(poly, 0, sizeof(*poly));
	for (corner = 0; corner < 8; corner++)
		for (axis = 0; axis < 3; axis++)
			points[corner][axis] = ((corner >> axis) & 1U) ?
				bounds->maxs.value[axis] : bounds->mins.value[axis];
	poly->faces = calloc(6, sizeof(*poly->faces));
	if (!poly->faces)
		return 0;
	for (face = 0; face < 6; face++)
	{
		config_mesh_face_t *record = &poly->faces[face];

		record->vertices = calloc(4, sizeof(*record->vertices));
		if (!record->vertices)
		{
			poly->face_count = face;
			FreePoly(poly);
			return 0;
		}
		record->vertex_count = 4;
		record->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
		record->plane.source_index = face / 2U;
		record->plane.source_variant = face;
		record->plane.reversed = face & 1U;
		axis = face / 2U;
		record->plane.normal[axis] = (face & 1U) ? -1.0f : 1.0f;
		record->plane.distance = (face & 1U) ?
			-bounds->mins.value[axis] : bounds->maxs.value[axis];
		for (vertex = 0; vertex < 4; vertex++)
			CopyVector(record->vertices[vertex].value,
				points[corners[face][vertex]]);
		poly->face_count++;
	}
	return 1;
}

static void PolyBounds(const config_poly_t *poly, float mins[3], float maxs[3])
{
	uint32_t face, vertex, axis;

	for (axis = 0; axis < 3; axis++)
	{
		mins[axis] = INFINITY;
		maxs[axis] = -INFINITY;
	}
	for (face = 0; face < poly->face_count; face++)
		for (vertex = 0; vertex < poly->faces[face].vertex_count; vertex++)
			for (axis = 0; axis < 3; axis++)
			{
				float value = poly->faces[face].vertices[vertex].value[axis];

				if (value < mins[axis])
					mins[axis] = value;
				if (value > maxs[axis])
					maxs[axis] = value;
			}
}

static float Determinant3(const float a[3], const float b[3],
	const float c[3])
{
	return a[0] * (b[1] * c[2] - b[2] * c[1]) -
		a[1] * (b[0] * c[2] - b[2] * c[0]) +
		a[2] * (b[0] * c[1] - b[1] * c[0]);
}

static int PolyHasVolume(const config_poly_t *poly)
{
	float points[4][3];
	uint32_t point_count = 0;
	uint32_t face, vertex;

	for (face = 0; face < poly->face_count; face++)
		for (vertex = 0; vertex < poly->faces[face].vertex_count; vertex++)
		{
			const float *point = poly->faces[face].vertices[vertex].value;
			if (!point_count)
			{
				CopyVector(points[point_count++], point);
				continue;
			}
			if (point_count == 1U)
			{
				float delta[3];
				uint32_t axis;

				for (axis = 0; axis < 3; axis++)
					delta[axis] = point[axis] - points[0][axis];
				if (Dot(delta, delta) > CONFIG_AREA_EPSILON)
					CopyVector(points[point_count++], point);
				continue;
			}
			{
				float a[3], b[3], cross[3];
				uint32_t axis;

				for (axis = 0; axis < 3; axis++)
				{
					a[axis] = points[1][axis] - points[0][axis];
					b[axis] = point[axis] - points[0][axis];
				}
				if (point_count == 2U)
				{
					cross[0] = a[1] * b[2] - a[2] * b[1];
					cross[1] = a[2] * b[0] - a[0] * b[2];
					cross[2] = a[0] * b[1] - a[1] * b[0];
					if (Dot(cross, cross) > CONFIG_AREA_EPSILON)
						CopyVector(points[point_count++], point);
					continue;
				}
				for (axis = 0; axis < 3; axis++)
					cross[axis] = points[2][axis] - points[0][axis];
				if (fabsf(Determinant3(a, cross, b)) > CONFIG_AREA_EPSILON)
					return 1;
			}
		}
	return 0;
}

static int BoundsOverlap(const float left_mins[3], const float left_maxs[3],
	const float right_mins[3], const float right_maxs[3])
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		if (left_maxs[axis] < right_mins[axis] - CONFIG_EPSILON ||
			left_mins[axis] > right_maxs[axis] + CONFIG_EPSILON)
			return 0;
	return 1;
}

static sg_configuration_plane_t BspPlane(const sg_bsp_world_t *world,
	uint32_t plane_index)
{
	sg_configuration_plane_t result;

	memset(&result, 0, sizeof(result));
	CopyVector(result.normal, world->planes[plane_index].normal.value);
	result.distance = world->planes[plane_index].distance;
	result.source_kind = SG_CONFIGURATION_PLANE_BSP;
	result.source_index = plane_index;
	return result;
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

static const sg_rune_hull_profile_t *StanceHull(const config_build_t *build,
	sg_rune_stance_t stance)
{
	return stance == SG_RUNE_STANCE_STANDING ?
		&build->authority->identity.standing_hull :
		&build->authority->identity.crouching_hull;
}

static sg_configuration_plane_t BrushPlane(const config_build_t *build,
	uint32_t brush_index, uint32_t side_offset, sg_rune_stance_t stance)
{
	const sg_bsp_world_t *world = build->authority->world;
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	uint32_t side_index = brush->first_side + side_offset;
	uint32_t plane_index = world->brush_sides[side_index].plane;
	sg_configuration_plane_t result = BspPlane(world, plane_index);

	result.distance -= HullMinimum(StanceHull(build, stance), result.normal);
	result.source_kind = SG_CONFIGURATION_PLANE_EXPANDED_BRUSH;
	result.source_index = side_index;
	result.source_variant = (uint32_t)stance;
	return result;
}

static int BlockingBrush(const sg_bsp_brush_t *brush)
{
	uint32_t contents = (uint32_t)brush->contents;

	return brush->side_count && (contents & SG_HOST_MASK_PLAYER_SOLID) != 0U;
}

static int PlaneIsOpen(const sg_configuration_plane_t *plane)
{
	return plane->source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
		plane->reversed != 0U;
}

static int FindProtocolWitness(config_build_t *build, const config_poly_t *poly,
	sg_rune_stance_t stance, float witness[3])
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	sg_host_collision_pose_t pose;
	uint32_t face, axis;
	int result, positive_margin;

	halfspaces = malloc((size_t)poly->face_count * sizeof(*halfspaces));
	clearance = malloc((size_t)poly->face_count * sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		return -1;
	}
	for (face = 0; face < poly->face_count; face++)
	{
		CopyVector(halfspaces[face].normal, poly->faces[face].plane.normal);
		halfspaces[face].distance = poly->faces[face].plane.distance;
		halfspaces[face].open = PlaneIsOpen(&poly->faces[face].plane);
		clearance[face] = 1U;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		poly->face_count, NULL, point, &positive_margin, &stats);
	free(halfspaces);
	free(clearance);
	build->space->lattice_solve_calls += stats.solve_calls;
	build->space->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > build->space->lattice_maximum_binary_shift)
		build->space->lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (result <= 0)
		return result;
	if (!positive_margin)
		return 0;
	for (axis = 0; axis < 3; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	if (!SG_HostCollisionClassifyPose(build->authority, NULL, witness, stance,
			&pose) || !pose.valid)
		return -1;
	return 1;
}

static int AppendCertificate(config_build_t *build,
	sg_configuration_certificate_kind_t kind, sg_rune_stance_t stance,
	uint32_t leaf, uint32_t *index_out)
{
	sg_configuration_certificate_node_t *grown;
	uint32_t index = build->space->certificate_node_count;

	if (index >= build->limits.max_certificate_nodes || index == UINT32_MAX)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, index);
		return 0;
	}
	if (!GrowArray((void **)&build->space->certificate_nodes,
			&build->certificate_capacity, index + 1U,
			build->limits.max_certificate_nodes, sizeof(*grown)))
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
		return 0;
	}
	grown = build->space->certificate_nodes;
	memset(&grown[index], 0, sizeof(grown[index]));
	grown[index].kind = kind;
	grown[index].front = SG_CONFIGURATION_INDEX_NONE;
	grown[index].back = SG_CONFIGURATION_INDEX_NONE;
	grown[index].cell = SG_CONFIGURATION_INDEX_NONE;
	grown[index].blocking_brush = SG_CONFIGURATION_INDEX_NONE;
	grown[index].bsp_leaf = leaf;
	grown[index].stance = stance;
	build->space->certificate_node_count++;
	*index_out = index;
	return 1;
}

static int AppendCell(config_build_t *build, const config_poly_t *poly,
	sg_rune_stance_t stance, uint32_t leaf_index,
	const float protocol_witness[3], uint32_t *cell_out)
{
	const sg_bsp_world_t *world = build->authority->world;
	sg_configuration_cell_t *cells;
	sg_configuration_cell_t *cell;
	sg_host_collision_pose_t pose;
	float witness[3];
	uint32_t face;
	uint32_t cell_index = build->space->cell_count;

	if (cell_index >= build->limits.max_cells || cell_index == UINT32_MAX)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, cell_index);
		return 0;
	}
	if (!GrowArray((void **)&build->space->cells, &build->cell_capacity,
			cell_index + 1U, build->limits.max_cells, sizeof(*cells)))
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, cell_index);
		return 0;
	}
	cells = build->space->cells;
	cell = &cells[cell_index];
	memset(cell, 0, sizeof(*cell));
	cell->first_face = build->space->face_count;
	cell->stance = stance;
	cell->bsp_leaf.index = leaf_index;
	cell->bsp_area.index = world->leaves[leaf_index].area;
	cell->bsp_cluster.index = world->leaves[leaf_index].cluster < 0 ?
		UINT32_MAX : (uint32_t)world->leaves[leaf_index].cluster;
	cell->contents = SG_HostCollisionRuneContents(
		(uint32_t)world->leaves[leaf_index].contents);
	for (face = 0; face < poly->face_count; face++)
	{
		const config_mesh_face_t *source = &poly->faces[face];
		sg_configuration_face_t *faces;
		sg_configuration_face_t *destination;

		if (build->space->face_count >= build->limits.max_faces ||
			build->space->face_count == UINT32_MAX ||
			source->vertex_count > build->limits.max_vertices -
				build->space->vertex_count)
		{
			SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, face);
			return 0;
		}
		if (!GrowArray((void **)&build->space->faces, &build->face_capacity,
				build->space->face_count + 1U, build->limits.max_faces,
				sizeof(*faces)))
		{
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, face);
			return 0;
		}
		faces = build->space->faces;
		destination = &faces[build->space->face_count];
		destination->plane = source->plane;
		destination->first_vertex = build->space->vertex_count;
		destination->vertex_count = source->vertex_count;
		{
			sg_rune_vec3_t *points;

			if (!GrowArray((void **)&build->space->vertices,
					&build->vertex_capacity,
					build->space->vertex_count + source->vertex_count,
					build->limits.max_vertices, sizeof(*points)))
			{
				SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, face);
				return 0;
			}
			points = build->space->vertices;
			memcpy(&points[build->space->vertex_count], source->vertices,
				(size_t)source->vertex_count * sizeof(*points));
		}
		build->space->vertex_count += source->vertex_count;
		build->space->face_count++;
		cell->face_count++;
		if (source->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
			cell->witness_pose_flags |= SG_CONFIGURATION_POSE_VOID_ADJACENT;
	}
	CopyVector(witness, protocol_witness);
	CopyVector(cell->interior_witness.value, protocol_witness);
	PolyBounds(poly, cell->bounds.mins.value, cell->bounds.maxs.value);
	if (!SG_HostCollisionClassifyPose(build->authority, NULL, witness, stance,
			&pose) || !pose.valid)
	{
		SetError(build, SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT, leaf_index);
		return 0;
	}
	if (pose.supported)
		cell->witness_pose_flags |= SG_CONFIGURATION_POSE_SUPPORTED;
	else
		cell->witness_pose_flags |= SG_CONFIGURATION_POSE_AIRBORNE;
	if (pose.water_level)
		cell->witness_pose_flags |= SG_CONFIGURATION_POSE_WATER;
	if (world->leaves[leaf_index].cluster < 0)
		cell->witness_pose_flags |= SG_CONFIGURATION_POSE_VOID_ADJACENT;
	cell->witness_water_level = pose.water_level;
	cell->order.source_set_identity = build->space->identity.source_set_identity;
	cell->order.domain = SG_RUNE_ORDER_CELL;
	cell->order.source_index = leaf_index;
	cell->order.local_ordinal = cell_index;
	cell->order.variant = (uint32_t)stance;
	cell->id.value = SG_RuneModelStableIdFromOrderKey(&cell->order);
	build->space->cell_count++;
	*cell_out = cell_index;
	return 1;
}

static int EmptyTerminal(config_build_t *build, sg_rune_stance_t stance,
	uint32_t leaf, uint32_t *node_out)
{
	return AppendCertificate(build, SG_CONFIGURATION_CERTIFICATE_EMPTY,
		stance, leaf, node_out);
}

static int CarveBrushSide(config_build_t *build, config_poly_t *poly,
	sg_rune_stance_t stance, uint32_t leaf, uint32_t brush_index,
	uint32_t side_offset, uint32_t next_brush, uint32_t *node_out);

static int CarveBrushes(config_build_t *build, config_poly_t *poly,
	sg_rune_stance_t stance, uint32_t leaf, uint32_t first_brush,
	uint32_t *node_out)
{
	const sg_bsp_world_t *world = build->authority->world;
	float poly_mins[3], poly_maxs[3];
	uint32_t brush;

	PolyBounds(poly, poly_mins, poly_maxs);
	for (brush = first_brush; brush < world->brush_count; brush++)
		if (build->world_brushes[brush] && BlockingBrush(&world->brushes[brush]) &&
			build->brush_bounds[stance][brush].usable &&
			BoundsOverlap(poly_mins, poly_maxs,
				build->brush_bounds[stance][brush].mins,
				build->brush_bounds[stance][brush].maxs))
			return CarveBrushSide(build, poly, stance, leaf, brush, 0,
				brush + 1U, node_out);
	if (!PolyHasVolume(poly))
		return EmptyTerminal(build, stance, leaf, node_out);
	{
		uint32_t cell;
		float witness[3];
		int representable = FindProtocolWitness(build, poly, stance, witness);

		if (representable < 0)
		{
			SetError(build, SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT, leaf);
			return 0;
		}
		if (!representable)
			return EmptyTerminal(build, stance, leaf, node_out);
		if (!AppendCell(build, poly, stance, leaf, witness, &cell) ||
			!AppendCertificate(build, SG_CONFIGURATION_CERTIFICATE_VALID,
				stance, leaf, node_out))
			return 0;
		build->space->certificate_nodes[*node_out].cell = cell;
	}
	return 1;
}

static int CarveBrushSide(config_build_t *build, config_poly_t *poly,
	sg_rune_stance_t stance, uint32_t leaf, uint32_t brush_index,
	uint32_t side_offset, uint32_t next_brush, uint32_t *node_out)
{
	const sg_bsp_brush_t *brush =
		&build->authority->world->brushes[brush_index];
	sg_configuration_plane_t plane;
	config_poly_t front, back;
	uint32_t node, front_node, back_node;
	int split;

	if (side_offset == brush->side_count)
	{
		if (!AppendCertificate(build, SG_CONFIGURATION_CERTIFICATE_BLOCKED,
				stance, leaf, node_out))
			return 0;
		build->space->certificate_nodes[*node_out].blocking_brush = brush_index;
		return 1;
	}
	plane = BrushPlane(build, brush_index, side_offset, stance);
	split = SplitPoly(poly, &plane, &front, &back);
	if (split < 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, brush_index);
		return 0;
	}
	if (!AppendCertificate(build, SG_CONFIGURATION_CERTIFICATE_SPLIT,
			stance, leaf, &node))
	{
		FreePoly(&front);
		FreePoly(&back);
		return 0;
	}
	build->space->certificate_nodes[node].plane = plane;
	if (front.face_count)
	{
		if (!CarveBrushes(build, &front, stance, leaf, next_brush, &front_node))
			goto failure;
	}
	else if (!EmptyTerminal(build, stance, leaf, &front_node))
		goto failure;
	if (back.face_count)
	{
		if (!CarveBrushSide(build, &back, stance, leaf, brush_index,
				side_offset + 1U, next_brush, &back_node))
			goto failure;
	}
	else if (!EmptyTerminal(build, stance, leaf, &back_node))
		goto failure;
	build->space->certificate_nodes[node].front = front_node;
	build->space->certificate_nodes[node].back = back_node;
	FreePoly(&front);
	FreePoly(&back);
	*node_out = node;
	return 1;

failure:
	FreePoly(&front);
	FreePoly(&back);
	return 0;
}

static int BuildBsp(config_build_t *build, config_poly_t *poly,
	sg_rune_stance_t stance, int32_t child, uint32_t *node_out)
{
	const sg_bsp_world_t *world = build->authority->world;
	sg_configuration_plane_t plane;
	config_poly_t front, back;
	uint32_t node, front_node, back_node;
	int split;

	if (child < 0)
	{
		uint32_t leaf = (uint32_t)(-1 - child);

		return CarveBrushes(build, poly, stance, leaf, 0, node_out);
	}
	plane = BspPlane(world, world->nodes[(uint32_t)child].plane);
	split = SplitPoly(poly, &plane, &front, &back);
	if (split < 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, (uint32_t)child);
		return 0;
	}
	if (!AppendCertificate(build, SG_CONFIGURATION_CERTIFICATE_SPLIT,
			stance, SG_CONFIGURATION_INDEX_NONE, &node))
		goto failure;
	build->space->certificate_nodes[node].plane = plane;
	if (front.face_count)
	{
		if (!BuildBsp(build, &front, stance,
				world->nodes[(uint32_t)child].children[0], &front_node))
			goto failure;
	}
	else if (!EmptyTerminal(build, stance, SG_CONFIGURATION_INDEX_NONE,
			&front_node))
		goto failure;
	if (back.face_count)
	{
		if (!BuildBsp(build, &back, stance,
				world->nodes[(uint32_t)child].children[1], &back_node))
			goto failure;
	}
	else if (!EmptyTerminal(build, stance, SG_CONFIGURATION_INDEX_NONE,
			&back_node))
		goto failure;
	build->space->certificate_nodes[node].front = front_node;
	build->space->certificate_nodes[node].back = back_node;
	FreePoly(&front);
	FreePoly(&back);
	*node_out = node;
	return 1;

failure:
	FreePoly(&front);
	FreePoly(&back);
	return 0;
}

static int MarkWorldBrushes(config_build_t *build)
{
	const sg_bsp_world_t *world = build->authority->world;
	int32_t *stack;
	uint32_t count = 0;

	stack = malloc((size_t)(world->node_count + world->leaf_count) *
		sizeof(*stack));
	if (!stack)
		return 0;
	stack[count++] = world->models[0].headnode;
	while (count)
	{
		int32_t child = stack[--count];

		if (child >= 0)
		{
			stack[count++] = world->nodes[(uint32_t)child].children[0];
			stack[count++] = world->nodes[(uint32_t)child].children[1];
		}
		else
		{
			const sg_bsp_leaf_t *leaf = &world->leaves[(uint32_t)(-1 - child)];
			uint32_t offset;

			for (offset = 0; offset < leaf->leaf_brush_count; offset++)
				build->world_brushes[world->leaf_brushes[
					leaf->first_leaf_brush + offset]] = 1;
		}
	}
	free(stack);
	return 1;
}

static int ComputeBrushBounds(config_build_t *build, uint32_t brush_index,
	sg_rune_stance_t stance)
{
	const sg_bsp_brush_t *brush =
		&build->authority->world->brushes[brush_index];
	config_poly_t domain, clipped, next;
	uint32_t side;

	if (!BoxPoly(&build->space->domain, &domain))
		return 0;
	clipped = domain;
	for (side = 0; side < brush->side_count; side++)
	{
		sg_configuration_plane_t plane =
			BrushPlane(build, brush_index, side, stance);

		if (!ClipPoly(&clipped, &plane, 1, &next))
		{
			FreePoly(&clipped);
			return 0;
		}
		FreePoly(&clipped);
		clipped = next;
		if (!clipped.face_count)
			break;
	}
	if (clipped.face_count)
	{
		config_brush_bounds_t *bounds =
			&build->brush_bounds[stance][brush_index];

		PolyBounds(&clipped, bounds->mins, bounds->maxs);
		bounds->usable = 1;
	}
	FreePoly(&clipped);
	return 1;
}

static int PrepareBrushes(config_build_t *build)
{
	const sg_bsp_world_t *world = build->authority->world;
	uint32_t brush, stance;

	build->world_brushes = calloc(world->brush_count ? world->brush_count : 1U,
		sizeof(*build->world_brushes));
	if (!build->world_brushes || !MarkWorldBrushes(build))
		return 0;
	for (stance = 0; stance < SG_RUNE_STANCE_COUNT; stance++)
	{
		build->brush_bounds[stance] = calloc(
			world->brush_count ? world->brush_count : 1U,
			sizeof(*build->brush_bounds[stance]));
		if (!build->brush_bounds[stance])
			return 0;
		for (brush = 0; brush < world->brush_count; brush++)
			if (build->world_brushes[brush] &&
				BlockingBrush(&world->brushes[brush]) &&
				!ComputeBrushBounds(build, brush, (sg_rune_stance_t)stance))
				return 0;
	}
	return 1;
}

static int FaceRefCompare(const sg_configuration_space_t *space,
	const config_face_ref_t *left, const config_face_ref_t *right)
{
	const sg_configuration_cell_t *left_cell =
		&space->cells[left->cell];
	const sg_configuration_cell_t *right_cell =
		&space->cells[right->cell];

#define COMPARE_FIELD(a, b) do { if ((a) < (b)) return -1; \
	if ((a) > (b)) return 1; } while (0)
	COMPARE_FIELD(left_cell->stance, right_cell->stance);
	COMPARE_FIELD(left->canonical_normal[0], right->canonical_normal[0]);
	COMPARE_FIELD(left->canonical_normal[1], right->canonical_normal[1]);
	COMPARE_FIELD(left->canonical_normal[2], right->canonical_normal[2]);
	COMPARE_FIELD(left->canonical_distance, right->canonical_distance);
	COMPARE_FIELD(left->sweep_min, right->sweep_min);
	COMPARE_FIELD(left->sweep_max, right->sweep_max);
	COMPARE_FIELD(left->cell, right->cell);
	COMPARE_FIELD(left->face, right->face);
#undef COMPARE_FIELD
	return 0;
}

static void CanonicalPlane(const sg_configuration_plane_t *plane,
	float normal[3], float *distance)
{
	uint32_t axis, dominant = 0;
	float scale;
	int flip;

	for (axis = 1; axis < 3; axis++)
		if (fabsf(plane->normal[axis]) > fabsf(plane->normal[dominant]))
			dominant = axis;
	scale = fabsf(plane->normal[dominant]);
	flip = plane->normal[dominant] < 0.0f;
	for (axis = 0; axis < 3; axis++)
	{
		normal[axis] = (flip ? -plane->normal[axis] : plane->normal[axis]) /
			scale;
		if (normal[axis] == 0.0f)
			normal[axis] = 0.0f;
	}
	*distance = (flip ? -plane->distance : plane->distance) / scale;
	if (*distance == 0.0f)
		*distance = 0.0f;
}

static int SortFaceRefs(const sg_configuration_space_t *space,
	config_face_ref_t *values, uint32_t count)
{
	config_face_ref_t *temporary;
	uint32_t width;

	if (count < 2U)
		return 1;
	temporary = malloc((size_t)count * sizeof(*temporary));
	if (!temporary)
		return 0;
	for (width = 1U; width < count; )
	{
		uint32_t start;

		for (start = 0; start < count; start += width * 2U)
		{
			uint32_t middle = start + width < count ? start + width : count;
			uint32_t end = middle + width < count ? middle + width : count;
			uint32_t left = start, right = middle, output = start;

			while (left < middle || right < end)
				if (right == end || (left < middle &&
					FaceRefCompare(space, &values[left], &values[right]) <= 0))
					temporary[output++] = values[left++];
				else
					temporary[output++] = values[right++];
		}
		memcpy(values, temporary, (size_t)count * sizeof(*values));
		if (width > count / 2U)
			break;
		width *= 2U;
	}
	free(temporary);
	return 1;
}

static float Cross2(const float a[2], const float b[2], const float c[2])
{
	return (b[0] - a[0]) * (c[1] - a[1]) -
		(b[1] - a[1]) * (c[0] - a[0]);
}

static void Project(const float point[3], uint32_t drop, float result[2])
{
	result[0] = point[(drop + 1U) % 3U];
	result[1] = point[(drop + 2U) % 3U];
}

static float PolygonArea2(const sg_rune_vec3_t *vertices, uint32_t count,
	uint32_t drop)
{
	float area = 0.0f;
	uint32_t index;

	for (index = 0; index < count; index++)
	{
		float current[2], next[2];

		Project(vertices[index].value, drop, current);
		Project(vertices[(index + 1U) % count].value, drop, next);
		area += current[0] * next[1] - next[0] * current[1];
	}
	return area * 0.5f;
}

static int IntersectFaces(const sg_configuration_space_t *space,
	const sg_configuration_face_t *subject_face,
	const sg_configuration_face_t *clip_face, sg_rune_vec3_t **result_out,
	uint32_t *count_out, float *area_out)
{
	sg_rune_vec3_t *polygon;
	uint32_t count = subject_face->vertex_count;
	uint32_t drop = DominantAxis(subject_face->plane.normal);
	const sg_rune_vec3_t *subject =
		&space->vertices[subject_face->first_vertex];
	const sg_rune_vec3_t *clip = &space->vertices[clip_face->first_vertex];
	float orientation = PolygonArea2(clip, clip_face->vertex_count, drop);
	uint32_t edge;

	*result_out = NULL;
	*count_out = 0;
	*area_out = 0.0f;
	polygon = malloc((size_t)count * sizeof(*polygon));
	if (!polygon)
		return 0;
	memcpy(polygon, subject, (size_t)count * sizeof(*polygon));
	for (edge = 0; edge < clip_face->vertex_count && count >= 3U; edge++)
	{
		sg_rune_vec3_t *next = NULL;
		uint32_t next_count = 0;
		float clip_a[2], clip_b[2];
		uint32_t index;

		Project(clip[edge].value, drop, clip_a);
		Project(clip[(edge + 1U) % clip_face->vertex_count].value, drop, clip_b);
		for (index = 0; index < count; index++)
		{
			const sg_rune_vec3_t *a = &polygon[index];
			const sg_rune_vec3_t *b = &polygon[(index + 1U) % count];
			float projected_a[2], projected_b[2];
			float da, db;
			int inside_a, inside_b;

			Project(a->value, drop, projected_a);
			Project(b->value, drop, projected_b);
			da = Cross2(clip_a, clip_b, projected_a);
			db = Cross2(clip_a, clip_b, projected_b);
			inside_a = orientation >= 0.0f ? da >= -CONFIG_EPSILON :
				da <= CONFIG_EPSILON;
			inside_b = orientation >= 0.0f ? db >= -CONFIG_EPSILON :
				db <= CONFIG_EPSILON;
			if (inside_a && !AppendPoint(&next, &next_count, a->value, 0))
				goto failure;
			if (inside_a != inside_b)
			{
				float denominator = da - db;
				float point[3];

				if (denominator == 0.0f)
					goto failure;
				Lerp(a->value, b->value, da / denominator, point);
				if (!AppendPoint(&next, &next_count, point, 0))
					goto failure;
			}
		}
		free(polygon);
		polygon = next;
		count = next_count;
		continue;

failure:
		free(next);
		free(polygon);
		return 0;
	}
	if (count >= 3U)
	{
		float projected_area = fabsf(PolygonArea2(polygon, count, drop));
		float scale = fabsf(subject_face->plane.normal[drop]);

		if (scale > CONFIG_EPSILON)
			*area_out = projected_area / scale;
	}
	if (*area_out <= CONFIG_AREA_EPSILON)
	{
		free(polygon);
		return 1;
	}
	*result_out = polygon;
	*count_out = count;
	return 1;
}

static int SameFaceGroup(const sg_configuration_space_t *space,
	const config_face_ref_t *left, const config_face_ref_t *right)
{
	return space->cells[left->cell].stance == space->cells[right->cell].stance &&
		fabsf(left->canonical_normal[0] - right->canonical_normal[0]) <=
			CONFIG_POINT_EPSILON &&
		fabsf(left->canonical_normal[1] - right->canonical_normal[1]) <=
			CONFIG_POINT_EPSILON &&
		fabsf(left->canonical_normal[2] - right->canonical_normal[2]) <=
			CONFIG_POINT_EPSILON &&
		fabsf(left->canonical_distance - right->canonical_distance) <=
			CONFIG_POINT_EPSILON;
}

static int FindPortalSideWitness(config_build_t *build, uint32_t cell_index,
	const sg_configuration_face_t *boundary, const sg_rune_vec3_t *polygon,
	uint32_t polygon_count, const float center[3], float witness[3])
{
	const sg_configuration_cell_t *cell = &build->space->cells[cell_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	sg_host_collision_pose_t pose;
	uint32_t offset, edge, axis, constraint_count;
	int result, positive_margin;

	halfspaces = calloc((size_t)cell->face_count + polygon_count,
		sizeof(*halfspaces));
	clearance = calloc((size_t)cell->face_count + polygon_count,
		sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		return -1;
	}
	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_plane_t *plane =
			&build->space->faces[cell->first_face + offset].plane;

		CopyVector(halfspaces[offset].normal, plane->normal);
		halfspaces[offset].distance = plane->distance;
		halfspaces[offset].open = PlaneIsOpen(plane);
		clearance[offset] = plane != &boundary->plane;
	}
	constraint_count = cell->face_count;
	for (edge = 0; edge < polygon_count; edge++)
	{
		const float *a = polygon[edge].value;
		const float *b = polygon[(edge + 1U) % polygon_count].value;
		float direction[3];
		sg_configuration_lattice_halfspace_t *side =
			&halfspaces[constraint_count];

		for (axis = 0; axis < 3; axis++)
			direction[axis] = b[axis] - a[axis];
		side->normal[0] = direction[1] * boundary->plane.normal[2] -
			direction[2] * boundary->plane.normal[1];
		side->normal[1] = direction[2] * boundary->plane.normal[0] -
			direction[0] * boundary->plane.normal[2];
		side->normal[2] = direction[0] * boundary->plane.normal[1] -
			direction[1] * boundary->plane.normal[0];
		if (side->normal[0] == 0.0f && side->normal[1] == 0.0f &&
			side->normal[2] == 0.0f)
			continue;
		side->distance = Dot(side->normal, a);
		if (Dot(side->normal, center) > side->distance)
		{
			for (axis = 0; axis < 3; axis++)
				side->normal[axis] = -side->normal[axis];
			side->distance = -side->distance;
		}
		constraint_count++;
		clearance[constraint_count - 1U] = 1U;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, boundary->plane.normal, point, &positive_margin,
		&stats);
	free(halfspaces);
	free(clearance);
	build->space->lattice_solve_calls += stats.solve_calls;
	build->space->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > build->space->lattice_maximum_binary_shift)
		build->space->lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (result <= 0)
		return result;
	if (!positive_margin)
		return 0;
	for (axis = 0; axis < 3; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	if (!SG_HostCollisionClassifyPose(build->authority, NULL, witness,
			cell->stance, &pose) || !pose.valid)
		return -1;
	return 1;
}

static int AppendPortal(config_build_t *build, uint32_t from, uint32_t to,
	const sg_configuration_face_t *face, const sg_configuration_face_t *other_face,
	sg_rune_vec3_t *vertices,
	uint32_t vertex_count, float area)
{
	sg_configuration_portal_t *grown;
	sg_configuration_portal_t *portal;
	uint32_t index = build->space->portal_count;
	float center[3] = { 0.0f, 0.0f, 0.0f };
	float from_witness[3], to_witness[3];
	sg_host_collision_transition_t transition;
	uint32_t vertex, axis;
	uint32_t face_index = (uint32_t)(face - build->space->faces);
	const sg_configuration_face_t *from_face =
		build->space->cells[from].first_face <= face_index &&
		face_index < build->space->cells[from].first_face +
			build->space->cells[from].face_count ? face : other_face;
	const sg_configuration_face_t *to_face = from_face == face ?
		other_face : face;
	int from_result, to_result;

	for (vertex = 0; vertex < vertex_count; vertex++)
		for (axis = 0; axis < 3; axis++)
			center[axis] += vertices[vertex].value[axis];
	for (axis = 0; axis < 3; axis++)
		center[axis] /= (float)vertex_count;
	from_result = FindPortalSideWitness(build, from, from_face, vertices,
		vertex_count, center, from_witness);
	to_result = FindPortalSideWitness(build, to, to_face, vertices,
		vertex_count, center, to_witness);
	if (from_result < 0 || to_result < 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT, index);
		return 0;
	}
	if (!from_result || !to_result ||
		!SG_HostCollisionTransition(build->authority, NULL, from_witness,
			to_witness, build->space->cells[from].stance, &transition) ||
		!transition.clear)
		return 1;

	if (index >= build->limits.max_portals || index == UINT32_MAX ||
		vertex_count > build->limits.max_vertices - build->space->vertex_count)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, index);
		return 0;
	}
	if (!GrowArray((void **)&build->space->portals, &build->portal_capacity,
			index + 1U, build->limits.max_portals, sizeof(*grown)))
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
		return 0;
	}
	grown = build->space->portals;
	portal = &grown[index];
	memset(portal, 0, sizeof(*portal));
	portal->from_cell = from;
	portal->to_cell = to;
	portal->stance = build->space->cells[from].stance;
	portal->plane = face->plane;
	portal->first_vertex = build->space->vertex_count;
	portal->vertex_count = vertex_count;
	portal->clearance = sqrtf(area);
	portal->order.source_set_identity = build->space->identity.source_set_identity;
	portal->order.domain = SG_RUNE_ORDER_PORTAL;
	portal->order.source_index = from;
	portal->order.local_ordinal = to;
	portal->order.variant = index;
	portal->id.value = SG_RuneModelStableIdFromOrderKey(&portal->order);
	{
		sg_rune_vec3_t *points;

		if (!GrowArray((void **)&build->space->vertices,
				&build->vertex_capacity,
				build->space->vertex_count + vertex_count,
				build->limits.max_vertices, sizeof(*points)))
		{
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
			return 0;
		}
		points = build->space->vertices;
		memcpy(&points[build->space->vertex_count], vertices,
			(size_t)vertex_count * sizeof(*points));
	}
	build->space->vertex_count += vertex_count;
	build->space->portal_count++;
	return 1;
}

static int BuildPortals(config_build_t *build)
{
	config_face_ref_t *references;
	uint32_t face = 0, group_start, cell;

	references = malloc((size_t)build->space->face_count * sizeof(*references));
	if (!references && build->space->face_count)
		return 0;
	for (cell = 0; cell < build->space->cell_count; cell++)
	{
		uint32_t offset;

		for (offset = 0; offset < build->space->cells[cell].face_count; offset++)
		{
			const sg_configuration_face_t *record;
			uint32_t vertex, axis, drop, sweep_axis;

			references[face].face =
				build->space->cells[cell].first_face + offset;
			references[face].cell = cell;
			record = &build->space->faces[references[face].face];
			CanonicalPlane(&record->plane, references[face].canonical_normal,
				&references[face].canonical_distance);
			for (axis = 0; axis < 3; axis++)
			{
				references[face].mins[axis] = INFINITY;
				references[face].maxs[axis] = -INFINITY;
			}
			for (vertex = 0; vertex < record->vertex_count; vertex++)
				for (axis = 0; axis < 3; axis++)
				{
					float value = build->space->vertices[
						record->first_vertex + vertex].value[axis];

					if (value < references[face].mins[axis])
						references[face].mins[axis] = value;
					if (value > references[face].maxs[axis])
						references[face].maxs[axis] = value;
				}
			drop = DominantAxis(record->plane.normal);
			sweep_axis = (drop + 1U) % 3U;
			references[face].sweep_min = references[face].mins[sweep_axis];
			references[face].sweep_max = references[face].maxs[sweep_axis];
			face++;
		}
	}
	if (face != build->space->face_count ||
		!SortFaceRefs(build->space, references, face))
	{
		free(references);
		return 0;
	}
	for (group_start = 0; group_start < build->space->face_count; )
	{
		uint32_t group_end = group_start + 1U;
		uint32_t left, right;

		while (group_end < build->space->face_count &&
			SameFaceGroup(build->space, &references[group_start],
				&references[group_end]))
			group_end++;
		for (left = group_start; left < group_end; left++)
			for (right = left + 1U; right < group_end; right++)
			{
				const config_face_ref_t *a = &references[left];
				const config_face_ref_t *b = &references[right];
				const sg_configuration_face_t *face_a =
					&build->space->faces[a->face];
				const sg_configuration_face_t *face_b =
					&build->space->faces[b->face];
				sg_rune_vec3_t *intersection = NULL;
				uint32_t intersection_count = 0;
				float area = 0.0f;

				if (b->sweep_min > a->sweep_max + CONFIG_EPSILON)
					break;
				if (a->cell == b->cell ||
					!BoundsOverlap(a->mins, a->maxs, b->mins, b->maxs) ||
					Dot(face_a->plane.normal, face_b->plane.normal) >= 0.0f)
					continue;
				if (!IntersectFaces(build->space, face_a, face_b,
						&intersection, &intersection_count, &area))
				{
					free(references);
					return 0;
				}
				if (intersection_count)
				{
					uint32_t from = a->cell < b->cell ? a->cell : b->cell;
					uint32_t to = a->cell < b->cell ? b->cell : a->cell;

					if (!AppendPortal(build, from, to, face_a, face_b, intersection,
							intersection_count, area))
					{
						free(intersection);
						free(references);
						return 0;
					}
				}
				free(intersection);
			}
		group_start = group_end;
	}
	free(references);
	return 1;
}

static int PolyFromCell(const sg_configuration_space_t *space,
	uint32_t cell_index, config_poly_t *poly)
{
	const sg_configuration_cell_t *cell = &space->cells[cell_index];
	uint32_t offset;

	memset(poly, 0, sizeof(*poly));
	poly->faces = calloc(cell->face_count, sizeof(*poly->faces));
	if (!poly->faces)
		return 0;
	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_face_t *source =
			&space->faces[cell->first_face + offset];
		config_mesh_face_t *destination = &poly->faces[offset];

		destination->plane = source->plane;
		destination->vertex_count = source->vertex_count;
		destination->vertices = malloc((size_t)source->vertex_count *
			sizeof(*destination->vertices));
		if (!destination->vertices)
		{
			poly->face_count = offset;
			FreePoly(poly);
			return 0;
		}
		memcpy(destination->vertices,
			&space->vertices[source->first_vertex],
			(size_t)source->vertex_count * sizeof(*destination->vertices));
		poly->face_count++;
	}
	return 1;
}

static int AppendOverlap(config_build_t *build, uint32_t standing,
	uint32_t crouching, const config_poly_t *poly, const float witness[3])
{
	sg_configuration_stance_overlap_t *grown;
	sg_configuration_stance_overlap_t *overlap;
	uint32_t index = build->space->stance_overlap_count;
	uint32_t face;

	if (index >= build->limits.max_stance_overlaps || index == UINT32_MAX)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, index);
		return 0;
	}
	if (!GrowArray((void **)&build->space->stance_overlaps,
			&build->overlap_capacity, index + 1U,
			build->limits.max_stance_overlaps, sizeof(*grown)))
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
		return 0;
	}
	grown = build->space->stance_overlaps;
	overlap = &grown[index];
	memset(overlap, 0, sizeof(*overlap));
	overlap->standing_cell = standing;
	overlap->crouching_cell = crouching;
	overlap->first_face = build->space->face_count;
	PolyBounds(poly, overlap->bounds.mins.value, overlap->bounds.maxs.value);
	for (face = 0; face < poly->face_count; face++)
	{
		const config_mesh_face_t *source = &poly->faces[face];
		sg_configuration_face_t *faces;
		sg_configuration_face_t *destination;
		sg_rune_vec3_t *points;

		if (build->space->face_count >= build->limits.max_faces ||
			source->vertex_count > build->limits.max_vertices -
				build->space->vertex_count)
		{
			SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, index);
			return 0;
		}
		if (!GrowArray((void **)&build->space->faces, &build->face_capacity,
				build->space->face_count + 1U, build->limits.max_faces,
				sizeof(*faces)))
		{
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
			return 0;
		}
		faces = build->space->faces;
		destination = &faces[build->space->face_count];
		destination->plane = source->plane;
		destination->first_vertex = build->space->vertex_count;
		destination->vertex_count = source->vertex_count;
		if (!GrowArray((void **)&build->space->vertices,
				&build->vertex_capacity,
				build->space->vertex_count + source->vertex_count,
				build->limits.max_vertices, sizeof(*points)))
		{
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
			return 0;
		}
		points = build->space->vertices;
		memcpy(&points[build->space->vertex_count], source->vertices,
			(size_t)source->vertex_count * sizeof(*points));
		build->space->vertex_count += source->vertex_count;
		build->space->face_count++;
		overlap->face_count++;
	}
	CopyVector(overlap->interior_witness.value, witness);
	build->space->stance_overlap_count++;
	return 1;
}

static int CellRefCompare(const sg_configuration_space_t *space,
	const config_cell_ref_t *left, const config_cell_ref_t *right)
{
	const sg_configuration_cell_t *a = &space->cells[left->cell];
	const sg_configuration_cell_t *b = &space->cells[right->cell];

	if (a->bsp_leaf.index < b->bsp_leaf.index)
		return -1;
	if (a->bsp_leaf.index > b->bsp_leaf.index)
		return 1;
	if (a->stance < b->stance)
		return -1;
	if (a->stance > b->stance)
		return 1;
	if (left->sweep_min < right->sweep_min)
		return -1;
	if (left->sweep_min > right->sweep_min)
		return 1;
	if (left->sweep_max < right->sweep_max)
		return -1;
	if (left->sweep_max > right->sweep_max)
		return 1;
	if (left->cell < right->cell)
		return -1;
	if (left->cell > right->cell)
		return 1;
	return 0;
}

static int SortCellRefs(const sg_configuration_space_t *space,
	config_cell_ref_t *values, uint32_t count)
{
	config_cell_ref_t *temporary;
	uint32_t width;

	if (count < 2U)
		return 1;
	temporary = malloc((size_t)count * sizeof(*temporary));
	if (!temporary)
		return 0;
	for (width = 1U; width < count; )
	{
		uint32_t start;

		for (start = 0; start < count; start += width * 2U)
		{
			uint32_t middle = start + width < count ? start + width : count;
			uint32_t end = middle + width < count ? middle + width : count;
			uint32_t left = start, right = middle, output = start;

			while (left < middle || right < end)
				if (right == end || (left < middle &&
					CellRefCompare(space, &values[left], &values[right]) <= 0))
					temporary[output++] = values[left++];
				else
					temporary[output++] = values[right++];
		}
		memcpy(values, temporary, (size_t)count * sizeof(*values));
		if (width > count / 2U)
			break;
		width *= 2U;
	}
	free(temporary);
	return 1;
}

static int BuildStanceOverlaps(config_build_t *build)
{
	config_cell_ref_t *references;
	uint32_t index, group_start;

	references = malloc((size_t)build->space->cell_count * sizeof(*references));
	if (!references && build->space->cell_count)
		return 0;
	for (index = 0; index < build->space->cell_count; index++)
	{
		references[index].cell = index;
		references[index].sweep_min =
			build->space->cells[index].bounds.mins.value[0];
		references[index].sweep_max =
			build->space->cells[index].bounds.maxs.value[0];
	}
	if (!SortCellRefs(build->space, references, build->space->cell_count))
	{
		free(references);
		return 0;
	}
	for (group_start = 0; group_start < build->space->cell_count; )
	{
		uint32_t group_end = group_start + 1U;
		uint32_t standing_start, crouching_start, standing_ref, crouching_ref;

		while (group_end < build->space->cell_count &&
			build->space->cells[references[group_end].cell].bsp_leaf.index ==
				build->space->cells[references[group_start].cell].bsp_leaf.index)
			group_end++;
		standing_start = group_start;
		crouching_start = standing_start;
		while (crouching_start < group_end &&
			build->space->cells[references[crouching_start].cell].stance ==
				SG_RUNE_STANCE_STANDING)
			crouching_start++;
		for (standing_ref = standing_start; standing_ref < crouching_start;
			standing_ref++)
			for (crouching_ref = crouching_start; crouching_ref < group_end;
				crouching_ref++)
		{
			uint32_t standing = references[standing_ref].cell;
			uint32_t crouching = references[crouching_ref].cell;
			const sg_configuration_cell_t *standing_cell =
				&build->space->cells[standing];
			const sg_configuration_cell_t *crouching_cell =
				&build->space->cells[crouching];
			config_poly_t overlap, clipped;
			uint32_t face;

			if (references[crouching_ref].sweep_min >
					references[standing_ref].sweep_max + CONFIG_EPSILON)
				break;
			if (references[crouching_ref].sweep_max <
					references[standing_ref].sweep_min - CONFIG_EPSILON ||
				!BoundsOverlap(standing_cell->bounds.mins.value,
					standing_cell->bounds.maxs.value,
					crouching_cell->bounds.mins.value,
					crouching_cell->bounds.maxs.value))
				continue;
			if (!PolyFromCell(build->space, standing, &overlap))
				goto failure;
			for (face = 0; face < crouching_cell->face_count &&
				overlap.face_count; face++)
			{
				const sg_configuration_plane_t *plane =
					&build->space->faces[crouching_cell->first_face + face].plane;

				if (!ClipPoly(&overlap, plane, 1, &clipped))
				{
					FreePoly(&overlap);
					goto failure;
				}
				FreePoly(&overlap);
				overlap = clipped;
			}
			if (overlap.face_count && PolyHasVolume(&overlap))
			{
				float witness[3];

				int representable = FindProtocolWitness(build, &overlap,
					SG_RUNE_STANCE_STANDING, witness);

				if (representable < 0 || (representable &&
					!AppendOverlap(build, standing, crouching, &overlap, witness)))
				{
					FreePoly(&overlap);
					goto failure;
				}
			}
			FreePoly(&overlap);
		}
		group_start = group_end;
	}
	free(references);
	return 1;

failure:
	free(references);
	return 0;
}

static int LimitsValid(const sg_configuration_limits_t *limits)
{
	return limits && limits->max_cells && limits->max_faces &&
		limits->max_vertices && limits->max_portals &&
		limits->max_stance_overlaps &&
		limits->max_certificate_nodes;
}

static int AuthorityValid(const sg_host_collision_authority_t *authority)
{
	const sg_bsp_world_t *world;
	uint32_t axis, index;

	if (!authority || !(world = authority->world) || !world->planes ||
		!world->plane_count || !world->nodes || !world->node_count ||
		!world->leaves || !world->leaf_count || !world->models ||
		!world->model_count || (world->brush_count && !world->brushes) ||
		(world->brush_side_count && !world->brush_sides))
		return 0;
	if (world->models[0].headnode < 0 ||
		(uint32_t)world->models[0].headnode >= world->node_count)
		return 0;
	for (index = 0; index < world->plane_count; index++)
		if (!isfinite(world->planes[index].normal.value[0]) ||
			!isfinite(world->planes[index].normal.value[1]) ||
			!isfinite(world->planes[index].normal.value[2]) ||
			!isfinite(world->planes[index].distance) ||
			(world->planes[index].normal.value[0] == 0.0f &&
			 world->planes[index].normal.value[1] == 0.0f &&
			 world->planes[index].normal.value[2] == 0.0f))
			return 0;
	for (index = 0; index < world->node_count; index++)
	{
		uint32_t child;

		if (world->nodes[index].plane >= world->plane_count)
			return 0;
		for (child = 0; child < 2U; child++)
			if ((world->nodes[index].children[child] >= 0 &&
				(uint32_t)world->nodes[index].children[child] >= world->node_count) ||
				(world->nodes[index].children[child] < 0 &&
				 (uint32_t)(-1 - world->nodes[index].children[child]) >=
					world->leaf_count))
				return 0;
	}
	for (index = 0; index < world->leaf_count; index++)
	{
		const sg_bsp_leaf_t *leaf = &world->leaves[index];
		uint32_t offset;

		if (leaf->first_leaf_brush > world->leaf_brush_count ||
			leaf->leaf_brush_count > world->leaf_brush_count -
				leaf->first_leaf_brush)
			return 0;
		for (offset = 0; offset < leaf->leaf_brush_count; offset++)
			if (world->leaf_brushes[leaf->first_leaf_brush + offset] >=
				world->brush_count)
				return 0;
	}
	for (index = 0; index < world->brush_count; index++)
	{
		const sg_bsp_brush_t *brush = &world->brushes[index];
		uint32_t offset;

		if (brush->first_side > world->brush_side_count ||
			brush->side_count > world->brush_side_count - brush->first_side)
			return 0;
		for (offset = 0; offset < brush->side_count; offset++)
			if (world->brush_sides[brush->first_side + offset].plane >=
				world->plane_count)
				return 0;
	}
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(world->models[0].mins.value[axis]) ||
			!isfinite(world->models[0].maxs.value[axis]) ||
			world->models[0].mins.value[axis] >=
				world->models[0].maxs.value[axis])
			return 0;
	return 1;
}

void SG_ConfigurationDefaultLimits(sg_configuration_limits_t *limits_out)
{
	if (!limits_out)
		return;
	limits_out->max_cells = SG_CONFIGURATION_DEFAULT_MAX_CELLS;
	limits_out->max_faces = SG_CONFIGURATION_DEFAULT_MAX_FACES;
	limits_out->max_vertices = SG_CONFIGURATION_DEFAULT_MAX_VERTICES;
	limits_out->max_portals = SG_CONFIGURATION_DEFAULT_MAX_PORTALS;
	limits_out->max_stance_overlaps =
		SG_CONFIGURATION_DEFAULT_MAX_STANCE_OVERLAPS;
	limits_out->max_certificate_nodes =
		SG_CONFIGURATION_DEFAULT_MAX_CERTIFICATE_NODES;
}

int SG_ConfigurationBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out)
{
	config_build_t build;
	sg_configuration_limits_t defaults;
	config_poly_t domain;
	uint32_t stance;
	int result = 0;

	memset(&build, 0, sizeof(build));
	memset(&domain, 0, sizeof(domain));
	if (error_out)
	{
		error_out->code = SG_CONFIGURATION_ERROR_NONE;
		error_out->source_index = SG_CONFIGURATION_INDEX_NONE;
	}
	if (!space_out || *space_out || !AuthorityValid(authority))
	{
		if (error_out)
			error_out->code = !space_out || (space_out && *space_out) ?
				SG_CONFIGURATION_ERROR_INVALID_ARGUMENT :
				SG_CONFIGURATION_ERROR_INVALID_WORLD;
		return 0;
	}
	SG_ConfigurationDefaultLimits(&defaults);
	build.limits = limits ? *limits : defaults;
	if (!LimitsValid(&build.limits))
	{
		if (error_out)
			error_out->code = SG_CONFIGURATION_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	build.authority = authority;
	build.error.source_index = SG_CONFIGURATION_INDEX_NONE;
	build.space = calloc(1, sizeof(*build.space));
	if (!build.space)
	{
		build.error.code = SG_CONFIGURATION_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	build.space->identity = authority->identity;
	for (stance = 0; stance < 3; stance++)
	{
		build.space->domain.mins.value[stance] =
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN;
		build.space->domain.maxs.value[stance] =
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX;
	}
	if (!PrepareBrushes(&build))
	{
		SetError(&build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0);
		goto done;
	}
	for (stance = 0; stance < SG_RUNE_STANCE_COUNT; stance++)
	{
		if (!BoxPoly(&build.space->domain, &domain))
		{
			SetError(&build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, stance);
			goto done;
		}
		if (!BuildBsp(&build, &domain, (sg_rune_stance_t)stance,
				authority->world->models[0].headnode,
				&build.space->certificate_roots[stance]))
			goto done;
		FreePoly(&domain);
	}
	if (!BuildPortals(&build))
	{
		SetError(&build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0);
		goto done;
	}
	if (!BuildStanceOverlaps(&build))
	{
		SetError(&build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0);
		goto done;
	}
	*space_out = build.space;
	build.space = NULL;
	result = 1;

done:
	FreePoly(&domain);
	free(build.world_brushes);
	for (stance = 0; stance < SG_RUNE_STANCE_COUNT; stance++)
		free(build.brush_bounds[stance]);
	SG_ConfigurationDestroy(build.space);
	if (error_out)
		*error_out = build.error;
	return result;
}

void SG_ConfigurationDestroy(sg_configuration_space_t *space)
{
	if (!space)
		return;
	free(space->cells);
	free(space->faces);
	free(space->vertices);
	free(space->portals);
	free(space->stance_overlaps);
	free(space->certificate_nodes);
	free(space);
}

const char *SG_ConfigurationErrorString(sg_configuration_error_code_t code)
{
	switch (code)
	{
	case SG_CONFIGURATION_ERROR_NONE: return "none";
	case SG_CONFIGURATION_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_CONFIGURATION_ERROR_INVALID_WORLD: return "invalid BSP world";
	case SG_CONFIGURATION_ERROR_INVALID_HULL: return "invalid player hull";
	case SG_CONFIGURATION_ERROR_NONFINITE_GEOMETRY:
		return "non-finite geometry";
	case SG_CONFIGURATION_ERROR_DEGENERATE_GEOMETRY:
		return "degenerate geometry";
	case SG_CONFIGURATION_ERROR_OVERFLOW: return "representation overflow";
	case SG_CONFIGURATION_ERROR_OUT_OF_MEMORY: return "out of memory";
	case SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT:
		return "host collision disagreement";
	default: return "unknown configuration error";
	}
}
