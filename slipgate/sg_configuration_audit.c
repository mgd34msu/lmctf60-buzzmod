#include "sg_configuration_audit.h"
#include "sg_configuration_lattice.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define AUDIT_EPSILON 0.001f
#define AUDIT_GEOMETRY_EPSILON 0.000001f
#define AUDIT_AREA_EPSILON 0.000001f

typedef struct audit_halfspace_s
{
	float normal[3];
	float distance;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint32_t reversed;
} audit_halfspace_t;

typedef struct audit_context_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *space;
	sg_configuration_audit_result_t result;
	uint8_t *seen_nodes;
	uint8_t *seen_cells;
} audit_context_t;

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

static int FiniteVector(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static void Fail(audit_context_t *audit, sg_configuration_audit_code_t code,
	uint32_t record)
{
	if (audit->result.code == SG_CONFIGURATION_AUDIT_OK)
	{
		audit->result.code = code;
		audit->result.record = record;
	}
}

static audit_halfspace_t FromPlane(const sg_configuration_plane_t *plane)
{
	audit_halfspace_t result;

	CopyVector(result.normal, plane->normal);
	result.distance = plane->distance;
	result.source_kind = plane->source_kind;
	result.source_index = plane->source_index;
	result.source_variant = plane->source_variant;
	result.reversed = plane->reversed;
	return result;
}

static audit_halfspace_t ReverseHalfspace(audit_halfspace_t source)
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		source.normal[axis] = -source.normal[axis];
	source.distance = -source.distance;
	source.reversed ^= 1U;
	return source;
}

static int AppendHalfspace(const audit_halfspace_t *source,
	uint32_t source_count, audit_halfspace_t value,
	audit_halfspace_t **result_out)
{
	audit_halfspace_t *result = malloc((size_t)(source_count + 1U) *
		sizeof(*result));

	if (!result)
		return 0;
	if (source_count)
		memcpy(result, source, (size_t)source_count * sizeof(*result));
	result[source_count] = value;
	*result_out = result;
	return 1;
}

static float Determinant(const float a[3], const float b[3], const float c[3])
{
	return a[0] * (b[1] * c[2] - b[2] * c[1]) -
		a[1] * (b[0] * c[2] - b[2] * c[0]) +
		a[2] * (b[0] * c[1] - b[1] * c[0]);
}

static int IntersectThree(const audit_halfspace_t *a,
	const audit_halfspace_t *b, const audit_halfspace_t *c, float point[3])
{
	float determinant = Determinant(a->normal, b->normal, c->normal);
	float da[3] = { a->distance, b->distance, c->distance };
	float column0[3] = { da[0], da[1], da[2] };
	float column1[3] = { a->normal[1], b->normal[1], c->normal[1] };
	float column2[3] = { a->normal[2], b->normal[2], c->normal[2] };
	float x1[3] = { a->normal[0], b->normal[0], c->normal[0] };
	float x2[3] = { a->normal[2], b->normal[2], c->normal[2] };
	float y2[3] = { a->normal[1], b->normal[1], c->normal[1] };

	if (fabsf(determinant) <= AUDIT_GEOMETRY_EPSILON)
		return 0;
	point[0] = Determinant(column0, column1, column2) / determinant;
	point[1] = Determinant(x1, da, x2) / determinant;
	point[2] = Determinant(x1, y2, da) / determinant;
	return FiniteVector(point);
}

static int PointInside(const float point[3], const audit_halfspace_t *spaces,
	uint32_t count)
{
	uint32_t index;

	for (index = 0; index < count; index++)
		if (Dot(point, spaces[index].normal) - spaces[index].distance >
			AUDIT_EPSILON)
			return 0;
	return 1;
}

static int PointInsideVolume(const float point[3],
	const audit_halfspace_t *spaces, uint32_t count)
{
	uint32_t index;

	for (index = 0; index < count; index++)
		if (Dot(point, spaces[index].normal) - spaces[index].distance >
			AUDIT_GEOMETRY_EPSILON)
			return 0;
	return 1;
}

static int RegionHasVolume(const audit_halfspace_t *spaces, uint32_t count)
{
	uint32_t first, second, third;
	float points[4][3];
	uint32_t point_count = 0;
	float mins[3] = { INFINITY, INFINITY, INFINITY };
	float maxs[3] = { -INFINITY, -INFINITY, -INFINITY };
	int noncoplanar = 0;

	for (first = 0; first < count; first++)
		for (second = first + 1U; second < count; second++)
			for (third = second + 1U; third < count; third++)
			{
				float point[3];
				uint32_t index;
				int unique = 1;

				if (!IntersectThree(&spaces[first], &spaces[second],
						&spaces[third], point) ||
					!PointInsideVolume(point, spaces, count))
					continue;
				for (index = 0; index < point_count; index++)
					if (fabsf(point[0] - points[index][0]) <=
							AUDIT_GEOMETRY_EPSILON &&
						fabsf(point[1] - points[index][1]) <=
							AUDIT_GEOMETRY_EPSILON &&
						fabsf(point[2] - points[index][2]) <=
							AUDIT_GEOMETRY_EPSILON)
						unique = 0;
				if (!unique)
					continue;
				for (index = 0; index < 3; index++)
				{
					if (point[index] < mins[index])
						mins[index] = point[index];
					if (point[index] > maxs[index])
						maxs[index] = point[index];
				}
				if (point_count < 4U)
					CopyVector(points[point_count++], point);
				if (point_count == 4U)
				{
					float x[3], y[3], z[3];
					uint32_t axis;

					for (axis = 0; axis < 3; axis++)
					{
						x[axis] = points[1][axis] - points[0][axis];
						y[axis] = points[2][axis] - points[0][axis];
						z[axis] = points[3][axis] - points[0][axis];
					}
					if (fabsf(Determinant(x, y, z)) > AUDIT_GEOMETRY_EPSILON)
						noncoplanar = 1;
					else
						point_count = 3U;
				}
			}
	if (!noncoplanar)
		return 0;
	(void)mins;
	(void)maxs;
	return 1;
}

static int HalfspaceIsOpen(const audit_halfspace_t *space)
{
	return space->source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
		space->reversed != 0U;
}

