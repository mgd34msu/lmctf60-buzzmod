#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PORTAL_PLANE_EPSILON 0.000001f
#define PORTAL_AREA_EPSILON 0.000001f
#define PORTAL_MATCH_EPSILON 0.001f
#define PORTAL_MATCH_ROUNDING_ULPS 32.0f

typedef struct portal_point2_s
{
	float value[2];
} portal_point2_t;

static uint32_t DominantAxis(const float normal[3])
{
	uint32_t axis = 0U;
	uint32_t candidate;

	for (candidate = 1U; candidate < 3U; candidate++)
		if (fabsf(normal[candidate]) > fabsf(normal[axis]))
			axis = candidate;
	return axis;
}

static void Project(const float point[3], uint32_t drop, float projected[2])
{
	projected[0] = point[(drop + 1U) % 3U];
	projected[1] = point[(drop + 2U) % 3U];
}

static float Cross2(const float a[2], const float b[2], const float c[2])
{
	return (b[0] - a[0]) * (c[1] - a[1]) -
		(b[1] - a[1]) * (c[0] - a[0]);
}

static void CanonicalPlane(const sg_configuration_plane_t *plane,
	float normal[3], float *distance)
{
	uint32_t axis;
	uint32_t dominant = 2U;
	float scale;
	int flip;

	for (axis = 2U; axis-- > 0U; )
		if (fabsf(plane->normal[axis]) >= fabsf(plane->normal[dominant]))
			dominant = axis;
	scale = fabsf(plane->normal[dominant]);
	flip = plane->normal[dominant] < 0.0f;
	for (axis = 0; axis < 3U; axis++)
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

static int Coplanar(const sg_configuration_plane_t *left,
	const sg_configuration_plane_t *right)
{
	float left_normal[3], right_normal[3];
	float left_distance, right_distance;

	CanonicalPlane(left, left_normal, &left_distance);
	CanonicalPlane(right, right_normal, &right_distance);
	return fabsf(left_normal[0] - right_normal[0]) <= PORTAL_PLANE_EPSILON &&
		fabsf(left_normal[1] - right_normal[1]) <= PORTAL_PLANE_EPSILON &&
		fabsf(left_normal[2] - right_normal[2]) <= PORTAL_PLANE_EPSILON &&
		fabsf(left_distance - right_distance) <= PORTAL_PLANE_EPSILON;
}

static int OpposingPlanes(const sg_configuration_plane_t *left,
	const sg_configuration_plane_t *right)
{
	return SG_BspProofDot(left->normal, right->normal) < 0.0f &&
		Coplanar(left, right);
}

static int AppendPoint2(portal_point2_t **points, uint32_t *count,
	const float point[2])
{
	portal_point2_t *grown;

	if (*count == UINT32_MAX)
		return 0;
	grown = realloc(*points, (size_t)(*count + 1U) * sizeof(*grown));
	if (!grown)
		return 0;
	*points = grown;
	grown[*count].value[0] = point[0];
	grown[*count].value[1] = point[1];
	(*count)++;
	return 1;
}

static float PolygonSignedArea2(const portal_point2_t *polygon,
	uint32_t count)
{
	float area = 0.0f;
	uint32_t index;

	for (index = 0; index < count; index++)
		area += polygon[index].value[0] *
			polygon[(index + 1U) % count].value[1] -
			polygon[(index + 1U) % count].value[0] *
			polygon[index].value[1];
	return area;
}

static float MatchTolerance(float magnitude)
{
	return fmaxf(PORTAL_MATCH_EPSILON,
		fabsf(magnitude) * FLT_EPSILON * PORTAL_MATCH_ROUNDING_ULPS);
}

static int ClipHalfspace2(portal_point2_t **polygon, uint32_t *count,
	float coefficient_u, float coefficient_v, float distance)
{
	portal_point2_t *next = NULL;
	uint32_t next_count = 0U;
	uint32_t index;

	if (fabsf(coefficient_u) + fabsf(coefficient_v) <=
		PORTAL_PLANE_EPSILON)
	{
		if (distance >= -PORTAL_PLANE_EPSILON)
			return 1;
		free(*polygon);
		*polygon = NULL;
		*count = 0U;
		return 1;
	}
	for (index = 0; index < *count; index++)
	{
		const portal_point2_t *start = &(*polygon)[index];
		const portal_point2_t *end = &(*polygon)[(index + 1U) % *count];
		float start_distance = coefficient_u * start->value[0] +
			coefficient_v * start->value[1] - distance;
		float end_distance = coefficient_u * end->value[0] +
			coefficient_v * end->value[1] - distance;
		int start_inside = start_distance <= 0.0f;
		int end_inside = end_distance <= 0.0f;

		if (start_inside && !AppendPoint2(&next, &next_count, start->value))
			goto failure;
		if (start_inside != end_inside)
		{
			float denominator = start_distance - end_distance;
			float point[2];

			if (denominator == 0.0f)
				goto failure;
			point[0] = start->value[0] + start_distance / denominator *
				(end->value[0] - start->value[0]);
			point[1] = start->value[1] + start_distance / denominator *
				(end->value[1] - start->value[1]);
			if (!AppendPoint2(&next, &next_count, point))
				goto failure;
		}
	}
	free(*polygon);
	*polygon = next;
	*count = next_count;
	return 1;

failure:
	free(next);
	return 0;
}

static int CellFacePolygon(sg_bsp_proof_context_t *proof,
	uint32_t cell_index, const sg_configuration_face_t *boundary,
	sg_rune_vec3_t **vertices_out, uint32_t *count_out)
{
	const sg_configuration_cell_t *cell = &proof->space->cells[cell_index];
	portal_point2_t *polygon = NULL;
	uint32_t count = 0U;
	uint32_t drop = DominantAxis(boundary->plane.normal);
	uint32_t u = (drop + 1U) % 3U;
	uint32_t v = (drop + 2U) % 3U;
	uint32_t index;
	float initial[4][2] = {
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN },
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MAX,
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN },
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MAX,
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX },
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX }
	};

	*vertices_out = NULL;
	*count_out = 0U;
	for (index = 0; index < 4U; index++)
		if (!AppendPoint2(&polygon, &count, initial[index]))
			goto failure;
	for (index = 0; index < cell->face_count && count >= 3U; index++)
	{
		const sg_configuration_plane_t *clip =
			&proof->space->faces[cell->first_face + index].plane;
		float ratio = clip->normal[drop] / boundary->plane.normal[drop];
		float coefficient_u = clip->normal[u] -
			ratio * boundary->plane.normal[u];
		float coefficient_v = clip->normal[v] -
			ratio * boundary->plane.normal[v];
		float distance = clip->distance - ratio * boundary->plane.distance;

		if (!ClipHalfspace2(&polygon, &count, coefficient_u, coefficient_v,
				distance))
			goto failure;
	}
	if (count >= 3U)
	{
		sg_rune_vec3_t *vertices = calloc(count, sizeof(*vertices));

		if (!vertices)
			goto failure;
		for (index = 0; index < count; index++)
		{
			vertices[index].value[u] = polygon[index].value[0];
			vertices[index].value[v] = polygon[index].value[1];
			vertices[index].value[drop] = (boundary->plane.distance -
				boundary->plane.normal[u] * vertices[index].value[u] -
				boundary->plane.normal[v] * vertices[index].value[v]) /
				boundary->plane.normal[drop];
		}
		*vertices_out = vertices;
		*count_out = count;
	}
	free(polygon);
	return 1;

