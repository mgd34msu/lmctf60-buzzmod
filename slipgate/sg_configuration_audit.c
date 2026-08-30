#include "sg_configuration_audit.h"
#include "sg_configuration_lattice.h"

#include <float.h>
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
		face->distance != space->distance)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (face->normal[axis] != space->normal[axis])
			return 0;
	return 1;
}

static int CellImpliesConstraintQ8(audit_context_t *audit,
	const sg_configuration_cell_t *cell, const audit_halfspace_t *constraint)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t face, axis;
	int result;

	if (cell->face_count == UINT32_MAX)
		return -1;
#if SIZE_MAX == UINT32_MAX
	if ((size_t)(cell->face_count + 1U) > SIZE_MAX / sizeof(*halfspaces))
		return -1;
#endif
	halfspaces = malloc((size_t)(cell->face_count + 1U) *
		sizeof(*halfspaces));
	if (!halfspaces)
		return -1;
	for (face = 0U; face < cell->face_count; face++)
	{
		const sg_configuration_plane_t *plane = &audit->space->faces[
			cell->first_face + face].plane;
		audit_halfspace_t source = FromPlane(plane);

		CopyVector(halfspaces[face].normal, plane->normal);
		halfspaces[face].distance = plane->distance;
		halfspaces[face].open = HalfspaceIsOpen(&source);
	}
	for (axis = 0U; axis < 3U; axis++)
		halfspaces[cell->face_count].normal[axis] = -constraint->normal[axis];
	halfspaces[cell->face_count].distance = -constraint->distance;
	halfspaces[cell->face_count].open = !HalfspaceIsOpen(constraint);
	result = SG_ConfigurationLatticeFind(halfspaces, cell->face_count + 1U,
		NULL, point, &stats);
	free(halfspaces);
	audit->result.lattice_solve_calls += stats.solve_calls;
	audit->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > audit->result.lattice_maximum_binary_shift)
		audit->result.lattice_maximum_binary_shift =
			stats.maximum_binary_shift;
	return result < 0 ? -1 : result == 0;
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

		if (face->kind > SG_CONFIGURATION_FACE_CONSTRAINT_ONLY ||
			(face->kind == SG_CONFIGURATION_FACE_FACET &&
				face->vertex_count < 3U) ||
			(face->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY &&
				face->vertex_count != 0U) ||
			face->first_vertex > configuration->vertex_count ||
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
	for (constraint = 0U; constraint < space_count; constraint++)
	{
		int found = 0;
		int implied;

		for (face_offset = 0U; face_offset < cell->face_count; face_offset++)
			if (PlaneMatches(&configuration->faces[
					cell->first_face + face_offset].plane, &spaces[constraint]))
			{
				found = 1;
				break;
			}
		if (found)
			continue;
		implied = CellImpliesConstraintQ8(audit, cell, &spaces[constraint]);
		if (implied <= 0)
		{
			Fail(audit, implied < 0 ? SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY :
				SG_CONFIGURATION_AUDIT_INVALID_CELL, cell_index);
			return 0;
		}
	}
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

static int AuditCellGeometry(audit_context_t *audit, uint32_t cell_index);

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
		int geometry;

		geometry = AuditCellGeometry(audit, cell_index);
		if (geometry <= 0)
		{
			Fail(audit, geometry < 0 ? SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY :
				SG_CONFIGURATION_AUDIT_INVALID_CELL, cell_index);
			return 0;
		}

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

			if (face->kind != SG_CONFIGURATION_FACE_FACET)
				continue;
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
	return (double)a->normal[0] * b->normal[0] +
		(double)a->normal[1] * b->normal[1] +
		(double)a->normal[2] * b->normal[2] < 0.0 &&
		CanonicalPlanesClose(a, b);
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
		int same = ((double)candidate->normal[0] * boundary->normal[0] +
			(double)candidate->normal[1] * boundary->normal[1] +
			(double)candidate->normal[2] * boundary->normal[2]) > 0.0 &&
			CanonicalPlanesClose(candidate, boundary);

		if (same || SameBoundary(candidate, boundary))
			return candidate;
	}
	return NULL;
}

static int AuditPortalSideWitness(audit_context_t *audit, uint32_t cell_index,
	uint32_t other_cell_index, const sg_configuration_plane_t *boundary,
	float witness[3])
{
	const sg_configuration_cell_t *cell = &audit->space->cells[cell_index];
	const sg_configuration_cell_t *other =
		&audit->space->cells[other_cell_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	const sg_configuration_plane_t **planes;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3], clearance_point[3];
	sg_host_collision_pose_t pose;
	uint32_t offset, axis, constraint_count;
	int result, positive_margin;
	int classified;

	if (cell->face_count > UINT32_MAX - other->face_count)
	{
		Fail(audit, SG_CONFIGURATION_AUDIT_OVERFLOW, cell_index);
		return -1;
	}
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
		Fail(audit, SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY, cell_index);
		return -1;
	}
	constraint_count = 0U;
	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_plane_t *plane =
			&audit->space->faces[cell->first_face + offset].plane;
		uint32_t existing;

		for (existing = 0U; existing < constraint_count; existing++)
			if (CanonicalPlanesClose(planes[existing], plane) &&
				((double)planes[existing]->normal[0] * plane->normal[0] +
				(double)planes[existing]->normal[1] * plane->normal[1] +
				(double)planes[existing]->normal[2] * plane->normal[2]) > 0.0)
			{
				if (CanonicalPlanesClose(plane, boundary))
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
			(uint8_t)!CanonicalPlanesClose(plane, boundary);
	}
	for (offset = 0; offset < other->face_count; offset++)
	{
		const sg_configuration_plane_t *plane = &audit->space->faces[
			other->first_face + offset].plane;
		uint32_t existing;

		if (CanonicalPlanesClose(plane, boundary))
			continue;
		for (existing = 0U; existing < constraint_count; existing++)
			if (CanonicalPlanesClose(planes[existing], plane) &&
				((double)planes[existing]->normal[0] * plane->normal[0] +
				(double)planes[existing]->normal[1] * plane->normal[1] +
				(double)planes[existing]->normal[2] * plane->normal[2]) > 0.0)
				break;
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
			boundary->normal, point, &stats);
	}
	free(halfspaces);
	free(planes);
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
	classified = SG_HostCollisionClassifyPose(audit->authority, NULL, witness,
		cell->stance, &pose);
	if (!classified)
		return -1;
	if (pose.valid)
		return 1;
	for (axis = 0; axis < 3; axis++)
		witness[axis] = (float)clearance_point[axis] * 0.125f;
	classified = SG_HostCollisionClassifyPose(audit->authority, NULL, witness,
		cell->stance, &pose);
	return classified && pose.valid ? 1 : -1;
}

typedef struct audit_face_ref_s
{
	uint32_t cell;
	uint32_t face;
	float canonical_normal[3];
	float canonical_distance;
	int64_t bin[4];
} audit_face_ref_t;

typedef struct audit_portal_ref_s
{
	uint32_t portal;
	uint32_t from;
	uint32_t to;
	float canonical_normal[3];
	float canonical_distance;
	int64_t bin[4];
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
	AUDIT_COMPARE(left->bin[0], right->bin[0]);
	AUDIT_COMPARE(left->bin[1], right->bin[1]);
	AUDIT_COMPARE(left->bin[2], right->bin[2]);
	AUDIT_COMPARE(left->bin[3], right->bin[3]);
	AUDIT_COMPARE(left->cell, right->cell);
	AUDIT_COMPARE(left->face, right->face);
#undef AUDIT_COMPARE
	return 0;
}