static int RegionHasProtocolWitness(audit_context_t *audit,
	const audit_halfspace_t *spaces, uint32_t count, sg_rune_stance_t stance)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	float witness[3];
	sg_host_collision_pose_t pose;
	uint32_t index, axis;
	int result, positive_margin;

	halfspaces = malloc((size_t)count * sizeof(*halfspaces));
	clearance = malloc((size_t)count * sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		return -1;
	}
	for (index = 0; index < count; index++)
	{
		CopyVector(halfspaces[index].normal, spaces[index].normal);
		halfspaces[index].distance = spaces[index].distance;
		halfspaces[index].open = HalfspaceIsOpen(&spaces[index]);
		clearance[index] = 1U;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		count, NULL, point, &positive_margin, &stats);
	free(halfspaces);
	free(clearance);
	audit->result.lattice_solve_calls += stats.solve_calls;
	audit->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > audit->result.lattice_maximum_binary_shift)
		audit->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (result <= 0)
		return result;
	if (!positive_margin)
		return 0;
	for (axis = 0; axis < 3; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	return SG_HostCollisionClassifyPose(audit->authority, NULL, witness, stance,
		&pose) && pose.valid ? 1 : -1;
}

static int PlaneMatches(const sg_configuration_plane_t *face,
	const audit_halfspace_t *space)
{
	uint32_t axis;

	if (face->source_kind != space->source_kind ||
		face->source_index != space->source_index ||
		face->source_variant != space->source_variant ||
		face->reversed != space->reversed ||
		fabsf(face->distance - space->distance) > AUDIT_EPSILON)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (fabsf(face->normal[axis] - space->normal[axis]) > AUDIT_EPSILON)
			return 0;
	return 1;
}

static int ValidateCellRegion(audit_context_t *audit, uint32_t cell_index,
	uint32_t leaf, sg_rune_stance_t stance,
	const audit_halfspace_t *spaces, uint32_t space_count)
{
	const sg_configuration_space_t *configuration = audit->space;
	const sg_configuration_cell_t *cell;
	uint32_t face_offset, vertex_offset, constraint;

	if (cell_index >= configuration->cell_count ||
		audit->seen_cells[cell_index])
		return 0;
	cell = &configuration->cells[cell_index];
	if (cell->stance != stance || cell->bsp_leaf.index != leaf ||
		cell->first_face > configuration->face_count ||
		cell->face_count > configuration->face_count - cell->first_face)
		return 0;
	/* Every output vertex must remain in the inherited certificate region. */
	for (face_offset = 0; face_offset < cell->face_count; face_offset++)
	{
		const sg_configuration_face_t *face =
			&configuration->faces[cell->first_face + face_offset];
		int inherited = 0;

		if (face->first_vertex > configuration->vertex_count ||
			face->vertex_count < 3U ||
			face->vertex_count > configuration->vertex_count -
				face->first_vertex)
			return 0;
		for (constraint = 0; constraint < space_count; constraint++)
			if (PlaneMatches(&face->plane, &spaces[constraint]))
			{
				inherited = 1;
				break;
			}
		if (!inherited)
			return 0;
		for (vertex_offset = 0; vertex_offset < face->vertex_count;
			vertex_offset++)
			if (!PointInside(configuration->vertices[
					face->first_vertex + vertex_offset].value, spaces,
					space_count))
				return 0;
	}
	/* Every output face is an inherited inequality, so the certificate region
	 * is a subset of the cell. The vertex checks above prove the reverse. */
	audit->seen_cells[cell_index] = 1;
	audit->result.proved_cells++;
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

static int ValidateBlocked(const audit_context_t *audit, uint32_t brush_index,
	sg_rune_stance_t stance, const audit_halfspace_t *spaces,
	uint32_t space_count)
{
	const sg_bsp_world_t *world = audit->authority->world;
	const sg_rune_hull_profile_t *hull = stance == SG_RUNE_STANCE_STANDING ?
		&audit->authority->identity.standing_hull :
		&audit->authority->identity.crouching_hull;
	const sg_bsp_brush_t *brush;
	uint32_t side;

	if (brush_index >= world->brush_count)
		return 0;
	brush = &world->brushes[brush_index];
	if (!brush->side_count || !((uint32_t)brush->contents &
		SG_HOST_MASK_PLAYER_SOLID))
		return 0;
	for (side = 0; side < brush->side_count; side++)
	{
		uint32_t side_index = brush->first_side + side;
		uint32_t plane_index = world->brush_sides[side_index].plane;
		const sg_bsp_plane_t *plane = &world->planes[plane_index];
		float distance = plane->distance - HullMinimum(hull, plane->normal.value);
		uint32_t constraint;
		int found = 0;

		for (constraint = 0; constraint < space_count; constraint++)
			if (spaces[constraint].source_kind ==
					SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
				spaces[constraint].source_index == side_index &&
				spaces[constraint].source_variant == (uint32_t)stance &&
				spaces[constraint].reversed == 0U &&
				fabsf(spaces[constraint].distance - distance) <= AUDIT_EPSILON)
			{
				found = 1;
				break;
			}
		if (!found)
			return 0;
	}
	return 1;
}

static int AuditCertificateNode(audit_context_t *audit, uint32_t node_index,
	sg_rune_stance_t stance, uint32_t leaf,
	const audit_halfspace_t *spaces, uint32_t space_count)
{
	const sg_configuration_certificate_node_t *node;

	if (node_index >= audit->space->certificate_node_count ||
		audit->seen_nodes[node_index])
	{
		Fail(audit, SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE, node_index);
		return 0;
	}
	audit->seen_nodes[node_index] = 1;
	node = &audit->space->certificate_nodes[node_index];
	if (node->stance != stance)
	{
		Fail(audit, SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE, node_index);
		return 0;
	}
	if (node->bsp_leaf != SG_CONFIGURATION_INDEX_NONE)
		leaf = node->bsp_leaf;
	switch (node->kind)
	{
	case SG_CONFIGURATION_CERTIFICATE_SPLIT:
	{
		audit_halfspace_t split = FromPlane(&node->plane);
		audit_halfspace_t *front_spaces = NULL;
		audit_halfspace_t *back_spaces = NULL;
		int valid;

		if (!FiniteVector(split.normal) || !isfinite(split.distance) ||
			node->front == SG_CONFIGURATION_INDEX_NONE ||
			node->back == SG_CONFIGURATION_INDEX_NONE ||
			!AppendHalfspace(spaces, space_count, ReverseHalfspace(split),
				&front_spaces) ||
			!AppendHalfspace(spaces, space_count, split, &back_spaces))
		{
			free(front_spaces);
			free(back_spaces);
			return 0;
		}
		valid = AuditCertificateNode(audit, node->front, stance, leaf,
			front_spaces, space_count + 1U) &&
			AuditCertificateNode(audit, node->back, stance, leaf,
				back_spaces, space_count + 1U);
		free(front_spaces);
		free(back_spaces);
		return valid;
	}
	case SG_CONFIGURATION_CERTIFICATE_VALID:
		if (leaf != SG_CONFIGURATION_INDEX_NONE &&
			ValidateCellRegion(audit, node->cell, leaf, stance, spaces,
				space_count))
			return 1;
		break;
	case SG_CONFIGURATION_CERTIFICATE_BLOCKED:
		if (leaf != SG_CONFIGURATION_INDEX_NONE &&
			ValidateBlocked(audit, node->blocking_brush, stance, spaces,
				space_count))
			return 1;
		break;
	case SG_CONFIGURATION_CERTIFICATE_EMPTY:
	{
		int witness = RegionHasProtocolWitness(audit, spaces, space_count,
			stance);

		if (witness < 0)
			break;
		if (!witness || !RegionHasVolume(spaces, space_count))
			return 1;
		break;
	}
	default:
		break;
	}
	Fail(audit, SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE, node_index);
	return 0;
}

static int PointLeaf(const sg_bsp_world_t *world, const float point[3],
	uint32_t *leaf_out)
{
	int32_t child = world->models[0].headnode;

	while (child >= 0)
	{
		const sg_bsp_node_t *node = &world->nodes[(uint32_t)child];
		const sg_bsp_plane_t *plane = &world->planes[node->plane];

		child = node->children[Dot(point, plane->normal.value) -
			plane->distance < 0.0f];
	}
	*leaf_out = (uint32_t)(-1 - child);
	return *leaf_out < world->leaf_count;
}

static int AuditFaceInteriorWitness(audit_context_t *audit,
	const sg_configuration_cell_t *cell,
	const sg_configuration_face_t *boundary)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	float witness[3];
	sg_host_collision_pose_t pose;
	uint32_t offset, axis;
	int positive_margin, result;

	halfspaces = malloc((size_t)cell->face_count * sizeof(*halfspaces));
	clearance = malloc((size_t)cell->face_count * sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		return -1;
	}
	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_face_t *face =
			&audit->space->faces[cell->first_face + offset];

		CopyVector(halfspaces[offset].normal, face->plane.normal);
		halfspaces[offset].distance = face->plane.distance;
		halfspaces[offset].open = face->plane.source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH && face->plane.reversed != 0U;
		clearance[offset] = face != boundary;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		cell->face_count, boundary->plane.normal, point, &positive_margin,
		&stats);
	free(halfspaces);
	free(clearance);
	audit->result.lattice_solve_calls += stats.solve_calls;
	audit->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > audit->result.lattice_maximum_binary_shift)
		audit->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (result <= 0)
		return result;
	if (!positive_margin)
		return 0;
	for (axis = 0; axis < 3U; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	return SG_HostCollisionClassifyPose(audit->authority, NULL, witness,
		cell->stance, &pose) && pose.valid ? 1 : -1;
}

