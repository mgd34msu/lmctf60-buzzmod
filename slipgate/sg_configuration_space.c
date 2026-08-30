#include "sg_configuration_space.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_EPSILON 0.0001f
#define CONFIG_POINT_EPSILON 0.000001f
#define CONFIG_AREA_EPSILON 0.000001f
#define CONFIG_CANONICAL_POINT_EPSILON 0.00001f

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
	uint8_t exact_split;
	uint8_t exact_bounds_valid;
	float exact_mins[3];
	float exact_maxs[3];
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

static int CanonicalizeClip(config_build_t *build,
	const config_poly_t *source, const sg_configuration_plane_t *clip,
	int keep_back, const config_poly_t *legacy, config_poly_t *result);
static int PlaneIsOpen(const sg_configuration_plane_t *plane);
static void AddLatticeStats(config_build_t *build,
	const sg_configuration_lattice_stats_t *stats);

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
	if (!source->vertex_count)
		return 1;
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
	destination->exact_split = source->exact_split;
	destination->exact_bounds_valid = source->exact_bounds_valid;
	CopyVector(destination->exact_mins, source->exact_mins);
	CopyVector(destination->exact_maxs, source->exact_maxs);
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
	result->exact_split = source->exact_split;
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

static int PolySideHasQ8(config_build_t *build, const config_poly_t *source,
	const sg_configuration_plane_t *plane, int keep_back)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_configuration_lattice_stats_t stats = { 0 };
	sg_configuration_plane_t side = *plane;
	int32_t point[3];
	uint32_t face, axis;
	int result;

	if (source->face_count == UINT32_MAX)
		return -1;
#if SIZE_MAX == UINT32_MAX
	if ((size_t)(source->face_count + 1U) > SIZE_MAX / sizeof(*halfspaces))
		return -1;
#endif
	halfspaces = calloc((size_t)(source->face_count + 1U),
		sizeof(*halfspaces));
	if (!halfspaces)
		return -1;
	if (!keep_back)
	{
		for (axis = 0; axis < 3U; axis++)
			side.normal[axis] = -side.normal[axis];
		side.distance = -side.distance;
		side.reversed ^= 1U;
	}
	for (face = 0; face < source->face_count; face++)
	{
		CopyVector(halfspaces[face].normal, source->faces[face].plane.normal);
		halfspaces[face].distance = source->faces[face].plane.distance;
		halfspaces[face].open = PlaneIsOpen(&source->faces[face].plane);
	}
	CopyVector(halfspaces[source->face_count].normal, side.normal);
	halfspaces[source->face_count].distance = side.distance;
	halfspaces[source->face_count].open = PlaneIsOpen(&side);
	result = SG_ConfigurationLatticeFind(halfspaces, source->face_count + 1U,
		NULL, point, &stats);
	free(halfspaces);
	AddLatticeStats(build, &stats);
	return result;
}