static int AuditPlaneBins(const float normal[3], float distance,
	int64_t bin[4])
{
	const float values[4] = { normal[0], normal[1], normal[2], distance };
	uint32_t component;

	for (component = 0U; component < 4U; component++)
	{
		double value = floor((double)values[component] /
			(2.0 * (double)AUDIT_GEOMETRY_EPSILON));

		if (!isfinite(value) || value < (double)INT64_MIN ||
			value > (double)INT64_MAX)
			return 0;
		bin[component] = (int64_t)value;
	}
	return 1;
}

static int AuditFaceBins(audit_face_ref_t *face)
{
	return AuditPlaneBins(face->canonical_normal, face->canonical_distance,
		face->bin);
}

static int AuditFaceBinKeyCompare(const sg_configuration_space_t *space,
	const audit_face_ref_t *face, sg_rune_stance_t stance,
	const int64_t bin[4])
{
	uint32_t component;
	const sg_rune_stance_t face_stance = space->cells[face->cell].stance;

	if (face_stance != stance)
		return face_stance < stance ? -1 : 1;
	for (component = 0U; component < 4U; component++)
		if (face->bin[component] != bin[component])
			return face->bin[component] < bin[component] ? -1 : 1;
	return 0;
}

static uint32_t AuditFaceBinLowerBound(const sg_configuration_space_t *space,
	const audit_face_ref_t *faces, uint32_t count, sg_rune_stance_t stance,
	const int64_t bin[4])
{
	uint32_t low = 0U, high = count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (AuditFaceBinKeyCompare(space, &faces[middle], stance, bin) < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	return low;
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

static int PortalRefCompare(const audit_portal_ref_t *a,
	const audit_portal_ref_t *b)
{
#define AUDIT_COMPARE(a_value, b_value) do { \
	if ((a_value) < (b_value)) return -1; \
	if ((a_value) > (b_value)) return 1; } while (0)
	AUDIT_COMPARE(a->from, b->from);
	AUDIT_COMPARE(a->to, b->to);
	AUDIT_COMPARE(a->bin[0], b->bin[0]);
	AUDIT_COMPARE(a->bin[1], b->bin[1]);
	AUDIT_COMPARE(a->bin[2], b->bin[2]);
	AUDIT_COMPARE(a->bin[3], b->bin[3]);
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

static int OffsetAuditBin(int64_t value, int delta, int64_t *result)
{
	if ((delta < 0 && value == INT64_MIN) ||
		(delta > 0 && value == INT64_MAX))
		return 0;
	*result = value + (int64_t)delta;
	return 1;
}

static int FindPortalRef(const audit_portal_ref_t *references, uint32_t count,
	uint8_t *seen,
	uint32_t a, uint32_t b,
	const sg_configuration_plane_t *plane)
{
	audit_portal_ref_t query;
	uint32_t from = a < b ? a : b;
	uint32_t to = a < b ? b : a;
	int d0, d1, d2, d3;

	memset(&query, 0, sizeof(query));
	query.from = from;
	query.to = to;
	AuditCanonicalPlane(plane, query.canonical_normal,
		&query.canonical_distance);
	if (!AuditPlaneBins(query.canonical_normal, query.canonical_distance,
			query.bin))
		return 0;
	for (d0 = -1; d0 <= 1; d0++)
		for (d1 = -1; d1 <= 1; d1++)
			for (d2 = -1; d2 <= 1; d2++)
				for (d3 = -1; d3 <= 1; d3++)
				{
					audit_portal_ref_t key = query;
					uint32_t low = 0U, high = count;

					if (!OffsetAuditBin(query.bin[0], d0, &key.bin[0]) ||
						!OffsetAuditBin(query.bin[1], d1, &key.bin[1]) ||
						!OffsetAuditBin(query.bin[2], d2, &key.bin[2]) ||
						!OffsetAuditBin(query.bin[3], d3, &key.bin[3]))
						continue;
					while (low < high)
					{
						uint32_t middle = low + (high - low) / 2U;
						audit_portal_ref_t candidate = references[middle];

						candidate.portal = 0U;
						if (PortalRefCompare(&candidate, &key) < 0)
							low = middle + 1U;
						else
							high = middle;
					}
					while (low < count && references[low].from == from &&
						references[low].to == to &&
						memcmp(references[low].bin, key.bin,
							sizeof(key.bin)) == 0)
					{
						const audit_portal_ref_t *candidate = &references[low++];

						if (fabsf(candidate->canonical_normal[0] -
								query.canonical_normal[0]) <=
								AUDIT_GEOMETRY_EPSILON &&
							fabsf(candidate->canonical_normal[1] -
								query.canonical_normal[1]) <=
								AUDIT_GEOMETRY_EPSILON &&
							fabsf(candidate->canonical_normal[2] -
								query.canonical_normal[2]) <=
								AUDIT_GEOMETRY_EPSILON &&
							fabsf(candidate->canonical_distance -
								query.canonical_distance) <=
								AUDIT_GEOMETRY_EPSILON)
						{
							seen[candidate->portal] = 1U;
							return 1;
						}
					}
				}
	return 0;
}

static int AuditPortalGeometry(const sg_configuration_space_t *space,
	const sg_configuration_portal_t *portal, float *area_out)
{
	const sg_configuration_cell_t *cells[2] = {
		&space->cells[portal->from_cell], &space->cells[portal->to_cell]
	};
	uint32_t drop = DominantAxis(portal->plane.normal);
	uint32_t u = (drop + 1U) % 3U;
	uint32_t v = (drop + 2U) % 3U;
	uint32_t vertex, cell, face;
	const float *origin;
	double area2 = 0.0;
	double portal_length = hypot(hypot((double)portal->plane.normal[0],
		(double)portal->plane.normal[1]),
		(double)portal->plane.normal[2]);

	if (!(portal_length > 0.0) ||
		!(fabs((double)portal->plane.normal[drop]) / portal_length > 0.0))
		return 0;
	origin = space->vertices[portal->first_vertex].value;
	for (vertex = 0; vertex < portal->vertex_count; vertex++)
	{
		const float *point = space->vertices[
			portal->first_vertex + vertex].value;
		const float *next = space->vertices[portal->first_vertex +
			(vertex + 1U) % portal->vertex_count].value;

		if (!FiniteVector(point) || fabs(((double)point[0] *
				portal->plane.normal[0] + (double)point[1] *
				portal->plane.normal[1] + (double)point[2] *
				portal->plane.normal[2] - portal->plane.distance) / portal_length) >
				AUDIT_EPSILON)
			return 0;
		for (cell = 0; cell < 2U; cell++)
			for (face = 0; face < cells[cell]->face_count; face++)
			{
				const sg_configuration_plane_t *plane = &space->faces[
					cells[cell]->first_face + face].plane;
				double length = hypot(hypot((double)plane->normal[0],
					(double)plane->normal[1]), (double)plane->normal[2]);

				if (!(length > 0.0) || ((double)point[0] * plane->normal[0] +
						(double)point[1] * plane->normal[1] +
						(double)point[2] * plane->normal[2] - plane->distance) /
						length > AUDIT_EPSILON)
					return 0;
			}
		area2 += ((double)point[u] - origin[u]) *
			((double)next[v] - origin[v]) -
			((double)next[u] - origin[u]) *
			((double)point[v] - origin[v]);
	}
	*area_out = (float)(fabs(area2) * 0.5 * portal_length /
		fabs((double)portal->plane.normal[drop]));
	return isfinite(*area_out) && *area_out > AUDIT_AREA_EPSILON;
}

typedef struct audit_boundary_point_s
{
	double value[3];
	double angle;
} audit_boundary_point_t;

typedef struct audit_double_halfspace_s
{
	double normal[3];
	double distance;
} audit_double_halfspace_t;

typedef enum audit_geometry_result_e
{
	AUDIT_GEOMETRY_OUT_OF_MEMORY = -2,
	AUDIT_GEOMETRY_INVALID = -1,
	AUDIT_GEOMETRY_EMPTY = 0,
	AUDIT_GEOMETRY_POLYGON = 1
} audit_geometry_result_t;

static double AuditDeterminantDouble(const double a[3], const double b[3],
	const double c[3])
{
	return a[0] * (b[1] * c[2] - b[2] * c[1]) -
		a[1] * (b[0] * c[2] - b[2] * c[0]) +
		a[2] * (b[0] * c[1] - b[1] * c[0]);
}

static int NormalizeAuditHalfspace(const sg_configuration_plane_t *plane,
	audit_double_halfspace_t *result)
{
	double scale = fmax(fabs((double)plane->normal[0]),
		fmax(fabs((double)plane->normal[1]),
			fabs((double)plane->normal[2])));
	double length;
	uint32_t axis;

	if (!(scale > 0.0) || !isfinite(scale) || !isfinite(plane->distance))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		result->normal[axis] = (double)plane->normal[axis] / scale;
	length = sqrt(result->normal[0] * result->normal[0] +
		result->normal[1] * result->normal[1] +
		result->normal[2] * result->normal[2]);
	if (!(length > 0.0) || !isfinite(length))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		result->normal[axis] /= length;
	result->distance = ((double)plane->distance / scale) / length;
	return isfinite(result->normal[0]) && isfinite(result->normal[1]) &&
		isfinite(result->normal[2]) && isfinite(result->distance);
}

static int IntersectThreeDouble(const audit_double_halfspace_t *a,
	const audit_double_halfspace_t *b, const audit_double_halfspace_t *c,
	double point[3])
{
	double na[3], nb[3], nc[3], distances[3];
	double column0[3], column1[3], column2[3];
	double determinant;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		na[axis] = a->normal[axis];
		nb[axis] = b->normal[axis];
		nc[axis] = c->normal[axis];
	}
	determinant = AuditDeterminantDouble(na, nb, nc);
	{
		double scale = fabs(na[0] * (nb[1] * nc[2] - nb[2] * nc[1])) +
			fabs(na[1] * (nb[2] * nc[0] - nb[0] * nc[2])) +
			fabs(na[2] * (nb[0] * nc[1] - nb[1] * nc[0]));

		if (fabs(determinant) <= DBL_EPSILON * fmax(1.0, scale))
			return 0;
	}
	distances[0] = a->distance;
	distances[1] = b->distance;
	distances[2] = c->distance;
	column0[0] = distances[0];
	column0[1] = distances[1];
	column0[2] = distances[2];
	column1[0] = na[1];
	column1[1] = nb[1];
	column1[2] = nc[1];
	column2[0] = na[2];
	column2[1] = nb[2];
	column2[2] = nc[2];
	point[0] = AuditDeterminantDouble(column0, column1, column2) / determinant;
	column0[0] = na[0];
	column0[1] = nb[0];
	column0[2] = nc[0];
	column1[0] = distances[0];
	column1[1] = distances[1];
	column1[2] = distances[2];
	point[1] = AuditDeterminantDouble(column0, column1, column2) / determinant;
	column1[0] = na[1];
	column1[1] = nb[1];
	column1[2] = nc[1];
	column2[0] = distances[0];
	column2[1] = distances[1];
	column2[2] = distances[2];
	point[2] = AuditDeterminantDouble(column0, column1, column2) / determinant;
	return isfinite(point[0]) && isfinite(point[1]) && isfinite(point[2]);
}

static int AuditPointInsideClosure(const double point[3],
	const audit_double_halfspace_t *spaces, uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		if (point[0] * spaces[index].normal[0] +
			point[1] * spaces[index].normal[1] +
			point[2] * spaces[index].normal[2] - spaces[index].distance >
			(double)AUDIT_GEOMETRY_EPSILON)
			return 0;
	return 1;
}

static float AuditPortalCoordinateTolerance(double value);

static int AppendAuditBoundaryPoint(audit_boundary_point_t **points,
	uint32_t *count, uint32_t *capacity, const double point[3])
{
	audit_boundary_point_t *grown;
	uint32_t index, next;

	for (index = 0U; index < *count; index++)
		if (fabs((double)(float)point[0] -
				(double)(float)(*points)[index].value[0]) <=
				(double)fmaxf(AuditPortalCoordinateTolerance(point[0]),
					AuditPortalCoordinateTolerance((*points)[index].value[0])) &&
			fabs((double)(float)point[1] -
				(double)(float)(*points)[index].value[1]) <=
				(double)fmaxf(AuditPortalCoordinateTolerance(point[1]),
					AuditPortalCoordinateTolerance((*points)[index].value[1])) &&
			fabs((double)(float)point[2] -
				(double)(float)(*points)[index].value[2]) <=
				(double)fmaxf(AuditPortalCoordinateTolerance(point[2]),
					AuditPortalCoordinateTolerance((*points)[index].value[2])))
			return 1;
	if (*count == UINT32_MAX)
		return 0;
	if (*count == *capacity)
	{
		next = *capacity ? *capacity : 8U;
		if (next <= *count)
		{
			if (next > UINT32_MAX / 2U)
				next = UINT32_MAX;
			else
				next *= 2U;
		}
		if (next <= *count)
			return 0;
#if SIZE_MAX == UINT32_MAX
		if ((size_t)next > SIZE_MAX / sizeof(*grown))
			return 0;
#endif
		grown = realloc(*points, (size_t)next * sizeof(*grown));
		if (!grown)
			return 0;
		*points = grown;
		*capacity = next;
	}
	memcpy((*points)[*count].value, point, 3U * sizeof(*point));
	(*count)++;
	return 1;
}

static int AuditAuthoritativeBoundaryGeometry(
	const sg_configuration_space_t *space,
	uint32_t a, uint32_t b, const sg_configuration_plane_t *boundary,
	audit_boundary_point_t **points_out, uint32_t *point_count_out,
	float *area_out)
{
	const sg_configuration_cell_t *cells[2] = { &space->cells[a],
		&space->cells[b] };
	uint32_t count, cell, face, first, second, point_count = 0U;
	uint32_t point_capacity = 0U;
	audit_double_halfspace_t *halfspaces = NULL, boundary_space;
	audit_boundary_point_t *points = NULL;
	uint32_t drop, u, v, index;
	double center_u = 0.0, center_v = 0.0, area2 = 0.0;
	int result = AUDIT_GEOMETRY_INVALID;

	*points_out = NULL;
	*point_count_out = 0U;
	*area_out = 0.0f;
	if (cells[0]->face_count > UINT32_MAX - cells[1]->face_count)
		return AUDIT_GEOMETRY_OUT_OF_MEMORY;
	if (!NormalizeAuditHalfspace(boundary, &boundary_space))
		return AUDIT_GEOMETRY_INVALID;
	count = cells[0]->face_count + cells[1]->face_count;
#if SIZE_MAX == UINT32_MAX
	if ((size_t)count > SIZE_MAX / sizeof(*halfspaces))
		return AUDIT_GEOMETRY_OUT_OF_MEMORY;
#endif
	halfspaces = malloc((size_t)count * sizeof(*halfspaces));
	if (!halfspaces && count)
	{
		result = AUDIT_GEOMETRY_OUT_OF_MEMORY;
		goto done;
	}
	count = 0U;
	for (cell = 0U; cell < 2U; cell++)
		for (face = 0U; face < cells[cell]->face_count; face++)
			if (!NormalizeAuditHalfspace(&space->faces[
					cells[cell]->first_face + face].plane, &halfspaces[count++]))
				goto done;
	for (first = 0U; first < count; first++)
		for (second = first + 1U; second < count; second++)
		{
			double point[3];

			if (!IntersectThreeDouble(&boundary_space, &halfspaces[first],
					&halfspaces[second], point) ||
				!AuditPointInsideClosure(point, halfspaces, count))
				continue;
			if (!AppendAuditBoundaryPoint(&points, &point_count, &point_capacity,
					point))
			{
				result = AUDIT_GEOMETRY_OUT_OF_MEMORY;
				goto done;
			}
		}
	if (point_count < 3U)
	{
		result = AUDIT_GEOMETRY_EMPTY;
		goto done;
	}
	drop = DominantAxis(boundary->normal);
	u = (drop + 1U) % 3U;
	v = (drop + 2U) % 3U;
	for (index = 0U; index < point_count; index++)
	{
		center_u += points[index].value[u];
		center_v += points[index].value[v];
	}
	center_u /= (double)point_count;
	center_v /= (double)point_count;
	for (index = 0U; index < point_count; index++)
		points[index].angle = atan2(points[index].value[v] - center_v,
			points[index].value[u] - center_u);
	for (index = 1U; index < point_count; index++)
	{
		audit_boundary_point_t value = points[index];
		uint32_t position = index;

		while (position > 0U && points[position - 1U].angle > value.angle)
		{
			points[position] = points[position - 1U];
			position--;
		}
		points[position] = value;
	}
	for (index = 0U; index < point_count; index++)
		area2 += points[index].value[u] *
			points[(index + 1U) % point_count].value[v] -
			points[(index + 1U) % point_count].value[u] *
			points[index].value[v];
	if (!(fabs(boundary_space.normal[drop]) > 0.0))
		goto done;
	area2 = fabs(area2) * 0.5 / fabs(boundary_space.normal[drop]);
	if (!isfinite(area2) || area2 > (double)FLT_MAX)
		goto done;
	*area_out = (float)area2;
	result = *area_out > AUDIT_AREA_EPSILON ? AUDIT_GEOMETRY_POLYGON :
		AUDIT_GEOMETRY_EMPTY;
	if (result == AUDIT_GEOMETRY_POLYGON)
	{
		*points_out = points;
		*point_count_out = point_count;
		points = NULL;
	}

done:
	free(points);
	free(halfspaces);
	return result;
}

static float AuditPortalMatchTolerance(float magnitude)
{
	return fmaxf(AUDIT_GEOMETRY_EPSILON,
		fabsf(magnitude) * FLT_EPSILON * 64.0f);
}

static float AuditPortalCoordinateTolerance(double value)
{
	float rounded = (float)value;
	float lower = nextafterf(rounded, -INFINITY);
	float upper = nextafterf(rounded, INFINITY);
	float ulp = fmaxf(rounded - lower, upper - rounded);

	return fmaxf(AUDIT_GEOMETRY_EPSILON, ulp * 2.0f);
}

static int AuditOrderedFacet(const sg_configuration_space_t *space,
	const sg_configuration_face_t *face)
{
	uint32_t drop = DominantAxis(face->plane.normal);
	uint32_t u = (drop + 1U) % 3U, v = (drop + 2U) % 3U;
	uint32_t vertex;
	double sign = 0.0;

	if (face->vertex_count < 3U)
		return 0;
	for (vertex = 0U; vertex < face->vertex_count; vertex++)
	{
		const float *a = space->vertices[
			face->first_vertex + vertex].value;
		const float *b = space->vertices[face->first_vertex +
			(vertex + 1U) % face->vertex_count].value;
		const float *c = space->vertices[face->first_vertex +
			(vertex + 2U) % face->vertex_count].value;
		double cross = ((double)b[u] - a[u]) * ((double)c[v] - b[v]) -
			((double)b[v] - a[v]) * ((double)c[u] - b[u]);

		if (!isfinite(cross))
			return 0;
		if (cross == 0.0)
			continue;
		if (sign == 0.0)
			sign = cross;
		else if ((sign < 0.0) != (cross < 0.0))
			return 0;
	}
	return sign != 0.0 &&
		(sign < 0.0) == (face->plane.normal[drop] < 0.0f);
}

static int AuditCellExactBounds(audit_context_t *audit,
	const sg_configuration_cell_t *cell, float mins[3], float maxs[3])
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint32_t face, axis;

	halfspaces = malloc((size_t)cell->face_count * sizeof(*halfspaces));
	if (!halfspaces && cell->face_count)
		return -1;
	for (face = 0U; face < cell->face_count; face++)
	{
		const sg_configuration_plane_t *plane = &audit->space->faces[
			cell->first_face + face].plane;
		audit_halfspace_t source = FromPlane(plane);

		CopyVector(halfspaces[face].normal, plane->normal);
		halfspaces[face].distance = plane->distance;
		halfspaces[face].open = HalfspaceIsOpen(&source);
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		int32_t minimum, maximum;
		sg_configuration_lattice_stats_t stats = { 0 };
		int result = SG_ConfigurationLatticeCoordinateBounds(halfspaces,
			cell->face_count, axis, &minimum, &maximum, &stats);

		audit->result.lattice_solve_calls += stats.solve_calls;
		audit->result.lattice_constraints += stats.constraints;
		if (stats.maximum_binary_shift >
			audit->result.lattice_maximum_binary_shift)
			audit->result.lattice_maximum_binary_shift =
				stats.maximum_binary_shift;
		if (result <= 0)
		{
			free(halfspaces);
			return result;
		}
		mins[axis] = (float)minimum * 0.125f;
		maxs[axis] = (float)maximum * 0.125f;
	}
	free(halfspaces);
	return 1;
}

