#include "sg_rune_compact_model.h"
#include "sg_rune_compact_static.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum facet_polygon_result_e
{
	FACET_POLYGON_VALID = 0,
	FACET_POLYGON_INVALID_GEOMETRY,
	FACET_POLYGON_NONCANONICAL
} facet_polygon_result_t;

static void SetError(sg_rune_compact_error_t *error,
	sg_rune_compact_error_code_t code,
	sg_rune_compact_record_domain_t domain, uint32_t record)
{
	if (!error)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int ArrayPresent(const void *values, uint32_t count)
{
	return count == 0U || values != NULL;
}

static int ReservedBytesZero(const uint8_t reserved[3])
{
	return reserved[0] == 0U && reserved[1] == 0U && reserved[2] == 0U;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareI32(int32_t left, int32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int Q8VecCompare(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const int comparison = CompareI32(left->value[axis],
			right->value[axis]);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int Q8VecMatches(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	return left->value[0] == right->value[0] &&
		left->value[1] == right->value[1] &&
		left->value[2] == right->value[2];
}

static int Binary32CanonicalFinite(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static int Binary32Nonnegative(uint32_t bits)
{
	return Binary32CanonicalFinite(bits) &&
		(bits & UINT32_C(0x80000000)) == 0U;
}

static double Binary32Value(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return (double)value;
}

static int Sha256Present(const uint8_t digest[32])
{
	uint32_t index;

	for (index = 0U; index < 32U; index++)
		if (digest[index] != 0U)
			return 1;
	return 0;
}

static int BoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (!bounds)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PlaneValid(const sg_rune_binary32_plane_t *plane)
{
	uint32_t axis;
	int has_normal = 0;

	if (!plane || !Binary32CanonicalFinite(plane->distance_bits))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		if (!Binary32CanonicalFinite(plane->normal_bits[axis]))
			return 0;
		if ((plane->normal_bits[axis] & UINT32_C(0x7fffffff)) != 0U)
			has_normal = 1;
	}
	return has_normal;
}

static double CrossDot(const double normal[3],
	const sg_rune_q8_vec3_t *origin, const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right, double *scale_out)
{
	const double left_x = (double)left->value[0] - (double)origin->value[0];
	const double left_y = (double)left->value[1] - (double)origin->value[1];
	const double left_z = (double)left->value[2] - (double)origin->value[2];
	const double right_x = (double)right->value[0] - (double)origin->value[0];
	const double right_y = (double)right->value[1] - (double)origin->value[1];
	const double right_z = (double)right->value[2] - (double)origin->value[2];
	const double cross_x = left_y * right_z - left_z * right_y;
	const double cross_y = left_z * right_x - left_x * right_z;
	const double cross_z = left_x * right_y - left_y * right_x;

	*scale_out = fabs(cross_x * normal[0]) +
		fabs(cross_y * normal[1]) + fabs(cross_z * normal[2]);
	return cross_x * normal[0] + cross_y * normal[1] +
		cross_z * normal[2];
}

static int VertexOnPlane(const sg_rune_q8_vec3_t *vertex,
	const double normal[3], double distance)
{
	const double scaled_distance = distance * 8.0;
	const double terms[3] = {
		normal[0] * (double)vertex->value[0],
		normal[1] * (double)vertex->value[1],
		normal[2] * (double)vertex->value[2]
	};
	const double residual = terms[0] + terms[1] + terms[2] -
		scaled_distance;
	const double quantization_bound = 0.5 *
		(fabs(normal[0]) + fabs(normal[1]) + fabs(normal[2]));
	const double arithmetic_bound = 32.0 * DBL_EPSILON *
		(fabs(terms[0]) + fabs(terms[1]) + fabs(terms[2]) +
		 fabs(scaled_distance) + 1.0);

	return fabs(residual) <= quantization_bound + arithmetic_bound;
}

static facet_polygon_result_t ValidateFacetPolygon(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_facet_t *facet)
{
	const sg_rune_q8_vec3_t *vertices =
		&model->vertices[facet->vertices.first];
	double normal[3];
	double area = 0.0;
	double area_scale = 0.0;
	double area_tolerance;
	double distance;
	uint32_t vertex_index;

	for (vertex_index = 0U; vertex_index < 3U; vertex_index++)
		normal[vertex_index] =
			Binary32Value(facet->plane.normal_bits[vertex_index]);
	distance = Binary32Value(facet->plane.distance_bits);
	for (vertex_index = 0U; vertex_index < facet->vertices.count;
		vertex_index++) {
		const uint32_t next =
			(vertex_index + 1U) % facet->vertices.count;
		const int first_comparison = vertex_index == 0U ? -1 :
			Q8VecCompare(&vertices[0], &vertices[vertex_index]);

		if (!VertexOnPlane(&vertices[vertex_index], normal, distance) ||
			Q8VecMatches(&vertices[vertex_index], &vertices[next]))
			return FACET_POLYGON_INVALID_GEOMETRY;
		if (first_comparison == 0)
			return FACET_POLYGON_INVALID_GEOMETRY;
		if (first_comparison > 0)
			return FACET_POLYGON_NONCANONICAL;
	}
	for (vertex_index = 1U; vertex_index + 1U < facet->vertices.count;
		vertex_index++) {
		double scale;
		double triangle;
		double tolerance;

		triangle = CrossDot(normal, &vertices[0], &vertices[vertex_index],
			&vertices[vertex_index + 1U], &scale);
		tolerance = 32.0 * DBL_EPSILON * (scale + 1.0);
		if (triangle <= tolerance)
			return FACET_POLYGON_INVALID_GEOMETRY;
		area += triangle;
		area_scale += scale;
	}
	area_tolerance = 32.0 * DBL_EPSILON * (area_scale + 1.0) *
		(double)facet->vertices.count;
	if (fabs(area) <= area_tolerance)
		return FACET_POLYGON_INVALID_GEOMETRY;
	if (area < 0.0)
		return FACET_POLYGON_NONCANONICAL;

	/* Strict positive turns encode a canonical convex BSP facet. Together with
	 * the unique minimum and distinct neighbors, they exclude every repeated
	 * nonadjacent vertex in one pass. */
	for (vertex_index = 0U; vertex_index < facet->vertices.count;
		vertex_index++) {
		const uint32_t previous = vertex_index == 0U ?
			facet->vertices.count - 1U : vertex_index - 1U;
		const uint32_t next =
			(vertex_index + 1U) % facet->vertices.count;
		double scale;
		const double turn = CrossDot(normal, &vertices[vertex_index],
			&vertices[next], &vertices[previous], &scale);
		const double tolerance = 32.0 * DBL_EPSILON * (scale + 1.0);

		if (turn <= tolerance)
			return FACET_POLYGON_INVALID_GEOMETRY;
	}
	return FACET_POLYGON_VALID;
}

static int PlaneCompare(const sg_rune_binary32_plane_t *left,
	const sg_rune_binary32_plane_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const int comparison = CompareU32(left->normal_bits[axis],
			right->normal_bits[axis]);

		if (comparison != 0)
			return comparison;
	}
	return CompareU32(left->distance_bits, right->distance_bits);
}

static int HullValid(const sg_rune_compact_hull_t *hull)
{
	const sg_rune_q8_bounds_t bounds = { hull->mins, hull->maxs };

	return BoundsValid(&bounds);
}

static int HullMatches(const sg_rune_compact_hull_t *left,
	const sg_rune_compact_hull_t *right)
{
	return Q8VecMatches(&left->mins, &right->mins) &&
		Q8VecMatches(&left->maxs, &right->maxs);
}

static int PhysicsValid(const sg_rune_compact_physics_t *physics)
{
	const uint32_t values[] = {
		physics->gravity_bits, physics->ground_acceleration_bits,
		physics->air_acceleration_bits, physics->water_acceleration_bits,
		physics->hook_acceleration_bits, physics->external_acceleration_bits,
		physics->water_drag_bits, physics->max_velocity_bits
	};
	uint32_t index;

	for (index = 0U; index < sizeof(values) / sizeof(values[0]); index++)
		if (!Binary32Nonnegative(values[index]))
			return 0;
	return physics->gravity_bits != 0U && physics->max_velocity_bits != 0U &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
		physics->substep_ms <= physics->frame_ms;
}

static int PhysicsMatches(const sg_rune_compact_physics_t *left,
	const sg_rune_compact_physics_t *right)
{
	return left->gravity_bits == right->gravity_bits &&
		left->ground_acceleration_bits == right->ground_acceleration_bits &&
		left->air_acceleration_bits == right->air_acceleration_bits &&
		left->water_acceleration_bits == right->water_acceleration_bits &&
		left->hook_acceleration_bits == right->hook_acceleration_bits &&
		left->external_acceleration_bits == right->external_acceleration_bits &&
		left->water_drag_bits == right->water_drag_bits &&
		left->max_velocity_bits == right->max_velocity_bits &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int SourceCountsValid(const sg_rune_compact_source_counts_t *counts)
{
	return counts->model_count != 0U && counts->leaf_count != 0U &&
		counts->area_count != 0U && counts->plane_count != 0U &&
		counts->entity_count != 0U &&
		(counts->brush_count == 0U) == (counts->brush_side_count == 0U);
}

static int SourceCountsMatch(const sg_rune_compact_source_counts_t *left,
	const sg_rune_compact_source_counts_t *right)
{
	return left->model_count == right->model_count &&
		left->leaf_count == right->leaf_count &&
		left->area_count == right->area_count &&
		left->plane_count == right->plane_count &&
		left->brush_count == right->brush_count &&
		left->brush_side_count == right->brush_side_count &&
		left->entity_count == right->entity_count;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	uint32_t digest_byte;

	if (!actual || !expected)
		return 0;
	for (digest_byte = 0U; digest_byte < 32U; digest_byte++)
		if (actual->bsp_sha256[digest_byte] !=
			expected->bsp_sha256[digest_byte])
			return 0;
	return actual->bsp_bytes == expected->bsp_bytes &&
		actual->bsp_checksum == expected->bsp_checksum &&
		actual->entity_crc32 == expected->entity_crc32 &&
		actual->entity_semantics_id == expected->entity_semantics_id &&
		actual->physics_abi_id == expected->physics_abi_id &&
		actual->collision_law_id == expected->collision_law_id &&
		actual->pmove_law_id == expected->pmove_law_id &&
		actual->gravity_law_id == expected->gravity_law_id &&
		actual->hook_law_id == expected->hook_law_id &&
		actual->mechanism_law_id == expected->mechanism_law_id &&
		actual->weapon_law_id == expected->weapon_law_id &&
		actual->construction_id == expected->construction_id &&
		actual->schema_id == expected->schema_id &&
		actual->producer_identity == expected->producer_identity &&
		SourceCountsMatch(&actual->source_counts, &expected->source_counts) &&
		HullMatches(&actual->standing_hull, &expected->standing_hull) &&
		HullMatches(&actual->crouching_hull, &expected->crouching_hull) &&
		PhysicsMatches(&actual->physics, &expected->physics);
}

static int IdentityValid(const sg_rune_compact_identity_t *identity)
{
	return identity && Sha256Present(identity->bsp_sha256) &&
		identity->bsp_bytes != 0U && identity->entity_semantics_id != 0U &&
		identity->physics_abi_id != 0U && identity->collision_law_id != 0U &&
		identity->pmove_law_id != 0U && identity->gravity_law_id != 0U &&
		identity->hook_law_id != 0U && identity->mechanism_law_id != 0U &&
		identity->weapon_law_id != 0U && identity->construction_id != 0U &&
		identity->schema_id != 0U && identity->producer_identity != 0U &&
		SourceCountsValid(&identity->source_counts) &&
		HullValid(&identity->standing_hull) &&
		HullValid(&identity->crouching_hull) && PhysicsValid(&identity->physics);
}

static int StancesValid(sg_rune_stance_validity_t stances)
{
	return stances != 0U &&
		(stances & (sg_rune_stance_validity_t)~SG_RUNE_STANCE_VALID_ALL) == 0U;
}

static int CellSourceValid(const sg_rune_compact_cell_source_t *source,
	const sg_rune_compact_source_counts_t *counts)
{
	return source && source->model < counts->model_count &&
		source->leaf < counts->leaf_count && source->area < counts->area_count &&
		source->cluster >= -1 &&
		source->split_ordinal != UINT32_MAX;
}

static int CellSourceCompare(const sg_rune_compact_cell_source_t *left,
	const sg_rune_compact_cell_source_t *right)
{
	int comparison = CompareU32(left->model, right->model);

	if (comparison == 0)
		comparison = CompareU32(left->leaf, right->leaf);
	if (comparison == 0)
		comparison = CompareU32(left->area, right->area);
	if (comparison == 0)
		comparison = CompareI32(left->cluster, right->cluster);
	if (comparison == 0)
		comparison = CompareU32(left->split_ordinal, right->split_ordinal);
	return comparison;
}

static int SourceValid(const sg_rune_compact_source_t *source,
	uint32_t split_parent_limit,
	const sg_rune_compact_source_counts_t *counts)
{
	if (!source || source->kind < 0 ||
		source->kind >= SG_RUNE_COMPACT_SOURCE_KIND_COUNT)
		return 0;
	switch (source->kind) {
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		return source->value.domain.axis < 3U &&
			source->value.domain.maximum_side < 2U;
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		return source->value.bsp_plane.model < counts->model_count &&
			source->value.bsp_plane.leaf < counts->leaf_count &&
			source->value.bsp_plane.plane < counts->plane_count;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		return source->value.brush_side.model < counts->model_count &&
			source->value.brush_side.brush < counts->brush_count &&
			source->value.brush_side.brush_side < counts->brush_side_count &&
			source->value.brush_side.plane < counts->plane_count;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		return source->value.split.parent_facet.value < split_parent_limit &&
			source->value.split.ordinal != UINT32_MAX;
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int SourceCompare(const sg_rune_compact_source_t *left,
	const sg_rune_compact_source_t *right)
{
	int comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);

	if (comparison != 0)
		return comparison;
	switch (left->kind) {
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		comparison = CompareU32(left->value.domain.axis,
			right->value.domain.axis);
		return comparison != 0 ? comparison :
			CompareU32(left->value.domain.maximum_side,
				right->value.domain.maximum_side);
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		comparison = CompareU32(left->value.bsp_plane.model,
			right->value.bsp_plane.model);
		if (comparison == 0)
			comparison = CompareU32(left->value.bsp_plane.leaf,
				right->value.bsp_plane.leaf);
		if (comparison == 0)
			comparison = CompareU32(left->value.bsp_plane.plane,
				right->value.bsp_plane.plane);
		return comparison;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		comparison = CompareU32(left->value.brush_side.model,
			right->value.brush_side.model);
		if (comparison == 0)
			comparison = CompareU32(left->value.brush_side.brush,
				right->value.brush_side.brush);
		if (comparison == 0)
			comparison = CompareU32(left->value.brush_side.brush_side,
				right->value.brush_side.brush_side);
		if (comparison == 0)
			comparison = CompareU32(left->value.brush_side.plane,
				right->value.brush_side.plane);
		return comparison;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		comparison = CompareU32(left->value.split.parent_facet.value,
			right->value.split.parent_facet.value);
		return comparison != 0 ? comparison :
			CompareU32(left->value.split.ordinal,
				right->value.split.ordinal);
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int FacetCompare(const sg_rune_compact_model_t *model,
	const sg_rune_compact_facet_t *left,
	const sg_rune_compact_facet_t *right)
{
	int comparison = SourceCompare(&left->source, &right->source);

	if (comparison == 0)
		comparison = PlaneCompare(&left->plane, &right->plane);
	if (comparison == 0)
		comparison = CompareU32(left->vertices.first, right->vertices.first);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0 &&
		left->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY)
		comparison = CompareU32(
			model->incidences[left->incidences.first].cell.value,
			model->incidences[right->incidences.first].cell.value);
	return comparison;
}

static int PortalTouchesCell(const sg_rune_compact_model_t *model,
	uint32_t portal_index, uint32_t cell_index)
{
	const sg_rune_compact_portal_t *portal;

	if (portal_index >= model->portal_count)
		return 0;
	portal = &model->portals[portal_index];
	if (portal->negative_incidence.value >= model->incidence_count ||
		portal->positive_incidence.value >= model->incidence_count)
		return 0;
	return model->incidences[portal->negative_incidence.value].cell.value ==
			cell_index ||
		model->incidences[portal->positive_incidence.value].cell.value ==
			cell_index;
}

static int ValidateCounts(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	if (model->analytic == NULL || model->static_data == NULL) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (model->cell_count == 0U || model->facet_count == 0U ||
		model->incidence_count == 0U || model->cell_incidence_count == 0U ||
		model->movement_field_count == 0U ||
		model->weapon_region_count == 0U || model->weapon_profile_count == 0U ||
		model->weapon_kernel_count == 0U ||
		model->cell_count > SG_RUNE_COMPACT_MAX_CELLS ||
		model->facet_count > SG_RUNE_COMPACT_MAX_FACETS ||
		model->incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		model->cell_incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		model->vertex_count > SG_RUNE_COMPACT_MAX_VERTICES ||
		model->portal_count > SG_RUNE_COMPACT_MAX_PORTALS ||
		model->movement_field_count > SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS ||
		model->weapon_region_count > SG_RUNE_COMPACT_MAX_WEAPON_REGIONS ||
		model->weapon_profile_count > SG_RUNE_COMPACT_MAX_WEAPON_PROFILES ||
		model->weapon_kernel_count > SG_RUNE_COMPACT_MAX_WEAPON_KERNELS ||
		model->analytic_function_ref_count >
			SG_RUNE_COMPACT_MAX_ANALYTIC_FUNCTION_REFS) {
		SetError(error, SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!ArrayPresent(model->cells, model->cell_count) ||
		!ArrayPresent(model->facets, model->facet_count) ||
		!ArrayPresent(model->incidences, model->incidence_count) ||
		!ArrayPresent(model->cell_incidences, model->cell_incidence_count) ||
		!ArrayPresent(model->vertices, model->vertex_count) ||
		!ArrayPresent(model->portals, model->portal_count) ||
		!ArrayPresent(model->movement_fields, model->movement_field_count) ||
		!ArrayPresent(model->weapon_regions, model->weapon_region_count) ||
		!ArrayPresent(model->weapon_profiles, model->weapon_profile_count) ||
		!ArrayPresent(model->weapon_kernels, model->weapon_kernel_count) ||
		!ArrayPresent(model->analytic_function_refs,
			model->analytic_function_ref_count)) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidateAnalyticUses(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t *uses;
	uint32_t index;

	uses = (uint32_t *)calloc(model->analytic->function_count, sizeof(*uses));
	if (uses == NULL) {
		SetError(error, SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	for (index = 0U; index < model->analytic_function_ref_count; index++) {
		const uint32_t function = model->analytic_function_refs[index].value;

		if (function >= model->analytic->function_count) {
			free(uses);
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
				SG_RUNE_COMPACT_RECORD_MODEL, index);
			return 0;
		}
		uses[function]++;
	}
	for (index = 0U; index < model->analytic->piecewise_count; index++) {
		const sg_rune_analytic_piecewise_t *piecewise =
			&model->analytic->piecewise[index];
		uint32_t clause;

		uses[piecewise->default_function.value]++;
		for (clause = piecewise->clauses.first;
			clause < piecewise->clauses.first + piecewise->clauses.count;
			clause++)
			uses[model->analytic->piecewise_clauses[clause].function.value]++;
	}
	for (index = 0U; index < model->analytic->function_count; index++) {
		if (uses[index] == 0U) {
			free(uses);
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_MODEL, index);
			return 0;
		}
	}
	free(uses);
	return 1;
}

static int ValidateCells(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t cell_index;
	uint32_t incidence_cursor = 0U;
	uint32_t movement_cursor = 0U;
	uint32_t weapon_cursor = 0U;

	for (cell_index = 0U; cell_index < model->cell_count; cell_index++) {
		const sg_rune_compact_cell_t *cell = &model->cells[cell_index];
		uint32_t local;

		if (!CellSourceValid(&cell->source, &model->identity.source_counts)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (cell_index != 0U && CellSourceCompare(
				&model->cells[cell_index - 1U].source, &cell->source) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (!BoundsValid(&cell->bounds)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (!StancesValid(cell->valid_stances)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_STANCE,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (!ReservedBytesZero(cell->reserved)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if ((cell->contents &
				~(sg_rune_compact_contents_mask_t)
					SG_RUNE_COMPACT_CONTENTS_KNOWN) != 0U ||
			(cell->semantics &
				~(sg_rune_compact_cell_semantics_t)
					SG_RUNE_COMPACT_CELL_SEMANTICS_KNOWN) != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (cell->incidences.count == 0U || cell->movement_fields.count == 0U ||
			cell->weapon_regions.count == 0U ||
			cell->incidences.first != incidence_cursor ||
			!SpanWithin(cell->incidences.first, cell->incidences.count,
				model->cell_incidence_count) ||
			cell->movement_fields.first != movement_cursor ||
			!SpanWithin(cell->movement_fields.first, cell->movement_fields.count,
				model->movement_field_count) ||
			cell->weapon_regions.first != weapon_cursor ||
			!SpanWithin(cell->weapon_regions.first, cell->weapon_regions.count,
				model->weapon_region_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		for (local = 0U; local < cell->incidences.count; local++) {
			const uint32_t reference = cell->incidences.first + local;
			const uint32_t incidence = model->cell_incidences[reference].value;

			if (incidence >= model->incidence_count ||
				model->incidences[incidence].cell.value != cell_index ||
				model->incidences[incidence].cell_ordinal != local) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_CELL, cell_index);
				return 0;
			}
		}
		incidence_cursor += cell->incidences.count;
		movement_cursor += cell->movement_fields.count;
		weapon_cursor += cell->weapon_regions.count;
	}
	if (incidence_cursor != model->cell_incidence_count ||
		incidence_cursor != model->incidence_count ||
		movement_cursor != model->movement_field_count ||
		weapon_cursor != model->weapon_region_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidateFacetsAndIncidences(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t facet_index;
	uint32_t incidence_cursor = 0U;
	uint32_t vertex_cursor = 0U;

	for (facet_index = 0U; facet_index < model->facet_count; facet_index++) {
		const sg_rune_compact_facet_t *facet = &model->facets[facet_index];
		uint32_t incidence_index;

		if (!SourceValid(&facet->source, facet_index,
			&model->identity.source_counts)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		if (facet->kind < 0 || facet->kind >= SG_RUNE_COMPACT_FACET_KIND_COUNT ||
			!PlaneValid(&facet->plane) ||
			facet->vertices.first != vertex_cursor ||
			!SpanWithin(facet->vertices.first, facet->vertices.count,
				model->vertex_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		if (facet->kind == SG_RUNE_COMPACT_FACET_POLYGON) {
			facet_polygon_result_t polygon_result;

			if (facet->vertices.count < 3U) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
			polygon_result = ValidateFacetPolygon(model, facet);
			if (polygon_result != FACET_POLYGON_VALID) {
				SetError(error,
					polygon_result == FACET_POLYGON_NONCANONICAL ?
						SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER :
						SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
		} else if (facet->vertices.count != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		if (facet->incidences.first != incidence_cursor ||
			(facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY ?
				facet->incidences.count != 1U :
				(facet->incidences.count != 1U &&
				 facet->incidences.count != 2U)) ||
			!SpanWithin(facet->incidences.first, facet->incidences.count,
				model->incidence_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		for (incidence_index = facet->incidences.first;
			incidence_index < facet->incidences.first + facet->incidences.count;
			incidence_index++) {
			const sg_rune_compact_incidence_t *incidence =
				&model->incidences[incidence_index];

			if (incidence->cell.value >= model->cell_count ||
				incidence->facet.value != facet_index ||
				incidence->side < 0 ||
				incidence->side >= SG_RUNE_FACET_SIDE_COUNT ||
				incidence->boundary < 0 ||
				incidence->boundary >= SG_RUNE_BOUNDARY_OWNERSHIP_COUNT ||
				incidence->cell_ordinal >=
					model->cells[incidence->cell.value].incidences.count ||
				model->cell_incidences[
					model->cells[incidence->cell.value].incidences.first +
					incidence->cell_ordinal].value != incidence_index) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_INCIDENCE, incidence_index);
				return 0;
			}
			if (incidence_index != facet->incidences.first) {
				const sg_rune_compact_incidence_t *previous = incidence - 1;

				if (previous->side > incidence->side ||
					(previous->side == incidence->side &&
					 previous->cell.value >= incidence->cell.value)) {
					SetError(error,
						SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
						SG_RUNE_COMPACT_RECORD_INCIDENCE,
						incidence_index);
					return 0;
				}
			}
		}
		if (facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY ||
			facet->incidences.count == 1U) {
			if (facet->portal.value != SG_RUNE_COMPACT_INDEX_NONE) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
		} else {
			const sg_rune_compact_incidence_t *negative =
				&model->incidences[facet->incidences.first];
			const sg_rune_compact_incidence_t *positive = negative + 1;

			if (facet->portal.value >= model->portal_count ||
				model->portals[facet->portal.value].facet.value != facet_index ||
				negative->cell.value == positive->cell.value ||
				negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
				positive->side != SG_RUNE_FACET_POSITIVE_SIDE ||
				negative->boundary == positive->boundary) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
		}
		if (facet_index != 0U && FacetCompare(model,
				&model->facets[facet_index - 1U], facet) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		vertex_cursor += facet->vertices.count;
		incidence_cursor += facet->incidences.count;
	}
	if (vertex_cursor != model->vertex_count ||
		incidence_cursor != model->incidence_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidatePortals(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t portal_index;

	for (portal_index = 0U; portal_index < model->portal_count; portal_index++) {
		const sg_rune_compact_portal_t *portal = &model->portals[portal_index];
		const sg_rune_compact_facet_t *facet;
		const sg_rune_compact_incidence_t *negative;
		const sg_rune_compact_incidence_t *positive;
		sg_rune_stance_validity_t shared_stances;

		if (!SourceValid(&portal->source, model->facet_count,
			&model->identity.source_counts)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (portal->facet.value >= model->facet_count ||
			portal->negative_incidence.value >= model->incidence_count ||
			portal->positive_incidence.value >= model->incidence_count) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (portal_index != 0U &&
			model->portals[portal_index - 1U].facet.value >= portal->facet.value) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		facet = &model->facets[portal->facet.value];
		negative = &model->incidences[portal->negative_incidence.value];
		positive = &model->incidences[portal->positive_incidence.value];
		if (facet->portal.value != portal_index || facet->incidences.count != 2U ||
			negative->facet.value != portal->facet.value ||
			positive->facet.value != portal->facet.value ||
			negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
			positive->side != SG_RUNE_FACET_POSITIVE_SIDE ||
			negative->boundary == positive->boundary ||
			portal->direction < 0 ||
			portal->direction >= SG_RUNE_PORTAL_CONTINUITY_COUNT ||
			portal->clearance_q8 == 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		shared_stances = (sg_rune_stance_validity_t)(
			model->cells[negative->cell.value].valid_stances &
			model->cells[positive->cell.value].valid_stances);
		if (!StancesValid(portal->valid_stances) ||
			(portal->valid_stances & shared_stances) != portal->valid_stances) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_STANCE,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (!ReservedBytesZero(portal->reserved)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
	}
	return 1;
}

static int MovementFieldCompare(const sg_rune_movement_field_attachment_t *left,
	const sg_rune_movement_field_attachment_t *right)
{
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->boundary_portal.value,
			right->boundary_portal.value);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->family, (uint32_t)right->family);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->valid_stances,
			(uint32_t)right->valid_stances);
	if (comparison == 0)
		comparison = CompareU32(left->functions.first, right->functions.first);
	if (comparison == 0)
		comparison = CompareU32(left->functions.count, right->functions.count);
	return comparison;
}

static int MovementFieldsShareDomain(
	const sg_rune_movement_field_attachment_t *left,
	const sg_rune_movement_field_attachment_t *right)
{
	return left->cell.value == right->cell.value &&
		left->boundary_portal.value == right->boundary_portal.value &&
		left->family == right->family;
}

static int MovementOutputValid(sg_rune_analytic_output_meaning_t output)
{
	return output == SG_RUNE_ANALYTIC_OUTPUT_COST ||
		output == SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS ||
		(output >= SG_RUNE_ANALYTIC_OUTPUT_POSITION_X &&
		 output <= SG_RUNE_ANALYTIC_OUTPUT_ACCELERATION_Z) ||
		output == SG_RUNE_ANALYTIC_OUTPUT_CLEARANCE ||
		output == SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
}

static int ValidateMovementFields(const sg_rune_compact_model_t *model,
	uint32_t *function_cursor, sg_rune_compact_error_t *error)
{
	uint32_t field_index;
	uint32_t cell_index;

	for (field_index = 0U; field_index < model->movement_field_count;
		field_index++) {
		const sg_rune_movement_field_attachment_t *field =
			&model->movement_fields[field_index];
		uint32_t output_offset;
		int has_cost = 0;
		int has_time = 0;
		int has_reachability = 0;

		if (field_index != 0U && MovementFieldCompare(
				&model->movement_fields[field_index - 1U], field) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
			return 0;
		}
		if (field_index != 0U && MovementFieldsShareDomain(
				&model->movement_fields[field_index - 1U], field) &&
			(model->movement_fields[field_index - 1U].valid_stances &
			 field->valid_stances) != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_STANCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
			return 0;
		}
		if (field->cell.value >= model->cell_count ||
			field_index < model->cells[field->cell.value].movement_fields.first ||
			field_index >= model->cells[field->cell.value].movement_fields.first +
				model->cells[field->cell.value].movement_fields.count ||
			(field->boundary_portal.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 !PortalTouchesCell(model, field->boundary_portal.value,
				 field->cell.value))) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
			return 0;
		}
		if (!ReservedBytesZero(field->reserved)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
			return 0;
		}
		if (field->family < 0 ||
			field->family >= SG_RUNE_MOVEMENT_FIELD_FAMILY_COUNT ||
			!StancesValid(field->valid_stances) ||
			(field->valid_stances &
			 model->cells[field->cell.value].valid_stances) != field->valid_stances ||
			(field->boundary_portal.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 (field->valid_stances & model->portals[
				field->boundary_portal.value].valid_stances) !=
				field->valid_stances) ||
			field->functions.first != *function_cursor ||
			field->functions.count == 0U ||
			!SpanWithin(field->functions.first, field->functions.count,
				model->analytic_function_ref_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
			return 0;
		}
		for (output_offset = 0U; output_offset < field->functions.count;
			output_offset++) {
			const uint32_t reference = field->functions.first + output_offset;
			const uint32_t function =
				model->analytic_function_refs[reference].value;
			sg_rune_analytic_output_meaning_t output;

			if (function >= model->analytic->function_count) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
					SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
				return 0;
			}
			output = model->analytic->functions[function].output;
			if (!MovementOutputValid(output) ||
				(output_offset != 0U &&
				 model->analytic->functions[model->analytic_function_refs[
					reference - 1U].value].output >= output)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
					SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
				return 0;
			}
			has_cost |= output == SG_RUNE_ANALYTIC_OUTPUT_COST;
			has_time |= output == SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
			has_reachability |=
				output == SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
		}
		if (!has_cost || !has_time || !has_reachability) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, field_index);
			return 0;
		}
		*function_cursor += field->functions.count;
	}
	for (cell_index = 0U; cell_index < model->cell_count; cell_index++) {
		const sg_rune_compact_cell_t *cell = &model->cells[cell_index];
		sg_rune_stance_validity_t hook_stances = 0U;

		for (field_index = cell->movement_fields.first;
			field_index < cell->movement_fields.first + cell->movement_fields.count;
			field_index++)
			if (model->movement_fields[field_index].family ==
				SG_RUNE_MOVEMENT_FIELD_HOOK)
				hook_stances = (sg_rune_stance_validity_t)(hook_stances |
					model->movement_fields[field_index].valid_stances);
		if ((hook_stances & cell->valid_stances) != cell->valid_stances) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
	}
	return 1;
}

static int WeaponRegionCompare(const sg_rune_weapon_response_region_t *left,
	const sg_rune_weapon_response_region_t *right)
{
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->boundary_incidences.first,
			right->boundary_incidences.first);
	if (comparison == 0)
		comparison = CompareU32(left->boundary_incidences.count,
			right->boundary_incidences.count);
	return comparison;
}

static int WeaponKernelCompare(const sg_rune_weapon_response_kernel_t *left,
	const sg_rune_weapon_response_kernel_t *right)
{
	int comparison = CompareU32(left->region.value, right->region.value);

	if (comparison == 0)
		comparison = CompareU32(left->profile, right->profile);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->family,
			(uint32_t)right->family);
	if (comparison == 0)
		comparison = CompareU32(left->functions.first, right->functions.first);
	if (comparison == 0)
		comparison = CompareU32(left->functions.count, right->functions.count);
	return comparison;
}

static int ValidateWeaponProfiles(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	sg_rune_weapon_response_family_mask_t family_mask = 0U;
	uint32_t profile_index;

	for (profile_index = 0U; profile_index < model->weapon_profile_count;
		profile_index++) {
		const sg_rune_weapon_profile_t *profile =
			&model->weapon_profiles[profile_index];

		if (profile->source_profile == 0U ||
			profile->response_families == 0U ||
			(profile->response_families &
				~SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL) != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE, profile_index);
			return 0;
		}
		if (profile_index != 0U &&
			model->weapon_profiles[profile_index - 1U].source_profile >=
				profile->source_profile) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE, profile_index);
			return 0;
		}
		family_mask |= profile->response_families;
	}
	if (family_mask != SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

_Static_assert((uint64_t)SG_RUNE_COMPACT_MAX_WEAPON_PROFILES *
	(uint64_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT <= UINT32_MAX,
	"weapon kernels per region must fit uint32_t");

static uint32_t WeaponKernelsPerRegion(const sg_rune_compact_model_t *model)
{
	uint32_t count = 0U;
	uint32_t profile_index;

	for (profile_index = 0U; profile_index < model->weapon_profile_count;
		profile_index++) {
		sg_rune_weapon_response_family_mask_t families =
			model->weapon_profiles[profile_index].response_families;

		while (families != 0U) {
			count += families & UINT32_C(1);
			families >>= 1U;
		}
	}
	return count;
}

static int WeaponOutputValid(sg_rune_analytic_output_meaning_t output)
{
	return output == SG_RUNE_ANALYTIC_OUTPUT_COST ||
		output == SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS ||
		(output >= SG_RUNE_ANALYTIC_OUTPUT_POSITION_X &&
		 output <= SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_Z) ||
		output == SG_RUNE_ANALYTIC_OUTPUT_DAMAGE ||
		output == SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY ||
		(output >= SG_RUNE_ANALYTIC_OUTPUT_IMPULSE_X &&
		 output <= SG_RUNE_ANALYTIC_OUTPUT_IMPULSE_Z) ||
		output == SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION ||
		output == SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
}

static int ValidateWeaponFields(const sg_rune_compact_model_t *model,
	uint32_t *function_cursor, sg_rune_compact_error_t *error)
{
	uint32_t region_index;
	uint32_t kernel_cursor = 0U;
	const uint32_t kernels_per_region = WeaponKernelsPerRegion(model);

	for (region_index = 0U; region_index < model->weapon_region_count;
		region_index++) {
		const sg_rune_weapon_response_region_t *region =
			&model->weapon_regions[region_index];
		const sg_rune_compact_cell_t *cell;

		if (region_index != 0U && WeaponRegionCompare(
				&model->weapon_regions[region_index - 1U], region) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_WEAPON_REGION, region_index);
			return 0;
		}
		if (region->cell.value >= model->cell_count ||
			region->kernels.count != kernels_per_region) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_REGION, region_index);
			return 0;
		}
		cell = &model->cells[region->cell.value];
		if (region_index < cell->weapon_regions.first ||
			region_index >= cell->weapon_regions.first +
				cell->weapon_regions.count ||
			region->boundary_incidences.first < cell->incidences.first ||
			!SpanWithin(region->boundary_incidences.first,
				region->boundary_incidences.count,
				cell->incidences.first + cell->incidences.count) ||
			region->kernels.first != kernel_cursor ||
			!SpanWithin(region->kernels.first, region->kernels.count,
				model->weapon_kernel_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_REGION, region_index);
			return 0;
		}
		kernel_cursor += region->kernels.count;
	}
	if (kernel_cursor != model->weapon_kernel_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
		return 0;
	}
	kernel_cursor = 0U;

	for (region_index = 0U; region_index < model->weapon_region_count;
		region_index++) {
		uint32_t profile_index;

		for (profile_index = 0U; profile_index < model->weapon_profile_count;
			profile_index++) {
			const sg_rune_weapon_profile_t *profile =
				&model->weapon_profiles[profile_index];
			uint32_t family;

			for (family = 0U;
				family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
				family++) {
				const uint32_t family_bit =
					SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);
				const sg_rune_weapon_response_kernel_t *kernel;
				uint32_t output_offset;
				int has_damage = 0;
				int has_hit_probability = 0;
				int has_time = 0;
				int has_visibility = 0;
				int has_fuse = 0;

				if ((profile->response_families & family_bit) == 0U)
					continue;
				if (kernel_cursor >= model->weapon_kernel_count) {
					SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
					return 0;
				}
				kernel = &model->weapon_kernels[kernel_cursor];
				if (kernel_cursor != 0U && WeaponKernelCompare(
						&model->weapon_kernels[kernel_cursor - 1U], kernel) >= 0) {
					SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
						SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
					return 0;
				}
				if (kernel->region.value != region_index ||
					kernel->profile != profile_index ||
					(uint32_t)kernel->family != family) {
					SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
					return 0;
				}
				if (kernel->functions.first != *function_cursor ||
					kernel->functions.count == 0U ||
					!SpanWithin(kernel->functions.first, kernel->functions.count,
						model->analytic_function_ref_count)) {
					SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
						SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
					return 0;
				}
				for (output_offset = 0U;
					output_offset < kernel->functions.count; output_offset++) {
					const uint32_t reference =
						kernel->functions.first + output_offset;
					const uint32_t function =
						model->analytic_function_refs[reference].value;
					sg_rune_analytic_output_meaning_t output;

					if (function >= model->analytic->function_count) {
						SetError(error,
							SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
							SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL,
							kernel_cursor);
						return 0;
					}
					output = model->analytic->functions[function].output;
					if (!WeaponOutputValid(output) ||
						(output_offset != 0U &&
						 model->analytic->functions[
							model->analytic_function_refs[
								reference - 1U].value].output >= output)) {
						SetError(error,
							SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
							SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL,
							kernel_cursor);
						return 0;
					}
					has_time |= output ==
						SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
					has_damage |= output == SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
					has_hit_probability |= output ==
						SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY;
					has_visibility |= output ==
						SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION;
					has_fuse |= output ==
						SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
				}
				if (!has_time || !has_damage || !has_hit_probability ||
					!has_visibility ||
					((family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT ||
					  family == SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE) &&
					 !has_fuse)) {
					SetError(error,
						SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
						SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
					return 0;
				}
				*function_cursor += kernel->functions.count;
				kernel_cursor++;
			}
		}
	}
	if (kernel_cursor != model->weapon_kernel_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
		return 0;
	}
	return 1;
}

int SG_RuneCompactModelValidate(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error_out)
{
	sg_rune_analytic_error_t analytic_error;
	sg_rune_compact_static_error_t static_error;
	uint32_t function_cursor = 0U;

	SetError(error_out, SG_RUNE_COMPACT_ERROR_NONE,
		SG_RUNE_COMPACT_RECORD_MODEL, 0U);
	if (!model) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (model->version != SG_RUNE_COMPACT_MODEL_VERSION ||
		model->schema_tag != SG_RUNE_COMPACT_MODEL_SCHEMA_TAG ||
		model->reserved != 0U) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_UNSUPPORTED_VERSION,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!IdentityValid(&model->identity)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!ValidateCounts(model, error_out))
		return 0;
	if (!SG_RuneCompactAnalyticValidate(model->analytic, &analytic_error)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
			SG_RUNE_COMPACT_RECORD_MODEL, analytic_error.record);
		return 0;
	}
	if (!ValidateAnalyticUses(model, error_out))
		return 0;
	if (!ValidateCells(model, error_out) ||
		!ValidateFacetsAndIncidences(model, error_out) ||
		!ValidatePortals(model, error_out) ||
		!ValidateMovementFields(model, &function_cursor, error_out) ||
		!ValidateWeaponProfiles(model, error_out) ||
		!ValidateWeaponFields(model, &function_cursor, error_out))
		return 0;
	if (function_cursor != model->analytic_function_ref_count) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
			SG_RUNE_COMPACT_RECORD_MODEL, function_cursor);
		return 0;
	}
	if (!SG_RuneCompactStaticValidate(model, model->static_data,
		&static_error)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_STATIC_DATA,
			SG_RUNE_COMPACT_RECORD_MODEL, static_error.record);
		return 0;
	}
	return 1;
}

int SG_RuneCompactModelValidateBound(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out)
{
	if (!expected_identity) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!SG_RuneCompactModelValidate(model, error_out))
		return 0;
	if (!SG_RuneCompactIdentityMatches(&model->identity, expected_identity)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

const char *SG_RuneCompactModelErrorString(sg_rune_compact_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_ERROR_UNSUPPORTED_VERSION:
		return "unsupported version";
	case SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED:
		return "nonzero reserved field";
	case SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER:
		return "noncanonical order";
	case SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE:
		return "invalid provenance";
	case SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY:
		return "invalid geometry";
	case SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY:
		return "invalid topology";
	case SG_RUNE_COMPACT_ERROR_INVALID_STANCE:
		return "invalid stance";
	case SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD:
		return "invalid analytic field";
	case SG_RUNE_COMPACT_ERROR_INVALID_STATIC_DATA:
		return "invalid static data";
	case SG_RUNE_COMPACT_ERROR_CODE_COUNT:
		break;
	}
	return "unknown compact RUNE model error";
}