/* Returns -1 on allocation failure, 0 when unsplit, and 1 when split. */
static int SplitPoly(config_build_t *build, const config_poly_t *source,
	const sg_configuration_plane_t *plane, config_poly_t *front,
	config_poly_t *back)
{
	config_poly_t canonical;
	float minimum = INFINITY;
	float maximum = -INFINITY;
	uint32_t face, vertex;
	int status;

	memset(front, 0, sizeof(*front));
	memset(back, 0, sizeof(*back));
	if (source->exact_split)
	{
		config_poly_t empty = { 0 };
		int has_front = PolySideHasQ8(build, source, plane, 0);
		int has_back = PolySideHasQ8(build, source, plane, 1);

		if (has_front < 0 || has_back < 0)
			return -1;
		if (!has_front && !has_back)
			return 0;
		if (!has_front)
			return CopyPoly(source, back) ? 0 : -1;
		if (!has_back)
			return CopyPoly(source, front) ? 0 : -1;
		status = CanonicalizeClip(build, source, plane, 0, &empty, front);
		if (status <= 0)
			goto failure;
		status = CanonicalizeClip(build, source, plane, 1, &empty, back);
		if (status <= 0)
			goto failure;
		return 1;
	}
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
	status = CanonicalizeClip(build, source, plane, 0, front, &canonical);
	if (status <= 0)
		goto failure;
	if (status == 1)
	{
		FreePoly(front);
		*front = canonical;
	}
	status = CanonicalizeClip(build, source, plane, 1, back, &canonical);
	if (status <= 0)
		goto failure;
	if (status == 1)
	{
		FreePoly(back);
		*back = canonical;
	}
	return 1;

failure:
	FreePoly(front);
	FreePoly(back);
	return -1;
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

static void AddLatticeStats(config_build_t *build,
	const sg_configuration_lattice_stats_t *stats)
{
	build->space->lattice_solve_calls += stats->solve_calls;
	build->space->lattice_constraints += stats->constraints;
	if (stats->maximum_binary_shift >
		build->space->lattice_maximum_binary_shift)
		build->space->lattice_maximum_binary_shift =
			stats->maximum_binary_shift;
}

static int CanonicalSamePoint(const sg_rune_vec3_t *point,
	const float value[3])
{
	return fabsf(point->value[0] - value[0]) <=
			CONFIG_CANONICAL_POINT_EPSILON &&
		fabsf(point->value[1] - value[1]) <=
			CONFIG_CANONICAL_POINT_EPSILON &&
		fabsf(point->value[2] - value[2]) <=
			CONFIG_CANONICAL_POINT_EPSILON;
}

static int CanonicalIntersect3(const sg_configuration_plane_t *a,
	const sg_configuration_plane_t *b, const sg_configuration_plane_t *c,
	float point[3])
{
	double n0[3], n1[3], n2[3];
	double cross12[3], cross20[3], cross01[3], determinant;
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
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
	for (axis = 0; axis < 3U; axis++)
	{
		double value = ((double)a->distance * cross12[axis] +
			(double)b->distance * cross20[axis] +
			(double)c->distance * cross01[axis]) / determinant;

		point[axis] = (float)value;
		if (!isfinite(point[axis]))
			return 0;
	}
	return 1;
}

static int CanonicalPointInside(const config_poly_t *poly,
	const uint8_t *active, const float point[3])
{
	uint32_t face;

	for (face = 0; face < poly->face_count; face++)
		if (active[face] &&
			Dot(point, poly->faces[face].plane.normal) -
				poly->faces[face].plane.distance >
				CONFIG_CANONICAL_POINT_EPSILON)
			return 0;
	return 1;
}

static int CanonicalAppendPoint(sg_rune_vec3_t **points, uint32_t *count,
	uint32_t *capacity, const float point[3])
{
	uint32_t existing;

	for (existing = 0; existing < *count; existing++)
		if (CanonicalSamePoint(&(*points)[existing], point))
			return 1;
	if (*count == UINT32_MAX ||
		!GrowArray((void **)points, capacity, *count + 1U, UINT32_MAX,
			sizeof(**points)))
		return 0;
	CopyVector((*points)[*count].value, point);
	(*count)++;
	return 1;
}

static int CanonicalClipPoints(const config_poly_t *source,
	const config_poly_t *constraints, const uint8_t *active,
	sg_rune_vec3_t **points_out, uint32_t *point_count_out)
{
	sg_rune_vec3_t *points = NULL;
	uint32_t point_count = 0, point_capacity = 0;
	uint32_t source_face, source_vertex, first, second;
	uint32_t clip = constraints->face_count - 1U;

	for (source_face = 0; source_face < source->face_count; source_face++)
		for (source_vertex = 0;
			source_vertex < source->faces[source_face].vertex_count;
			source_vertex++)
		{
			const float *seed =
				source->faces[source_face].vertices[source_vertex].value;

			if (CanonicalPointInside(constraints, active, seed) &&
				!CanonicalAppendPoint(&points, &point_count, &point_capacity,
					seed))
			{
				free(points);
				return 0;
			}
		}
	for (first = 0; first < source->face_count; first++)
		for (second = first + 1U; second < source->face_count; second++)
		{
			float point[3];

			if (!CanonicalIntersect3(&constraints->faces[clip].plane,
				&constraints->faces[first].plane,
				&constraints->faces[second].plane, point) ||
				!CanonicalPointInside(constraints, active, point))
				continue;
			if (!CanonicalAppendPoint(&points, &point_count, &point_capacity,
				point))
			{
				free(points);
				return 0;
			}
		}
	*points_out = points;
	*point_count_out = point_count;
	return 1;
}

static int CanonicalAllPoints(const config_poly_t *poly,
	const uint8_t *active, sg_rune_vec3_t **points_out,
	uint32_t *point_count_out)
{
	sg_rune_vec3_t *points = NULL;
	uint32_t point_count = 0, point_capacity = 0;
	uint32_t first, second, third;

	for (first = 0; first < poly->face_count; first++)
		if (active[first])
			for (second = first + 1U; second < poly->face_count; second++)
				if (active[second])
					for (third = second + 1U; third < poly->face_count;
						third++)
						if (active[third])
						{
							float point[3];

							if (!CanonicalIntersect3(&poly->faces[first].plane,
								&poly->faces[second].plane,
								&poly->faces[third].plane, point) ||
								!CanonicalPointInside(poly, active, point))
								continue;
							if (!CanonicalAppendPoint(&points, &point_count,
								&point_capacity, point))
							{
								free(points);
								return 0;
							}
						}
	*points_out = points;
	*point_count_out = point_count;
	return 1;
}

static uint32_t CanonicalFacePointCount(const config_mesh_face_t *face,
	const sg_rune_vec3_t *points, uint32_t point_count)
{
	uint32_t point, count = 0;

	for (point = 0; point < point_count; point++)
		if (fabsf(Dot(points[point].value, face->plane.normal) -
			face->plane.distance) <= CONFIG_CANONICAL_POINT_EPSILON * 4.0f)
			count++;
	return count;
}

static int CanonicalFaceRedundantQ8(config_build_t *build,
	const config_poly_t *poly, const uint8_t *active, uint32_t candidate)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t face, count = 0;
	int result;

	halfspaces = malloc((size_t)poly->face_count * sizeof(*halfspaces));
	if (!halfspaces)
		return -1;
	for (face = 0; face < poly->face_count; face++)
		if (active[face] && face != candidate)
		{
			CopyVector(halfspaces[count].normal,
				poly->faces[face].plane.normal);
			halfspaces[count].distance = poly->faces[face].plane.distance;
			halfspaces[count].open = PlaneIsOpen(&poly->faces[face].plane);
			count++;
		}
	for (face = 0; face < 3U; face++)
		halfspaces[count].normal[face] =
			-poly->faces[candidate].plane.normal[face];
	halfspaces[count].distance = -poly->faces[candidate].plane.distance;
	halfspaces[count].open = !PlaneIsOpen(&poly->faces[candidate].plane);
	count++;
	result = SG_ConfigurationLatticeFind(halfspaces, count, NULL, point, &stats);
	free(halfspaces);
	AddLatticeStats(build, &stats);
	return result;
}

static int CanonicalPointLess(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
	{
		if (left->value[axis] < right->value[axis])
			return 1;
		if (left->value[axis] > right->value[axis])
			return 0;
	}
	return 0;
}

static float CanonicalProjectedArea(const config_mesh_face_t *face,
	uint32_t axis)
{
	uint32_t u = (axis + 1U) % 3U;
	uint32_t v = (axis + 2U) % 3U;
	uint32_t point;
	float area = 0.0f;

	for (point = 0; point < face->vertex_count; point++)
	{
		const float *a = face->vertices[point].value;
		const float *b =
			face->vertices[(point + 1U) % face->vertex_count].value;

		area += a[u] * b[v] - a[v] * b[u];
	}
	return area;
}