failure:
	free(polygon);
	SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, cell_index);
	return 0;
}

static int OverlapPolygon(const sg_rune_vec3_t *subject,
	uint32_t subject_count, const sg_rune_vec3_t *clip, uint32_t clip_count,
	const sg_configuration_plane_t *plane, portal_point2_t **polygon_out,
	uint32_t *count_out, float center_out[3], float *area_out)
{
	portal_point2_t *polygon = NULL;
	uint32_t count = 0U;
	uint32_t drop = DominantAxis(plane->normal);
	float clip_orientation;
	uint32_t edge;

	*polygon_out = NULL;
	*count_out = 0U;
	*area_out = 0.0f;
	memset(center_out, 0, 3U * sizeof(*center_out));
	for (edge = 0; edge < subject_count; edge++)
	{
		float projected[2];

		Project(subject[edge].value, drop, projected);
		if (!AppendPoint2(&polygon, &count, projected))
			goto failure;
	}
	{
		portal_point2_t *clip_projected = NULL;
		uint32_t clip_projected_count = 0U;

		for (edge = 0; edge < clip_count; edge++)
		{
			float projected[2];

			Project(clip[edge].value, drop, projected);
			if (!AppendPoint2(&clip_projected, &clip_projected_count,
					projected))
			{
				free(clip_projected);
				goto failure;
			}
		}
		clip_orientation = PolygonSignedArea2(clip_projected,
			clip_projected_count);
		free(clip_projected);
	}
	for (edge = 0; edge < clip_count && count >= 3U; edge++)
	{
		portal_point2_t *next = NULL;
		uint32_t next_count = 0U;
		float clip_a[2], clip_b[2];
		uint32_t index;

		Project(clip[edge].value, drop, clip_a);
		Project(clip[(edge + 1U) % clip_count].value, drop, clip_b);
		for (index = 0; index < count; index++)
		{
			const portal_point2_t *start = &polygon[index];
			const portal_point2_t *end = &polygon[(index + 1U) % count];
			float start_distance = Cross2(clip_a, clip_b, start->value);
			float end_distance = Cross2(clip_a, clip_b, end->value);
			int start_inside = clip_orientation >= 0.0f ?
				start_distance >= 0.0f : start_distance <= 0.0f;
			int end_inside = clip_orientation >= 0.0f ?
				end_distance >= 0.0f : end_distance <= 0.0f;

			if (start_inside && !AppendPoint2(&next, &next_count,
					start->value))
			{
				free(next);
				goto failure;
			}
			if (start_inside != end_inside)
			{
				float denominator = start_distance - end_distance;
				float point[2];

				if (denominator == 0.0f)
				{
					free(next);
					goto failure;
				}
				point[0] = start->value[0] + start_distance / denominator *
					(end->value[0] - start->value[0]);
				point[1] = start->value[1] + start_distance / denominator *
					(end->value[1] - start->value[1]);
				if (!AppendPoint2(&next, &next_count, point))
				{
					free(next);
					goto failure;
				}
			}
		}
		free(polygon);
		polygon = next;
		count = next_count;
	}
	if (count >= 3U && fabsf(plane->normal[drop]) > PORTAL_AREA_EPSILON)
	{
		float projected_area = fabsf(PolygonSignedArea2(polygon, count)) * 0.5f;
		float normal_length = sqrtf(SG_BspProofDot(plane->normal,
			plane->normal));
		float perimeter = 0.0f;
		float coordinate_scale = 1.0f;
		uint32_t index;
		uint32_t u = (drop + 1U) % 3U;
		uint32_t v = (drop + 2U) % 3U;

		*area_out = projected_area * normal_length /
			fabsf(plane->normal[drop]);
		for (index = 0; index < count; index++)
		{
			float dx = polygon[(index + 1U) % count].value[0] -
				polygon[index].value[0];
			float dy = polygon[(index + 1U) % count].value[1] -
				polygon[index].value[1];

			center_out[u] += polygon[index].value[0];
			center_out[v] += polygon[index].value[1];
			perimeter += sqrtf(dx * dx + dy * dy);
			coordinate_scale = fmaxf(coordinate_scale,
				fabsf(polygon[index].value[0]));
			coordinate_scale = fmaxf(coordinate_scale,
				fabsf(polygon[index].value[1]));
		}
		if (projected_area <= perimeter * coordinate_scale * FLT_EPSILON *
				16.0f)
			*area_out = 0.0f;
		center_out[u] /= (float)count;
		center_out[v] /= (float)count;
		center_out[drop] = (plane->distance - plane->normal[u] * center_out[u] -
			plane->normal[v] * center_out[v]) / plane->normal[drop];
	}
	if (*area_out <= PORTAL_AREA_EPSILON)
	{
		free(polygon);
		return 1;
	}
	*polygon_out = polygon;
	*count_out = count;
	return 1;

failure:
	free(polygon);
	return 0;
}