static int AuditHostCells(audit_context_t *audit)
{
	const sg_bsp_world_t *world = audit->authority->world;
	uint32_t cell_index;

	for (cell_index = 0; cell_index < audit->space->cell_count; cell_index++)
	{
		const sg_configuration_cell_t *cell = &audit->space->cells[cell_index];
		sg_host_collision_pose_t pose;
		uint32_t leaf, face_offset;
		sg_rune_contents_mask_t contents;

		if (!SG_HostCollisionClassifyPose(audit->authority, NULL,
				cell->interior_witness.value, cell->stance, &pose) || !pose.valid ||
			!PointLeaf(world, cell->interior_witness.value, &leaf) ||
			leaf != cell->bsp_leaf.index)
			return 0;
		contents = SG_HostCollisionRuneContents(
			SG_HostCollisionPointContents(audit->authority, NULL,
				cell->interior_witness.value));
		if (contents != cell->contents ||
			world->leaves[leaf].area != cell->bsp_area.index ||
			(world->leaves[leaf].cluster < 0 ? UINT32_MAX :
				(uint32_t)world->leaves[leaf].cluster) != cell->bsp_cluster.index)
			return 0;
		for (face_offset = 0; face_offset < cell->face_count; face_offset++)
		{
			const sg_configuration_face_t *face =
				&audit->space->faces[cell->first_face + face_offset];

			if (AuditFaceInteriorWitness(audit, cell, face) != 1)
				return 0;
			audit->result.boundary_witnesses++;
		}
	}
	return 1;
}