static void CanonicalOrderFace(config_mesh_face_t *face,
	const config_mesh_face_t *source)
{
	float center[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t axis = DominantAxis(face->plane.normal);
	uint32_t point, component, first = 0;
	int reverse;

	for (point = 0; point < face->vertex_count; point++)
		for (component = 0; component < 3U; component++)
			center[component] += face->vertices[point].value[component];
	for (component = 0; component < 3U; component++)
		center[component] /= (float)face->vertex_count;
	for (point = 1; point < face->vertex_count; point++)
	{
		sg_rune_vec3_t value = face->vertices[point];
		float angle = CapAngle(&value, axis, center);
		uint32_t insert = point;

		while (insert > 0U)
		{
			float prior = CapAngle(&face->vertices[insert - 1U], axis, center);

			if (prior < angle || (prior == angle &&
				!CanonicalPointLess(&value, &face->vertices[insert - 1U])))
				break;
			face->vertices[insert] = face->vertices[insert - 1U];
			insert--;
		}
		face->vertices[insert] = value;
	}
	reverse = source->vertex_count >= 3U ?
		CanonicalProjectedArea(source, axis) < 0.0f :
		face->plane.normal[axis] < 0.0f;
	if (reverse)
		for (point = 0; point < face->vertex_count / 2U; point++)
		{
			sg_rune_vec3_t value = face->vertices[point];
			uint32_t opposite = face->vertex_count - point - 1U;

			face->vertices[point] = face->vertices[opposite];
			face->vertices[opposite] = value;
		}
	for (point = 1; point < face->vertex_count; point++)
		if (CanonicalPointLess(&face->vertices[point], &face->vertices[first]))
			first = point;
	while (first--)
	{
		sg_rune_vec3_t value = face->vertices[0];

		memmove(&face->vertices[0], &face->vertices[1],
			(size_t)(face->vertex_count - 1U) * sizeof(*face->vertices));
		face->vertices[face->vertex_count - 1U] = value;
	}
}

#if defined(SG_CONFIGURATION_SPACE_TESTING)
int SG_ConfigurationTestConstraintFacetWinding(void)
{
	config_mesh_face_t source;
	config_mesh_face_t facet;
	sg_rune_vec3_t vertices[4] = {
		{ { -1.0f, -1.0f, 0.0f } },
		{ { 1.0f, 1.0f, 0.0f } },
		{ { -1.0f, 1.0f, 0.0f } },
		{ { 1.0f, -1.0f, 0.0f } }
	};

	memset(&source, 0, sizeof(source));
	memset(&facet, 0, sizeof(facet));
	source.plane.normal[2] = -1.0f;
	facet.plane = source.plane;
	facet.vertices = vertices;
	facet.vertex_count = 4U;
	CanonicalOrderFace(&facet, &source);
	return CanonicalProjectedArea(&facet, 2U) < 0.0f;
}
#endif

/* Reconstructs a changed split from old vertices and new-plane intersections. */
static int CanonicalizeClip(config_build_t *build,
	const config_poly_t *source, const sg_configuration_plane_t *clip,
	int keep_back, const config_poly_t *legacy, config_poly_t *result)
{
	config_poly_t constraints;
	uint8_t *active, *constraint_only;
	sg_rune_vec3_t *points = NULL;
	uint32_t point_count = 0, active_count;
	uint32_t face;
	int changed;

	memset(result, 0, sizeof(*result));
	result->exact_split = source->exact_split;
	memset(&constraints, 0, sizeof(constraints));
	if (source->face_count == UINT32_MAX)
		return 0;
	constraints.face_count = source->face_count + 1U;
	constraints.faces = calloc(constraints.face_count,
		sizeof(*constraints.faces));
	if (!constraints.faces)
		return 0;
	for (face = 0; face < source->face_count; face++)
		constraints.faces[face].plane = source->faces[face].plane;
	constraints.faces[source->face_count].plane = *clip;
	if (!keep_back)
	{
		for (face = 0; face < 3U; face++)
			constraints.faces[source->face_count].plane.normal[face] =
				-constraints.faces[source->face_count].plane.normal[face];
		constraints.faces[source->face_count].plane.distance = -clip->distance;
		constraints.faces[source->face_count].plane.reversed ^= 1U;
	}
	active_count = constraints.face_count;
	active = malloc(constraints.face_count);
	constraint_only = calloc(constraints.face_count, 1U);
	if (!active || !constraint_only)
	{
		free(active);
		free(constraint_only);
		FreePoly(&constraints);
		return 0;
	}
	memset(active, 1, constraints.face_count);
	if (!CanonicalClipPoints(source, &constraints, active, &points,
		&point_count))
	{
		free(active);
		free(constraint_only);
		FreePoly(&constraints);
		return 0;
	}
	do
	{
		changed = 0;
		for (face = 0; face < constraints.face_count; face++)
			if (active[face] && !constraint_only[face] &&
				CanonicalFacePointCount(&constraints.faces[face],
					points, point_count) < 3U)
			{
				if (constraints.faces[face].plane.source_kind ==
					SG_CONFIGURATION_PLANE_DOMAIN)
				{
					constraint_only[face] = 1U;
					result->exact_split = 1U;
					continue;
				}
				int witness = CanonicalFaceRedundantQ8(build, &constraints,
					active, face);

				if (witness < 0)
				{
					free(points);
					free(active);
					free(constraint_only);
					FreePoly(&constraints);
					return 0;
				}
				if (witness > 0)
				{
					constraint_only[face] = 1U;
					result->exact_split = 1U;
					continue;
				}
				active[face] = 0;
				result->exact_split = 1U;
				active_count--;
				changed = 1;
				free(points);
				points = NULL;
				point_count = 0;
				if (!CanonicalAllPoints(&constraints, active, &points,
					&point_count))
				{
					free(active);
					free(constraint_only);
					FreePoly(&constraints);
					return 0;
				}
				memset(constraint_only, 0, constraints.face_count);
				break;
			}
	} while (changed);
	result->faces = calloc(active_count, sizeof(*result->faces));
	if (!result->faces)
	{
		free(points);
		free(active);
		free(constraint_only);
		FreePoly(&constraints);
		return 0;
	}
	for (face = 0; face < constraints.face_count; face++)
		if (active[face])
		{
			config_mesh_face_t *destination =
				&result->faces[result->face_count];
			const config_mesh_face_t *orientation = face < source->face_count ?
				&source->faces[face] :
				(legacy->face_count ? &legacy->faces[legacy->face_count - 1U] :
					&constraints.faces[face]);
			uint32_t point, count = CanonicalFacePointCount(
				&constraints.faces[face], points, point_count);

			destination->plane = constraints.faces[face].plane;
			if (constraint_only[face])
			{
				result->face_count++;
				continue;
			}
			destination->vertices = malloc((size_t)count *
				sizeof(*destination->vertices));
			if (!destination->vertices)
			{
				FreePoly(result);
				free(points);
				free(active);
				free(constraint_only);
				FreePoly(&constraints);
				return 0;
			}
			for (point = 0; point < point_count; point++)
				if (fabsf(Dot(points[point].value,
					constraints.faces[face].plane.normal) -
					constraints.faces[face].plane.distance) <=
					CONFIG_CANONICAL_POINT_EPSILON * 4.0f)
					destination->vertices[destination->vertex_count++] =
						points[point];
			CanonicalOrderFace(destination, orientation);
			result->face_count++;
		}
	free(points);
	free(active);
	free(constraint_only);
	FreePoly(&constraints);
	return 1;
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
	AddLatticeStats(build, &stats);
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

static int PolyExactBounds(config_build_t *build, config_poly_t *poly,
	float mins[3], float maxs[3])
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint32_t face, axis;

	if (poly->exact_bounds_valid)
	{
		CopyVector(mins, poly->exact_mins);
		CopyVector(maxs, poly->exact_maxs);
		return 1;
	}
	if (!poly->face_count)
		return 0;
	#if SIZE_MAX == UINT32_MAX
	if ((size_t)poly->face_count > SIZE_MAX / sizeof(*halfspaces))
		return -1;
	#endif
	halfspaces = malloc((size_t)poly->face_count * sizeof(*halfspaces));
	if (!halfspaces)
		return -1;
	for (face = 0; face < poly->face_count; face++)
	{
		CopyVector(halfspaces[face].normal, poly->faces[face].plane.normal);
		halfspaces[face].distance = poly->faces[face].plane.distance;
		halfspaces[face].open = PlaneIsOpen(&poly->faces[face].plane);
	}
	for (axis = 0; axis < 3U; axis++)
	{
		int32_t minimum, maximum;
		sg_configuration_lattice_stats_t stats = { 0 };
		int result;

		result = SG_ConfigurationLatticeCoordinateBounds(halfspaces,
			poly->face_count, axis, &minimum, &maximum, &stats);
		AddLatticeStats(build, &stats);
		if (result <= 0)
		{
			free(halfspaces);
			return result;
		}
		poly->exact_mins[axis] = (float)minimum * 0.125f;
		poly->exact_maxs[axis] = (float)maximum * 0.125f;
	}
	free(halfspaces);
	poly->exact_bounds_valid = 1U;
	CopyVector(mins, poly->exact_mins);
	CopyVector(maxs, poly->exact_maxs);
	return 1;
}

static int AuthoritativePolyBounds(config_build_t *build, config_poly_t *poly,
	float mins[3], float maxs[3])
{
	uint32_t axis, face;

	if (!poly->face_count)
		return 0;
	for (face = 0; face < poly->face_count; face++)
		if (!poly->faces[face].vertex_count)
			return PolyExactBounds(build, poly, mins, maxs);
	PolyBounds(poly, mins, maxs);
	for (axis = 0; axis < 3U; axis++)
		if (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
			mins[axis] > maxs[axis])
			return 0;
	return 1;
}

#if defined(SG_CONFIGURATION_SPACE_TESTING)
int SG_ConfigurationTestFinalRepresentationBounds(void)
{
	sg_rune_bounds_t bounds;
	sg_configuration_plane_t redundant, clip;
	sg_configuration_space_t space;
	config_build_t build;
	config_poly_t source, result, empty = { 0 };
	float mins[3], maxs[3], vertex_mins[3], vertex_maxs[3];
	uint32_t face;
	int valid = 0;

	memset(&bounds, 0, sizeof(bounds));
	memset(&redundant, 0, sizeof(redundant));
	memset(&clip, 0, sizeof(clip));
	memset(&space, 0, sizeof(space));
	memset(&build, 0, sizeof(build));
	memset(&source, 0, sizeof(source));
	memset(&result, 0, sizeof(result));
	for (face = 0; face < 3U; face++)
	{
		bounds.mins.value[face] = -1.0f;
		bounds.maxs.value[face] = 1.0f;
	}
	if (!BoxPoly(&bounds, &source))
		goto done;
	for (face = 0; face < source.face_count; face++)
	{
		source.faces[face].plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
		source.faces[face].plane.source_index = face;
	}
	{
		config_mesh_face_t *grown = realloc(source.faces,
			(size_t)(source.face_count + 1U) * sizeof(*grown));

		if (!grown)
			goto done;
		source.faces = grown;
		memset(&source.faces[source.face_count], 0,
			sizeof(source.faces[source.face_count]));
	}
	redundant.normal[0] = 1.0f;
	redundant.distance = 2.0f;
	redundant.source_kind = SG_CONFIGURATION_PLANE_BSP;
	redundant.source_index = 6U;
	source.faces[source.face_count].plane = redundant;
	source.face_count++;
	source.exact_split = 1U;
	clip.normal[0] = 1.0f;
	clip.normal[1] = 0.1f;
	clip.source_kind = SG_CONFIGURATION_PLANE_BSP;
	clip.source_index = 7U;
	build.space = &space;
	if (!CanonicalizeClip(&build, &source, &clip, 1, &empty, &result) ||
		!result.exact_split || result.face_count != 6U)
		goto done;
	for (face = 0; face < result.face_count; face++)
		if (!result.faces[face].vertex_count)
			goto done;
	PolyBounds(&result, vertex_mins, vertex_maxs);
	if (AuthoritativePolyBounds(&build, &result, mins, maxs) <= 0)
		goto done;
	for (face = 0; face < 3U; face++)
		if (mins[face] != vertex_mins[face] || maxs[face] != vertex_maxs[face])
			goto done;
	if (vertex_mins[0] != -1.0f ||
		fabsf(vertex_maxs[0] - 0.1f) > CONFIG_POINT_EPSILON)
		goto done;
	{
		config_mesh_face_t *grown = realloc(result.faces,
			(size_t)(result.face_count + 1U) * sizeof(*grown));

		if (!grown)
			goto done;
		result.faces = grown;
		memset(&result.faces[result.face_count], 0,
			sizeof(result.faces[result.face_count]));
		result.faces[result.face_count].plane = redundant;
		result.face_count++;
		result.exact_bounds_valid = 0U;
	}
	if (AuthoritativePolyBounds(&build, &result, mins, maxs) <= 0 ||
		mins[0] != -1.0f || maxs[0] != 0.0f)
		goto done;
	valid = 1;

done:
	FreePoly(&result);
	FreePoly(&source);
	return valid;
}
#endif

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

static int AppendCell(config_build_t *build, config_poly_t *poly,
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
		destination->kind = source->vertex_count ?
			SG_CONFIGURATION_FACE_FACET :
			SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
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
			if (source->vertex_count)
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
	if (AuthoritativePolyBounds(build, poly, cell->bounds.mins.value,
			cell->bounds.maxs.value) <= 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT, leaf_index);
		return 0;
	}
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
	int bounds = AuthoritativePolyBounds(build, poly, poly_mins, poly_maxs);

	if (bounds < 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT, leaf);
		return 0;
	}
	if (!bounds)
		return EmptyTerminal(build, stance, leaf, node_out);
	for (brush = first_brush; brush < world->brush_count; brush++)
		if (build->world_brushes[brush] && BlockingBrush(&world->brushes[brush]) &&
			build->brush_bounds[stance][brush].usable &&
			BoundsOverlap(poly_mins, poly_maxs,
				build->brush_bounds[stance][brush].mins,
				build->brush_bounds[stance][brush].maxs))
			return CarveBrushSide(build, poly, stance, leaf, brush, 0,
				brush + 1U, node_out);
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
	split = SplitPoly(build, poly, &plane, &front, &back);
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
	split = SplitPoly(build, poly, &plane, &front, &back);
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

		int bounded = AuthoritativePolyBounds(build, &clipped, bounds->mins,
			bounds->maxs);

		if (bounded < 0)
		{
			FreePoly(&clipped);
			return 0;
		}
		bounds->usable = bounded != 0;
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