static int PortalSideWitness(sg_bsp_proof_context_t *proof,
	uint32_t cell_index, const sg_configuration_face_t *boundary,
	const portal_point2_t *polygon, uint32_t polygon_count,
	const float center[3], float witness[3])
{
	const sg_configuration_cell_t *cell = &proof->space->cells[cell_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t drop = DominantAxis(boundary->plane.normal);
	uint32_t constraint_count = cell->face_count;
	uint32_t offset, edge, axis;
	int positive_margin = 0;
	int solved;
	sg_host_collision_pose_t pose;

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
			&proof->space->faces[cell->first_face + offset].plane;

		memcpy(halfspaces[offset].normal, plane->normal,
			sizeof(halfspaces[offset].normal));
		halfspaces[offset].distance = plane->distance;
		halfspaces[offset].open =
			plane->source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			plane->reversed != 0U;
		clearance[offset] = (uint8_t)!Coplanar(plane, &boundary->plane);
	}
	for (edge = 0; edge < polygon_count; edge++)
	{
		const float *a = polygon[edge].value;
		const float *b = polygon[(edge + 1U) % polygon_count].value;
		float a3[3] = { 0.0f, 0.0f, 0.0f };
		float b3[3] = { 0.0f, 0.0f, 0.0f };
		float direction[3];
		sg_configuration_lattice_halfspace_t *side =
			&halfspaces[constraint_count];
		uint32_t u = (drop + 1U) % 3U;
		uint32_t v = (drop + 2U) % 3U;

		a3[u] = a[0]; a3[v] = a[1];
		b3[u] = b[0]; b3[v] = b[1];
		a3[drop] = (boundary->plane.distance -
			boundary->plane.normal[u] * a3[u] -
			boundary->plane.normal[v] * a3[v]) /
			boundary->plane.normal[drop];
		b3[drop] = (boundary->plane.distance -
			boundary->plane.normal[u] * b3[u] -
			boundary->plane.normal[v] * b3[v]) /
			boundary->plane.normal[drop];
		for (axis = 0; axis < 3U; axis++)
			direction[axis] = b3[axis] - a3[axis];
		side->normal[0] = direction[1] * boundary->plane.normal[2] -
			direction[2] * boundary->plane.normal[1];
		side->normal[1] = direction[2] * boundary->plane.normal[0] -
			direction[0] * boundary->plane.normal[2];
		side->normal[2] = direction[0] * boundary->plane.normal[1] -
			direction[1] * boundary->plane.normal[0];
		if (side->normal[0] == 0.0f && side->normal[1] == 0.0f &&
			side->normal[2] == 0.0f)
			continue;
		side->distance = SG_BspProofDot(side->normal, a3);
		if (SG_BspProofDot(side->normal, center) > side->distance)
		{
			for (axis = 0; axis < 3U; axis++)
				side->normal[axis] = -side->normal[axis];
			side->distance = -side->distance;
		}
		clearance[constraint_count++] = 1U;
	}
	solved = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, boundary->plane.normal, point, &positive_margin,
		&stats);
	free(halfspaces);
	free(clearance);
	proof->result.lattice_solve_calls += stats.solve_calls;
	proof->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		proof->result.lattice_maximum_binary_shift)
		proof->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (solved <= 0 || !positive_margin)
		return solved;
	for (axis = 0; axis < 3U; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	return SG_HostCollisionClassifyPose(proof->authority, NULL, witness,
		cell->stance, &pose) && pose.valid ? 1 : -1;
}

