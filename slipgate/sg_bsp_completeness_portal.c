#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PORTAL_AREA_EPSILON 0.000001f
#define PORTAL_COORDINATE_ROUNDING_ULPS 2.0
#define PORTAL_COPLANAR_ROUNDING_ULPS 2.0f
#define PORTAL_CANONICAL_POINT_EPSILON 0.00001
#define PORTAL_PI 3.14159265358979323846

typedef struct portal_point2_s
{
	double value[2];
} portal_point2_t;

static int ZeroPolygonUsesAuthoritativeFallback(
	const sg_configuration_face_t *left,
	const sg_configuration_face_t *right)
{
	return left->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY ||
		right->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
}

int SG_BspProofTestZeroPolygonPortalKinds(void)
{
	sg_configuration_face_t left, right;

	memset(&left, 0, sizeof(left));
	memset(&right, 0, sizeof(right));
	left.kind = SG_CONFIGURATION_FACE_FACET;
	right.kind = SG_CONFIGURATION_FACE_FACET;
	if (ZeroPolygonUsesAuthoritativeFallback(&left, &right))
		return 0;
	left.kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
	if (!ZeroPolygonUsesAuthoritativeFallback(&left, &right))
		return 0;
	left.kind = SG_CONFIGURATION_FACE_FACET;
	right.kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
	return ZeroPolygonUsesAuthoritativeFallback(&left, &right);
}

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

static double FloatUlp(float value);

static int PointLexicographicallyLess(const float left[3],
	const float right[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (left[axis] < right[axis])
			return 1;
		if (left[axis] > right[axis])
			return 0;
	}
	return 0;
}

static int PortalPolygonCanonical(const sg_configuration_space_t *space,
	const sg_configuration_portal_t *portal, double *area2_out)
{
	const sg_rune_vec3_t *vertices =
		&space->vertices[portal->first_vertex];
	uint32_t drop = DominantAxis(portal->plane.normal);
	uint32_t vertex, edge;
	double area2 = 0.0;
	double orientation;
	double origin[2];

	Project(vertices[0].value, drop, origin);
	for (vertex = 1U; vertex < portal->vertex_count; vertex++)
		if (PointLexicographicallyLess(vertices[vertex].value,
				vertices[0].value))
			return 0;
	for (vertex = 1U; vertex + 1U < portal->vertex_count; vertex++)
	{
		double current[2], next[2];

		Project(vertices[vertex].value, drop, current);
		Project(vertices[vertex + 1U].value, drop, next);
		area2 += Cross2(origin, current, next);
	}
	if (area2 == 0.0 ||
		(area2 > 0.0) != (portal->plane.normal[drop] > 0.0f))
		return 0;
	orientation = area2 > 0.0 ? 1.0 : -1.0;
	for (edge = 0U; edge < portal->vertex_count; edge++)
	{
		double start[2], end[2];

		Project(vertices[edge].value, drop, start);
		Project(vertices[(edge + 1U) % portal->vertex_count].value, drop, end);
		for (vertex = 0U; vertex < portal->vertex_count; vertex++)
		{
			double point[2];

			if (vertex == edge ||
				vertex == (edge + 1U) % portal->vertex_count)
				continue;
			Project(vertices[vertex].value, drop, point);
			if (Cross2(start, end, point) * orientation <= 0.0)
				return 0;
		}
	}
	*area2_out = area2;
	return 1;
}

static int PortalRecordValid(const sg_bsp_proof_context_t *proof,
	uint32_t portal_index)
{
	const sg_configuration_portal_t *portal =
		&proof->space->portals[portal_index];
	double normal[3], distance, area2, surface_scale;
	float area, expected_clearance;
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
		portal->vertex_count > SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL ||
		portal->vertex_count > proof->space->vertex_count -
			portal->first_vertex ||
		!isfinite(portal->clearance) || !(portal->clearance > 0.0f) ||
		!SG_BspProofOrientedPlane(&portal->plane, normal, &distance))
		return 0;
	for (vertex = 0; vertex < portal->vertex_count; vertex++)
	{
		const float *point = proof->space->vertices[
			portal->first_vertex + vertex].value;
		double weighted_scale;
		double residual;

		if (!SG_BspProofFiniteVector(point))
			return 0;
		weighted_scale = fabs(distance) +
			fabs(normal[0] * (double)point[0]) +
			fabs(normal[1] * (double)point[1]) +
			fabs(normal[2] * (double)point[2]);
		residual = fabs(normal[0] * (double)point[0] +
			normal[1] * (double)point[1] + normal[2] * (double)point[2] -
			distance);
		if (residual > weighted_scale * (double)FLT_EPSILON *
				(double)PORTAL_COPLANAR_ROUNDING_ULPS)
			return 0;
	}
	if (!PortalPolygonCanonical(proof->space, portal, &area2))
		return 0;
	surface_scale = sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
		normal[2] * normal[2]) /
		fabs(normal[DominantAxis(portal->plane.normal)]);
	area = (float)(fabs(area2) * 0.5 * surface_scale);
	if (!isfinite(area) || !(area > PORTAL_AREA_EPSILON))
		return 0;
	expected_clearance = sqrtf(area);
	return fabs((double)portal->clearance - (double)expected_clearance) <=
		2.0 * FloatUlp(expected_clearance);
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

	if (count < 3U)
		return 0.0;
	for (index = 1U; index + 1U < count; index++)
	{
		double ax = polygon[index].value[0] - polygon[0].value[0];
		double ay = polygon[index].value[1] - polygon[0].value[1];
		double bx = polygon[index + 1U].value[0] - polygon[0].value[0];
		double by = polygon[index + 1U].value[1] - polygon[0].value[1];

		area += ax * by - ay * bx;
	}
	return area;
}