static int EquivalentPlaneGeometry(const sg_configuration_plane_t *a,
	const sg_configuration_plane_t *b)
{
	float normal_a[3], normal_b[3], distance_a, distance_b;

	CanonicalPlane(a, normal_a, &distance_a);
	CanonicalPlane(b, normal_b, &distance_b);
	return fabsf(normal_a[0] - normal_b[0]) <= CONFIG_POINT_EPSILON &&
		fabsf(normal_a[1] - normal_b[1]) <= CONFIG_POINT_EPSILON &&
		fabsf(normal_a[2] - normal_b[2]) <= CONFIG_POINT_EPSILON &&
		fabsf(distance_a - distance_b) <= CONFIG_POINT_EPSILON;
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

static int HostValidatedCandidate(
	const sg_host_collision_authority_t *authority, sg_rune_stance_t stance,
	const int32_t primary[3], const int32_t fallback[3], float witness[3])
{
	sg_host_collision_pose_t pose;
	uint32_t axis;
	int classified;

	for (axis = 0U; axis < 3U; axis++)
		witness[axis] = (float)primary[axis] * 0.125f;
	classified = SG_HostCollisionClassifyPose(authority, NULL, witness, stance,
		&pose);
	if (!classified)
		return -1;
	if (pose.valid)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		witness[axis] = (float)fallback[axis] * 0.125f;
	classified = SG_HostCollisionClassifyPose(authority, NULL, witness, stance,
		&pose);
	return classified && pose.valid ? 1 : -1;
}

static int FindPortalSideWitness(config_build_t *build, uint32_t cell_index,
	uint32_t other_cell_index, const sg_configuration_face_t *boundary,
	float witness[3])
{
	const sg_configuration_cell_t *cell = &build->space->cells[cell_index];
	const sg_configuration_cell_t *other =
		&build->space->cells[other_cell_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	const sg_configuration_plane_t **planes;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3], clearance_point[3];
	uint32_t offset, constraint_count;
	int result, positive_margin;

	if (cell->face_count > UINT32_MAX - other->face_count)
		return -1;
	halfspaces = calloc((size_t)cell->face_count + other->face_count,
		sizeof(*halfspaces));
	planes = calloc((size_t)cell->face_count + other->face_count,
		sizeof(*planes));
	clearance = calloc((size_t)cell->face_count + other->face_count,
		sizeof(*clearance));
	if (!halfspaces || !planes || !clearance)
	{
		free(halfspaces);
		free(planes);
		free(clearance);
		return -1;
	}
	constraint_count = 0U;
	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_plane_t *plane =
			&build->space->faces[cell->first_face + offset].plane;
		uint32_t existing;

		for (existing = 0U; existing < constraint_count; existing++)
			if (EquivalentPlaneGeometry(planes[existing], plane) &&
				Dot(planes[existing]->normal, plane->normal) > 0.0f)
			{
				if (PlaneIsOpen(plane))
					halfspaces[existing].open = 1U;
				if (EquivalentPlaneGeometry(plane, &boundary->plane))
					clearance[existing] = 0U;
				break;
			}
		if (existing < constraint_count)
			continue;
		planes[constraint_count] = plane;
		CopyVector(halfspaces[constraint_count].normal, plane->normal);
		halfspaces[constraint_count].distance = plane->distance;
		halfspaces[constraint_count].open = 1;
		clearance[constraint_count++] =
			(uint8_t)!EquivalentPlaneGeometry(plane, &boundary->plane);
	}
	for (offset = 0; offset < other->face_count; offset++)
	{
		const sg_configuration_plane_t *plane =
			&build->space->faces[other->first_face + offset].plane;
		int interface = EquivalentPlaneGeometry(plane, &boundary->plane);
		uint32_t existing;

		if (interface)
			continue;
		for (existing = 0U; existing < constraint_count; existing++)
			if (EquivalentPlaneGeometry(planes[existing], plane) &&
				Dot(planes[existing]->normal, plane->normal) > 0.0f)
			{
				if (PlaneIsOpen(plane))
					halfspaces[existing].open = 1U;
				break;
			}
		if (existing < constraint_count)
			continue;
		planes[constraint_count] = plane;
		CopyVector(halfspaces[constraint_count].normal, plane->normal);
		halfspaces[constraint_count].distance = plane->distance;
		halfspaces[constraint_count].open = 1;
		clearance[constraint_count++] = 1U;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, NULL, point, &positive_margin, &stats);
	if (result > 0 && positive_margin)
	{
		memcpy(clearance_point, point, sizeof(clearance_point));
		result = SG_ConfigurationLatticeFind(halfspaces, constraint_count,
			boundary->plane.normal, point, &stats);
	}
	free(halfspaces);
	free(planes);
	free(clearance);
	build->space->lattice_solve_calls += stats.solve_calls;
	build->space->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > build->space->lattice_maximum_binary_shift)
		build->space->lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (result <= 0)
		return result;
	if (!positive_margin)
		return 0;
	return HostValidatedCandidate(build->authority, cell->stance, point,
		clearance_point, witness);
}