static uint32_t DominantAxis(const float normal[3])
{
	uint32_t axis = 0;
	uint32_t candidate;

	for (candidate = 1; candidate < 3; candidate++)
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

typedef struct audit_point2_s
{
	float value[2];
} audit_point2_t;

static int FacesOverlapArea(const sg_configuration_space_t *space,
	const sg_configuration_face_t *a, const sg_configuration_face_t *b,
	float center_out[3])
{
	const sg_rune_vec3_t *a_vertices = &space->vertices[a->first_vertex];
	const sg_rune_vec3_t *b_vertices = &space->vertices[b->first_vertex];
	uint32_t drop = DominantAxis(a->plane.normal);
	audit_point2_t *polygon;
	uint32_t count = a->vertex_count;
	float clip_orientation = 0.0f;
	uint32_t index, edge;

	polygon = malloc((size_t)count * sizeof(*polygon));
	if (!polygon)
		return 0;
	for (index = 0; index < count; index++)
		Project(a_vertices[index].value, drop, polygon[index].value);
	for (index = 0; index < b->vertex_count; index++)
	{
		float current[2], next[2];

		Project(b_vertices[index].value, drop, current);
		Project(b_vertices[(index + 1U) % b->vertex_count].value, drop, next);
		clip_orientation += current[0] * next[1] - next[0] * current[1];
	}
	for (edge = 0; edge < b->vertex_count && count >= 3U; edge++)
	{
		audit_point2_t *next_polygon = malloc((size_t)(count + 1U) *
			sizeof(*next_polygon));
		float clip_a[2], clip_b[2];
		uint32_t next_count = 0;

		if (!next_polygon)
		{
			free(polygon);
			return 0;
		}
		Project(b_vertices[edge].value, drop, clip_a);
		Project(b_vertices[(edge + 1U) % b->vertex_count].value, drop, clip_b);
		for (index = 0; index < count; index++)
		{
			const audit_point2_t *start = &polygon[index];
			const audit_point2_t *end = &polygon[(index + 1U) % count];
			float start_distance = Cross2(clip_a, clip_b, start->value);
			float end_distance = Cross2(clip_a, clip_b, end->value);
			int start_inside = clip_orientation >= 0.0f ?
				start_distance >= -AUDIT_AREA_EPSILON :
				start_distance <= AUDIT_AREA_EPSILON;
			int end_inside = clip_orientation >= 0.0f ?
				end_distance >= -AUDIT_AREA_EPSILON :
				end_distance <= AUDIT_AREA_EPSILON;

			if (start_inside)
				next_polygon[next_count++] = *start;
			if (start_inside != end_inside)
			{
				float fraction = start_distance /
					(start_distance - end_distance);

				next_polygon[next_count].value[0] = start->value[0] +
					fraction * (end->value[0] - start->value[0]);
				next_polygon[next_count].value[1] = start->value[1] +
					fraction * (end->value[1] - start->value[1]);
				next_count++;
			}
		}
		free(polygon);
		polygon = next_polygon;
		count = next_count;
	}
	if (count >= 3U)
	{
		float area = 0.0f;
		float projected_center[2] = { 0.0f, 0.0f };
		uint32_t u = (drop + 1U) % 3U;
		uint32_t v = (drop + 2U) % 3U;

		for (index = 0; index < count; index++)
		{
			area += polygon[index].value[0] *
				polygon[(index + 1U) % count].value[1] -
				polygon[(index + 1U) % count].value[0] *
				polygon[index].value[1];
			projected_center[0] += polygon[index].value[0];
			projected_center[1] += polygon[index].value[1];
		}
		free(polygon);
		if (fabsf(area) <= AUDIT_AREA_EPSILON ||
			fabsf(a->plane.normal[drop]) <= AUDIT_AREA_EPSILON)
			return 0;
		memset(center_out, 0, 3U * sizeof(*center_out));
		center_out[u] = projected_center[0] / (float)count;
		center_out[v] = projected_center[1] / (float)count;
		center_out[drop] = (a->plane.distance -
			a->plane.normal[u] * center_out[u] -
			a->plane.normal[v] * center_out[v]) / a->plane.normal[drop];
		return 1;
	}
	free(polygon);
	return 0;
}

static void AuditCanonicalPlane(const sg_configuration_plane_t *plane,
	float normal[3], float *distance);

static int CanonicalPlanesClose(const sg_configuration_plane_t *a,
	const sg_configuration_plane_t *b)
{
	float normal_a[3], normal_b[3], distance_a, distance_b;

	AuditCanonicalPlane(a, normal_a, &distance_a);
	AuditCanonicalPlane(b, normal_b, &distance_b);
	return fabsf(normal_a[0] - normal_b[0]) <= AUDIT_GEOMETRY_EPSILON &&
		fabsf(normal_a[1] - normal_b[1]) <= AUDIT_GEOMETRY_EPSILON &&
		fabsf(normal_a[2] - normal_b[2]) <= AUDIT_GEOMETRY_EPSILON &&
		fabsf(distance_a - distance_b) <= AUDIT_GEOMETRY_EPSILON;
}

static int SameBoundary(const sg_configuration_plane_t *a,
	const sg_configuration_plane_t *b)
{
	return Dot(a->normal, b->normal) < 0.0f && CanonicalPlanesClose(a, b);
}

static const sg_configuration_plane_t *CellBoundaryPlane(
	const sg_configuration_space_t *space, uint32_t cell_index,
	const sg_configuration_plane_t *boundary)
{
	const sg_configuration_cell_t *cell = &space->cells[cell_index];
	uint32_t offset;

	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_plane_t *candidate =
			&space->faces[cell->first_face + offset].plane;
		int same = Dot(candidate->normal, boundary->normal) > 0.0f &&
			CanonicalPlanesClose(candidate, boundary);

		if (same || SameBoundary(candidate, boundary))
			return candidate;
	}
	return NULL;
}