static double FloatUlp(float value)
{
	float above = nextafterf(value, INFINITY);
	float below = nextafterf(value, -INFINITY);
	double upward = isfinite(above) ?
		fabs((double)above - (double)value) : 0.0;
	double downward = isfinite(below) ?
		fabs((double)value - (double)below) : 0.0;

	return fmax(upward, downward);
}

static double ProjectedPerimeter(const sg_rune_vec3_t *polygon,
	uint32_t count, uint32_t drop)
{
	double perimeter = 0.0;
	uint32_t index;
	uint32_t u = (drop + 1U) % 3U;
	uint32_t v = (drop + 2U) % 3U;

	for (index = 0U; index < count; index++)
	{
		const float *a = polygon[index].value;
		const float *b = polygon[(index + 1U) % count].value;
		double du = (double)b[u] - (double)a[u];
		double dv = (double)b[v] - (double)a[v];

		perimeter += sqrt(du * du + dv * dv);
	}
	return perimeter;
}

static double PortalAreaMatchTolerance(const sg_rune_vec3_t *expected,
	uint32_t expected_count, const sg_configuration_plane_t *plane,
	float expected_area)
{
	double normal[3], distance;
	double coordinate_ulp = 0.0;
	double projected_radius;
	double projected_error;
	double surface_scale;
	double tolerance;
	uint32_t drop = DominantAxis(plane->normal);
	uint32_t u = (drop + 1U) % 3U;
	uint32_t v = (drop + 2U) % 3U;
	uint32_t index;

	if (!SG_BspProofOrientedPlane(plane, normal, &distance) ||
		normal[drop] == 0.0)
		return 0.0;
	for (index = 0U; index < expected_count; index++)
	{
		coordinate_ulp = fmax(coordinate_ulp,
			FloatUlp(expected[index].value[u]));
		coordinate_ulp = fmax(coordinate_ulp,
			FloatUlp(expected[index].value[v]));
	}
	/*
	 * Publication and recovery can separate corresponding coordinates by two
	 * ULPs. sqrt(2) converts that component bound to projected distance. The
	 * authoritative polygon's radius-r neighborhood adds at most P*r + pi*r^2
	 * area. Only authoritative geometry contributes to the acceptance bound.
	 */
	projected_radius = sqrt(2.0) * PORTAL_COORDINATE_ROUNDING_ULPS *
		coordinate_ulp;
	projected_error = ProjectedPerimeter(expected, expected_count, drop) *
		projected_radius + PORTAL_PI * projected_radius * projected_radius;
	surface_scale = sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
		normal[2] * normal[2]) / fabs(normal[drop]);
	tolerance = projected_error * surface_scale +
		2.0 * FloatUlp(expected_area);
	return tolerance;
}