static int PortalMatches(sg_bsp_proof_context_t *proof, uint32_t portal_index,
	uint32_t cell_a, uint32_t cell_b, const sg_configuration_face_t *face,
	const portal_point2_t *expected_polygon, uint32_t expected_count,
	float expected_area)
{
	const sg_configuration_portal_t *portal =
		&proof->space->portals[portal_index];
	portal_point2_t *overlap = NULL;
	uint32_t overlap_count = 0U;
	float overlap_center[3], overlap_area;
	float portal_center[3], portal_area;
	portal_point2_t *portal_polygon = NULL;
	uint32_t portal_count = 0U;
	sg_rune_vec3_t *expected3 = NULL;
	sg_configuration_plane_t expected_plane = face->plane;
	uint32_t index;
	uint32_t drop = DominantAxis(face->plane.normal);
	uint32_t u = (drop + 1U) % 3U;
	uint32_t v = (drop + 2U) % 3U;
	int endpoints = (portal->from_cell == cell_a && portal->to_cell == cell_b) ||
		(portal->from_cell == cell_b && portal->to_cell == cell_a);
	int match = 0;

	if (!endpoints || portal->stance != proof->space->cells[cell_a].stance ||
		portal->first_vertex > proof->space->vertex_count ||
		portal->vertex_count < 3U ||
		portal->vertex_count > proof->space->vertex_count - portal->first_vertex ||
		!Coplanar(&portal->plane, &face->plane))
		return 0;
	expected3 = calloc(expected_count, sizeof(*expected3));
	if (!expected3)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY,
			portal_index);
		return -1;
	}
	for (index = 0; index < expected_count; index++)
	{
		expected3[index].value[u] = expected_polygon[index].value[0];
		expected3[index].value[v] = expected_polygon[index].value[1];
		expected3[index].value[drop] = (expected_plane.distance -
			expected_plane.normal[u] * expected3[index].value[u] -
			expected_plane.normal[v] * expected3[index].value[v]) /
			expected_plane.normal[drop];
	}
	if (!OverlapPolygon(&proof->space->vertices[portal->first_vertex],
			portal->vertex_count, &proof->space->vertices[portal->first_vertex],
			portal->vertex_count, &portal->plane, &portal_polygon, &portal_count,
			portal_center, &portal_area) ||
		!OverlapPolygon(expected3, expected_count,
			&proof->space->vertices[portal->first_vertex], portal->vertex_count,
			&face->plane, &overlap, &overlap_count,
			overlap_center, &overlap_area))
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_CELL,
			portal_index);
		match = -1;
		goto done;
	}
	if (fabsf(portal_area - expected_area) <= MatchTolerance(expected_area) &&
		fabsf(overlap_area - expected_area) <= MatchTolerance(expected_area) &&
		fabsf(portal->clearance - sqrtf(expected_area)) <=
			MatchTolerance(sqrtf(expected_area)))
		match = 1;