static int AuditPortalSideWitness(audit_context_t *audit, uint32_t cell_index,
	const sg_configuration_plane_t *boundary,
	const sg_rune_vec3_t *polygon_a, uint32_t count_a,
	const sg_rune_vec3_t *polygon_b, uint32_t count_b,
	const float center[3], float witness[3])
{
	const sg_configuration_cell_t *cell = &audit->space->cells[cell_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	sg_host_collision_pose_t pose;
	uint32_t offset, polygon_index, edge, axis, constraint_count;
	int result, positive_margin;

	halfspaces = calloc((size_t)cell->face_count + count_a + count_b,
		sizeof(*halfspaces));
	clearance = calloc((size_t)cell->face_count + count_a + count_b,
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
			&audit->space->faces[cell->first_face + offset].plane;

		CopyVector(halfspaces[offset].normal, plane->normal);
		halfspaces[offset].distance = plane->distance;
		halfspaces[offset].open = plane->source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH && plane->reversed != 0U;
		clearance[offset] = !(Dot(plane->normal, boundary->normal) > 0.0f &&
			CanonicalPlanesClose(plane, boundary));
	}
	constraint_count = cell->face_count;
	for (polygon_index = 0; polygon_index < 2U; polygon_index++)
	{
		const sg_rune_vec3_t *polygon = polygon_index ? polygon_b : polygon_a;
		uint32_t count = polygon_index ? count_b : count_a;

		for (edge = 0; edge < count; edge++)
		{
			const float *a = polygon[edge].value;
			const float *b = polygon[(edge + 1U) % count].value;
			float direction[3];
			sg_configuration_lattice_halfspace_t *side =
				&halfspaces[constraint_count];

			for (axis = 0; axis < 3; axis++)
				direction[axis] = b[axis] - a[axis];
			side->normal[0] = direction[1] * boundary->normal[2] -
				direction[2] * boundary->normal[1];
			side->normal[1] = direction[2] * boundary->normal[0] -
				direction[0] * boundary->normal[2];
			side->normal[2] = direction[0] * boundary->normal[1] -
				direction[1] * boundary->normal[0];
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
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, boundary->normal, point, &positive_margin, &stats);
	free(halfspaces);
	free(clearance);
	audit->result.lattice_solve_calls += stats.solve_calls;
	audit->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > audit->result.lattice_maximum_binary_shift)
		audit->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (result <= 0)
		return result;
	if (!positive_margin)
		return 0;
	for (axis = 0; axis < 3; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	return SG_HostCollisionClassifyPose(audit->authority, NULL, witness,
		cell->stance, &pose) && pose.valid ? 1 : -1;
}

typedef struct audit_face_ref_s
{
	uint32_t cell;
	uint32_t face;
	float mins[3];
	float maxs[3];
	float canonical_normal[3];
	float canonical_distance;
	float sweep_min;
	float sweep_max;
} audit_face_ref_t;

typedef struct audit_portal_ref_s
{
	uint32_t portal;
	uint32_t from;
	uint32_t to;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
} audit_portal_ref_t;

static int AuditFaceCompare(const sg_configuration_space_t *space,
	const audit_face_ref_t *left, const audit_face_ref_t *right)
{
	const sg_configuration_cell_t *ca = &space->cells[left->cell];
	const sg_configuration_cell_t *cb = &space->cells[right->cell];

#define AUDIT_COMPARE(a_value, b_value) do { \
	if ((a_value) < (b_value)) return -1; \
	if ((a_value) > (b_value)) return 1; } while (0)
	AUDIT_COMPARE(ca->stance, cb->stance);
	AUDIT_COMPARE(left->canonical_normal[0], right->canonical_normal[0]);
	AUDIT_COMPARE(left->canonical_normal[1], right->canonical_normal[1]);
	AUDIT_COMPARE(left->canonical_normal[2], right->canonical_normal[2]);
	AUDIT_COMPARE(left->canonical_distance, right->canonical_distance);
	AUDIT_COMPARE(left->sweep_min, right->sweep_min);
	AUDIT_COMPARE(left->sweep_max, right->sweep_max);
	AUDIT_COMPARE(left->cell, right->cell);
	AUDIT_COMPARE(left->face, right->face);
#undef AUDIT_COMPARE
	return 0;
}

static void AuditCanonicalPlane(const sg_configuration_plane_t *plane,
	float normal[3], float *distance)
{
	uint32_t axis, dominant = 2U;
	float scale;
	int flip;

	for (axis = 2U; axis-- > 0U; )
		if (fabsf(plane->normal[axis]) >= fabsf(plane->normal[dominant]))
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

static int SortAuditFaces(const sg_configuration_space_t *space,
	audit_face_ref_t *values, uint32_t count)
{
	audit_face_ref_t *temporary;
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
				if (right == end || (left < middle && AuditFaceCompare(space,
						&values[left], &values[right]) <= 0))
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

static int SameAuditFaceGroup(const sg_configuration_space_t *space,
	const audit_face_ref_t *left, const audit_face_ref_t *right)
{
	return space->cells[left->cell].stance == space->cells[right->cell].stance &&
		fabsf(left->canonical_normal[0] - right->canonical_normal[0]) <=
			AUDIT_GEOMETRY_EPSILON &&
		fabsf(left->canonical_normal[1] - right->canonical_normal[1]) <=
			AUDIT_GEOMETRY_EPSILON &&
		fabsf(left->canonical_normal[2] - right->canonical_normal[2]) <=
			AUDIT_GEOMETRY_EPSILON &&
		fabsf(left->canonical_distance - right->canonical_distance) <=
			AUDIT_GEOMETRY_EPSILON;
}

static int RefBoundsOverlap(const audit_face_ref_t *a,
	const audit_face_ref_t *b)
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		if (a->maxs[axis] < b->mins[axis] - AUDIT_EPSILON ||
			a->mins[axis] > b->maxs[axis] + AUDIT_EPSILON)
			return 0;
	return 1;
}

static int PortalRefCompare(const audit_portal_ref_t *a,
	const audit_portal_ref_t *b)
{
#define AUDIT_COMPARE(a_value, b_value) do { \
	if ((a_value) < (b_value)) return -1; \
	if ((a_value) > (b_value)) return 1; } while (0)
	AUDIT_COMPARE(a->from, b->from);
	AUDIT_COMPARE(a->to, b->to);
	AUDIT_COMPARE(a->source_kind, b->source_kind);
	AUDIT_COMPARE(a->source_index, b->source_index);
	AUDIT_COMPARE(a->source_variant, b->source_variant);
	AUDIT_COMPARE(a->portal, b->portal);
#undef AUDIT_COMPARE
	return 0;
}

static int SortPortalRefs(audit_portal_ref_t *values, uint32_t count)
{
	audit_portal_ref_t *temporary;
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
					PortalRefCompare(&values[left], &values[right]) <= 0))
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