static double PortalClearanceMatchTolerance(float expected_area,
	double area_tolerance, float expected_clearance)
{
	double expected_root = sqrt((double)expected_area);
	double minimum_area = fmax(0.0,
		(double)expected_area - area_tolerance);
	double denominator = expected_root + sqrt(minimum_area);
	double tolerance = denominator > 0.0 ? area_tolerance / denominator : 0.0;

	return tolerance + 2.0 * FloatUlp(expected_clearance);
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
		double center[3] = { 0.0, 0.0, 0.0 };
		uint32_t index;
		uint32_t u = (drop + 1U) % 3U;
		uint32_t v = (drop + 2U) % 3U;

		for (index = 0; index < count; index++)
		{
			center[u] += polygon[index].value[0];
			center[v] += polygon[index].value[1];
		}
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
	uint32_t cell_index, uint32_t other_cell_index,
	const sg_configuration_face_t *boundary, float witness[3])
{
	const sg_configuration_cell_t *cell = &proof->space->cells[cell_index];
	const sg_configuration_cell_t *other =
		&proof->space->cells[other_cell_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	const sg_configuration_plane_t **planes;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3], clearance_point[3];
	uint32_t constraint_count;
	uint32_t offset, axis;
	double boundary_normal[3], boundary_distance;
	float objective[3];
	int positive_margin = 0;
	int solved;
	int classified;
	sg_host_collision_pose_t pose;

	if (!SG_BspProofOrientedPlane(&boundary->plane, boundary_normal,
			&boundary_distance))
		return -1;
	for (axis = 0; axis < 3U; axis++)
		objective[axis] = (float)boundary_normal[axis];
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
			&proof->space->faces[cell->first_face + offset].plane;
		double normal[3], distance;
		uint32_t existing;

		if (!SG_BspProofOrientedPlane(plane, normal, &distance))
		{
			free(halfspaces);
			free(planes);
			free(clearance);
			return -1;
		}
		for (existing = 0U; existing < constraint_count; existing++)
			if (SG_BspProofPlanesCoplanar(planes[existing], plane) &&
				!SG_BspProofPlanesOppose(planes[existing], plane))
			{
				if ((plane->source_kind == SG_CONFIGURATION_PLANE_BSP &&
						plane->reversed == 0U) ||
					(plane->source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
						plane->reversed != 0U))
					halfspaces[existing].open = 1U;
				if (SG_BspProofPlanesCoplanar(plane, &boundary->plane))
					clearance[existing] = 0U;
				break;
			}
		if (existing < constraint_count)
			continue;
		for (axis = 0; axis < 3U; axis++)
			halfspaces[constraint_count].normal[axis] = (float)normal[axis];
		halfspaces[constraint_count].distance = (float)distance;
		halfspaces[constraint_count].open = 1;
		planes[constraint_count] = plane;
		clearance[constraint_count++] =
			(uint8_t)!SG_BspProofPlanesCoplanar(plane, &boundary->plane);
	}
	for (offset = 0; offset < other->face_count; offset++)
	{
		const sg_configuration_plane_t *plane = &proof->space->faces[
			other->first_face + offset].plane;
		sg_configuration_lattice_halfspace_t *side =
			&halfspaces[constraint_count];
		double normal[3], distance;
		uint32_t existing;

		if (SG_BspProofPlanesCoplanar(plane, &boundary->plane))
			continue;
		if (!SG_BspProofOrientedPlane(plane, normal, &distance))
		{
			free(halfspaces);
			free(planes);
			free(clearance);
			return -1;
		}
		for (existing = 0U; existing < constraint_count; existing++)
			if (SG_BspProofPlanesCoplanar(planes[existing], plane) &&
				!SG_BspProofPlanesOppose(planes[existing], plane))
			{
				if ((plane->source_kind == SG_CONFIGURATION_PLANE_BSP &&
						plane->reversed == 0U) ||
					(plane->source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
						plane->reversed != 0U))
					halfspaces[existing].open = 1U;
				break;
			}
		if (existing < constraint_count)
			continue;
		for (axis = 0; axis < 3U; axis++)
			side->normal[axis] = (float)normal[axis];
		side->distance = (float)distance;
		side->open = 1;
		planes[constraint_count] = plane;
		clearance[constraint_count++] = 1U;
	}
	solved = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, NULL, point, &positive_margin, &stats);
	if (solved > 0 && positive_margin)
	{
		memcpy(clearance_point, point, sizeof(clearance_point));
		solved = SG_ConfigurationLatticeFind(halfspaces, constraint_count,
			objective, point, &stats);
	}
	free(halfspaces);
	free(planes);
	free(clearance);
	proof->result.lattice_solve_calls += stats.solve_calls;
	proof->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		proof->result.lattice_maximum_binary_shift)
		proof->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (solved <= 0)
		return solved;
	if (!positive_margin)
		return 0;
	for (axis = 0; axis < 3U; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	classified = SG_HostCollisionClassifyPose(proof->authority, NULL, witness,
		cell->stance, &pose);
	if (!classified)
		return -1;
	if (pose.valid)
		return 1;
	for (axis = 0; axis < 3U; axis++)
		witness[axis] = (float)clearance_point[axis] * 0.125f;
	classified = SG_HostCollisionClassifyPose(proof->authority, NULL, witness,
		cell->stance, &pose);
	return classified && pose.valid ? 1 : -1;
}

static int UnitPlane(const sg_configuration_plane_t *plane,
	double normal[3], double *distance)
{
	double length;
	uint32_t axis;

	if (!SG_BspProofOrientedPlane(plane, normal, distance))
		return 0;
	length = sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
		normal[2] * normal[2]);
	if (!(length > 0.0) || !isfinite(length))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] /= length;
	*distance /= length;
	return 1;
}

static int PointInsidePortalCells(const sg_bsp_proof_context_t *proof,
	uint32_t first_cell, uint32_t second_cell, const float point[3])
{
	const uint32_t cells[2] = { first_cell, second_cell };
	uint32_t side, offset;

	for (side = 0U; side < 2U; side++)
	{
		const sg_configuration_cell_t *cell =
			&proof->space->cells[cells[side]];

		for (offset = 0U; offset < cell->face_count; offset++)
		{
			const sg_configuration_plane_t *plane = &proof->space->faces[
				cell->first_face + offset].plane;
			double normal[3], distance;

			if (!UnitPlane(plane, normal, &distance) ||
				(double)point[0] * normal[0] +
				(double)point[1] * normal[1] +
				(double)point[2] * normal[2] - distance >
					PORTAL_CANONICAL_POINT_EPSILON)
				return 0;
		}
	}
	return 1;
}