done:
	free(expected3);
	free(portal_polygon);
	free(overlap);
	return match;
}

static int FindPortal(sg_bsp_proof_context_t *proof, uint8_t *seen,
	uint32_t cell_a, uint32_t cell_b, const sg_configuration_face_t *face,
	const portal_point2_t *polygon, uint32_t polygon_count, float area)
{
	uint32_t portal;

	for (portal = 0; portal < proof->space->portal_count; portal++)
		if (!seen[portal])
		{
			int matches = PortalMatches(proof, portal, cell_a, cell_b, face,
				polygon, polygon_count, area);

			if (matches < 0)
				return -1;
			if (matches)
			{
				seen[portal] = 1U;
				return 1;
			}
		}
	return 0;
}

int SG_BspProofAuditPortals(sg_bsp_proof_context_t *proof)
{
	uint8_t *seen;
	uint32_t cell_a, cell_b;

	seen = calloc(proof->space->portal_count ? proof->space->portal_count : 1U,
		sizeof(*seen));
	if (!seen)
		return 0;
	for (cell_a = 0; cell_a < proof->space->portal_count; cell_a++)
		for (cell_b = cell_a + 1U; cell_b < proof->space->portal_count; cell_b++)
		{
			const sg_configuration_portal_t *left =
				&proof->space->portals[cell_a];
			const sg_configuration_portal_t *right =
				&proof->space->portals[cell_b];
			int same_endpoints =
				(left->from_cell == right->from_cell &&
				 left->to_cell == right->to_cell) ||
				(left->from_cell == right->to_cell &&
				 left->to_cell == right->from_cell);

			if (same_endpoints && left->stance == right->stance)
			{
				proof->result.invented_portals++;
				SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVENTED_PORTAL,
					cell_b);
				goto failure;
			}
		}
	for (cell_a = 0; cell_a < proof->space->cell_count; cell_a++)
		for (cell_b = cell_a + 1U; cell_b < proof->space->cell_count; cell_b++)
		{
			const sg_configuration_cell_t *a = &proof->space->cells[cell_a];
			const sg_configuration_cell_t *b = &proof->space->cells[cell_b];
			uint32_t face_a, face_b;

			if (a->stance != b->stance)
				continue;
			for (face_a = 0; face_a < a->face_count; face_a++)
				for (face_b = 0; face_b < b->face_count; face_b++)
				{
					const sg_configuration_face_t *left =
						&proof->space->faces[a->first_face + face_a];
					const sg_configuration_face_t *right =
						&proof->space->faces[b->first_face + face_b];
					sg_rune_vec3_t *left_vertices = NULL;
					sg_rune_vec3_t *right_vertices = NULL;
					uint32_t left_count = 0U, right_count = 0U;
					portal_point2_t *polygon = NULL;
					uint32_t polygon_count = 0U;
					float center[3], area;
					float from[3], to[3];
					sg_host_collision_transition_t transition;
					int from_result, to_result, found;

					if (!OpposingPlanes(&left->plane, &right->plane))
						continue;
					if (!CellFacePolygon(proof, cell_a, left, &left_vertices,
							&left_count) ||
						!CellFacePolygon(proof, cell_b, right,
							&right_vertices, &right_count))
					{
						free(left_vertices);
						free(right_vertices);
						goto failure;
					}
					if (!left_count || !right_count)
					{
						free(left_vertices);
						free(right_vertices);
						continue;
					}
					if (!OverlapPolygon(left_vertices, left_count,
							right_vertices, right_count, &left->plane,
							&polygon,
							&polygon_count, center, &area))
					{
						free(left_vertices);
						free(right_vertices);
						goto failure;
					}
					free(left_vertices);
					free(right_vertices);
					if (!polygon_count)
						continue;
					from_result = PortalSideWitness(proof, cell_a, left,
						polygon, polygon_count, center, from);
					to_result = PortalSideWitness(proof, cell_b, right,
						polygon, polygon_count, center, to);
					if (from_result < 0 || to_result < 0)
					{
						free(polygon);
						goto failure;
					}
					if (!from_result || !to_result ||
						!SG_HostCollisionTransition(proof->authority, NULL,
							from, to, a->stance, &transition) ||
						!transition.clear)
					{
						free(polygon);
						continue;
					}
					proof->result.expected_portals++;
					found = FindPortal(proof, seen, cell_a, cell_b, left,
						polygon, polygon_count, area);
					free(polygon);
					if (found < 0)
						goto failure;
					if (!found)
					{
						proof->result.omitted_portals++;
						SG_BspProofFail(proof,
							SG_BSP_COMPLETENESS_OMITTED_PORTAL,
							proof->result.expected_portals - 1U);
						goto failure;
					}
					proof->result.proved_portals++;
				}
		}
	for (cell_a = 0; cell_a < proof->space->portal_count; cell_a++)
		if (!seen[cell_a])
		{
			proof->result.invented_portals++;
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVENTED_PORTAL,
				cell_a);
			goto failure;
		}
	free(seen);
	return 1;

failure:
	free(seen);
	return 0;
}