static int FindPortalRef(const audit_portal_ref_t *references, uint32_t count,
	uint8_t *seen, uint32_t a, uint32_t b,
	const sg_configuration_plane_t *plane)
{
	audit_portal_ref_t key;
	uint32_t low = 0, high = count;

	memset(&key, 0, sizeof(key));
	key.from = a < b ? a : b;
	key.to = a < b ? b : a;
	key.source_kind = plane->source_kind;
	key.source_index = plane->source_index;
	key.source_variant = plane->source_variant;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		audit_portal_ref_t candidate = references[middle];

		candidate.portal = 0;
		if (PortalRefCompare(&candidate, &key) < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	while (low < count && references[low].from == key.from &&
		references[low].to == key.to &&
		references[low].source_kind == key.source_kind &&
		references[low].source_index == key.source_index &&
		references[low].source_variant == key.source_variant)
	{
		if (!seen[references[low].portal])
		{
			seen[references[low].portal] = 1;
			return 1;
		}
		low++;
	}
	return 0;
}

static int AuditPortals(audit_context_t *audit)
{
	uint8_t *seen = NULL;
	audit_face_ref_t *faces = NULL;
	audit_portal_ref_t *portals = NULL;
	uint32_t face_count = 0, face_index = 0, portal_index, cell;
	uint32_t group_start;
	int valid = 0;

	for (cell = 0; cell < audit->space->cell_count; cell++)
	{
		if (audit->space->cells[cell].face_count > UINT32_MAX - face_count)
			goto done;
		face_count += audit->space->cells[cell].face_count;
	}
	seen = calloc(audit->space->portal_count ? audit->space->portal_count : 1U,
		sizeof(*seen));
	faces = malloc((size_t)face_count * sizeof(*faces));
	portals = malloc((size_t)audit->space->portal_count * sizeof(*portals));
	if (!seen || (!faces && face_count) ||
		(!portals && audit->space->portal_count))
		goto done;
	for (cell = 0; cell < audit->space->cell_count; cell++)
	{
		uint32_t offset;

		for (offset = 0; offset < audit->space->cells[cell].face_count; offset++)
		{
			audit_face_ref_t *reference = &faces[face_index++];
			const sg_configuration_face_t *face;
			uint32_t vertex, axis, drop, sweep_axis;

			reference->cell = cell;
			reference->face = audit->space->cells[cell].first_face + offset;
			face = &audit->space->faces[reference->face];
			AuditCanonicalPlane(&face->plane, reference->canonical_normal,
				&reference->canonical_distance);
			for (axis = 0; axis < 3; axis++)
			{
				reference->mins[axis] = INFINITY;
				reference->maxs[axis] = -INFINITY;
			}
			for (vertex = 0; vertex < face->vertex_count; vertex++)
				for (axis = 0; axis < 3; axis++)
				{
					float value = audit->space->vertices[
						face->first_vertex + vertex].value[axis];

					if (value < reference->mins[axis])
						reference->mins[axis] = value;
					if (value > reference->maxs[axis])
						reference->maxs[axis] = value;
				}
			drop = DominantAxis(face->plane.normal);
			sweep_axis = (drop + 1U) % 3U;
			reference->sweep_min = reference->mins[sweep_axis];
			reference->sweep_max = reference->maxs[sweep_axis];
		}
	}
	for (portal_index = 0; portal_index < audit->space->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&audit->space->portals[portal_index];

		portals[portal_index].portal = portal_index;
		portals[portal_index].from = portal->from_cell;
		portals[portal_index].to = portal->to_cell;
		portals[portal_index].source_kind = portal->plane.source_kind;
		portals[portal_index].source_index = portal->plane.source_index;
		portals[portal_index].source_variant = portal->plane.source_variant;
	}
	if (!SortAuditFaces(audit->space, faces, face_count) ||
		!SortPortalRefs(portals, audit->space->portal_count))
		goto done;
	for (group_start = 0; group_start < face_count; )
	{
		uint32_t group_end = group_start + 1U;
		uint32_t left, right;

		while (group_end < face_count && SameAuditFaceGroup(audit->space,
			&faces[group_start], &faces[group_end]))
			group_end++;
		for (left = group_start; left < group_end; left++)
			for (right = left + 1U; right < group_end; right++)
			{
				const audit_face_ref_t *a = &faces[left];
				const audit_face_ref_t *b = &faces[right];
				const sg_configuration_face_t *face_a =
					&audit->space->faces[a->face];
				const sg_configuration_face_t *face_b =
					&audit->space->faces[b->face];
				const sg_configuration_cell_t *cell_a =
					&audit->space->cells[a->cell];
				float center[3], from[3], to[3];
				sg_host_collision_transition_t transition;
				int from_result, to_result;

				if (b->sweep_min > a->sweep_max + AUDIT_EPSILON)
					break;
				if (a->cell == b->cell || !RefBoundsOverlap(a, b) ||
					!SameBoundary(&face_a->plane, &face_b->plane) ||
					!FacesOverlapArea(audit->space, face_a, face_b, center))
					continue;
				from_result = AuditPortalSideWitness(audit, a->cell,
					&face_a->plane,
					&audit->space->vertices[face_a->first_vertex],
					face_a->vertex_count,
					&audit->space->vertices[face_b->first_vertex],
					face_b->vertex_count, center, from);
				to_result = AuditPortalSideWitness(audit, b->cell,
					&face_b->plane,
					&audit->space->vertices[face_a->first_vertex],
					face_a->vertex_count,
					&audit->space->vertices[face_b->first_vertex],
					face_b->vertex_count, center, to);
				if (from_result < 0 || to_result < 0)
					goto done;
				if (!from_result || !to_result ||
					!SG_HostCollisionTransition(audit->authority, NULL, from, to,
						cell_a->stance, &transition) || !transition.clear)
					continue;
				if (!FindPortalRef(portals, audit->space->portal_count, seen,
						a->cell, b->cell, &face_a->plane))
				{
					audit->result.omitted_portals++;
					goto done;
				}
			}
		group_start = group_end;
	}
	for (portal_index = 0; portal_index < audit->space->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&audit->space->portals[portal_index];
		float center[3] = { 0.0f, 0.0f, 0.0f };
		float from[3], to[3];
		sg_host_collision_transition_t transition;
		uint32_t vertex, axis;
		const sg_configuration_plane_t *from_plane, *to_plane;

		if (!seen[portal_index] ||
			portal->from_cell >= audit->space->cell_count ||
			portal->to_cell >= audit->space->cell_count ||
			portal->first_vertex > audit->space->vertex_count ||
			portal->vertex_count < 3U ||
			portal->vertex_count > audit->space->vertex_count -
				portal->first_vertex)
		{
			audit->result.invented_portals++;
			goto done;
		}
		for (vertex = 0; vertex < portal->vertex_count; vertex++)
			for (axis = 0; axis < 3; axis++)
				center[axis] += audit->space->vertices[
					portal->first_vertex + vertex].value[axis];
		for (axis = 0; axis < 3; axis++)
			center[axis] /= (float)portal->vertex_count;
		from_plane = CellBoundaryPlane(audit->space, portal->from_cell,
			&portal->plane);
		to_plane = CellBoundaryPlane(audit->space, portal->to_cell,
			&portal->plane);
		if (!from_plane || !to_plane ||
			AuditPortalSideWitness(audit, portal->from_cell, from_plane,
				&audit->space->vertices[portal->first_vertex],
				portal->vertex_count, NULL, 0, center, from) != 1 ||
			AuditPortalSideWitness(audit, portal->to_cell, to_plane,
				&audit->space->vertices[portal->first_vertex],
				portal->vertex_count, NULL, 0, center, to) != 1 ||
			!SG_HostCollisionTransition(audit->authority, NULL, from, to,
				portal->stance, &transition) || !transition.clear)
			goto done;
		audit->result.proved_portals++;
		audit->result.boundary_witnesses += 2U;
	}
	valid = 1;

done:
	free(seen);
	free(faces);
	free(portals);
	return valid;
}

static void AddCellHalfspaces(const sg_configuration_space_t *space,
	uint32_t cell_index, audit_halfspace_t *spaces, uint32_t *count)
{
	const sg_configuration_cell_t *cell = &space->cells[cell_index];
	uint32_t face;

	for (face = 0; face < cell->face_count; face++)
		spaces[(*count)++] = FromPlane(
			&space->faces[cell->first_face + face].plane);
}

static int CellsIntersectVolume(const sg_configuration_space_t *space,
	uint32_t a, uint32_t b)
{
	const sg_configuration_cell_t *cell_a = &space->cells[a];
	const sg_configuration_cell_t *cell_b = &space->cells[b];
	uint32_t count = cell_a->face_count + cell_b->face_count;
	audit_halfspace_t *spaces = malloc((size_t)count * sizeof(*spaces));
	float points[4][3];
	uint32_t point_count = 0;
	uint32_t first, second, third;
	int volume = 0;

	if (!spaces)
		return -1;
	count = 0;
	AddCellHalfspaces(space, a, spaces, &count);
	AddCellHalfspaces(space, b, spaces, &count);
	for (first = 0; first < count && !volume; first++)
		for (second = first + 1U; second < count && !volume; second++)
			for (third = second + 1U; third < count && !volume; third++)
			{
				float point[3];
				uint32_t index;
				int unique = 1;

				if (!IntersectThree(&spaces[first], &spaces[second],
						&spaces[third], point) || !PointInside(point, spaces, count))
					continue;
				for (index = 0; index < point_count; index++)
					if (fabsf(point[0] - points[index][0]) <=
							AUDIT_GEOMETRY_EPSILON &&
						fabsf(point[1] - points[index][1]) <=
							AUDIT_GEOMETRY_EPSILON &&
						fabsf(point[2] - points[index][2]) <=
							AUDIT_GEOMETRY_EPSILON)
						unique = 0;
				if (!unique)
					continue;
				if (point_count < 4U)
					CopyVector(points[point_count++], point);
				if (point_count == 4U)
				{
					float x[3], y[3], z[3];
					uint32_t axis;

					for (axis = 0; axis < 3; axis++)
					{
						x[axis] = points[1][axis] - points[0][axis];
						y[axis] = points[2][axis] - points[0][axis];
						z[axis] = points[3][axis] - points[0][axis];
					}
					volume = fabsf(Determinant(x, y, z)) >
						AUDIT_GEOMETRY_EPSILON;
					if (!volume)
						point_count = 3U;
				}
			}
	free(spaces);
	return volume;
}

typedef struct audit_cell_ref_s
{
	uint32_t cell;
	float minimum;
	float maximum;
} audit_cell_ref_t;

typedef struct audit_overlap_ref_s
{
	uint32_t overlap;
	uint32_t standing;
	uint32_t crouching;
} audit_overlap_ref_t;

static int AuditCellCompare(const sg_configuration_space_t *space,
	const audit_cell_ref_t *a, const audit_cell_ref_t *b)
{
	const sg_configuration_cell_t *ca = &space->cells[a->cell];
	const sg_configuration_cell_t *cb = &space->cells[b->cell];

#define AUDIT_COMPARE(a_value, b_value) do { \
	if ((a_value) < (b_value)) return -1; \
	if ((a_value) > (b_value)) return 1; } while (0)
	AUDIT_COMPARE(ca->bsp_leaf.index, cb->bsp_leaf.index);
	AUDIT_COMPARE(ca->stance, cb->stance);
	AUDIT_COMPARE(a->minimum, b->minimum);
	AUDIT_COMPARE(a->maximum, b->maximum);
	AUDIT_COMPARE(a->cell, b->cell);
#undef AUDIT_COMPARE
	return 0;
}

