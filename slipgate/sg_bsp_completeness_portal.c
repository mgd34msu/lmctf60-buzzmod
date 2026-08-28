#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PORTAL_AREA_EPSILON 0.000001f
#define PORTAL_MATCH_EPSILON 0.001f
#define PORTAL_MATCH_ROUNDING_ULPS 32.0f
#define PORTAL_COPLANAR_ROUNDING_ULPS 2.0f

typedef struct portal_point2_s
{
	double value[2];
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

static void Project(const float point[3], uint32_t drop, double projected[2])
{
	projected[0] = point[(drop + 1U) % 3U];
	projected[1] = point[(drop + 2U) % 3U];
}

static double Cross2(const double a[2], const double b[2],
	const double c[2])
{
	return (b[0] - a[0]) * (c[1] - a[1]) -
		(b[1] - a[1]) * (c[0] - a[0]);
}

static int PortalRecordValid(const sg_bsp_proof_context_t *proof,
	uint32_t portal_index)
{
	const sg_configuration_portal_t *portal =
		&proof->space->portals[portal_index];
	double normal[3], distance;
	uint32_t vertex;

	if (portal->from_cell >= proof->space->cell_count ||
		portal->to_cell >= proof->space->cell_count ||
		portal->from_cell == portal->to_cell ||
		portal->stance >= SG_RUNE_STANCE_COUNT ||
		!SG_BspProofFiniteVector(portal->plane.normal) ||
		!isfinite(portal->plane.distance) ||
		(portal->plane.normal[0] == 0.0f &&
		 portal->plane.normal[1] == 0.0f &&
		 portal->plane.normal[2] == 0.0f) ||
		portal->first_vertex > proof->space->vertex_count ||
		portal->vertex_count < 3U ||
		portal->vertex_count > proof->space->vertex_count -
			portal->first_vertex ||
		!SG_BspProofOrientedPlane(&portal->plane, normal, &distance))
		return 0;
	for (vertex = 0; vertex < portal->vertex_count; vertex++)
	{
		const float *point = proof->space->vertices[
			portal->first_vertex + vertex].value;
		double coordinate_scale;
		double scale;
		double residual;

		if (!SG_BspProofFiniteVector(point))
			return 0;
		coordinate_scale = fmax(fabs((double)point[0]),
			fmax(fabs((double)point[1]), fabs((double)point[2])));
		scale = fabs(distance) + coordinate_scale + 1.0 +
			fabs(normal[0] * (double)point[0]) +
			fabs(normal[1] * (double)point[1]) +
			fabs(normal[2] * (double)point[2]);
		residual = fabs(normal[0] * (double)point[0] +
			normal[1] * (double)point[1] + normal[2] * (double)point[2] -
			distance);
		if (residual > scale * (double)FLT_EPSILON *
				(double)PORTAL_COPLANAR_ROUNDING_ULPS)
			return 0;
	}
	return 1;
}

static int AppendPoint2(portal_point2_t **points, uint32_t *count,
	const double point[2])
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

static double PolygonSignedArea2(const portal_point2_t *polygon,
	uint32_t count)
{
	double area = 0.0;
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

static int OverlapPolygon(const sg_rune_vec3_t *subject,
	uint32_t subject_count, const sg_rune_vec3_t *clip, uint32_t clip_count,
	const sg_configuration_plane_t *plane, portal_point2_t **polygon_out,
	uint32_t *count_out, float center_out[3], float *area_out)
{
	portal_point2_t *polygon = NULL;
	uint32_t count = 0U;
	uint32_t drop = DominantAxis(plane->normal);
	double normal[3], distance;
	double clip_orientation;
	uint32_t edge;

	*polygon_out = NULL;
	*count_out = 0U;
	*area_out = 0.0f;
	memset(center_out, 0, 3U * sizeof(*center_out));
	if (!SG_BspProofOrientedPlane(plane, normal, &distance))
		return 0;
	for (edge = 0; edge < subject_count; edge++)
	{
		double projected[2];

		Project(subject[edge].value, drop, projected);
		if (!AppendPoint2(&polygon, &count, projected))
			goto failure;
	}
	{
		portal_point2_t *clip_projected = NULL;
		uint32_t clip_projected_count = 0U;

		for (edge = 0; edge < clip_count; edge++)
		{
			double projected[2];

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
		double clip_a[2], clip_b[2];
		uint32_t index;

		Project(clip[edge].value, drop, clip_a);
		Project(clip[(edge + 1U) % clip_count].value, drop, clip_b);
		for (index = 0; index < count; index++)
		{
			const portal_point2_t *start = &polygon[index];
			const portal_point2_t *end = &polygon[(index + 1U) % count];
			double start_distance = Cross2(clip_a, clip_b, start->value);
			double end_distance = Cross2(clip_a, clip_b, end->value);
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
				double denominator = start_distance - end_distance;
				double point[2];

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
	if (count >= 3U && fabs(normal[drop]) > (double)PORTAL_AREA_EPSILON)
	{
		double projected_area = fabs(PolygonSignedArea2(polygon, count)) * 0.5;
		double normal_length = sqrt(normal[0] * normal[0] +
			normal[1] * normal[1] + normal[2] * normal[2]);
		double area = projected_area * normal_length / fabs(normal[drop]);
		double perimeter = 0.0;
		double coordinate_scale = 1.0;
		double center[3] = { 0.0, 0.0, 0.0 };
		uint32_t index;
		uint32_t u = (drop + 1U) % 3U;
		uint32_t v = (drop + 2U) % 3U;

		for (index = 0; index < count; index++)
		{
			double dx = polygon[(index + 1U) % count].value[0] -
				polygon[index].value[0];
			double dy = polygon[(index + 1U) % count].value[1] -
				polygon[index].value[1];

			center[u] += polygon[index].value[0];
			center[v] += polygon[index].value[1];
			perimeter += sqrt(dx * dx + dy * dy);
			coordinate_scale = fmax(coordinate_scale,
				fabs(polygon[index].value[0]));
			coordinate_scale = fmax(coordinate_scale,
				fabs(polygon[index].value[1]));
		}
		if (projected_area <= perimeter * coordinate_scale *
				(double)FLT_EPSILON * 16.0)
			area = 0.0;
		center[u] /= (double)count;
		center[v] /= (double)count;
		center[drop] = (distance - normal[u] * center[u] -
			normal[v] * center[v]) / normal[drop];
		if (!isfinite(area) || area > (double)FLT_MAX ||
			!isfinite(center[0]) || !isfinite(center[1]) ||
			!isfinite(center[2]))
			goto failure;
		*area_out = (float)area;
		center_out[0] = (float)center[0];
		center_out[1] = (float)center[1];
		center_out[2] = (float)center[2];
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
	double boundary_normal[3], boundary_distance;
	float objective[3];
	int positive_margin = 0;
	int solved;
	sg_host_collision_pose_t pose;

	if (!SG_BspProofOrientedPlane(&boundary->plane, boundary_normal,
			&boundary_distance))
		return -1;
	for (axis = 0; axis < 3U; axis++)
		objective[axis] = (float)boundary_normal[axis];
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
		double normal[3], distance;

		if (!SG_BspProofOrientedPlane(plane, normal, &distance))
		{
			free(halfspaces);
			free(clearance);
			return -1;
		}
		for (axis = 0; axis < 3U; axis++)
			halfspaces[offset].normal[axis] = (float)normal[axis];
		halfspaces[offset].distance = (float)distance;
		halfspaces[offset].open =
			(plane->source_kind == SG_CONFIGURATION_PLANE_BSP &&
			 plane->reversed == 0U) ||
			(plane->source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			 plane->reversed != 0U);
		clearance[offset] =
			(uint8_t)!SG_BspProofPlanesCoplanar(plane, &boundary->plane);
	}
	for (edge = 0; edge < polygon_count; edge++)
	{
		const double *a = polygon[edge].value;
		const double *b = polygon[(edge + 1U) % polygon_count].value;
		double a3[3] = { 0.0, 0.0, 0.0 };
		double b3[3] = { 0.0, 0.0, 0.0 };
		double direction[3], side_normal[3], side_distance, side_scale;
		sg_configuration_lattice_halfspace_t *side =
			&halfspaces[constraint_count];
		uint32_t u = (drop + 1U) % 3U;
		uint32_t v = (drop + 2U) % 3U;

		a3[u] = a[0]; a3[v] = a[1];
		b3[u] = b[0]; b3[v] = b[1];
		a3[drop] = (boundary_distance - boundary_normal[u] * a3[u] -
			boundary_normal[v] * a3[v]) / boundary_normal[drop];
		b3[drop] = (boundary_distance - boundary_normal[u] * b3[u] -
			boundary_normal[v] * b3[v]) / boundary_normal[drop];
		for (axis = 0; axis < 3U; axis++)
			direction[axis] = b3[axis] - a3[axis];
		side_normal[0] = direction[1] * boundary_normal[2] -
			direction[2] * boundary_normal[1];
		side_normal[1] = direction[2] * boundary_normal[0] -
			direction[0] * boundary_normal[2];
		side_normal[2] = direction[0] * boundary_normal[1] -
			direction[1] * boundary_normal[0];
		side_scale = fmax(fabs(side_normal[0]),
			fmax(fabs(side_normal[1]), fabs(side_normal[2])));
		if (side_scale == 0.0)
			continue;
		for (axis = 0; axis < 3U; axis++)
			side_normal[axis] /= side_scale;
		side_distance = side_normal[0] * a3[0] + side_normal[1] * a3[1] +
			side_normal[2] * a3[2];
		if (side_normal[0] * (double)center[0] +
			side_normal[1] * (double)center[1] +
			side_normal[2] * (double)center[2] > side_distance)
		{
			for (axis = 0; axis < 3U; axis++)
				side_normal[axis] = -side_normal[axis];
			side_distance = -side_distance;
		}
		for (axis = 0; axis < 3U; axis++)
			side->normal[axis] = (float)side_normal[axis];
		side->distance = (float)side_distance;
		clearance[constraint_count++] = 1U;
	}
	solved = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, objective, point, &positive_margin,
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
	double expected_normal[3], expected_distance;
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
			!SG_BspProofPlanesCoplanar(&portal->plane, &face->plane))
		return 0;
	if (!SG_BspProofOrientedPlane(&face->plane, expected_normal,
			&expected_distance))
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
		expected3[index].value[u] = (float)expected_polygon[index].value[0];
		expected3[index].value[v] = (float)expected_polygon[index].value[1];
		expected3[index].value[drop] = (float)((expected_distance -
			expected_normal[u] * expected_polygon[index].value[0] -
			expected_normal[v] * expected_polygon[index].value[1]) /
			expected_normal[drop]);
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
	const sg_bsp_proof_portal_ref_t *refs, uint32_t cell_a, uint32_t cell_b,
	const sg_configuration_face_t *face, const portal_point2_t *polygon,
	uint32_t polygon_count, float area)
{
	uint32_t low_cell = cell_a < cell_b ? cell_a : cell_b;
	uint32_t high_cell = cell_a < cell_b ? cell_b : cell_a;
	uint32_t stance = (uint32_t)proof->space->cells[cell_a].stance;
	uint32_t cursor = SG_BspProofPortalLowerBound(refs,
		proof->space->portal_count, low_cell, high_cell, stance);

	proof->result.portal_endpoint_lookups++;
	for (; cursor < proof->space->portal_count; cursor++)
	{
		uint32_t portal = refs[cursor].portal;

		if (refs[cursor].low_cell != low_cell ||
			refs[cursor].high_cell != high_cell ||
			refs[cursor].stance != stance)
			break;
		proof->result.portal_lookup_candidates++;
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
	}
	return 0;
}

static int AuditFacePair(sg_bsp_proof_context_t *proof, uint8_t *seen,
	const sg_bsp_proof_portal_ref_t *portals,
	const sg_bsp_proof_face_ref_t *left_ref,
	const sg_bsp_proof_face_ref_t *right_ref)
{
	uint32_t left_cell = left_ref->cell;
	uint32_t right_cell = right_ref->cell;
	const sg_configuration_cell_t *a = &proof->space->cells[left_cell];
	const sg_configuration_face_t *left =
		&proof->space->faces[left_ref->face];
	const sg_configuration_face_t *right =
		&proof->space->faces[right_ref->face];
	portal_point2_t *polygon = NULL;
	uint32_t polygon_count = 0U;
	float center[3], area;
	float from[3], to[3];
	sg_host_collision_transition_t transition;
	int from_result, to_result, found;
	uint32_t axis;

	proof->result.portal_face_pair_visits++;
	if (left_cell == right_cell ||
		left_ref->orientation == right_ref->orientation)
		return 1;
	for (axis = 0; axis < 3U; axis++)
		if (left_ref->bounds_maxs[axis] < right_ref->bounds_mins[axis] ||
			right_ref->bounds_maxs[axis] < left_ref->bounds_mins[axis])
			return 1;
	proof->result.portal_face_candidates++;
	if (!SG_BspProofPlanesOppose(&left->plane, &right->plane))
		return 1;
	if (!OverlapPolygon(left_ref->vertices, left_ref->vertex_count,
			right_ref->vertices, right_ref->vertex_count, &left->plane,
			&polygon, &polygon_count, center, &area))
		return 0;
	if (!polygon_count)
		return 1;
	from_result = PortalSideWitness(proof, left_cell, left, polygon,
		polygon_count, center, from);
	to_result = PortalSideWitness(proof, right_cell, right, polygon,
		polygon_count, center, to);
	if (from_result < 0 || to_result < 0)
	{
		free(polygon);
		return 0;
	}
	if (!from_result || !to_result ||
		!SG_HostCollisionTransition(proof->authority, NULL, from, to,
			a->stance, &transition) || !transition.clear)
	{
		free(polygon);
		return 1;
	}
	proof->result.expected_portals++;
	found = FindPortal(proof, seen, portals, left_cell, right_cell, left,
		polygon, polygon_count, area);
	free(polygon);
	if (found < 0)
		return 0;
	if (!found)
	{
		proof->result.omitted_portals++;
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OMITTED_PORTAL,
			proof->result.expected_portals - 1U);
		return 0;
	}
	proof->result.proved_portals++;
	return 1;
}

static int CompareFaceGroup(const sg_bsp_proof_face_ref_t *face,
	uint32_t stance, uint32_t dominant, int64_t normal_bucket_0,
	int64_t normal_bucket_1, int64_t normal_bucket_2,
	int64_t plane_bucket, uint8_t orientation)
{
	if (face->stance != stance)
		return face->stance < stance ? -1 : 1;
	if (face->dominant != dominant)
		return face->dominant < dominant ? -1 : 1;
	if (face->normal_buckets[0] != normal_bucket_0)
		return face->normal_buckets[0] < normal_bucket_0 ? -1 : 1;
	if (face->normal_buckets[1] != normal_bucket_1)
		return face->normal_buckets[1] < normal_bucket_1 ? -1 : 1;
	if (face->normal_buckets[2] != normal_bucket_2)
		return face->normal_buckets[2] < normal_bucket_2 ? -1 : 1;
	if (face->plane_bucket != plane_bucket)
		return face->plane_bucket < plane_bucket ? -1 : 1;
	if (face->orientation != orientation)
		return face->orientation < orientation ? -1 : 1;
	return 0;
}

static uint32_t FaceGroupBound(const sg_bsp_proof_face_ref_t *faces,
	uint32_t count, uint32_t stance, uint32_t dominant,
	int64_t normal_bucket_0, int64_t normal_bucket_1,
	int64_t normal_bucket_2, int64_t plane_bucket, uint8_t orientation,
	int upper)
{
	uint32_t first = 0U;
	uint32_t length = count;

	while (length)
	{
		uint32_t half = length / 2U;
		uint32_t middle = first + half;
		int comparison = CompareFaceGroup(&faces[middle], stance, dominant,
			normal_bucket_0, normal_bucket_1, normal_bucket_2, plane_bucket,
			orientation);

		if (comparison < 0 || (upper && comparison == 0))
		{
			first = middle + 1U;
			length -= half + 1U;
		}
		else
			length = half;
	}
	return first;
}

static int OffsetBucket(int64_t source, int delta, int64_t *result)
{
	if ((delta < 0 && source == INT64_MIN) ||
		(delta > 0 && source == INT64_MAX))
		return 0;
	*result = source + (int64_t)delta;
	return 1;
}

static int QueryFaceGroup(sg_bsp_proof_context_t *proof, uint8_t *seen,
	const sg_bsp_proof_portal_ref_t *portals,
	const sg_bsp_proof_face_ref_t *faces, uint32_t left_index,
	uint32_t first, uint32_t count)
{
	const sg_bsp_proof_face_ref_t *left = &faces[left_index];
	uint32_t sweep_axis;
	uint32_t left_count;
	uint32_t middle;

	if (!count)
		return 1;
	sweep_axis = (faces[first].dominant + 1U) % 3U;
	left_count = count / 2U;
	middle = first + left_count;
	if (left_count && faces[first + left_count / 2U].subtree_sweep_max >=
			left->bounds_mins[sweep_axis] &&
		!QueryFaceGroup(proof, seen, portals, faces,
			left_index, first, left_count))
		return 0;
	if (middle > left_index && faces[middle].sweep_min <=
			left->bounds_maxs[sweep_axis] && faces[middle].sweep_max >=
			left->bounds_mins[sweep_axis] &&
		!AuditFacePair(proof, seen, portals, left, &faces[middle]))
		return 0;
	if (count - left_count - 1U &&
		faces[middle + 1U].sweep_min <= left->bounds_maxs[sweep_axis] &&
		!QueryFaceGroup(proof, seen, portals, faces, left_index, middle + 1U,
			count - left_count - 1U))
		return 0;
	return 1;
}

int SG_BspProofAuditPortals(sg_bsp_proof_context_t *proof)
{
	uint8_t *seen = NULL;
	sg_bsp_proof_face_ref_t *faces = NULL;
	sg_bsp_proof_portal_ref_t *portals = NULL;
	uint32_t face_count = 0U;
	uint32_t cell_a;

	seen = calloc(proof->space->portal_count ? proof->space->portal_count : 1U,
		sizeof(*seen));
	if (!seen)
		return 0;
	for (cell_a = 0; cell_a < proof->space->portal_count; cell_a++)
		if (!PortalRecordValid(proof, cell_a))
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_PORTAL,
				cell_a);
			goto failure;
		}
	if (!SG_BspProofBuildPortalRefs(proof, &portals) ||
		!SG_BspProofBuildFaceRefs(proof, &faces, &face_count))
		goto failure;
	for (cell_a = 1U; cell_a < proof->space->portal_count; cell_a++)
		if (portals[cell_a - 1U].low_cell == portals[cell_a].low_cell &&
			portals[cell_a - 1U].high_cell == portals[cell_a].high_cell &&
			portals[cell_a - 1U].stance == portals[cell_a].stance)
		{
			proof->result.invented_portals++;
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVENTED_PORTAL,
				portals[cell_a].portal);
			goto failure;
		}
	for (cell_a = 0; cell_a < face_count; cell_a++)
	{
		const sg_bsp_proof_face_ref_t *left = &faces[cell_a];
		uint8_t target_orientation = (uint8_t)!left->orientation;
		int delta_0, delta_1, delta_2, delta_distance;
		uint32_t target_dominant;

		for (target_dominant = 0; target_dominant < 3U; target_dominant++)
			for (delta_0 = -1; delta_0 <= 1; delta_0++)
				for (delta_1 = -1; delta_1 <= 1; delta_1++)
					for (delta_2 = -1; delta_2 <= 1; delta_2++)
						for (delta_distance = -1; delta_distance <= 1;
							delta_distance++)
				{
					int64_t normal_bucket_0, normal_bucket_1;
					int64_t normal_bucket_2, plane_bucket;
					uint32_t first, end;

					if (!OffsetBucket(left->normal_buckets[0], delta_0,
							&normal_bucket_0) ||
						!OffsetBucket(left->normal_buckets[1], delta_1,
							&normal_bucket_1) ||
						!OffsetBucket(left->normal_buckets[2], delta_2,
							&normal_bucket_2) ||
						!OffsetBucket(left->plane_bucket, delta_distance,
							&plane_bucket))
						continue;
					first = FaceGroupBound(faces, face_count,
						left->stance, target_dominant,
						normal_bucket_0, normal_bucket_1, normal_bucket_2,
						plane_bucket, target_orientation, 0);
					end = FaceGroupBound(faces, face_count,
						left->stance, target_dominant,
						normal_bucket_0, normal_bucket_1, normal_bucket_2,
						plane_bucket, target_orientation, 1);
					if (!QueryFaceGroup(proof, seen, portals, faces, cell_a,
							first, end - first))
						goto failure;
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
	SG_BspProofFreeFaceRefs(faces, face_count);
	free(portals);
	free(seen);
	return 1;

failure:
	SG_BspProofFreeFaceRefs(faces, face_count);
	free(portals);
	free(seen);
	return 0;
}