static int AuditCellGeometry(audit_context_t *audit, uint32_t cell_index)
{
	const sg_configuration_cell_t *cell = &audit->space->cells[cell_index];
	audit_double_halfspace_t *halfspaces = NULL;
	audit_boundary_point_t *points = NULL;
	uint32_t point_count = 0U, point_capacity = 0U;
	uint32_t first, second, third, face, axis;
	float expected_mins[3] = { INFINITY, INFINITY, INFINITY };
	float expected_maxs[3] = { -INFINITY, -INFINITY, -INFINITY };
	int has_constraint = 0, valid = 0;

	if (!cell->face_count || cell->first_face > audit->space->face_count ||
		cell->face_count > audit->space->face_count - cell->first_face)
		return 0;
#if SIZE_MAX == UINT32_MAX
	if ((size_t)cell->face_count > SIZE_MAX / sizeof(*halfspaces))
		return -1;
#endif
	halfspaces = malloc((size_t)cell->face_count * sizeof(*halfspaces));
	if (!halfspaces)
		return -1;
	for (face = 0U; face < cell->face_count; face++)
	{
		const sg_configuration_face_t *source = &audit->space->faces[
			cell->first_face + face];

		if (source->kind > SG_CONFIGURATION_FACE_CONSTRAINT_ONLY ||
			source->first_vertex > audit->space->vertex_count ||
			source->vertex_count > audit->space->vertex_count -
				source->first_vertex ||
			!NormalizeAuditHalfspace(&source->plane, &halfspaces[face]))
			goto done;
		has_constraint |=
			source->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
	}
	for (first = 0U; first < cell->face_count; first++)
		for (second = first + 1U; second < cell->face_count; second++)
			for (third = second + 1U; third < cell->face_count; third++)
			{
				double point[3];

				if (!IntersectThreeDouble(&halfspaces[first],
						&halfspaces[second], &halfspaces[third], point) ||
					!AuditPointInsideClosure(point, halfspaces,
						cell->face_count))
					continue;
				if (!AppendAuditBoundaryPoint(&points, &point_count,
						&point_capacity, point))
				{
					valid = -1;
					goto done;
				}
			}
	if (!point_count)
		goto done;
	for (face = 0U; face < cell->face_count; face++)
	{
		const sg_configuration_face_t *actual = &audit->space->faces[
			cell->first_face + face];
		uint32_t incident_count = 0U, vertex, candidate;
		uint8_t *matched = NULL;
		int expected_facet;

		for (candidate = 0U; candidate < point_count; candidate++)
		{
			double residual = points[candidate].value[0] *
				halfspaces[face].normal[0] + points[candidate].value[1] *
				halfspaces[face].normal[1] + points[candidate].value[2] *
				halfspaces[face].normal[2] - halfspaces[face].distance;

			if (fabs(residual) <= (double)AUDIT_GEOMETRY_EPSILON)
				incident_count++;
		}
		expected_facet = incident_count >= 3U;
		if ((actual->kind == SG_CONFIGURATION_FACE_FACET) != expected_facet ||
			(expected_facet && actual->vertex_count != incident_count) ||
			(!expected_facet && actual->vertex_count != 0U))
			goto done;
		if (!expected_facet)
			continue;
		if (!AuditOrderedFacet(audit->space, actual))
			goto done;
		matched = calloc(point_count, sizeof(*matched));
		if (!matched)
		{
			valid = -1;
			goto done;
		}
		for (vertex = 0U; vertex < actual->vertex_count; vertex++)
		{
			const float *value = audit->space->vertices[
				actual->first_vertex + vertex].value;
			int found = 0;

			if (!FiniteVector(value))
			{
				free(matched);
				goto done;
			}
			for (candidate = 0U; candidate < point_count; candidate++)
			{
				double residual = points[candidate].value[0] *
					halfspaces[face].normal[0] + points[candidate].value[1] *
					halfspaces[face].normal[1] + points[candidate].value[2] *
					halfspaces[face].normal[2] - halfspaces[face].distance;

				if (matched[candidate] || fabs(residual) >
						(double)AUDIT_GEOMETRY_EPSILON ||
					fabs((double)value[0] - points[candidate].value[0]) >
						(double)AuditPortalCoordinateTolerance(
							points[candidate].value[0]) ||
					fabs((double)value[1] - points[candidate].value[1]) >
						(double)AuditPortalCoordinateTolerance(
							points[candidate].value[1]) ||
					fabs((double)value[2] - points[candidate].value[2]) >
						(double)AuditPortalCoordinateTolerance(
							points[candidate].value[2]))
					continue;
				matched[candidate] = 1U;
				found = 1;
				break;
			}
			if (!found)
			{
				free(matched);
				goto done;
			}
		}
		free(matched);
	}
	if (has_constraint)
	{
		int result = AuditCellExactBounds(audit, cell, expected_mins,
			expected_maxs);

		if (result <= 0)
		{
			valid = result;
			goto done;
		}
	}
	else
		for (first = 0U; first < point_count; first++)
			for (axis = 0U; axis < 3U; axis++)
			{
				float value = (float)points[first].value[axis];

				if (value < expected_mins[axis])
					expected_mins[axis] = value;
				if (value > expected_maxs[axis])
					expected_maxs[axis] = value;
			}
	for (axis = 0U; axis < 3U; axis++)
	{
		float minimum = cell->bounds.mins.value[axis];
		float maximum = cell->bounds.maxs.value[axis];

		if (!isfinite(minimum) || !isfinite(maximum) ||
			(has_constraint ? minimum != expected_mins[axis] :
			 fabsf(minimum - expected_mins[axis]) >
				AuditPortalCoordinateTolerance(expected_mins[axis])) ||
			(has_constraint ? maximum != expected_maxs[axis] :
			 fabsf(maximum - expected_maxs[axis]) >
				AuditPortalCoordinateTolerance(expected_maxs[axis])))
			goto done;
	}
	valid = 1;

done:
	free(points);
	free(halfspaces);
	return valid;
}