static int SortAuditCells(const sg_configuration_space_t *space,
	audit_cell_ref_t *values, uint32_t count)
{
	audit_cell_ref_t *temporary;
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
				if (right == end || (left < middle && AuditCellCompare(space,
						&values[left], &values[right]) <= 0))
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

static int OverlapRefCompare(const audit_overlap_ref_t *a,
	const audit_overlap_ref_t *b)
{
	if (a->standing != b->standing)
		return a->standing < b->standing ? -1 : 1;
	if (a->crouching != b->crouching)
		return a->crouching < b->crouching ? -1 : 1;
	if (a->overlap != b->overlap)
		return a->overlap < b->overlap ? -1 : 1;
	return 0;
}

static int SortOverlapRefs(audit_overlap_ref_t *values, uint32_t count)
{
	audit_overlap_ref_t *temporary;
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
					OverlapRefCompare(&values[left], &values[right]) <= 0))
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

static int FindOverlapRef(const audit_overlap_ref_t *references,
	uint32_t count, uint8_t *seen, uint32_t standing, uint32_t crouching)
{
	uint32_t low = 0, high = count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		const audit_overlap_ref_t *value = &references[middle];

		if (value->standing < standing ||
			(value->standing == standing && value->crouching < crouching))
			low = middle + 1U;
		else
			high = middle;
	}
	while (low < count && references[low].standing == standing &&
		references[low].crouching == crouching)
	{
		if (!seen[references[low].overlap])
		{
			seen[references[low].overlap] = 1;
			return 1;
		}
		low++;
	}
	return 0;
}

static int AuditStanceOverlaps(audit_context_t *audit)
{
	uint8_t *seen = NULL, *covered = NULL;
	audit_cell_ref_t *cells = NULL;
	audit_overlap_ref_t *overlaps = NULL;
	uint32_t index, group_start;
	int valid = 0;

	seen = calloc(audit->space->stance_overlap_count ?
		audit->space->stance_overlap_count : 1U, sizeof(*seen));
	covered = calloc(audit->space->cell_count ? audit->space->cell_count : 1U,
		sizeof(*covered));
	cells = malloc((size_t)audit->space->cell_count * sizeof(*cells));
	overlaps = malloc((size_t)audit->space->stance_overlap_count *
		sizeof(*overlaps));
	if (!seen || !covered || (!cells && audit->space->cell_count) ||
		(!overlaps && audit->space->stance_overlap_count))
		goto done;
	for (index = 0; index < audit->space->cell_count; index++)
	{
		cells[index].cell = index;
		cells[index].minimum = audit->space->cells[index].bounds.mins.value[0];
		cells[index].maximum = audit->space->cells[index].bounds.maxs.value[0];
	}
	for (index = 0; index < audit->space->stance_overlap_count; index++)
	{
		overlaps[index].overlap = index;
		overlaps[index].standing =
			audit->space->stance_overlaps[index].standing_cell;
		overlaps[index].crouching =
			audit->space->stance_overlaps[index].crouching_cell;
	}
	if (!SortAuditCells(audit->space, cells, audit->space->cell_count) ||
		!SortOverlapRefs(overlaps, audit->space->stance_overlap_count))
		goto done;
	for (group_start = 0; group_start < audit->space->cell_count; )
	{
		uint32_t group_end = group_start + 1U;
		uint32_t crouching_start, standing_ref, crouching_ref;

		while (group_end < audit->space->cell_count &&
			audit->space->cells[cells[group_end].cell].bsp_leaf.index ==
				audit->space->cells[cells[group_start].cell].bsp_leaf.index)
			group_end++;
		crouching_start = group_start;
		while (crouching_start < group_end && audit->space->cells[
			cells[crouching_start].cell].stance == SG_RUNE_STANCE_STANDING)
			crouching_start++;
		for (standing_ref = group_start; standing_ref < crouching_start;
			standing_ref++)
			for (crouching_ref = crouching_start; crouching_ref < group_end;
				crouching_ref++)
			{
				uint32_t standing = cells[standing_ref].cell;
				uint32_t crouching = cells[crouching_ref].cell;
				const sg_configuration_cell_t *a = &audit->space->cells[standing];
				const sg_configuration_cell_t *b = &audit->space->cells[crouching];
				int intersects;
				uint32_t axis;
				int bounds_overlap = 1;

				if (cells[crouching_ref].minimum >
					cells[standing_ref].maximum + AUDIT_EPSILON)
					break;
				if (cells[crouching_ref].maximum <
					cells[standing_ref].minimum - AUDIT_EPSILON)
					continue;
				for (axis = 0; axis < 3; axis++)
					if (a->bounds.maxs.value[axis] < b->bounds.mins.value[axis] -
							AUDIT_EPSILON ||
						a->bounds.mins.value[axis] > b->bounds.maxs.value[axis] +
							AUDIT_EPSILON)
						bounds_overlap = 0;
				if (!bounds_overlap)
					continue;
				intersects = CellsIntersectVolume(audit->space, standing, crouching);
				if (intersects < 0)
					goto done;
				if (!intersects)
					continue;
				if (!FindOverlapRef(overlaps,
						audit->space->stance_overlap_count, seen, standing,
						crouching))
					goto done;
				covered[standing] = 1;
			}
		group_start = group_end;
	}
	for (index = 0; index < audit->space->cell_count; index++)
		if (audit->space->cells[index].stance == SG_RUNE_STANCE_STANDING &&
			!covered[index])
			goto done;
	for (index = 0; index < audit->space->stance_overlap_count; index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&audit->space->stance_overlaps[index];
		sg_host_collision_pose_t pose;

		if (!seen[index] ||
			!SG_HostCollisionClassifyPose(audit->authority, NULL,
				overlap->interior_witness.value, SG_RUNE_STANCE_STANDING,
				&pose) || !pose.valid ||
			!SG_HostCollisionClassifyPose(audit->authority, NULL,
				overlap->interior_witness.value, SG_RUNE_STANCE_CROUCHING,
				&pose) || !pose.valid)
			goto done;
		audit->result.boundary_witnesses += 2U;
	}
	valid = 1;

done:
	free(seen);
	free(covered);
	free(cells);
	free(overlaps);
	return valid;
}