#if defined(SG_CONFIGURATION_SPACE_TESTING)
int SG_ConfigurationTestHostValidatedCandidate(
	const sg_host_collision_authority_t *authority, sg_rune_stance_t stance,
	const int32_t primary[3], const int32_t fallback[3], float witness[3])
{
	return HostValidatedCandidate(authority, stance, primary, fallback, witness);
}
#endif

static int PointInsideCells(const sg_configuration_space_t *space,
	const sg_configuration_cell_t *first, const sg_configuration_cell_t *second,
	const float point[3]);

static int TangentBoundaryPolygon(const sg_configuration_space_t *space,
	const sg_configuration_cell_t *first, const sg_configuration_cell_t *second,
	const sg_configuration_face_t *boundary, const float first_witness[3],
	const float second_witness[3], sg_rune_vec3_t **vertices_out,
	uint32_t *vertex_count_out, float *area_out)
{
	const sg_configuration_cell_t *cells[2] = { first, second };
	config_mesh_face_t ordered, orientation;
	sg_rune_vec3_t *vertices;
	double normal_squared = (double)Dot(boundary->plane.normal,
		boundary->plane.normal);
	double first_side, second_side, denominator, fraction;
	float center[3], tangent[3], bitangent[3];
	float minimum_slack = INFINITY, radius, scale;
	double area2 = 0.0;
	uint32_t cell, face, axis, seed_axis = 0U;

	if (!(normal_squared > 0.0) || !isfinite(normal_squared))
		return 0;
	first_side = (double)Dot(first_witness, boundary->plane.normal) -
		boundary->plane.distance;
	second_side = (double)Dot(second_witness, boundary->plane.normal) -
		boundary->plane.distance;
	denominator = first_side - second_side;
	if (first_side * second_side > 0.0 || denominator == 0.0 ||
		!isfinite(denominator))
		return 0;
	fraction = first_side / denominator;
	for (axis = 0; axis < 3U; axis++)
		center[axis] = (float)((double)first_witness[axis] + fraction *
			((double)second_witness[axis] - first_witness[axis]));
	{
		double residual = ((double)Dot(center, boundary->plane.normal) -
			boundary->plane.distance) / normal_squared;

		for (axis = 0; axis < 3U; axis++)
			center[axis] = (float)((double)center[axis] - residual *
				boundary->plane.normal[axis]);
	}
	for (axis = 1U; axis < 3U; axis++)
		if (fabsf(boundary->plane.normal[axis]) <
			fabsf(boundary->plane.normal[seed_axis]))
			seed_axis = axis;
	memset(tangent, 0, sizeof(tangent));
	tangent[seed_axis] = 1.0f;
	{
		float projection = Dot(tangent, boundary->plane.normal) /
			(float)normal_squared;
		double length_squared;

		for (axis = 0; axis < 3U; axis++)
			tangent[axis] -= projection * boundary->plane.normal[axis];
		length_squared = (double)Dot(tangent, tangent);
		if (!(length_squared > 0.0))
			return 0;
		scale = (float)(1.0 / sqrt(length_squared));
		for (axis = 0; axis < 3U; axis++)
			tangent[axis] *= scale;
	}
	bitangent[0] = boundary->plane.normal[1] * tangent[2] -
		boundary->plane.normal[2] * tangent[1];
	bitangent[1] = boundary->plane.normal[2] * tangent[0] -
		boundary->plane.normal[0] * tangent[2];
	bitangent[2] = boundary->plane.normal[0] * tangent[1] -
		boundary->plane.normal[1] * tangent[0];
	scale = (float)(1.0 / sqrt((double)Dot(bitangent, bitangent)));
	for (axis = 0; axis < 3U; axis++)
		bitangent[axis] *= scale;
	for (cell = 0; cell < 2U; cell++)
		for (face = 0; face < cells[cell]->face_count; face++)
		{
			const sg_configuration_plane_t *plane = &space->faces[
				cells[cell]->first_face + face].plane;
			double length, slack;

			if (EquivalentPlaneGeometry(plane, &boundary->plane))
				continue;
			length = sqrt((double)Dot(plane->normal, plane->normal));
			if (!(length > 0.0))
				return 0;
			slack = ((double)plane->distance - Dot(center, plane->normal)) /
				length;
			if (!(slack > 0.0))
				return 0;
			if (slack < minimum_slack)
				minimum_slack = (float)slack;
		}
	if (!isfinite(minimum_slack) ||
		!(minimum_slack > CONFIG_CANONICAL_POINT_EPSILON * 8.0f))
		return 0;
	radius = minimum_slack * 0.125f;
	vertices = malloc(4U * sizeof(*vertices));
	if (!vertices)
		return -1;
	for (face = 0; face < 4U; face++)
		for (axis = 0; axis < 3U; axis++)
			vertices[face].value[axis] = center[axis] + radius *
				((face == 0U || face == 3U) ? tangent[axis] : -tangent[axis]) +
				radius * ((face < 2U) ? bitangent[axis] : -bitangent[axis]);
	for (face = 0; face < 4U; face++)
		if (!PointInsideCells(space, first, second, vertices[face].value))
		{
			free(vertices);
			return 0;
		}
	memset(&ordered, 0, sizeof(ordered));
	memset(&orientation, 0, sizeof(orientation));
	ordered.plane = boundary->plane;
	ordered.vertices = vertices;
	ordered.vertex_count = 4U;
	orientation.plane = boundary->plane;
	CanonicalOrderFace(&ordered, &orientation);
	axis = DominantAxis(boundary->plane.normal);
	{
		uint32_t u = (axis + 1U) % 3U;
		uint32_t v = (axis + 2U) % 3U;

		for (face = 0U; face < 4U; face++)
		{
			const float *point = vertices[face].value;
			const float *next = vertices[(face + 1U) % 4U].value;

			area2 += ((double)point[u] - center[u]) *
				((double)next[v] - center[v]) -
				((double)next[u] - center[u]) *
				((double)point[v] - center[v]);
		}
	}
	*area_out = (float)(fabs(area2) /
		fabs((double)boundary->plane.normal[axis]));
	if (!(*area_out > CONFIG_AREA_EPSILON))
	{
		free(vertices);
		return 0;
	}
	*vertices_out = vertices;
	*vertex_count_out = 4U;
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
	float from_witness[3], to_witness[3];
	sg_host_collision_transition_t transition;
	uint32_t face_index = (uint32_t)(face - build->space->faces);
	const sg_configuration_face_t *from_face =
		build->space->cells[from].first_face <= face_index &&
		face_index < build->space->cells[from].first_face +
			build->space->cells[from].face_count ? face : other_face;
	const sg_configuration_face_t *to_face = from_face == face ?
		other_face : face;
	int from_result, to_result;
	uint32_t existing;
	sg_rune_vec3_t *fallback = NULL;

	for (existing = 0; existing < build->space->portal_count; existing++)
	{
		const sg_configuration_portal_t *value =
			&build->space->portals[existing];

		if (value->from_cell == from && value->to_cell == to &&
			EquivalentPlaneGeometry(&value->plane, &face->plane))
			return 1;
	}

	from_result = FindPortalSideWitness(build, from, to, from_face,
		from_witness);
	to_result = FindPortalSideWitness(build, to, from, to_face, to_witness);
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
	if (vertex_count < 3U || !(area > CONFIG_AREA_EPSILON))
	{
		int materialized = TangentBoundaryPolygon(build->space,
			&build->space->cells[from], &build->space->cells[to], from_face,
			from_witness, to_witness, &fallback, &vertex_count, &area);

		if (materialized < 0)
		{
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
			return 0;
		}
		if (!materialized)
			return 1;
		vertices = fallback;
	}

	if (index >= build->limits.max_portals || index == UINT32_MAX ||
		vertex_count > build->limits.max_vertices - build->space->vertex_count)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, index);
		free(fallback);
		return 0;
	}
	if (!GrowArray((void **)&build->space->portals, &build->portal_capacity,
			index + 1U, build->limits.max_portals, sizeof(*grown)))
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
		free(fallback);
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
			free(fallback);
			return 0;
		}
		points = build->space->vertices;
		memcpy(&points[build->space->vertex_count], vertices,
			(size_t)vertex_count * sizeof(*points));
	}
	build->space->vertex_count += vertex_count;
	build->space->portal_count++;
	free(fallback);
	return 1;
}