static int TangentExpectedPolygon(const sg_bsp_proof_context_t *proof,
	uint32_t first_cell, uint32_t second_cell,
	const sg_configuration_face_t *boundary, const float first_witness[3],
	const float second_witness[3], portal_point2_t polygon[4],
	float *area_out)
{
	const uint32_t cell_indices[2] = { first_cell, second_cell };
	double normal[3], distance;
	double first_side, second_side, denominator, fraction;
	float center[3], tangent[3], bitangent[3], vertices[4][3];
	float minimum_slack = INFINITY, radius, scale;
	uint32_t side, offset, axis, vertex, seed_axis = 0U;
	uint32_t drop = DominantAxis(boundary->plane.normal);

	if (!UnitPlane(&boundary->plane, normal, &distance))
		return 0;
	first_side = (double)first_witness[0] * normal[0] +
		(double)first_witness[1] * normal[1] +
		(double)first_witness[2] * normal[2] - distance;
	second_side = (double)second_witness[0] * normal[0] +
		(double)second_witness[1] * normal[1] +
		(double)second_witness[2] * normal[2] - distance;
	denominator = first_side - second_side;
	if (first_side * second_side > 0.0 || denominator == 0.0 ||
		!isfinite(denominator))
		return 0;
	fraction = first_side / denominator;
	for (axis = 0U; axis < 3U; axis++)
		center[axis] = (float)((double)first_witness[axis] + fraction *
			((double)second_witness[axis] - first_witness[axis]));
	{
		double residual = (double)center[0] * normal[0] +
			(double)center[1] * normal[1] +
			(double)center[2] * normal[2] - distance;

		for (axis = 0U; axis < 3U; axis++)
			center[axis] = (float)((double)center[axis] -
				residual * normal[axis]);
	}
	for (axis = 1U; axis < 3U; axis++)
		if (fabs(normal[axis]) < fabs(normal[seed_axis]))
			seed_axis = axis;
	memset(tangent, 0, sizeof(tangent));
	tangent[seed_axis] = 1.0f;
	{
		double projection = (double)tangent[0] * normal[0] +
			(double)tangent[1] * normal[1] +
			(double)tangent[2] * normal[2];
		double length_squared;

		for (axis = 0U; axis < 3U; axis++)
			tangent[axis] = (float)((double)tangent[axis] -
				projection * normal[axis]);
		length_squared = (double)tangent[0] * tangent[0] +
			(double)tangent[1] * tangent[1] +
			(double)tangent[2] * tangent[2];
		if (!(length_squared > 0.0))
			return 0;
		scale = (float)(1.0 / sqrt(length_squared));
		for (axis = 0U; axis < 3U; axis++)
			tangent[axis] *= scale;
	}
	bitangent[0] = (float)(normal[1] * tangent[2] -
		normal[2] * tangent[1]);
	bitangent[1] = (float)(normal[2] * tangent[0] -
		normal[0] * tangent[2]);
	bitangent[2] = (float)(normal[0] * tangent[1] -
		normal[1] * tangent[0]);
	scale = (float)(1.0 / sqrt((double)bitangent[0] * bitangent[0] +
		(double)bitangent[1] * bitangent[1] +
		(double)bitangent[2] * bitangent[2]));
	for (axis = 0U; axis < 3U; axis++)
		bitangent[axis] *= scale;
	for (side = 0U; side < 2U; side++)
	{
		const sg_configuration_cell_t *cell =
			&proof->space->cells[cell_indices[side]];

		for (offset = 0U; offset < cell->face_count; offset++)
		{
			const sg_configuration_plane_t *plane = &proof->space->faces[
				cell->first_face + offset].plane;
			double clip_normal[3], clip_distance, slack;

			if (SG_BspProofPlanesCoplanar(plane, &boundary->plane))
				continue;
			if (!UnitPlane(plane, clip_normal, &clip_distance))
				return 0;
			slack = clip_distance -
				((double)center[0] * clip_normal[0] +
				 (double)center[1] * clip_normal[1] +
				 (double)center[2] * clip_normal[2]);
			if (!(slack > 0.0))
				return 0;
			if (slack < minimum_slack)
				minimum_slack = (float)slack;
		}
	}
	if (!isfinite(minimum_slack) ||
		!(minimum_slack > PORTAL_CANONICAL_POINT_EPSILON * 8.0))
		return 0;
	radius = minimum_slack * 0.125f;
	for (vertex = 0U; vertex < 4U; vertex++)
	{
		for (axis = 0U; axis < 3U; axis++)
			vertices[vertex][axis] = center[axis] + radius *
				((vertex == 0U || vertex == 3U) ? tangent[axis] :
				 -tangent[axis]) + radius *
				((vertex < 2U) ? bitangent[axis] : -bitangent[axis]);
		if (!PointInsidePortalCells(proof, first_cell, second_cell,
				vertices[vertex]))
			return 0;
		Project(vertices[vertex], drop, polygon[vertex].value);
	}
	*area_out = (float)(fabs(PolygonSignedArea2(polygon, 4U)) * 0.5 /
		fabs(normal[drop]));
	return isfinite(*area_out) && *area_out > PORTAL_AREA_EPSILON;
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
	double area_tolerance;
	double clearance_tolerance;
	float expected_clearance;
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
	area_tolerance = PortalAreaMatchTolerance(expected3, expected_count,
		&face->plane, expected_area);
	expected_clearance = sqrtf(expected_area);
	clearance_tolerance = PortalClearanceMatchTolerance(expected_area,
		area_tolerance, expected_clearance);
	if (fabs((double)portal_area - (double)expected_area) <= area_tolerance &&
		fabs((double)overlap_area - (double)expected_area) <= area_tolerance &&
		fabs((double)portal_area - (double)overlap_area) <= area_tolerance &&
		fabs((double)portal->clearance - (double)expected_clearance) <=
			clearance_tolerance &&
		fabs((double)portal->clearance - sqrt((double)portal_area)) <=
			clearance_tolerance)
		match = 1;

done:
	free(expected3);
	free(portal_polygon);
	free(overlap);
	return match;
}