static int AddDomainHalfspaces(const sg_rune_bounds_t *bounds,
	audit_halfspace_t spaces[6])
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
	{
		memset(&spaces[axis * 2U], 0, sizeof(spaces[axis * 2U]));
		spaces[axis * 2U].normal[axis] = 1.0f;
		spaces[axis * 2U].distance = bounds->maxs.value[axis];
		spaces[axis * 2U].source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
		spaces[axis * 2U].source_index = axis;
		spaces[axis * 2U].source_variant = axis * 2U;
		memset(&spaces[axis * 2U + 1U], 0,
			sizeof(spaces[axis * 2U + 1U]));
		spaces[axis * 2U + 1U].normal[axis] = -1.0f;
		spaces[axis * 2U + 1U].distance = -bounds->mins.value[axis];
		spaces[axis * 2U + 1U].source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
		spaces[axis * 2U + 1U].source_index = axis;
		spaces[axis * 2U + 1U].source_variant = axis * 2U + 1U;
		spaces[axis * 2U + 1U].reversed = 1U;
	}
	return 1;
}

int SG_ConfigurationAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_configuration_audit_result_t *result_out)
{
	audit_context_t audit;
	audit_halfspace_t domain[6];
	uint32_t stance, index;
	int success = 0;

	memset(&audit, 0, sizeof(audit));
	audit.result.record = SG_CONFIGURATION_INDEX_NONE;
	if (!result_out || !authority || !space || authority->world == NULL ||
		space->identity.bsp_content_id != authority->identity.bsp_content_id ||
		space->identity.physics_abi_id != authority->identity.physics_abi_id ||
		space->certificate_node_count == 0U)
	{
		if (result_out)
		{
			memset(result_out, 0, sizeof(*result_out));
			result_out->code = SG_CONFIGURATION_AUDIT_INVALID_ARGUMENT;
			result_out->record = SG_CONFIGURATION_INDEX_NONE;
		}
		return 0;
	}
	audit.authority = authority;
	audit.space = space;
	audit.seen_nodes = calloc(space->certificate_node_count,
		sizeof(*audit.seen_nodes));
	audit.seen_cells = calloc(space->cell_count ? space->cell_count : 1U,
		sizeof(*audit.seen_cells));
	if (!audit.seen_nodes || !audit.seen_cells)
	{
		Fail(&audit, SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY, 0);
		goto done;
	}
	AddDomainHalfspaces(&space->domain, domain);
	for (stance = 0; stance < SG_RUNE_STANCE_COUNT; stance++)
		if (!AuditCertificateNode(&audit, space->certificate_roots[stance],
				(sg_rune_stance_t)stance, SG_CONFIGURATION_INDEX_NONE,
				domain, 6))
		{
			Fail(&audit, SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE, stance);
			goto done;
		}
	for (index = 0; index < space->cell_count; index++)
		if (!audit.seen_cells[index])
		{
			audit.result.omitted_cells++;
			Fail(&audit, SG_CONFIGURATION_AUDIT_OMITTED_CELL, index);
			goto done;
		}
	for (index = 0; index < space->certificate_node_count; index++)
		if (!audit.seen_nodes[index])
		{
			Fail(&audit, SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE, index);
			goto done;
		}
	if (!AuditHostCells(&audit))
	{
		Fail(&audit, SG_CONFIGURATION_AUDIT_HOST_CELL_DISAGREEMENT, 0);
		goto done;
	}
	if (!AuditPortals(&audit))
	{
		Fail(&audit, audit.result.omitted_portals ?
			SG_CONFIGURATION_AUDIT_OMITTED_PORTAL :
			(audit.result.invented_portals ?
			 SG_CONFIGURATION_AUDIT_INVENTED_PORTAL :
			 SG_CONFIGURATION_AUDIT_HOST_PORTAL_DISAGREEMENT), 0);
		goto done;
	}
	if (!AuditStanceOverlaps(&audit))
	{
		Fail(&audit, SG_CONFIGURATION_AUDIT_INVALID_CELL, 0);
		goto done;
	}
	success = 1;

done:
	free(audit.seen_nodes);
	free(audit.seen_cells);
	*result_out = audit.result;
	return success;
}

const char *SG_ConfigurationAuditCodeString(sg_configuration_audit_code_t code)
{
	switch (code)
	{
	case SG_CONFIGURATION_AUDIT_OK: return "ok";
	case SG_CONFIGURATION_AUDIT_INVALID_ARGUMENT: return "invalid argument";
	case SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE:
		return "invalid completeness certificate";
	case SG_CONFIGURATION_AUDIT_OMITTED_CELL: return "omitted valid cell";
	case SG_CONFIGURATION_AUDIT_INVALID_CELL: return "invalid cell";
	case SG_CONFIGURATION_AUDIT_OMITTED_PORTAL: return "omitted valid portal";
	case SG_CONFIGURATION_AUDIT_INVENTED_PORTAL: return "invented portal";
	case SG_CONFIGURATION_AUDIT_HOST_CELL_DISAGREEMENT:
		return "host cell disagreement";
	case SG_CONFIGURATION_AUDIT_HOST_PORTAL_DISAGREEMENT:
		return "host portal disagreement";
	case SG_CONFIGURATION_AUDIT_OVERFLOW: return "audit overflow";
	case SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY: return "out of memory";
	default: return "unknown audit result";
	}
}