static int PointInsideCells(const sg_configuration_space_t *space,
	const sg_configuration_cell_t *first, const sg_configuration_cell_t *second,
	const float point[3])
{
	const sg_configuration_cell_t *cells[2] = { first, second };
	uint32_t cell, face;

	for (cell = 0; cell < 2U; cell++)
		for (face = 0; face < cells[cell]->face_count; face++)
		{
			const sg_configuration_plane_t *plane = &space->faces[
				cells[cell]->first_face + face].plane;
			double length = sqrt((double)Dot(plane->normal, plane->normal));

			if (!(length > 0.0) ||
				((double)Dot(point, plane->normal) - plane->distance) / length >
					CONFIG_CANONICAL_POINT_EPSILON)
				return 0;
		}
	return 1;
}

static int IntersectAuthoritativeBoundary(config_build_t *build,
	uint32_t first_cell, uint32_t second_cell,
	const sg_configuration_face_t *boundary, sg_rune_vec3_t **vertices_out,
	uint32_t *vertex_count_out, float *area_out)
{
	const sg_configuration_cell_t *first = &build->space->cells[first_cell];
	const sg_configuration_cell_t *second = &build->space->cells[second_cell];
	const sg_configuration_cell_t *cells[2] = { first, second };
	const sg_configuration_plane_t **planes = NULL;
	sg_rune_vec3_t *points = NULL;
	uint32_t plane_count, point_count = 0, point_capacity = 0;
	uint32_t cell, face, a, b, axis;
	config_mesh_face_t ordered, orientation;
	float scale;

	*vertices_out = NULL;
	*vertex_count_out = 0U;
	*area_out = 0.0f;
	if (first->face_count > UINT32_MAX - second->face_count)
		return 0;
	plane_count = first->face_count + second->face_count;
	#if SIZE_MAX == UINT32_MAX
	if ((size_t)plane_count > SIZE_MAX / sizeof(*planes))
		return 0;
	#endif
	planes = malloc((size_t)plane_count * sizeof(*planes));
	if (!planes)
		goto failure;
	plane_count = 0U;
	for (cell = 0; cell < 2U; cell++)
		for (face = 0; face < cells[cell]->face_count; face++)
			planes[plane_count++] = &build->space->faces[
				cells[cell]->first_face + face].plane;
	for (a = 0; a < plane_count; a++)
		for (b = a + 1U; b < plane_count; b++)
		{
			float point[3];

			if (!CanonicalIntersect3(&boundary->plane, planes[a], planes[b],
					point) || !PointInsideCells(build->space, first, second, point))
				continue;
			if (!CanonicalAppendPoint(&points, &point_count, &point_capacity,
					point))
				goto failure;
		}
	if (point_count < 3U)
		goto empty;
	memset(&ordered, 0, sizeof(ordered));
	memset(&orientation, 0, sizeof(orientation));
	ordered.plane = boundary->plane;
	ordered.vertices = points;
	ordered.vertex_count = point_count;
	orientation.plane = boundary->plane;
	if (boundary->kind == SG_CONFIGURATION_FACE_FACET)
	{
		orientation.vertices = &build->space->vertices[boundary->first_vertex];
		orientation.vertex_count = boundary->vertex_count;
	}
	CanonicalOrderFace(&ordered, &orientation);
	axis = DominantAxis(boundary->plane.normal);
	scale = fabsf(boundary->plane.normal[axis]);
	if (!(scale > CONFIG_EPSILON))
		goto empty;
	*area_out = fabsf(PolygonArea2(points, point_count, axis)) / scale;
	if (!(*area_out > CONFIG_AREA_EPSILON))
		goto empty;
	free(planes);
	*vertices_out = points;
	*vertex_count_out = point_count;
	return 1;

empty:
	free(points);
	free(planes);
	return 1;

failure:
	free(points);
	free(planes);
	return 0;
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

			record = &build->space->faces[
				build->space->cells[cell].first_face + offset];
			references[face].face =
				build->space->cells[cell].first_face + offset;
			references[face].cell = cell;
			CanonicalPlane(&record->plane, references[face].canonical_normal,
				&references[face].canonical_distance);
			for (axis = 0; axis < 3; axis++)
			{
				references[face].mins[axis] = INFINITY;
				references[face].maxs[axis] = -INFINITY;
			}
			if (record->kind == SG_CONFIGURATION_FACE_FACET)
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
			else
			{
				CopyVector(references[face].mins,
					build->space->cells[cell].bounds.mins.value);
				CopyVector(references[face].maxs,
					build->space->cells[cell].bounds.maxs.value);
			}
			drop = DominantAxis(record->plane.normal);
			sweep_axis = (drop + 1U) % 3U;
			references[face].sweep_min = references[face].mins[sweep_axis];
			references[face].sweep_max = references[face].maxs[sweep_axis];
			face++;
		}
	}
	if (!SortFaceRefs(build->space, references, face))
	{
		free(references);
		return 0;
	}
	for (group_start = 0; group_start < face; )
	{
		uint32_t group_end = group_start + 1U;
		uint32_t left, right;

		while (group_end < face &&
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
				if (!(face_a->kind == SG_CONFIGURATION_FACE_FACET &&
						face_b->kind == SG_CONFIGURATION_FACE_FACET ?
					IntersectFaces(build->space, face_a, face_b,
						&intersection, &intersection_count, &area) :
					IntersectAuthoritativeBoundary(build, a->cell, b->cell,
						face_a, &intersection, &intersection_count, &area)))
				{
					free(references);
					return 0;
				}
				if (intersection_count ||
					face_a->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY ||
					face_b->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY)
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

#if defined(SG_CONFIGURATION_SPACE_TESTING)
int SG_ConfigurationTestConstraintPortal(
	const sg_host_collision_authority_t *authority)
{
	config_build_t build;
	sg_configuration_space_t space;
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[12];
	uint32_t cell, axis;
	int valid = 0;

	memset(&build, 0, sizeof(build));
	memset(&space, 0, sizeof(space));
	memset(cells, 0, sizeof(cells));
	memset(faces, 0, sizeof(faces));
	build.authority = authority;
	build.space = &space;
	build.limits.max_portals = 8U;
	build.limits.max_vertices = 64U;
	space.identity = authority->identity;
	space.cells = cells;
	space.cell_count = 2U;
	space.faces = faces;
	space.face_count = 12U;
	for (cell = 0; cell < 2U; cell++)
	{
		cells[cell].first_face = cell * 6U;
		cells[cell].face_count = 6U;
		cells[cell].stance = SG_RUNE_STANCE_STANDING;
		for (axis = 0; axis < 3U; axis++)
		{
			float minimum = axis == 0U ? (cell ? 0.0f : -1.0f) : -1.0f;
			float maximum = axis == 0U ? (cell ? 1.0f : 0.0f) : 1.0f;
			sg_configuration_face_t *upper = &faces[cell * 6U + axis * 2U];
			sg_configuration_face_t *lower = upper + 1U;

			upper->plane.normal[axis] = 1.0f;
			upper->plane.distance = maximum;
			upper->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
			upper->plane.source_index = axis;
			upper->plane.source_variant = axis * 2U;
			upper->kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
			lower->plane.normal[axis] = -1.0f;
			lower->plane.distance = -minimum;
			lower->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
			lower->plane.source_index = axis;
			lower->plane.source_variant = axis * 2U + 1U;
			lower->plane.reversed = 1U;
			lower->kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
			cells[cell].bounds.mins.value[axis] = minimum;
			cells[cell].bounds.maxs.value[axis] = maximum;
		}
	}
	if (!BuildPortals(&build) || space.portal_count != 1U ||
		space.portals[0].from_cell != 0U || space.portals[0].to_cell != 1U ||
		space.portals[0].vertex_count < 3U ||
		!(space.portals[0].clearance > 0.0f))
		goto done;
	for (axis = 0; axis < space.portals[0].vertex_count; axis++)
		if (space.vertices[space.portals[0].first_vertex + axis].value[0] != 0.0f)
			goto done;
	valid = 1;

done:
	free(space.portals);
	free(space.vertices);
	return valid;
}
#endif

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
		if (!source->vertex_count)
		{
			poly->exact_split = 1U;
			poly->face_count++;
			continue;
		}
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

static int ConstraintPolyFromCells(const sg_configuration_space_t *space,
	uint32_t first_cell, uint32_t second_cell, config_poly_t *poly)
{
	const sg_configuration_cell_t *cells[2] = {
		&space->cells[first_cell], &space->cells[second_cell]
	};
	uint32_t cell, offset;

	memset(poly, 0, sizeof(*poly));
	poly->exact_split = 1U;
	if (cells[0]->face_count > UINT32_MAX - cells[1]->face_count)
		return 0;
	poly->faces = calloc(cells[0]->face_count + cells[1]->face_count,
		sizeof(*poly->faces));
	if (!poly->faces)
		return 0;
	for (cell = 0; cell < 2U; cell++)
		for (offset = 0; offset < cells[cell]->face_count; offset++)
		{
			poly->faces[poly->face_count].plane = space->faces[
				cells[cell]->first_face + offset].plane;
			poly->face_count++;
		}
	return 1;
}

static int AppendOverlap(config_build_t *build, uint32_t standing,
	uint32_t crouching, config_poly_t *poly, const float witness[3])
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
	if (AuthoritativePolyBounds(build, poly,
			overlap->bounds.mins.value, overlap->bounds.maxs.value) <= 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT, index);
		return 0;
	}
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
		destination->kind = source->vertex_count ?
			SG_CONFIGURATION_FACE_FACET :
			SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
		if (!GrowArray((void **)&build->space->vertices,
				&build->vertex_capacity,
				build->space->vertex_count + source->vertex_count,
				build->limits.max_vertices, sizeof(*points)))
		{
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, index);
			return 0;
		}
		points = build->space->vertices;
		if (source->vertex_count)
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
				config_poly_t canonical;
				int status;

				if (!ClipPoly(&overlap, plane, 1, &clipped))
				{
					FreePoly(&overlap);
					goto failure;
				}
				status = CanonicalizeClip(build, &overlap, plane, 1,
					&clipped, &canonical);
				FreePoly(&overlap);
				if (status <= 0)
				{
					FreePoly(&clipped);
					goto failure;
				}
				if (status == 1)
				{
					FreePoly(&clipped);
					overlap = canonical;
				}
				else
					overlap = clipped;
			}
			{
				float witness[3];
				sg_host_collision_pose_t crouching_pose;
				int representable = overlap.face_count ?
					FindProtocolWitness(build, &overlap,
						SG_RUNE_STANCE_STANDING, witness) : 0;

				if (representable < 0)
				{
					FreePoly(&overlap);
					goto failure;
				}
				if (!representable)
				{
					FreePoly(&overlap);
					if (!ConstraintPolyFromCells(build->space, standing,
						crouching, &overlap))
						goto failure;
					representable = FindProtocolWitness(build, &overlap,
						SG_RUNE_STANCE_STANDING, witness);
				}
				if (representable &&
					(!SG_HostCollisionClassifyPose(build->authority, NULL,
						witness, SG_RUNE_STANCE_CROUCHING, &crouching_pose) ||
						!crouching_pose.valid))
					representable = 0;
				if (representable < 0 || (representable &&
					!AppendOverlap(build, standing, crouching, &overlap,
						witness)))
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