static int OffsetBucket(int64_t source, int delta, int64_t *result);

static int FindPortal(sg_bsp_proof_context_t *proof, uint8_t *seen,
	const sg_bsp_proof_portal_ref_t *refs, uint32_t cell_a, uint32_t cell_b,
	const sg_configuration_face_t *face, const portal_point2_t *polygon,
	uint32_t polygon_count, float area)
{
	uint32_t low_cell = cell_a < cell_b ? cell_a : cell_b;
	uint32_t high_cell = cell_a < cell_b ? cell_b : cell_a;
	uint32_t stance = (uint32_t)proof->space->cells[cell_a].stance;
	int64_t normal_buckets[3], plane_bucket;
	int delta_0, delta_1, delta_2, delta_distance;
	uint32_t dominant;
	uint8_t orientation;

	proof->result.portal_endpoint_lookups++;
	if (!polygon || polygon_count < 3U)
		return 0;
	if (!SG_BspProofPlaneKey(&face->plane, &dominant, normal_buckets,
			&plane_bucket, &orientation))
		return 0;
	for (delta_0 = -1; delta_0 <= 1; delta_0++)
		for (delta_1 = -1; delta_1 <= 1; delta_1++)
			for (delta_2 = -1; delta_2 <= 1; delta_2++)
				for (delta_distance = -1; delta_distance <= 1;
					delta_distance++)
		{
			int64_t bucket_0, bucket_1, bucket_2, distance_bucket;
			uint32_t cursor, end;

			if (!OffsetBucket(normal_buckets[0], delta_0, &bucket_0) ||
				!OffsetBucket(normal_buckets[1], delta_1, &bucket_1) ||
				!OffsetBucket(normal_buckets[2], delta_2, &bucket_2) ||
				!OffsetBucket(plane_bucket, delta_distance, &distance_bucket))
				continue;
			cursor = SG_BspProofPortalGroupBound(refs,
				proof->space->portal_count, low_cell, high_cell, stance,
				bucket_0, bucket_1, bucket_2, distance_bucket, 0);
			end = SG_BspProofPortalGroupBound(refs,
				proof->space->portal_count, low_cell, high_cell, stance,
				bucket_0, bucket_1, bucket_2, distance_bucket, 1);
			for (; cursor < end; cursor++)
			{
				uint32_t portal = refs[cursor].portal;
				int matches;

				proof->result.portal_lookup_candidates++;
				matches = PortalMatches(proof, portal, cell_a, cell_b,
					face, polygon, polygon_count, area);
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

#ifdef SG_BSP_COMPLETENESS_TESTING
int SG_BspProofTestRepeatedExpectedPortal(void)
{
	sg_configuration_cell_t cells[2];
	sg_configuration_portal_t portal;
	sg_configuration_space_t space;
	sg_rune_vec3_t vertices[4];
	sg_configuration_face_t first, second;
	sg_bsp_proof_context_t proof;
	sg_bsp_proof_portal_ref_t *refs = NULL;
	portal_point2_t polygon[4] = {
		{ { -1.0, -1.0 } }, { { 1.0, -1.0 } },
		{ { 1.0, 1.0 } }, { { -1.0, 1.0 } }
	};
	uint8_t seen[1] = { 0U };
	int first_found, second_found;

	memset(cells, 0, sizeof(cells));
	memset(&portal, 0, sizeof(portal));
	memset(&space, 0, sizeof(space));
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	memset(&proof, 0, sizeof(proof));
	cells[0].stance = SG_RUNE_STANCE_STANDING;
	cells[1].stance = SG_RUNE_STANCE_STANDING;
	portal.to_cell = 1U;
	portal.stance = SG_RUNE_STANCE_STANDING;
	portal.plane.normal[0] = 1.0f;
	portal.vertex_count = 4U;
	portal.clearance = 2.0f;
	vertices[0].value[0] = vertices[1].value[0] = 0.0f;
	vertices[2].value[0] = vertices[3].value[0] = 0.0f;
	vertices[0].value[1] = vertices[3].value[1] = -1.0f;
	vertices[1].value[1] = vertices[2].value[1] = 1.0f;
	vertices[0].value[2] = vertices[1].value[2] = -1.0f;
	vertices[2].value[2] = vertices[3].value[2] = 1.0f;
	space.cells = cells;
	space.cell_count = 2U;
	space.vertices = vertices;
	space.vertex_count = 4U;
	space.portals = &portal;
	space.portal_count = 1U;
	proof.space = &space;
	first.plane.normal[0] = 1.0f;
	first.plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	first.plane.source_index = 7U;
	second.plane.normal[0] = -2.0f;
	second.plane.source_kind = SG_CONFIGURATION_PLANE_EXPANDED_BRUSH;
	second.plane.source_index = 99U;
	second.plane.source_variant = 3U;
	if (!SG_BspProofBuildPortalRefs(&proof, &refs))
		return 0;
	first_found = FindPortal(&proof, seen, refs, 0U, 1U, &first, polygon,
		4U, 4.0f);
	second_found = FindPortal(&proof, seen, refs, 0U, 1U, &second, polygon,
		4U, 4.0f);
	free(refs);
	return first_found == 1 && second_found == 1 && seen[0] == 1U;
}

int SG_BspProofTestPortalPlaneIndexScaling(uint32_t count,
	uint64_t *candidates_out)
{
	sg_configuration_cell_t cells[2];
	sg_configuration_portal_t *portals = NULL;
	sg_configuration_space_t space;
	sg_rune_vec3_t vertices[4];
	sg_configuration_face_t face;
	sg_bsp_proof_context_t proof;
	sg_bsp_proof_portal_ref_t *refs = NULL;
	portal_point2_t polygon[4] = {
		{ { -1.0, -1.0 } }, { { 1.0, -1.0 } },
		{ { 1.0, 1.0 } }, { { -1.0, 1.0 } }
	};
	uint8_t *seen = NULL;
	uint32_t index;
	int found, target_seen;

	if (!count || !candidates_out)
		return 0;
	portals = calloc(count, sizeof(*portals));
	seen = calloc(count, sizeof(*seen));
	if (!portals || !seen)
		goto done;
	memset(cells, 0, sizeof(cells));
	memset(&space, 0, sizeof(space));
	memset(&face, 0, sizeof(face));
	memset(&proof, 0, sizeof(proof));
	cells[0].stance = SG_RUNE_STANCE_STANDING;
	cells[1].stance = SG_RUNE_STANCE_STANDING;
	for (index = 0U; index < count; index++)
	{
		portals[index].to_cell = 1U;
		portals[index].stance = SG_RUNE_STANCE_STANDING;
		portals[index].plane.normal[0] = 1.0f;
		portals[index].plane.distance = 1.0f + 0.01f * (float)index;
	}
	portals[count - 1U].plane.distance = 0.0f;
	portals[count - 1U].vertex_count = 4U;
	portals[count - 1U].clearance = 2.0f;
	vertices[0].value[0] = vertices[1].value[0] = 0.0f;
	vertices[2].value[0] = vertices[3].value[0] = 0.0f;
	vertices[0].value[1] = vertices[3].value[1] = -1.0f;
	vertices[1].value[1] = vertices[2].value[1] = 1.0f;
	vertices[0].value[2] = vertices[1].value[2] = -1.0f;
	vertices[2].value[2] = vertices[3].value[2] = 1.0f;
	space.cells = cells;
	space.cell_count = 2U;
	space.vertices = vertices;
	space.vertex_count = 4U;
	space.portals = portals;
	space.portal_count = count;
	proof.space = &space;
	face.plane.normal[0] = -3.0f;
	if (!SG_BspProofBuildPortalRefs(&proof, &refs))
		goto done;
	found = FindPortal(&proof, seen, refs, 0U, 1U, &face, polygon, 4U, 4.0f);
	target_seen = seen[count - 1U] != 0U;
	*candidates_out = proof.result.portal_lookup_candidates;
	free(refs);
	free(seen);
	free(portals);
	return found == 1 && target_seen;

done:
	free(refs);
	free(seen);
	free(portals);
	return 0;
}
#endif

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
	portal_point2_t tangent_polygon[4];
	const portal_point2_t *expected_polygon;
	uint32_t polygon_count = 0U;
	uint32_t expected_count;
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
	if (!polygon_count && !ZeroPolygonUsesAuthoritativeFallback(left, right))
		return 1;
	from_result = PortalSideWitness(proof, left_cell, right_cell, left, from);
	to_result = PortalSideWitness(proof, right_cell, left_cell, right, to);
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
	expected_polygon = polygon;
	expected_count = polygon_count;
	if (!polygon_count)
	{
		if (!TangentExpectedPolygon(proof, left_cell, right_cell, left,
				from, to, tangent_polygon, &area))
		{
			free(polygon);
			return 1;
		}
		expected_polygon = tangent_polygon;
		expected_count = 4U;
	}
	proof->result.expected_portals++;
	found = FindPortal(proof, seen, portals, left_cell, right_cell, left,
		expected_polygon, expected_count, area);
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

#ifdef SG_BSP_COMPLETENESS_TESTING
static void SetTestPlane(sg_configuration_face_t *face, float x, float y,
	float z, float distance)
{
	memset(face, 0, sizeof(*face));
	face->plane.normal[0] = x;
	face->plane.normal[1] = y;
	face->plane.normal[2] = z;
	face->plane.distance = distance;
}

static int TestNarrowHighCoordinatePortal(
	const sg_host_collision_authority_t *authority, float expected_low,
	float expected_high, sg_rune_vec3_t portal_vertices[4],
	float portal_clearance, int zero_polygon,
	sg_bsp_completeness_result_t *result_out)
{
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[12];
	sg_rune_vec3_t expected_vertices[4];
	sg_configuration_portal_t portal;
	sg_configuration_space_t space;
	sg_bsp_proof_context_t proof;
	sg_bsp_proof_face_ref_t left, right;
	sg_bsp_proof_portal_ref_t *refs = NULL;
	uint8_t seen[1] = { 0U };
	float bounds_low = fmaxf(SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
		expected_low - 0.5f);
	float bounds_high = fminf(SG_CONFIGURATION_PMOVE_ORIGIN_MAX,
		expected_high + 0.5f);
	int audited;

	if (!authority || !portal_vertices || !result_out ||
		!(expected_low < expected_high))
		return 0;
	memset(cells, 0, sizeof(cells));
	memset(faces, 0, sizeof(faces));
	memset(expected_vertices, 0, sizeof(expected_vertices));
	memset(&portal, 0, sizeof(portal));
	memset(&space, 0, sizeof(space));
	memset(&proof, 0, sizeof(proof));
	memset(&left, 0, sizeof(left));
	memset(&right, 0, sizeof(right));
	cells[0].face_count = 6U;
	cells[0].stance = SG_RUNE_STANCE_STANDING;
	cells[0].bounds.mins.value[0] = -1.0f;
	cells[0].bounds.mins.value[1] = bounds_low;
	cells[0].bounds.mins.value[2] = bounds_low;
	cells[0].bounds.maxs.value[1] = bounds_high;
	cells[0].bounds.maxs.value[2] = bounds_high;
	cells[1] = cells[0];
	cells[1].first_face = 6U;
	cells[1].bounds.mins.value[0] = 0.0f;
	cells[1].bounds.maxs.value[0] = 1.0f;
	SetTestPlane(&faces[0], 1.0f, 0.0f, 0.0f, 0.0f);
	SetTestPlane(&faces[1], -1.0f, 0.0f, 0.0f, 1.0f);
	SetTestPlane(&faces[2], 0.0f, 1.0f, 0.0f, bounds_high);
	SetTestPlane(&faces[3], 0.0f, -1.0f, 0.0f, -bounds_low);
	SetTestPlane(&faces[4], 0.0f, 0.0f, 1.0f, bounds_high);
	SetTestPlane(&faces[5], 0.0f, 0.0f, -1.0f, -bounds_low);
	SetTestPlane(&faces[6], -1.0f, 0.0f, 0.0f, 0.0f);
	SetTestPlane(&faces[7], 1.0f, 0.0f, 0.0f, 1.0f);
	SetTestPlane(&faces[8], 0.0f, 1.0f, 0.0f, bounds_high);
	SetTestPlane(&faces[9], 0.0f, -1.0f, 0.0f, -bounds_low);
	SetTestPlane(&faces[10], 0.0f, 0.0f, 1.0f, bounds_high);
	SetTestPlane(&faces[11], 0.0f, 0.0f, -1.0f, -bounds_low);
	if (zero_polygon)
	{
		faces[0].kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
		faces[6].kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
	}
	expected_vertices[0].value[1] = expected_vertices[0].value[2] = expected_low;
	expected_vertices[1].value[1] = expected_high;
	expected_vertices[1].value[2] = expected_low;
	expected_vertices[2].value[1] = expected_vertices[2].value[2] = expected_high;
	expected_vertices[3].value[1] = expected_low;
	expected_vertices[3].value[2] = expected_high;
	portal.to_cell = 1U;
	portal.stance = SG_RUNE_STANCE_STANDING;
	portal.plane = faces[0].plane;
	portal.vertex_count = 4U;
	portal.clearance = portal_clearance;
	space.cells = cells;
	space.cell_count = 2U;
	space.faces = faces;
	space.face_count = 12U;
	space.vertices = portal_vertices;
	space.vertex_count = 4U;
	space.portals = &portal;
	space.portal_count = 1U;
	proof.authority = authority;
	proof.space = &space;
	if (!PortalRecordValid(&proof, 0U))
	{
		SG_BspProofFail(&proof, SG_BSP_COMPLETENESS_INVALID_PORTAL, 0U);
		*result_out = proof.result;
		return 0;
	}
	left.cell = 0U;
	left.face = 0U;
	left.stance = SG_RUNE_STANCE_STANDING;
	left.vertex_count = zero_polygon ? 0U : 4U;
	left.vertices = zero_polygon ? NULL : expected_vertices;
	left.bounds_mins[1] = left.bounds_mins[2] = expected_low;
	left.bounds_maxs[1] = left.bounds_maxs[2] = expected_high;
	right = left;
	right.cell = 1U;
	right.face = 6U;
	right.orientation = 1U;
	if (!SG_BspProofBuildPortalRefs(&proof, &refs))
	{
		*result_out = proof.result;
		return 0;
	}
	audited = AuditFacePair(&proof, seen, refs, &left, &right);
	free(refs);
	*result_out = proof.result;
	return audited && seen[0] == 1U;
}

int SG_BspProofTestNarrowHighCoordinatePortal(
	const sg_host_collision_authority_t *authority, float expected_low,
	float expected_high, float portal_low, float portal_high,
	sg_bsp_completeness_result_t *result_out)
{
	sg_rune_vec3_t vertices[4] = { 0 };

	if (!(portal_low < portal_high))
		return 0;
	vertices[0].value[1] = vertices[0].value[2] = portal_low;
	vertices[1].value[1] = portal_high;
	vertices[1].value[2] = portal_low;
	vertices[2].value[1] = vertices[2].value[2] = portal_high;
	vertices[3].value[1] = portal_low;
	vertices[3].value[2] = portal_high;
	return TestNarrowHighCoordinatePortal(authority, expected_low,
		expected_high, vertices, portal_high - portal_low, 0, result_out);
}

int SG_BspProofTestNarrowHighCoordinateBowtie(
	const sg_host_collision_authority_t *authority, float expected_low,
	float expected_high, sg_bsp_completeness_result_t *result_out)
{
	sg_rune_vec3_t vertices[4] = { 0 };

	vertices[0].value[1] = vertices[0].value[2] = -4096.0f;
	vertices[1].value[1] = vertices[1].value[2] = 4095.875f;
	vertices[2].value[1] = -4096.0f;
	vertices[2].value[2] = 4095.875f;
	vertices[3].value[1] = 4095.875f;
	vertices[3].value[2] = -4096.0f;
	return TestNarrowHighCoordinatePortal(authority, expected_low,
		expected_high, vertices, expected_high - expected_low, 0, result_out);
}

int SG_BspProofTestNormalDisplacedHighCoordinatePortal(
	const sg_host_collision_authority_t *authority, float normal_displacement,
	sg_bsp_completeness_result_t *result_out)
{
	const float low = 4095.7451171875f;
	const float high = 4095.7548828125f;
	sg_rune_vec3_t vertices[4] = { 0 };
	uint32_t vertex;

	for (vertex = 0U; vertex < 4U; vertex++)
		vertices[vertex].value[0] = normal_displacement;
	vertices[0].value[1] = vertices[0].value[2] = low;
	vertices[1].value[1] = high;
	vertices[1].value[2] = low;
	vertices[2].value[1] = vertices[2].value[2] = high;
	vertices[3].value[1] = low;
	vertices[3].value[2] = high;
	return TestNarrowHighCoordinatePortal(authority, low, high, vertices,
		high - low, 0, result_out);
}

int SG_BspProofTestConstraintFallbackInventedPortal(
	const sg_host_collision_authority_t *authority,
	sg_bsp_completeness_result_t *result_out)
{
	sg_rune_vec3_t vertices[4] = { 0 };

	vertices[0].value[1] = vertices[0].value[2] = -0.5f;
	vertices[1].value[1] = 0.5f;
	vertices[1].value[2] = -0.5f;
	vertices[2].value[1] = vertices[2].value[2] = 0.5f;
	vertices[3].value[1] = -0.5f;
	vertices[3].value[2] = 0.5f;
	return TestNarrowHighCoordinatePortal(authority, -0.125f, 0.125f,
		vertices, 1.0f, 1, result_out);
}

int SG_BspProofTestPortalVertexLimit(void)
{
	sg_configuration_cell_t cells[2] = { 0 };
	sg_configuration_portal_t portal = { 0 };
	sg_configuration_space_t space = { 0 };
	sg_rune_vec3_t vertices[SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL + 1U]
		= { 0 };
	sg_bsp_proof_context_t proof = { 0 };
	double area2 = 0.0;
	uint32_t index;

	for (index = 0U;
		index < SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL + 1U; index++)
	{
		double angle = PORTAL_PI + 2.0 * PORTAL_PI * (double)index /
			(double)(SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL + 1U);

		vertices[index].value[1] = (float)cos(angle);
		vertices[index].value[2] = (float)sin(angle);
	}
	for (index = 0U;
		index < SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL + 1U; index++)
	{
		const float *point = vertices[index].value;
		const float *next = vertices[(index + 1U) %
			(SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL + 1U)].value;

		area2 += (double)point[1] * next[2] - (double)next[1] * point[2];
	}

	portal.to_cell = 1U;
	portal.stance = SG_RUNE_STANCE_STANDING;
	portal.plane.normal[0] = 1.0f;
	portal.vertex_count = SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL + 1U;
	portal.clearance = sqrtf((float)(area2 * 0.5));
	space.cells = cells;
	space.cell_count = 2U;
	space.vertices = vertices;
	space.vertex_count = portal.vertex_count;
	space.portals = &portal;
	space.portal_count = 1U;
	proof.space = &space;
	return !PortalRecordValid(&proof, 0U);
}
#endif

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