static int AuditTangentBoundaryGeometry(const sg_configuration_space_t *space,
	const sg_configuration_portal_t *portal,
	const sg_configuration_plane_t *boundary, const float first_witness[3],
	const float second_witness[3], double expected[4][3], float *area_out)
{
	const sg_configuration_cell_t *cells[2] = {
		&space->cells[portal->from_cell], &space->cells[portal->to_cell]
	};
	audit_double_halfspace_t normalized;
	double first_side, second_side, denominator, fraction;
	float center[3], tangent[3] = { 0.0f, 0.0f, 0.0f };
	float bitangent[3], minimum_slack = INFINITY, radius, scale;
	uint32_t axis, seed_axis = 0U, cell, face, vertex;
	double area2 = 0.0;
	uint32_t drop, u, v;

	*area_out = 0.0f;
	if (!NormalizeAuditHalfspace(boundary, &normalized))
		return 0;
	first_side = (double)first_witness[0] * normalized.normal[0] +
		(double)first_witness[1] * normalized.normal[1] +
		(double)first_witness[2] * normalized.normal[2] - normalized.distance;
	second_side = (double)second_witness[0] * normalized.normal[0] +
		(double)second_witness[1] * normalized.normal[1] +
		(double)second_witness[2] * normalized.normal[2] - normalized.distance;
	denominator = first_side - second_side;
	if (first_side * second_side > 0.0 || denominator == 0.0 ||
		!isfinite(denominator))
		return 0;
	fraction = first_side / denominator;
	for (axis = 0U; axis < 3U; axis++)
		center[axis] = (float)((double)first_witness[axis] + fraction *
			((double)second_witness[axis] - first_witness[axis]));
	{
		double residual = (double)center[0] * normalized.normal[0] +
			(double)center[1] * normalized.normal[1] +
			(double)center[2] * normalized.normal[2] - normalized.distance;

		for (axis = 0U; axis < 3U; axis++)
			center[axis] = (float)((double)center[axis] -
				residual * normalized.normal[axis]);
	}
	for (axis = 1U; axis < 3U; axis++)
		if (fabs(normalized.normal[axis]) <
			fabs(normalized.normal[seed_axis]))
			seed_axis = axis;
	tangent[seed_axis] = 1.0f;
	{
		double projection = (double)tangent[0] * normalized.normal[0] +
			(double)tangent[1] * normalized.normal[1] +
			(double)tangent[2] * normalized.normal[2];
		double length_squared;

		for (axis = 0U; axis < 3U; axis++)
			tangent[axis] = (float)((double)tangent[axis] -
				projection * normalized.normal[axis]);
		length_squared = (double)tangent[0] * tangent[0] +
			(double)tangent[1] * tangent[1] +
			(double)tangent[2] * tangent[2];
		if (!(length_squared > 0.0))
			return 0;
		scale = (float)(1.0 / sqrt(length_squared));
		for (axis = 0U; axis < 3U; axis++)
			tangent[axis] *= scale;
	}
	bitangent[0] = (float)(normalized.normal[1] * tangent[2] -
		normalized.normal[2] * tangent[1]);
	bitangent[1] = (float)(normalized.normal[2] * tangent[0] -
		normalized.normal[0] * tangent[2]);
	bitangent[2] = (float)(normalized.normal[0] * tangent[1] -
		normalized.normal[1] * tangent[0]);
	scale = (float)(1.0 / sqrt((double)bitangent[0] * bitangent[0] +
		(double)bitangent[1] * bitangent[1] +
		(double)bitangent[2] * bitangent[2]));
	for (axis = 0U; axis < 3U; axis++)
		bitangent[axis] *= scale;
	for (cell = 0U; cell < 2U; cell++)
		for (face = 0U; face < cells[cell]->face_count; face++)
		{
			const sg_configuration_plane_t *plane = &space->faces[
				cells[cell]->first_face + face].plane;
			audit_double_halfspace_t plane_space;
			double slack;

			if (CanonicalPlanesClose(plane, boundary))
				continue;
			if (!NormalizeAuditHalfspace(plane, &plane_space))
				return 0;
			slack = plane_space.distance -
				((double)center[0] * plane_space.normal[0] +
				(double)center[1] * plane_space.normal[1] +
				(double)center[2] * plane_space.normal[2]);
			if (!(slack > 0.0))
				return 0;
			if (slack < minimum_slack)
				minimum_slack = (float)slack;
		}
	if (!isfinite(minimum_slack) ||
		!(minimum_slack > 0.00001f * 8.0f))
		return 0;
	radius = minimum_slack * 0.125f;
	for (vertex = 0U; vertex < 4U; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			expected[vertex][axis] = (double)center[axis] + radius *
				((vertex == 0U || vertex == 3U) ? tangent[axis] :
				 -tangent[axis]) + radius *
				((vertex < 2U) ? bitangent[axis] : -bitangent[axis]);
	drop = DominantAxis(boundary->normal);
	u = (drop + 1U) % 3U;
	v = (drop + 2U) % 3U;
	for (vertex = 0U; vertex < 4U; vertex++)
		area2 += (expected[vertex][u] - center[u]) *
			(expected[(vertex + 1U) % 4U][v] - center[v]) -
			(expected[(vertex + 1U) % 4U][u] - center[u]) *
			(expected[vertex][v] - center[v]);
	area2 = fabs(area2) * 0.5 / fabs(normalized.normal[drop]);
	if (!isfinite(area2) || !(area2 > (double)AUDIT_AREA_EPSILON) ||
		area2 > (double)FLT_MAX)
		return 0;
	*area_out = (float)area2;
	return 1;
}

static int AuditPortalCyclicMatches(const sg_configuration_space_t *space,
	const sg_configuration_portal_t *portal, const double expected[4][3])
{
	uint32_t rotation, vertex, axis;
	int reverse;

	if (portal->vertex_count != 4U)
		return 0;
	for (reverse = 0; reverse < 2; reverse++)
		for (rotation = 0U; rotation < 4U; rotation++)
		{
			int match = 1;

			for (vertex = 0U; vertex < 4U && match; vertex++)
			{
				uint32_t expected_index = reverse ?
					(rotation + 4U - vertex) % 4U :
					(rotation + vertex) % 4U;
				const float *actual = space->vertices[
					portal->first_vertex + vertex].value;

				for (axis = 0U; axis < 3U; axis++)
					if (fabs((double)actual[axis] -
							expected[expected_index][axis]) >
						(double)AuditPortalCoordinateTolerance(
							expected[expected_index][axis]))
						match = 0;
			}
			if (match)
				return 1;
		}
	return 0;
}

static int AuditPortalMatchesAuthoritative(
	const sg_configuration_space_t *space,
	const sg_configuration_portal_t *portal,
	const sg_configuration_plane_t *boundary, const float first_witness[3],
	const float second_witness[3], float portal_area)
{
	audit_boundary_point_t *expected = NULL;
	uint8_t *matched = NULL;
	uint32_t expected_count = 0U, vertex, candidate;
	float expected_area = 0.0f;
	float area_tolerance, clearance_tolerance;
	int reconstructed, valid = -1;

	reconstructed = AuditAuthoritativeBoundaryGeometry(space,
		portal->from_cell, portal->to_cell, boundary, &expected,
		&expected_count, &expected_area);
	if (reconstructed == AUDIT_GEOMETRY_OUT_OF_MEMORY)
		goto done;
	if (reconstructed == AUDIT_GEOMETRY_INVALID)
	{
		valid = 0;
		goto done;
	}
	if (reconstructed == AUDIT_GEOMETRY_EMPTY)
	{
		double tangent[4][3];
		float tangent_area;

		valid = AuditTangentBoundaryGeometry(space, portal, boundary,
			first_witness, second_witness, tangent, &tangent_area) &&
			AuditPortalCyclicMatches(space, portal,
				(const double (*)[3])tangent);
		if (!valid)
			goto done;
		area_tolerance = fmaxf(AUDIT_AREA_EPSILON,
			fabsf(tangent_area) * FLT_EPSILON * 64.0f);
		clearance_tolerance = AuditPortalMatchTolerance(sqrtf(tangent_area));
		valid = fabsf(portal_area - tangent_area) <= area_tolerance &&
			fabsf(portal->clearance - sqrtf(tangent_area)) <=
				clearance_tolerance;
		goto done;
	}
	if (expected_count != portal->vertex_count)
	{
		valid = 0;
		goto done;
	}
	matched = calloc(expected_count, sizeof(*matched));
	if (!matched)
		goto done;
	valid = 0;
	for (vertex = 0U; vertex < portal->vertex_count; vertex++)
	{
		const float *actual = space->vertices[
			portal->first_vertex + vertex].value;
		int found = 0;

		for (candidate = 0U; candidate < expected_count; candidate++)
			if (!matched[candidate] &&
				fabs((double)actual[0] - expected[candidate].value[0]) <=
					(double)AuditPortalCoordinateTolerance(
						expected[candidate].value[0]) &&
				fabs((double)actual[1] - expected[candidate].value[1]) <=
					(double)AuditPortalCoordinateTolerance(
						expected[candidate].value[1]) &&
				fabs((double)actual[2] - expected[candidate].value[2]) <=
					(double)AuditPortalCoordinateTolerance(
						expected[candidate].value[2]))
			{
				matched[candidate] = 1U;
				found = 1;
				break;
			}
		if (!found)
			goto done;
	}
	area_tolerance = fmaxf(AUDIT_AREA_EPSILON,
		fabsf(expected_area) * FLT_EPSILON * 64.0f);
	clearance_tolerance = AuditPortalMatchTolerance(sqrtf(expected_area));
	valid = fabsf(portal_area - expected_area) <= area_tolerance &&
		fabsf(portal->clearance - sqrtf(expected_area)) <= clearance_tolerance;

done:
	free(matched);
	free(expected);
	return valid;
}

#if defined(SG_CONFIGURATION_SPACE_TESTING)
int SG_ConfigurationAuditTestTangentPortalGeometry(void)
{
	sg_configuration_space_t space;
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[4];
	sg_configuration_portal_t portal;
	sg_rune_vec3_t vertices[4], saved[4];
	double expected[4][3];
	const float from[3] = { -0.5f, 0.0f, 0.0f };
	const float to[3] = { 0.5f, 0.0f, 0.0f };
	const float bin_normal_a[3] = { 1.0f, -FLT_TRUE_MIN, 0.0f };
	const float bin_normal_b[3] = { 1.0f, AUDIT_GEOMETRY_EPSILON, 0.0f };
	audit_portal_ref_t bin_reference;
	sg_configuration_plane_t bin_query;
	uint8_t bin_seen[1] = { 0U };
	int64_t bin_a[4], bin_b[4];
	float area, expected_area;
	uint32_t vertex, axis;
	int valid = 1;

	memset(&space, 0, sizeof(space));
	memset(cells, 0, sizeof(cells));
	memset(faces, 0, sizeof(faces));
	memset(&portal, 0, sizeof(portal));
	memset(vertices, 0, sizeof(vertices));
	space.cells = cells;
	space.cell_count = 2U;
	space.faces = faces;
	space.face_count = 4U;
	space.vertices = vertices;
	space.vertex_count = 4U;
	cells[0].first_face = 0U;
	cells[0].face_count = 2U;
	cells[1].first_face = 2U;
	cells[1].face_count = 2U;
	faces[0].plane.normal[0] = 1.0e-30f;
	faces[1].plane.normal[1] = 1.0f;
	faces[1].plane.distance = 1.0f;
	faces[2].plane.normal[0] = -1.0e-30f;
	faces[3].plane.normal[1] = -1.0f;
	faces[3].plane.distance = 1.0f;
	portal.from_cell = 0U;
	portal.to_cell = 1U;
	portal.vertex_count = 4U;
	portal.plane = faces[0].plane;
	if (!AuditTangentBoundaryGeometry(&space, &portal, &portal.plane,
			from, to, expected, &expected_area))
		return 0;
	for (vertex = 0U; vertex < 4U; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			vertices[vertex].value[axis] = (float)expected[vertex][axis];
	memcpy(saved, vertices, sizeof(saved));
	if (!AuditPortalGeometry(&space, &portal, &area))
		return 0;
	portal.clearance = sqrtf(expected_area);
	valid &= AuditPortalMatchesAuthoritative(&space, &portal, &portal.plane,
		from, to, area) == 1;
	portal.vertex_count = 3U;
	valid &= AuditPortalMatchesAuthoritative(&space, &portal, &portal.plane,
		from, to, area) == 0;
	portal.vertex_count = 4U;
	vertices[0].value[1] += 0.01f;
	valid &= AuditPortalMatchesAuthoritative(&space, &portal, &portal.plane,
		from, to, area) == 0;
	memcpy(vertices, saved, sizeof(saved));
	vertices[1] = saved[2];
	vertices[2] = saved[1];
	valid &= AuditPortalMatchesAuthoritative(&space, &portal, &portal.plane,
		from, to, area) == 0;
	memcpy(vertices, saved, sizeof(saved));
	portal.clearance *= 2.0f;
	valid &= AuditPortalMatchesAuthoritative(&space, &portal, &portal.plane,
		from, to, area) == 0;
	valid &= AuditPlaneBins(bin_normal_a, 0.0f, bin_a) &&
		AuditPlaneBins(bin_normal_b, 0.0f, bin_b);
	for (axis = 0U; axis < 4U; axis++)
		valid &= llabs(bin_a[axis] - bin_b[axis]) <= 1;
	memset(&bin_reference, 0, sizeof(bin_reference));
	memset(&bin_query, 0, sizeof(bin_query));
	bin_reference.from = 0U;
	bin_reference.to = 1U;
	memcpy(bin_reference.canonical_normal, bin_normal_a,
		sizeof(bin_reference.canonical_normal));
	memcpy(bin_reference.bin, bin_a, sizeof(bin_reference.bin));
	memcpy(bin_query.normal, bin_normal_b, sizeof(bin_query.normal));
	valid &= FindPortalRef(&bin_reference, 1U, bin_seen, 0U, 1U,
		&bin_query) && bin_seen[0];
	return valid;
}
#endif

static int AuditExpectedFacePair(audit_context_t *audit,
	const audit_face_ref_t *a, const audit_face_ref_t *b,
	const audit_portal_ref_t *portals, uint32_t portal_count, uint8_t *seen)
{
	const sg_configuration_face_t *face_a = &audit->space->faces[a->face];
	const sg_configuration_face_t *face_b = &audit->space->faces[b->face];
	const sg_configuration_cell_t *cell_a = &audit->space->cells[a->cell];
	float from[3], to[3];
	sg_host_collision_transition_t transition;
	int from_result, to_result;

	if (a->cell == b->cell ||
		!SameBoundary(&face_a->plane, &face_b->plane))
		return 1;
	from_result = AuditPortalSideWitness(audit, a->cell, b->cell,
		&face_a->plane, from);
	to_result = AuditPortalSideWitness(audit, b->cell, a->cell,
		&face_b->plane, to);
	if (from_result < 0 || to_result < 0)
		return 0;
	if (!from_result || !to_result ||
		!SG_HostCollisionTransition(audit->authority, NULL, from, to,
			cell_a->stance, &transition) || !transition.clear)
		return 1;
	if (!FindPortalRef(portals, portal_count, seen,
			a->cell, b->cell, &face_a->plane))
	{
		audit->result.omitted_portals++;
		return 0;
	}
	return 1;
}

static int AuditPortals(audit_context_t *audit)
{
	uint8_t *seen = NULL;
	audit_face_ref_t *faces = NULL;
	audit_portal_ref_t *portals = NULL;
	uint32_t face_count = 0, face_index = 0, portal_index, cell;
	int valid = 0;

	for (cell = 0; cell < audit->space->cell_count; cell++)
	{
		if (audit->space->cells[cell].face_count > UINT32_MAX - face_count)
		{
			Fail(audit, SG_CONFIGURATION_AUDIT_OVERFLOW, cell);
			goto done;
		}
		face_count += audit->space->cells[cell].face_count;
	}
	seen = calloc(audit->space->portal_count ? audit->space->portal_count : 1U,
		sizeof(*seen));
	faces = calloc(face_count ? face_count : 1U, sizeof(*faces));
	portals = calloc(audit->space->portal_count ?
		audit->space->portal_count : 1U, sizeof(*portals));
	if (!seen || !faces || !portals)
	{
		Fail(audit, SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY, 0U);
		goto done;
	}
	for (cell = 0; cell < audit->space->cell_count; cell++)
	{
		uint32_t offset;

		for (offset = 0; offset < audit->space->cells[cell].face_count; offset++)
		{
			audit_face_ref_t *reference;
			const sg_configuration_face_t *face;

			face = &audit->space->faces[
				audit->space->cells[cell].first_face + offset];
			reference = &faces[face_index++];
			reference->cell = cell;
			reference->face = audit->space->cells[cell].first_face + offset;
			AuditCanonicalPlane(&face->plane, reference->canonical_normal,
				&reference->canonical_distance);
			if (!AuditFaceBins(reference))
				goto done;
		}
	}
	face_count = face_index;
	for (portal_index = 0; portal_index < audit->space->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&audit->space->portals[portal_index];

		portals[portal_index].portal = portal_index;
		portals[portal_index].from = portal->from_cell < portal->to_cell ?
			portal->from_cell : portal->to_cell;
		portals[portal_index].to = portal->from_cell < portal->to_cell ?
			portal->to_cell : portal->from_cell;
		AuditCanonicalPlane(&portal->plane,
			portals[portal_index].canonical_normal,
			&portals[portal_index].canonical_distance);
		if (!AuditPlaneBins(portals[portal_index].canonical_normal,
				portals[portal_index].canonical_distance,
				portals[portal_index].bin))
			goto done;
	}
	if (!SortAuditFaces(audit->space, faces, face_count) ||
		!SortPortalRefs(portals, audit->space->portal_count))
	{
		Fail(audit, SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY, 0U);
		goto done;
	}
	for (face_index = 0U; face_index < face_count; face_index++)
	{
		const audit_face_ref_t *left = &faces[face_index];
		const sg_rune_stance_t stance =
			audit->space->cells[left->cell].stance;
		int d0, d1, d2, d3;

		for (d0 = -1; d0 <= 1; d0++)
			for (d1 = -1; d1 <= 1; d1++)
				for (d2 = -1; d2 <= 1; d2++)
					for (d3 = -1; d3 <= 1; d3++)
			{
				int64_t bin[4];
				uint32_t right;

				if (!OffsetAuditBin(left->bin[0], d0, &bin[0]) ||
					!OffsetAuditBin(left->bin[1], d1, &bin[1]) ||
					!OffsetAuditBin(left->bin[2], d2, &bin[2]) ||
					!OffsetAuditBin(left->bin[3], d3, &bin[3]))
					continue;
				right = AuditFaceBinLowerBound(audit->space, faces,
					face_count, stance, bin);
				while (right < face_count && AuditFaceBinKeyCompare(
					audit->space, &faces[right], stance, bin) == 0)
				{
					if (right > face_index && !AuditExpectedFacePair(audit,
							left, &faces[right], portals,
							audit->space->portal_count, seen))
						goto done;
					right++;
				}
			}
	}
	for (portal_index = 0; portal_index < audit->space->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&audit->space->portals[portal_index];
		float from[3], to[3];
		float portal_area = 0.0f;
		sg_host_collision_transition_t transition;
		const sg_configuration_plane_t *from_plane, *to_plane;
		int geometry;

		if (!seen[portal_index] ||
			portal->from_cell >= audit->space->cell_count ||
			portal->to_cell >= audit->space->cell_count ||
			portal->stance != audit->space->cells[portal->from_cell].stance ||
			portal->stance != audit->space->cells[portal->to_cell].stance ||
			!isfinite(portal->clearance) || !(portal->clearance > 0.0f) ||
			portal->first_vertex > audit->space->vertex_count ||
			portal->vertex_count < 3U ||
			portal->vertex_count > audit->space->vertex_count -
				portal->first_vertex ||
			!AuditPortalGeometry(audit->space, portal, &portal_area))
		{
			audit->result.invented_portals++;
			goto done;
		}
		from_plane = CellBoundaryPlane(audit->space, portal->from_cell,
			&portal->plane);
		to_plane = CellBoundaryPlane(audit->space, portal->to_cell,
			&portal->plane);
		if (!from_plane || !to_plane ||
			AuditPortalSideWitness(audit, portal->from_cell, portal->to_cell,
				from_plane, from) != 1 ||
			AuditPortalSideWitness(audit, portal->to_cell, portal->from_cell,
				to_plane, to) != 1)
			goto done;
		geometry = AuditPortalMatchesAuthoritative(audit->space, portal,
			from_plane, from, to, portal_area);
		if (geometry < 0)
		{
			Fail(audit, SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY, portal_index);
			goto done;
		}
		if (!geometry || !SG_HostCollisionTransition(audit->authority, NULL,
				from, to, portal->stance, &transition) || !transition.clear)
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

static int CellHasConstraintOnly(const sg_configuration_space_t *space,
	uint32_t cell_index)
{
	const sg_configuration_cell_t *cell = &space->cells[cell_index];
	uint32_t face;

	for (face = 0; face < cell->face_count; face++)
		if (space->faces[cell->first_face + face].kind ==
			SG_CONFIGURATION_FACE_CONSTRAINT_ONLY)
			return 1;
	return 0;
}

static int CellsIntersectFloatVolume(const sg_configuration_space_t *space,
	uint32_t a, uint32_t b)
{
	const sg_configuration_cell_t *cell_a = &space->cells[a];
	const sg_configuration_cell_t *cell_b = &space->cells[b];
	uint32_t count = cell_a->face_count + cell_b->face_count;
	audit_halfspace_t *spaces = malloc((size_t)count * sizeof(*spaces));
	float points[4][3];
	uint32_t point_count = 0, first, second, third;
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

					for (axis = 0; axis < 3U; axis++)
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

static int CellsIntersectQ8(audit_context_t *audit, uint32_t a, uint32_t b)
{
	const sg_configuration_space_t *space = audit->space;
	const sg_configuration_cell_t *cell_a = &space->cells[a];
	const sg_configuration_cell_t *cell_b = &space->cells[b];
	uint32_t count = cell_a->face_count + cell_b->face_count;
	audit_halfspace_t *spaces = malloc((size_t)count * sizeof(*spaces));
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t index;
	int result, positive_margin;

	halfspaces = malloc((size_t)count * sizeof(*halfspaces));
	clearance = malloc((size_t)count * sizeof(*clearance));
	if (!spaces || !halfspaces || !clearance)
	{
		free(spaces);
		free(halfspaces);
		free(clearance);
		return -1;
	}
	count = 0;
	AddCellHalfspaces(space, a, spaces, &count);
	AddCellHalfspaces(space, b, spaces, &count);
	for (index = 0; index < count; index++)
	{
		CopyVector(halfspaces[index].normal, spaces[index].normal);
		halfspaces[index].distance = spaces[index].distance;
		halfspaces[index].open = HalfspaceIsOpen(&spaces[index]);
		clearance[index] = 1U;
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		count, NULL, point, &positive_margin, &stats);
	free(spaces);
	free(halfspaces);
	free(clearance);
	audit->result.lattice_solve_calls += stats.solve_calls;
	audit->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift > audit->result.lattice_maximum_binary_shift)
		audit->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	return result > 0 && positive_margin ? 1 : result;
}

static int AuditOverlapExactBounds(audit_context_t *audit,
	const sg_configuration_stance_overlap_t *overlap)
{
	sg_configuration_lattice_halfspace_t *halfspaces;
	float mins[3], maxs[3];
	uint32_t face, axis;

	if (overlap->first_face > audit->space->face_count ||
		overlap->face_count > audit->space->face_count - overlap->first_face ||
		!overlap->face_count)
		return 0;
	halfspaces = malloc((size_t)overlap->face_count * sizeof(*halfspaces));
	if (!halfspaces)
		return -1;
	for (face = 0; face < overlap->face_count; face++)
	{
		const sg_configuration_plane_t *plane = &audit->space->faces[
			overlap->first_face + face].plane;
		audit_halfspace_t source = FromPlane(plane);

		CopyVector(halfspaces[face].normal, plane->normal);
		halfspaces[face].distance = plane->distance;
		halfspaces[face].open = HalfspaceIsOpen(&source);
	}
	for (axis = 0; axis < 3U; axis++)
	{
		int32_t minimum, maximum;
		sg_configuration_lattice_stats_t stats = { 0 };
		int result;

		result = SG_ConfigurationLatticeCoordinateBounds(halfspaces,
			overlap->face_count, axis, &minimum, &maximum, &stats);
		audit->result.lattice_solve_calls += stats.solve_calls;
		audit->result.lattice_constraints += stats.constraints;
		if (stats.maximum_binary_shift >
			audit->result.lattice_maximum_binary_shift)
			audit->result.lattice_maximum_binary_shift =
				stats.maximum_binary_shift;
		if (result <= 0)
		{
			free(halfspaces);
			return result;
		}
		mins[axis] = (float)minimum * 0.125f;
		maxs[axis] = (float)maximum * 0.125f;
	}
	free(halfspaces);
	for (axis = 0; axis < 3U; axis++)
		if (overlap->bounds.mins.value[axis] != mins[axis] ||
			overlap->bounds.maxs.value[axis] != maxs[axis])
			return 0;
	return 1;
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
	cells = calloc(audit->space->cell_count ? audit->space->cell_count : 1U,
		sizeof(*cells));
	overlaps = calloc(audit->space->stance_overlap_count ?
		audit->space->stance_overlap_count : 1U, sizeof(*overlaps));
	if (!seen || !covered || !cells || !overlaps)
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
				intersects = CellHasConstraintOnly(audit->space, standing) ||
					CellHasConstraintOnly(audit->space, crouching) ?
					CellsIntersectQ8(audit, standing, crouching) :
					CellsIntersectFloatVolume(audit->space, standing, crouching);
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
		uint32_t face;
		int exact_bounds = 0;

		if (overlap->first_face > audit->space->face_count ||
			overlap->face_count > audit->space->face_count - overlap->first_face)
			goto done;
		for (face = 0; face < overlap->face_count; face++)
			if (audit->space->faces[overlap->first_face + face].kind ==
				SG_CONFIGURATION_FACE_CONSTRAINT_ONLY)
				exact_bounds = 1;

		if (!seen[index] ||
			(exact_bounds && AuditOverlapExactBounds(audit, overlap) != 1) ||
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
