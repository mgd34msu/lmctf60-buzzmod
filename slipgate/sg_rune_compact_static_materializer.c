#include "sg_rune_compact_static_materializer.h"
#include "sg_rune_compact_mechanisms.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert((int)SG_RUNE_MECHANISM_DOOR ==
	(int)SG_RUNE_COMPACT_MECHANISM_DOOR,
	"compact mechanism ordering must match entity semantics");
_Static_assert((int)SG_RUNE_MECHANISM_ROTATOR ==
	(int)SG_RUNE_COMPACT_MECHANISM_ROTATOR,
	"compact mechanism ordering must match entity semantics");

/* This is the builder's existing compact identity derivation.  It is used
 * only to bind the legacy configuration/visibility identity to the compact
 * BSP digest; it does not create or publish any new artifact hash. */
#define MATERIALIZER_IDENTITY_HASH_OFFSET UINT64_C(14695981039346656037)
#define MATERIALIZER_IDENTITY_HASH_PRIME UINT64_C(1099511628211)

static uint64_t IdentityHashBytes(uint64_t hash, const uint8_t *bytes,
	size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
		hash = (hash ^ (uint64_t)bytes[index]) *
			MATERIALIZER_IDENTITY_HASH_PRIME;
	return hash;
}

static uint64_t DeriveBspContentId(const uint8_t digest[32])
{
	static const char domain[] = "lmctf.compact.bsp-content.v1";
	uint64_t hash = IdentityHashBytes(MATERIALIZER_IDENTITY_HASH_OFFSET,
		(const uint8_t *)domain, sizeof(domain) - 1U);

	hash = IdentityHashBytes(hash, digest, 32U);
	if (hash == 0U)
		return UINT64_C(1);
	if (hash == UINT64_MAX)
		return UINT64_MAX - UINT64_C(1);
	return hash;
}

static int Binary32CanonicalFinite(uint32_t bits)
{
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent != UINT32_C(0xff) && bits != UINT32_C(0x80000000);
}

/* Host AngleVectors preserves the sign bit of a zero component.  Launch
 * vectors therefore accept either signed zero while retaining the exact
 * source bits; the canonical predicate above remains in force elsewhere. */
static int Binary32FiniteAllowSignedZero(uint32_t bits)
{
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent != UINT32_C(0xff);
}

static int Binary32Nonnegative(uint32_t bits)
{
	return Binary32CanonicalFinite(bits) &&
		(bits & UINT32_C(0x80000000)) == 0U;
}

static int CompactIdentityValid(const sg_rune_compact_identity_t *identity)
{
	uint32_t index;
	int digest_present = 0;
	uint32_t physics[8];

	if (identity == NULL)
		return 0;
	physics[0] = identity->physics.gravity_bits;
	physics[1] = identity->physics.ground_acceleration_bits;
	physics[2] = identity->physics.air_acceleration_bits;
	physics[3] = identity->physics.water_acceleration_bits;
	physics[4] = identity->physics.hook_acceleration_bits;
	physics[5] = identity->physics.external_acceleration_bits;
	physics[6] = identity->physics.water_drag_bits;
	physics[7] = identity->physics.max_velocity_bits;
	for (index = 0U; index < 32U; index++)
		if (identity->bsp_sha256[index] != 0U)
			digest_present = 1;
	if (!digest_present || identity->bsp_bytes == 0U ||
		identity->entity_semantics_id == 0U || identity->physics_abi_id == 0U ||
		identity->collision_law_id == 0U || identity->pmove_law_id == 0U ||
		identity->gravity_law_id == 0U || identity->hook_law_id == 0U ||
		identity->mechanism_law_id == 0U || identity->weapon_law_id == 0U ||
		identity->construction_id == 0U || identity->schema_id == 0U ||
		identity->producer_identity == 0U ||
		identity->source_counts.model_count == 0U ||
		identity->source_counts.leaf_count == 0U ||
		identity->source_counts.area_count == 0U ||
		identity->source_counts.plane_count == 0U ||
		identity->source_counts.entity_count == 0U ||
		(identity->source_counts.brush_count == 0U) !=
			(identity->source_counts.brush_side_count == 0U))
		return 0;
	for (index = 0U; index < 3U; index++)
		if (identity->standing_hull.mins.value[index] >=
				identity->standing_hull.maxs.value[index] ||
			identity->crouching_hull.mins.value[index] >=
				identity->crouching_hull.maxs.value[index])
			return 0;
	for (index = 0U; index < sizeof(physics) / sizeof(physics[0]); index++)
		if (!Binary32Nonnegative(physics[index]))
			return 0;
	return Binary32Nonnegative(identity->physics.gravity_bits) &&
		identity->physics.gravity_bits != 0U &&
		Binary32Nonnegative(identity->physics.max_velocity_bits) &&
		identity->physics.max_velocity_bits != 0U &&
		identity->physics.frame_ms != 0U &&
		identity->physics.substep_ms != 0U &&
		identity->physics.substep_ms <= identity->physics.frame_ms;
}

#if defined(SG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING)
static size_t test_fail_after = SIZE_MAX;
static size_t test_allocation_count;

void SG_RuneCompactStaticMaterializerTestFailAfter(size_t allocation)
{
	test_fail_after = allocation;
	test_allocation_count = 0U;
}

size_t SG_RuneCompactStaticMaterializerTestAllocationCount(void)
{
	return test_allocation_count;
}
#endif

static void SetError(sg_rune_compact_static_materializer_error_t *error,
	sg_rune_compact_static_materializer_error_code_t code,
	sg_rune_compact_static_materializer_record_domain_t domain,
	uint32_t record)
{
	if (error == NULL)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

/* Compact fragments are identity-bound, but the identity contains arrays and
 * floating-point bit fields whose structure padding must not participate in a
 * binding decision. */
static int CompactIdentityEqual(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	uint32_t index;

	if (left == NULL || right == NULL)
		return 0;
	for (index = 0U; index < 32U; index++)
		if (left->bsp_sha256[index] != right->bsp_sha256[index])
			return 0;
	if (left->bsp_bytes != right->bsp_bytes ||
		left->bsp_checksum != right->bsp_checksum ||
		left->entity_crc32 != right->entity_crc32 ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->collision_law_id != right->collision_law_id ||
		left->pmove_law_id != right->pmove_law_id ||
		left->gravity_law_id != right->gravity_law_id ||
		left->hook_law_id != right->hook_law_id ||
		left->mechanism_law_id != right->mechanism_law_id ||
		left->weapon_law_id != right->weapon_law_id ||
		left->construction_id != right->construction_id ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity ||
		left->source_counts.model_count != right->source_counts.model_count ||
		left->source_counts.leaf_count != right->source_counts.leaf_count ||
		left->source_counts.area_count != right->source_counts.area_count ||
		left->source_counts.plane_count != right->source_counts.plane_count ||
		left->source_counts.entity_count != right->source_counts.entity_count ||
		left->source_counts.brush_count != right->source_counts.brush_count ||
		left->source_counts.brush_side_count !=
			right->source_counts.brush_side_count ||
		left->physics.frame_ms != right->physics.frame_ms ||
		left->physics.substep_ms != right->physics.substep_ms)
		return 0;
	for (index = 0U; index < 3U; index++)
	{
		if (left->standing_hull.mins.value[index] !=
			right->standing_hull.mins.value[index] ||
			left->standing_hull.maxs.value[index] !=
				right->standing_hull.maxs.value[index] ||
			left->crouching_hull.mins.value[index] !=
				right->crouching_hull.mins.value[index] ||
			left->crouching_hull.maxs.value[index] !=
			 right->crouching_hull.maxs.value[index])
			return 0;
	}
	return left->physics.gravity_bits == right->physics.gravity_bits &&
		left->physics.ground_acceleration_bits ==
			right->physics.ground_acceleration_bits &&
		left->physics.air_acceleration_bits ==
			right->physics.air_acceleration_bits &&
		left->physics.water_acceleration_bits ==
			right->physics.water_acceleration_bits &&
		left->physics.hook_acceleration_bits ==
			right->physics.hook_acceleration_bits &&
		left->physics.external_acceleration_bits ==
			right->physics.external_acceleration_bits &&
		left->physics.water_drag_bits == right->physics.water_drag_bits &&
		left->physics.max_velocity_bits == right->physics.max_velocity_bits;
}

static void *MaterializerCalloc(size_t count, size_t size)
{
#if defined(SG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING)
	if (test_allocation_count == test_fail_after)
	{
		test_allocation_count++;
		return NULL;
	}
	test_allocation_count++;
#endif
	return calloc(count, size);
}

static int SizeMultiply(size_t left, size_t right, size_t *result)
{
	if (result == NULL || (right != 0U && left > SIZE_MAX / right))
		return 0;
	*result = left * right;
	return 1;
}

static int CountAdd(uint32_t left, uint32_t right, uint32_t *result)
{
	if (result == NULL || left > UINT32_MAX - right)
		return 0;
	*result = left + right;
	return 1;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int FloatBitsEqual(float left, float right)
{
	return FloatBits(left) == FloatBits(right);
}

static int Finite3(const float value[3])
{
	return value != NULL && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int FloatBoundsValid(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	if (bounds == NULL || !Finite3(bounds->mins.value) ||
		!Finite3(bounds->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int BoundsValidQ8(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (bounds == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PointInBoundsQ8(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (point == NULL || !BoundsValidQ8(bounds))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int BoundsOverlapQ8(const sg_rune_q8_bounds_t *left,
	const sg_rune_q8_bounds_t *right)
{
	uint32_t axis;

	if (!BoundsValidQ8(left) || !BoundsValidQ8(right))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->maxs.value[axis] <= right->mins.value[axis] ||
			left->mins.value[axis] >= right->maxs.value[axis])
			return 0;
	return 1;
}

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int PlaneValue(const sg_rune_binary32_plane_t *plane,
	const double point[3], double *value_out)
{
	double value;
	uint32_t axis;

	if (plane == NULL || point == NULL || value_out == NULL)
		return 0;
	value = -(double)BitsFloat(plane->distance_bits);
	for (axis = 0U; axis < 3U; axis++)
		value += (double)BitsFloat(plane->normal_bits[axis]) * point[axis];
	if (!isfinite(value))
		return 0;
	*value_out = value;
	return 1;
}

static int CellContainsPointDouble(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t cell_index, const double point[3])
{
	const sg_rune_compact_cell_t *cell;
	uint32_t local;

	if (geometry == NULL || point == NULL || cell_index >= geometry->cell_count)
		return 0;
	cell = &geometry->cells[cell_index];
	for (local = 0U; local < 3U; local++)
		if (point[local] < (double)cell->bounds.mins.value[local] / 8.0 ||
			point[local] >= (double)cell->bounds.maxs.value[local] / 8.0)
			return 0;
	for (local = 0U; local < cell->incidences.count; local++)
	{
		const uint32_t incidence_index = cell->incidences.first + local;
		const sg_rune_compact_incidence_t *incidence;
		double value;

		if (incidence_index >= geometry->cell_incidence_count)
			return 0;
		if (geometry->cell_incidences[incidence_index].value >=
			geometry->incidence_count)
			return 0;
		incidence = &geometry->incidences[
			geometry->cell_incidences[incidence_index].value];
		if (incidence->facet.value >= geometry->facet_count ||
			!PlaneValue(&geometry->facets[incidence->facet.value].plane,
				point, &value))
			return 0;
		if (incidence->side == SG_RUNE_FACET_NEGATIVE_SIDE)
		{
			if (incidence->boundary == SG_RUNE_BOUNDARY_CLOSED)
			{
				if (value > 1.0e-7)
					return 0;
			}
			else if (value >= -1.0e-7)
				return 0;
		}
		else
		{
			if (incidence->boundary == SG_RUNE_BOUNDARY_CLOSED)
			{
				if (value < -1.0e-7)
					return 0;
			}
			else if (value <= 1.0e-7)
				return 0;
		}
	}
	return 1;
}

static int CellContainsPointQ8(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t cell_index, const sg_rune_q8_vec3_t *point)
{
	double coordinates[3];
	uint32_t axis;

	if (point == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		coordinates[axis] = (double)point->value[axis] / 8.0;
	return CellContainsPointDouble(geometry, cell_index, coordinates);
}

static int CellConstraintPlane(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t cell_index, uint32_t constraint, double normal[3],
	double *distance_out)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t axis;

	if (geometry == NULL || normal == NULL || distance_out == NULL ||
		cell_index >= geometry->cell_count)
		return 0;
	cell = &geometry->cells[cell_index];
	if (constraint < cell->incidences.count)
	{
		const uint32_t reference = cell->incidences.first + constraint;
		uint32_t incidence_index;
		const sg_rune_compact_incidence_t *incidence =
			NULL;
		const sg_rune_binary32_plane_t *plane;

		if (reference >= geometry->cell_incidence_count)
			return 0;
		incidence_index = geometry->cell_incidences[reference].value;
		if (incidence_index >= geometry->incidence_count)
			return 0;
		incidence = &geometry->incidences[incidence_index];
		if (incidence->facet.value >= geometry->facet_count ||
			incidence->cell.value != cell_index)
			return 0;
		plane = &geometry->facets[incidence->facet.value].plane;
		for (axis = 0U; axis < 3U; axis++)
			normal[axis] = (double)BitsFloat(plane->normal_bits[axis]);
		*distance_out = (double)BitsFloat(plane->distance_bits);
		return 1;
	}
	constraint -= cell->incidences.count;
	if (constraint >= 6U)
		return 0;
	axis = constraint / 2U;
	memset(normal, 0, 3U * sizeof(*normal));
	normal[axis] = 1.0;
	*distance_out = (double)(constraint % 2U == 0U ?
		cell->bounds.mins.value[axis] : cell->bounds.maxs.value[axis]) / 8.0;
	return 1;
}

static int IntersectionConstraintPlane(
	const sg_rune_compact_static_geometry_view_t *geometry, uint32_t cell_index,
	const sg_rune_q8_bounds_t *bounds, uint32_t constraint, double normal[3],
	double *distance_out)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t cell_constraint_count;
	uint32_t axis;

	if (geometry == NULL || bounds == NULL || normal == NULL ||
		distance_out == NULL || cell_index >= geometry->cell_count)
		return 0;
	cell = &geometry->cells[cell_index];
	if (!CountAdd(cell->incidences.count, 6U, &cell_constraint_count))
		return 0;
	if (constraint < cell_constraint_count)
		return CellConstraintPlane(geometry, cell_index, constraint, normal,
			distance_out);
	constraint -= cell_constraint_count;
	if (constraint >= 6U)
		return 0;
	axis = constraint / 2U;
	memset(normal, 0, 3U * sizeof(*normal));
	normal[axis] = 1.0;
	*distance_out = (double)(constraint % 2U == 0U ?
		bounds->mins.value[axis] : bounds->maxs.value[axis]) / 8.0;
	return 1;
}

static int SolvePlaneTriple(const double left_normal[3], double left_distance,
	const double middle_normal[3], double middle_distance,
	const double right_normal[3], double right_distance, double point[3])
{
	double determinant;
	double numerator;

	if (left_normal == NULL || middle_normal == NULL || right_normal == NULL ||
		point == NULL)
		return 0;
	determinant = left_normal[0] *
		(middle_normal[1] * right_normal[2] - middle_normal[2] * right_normal[1]) -
		left_normal[1] *
		(middle_normal[0] * right_normal[2] - middle_normal[2] * right_normal[0]) +
		left_normal[2] *
		(middle_normal[0] * right_normal[1] - middle_normal[1] * right_normal[0]);
	if (!isfinite(determinant) || fabs(determinant) < 1.0e-12)
		return 0;
	numerator = left_distance *
		(middle_normal[1] * right_normal[2] - middle_normal[2] * right_normal[1]) -
		left_normal[1] *
		(middle_distance * right_normal[2] - middle_normal[2] * right_distance) +
		left_normal[2] *
		(middle_distance * right_normal[1] - middle_normal[1] * right_distance);
	point[0] = numerator / determinant;
	numerator = left_normal[0] *
		(middle_distance * right_normal[2] - middle_normal[2] * right_distance) -
		left_distance *
		(middle_normal[0] * right_normal[2] - middle_normal[2] * right_normal[0]) +
		left_normal[2] *
		(middle_normal[0] * right_distance - middle_distance * right_normal[0]);
	point[1] = numerator / determinant;
	numerator = left_normal[0] *
		(middle_normal[1] * right_distance - middle_distance * right_normal[1]) -
		left_normal[1] *
		(middle_normal[0] * right_distance - middle_distance * right_normal[0]) +
		left_distance *
		(middle_normal[0] * right_normal[1] - middle_normal[1] * right_normal[0]);
	point[2] = numerator / determinant;
	return isfinite(point[0]) && isfinite(point[1]) && isfinite(point[2]);
}

static int PointInQ8BoundsDouble(const double point[3],
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (point == NULL || bounds == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < (double)bounds->mins.value[axis] / 8.0 ||
			point[axis] >= (double)bounds->maxs.value[axis] / 8.0)
			return 0;
	return 1;
}

static int BoundsIntersectCellExact(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t cell_index, const sg_rune_q8_bounds_t *bounds)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t constraint_count;
	uint32_t first;
	uint32_t middle;
	uint32_t right;
	double point[3];
	uint32_t axis;

	if (geometry == NULL || bounds == NULL || cell_index >= geometry->cell_count ||
		!BoundsValidQ8(bounds) || !BoundsOverlapQ8(bounds,
			&geometry->cells[cell_index].bounds))
		return 0;
	cell = &geometry->cells[cell_index];
	if (!CountAdd(cell->incidences.count, 12U, &constraint_count))
		return 0;
	/* The AABB intersection midpoint is an interior candidate even when the
	 * cell's open side lies exactly on one of the entity bounds.  Checking it
	 * against every BSP half-space preserves exactness and avoids mistaking a
	 * shared boundary for an empty overlap. */
	for (axis = 0U; axis < 3U; axis++)
	{
		double lower = fmax(
			(double)bounds->mins.value[axis] / 8.0,
			(double)cell->bounds.mins.value[axis] / 8.0);
		double upper = fmin(
			(double)bounds->maxs.value[axis] / 8.0,
			(double)cell->bounds.maxs.value[axis] / 8.0);

		point[axis] = (lower + upper) * 0.5;
	}
	if (CellContainsPointDouble(geometry, cell_index, point))
		return 1;
	for (axis = 0U; axis < 8U; axis++)
	{
		uint32_t coordinate;
		for (coordinate = 0U; coordinate < 3U; coordinate++)
			point[coordinate] = (double)((axis & (1U << coordinate)) != 0U ?
				bounds->maxs.value[coordinate] : bounds->mins.value[coordinate]) /
				8.0;
		if (PointInQ8BoundsDouble(point, bounds) &&
			CellContainsPointDouble(geometry, cell_index, point))
			return 1;
	}
	for (axis = 0U; axis < 3U; axis++)
		point[axis] = ((double)bounds->mins.value[axis] +
			(double)bounds->maxs.value[axis]) / 16.0;
	if (PointInQ8BoundsDouble(point, bounds) &&
		CellContainsPointDouble(geometry, cell_index, point))
		return 1;
	for (first = 0U; first < constraint_count; first++)
	{
		double first_normal[3];
		double first_distance;

		if (!IntersectionConstraintPlane(geometry, cell_index, bounds, first,
			first_normal, &first_distance))
			return 0;
		for (middle = first + 1U; middle < constraint_count; middle++)
		{
			double middle_normal[3];
			double middle_distance;

			if (!IntersectionConstraintPlane(geometry, cell_index, bounds,
				middle, middle_normal, &middle_distance))
				return 0;
			for (right = middle + 1U; right < constraint_count; right++)
			{
				double right_normal[3];
				double right_distance;

				if (!IntersectionConstraintPlane(geometry, cell_index, bounds,
					right, right_normal, &right_distance))
					return 0;
				if (!SolvePlaneTriple(first_normal, first_distance,
					middle_normal, middle_distance, right_normal, right_distance,
					point) || !PointInQ8BoundsDouble(point, bounds) ||
					!CellContainsPointDouble(geometry, cell_index, point))
					continue;
				return 1;
			}
		}
	}
	return 0;
}

static int FloatToQ8(float value, int32_t *output)
{
	double scaled;
	double rounded;

	if (output == NULL || !isfinite(value))
		return 0;
	scaled = (double)value * 8.0;
	rounded = nearbyint(scaled);
	if (!isfinite(scaled) || !isfinite(rounded) ||
		rounded < (double)INT32_MIN || rounded > (double)INT32_MAX ||
		fabs(scaled - rounded) > 0.5)
		return 0;
	*output = (int32_t)rounded;
	return 1;
}

static int FloatVecToQ8(const sg_rune_vec3_t *source,
	sg_rune_q8_vec3_t *output)
{
	uint32_t axis;

	if (source == NULL || output == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!FloatToQ8(source->value[axis], &output->value[axis]))
			return 0;
	return 1;
}

static int FloatBoundsToQ8(const sg_rune_bounds_t *source,
	sg_rune_q8_bounds_t *output)
{
	return source != NULL && output != NULL &&
		FloatVecToQ8(&source->mins, &output->mins) &&
		FloatVecToQ8(&source->maxs, &output->maxs) &&
		BoundsValidQ8(output);
}

static int PointBounds(const sg_rune_q8_vec3_t *point,
	sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (point == NULL || bounds == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (point->value[axis] == INT32_MIN ||
			point->value[axis] == INT32_MAX)
			return 0;
		bounds->mins.value[axis] = point->value[axis] - 1;
		bounds->maxs.value[axis] = point->value[axis] + 1;
	}
	return 1;
}

static int OldHullMatchesCompact(const sg_rune_hull_profile_t *old_hull,
	const sg_rune_compact_hull_t *compact_hull)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float old_min = old_hull->mins.value[axis];
		float old_max = old_hull->maxs.value[axis];
		float compact_min = (float)compact_hull->mins.value[axis] * 0.125f;
		float compact_max = (float)compact_hull->maxs.value[axis] * 0.125f;

		if (!FloatBitsEqual(old_min, compact_min) ||
			!FloatBitsEqual(old_max, compact_max))
			return 0;
	}
	return 1;
}

static int OldPhysicsMatchesCompact(const sg_rune_physics_parameters_t *old,
	const sg_rune_compact_physics_t *compact)
{
	return FloatBits(old->gravity) == compact->gravity_bits &&
		FloatBits(old->ground_acceleration) == compact->ground_acceleration_bits &&
		FloatBits(old->air_acceleration) == compact->air_acceleration_bits &&
		FloatBits(old->water_acceleration) == compact->water_acceleration_bits &&
		FloatBits(old->hook_acceleration) == compact->hook_acceleration_bits &&
		FloatBits(old->external_acceleration) == compact->external_acceleration_bits &&
		FloatBits(old->water_drag) == compact->water_drag_bits &&
		FloatBits(old->max_velocity) == compact->max_velocity_bits &&
		old->frame_ms == compact->frame_ms && old->substep_ms == compact->substep_ms;
}

static int OldIdentityMatchesCompact(const sg_rune_model_identity_t *old,
	const sg_rune_compact_identity_t *compact)
{
	return old != NULL && compact != NULL &&
		old->bsp_content_id == DeriveBspContentId(compact->bsp_sha256) &&
		old->entity_semantics_id == compact->entity_semantics_id &&
		old->physics_abi_id == compact->physics_abi_id &&
		old->schema_id == compact->schema_id &&
		old->producer_identity == compact->producer_identity &&
		OldHullMatchesCompact(&old->standing_hull, &compact->standing_hull) &&
		OldHullMatchesCompact(&old->crouching_hull, &compact->crouching_hull) &&
		OldPhysicsMatchesCompact(&old->physics, &compact->physics);
}

static int OldIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL ||
		left->bsp_content_id != right->bsp_content_id ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->source_set_identity != right->source_set_identity ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!FloatBitsEqual(left->standing_hull.mins.value[axis],
				right->standing_hull.mins.value[axis]) ||
			!FloatBitsEqual(left->standing_hull.maxs.value[axis],
				right->standing_hull.maxs.value[axis]) ||
			!FloatBitsEqual(left->crouching_hull.mins.value[axis],
				right->crouching_hull.mins.value[axis]) ||
			!FloatBitsEqual(left->crouching_hull.maxs.value[axis],
				right->crouching_hull.maxs.value[axis]))
			return 0;
	return FloatBits(left->physics.gravity) == FloatBits(right->physics.gravity) &&
		FloatBits(left->physics.ground_acceleration) ==
			FloatBits(right->physics.ground_acceleration) &&
		FloatBits(left->physics.air_acceleration) ==
			FloatBits(right->physics.air_acceleration) &&
		FloatBits(left->physics.water_acceleration) ==
			FloatBits(right->physics.water_acceleration) &&
		FloatBits(left->physics.hook_acceleration) ==
			FloatBits(right->physics.hook_acceleration) &&
		FloatBits(left->physics.external_acceleration) ==
			FloatBits(right->physics.external_acceleration) &&
		FloatBits(left->physics.water_drag) ==
			FloatBits(right->physics.water_drag) &&
		FloatBits(left->physics.max_velocity) ==
			FloatBits(right->physics.max_velocity) &&
		left->physics.frame_ms == right->physics.frame_ms &&
		left->physics.substep_ms == right->physics.substep_ms;
}

static int EntityStringOffsetValid(const sg_bsp_entity_semantics_t *semantics,
	uint32_t offset)
{
	uint32_t index;

	if (offset == SG_BSP_ENTITY_STRING_NONE)
		return 1;
	if (semantics == NULL || semantics->strings == NULL ||
		offset >= semantics->string_bytes)
		return 0;
	for (index = offset; index < semantics->string_bytes; index++)
		if (semantics->strings[index] == '\0')
			return 1;
	return 0;
}

static int EntityRefIsValid(uint32_t source, uint32_t source_count)
{
	return source < source_count;
}

static int ConfigurationCellCount(
	const sg_configuration_semantics_t *configuration,
	uint32_t *count_out)
{
	uint32_t index;
	uint32_t count = 0U;

	if (configuration == NULL || count_out == NULL)
		return 0;
	for (index = 0U; index < configuration->region_count; index++)
	{
		uint32_t cell = configuration->regions[index].cell;

		if (cell == UINT32_MAX)
			return 0;
		if (cell == UINT32_MAX - 1U)
			return 0;
		if (cell >= count)
			count = cell + 1U;
	}
	*count_out = count;
	return 1;
}

/* The semantics view deliberately does not duplicate the configuration-space
 * cell array.  The geometry materializer's span/index correspondence is the
 * authority whenever compact cells were split by stance overlays. */
static int CompactCellMappedToConfigurationCell(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t configuration_cell, uint32_t compact_cell)
{
	uint32_t local;
	const sg_rune_compact_geometry_cell_span_t *span;

	if (geometry == NULL || compact_cell >= geometry->cell_count)
		return 0;
	if (geometry->compact_cells_for_configuration_cell == NULL)
		return configuration_cell == compact_cell;
	if (configuration_cell >=
		geometry->compact_cells_for_configuration_cell_count)
		return 0;
	span = &geometry->compact_cells_for_configuration_cell[configuration_cell];
	if (span->first > geometry->configuration_cell_compact_cell_count ||
		span->count > geometry->configuration_cell_compact_cell_count -
			span->first || (span->count != 0U &&
			geometry->configuration_cell_compact_cells == NULL))
		return 0;
	for (local = 0U; local < span->count; local++)
		if (geometry->configuration_cell_compact_cells[span->first + local].value ==
			compact_cell)
			return 1;
	return 0;
}

static int CompactSourceValid(const sg_rune_compact_source_t *source,
	uint32_t facet_limit, const sg_rune_compact_source_counts_t *counts)
{
	if (source == NULL || counts == NULL ||
		(uint32_t)source->kind >= (uint32_t)SG_RUNE_COMPACT_SOURCE_KIND_COUNT)
		return 0;
	switch (source->kind)
	{
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
		return source->value.split.parent_facet.value < facet_limit &&
			source->value.split.ordinal != UINT32_MAX;
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int CompactCellSourceValid(
	const sg_rune_compact_cell_source_t *source,
	const sg_rune_compact_source_counts_t *counts)
{
	return source != NULL && counts != NULL &&
		source->model < counts->model_count &&
		source->leaf < counts->leaf_count &&
		source->area < counts->area_count && source->cluster >= -1 &&
		source->split_ordinal != UINT32_MAX;
}

static int CompactPlaneValid(const sg_rune_binary32_plane_t *plane)
{
	uint32_t axis;
	int has_normal = 0;

	if (plane == NULL || !Binary32CanonicalFinite(plane->distance_bits))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!Binary32CanonicalFinite(plane->normal_bits[axis]))
			return 0;
		if ((plane->normal_bits[axis] & UINT32_C(0x7fffffff)) != 0U)
			has_normal = 1;
	}
	return has_normal;
}

static int CompactSourceSurfaceCompare(
	const sg_rune_compact_source_surface_t *left,
	const sg_rune_compact_source_surface_t *right)
{
	if (left->source.model != right->source.model)
		return left->source.model < right->source.model ? -1 : 1;
	if (left->source.brush != right->source.brush)
		return left->source.brush < right->source.brush ? -1 : 1;
	if (left->source.brush_side != right->source.brush_side)
		return left->source.brush_side < right->source.brush_side ? -1 : 1;
	if (left->source.plane != right->source.plane)
		return left->source.plane < right->source.plane ? -1 : 1;
	return 0;
}

static int CompactSourceSurfaceValid(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t index)
{
	const sg_rune_compact_source_surface_t *surface;
	const sg_rune_compact_source_counts_t *counts;

	if (geometry == NULL || index >= geometry->source_surface_count ||
		geometry->source_surfaces == NULL)
		return 0;
	surface = &geometry->source_surfaces[index];
	counts = &geometry->identity.source_counts;
	if (surface->source.model >= counts->model_count ||
		surface->source.brush >= counts->brush_count ||
		surface->source.brush_side >= counts->brush_side_count ||
		surface->source.plane >= counts->plane_count ||
		(uint32_t)surface->frame >=
			(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT ||
		surface->frame != (surface->source.model == 0U ?
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
			SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) ||
		!CompactPlaneValid(&surface->plane) ||
		surface->vertices.count < 3U ||
		surface->vertices.first > geometry->source_surface_vertex_count ||
		surface->vertices.count > geometry->source_surface_vertex_count -
			surface->vertices.first)
		return 0;
	if (surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE)
		return surface->cell.value == SG_RUNE_COMPACT_INDEX_NONE &&
			surface->split_ordinal == 0U;
	if (surface->parent_surface >= index ||
		geometry->source_surfaces[surface->parent_surface].parent_surface !=
			SG_RUNE_COMPACT_INDEX_NONE || surface->source.model != 0U ||
		surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ||
		surface->cell.value >= geometry->cell_count ||
		surface->split_ordinal == 0U)
		return 0;
	return memcmp(&surface->source,
		&geometry->source_surfaces[surface->parent_surface].source,
		sizeof(surface->source)) == 0 &&
		memcmp(&surface->plane,
			&geometry->source_surfaces[surface->parent_surface].plane,
			sizeof(surface->plane)) == 0;
}

static int CompactSourceSurfaceMatchesConfiguration(
	const sg_rune_compact_source_surface_t *source,
	const sg_configuration_hook_surface_t *semantic)
{
	uint32_t axis;

	if (source == NULL || semantic == NULL ||
		source->source.model != semantic->model ||
		source->source.brush != semantic->brush ||
		source->source.brush_side != semantic->brush_side ||
		source->plane.distance_bits != FloatBits(semantic->distance))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (source->plane.normal_bits[axis] != FloatBits(semantic->normal[axis]))
			return 0;
	return 1;
}

static int FindSourceSurfaceIndexForConfiguration(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_configuration_hook_surface_t *semantic,
	uint32_t *index_out)
{
	uint32_t found = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t index;

	if (geometry == NULL || semantic == NULL || index_out == NULL)
		return 0;
	for (index = 0U; index < geometry->source_surface_count; index++)
		if (geometry->source_surfaces[index].parent_surface ==
				SG_RUNE_COMPACT_INDEX_NONE &&
			CompactSourceSurfaceMatchesConfiguration(
				&geometry->source_surfaces[index], semantic))
		{
			if (found != SG_RUNE_COMPACT_INDEX_NONE)
				return 0;
			found = index;
		}
	if (found == SG_RUNE_COMPACT_INDEX_NONE)
		return 0;
	*index_out = found;
	return 1;
}

static int SourceSurfaceMatchesFacet(
	const sg_rune_compact_source_surface_t *source,
	const sg_rune_compact_facet_t *facet)
{
	return source != NULL && facet != NULL &&
		source->parent_surface == SG_RUNE_COMPACT_INDEX_NONE &&
		facet->source.kind == SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE &&
		facet->source.value.brush_side.model == source->source.model &&
		facet->source.value.brush_side.brush == source->source.brush &&
		facet->source.value.brush_side.brush_side == source->source.brush_side &&
		facet->source.value.brush_side.plane == source->source.plane;
}

static int FindSourceSurfaceIndexForFacet(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_compact_facet_t *facet, uint32_t *index_out)
{
	uint32_t index;
	uint32_t found = SG_RUNE_COMPACT_INDEX_NONE;

	if (geometry == NULL || facet == NULL || index_out == NULL)
		return 0;
	for (index = 0U; index < geometry->source_surface_count; index++)
		if (geometry->source_surfaces[index].parent_surface ==
				SG_RUNE_COMPACT_INDEX_NONE &&
			SourceSurfaceMatchesFacet(&geometry->source_surfaces[index], facet))
		{
			if (found != SG_RUNE_COMPACT_INDEX_NONE)
				return 0;
			found = index;
		}
	if (found == SG_RUNE_COMPACT_INDEX_NONE)
		return 0;
	*index_out = found;
	return 1;
}

static int CompactFacetMatchesHook(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_compact_facet_t *facet,
	const sg_configuration_semantics_t *configuration,
	const sg_configuration_hook_surface_t *surface)
{
	uint32_t source_surface_index;
	uint32_t facet_source_surface_index;

	if (geometry == NULL || facet == NULL || configuration == NULL ||
		surface == NULL || facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY ||
		/* Plane identity is not surface identity.  A BSP_PLANE or SPLIT record
		 * may coincide geometrically with a configured surface while referring
		 * to a different brush side (or to no brush side at all).  Only the
		 * authenticated expanded-brush lineage may acquire surface facts. */
		facet->source.kind != SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE ||
		!FindSourceSurfaceIndexForConfiguration(geometry, surface,
			&source_surface_index) ||
		!FindSourceSurfaceIndexForFacet(geometry, facet,
			&facet_source_surface_index) ||
		/* Compact facets are the world cell complex.  A model-local source root
		 * is a moving-submodel surface and must remain in the exact source
		 * catalog; it is never projected onto a world facet by matching plane or
		 * vertices. */
		geometry->source_surfaces[source_surface_index].frame !=
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ||
		geometry->source_surfaces[source_surface_index].source.model !=
			SG_HOST_COLLISION_MODEL_WORLD ||
		geometry->source_surfaces[facet_source_surface_index].frame !=
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ||
		geometry->source_surfaces[facet_source_surface_index].source.model !=
			SG_HOST_COLLISION_MODEL_WORLD ||
		facet_source_surface_index != source_surface_index)
		return 0;
	return 1;
}

/* The mechanisms owner is the opaque public authority.  Once that owner is
 * authenticated/read at the API boundary, internal builders use this private
 * view so callers cannot forge a detached mechanism fragment. */
typedef struct materializer_input_view_s
{
	sg_rune_compact_static_geometry_view_t geometry;
	const sg_bsp_entity_semantics_t *entities;
	const sg_configuration_semantics_t *configuration;
	const sg_static_visibility_t *visibility;
	const sg_rune_compact_mechanisms_view_t *mechanisms;
} materializer_input_view_t;

static int MechanismAuthorityViewValid(
	const materializer_input_view_t *input,
	sg_rune_compact_static_materializer_error_t *error);

static int GeometryInputValid(
	const materializer_input_view_t *input,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_rune_compact_static_geometry_view_t *geometry;
	const sg_bsp_entity_semantics_t *entities;
	const sg_configuration_semantics_t *configuration;
	const sg_static_visibility_t *visibility;
	uint32_t configuration_cell_count;
	uint32_t index;
	uint32_t facet_vertex_cursor = 0U;
	uint32_t facet_incidence_cursor = 0U;
	uint32_t source_vertex_cursor = 0U;
	uint32_t current_source_root = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t previous_source_root = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t previous_source_child = SG_RUNE_COMPACT_INDEX_NONE;

	if (input == NULL ||
		input->geometry.cell_count == 0U || input->geometry.cells == NULL ||
		input->entities == NULL || input->configuration == NULL ||
		input->visibility == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	geometry = &input->geometry;
	entities = input->entities;
	configuration = input->configuration;
	visibility = input->visibility;
	if (!ConfigurationCellCount(configuration, &configuration_cell_count))
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, 0U);
		return 0;
	}
	if (geometry->cell_count > SG_RUNE_COMPACT_MAX_CELLS ||
		geometry->facet_count > SG_RUNE_COMPACT_MAX_FACETS ||
		geometry->incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		geometry->vertex_count > SG_RUNE_COMPACT_MAX_VERTICES ||
		geometry->portal_count > SG_RUNE_COMPACT_MAX_PORTALS ||
		geometry->source_surface_count > SG_RUNE_COMPACT_MAX_SOURCE_SURFACES ||
		geometry->source_surface_vertex_count >
			SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES)
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	if ((geometry->facet_count != 0U && geometry->facets == NULL) ||
		(geometry->incidence_count != 0U && geometry->incidences == NULL) ||
		(geometry->cell_incidence_count != 0U &&
			geometry->cell_incidences == NULL) ||
		(geometry->vertex_count != 0U && geometry->vertices == NULL) ||
		(geometry->portal_count != 0U && geometry->portals == NULL) ||
		(geometry->source_surface_count != 0U &&
			geometry->source_surfaces == NULL) ||
		(geometry->source_surface_vertex_count != 0U &&
			geometry->source_surface_vertices == NULL) ||
		(geometry->source_surface_count == 0U &&
			geometry->source_surface_vertex_count != 0U) ||
		(geometry->compact_cells_for_configuration_cell == NULL &&
			geometry->compact_cells_for_configuration_cell_count != 0U) ||
		(geometry->compact_cells_for_configuration_cell != NULL &&
			geometry->compact_cells_for_configuration_cell_count <
				configuration_cell_count) ||
		(geometry->configuration_cell_compact_cells == NULL &&
			geometry->configuration_cell_compact_cell_count != 0U) ||
		(geometry->compact_cells_for_configuration_cell == NULL &&
			geometry->configuration_cell_compact_cell_count != 0U))
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	if (geometry->compact_cells_for_configuration_cell == NULL)
	{
		/* Direct ordinal correspondence is valid only when every referenced
		 * configuration cell has a compact counterpart. */
		if (configuration_cell_count > geometry->cell_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, 0U);
			return 0;
		}
	}
	else
	{
		for (index = 0U; index <
			geometry->compact_cells_for_configuration_cell_count; index++)
		{
			const sg_rune_compact_geometry_cell_span_t *span =
				&geometry->compact_cells_for_configuration_cell[index];
			uint32_t local;

			if (span->first > geometry->configuration_cell_compact_cell_count ||
				span->count > geometry->configuration_cell_compact_cell_count -
					span->first || (span->count != 0U &&
					geometry->configuration_cell_compact_cells == NULL))
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, index);
				return 0;
			}
			for (local = 0U; local < span->count; local++)
			{
				const uint32_t reference = span->first + local;
				const uint32_t compact_cell =
					geometry->configuration_cell_compact_cells[reference].value;

				if (compact_cell >= geometry->cell_count ||
					(local != 0U && geometry->configuration_cell_compact_cells[
						reference - 1U].value >= compact_cell))
				{
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION,
						index);
					return 0;
				}
			}
		}
	}
	if (!CompactIdentityValid(&geometry->identity))
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	if (!OldIdentityMatchesCompact(&configuration->identity, &geometry->identity) ||
		!OldIdentityEqual(&configuration->identity, &visibility->identity) ||
		entities->entity_count != geometry->identity.source_counts.entity_count ||
		entities->source_set_identity != configuration->identity.source_set_identity ||
		entities->world.source_set_identity !=
			configuration->identity.source_set_identity)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	if (configuration->hook_surface_count != 0U &&
		geometry->source_surface_count == 0U)
	{
		/* Hook and mover facts are only meaningful when their exact all-model
		 * source roots were authenticated by geometry. */
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, 0U);
		return 0;
	}
	for (index = 0U; index < geometry->source_surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *surface =
			&geometry->source_surfaces[index];

		if (!CompactSourceSurfaceValid(geometry, index) ||
			surface->vertices.first != source_vertex_cursor)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
			return 0;
		}
		if (surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE)
		{
			if (previous_source_root != SG_RUNE_COMPACT_INDEX_NONE &&
				CompactSourceSurfaceCompare(
					&geometry->source_surfaces[previous_source_root], surface) >= 0)
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
				return 0;
			}
			previous_source_root = index;
			current_source_root = index;
			previous_source_child = SG_RUNE_COMPACT_INDEX_NONE;
		}
		else if (surface->parent_surface != current_source_root ||
			(previous_source_child != SG_RUNE_COMPACT_INDEX_NONE &&
			 (geometry->source_surfaces[previous_source_child].cell.value >
				surface->cell.value ||
			  (geometry->source_surfaces[previous_source_child].cell.value ==
				 surface->cell.value &&
			   geometry->source_surfaces[previous_source_child].split_ordinal >=
					surface->split_ordinal))))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
			return 0;
		}
		else
			previous_source_child = index;
		source_vertex_cursor += surface->vertices.count;
	}
	if (source_vertex_cursor != geometry->source_surface_vertex_count)
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL,
			source_vertex_cursor);
		return 0;
	}
	for (index = 0U; index < configuration->hook_surface_count; index++)
	{
		uint32_t source_surface_index = SG_RUNE_COMPACT_INDEX_NONE;

		if (!FindSourceSurfaceIndexForConfiguration(geometry,
			&configuration->hook_surfaces[index], &source_surface_index))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_AMBIGUOUS_BINDING,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, index);
			return 0;
		}
		(void)source_surface_index;
	}
	if (!MechanismAuthorityViewValid(input, error))
		return 0;
	for (index = 0U; index < geometry->cell_count; index++)
	{
		const sg_rune_compact_cell_t *cell = &geometry->cells[index];

		if (!CompactCellSourceValid(&cell->source,
			&geometry->identity.source_counts) ||
			!BoundsValidQ8(&cell->bounds) ||
			cell->valid_stances == 0U ||
			(cell->contents & (sg_rune_compact_contents_mask_t)
				~SG_RUNE_COMPACT_CONTENTS_KNOWN) != 0U ||
			(cell->semantics & (sg_rune_compact_cell_semantics_t)
				~SG_RUNE_COMPACT_CELL_SEMANTICS_KNOWN) != 0U ||
			(cell->valid_stances & (sg_rune_stance_validity_t)
				~SG_RUNE_STANCE_VALID_ALL) != 0U ||
			cell->reserved[0] != 0U || cell->reserved[1] != 0U ||
			cell->reserved[2] != 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
			return 0;
		}
		if (cell->incidences.first > geometry->cell_incidence_count ||
			cell->incidences.count > geometry->cell_incidence_count -
				cell->incidences.first)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
			return 0;
		}
		{
			uint32_t local;

			for (local = 0U; local < cell->incidences.count; local++)
			{
				const uint32_t reference = cell->incidences.first + local;
				const uint32_t incidence =
					geometry->cell_incidences[reference].value;

				if (incidence >= geometry->incidence_count ||
					geometry->incidences[incidence].cell.value != index ||
					geometry->incidences[incidence].cell_ordinal != local)
				{
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
					return 0;
				}
			}
		}
	}
	for (index = 0U; index < geometry->facet_count; index++)
	{
		const sg_rune_compact_facet_t *facet = &geometry->facets[index];

		if (!CompactSourceValid(&facet->source, geometry->facet_count,
			&geometry->identity.source_counts) ||
			!CompactPlaneValid(&facet->plane) ||
			facet->vertices.first != facet_vertex_cursor ||
			facet->incidences.first != facet_incidence_cursor ||
			facet->incidences.first > geometry->incidence_count ||
			facet->incidences.count > geometry->incidence_count -
				facet->incidences.first ||
			facet->vertices.first > geometry->vertex_count ||
			facet->vertices.count > geometry->vertex_count - facet->vertices.first ||
			(uint32_t)facet->kind >=
				(uint32_t)SG_RUNE_COMPACT_FACET_KIND_COUNT ||
			(facet->kind == SG_RUNE_COMPACT_FACET_POLYGON &&
				(facet->vertices.count < 3U ||
					(facet->incidences.count != 1U &&
						facet->incidences.count != 2U) ||
					((facet->incidences.count == 1U) !=
						(facet->portal.value == SG_RUNE_COMPACT_INDEX_NONE)) ||
					(facet->incidences.count == 2U &&
						(facet->portal.value == SG_RUNE_COMPACT_INDEX_NONE)) ||
					((facet->portal.value != SG_RUNE_COMPACT_INDEX_NONE) &&
						(facet->portal.value >= geometry->portal_count ||
							geometry->portals[facet->portal.value].facet.value != index)))) ||
			(facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY &&
				(facet->vertices.count != 0U || facet->incidences.count != 1U ||
				 facet->portal.value != SG_RUNE_COMPACT_INDEX_NONE)))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, index);
				return 0;
		}
		if (!CountAdd(facet_vertex_cursor, facet->vertices.count,
			&facet_vertex_cursor) || !CountAdd(facet_incidence_cursor,
			facet->incidences.count, &facet_incidence_cursor))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, index);
			return 0;
		}
	}
	if (facet_vertex_cursor != geometry->vertex_count ||
		facet_incidence_cursor != geometry->incidence_count)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	for (index = 0U; index < geometry->incidence_count; index++)
	{
		const sg_rune_compact_incidence_t *incidence =
			&geometry->incidences[index];

		if (incidence->cell.value >= geometry->cell_count ||
			incidence->facet.value >= geometry->facet_count ||
			(uint32_t)incidence->side >= (uint32_t)SG_RUNE_FACET_SIDE_COUNT ||
			(uint32_t)incidence->boundary >=
				(uint32_t)SG_RUNE_BOUNDARY_OWNERSHIP_COUNT ||
			incidence->cell_ordinal >=
				geometry->cells[incidence->cell.value].incidences.count ||
			geometry->cell_incidences[
				geometry->cells[incidence->cell.value].incidences.first +
				incidence->cell_ordinal].value != index)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
				return 0;
		}
		if (index != 0U && incidence->facet.value ==
			geometry->incidences[index - 1U].facet.value &&
			(incidence->side < geometry->incidences[index - 1U].side ||
			(incidence->side == geometry->incidences[index - 1U].side &&
				incidence->cell.value <=
					geometry->incidences[index - 1U].cell.value)))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, index);
			return 0;
		}
	}
	for (index = 0U; index < geometry->portal_count; index++)
	{
		const sg_rune_compact_portal_t *portal = &geometry->portals[index];
		const sg_rune_compact_incidence_t *negative;
		const sg_rune_compact_incidence_t *positive;

		if (!CompactSourceValid(&portal->source, geometry->facet_count,
			&geometry->identity.source_counts) ||
			portal->facet.value >= geometry->facet_count ||
			portal->negative_incidence.value >= geometry->incidence_count ||
			portal->positive_incidence.value >= geometry->incidence_count ||
			portal->reserved[0] != 0U || portal->reserved[1] != 0U ||
			portal->reserved[2] != 0U ||
			geometry->facets[portal->facet.value].kind !=
				SG_RUNE_COMPACT_FACET_POLYGON ||
			geometry->incidences[portal->negative_incidence.value].facet.value !=
				portal->facet.value ||
			geometry->incidences[portal->positive_incidence.value].facet.value !=
				portal->facet.value ||
			(uint32_t)portal->direction >=
				(uint32_t)SG_RUNE_PORTAL_CONTINUITY_COUNT ||
			portal->valid_stances == 0U ||
			(portal->valid_stances & (sg_rune_stance_validity_t)
				~SG_RUNE_STANCE_VALID_ALL) != 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, index);
			return 0;
		}
		negative = &geometry->incidences[portal->negative_incidence.value];
		positive = &geometry->incidences[portal->positive_incidence.value];
		if (negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
			positive->side != SG_RUNE_FACET_POSITIVE_SIDE ||
			negative->cell.value == positive->cell.value ||
			negative->boundary == positive->boundary ||
			portal->clearance_q8 == 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, index);
			return 0;
		}
	}
	if (entities->entity_count != 0U && entities->entities == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, 0U);
		return 0;
	}
	for (index = 0U; index < entities->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity = &entities->entities[index];
		uint32_t other;

		if (entity->source_set_identity != configuration->identity.source_set_identity ||
			entity->source_entity_ordinal == UINT32_MAX ||
			entity->canonical_ordinal != index ||
			!EntityRefIsValid(entity->canonical_ordinal,
				geometry->identity.source_counts.entity_count) ||
			((entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) != 0U &&
				(entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
					 entity->bsp_model >= geometry->identity.source_counts.model_count)) ||
			((entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U &&
				entity->bsp_model != SG_BSP_ENTITY_MODEL_NONE) ||
			!EntityStringOffsetValid(entities, entity->classname) ||
			!EntityStringOffsetValid(entities, entity->targetname) ||
			!EntityStringOffsetValid(entities, entity->required_item) ||
			!EntityStringOffsetValid(entities, entity->spawned_classname) ||
			!EntityStringOffsetValid(entities, entity->destination_map) ||
			((entity->flags & SG_BSP_ENTITY_HAS_BOUNDS) != 0U &&
				(!Finite3(entity->bounds.mins.value) ||
				 !Finite3(entity->bounds.maxs.value) ||
				 entity->bounds.mins.value[0] >= entity->bounds.maxs.value[0] ||
				 entity->bounds.mins.value[1] >= entity->bounds.maxs.value[1] ||
				 entity->bounds.mins.value[2] >= entity->bounds.maxs.value[2])) ||
			!Finite3(entity->origin.value))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, index);
			return 0;
		}
		for (other = 0U; other < index; other++)
			if (entities->entities[other].source_entity_ordinal ==
					entity->source_entity_ordinal ||
				entities->entities[other].canonical_ordinal ==
					entity->canonical_ordinal)
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, index);
				return 0;
			}
		if ((entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) != 0U &&
			(uint32_t)entity->landmark_kind >=
				(uint32_t)SG_RUNE_LANDMARK_KIND_COUNT)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, index);
			return 0;
		}
		if ((entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND) != 0U &&
			(uint32_t)entity->mechanism_kind >=
				(uint32_t)SG_RUNE_MECHANISM_KIND_COUNT)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, index);
			return 0;
		}
		if ((entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND) != 0U &&
			(entity->flags & SG_BSP_ENTITY_HAS_MECHANISM) == 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, index);
			return 0;
		}
		if ((entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) != 0U &&
			entity->landmark_kind == SG_RUNE_LANDMARK_FLAG_STAND &&
			(entity->flags & (SG_BSP_ENTITY_FLAG_RED | SG_BSP_ENTITY_FLAG_BLUE)) ==
				(SG_BSP_ENTITY_FLAG_RED | SG_BSP_ENTITY_FLAG_BLUE))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY, index);
			return 0;
		}
	}
	if (entities->edge_count != 0U && entities->edges == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, 0U);
		return 0;
	}
	for (index = 0U; index < entities->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &entities->edges[index];

		if (edge->source >= entities->entity_count ||
			edge->destination >= entities->entity_count ||
			edge->kind < SG_MECH_EDGE_TARGET ||
			edge->kind > SG_MECH_EDGE_ROUTE_TARGET ||
			!EntityStringOffsetValid(entities, edge->name))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
	}
	if ((configuration->region_count != 0U && configuration->regions == NULL) ||
		(configuration->face_count != 0U && configuration->faces == NULL) ||
		(configuration->vertex_count != 0U && configuration->vertices == NULL) ||
		(configuration->hook_surface_count != 0U &&
			configuration->hook_surfaces == NULL) ||
		(configuration->hook_vertex_count != 0U &&
			configuration->hook_vertices == NULL))
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, 0U);
		return 0;
	}
	for (index = 0U; index < configuration->region_count; index++)
	{
		const sg_configuration_semantic_region_t *region =
			&configuration->regions[index];

		if (region->cell >= configuration_cell_count ||
			region->first_face > configuration->face_count ||
			region->face_count > configuration->face_count - region->first_face)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, index);
			return 0;
		}
	}
	for (index = 0U; index < configuration->face_count; index++)
	{
		const sg_configuration_semantic_face_t *face = &configuration->faces[index];

		if (face->first_vertex > configuration->vertex_count ||
			face->vertex_count > configuration->vertex_count - face->first_vertex ||
			!Finite3(face->normal) || !isfinite(face->distance))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, index);
			return 0;
		}
	}
	for (index = 0U; index < configuration->hook_surface_count; index++)
	{
		const sg_configuration_hook_surface_t *surface =
			&configuration->hook_surfaces[index];

		if (surface->first_vertex > configuration->hook_vertex_count ||
			surface->vertex_count > configuration->hook_vertex_count -
				surface->first_vertex || !FloatBoundsValid(&surface->bounds) ||
			!Finite3(surface->normal) || !isfinite(surface->distance) ||
			(surface->flags & (sg_configuration_hook_surface_flags_t)
				~(SG_CONFIGURATION_HOOK_SURFACE_SKY |
					SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
					SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL)) != 0U ||
			((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY) != 0U &&
				(surface->flags & SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) != 0U))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION, index);
			return 0;
		}
	}
	if ((visibility->partition_count != 0U && visibility->partitions == NULL) ||
		(visibility->area_count != 0U && visibility->area_components == NULL) ||
		(visibility->occluder_count != 0U && visibility->occluders == NULL) ||
		(visibility->surface_count != 0U && visibility->surfaces == NULL))
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_VISIBILITY, 0U);
		return 0;
	}
	for (index = 0U; index < visibility->partition_count; index++)
	{
		const sg_static_visibility_partition_t *partition =
			&visibility->partitions[index];

		if (partition->configuration_region >= configuration->region_count ||
			partition->configuration_cell >= configuration_cell_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_VISIBILITY, index);
			return 0;
		}
	}
	for (index = 0U; index < visibility->surface_count; index++)
		if (visibility->surfaces[index].semantic_surface >=
				configuration->hook_surface_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_VISIBILITY, index);
			return 0;
		}
	return 1;
}

typedef struct mechanism_spec_s
{
	uint32_t entity_index;
	uint32_t canonical_ordinal;
	uint32_t controller_ordinal;
	uint32_t authority_index;
	uint32_t authority_transition_index;
	uint32_t authority_transition_count;
	sg_rune_compact_mechanism_t value;
	uint32_t bsp_model;
	uint32_t transition_portal;
	uint32_t destination_entity_index;
	uint32_t fanout_ordinal;
	int teleporter_pair;
	int has_landing;
	sg_rune_q8_vec3_t landing_origin;
	sg_rune_q8_bounds_t landing_bounds;
	uint32_t landing_cell;
	int has_activation_witness;
	sg_rune_q8_vec3_t activation_origin;
	sg_rune_q8_bounds_t activation_bounds;
	uint32_t activation_cell;
} mechanism_spec_t;

typedef struct landmark_spec_s
{
	uint32_t entity_index;
	uint32_t canonical_ordinal;
	sg_rune_compact_landmark_kind_t kind;
	uint16_t variant;
	uint32_t linked_entity_index;
	uint32_t linked_destination_entity_index;
	uint32_t linked_fanout_ordinal;
	int linked_pair;
	int use_witness;
} landmark_spec_t;

typedef struct edge_spec_s
{
	sg_rune_compact_mechanism_edge_t value;
} edge_spec_t;

typedef struct controller_spec_s
{
	sg_rune_compact_static_mechanism_controller_t value;
	uint32_t mechanism_index;
} controller_spec_t;

static int CompareI32(int32_t left, int32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareQ8Vec3(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		int comparison = CompareI32(left->value[axis], right->value[axis]);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int CompareQ8Bounds(const sg_rune_q8_bounds_t *left,
	const sg_rune_q8_bounds_t *right)
{
	int comparison = CompareQ8Vec3(&left->mins, &right->mins);

	if (comparison == 0)
		comparison = CompareQ8Vec3(&left->maxs, &right->maxs);
	return comparison;
}

static int CompareControllerValue(
	const sg_rune_compact_static_mechanism_controller_t *left,
	const sg_rune_compact_static_mechanism_controller_t *right)
{
	int comparison = left->controller.entity_ordinal <
		right->controller.entity_ordinal ? -1 :
		left->controller.entity_ordinal > right->controller.entity_ordinal ? 1 : 0;

	if (comparison == 0)
		comparison = left->topology_edge < right->topology_edge ? -1 :
			left->topology_edge > right->topology_edge ? 1 : 0;
	if (comparison == 0)
		comparison = left->spatiality < right->spatiality ? -1 :
			left->spatiality > right->spatiality ? 1 : 0;
	if (comparison == 0)
		comparison = left->activation_cell.value < right->activation_cell.value ? -1 :
			left->activation_cell.value > right->activation_cell.value ? 1 : 0;
	if (comparison == 0)
		comparison = CompareQ8Vec3(&left->activation_witness,
			&right->activation_witness);
	if (comparison == 0)
		comparison = CompareQ8Bounds(&left->activation_bounds,
			&right->activation_bounds);
	return comparison;
}

static int PortalTransitionCells(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_compact_portal_t *portal, uint32_t *entry_cell_out,
	uint32_t *exit_cell_out);
static int FindAuthorityMoverTransition(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_transition_t *transition,
	uint32_t *entry_cell_out, uint32_t *exit_cell_out,
	uint32_t *portal_out, uint32_t *model_out);

struct sg_rune_compact_static_materializer_s
{
	sg_rune_compact_identity_t identity;
	sg_rune_compact_static_t view;
	/* Construction-private authority -> canonical static transition provenance.
	 * It never crosses the artifact boundary: static mechanism numbering may
	 * change after canonical fanout and sorting. */
	uint32_t *authority_transition_static;
	uint32_t authority_transition_count;
	/* Construction-private canonical static mechanism -> authority mechanism
	 * provenance.  Teleport fanout makes this intentionally many-to-one. */
	uint32_t *static_mechanism_authority;
	uint32_t static_mechanism_count;
	sg_rune_compact_mechanism_t *mechanisms;
	sg_rune_compact_static_mechanism_controller_t *mechanism_controllers;
	sg_rune_compact_mechanism_edge_t *mechanism_edges;
	sg_rune_compact_static_transition_t *transitions;
	sg_rune_compact_landmark_t *landmarks;
	sg_rune_compact_cell_index_t *landmark_cells;
	sg_rune_compact_facet_annotation_t *facet_annotations;
	sg_rune_compact_portal_mechanism_t *portal_mechanisms;
};

static int CompareMechanismSpec(const void *left_pointer,
	const void *right_pointer)
{
	const mechanism_spec_t *left = left_pointer;
	const mechanism_spec_t *right = right_pointer;

	if (left->canonical_ordinal < right->canonical_ordinal)
		return -1;
	if (left->canonical_ordinal > right->canonical_ordinal)
		return 1;
	if (left->controller_ordinal < right->controller_ordinal)
		return -1;
	if (left->controller_ordinal > right->controller_ordinal)
		return 1;
	if ((uint32_t)left->value.kind < (uint32_t)right->value.kind)
		return -1;
	if ((uint32_t)left->value.kind > (uint32_t)right->value.kind)
		return 1;
	if (left->value.entry_cell.value < right->value.entry_cell.value)
		return -1;
	if (left->value.entry_cell.value > right->value.entry_cell.value)
		return 1;
	if (left->value.exit_cell.value < right->value.exit_cell.value)
		return -1;
	if (left->value.exit_cell.value > right->value.exit_cell.value)
		return 1;
	if (left->transition_portal < right->transition_portal)
		return -1;
	if (left->transition_portal > right->transition_portal)
		return 1;
	if (left->destination_entity_index < right->destination_entity_index)
		return -1;
	if (left->destination_entity_index > right->destination_entity_index)
		return 1;
	if (left->fanout_ordinal < right->fanout_ordinal)
		return -1;
	if (left->fanout_ordinal > right->fanout_ordinal)
		return 1;
	return 0;
}

static int CompareLandmarkSpec(const void *left_pointer,
	const void *right_pointer)
{
	const landmark_spec_t *left = left_pointer;
	const landmark_spec_t *right = right_pointer;

	if (left->canonical_ordinal < right->canonical_ordinal)
		return -1;
	if (left->canonical_ordinal > right->canonical_ordinal)
		return 1;
	if ((uint32_t)left->kind < (uint32_t)right->kind)
		return -1;
	if ((uint32_t)left->kind > (uint32_t)right->kind)
		return 1;
	if (left->variant < right->variant)
		return -1;
	if (left->variant > right->variant)
		return 1;
	if (left->linked_entity_index < right->linked_entity_index)
		return -1;
	if (left->linked_entity_index > right->linked_entity_index)
		return 1;
	if (left->linked_destination_entity_index <
		right->linked_destination_entity_index)
		return -1;
	if (left->linked_destination_entity_index >
		right->linked_destination_entity_index)
		return 1;
	if (left->linked_fanout_ordinal < right->linked_fanout_ordinal)
		return -1;
	if (left->linked_fanout_ordinal > right->linked_fanout_ordinal)
		return 1;
	return 0;
}

static int MechanismSpecEqual(const mechanism_spec_t *left,
	const mechanism_spec_t *right)
{
	return left->canonical_ordinal == right->canonical_ordinal &&
		left->controller_ordinal == right->controller_ordinal &&
		left->value.kind == right->value.kind &&
		left->value.entry_cell.value == right->value.entry_cell.value &&
		left->value.exit_cell.value == right->value.exit_cell.value &&
		left->transition_portal == right->transition_portal &&
		left->destination_entity_index == right->destination_entity_index &&
		left->fanout_ordinal == right->fanout_ordinal;
}

static int LandmarkSpecEqual(const landmark_spec_t *left,
	const landmark_spec_t *right)
{
	return left->canonical_ordinal == right->canonical_ordinal &&
		left->kind == right->kind && left->variant == right->variant &&
		left->linked_entity_index == right->linked_entity_index &&
		left->linked_destination_entity_index ==
			right->linked_destination_entity_index &&
		left->linked_fanout_ordinal == right->linked_fanout_ordinal;
}

static int MapLandmarkKind(sg_rune_landmark_kind_t source,
	sg_rune_compact_mechanism_kind_t mechanism_kind,
	sg_rune_compact_landmark_kind_t *output)
{
	if (output == NULL)
		return 0;
	switch (source)
	{
	case SG_RUNE_LANDMARK_FLAG_STAND:
		*output = SG_RUNE_COMPACT_LANDMARK_FLAG;
		return 1;
	case SG_RUNE_LANDMARK_ITEM:
		/* The compact schema keeps ammunition as the generic consumable
		 * landmark class; the source classifier already separates weapons,
		 * armor, health, and powerups. */
		*output = SG_RUNE_COMPACT_LANDMARK_AMMO;
		return 1;
	case SG_RUNE_LANDMARK_WEAPON:
		*output = SG_RUNE_COMPACT_LANDMARK_WEAPON;
		return 1;
	case SG_RUNE_LANDMARK_ARMOR:
		*output = SG_RUNE_COMPACT_LANDMARK_ARMOR;
		return 1;
	case SG_RUNE_LANDMARK_HEALTH:
		*output = SG_RUNE_COMPACT_LANDMARK_HEALTH;
		return 1;
	case SG_RUNE_LANDMARK_POWERUP:
		*output = SG_RUNE_COMPACT_LANDMARK_POWERUP;
		return 1;
	case SG_RUNE_LANDMARK_TRIGGER:
		*output = mechanism_kind == SG_RUNE_COMPACT_MECHANISM_PUSH ?
			SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING :
			SG_RUNE_COMPACT_LANDMARK_TRIGGER;
		return 1;
	case SG_RUNE_LANDMARK_MECHANISM_ENTRY:
		*output = SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
		return 1;
	case SG_RUNE_LANDMARK_DEFENSIVE_POSITION:
		*output = SG_RUNE_COMPACT_LANDMARK_DEFENSIVE_POSITION;
		return 1;
	case SG_RUNE_LANDMARK_KIND_COUNT:
		break;
	}
	return 0;
}

static int ActivationLandmarkMatchesMechanism(
	sg_rune_compact_landmark_kind_t landmark,
	sg_rune_compact_mechanism_kind_t mechanism)
{
	switch (landmark)
	{
	case SG_RUNE_COMPACT_LANDMARK_BUTTON:
		return mechanism == SG_RUNE_COMPACT_MECHANISM_BUTTON;
	case SG_RUNE_COMPACT_LANDMARK_TRIGGER:
		return mechanism == SG_RUNE_COMPACT_MECHANISM_TRIGGER;
	case SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING:
		return mechanism == SG_RUNE_COMPACT_MECHANISM_PUSH;
	case SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY:
		return mechanism == SG_RUNE_COMPACT_MECHANISM_DOOR ||
			mechanism == SG_RUNE_COMPACT_MECHANISM_LIFT ||
			mechanism == SG_RUNE_COMPACT_MECHANISM_TRAIN ||
			mechanism == SG_RUNE_COMPACT_MECHANISM_TELEPORT ||
			mechanism == SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	case SG_RUNE_COMPACT_LANDMARK_SPAWN:
	case SG_RUNE_COMPACT_LANDMARK_FLAG:
	case SG_RUNE_COMPACT_LANDMARK_WEAPON:
	case SG_RUNE_COMPACT_LANDMARK_AMMO:
	case SG_RUNE_COMPACT_LANDMARK_ARMOR:
	case SG_RUNE_COMPACT_LANDMARK_HEALTH:
	case SG_RUNE_COMPACT_LANDMARK_POWERUP:
	case SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION:
	case SG_RUNE_COMPACT_LANDMARK_DEFENSIVE_POSITION:
	case SG_RUNE_COMPACT_LANDMARK_KIND_COUNT:
		break;
	}
	return 0;
}

static int MapEdgeKind(sg_mech_edge_kind_t source,
	sg_rune_compact_mechanism_edge_kind_t *output)
{
	if (output == NULL)
		return 0;
	switch (source)
	{
	case SG_MECH_EDGE_TARGET:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET;
		return 1;
	case SG_MECH_EDGE_TARGET_ENT:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET_ENT;
		return 1;
	case SG_MECH_EDGE_MOVE_TARGET:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_MOVE_TARGET;
		return 1;
	case SG_MECH_EDGE_KILLTARGET:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_KILLTARGET;
		return 1;
	case SG_MECH_EDGE_TEAM:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_TEAM;
		return 1;
	case SG_MECH_EDGE_PATH_TARGET:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_PATH_TARGET;
		return 1;
	case SG_MECH_EDGE_ROUTE_TARGET:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_ROUTE_TARGET;
		return 1;
	case SG_MECH_EDGE_OWNER:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_OWNER;
		return 1;
	case SG_MECH_EDGE_ENEMY:
		*output = SG_RUNE_COMPACT_MECHANISM_EDGE_ENEMY;
		return 1;
	}
	return 0;
}

/* A mechanism's authenticated topology span contains both its own outgoing
 * entity edges and executable incoming controller edges.  The latter are
 * intentionally not reclassified as outgoing edges: the controller record's
 * topology_edge is their authority. */
static int EntityTeamEdgePresent(
	const sg_bsp_entity_semantics_t *entities, uint32_t left,
	uint32_t right);

static int AuthorityTopologyEdgeAccepted(
	const sg_rune_compact_mechanisms_view_t *view, uint32_t authority_index,
	uint32_t topology_index,
	const sg_rune_compact_mechanism_topology_edge_t *edge,
	const sg_bsp_entity_semantics_t *entities)
{
	const sg_rune_compact_mechanism_authority_t *authority;
	uint32_t local;

	if (view == NULL || edge == NULL || authority_index >= view->mechanism_count ||
		view->mechanisms == NULL)
		return 0;
	authority = &view->mechanisms[authority_index];
	if (edge->source.entity_ordinal == authority->source.entity_ordinal)
		return 1;
	if (edge->kind == SG_MECH_EDGE_TEAM && entities != NULL &&
		EntityTeamEdgePresent(entities, edge->source.entity_ordinal,
			edge->destination.entity_ordinal))
		return 1;
	if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
		edge->kind == SG_MECH_EDGE_TARGET && entities != NULL &&
		edge->source.entity_ordinal < entities->entity_count &&
		edge->destination.entity_ordinal < entities->entity_count &&
		entities->entities[edge->source.entity_ordinal].mechanism_role ==
			SG_MECH_NODE_PATH_CORNER &&
		entities->entities[edge->destination.entity_ordinal].mechanism_role ==
			SG_MECH_NODE_PATH_CORNER)
		return 1;
	for (local = 0U; local < authority->controllers.count; local++)
	{
		uint32_t controller_index;
		const sg_rune_compact_mechanism_controller_t *controller;

		if (!CountAdd(authority->controllers.first, local, &controller_index) ||
			controller_index >= view->controller_count || view->controllers == NULL)
			return 0;
		controller = &view->controllers[controller_index];
		if (controller->mechanism == authority_index &&
			controller->topology_edge == topology_index &&
			controller->controller.entity_ordinal == edge->source.entity_ordinal)
			return 1;
	}
	return 0;
}

static int AuthoritySpanValid(uint32_t first, uint32_t count, uint32_t limit);
static int AuthorityEntityIndex(
	const sg_bsp_entity_semantics_t *entities,
	sg_rune_compact_mechanism_entity_ref_t reference, uint32_t *index_out);

/* TEAM edges are emitted by the entity-semantics builder in member-to-master
 * order.  The host authority may retain the same authenticated relation in
 * either incidence order, so the relation is compared as an undirected pair
 * while its original topology record remains unchanged in the output. */
static int EntityTeamEdgePresent(
	const sg_bsp_entity_semantics_t *entities, uint32_t left,
	uint32_t right)
{
	uint32_t index;

	if (entities == NULL ||
		(entities->edge_count != 0U && entities->edges == NULL) ||
		left >= entities->entity_count || right >= entities->entity_count)
		return 0;
	for (index = 0U; index < entities->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &entities->edges[index];

		if (edge->kind == SG_MECH_EDGE_TEAM &&
			((edge->source == left && edge->destination == right) ||
			 (edge->source == right && edge->destination == left)))
			return 1;
	}
	return 0;
}

static int AuthorityTeamEdgePresent(
	const sg_rune_compact_mechanisms_view_t *view,
	const sg_rune_compact_mechanism_authority_t *authority,
	uint32_t left, uint32_t right)
{
	uint32_t local;

	if (view == NULL || authority == NULL || view->topology_edges == NULL ||
		!AuthoritySpanValid(authority->topology.first,
			authority->topology.count, view->topology_edge_count))
		return 0;
	for (local = 0U; local < authority->topology.count; local++)
	{
		uint32_t topology_index;
		const sg_rune_compact_mechanism_topology_edge_t *edge;

		if (!CountAdd(authority->topology.first, local, &topology_index))
			return 0;
		edge = &view->topology_edges[topology_index];
		if (edge->kind == SG_MECH_EDGE_TEAM &&
			((edge->source.entity_ordinal == left &&
				edge->destination.entity_ordinal == right) ||
			 (edge->source.entity_ordinal == right &&
				edge->destination.entity_ordinal == left)))
			return 1;
	}
	return 0;
}

/* A portal transition may select a member brush of a TEAM mover rather than
 * the master brush named by the mechanism entity.  Accept that model only
 * after joining the exact entity-semantics TEAM graph to the exact host
 * topology span.  This is a finite connectivity query, not a model-number
 * heuristic: an unrelated model, a missing host echo, and ambiguous model
 * ownership all fail closed.  Return -1 only for allocation failure. */
static int AuthorityTeamMoverModelValid(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	uint32_t mover_model)
{
	const sg_bsp_entity_semantics_t *entities;
	const sg_bsp_entity_semantic_t *root_entity;
	const sg_rune_compact_mechanisms_view_t *view;
	uint8_t *reachable;
	uint32_t root;
	uint32_t index;
	uint32_t selected_count = 0U;
	int changed;

	if (input == NULL || input->entities == NULL || input->mechanisms == NULL ||
		authority == NULL || mover_model == SG_BSP_ENTITY_MODEL_NONE ||
		!AuthorityEntityIndex(input->entities, authority->source, &root))
		return 0;
	entities = input->entities;
	view = input->mechanisms;
	root_entity = &entities->entities[root];
	if ((root_entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
		root_entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE)
		/* A controller-only authority has no master brush identity to compare;
		 * its authenticated transition model is already checked by geometry. */
		return 1;
	if (root_entity->bsp_model == mover_model)
		return 1;
	if (entities->entity_count == 0U)
		return 0;
	reachable = MaterializerCalloc((size_t)entities->entity_count,
		sizeof(*reachable));
	if (reachable == NULL)
		return -1;
	reachable[root] = 1U;
	do
	{
		changed = 0;
		for (index = 0U; index < entities->edge_count; index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge = &entities->edges[index];
			uint32_t source = edge->source;
			uint32_t destination = edge->destination;

			if (edge->kind != SG_MECH_EDGE_TEAM)
				continue;
			if (source >= entities->entity_count ||
				destination >= entities->entity_count ||
				!EntityTeamEdgePresent(entities, source, destination) ||
				!AuthorityTeamEdgePresent(view, authority, source, destination))
			{
				free(reachable);
				return 0;
			}
			if (reachable[source] != 0U && reachable[destination] == 0U)
			{
				reachable[destination] = 1U;
				changed = 1;
			}
			if (reachable[destination] != 0U && reachable[source] == 0U)
			{
				reachable[source] = 1U;
				changed = 1;
			}
		}
	} while (changed != 0);
	for (index = 0U; index < entities->entity_count; index++)
		if (reachable[index] != 0U &&
			(entities->entities[index].flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) != 0U &&
			entities->entities[index].bsp_model == mover_model)
			selected_count++;
	free(reachable);
	return selected_count == 1U;
}

static int EntityIsCanonicalMechanism(const sg_bsp_entity_semantic_t *entity)
{
	return entity != NULL &&
		(entity->flags & (SG_BSP_ENTITY_HAS_MECHANISM |
			SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND)) ==
			(SG_BSP_ENTITY_HAS_MECHANISM |
				SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND);
}

static int EntityIsTeleporterSource(const sg_bsp_entity_semantic_t *entity)
{
	return EntityIsCanonicalMechanism(entity) &&
		entity->mechanism_kind == SG_RUNE_MECHANISM_TELEPORT &&
		(entity->mechanism_role == SG_MECH_NODE_TELEPORTER ||
			entity->mechanism_role == SG_MECH_NODE_TELEPORT_TRIGGER);
}

static int EntityIsTeleporterDestination(
	const sg_bsp_entity_semantic_t *entity)
{
	return EntityIsCanonicalMechanism(entity) &&
		entity->mechanism_kind == SG_RUNE_MECHANISM_TELEPORT &&
		entity->mechanism_role == SG_MECH_NODE_TELEPORT_DEST;
}

static int EntityQ8Geometry(const sg_bsp_entity_semantic_t *entity,
	sg_rune_q8_vec3_t *origin, sg_rune_q8_bounds_t *bounds, int *has_bounds)
{
	if (entity == NULL || origin == NULL || bounds == NULL || has_bounds == NULL)
		return 0;
	*has_bounds = (entity->flags & SG_BSP_ENTITY_HAS_BOUNDS) != 0U;
	if (*has_bounds)
	{
		uint32_t axis;

		if (!FloatBoundsToQ8(&entity->bounds, bounds))
			return 0;
		/* Inline brush origins are commonly zero even when the brush model is
		 * elsewhere.  Keep a representable center as a fallback witness; a
		 * brush caller below still localizes from the exact bounds. */
		if (!FloatVecToQ8(&entity->origin, origin))
			for (axis = 0U; axis < 3U; axis++)
			{
				int64_t sum = (int64_t)bounds->mins.value[axis] +
					(int64_t)bounds->maxs.value[axis];

				if (sum < (int64_t)INT32_MIN * 2 ||
					sum > (int64_t)INT32_MAX * 2)
					return 0;
				origin->value[axis] = (int32_t)(sum / 2);
			}
	}
	else if (!FloatVecToQ8(&entity->origin, origin))
		return 0;
	return 1;
}

static int FindPointCell(const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_q8_vec3_t *origin, uint32_t *cell_out)
{
	uint32_t cell;

	if (geometry == NULL || origin == NULL || cell_out == NULL)
		return 0;
	for (cell = 0U; cell < geometry->cell_count; cell++)
		if (CellContainsPointQ8(geometry, cell, origin))
		{
			*cell_out = cell;
			return 1;
		}
	return 0;
}

static int CountCellsForBounds(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_q8_bounds_t *bounds, const sg_rune_q8_vec3_t *origin,
	uint32_t *count_out)
{
	uint32_t cell;
	uint32_t count = 0U;

	if (geometry == NULL || bounds == NULL || origin == NULL || count_out == NULL ||
		!BoundsValidQ8(bounds))
		return 0;
	for (cell = 0U; cell < geometry->cell_count; cell++)
		if (BoundsIntersectCellExact(geometry, cell, bounds))
			if (!CountAdd(count, 1U, &count))
				return 0;
	if (count == 0U)
	{
		uint32_t point_cell;

		if (!FindPointCell(geometry, origin, &point_cell) ||
			!BoundsIntersectCellExact(geometry, point_cell, bounds))
			return 0;
		count = 1U;
	}
	*count_out = count;
	return 1;
}

static int FillCellsForBounds(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_q8_bounds_t *bounds, const sg_rune_q8_vec3_t *origin,
	sg_rune_compact_cell_index_t *output, uint32_t expected_count,
	int *origin_owned)
{
	uint32_t cell;
	uint32_t count = 0U;
	int owns_origin = 0;

	if (geometry == NULL || bounds == NULL || origin == NULL || output == NULL ||
		origin_owned == NULL)
		return 0;
	for (cell = 0U; cell < geometry->cell_count; cell++)
		if (BoundsIntersectCellExact(geometry, cell, bounds))
		{
			if (count >= expected_count)
				return 0;
			output[count++].value = cell;
			if (CellContainsPointQ8(geometry, cell, origin))
				owns_origin = 1;
		}
	if (count == 0U)
	{
		uint32_t point_cell;

		if (!FindPointCell(geometry, origin, &point_cell) ||
			!BoundsIntersectCellExact(geometry, point_cell, bounds) ||
			expected_count != 1U)
			return 0;
		output[0].value = point_cell;
		count = 1U;
		owns_origin = 1;
	}
	*origin_owned = owns_origin;
	return count == expected_count && owns_origin;
}

static int DoubleToQ8(double value, int32_t *output)
{
	double scaled;
	double rounded;

	if (output == NULL || !isfinite(value))
		return 0;
	scaled = value * 8.0;
	rounded = nearbyint(scaled);
	if (!isfinite(rounded) || rounded < (double)INT32_MIN ||
		rounded > (double)INT32_MAX)
		return 0;
	*output = (int32_t)rounded;
	return 1;
}

static int TryIntersectionWitness(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t cell_index, const sg_rune_q8_bounds_t *bounds,
	const double point[3], sg_rune_q8_vec3_t *witness_out)
{
	sg_rune_q8_vec3_t witness;
	uint32_t axis;

	if (geometry == NULL || bounds == NULL || point == NULL ||
		witness_out == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!DoubleToQ8(point[axis], &witness.value[axis]))
			return 0;
	if (!PointInBoundsQ8(&witness, bounds) ||
		!CellContainsPointQ8(geometry, cell_index, &witness))
		return 0;
	*witness_out = witness;
	return 1;
}

static int FindCellIntersectionWitness(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t cell_index, const sg_rune_q8_bounds_t *bounds,
	sg_rune_q8_vec3_t *witness_out)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t constraint_count;
	uint32_t first;
	uint32_t middle;
	uint32_t right;
	uint32_t corner;
	double point[3];
	uint32_t axis;

	if (geometry == NULL || bounds == NULL || witness_out == NULL ||
		cell_index >= geometry->cell_count || !BoundsIntersectCellExact(geometry,
		cell_index, bounds))
		return 0;
	cell = &geometry->cells[cell_index];
	for (corner = 0U; corner < 8U; corner++)
	{
		for (axis = 0U; axis < 3U; axis++)
			point[axis] = (double)((corner & (1U << axis)) != 0U ?
				bounds->maxs.value[axis] : bounds->mins.value[axis]) / 8.0;
		if (TryIntersectionWitness(geometry, cell_index, bounds, point,
			witness_out))
			return 1;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		double lower = fmax(
			(double)bounds->mins.value[axis] / 8.0,
			(double)cell->bounds.mins.value[axis] / 8.0);
		double upper = fmin(
			(double)bounds->maxs.value[axis] / 8.0,
			(double)cell->bounds.maxs.value[axis] / 8.0);

		point[axis] = (lower + upper) * 0.5;
	}
	if (TryIntersectionWitness(geometry, cell_index, bounds, point,
		witness_out))
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		point[axis] = ((double)bounds->mins.value[axis] +
			(double)bounds->maxs.value[axis]) / 16.0;
	if (TryIntersectionWitness(geometry, cell_index, bounds, point,
		witness_out))
		return 1;
	if (!CountAdd(cell->incidences.count, 12U, &constraint_count))
		return 0;
	for (first = 0U; first < constraint_count; first++)
	{
		double first_normal[3];
		double first_distance;

			if (!IntersectionConstraintPlane(geometry, cell_index, bounds, first,
				first_normal, &first_distance))
			return 0;
		for (middle = first + 1U; middle < constraint_count; middle++)
		{
			double middle_normal[3];
			double middle_distance;

			if (!IntersectionConstraintPlane(geometry, cell_index, bounds,
				middle, middle_normal, &middle_distance))
				return 0;
			for (right = middle + 1U; right < constraint_count; right++)
			{
				double right_normal[3];
				double right_distance;

				if (!IntersectionConstraintPlane(geometry, cell_index, bounds,
					right, right_normal, &right_distance) ||
					!SolvePlaneTriple(first_normal, first_distance,
						middle_normal, middle_distance, right_normal,
						right_distance, point))
					continue;
				if (TryIntersectionWitness(geometry, cell_index, bounds, point,
					witness_out))
					return 1;
			}
		}
	}
	return 0;
}

static int FindEntityWitness(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_bsp_entity_semantic_t *entity, sg_rune_q8_vec3_t *origin_out,
	sg_rune_q8_bounds_t *bounds_out, int *has_bounds_out, uint32_t *cell_out)
{
	sg_rune_q8_vec3_t origin;
	sg_rune_q8_bounds_t bounds;
	int has_bounds;
	uint32_t cell;

	if (geometry == NULL || entity == NULL || origin_out == NULL ||
		bounds_out == NULL || has_bounds_out == NULL || cell_out == NULL ||
		!EntityQ8Geometry(entity, &origin, &bounds, &has_bounds))
		return 0;
	if (!has_bounds)
	{
		if (!FindPointCell(geometry, &origin, &cell))
			return 0;
		*origin_out = origin;
		if (!PointBounds(&origin, bounds_out))
			return 0;
		*has_bounds_out = 0;
		*cell_out = cell;
		return 1;
	}
	if ((entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U &&
		PointInBoundsQ8(&origin, &bounds) &&
		FindPointCell(geometry, &origin, &cell))
	{
		*origin_out = origin;
		*bounds_out = bounds;
		*has_bounds_out = 1;
		*cell_out = cell;
		return 1;
	}
	for (cell = 0U; cell < geometry->cell_count; cell++)
	{
		if (!FindCellIntersectionWitness(geometry, cell, &bounds, &origin))
			continue;
		*origin_out = origin;
		*bounds_out = bounds;
		*has_bounds_out = 1;
		*cell_out = cell;
		return 1;
	}
	return 0;
}

static int AuthoritySpanValid(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

static int AuthorityEntityIndex(
	const sg_bsp_entity_semantics_t *entities,
	sg_rune_compact_mechanism_entity_ref_t reference, uint32_t *index_out)
{
	if (entities == NULL || index_out == NULL || reference.entity_ordinal >=
		entities->entity_count || entities->entities == NULL ||
		entities->entities[reference.entity_ordinal].canonical_ordinal !=
			reference.entity_ordinal)
		return 0;
	*index_out = reference.entity_ordinal;
	return 1;
}

static int AuthorityMechanismKindValid(
	sg_rune_compact_mechanism_authority_kind_t kind)
{
	return (uint32_t)kind <
		(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT;
}

static int MapAuthorityMechanismKind(
	sg_rune_compact_mechanism_authority_kind_t source,
	sg_rune_compact_mechanism_kind_t *output)
{
	if (output == NULL)
		return 0;
	switch (source)
	{
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR:
		*output = SG_RUNE_COMPACT_MECHANISM_DOOR;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON:
		*output = SG_RUNE_COMPACT_MECHANISM_BUTTON;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT:
		*output = SG_RUNE_COMPACT_MECHANISM_LIFT;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN:
		*output = SG_RUNE_COMPACT_MECHANISM_TRAIN;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH:
		*output = SG_RUNE_COMPACT_MECHANISM_PUSH;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT:
		*output = SG_RUNE_COMPACT_MECHANISM_TELEPORT;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRIGGER:
		*output = SG_RUNE_COMPACT_MECHANISM_TRIGGER;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR:
		*output = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT:
		break;
	}
	return 0;
}

static int FiniteAngularDoorAuthority(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_authority_t *authority)
{
	const sg_bsp_entity_semantic_t *entity;
	uint32_t entity_index;

	if (input == NULL || input->entities == NULL || authority == NULL ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR ||
		!AuthorityEntityIndex(input->entities, authority->source, &entity_index))
		return 0;
	entity = &input->entities->entities[entity_index];
	return entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR &&
		SG_BspEntitySemanticHasFiniteAngularDoor(entity);
}

static int ContinuousRotatorAuthority(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_authority_t *authority)
{
	const sg_bsp_entity_semantic_t *entity;
	uint32_t entity_index;

	if (input == NULL || input->entities == NULL || authority == NULL ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR ||
		!AuthorityEntityIndex(input->entities, authority->source, &entity_index))
		return 0;
	entity = &input->entities->entities[entity_index];
	return entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR &&
		entity->angular_mover.kind ==
			SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
}

static int AuthorityTransitionKindAllowed(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	sg_rune_compact_mechanism_transition_kind_t transition_kind)
{
	if (authority == NULL)
		return 0;
	switch (authority->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR:
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON:
		return transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
			transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT:
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN:
		return transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH:
		return transition_kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT:
		return transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRIGGER:
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT:
		break;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR:
		return ((transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
			transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT) &&
			FiniteAngularDoorAuthority(input, authority)) ||
			(transition_kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
			ContinuousRotatorAuthority(input, authority));
	}
	return 0;
}

static int MapAuthorityState(
	sg_rune_compact_mechanism_authority_state_t source,
	sg_rune_compact_mechanism_state_t *output)
{
	if (output == NULL)
		return 0;
	switch (source)
	{
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE:
		*output = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVATING:
		*output = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVATING;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE:
		*output = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_RETURNING:
		*output = SG_RUNE_COMPACT_MECHANISM_STATE_RETURNING;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_DISABLED:
		*output = SG_RUNE_COMPACT_MECHANISM_STATE_DISABLED;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT:
		break;
	}
	return 0;
}

static int AuthorityStateValid(
	sg_rune_compact_mechanism_authority_state_t state)
{
	return (uint32_t)state <
		(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT;
}

static int MapActivationMask(
	sg_rune_compact_mechanism_activation_mask_t source,
	sg_rune_compact_static_activation_mask_t *output)
{
	if (output == NULL || source == 0U ||
		(source & (sg_rune_compact_mechanism_activation_mask_t)
			~SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U)
		return 0;
	/* The authority and static masks intentionally share bit positions, but
	 * the explicit assignment keeps that wire-level fact visible at the type
	 * boundary. */
	*output = (sg_rune_compact_static_activation_mask_t)source;
	return 1;
}

/* The mechanism owner deliberately uses a tagged union so that portal state,
 * teleport, push, and mover-transport facts cannot be confused.  The static
 * materializer has one canonical projection point; keep the union decoding in
 * one function so every consumer applies the same sentinel and provenance
 * rules.  Fields not carried by a selected authority variant remain explicit
 * sentinels rather than being guessed from entity geometry. */
typedef struct authority_transition_fields_s
{
	sg_rune_compact_mechanism_transition_kind_t kind;
	uint32_t portal;
	uint32_t destination;
	uint32_t entry_cell;
	uint32_t exit_cell;
	sg_rune_q8_vec3_t approach_witness;
	sg_rune_q8_vec3_t entry_witness;
	sg_rune_q8_vec3_t exit_witness;
	uint32_t launch_velocity_bits[3];
	uint32_t arrival_velocity_bits[3];
	uint32_t gravity_bits;
	uint32_t flight_ms;
	uint32_t mover_model;
	uint32_t fanout_ordinal;
	uint32_t source_surface_ordinal;
	sg_rune_compact_mechanism_authority_state_t source_state;
	sg_rune_compact_mechanism_authority_state_t destination_state;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t pause_ms;
	uint32_t travel_ms;
	uint32_t recovery_ms;
	uint8_t source_blocked;
	uint8_t destination_blocked;
	sg_rune_q8_vec3_t source_player_local;
	sg_rune_q8_vec3_t destination_player_local;
	sg_rune_q8_vec3_t source_support_local;
	sg_rune_q8_vec3_t destination_support_local;
	uint32_t source_player_world_bits[3];
	uint32_t destination_player_world_bits[3];
	uint32_t source_support_world_bits[3];
	uint32_t destination_support_world_bits[3];
	uint32_t source_mover_origin_bits[3];
	uint32_t source_mover_axis_bits[3][3];
	uint32_t destination_mover_origin_bits[3];
	uint32_t destination_mover_axis_bits[3][3];
	uint64_t elapsed_ms;
	uint32_t source_endpoint;
	uint32_t destination_endpoint;
	uint8_t swept_static_clear;
	uint8_t start_supported;
	uint8_t end_supported;
	uint8_t stance;
} authority_transition_fields_t;

/* Transition state semantics belong to the typed authority fact.  In
 * particular, teleport and push are stateless transport observations: an
 * inactive-to-active edge would invent an activation transition that the
 * host never supplied.  Keep the rules here so both the authority-span and
 * global transition passes, and therefore the emitted projection, agree. */
static int AuthorityTransitionStatesValid(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	const authority_transition_fields_t *fields)
{
	int portal_states_valid;

	if (authority == NULL || fields == NULL)
		return 0;
	switch (fields->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		portal_states_valid = fields->source_state == authority->initial_state &&
			fields->destination_state == authority->activated_state;
		if (!portal_states_valid &&
			(authority->flags &
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) == 0U &&
			fields->source_state == authority->activated_state)
		{
			if (authority->initial_state != authority->activated_state &&
				authority->reset_state == authority->activated_state)
				portal_states_valid = fields->destination_state ==
					authority->initial_state;
			else if (authority->reset_state != authority->activated_state)
				portal_states_valid = fields->destination_state ==
					authority->reset_state;
		}
		return (authority->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
			authority->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
			FiniteAngularDoorAuthority(input, authority)) &&
			portal_states_valid;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		return fields->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			fields->destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
			FiniteAngularDoorAuthority(input, authority))
			return fields->source_state == authority->initial_state &&
				fields->destination_state == authority->activated_state;
		if (ContinuousRotatorAuthority(input, authority))
			return fields->source_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
				fields->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
		if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
		{
			if ((authority->activation &
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO) != 0U)
				return fields->source_state ==
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
					fields->destination_state ==
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
			return fields->source_state == authority->initial_state &&
				fields->destination_state == authority->activated_state;
		}
		return 0;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

static int AuthorityTransitionWitnessesValid(
	const materializer_input_view_t *input,
	const authority_transition_fields_t *fields)
{
	if (input == NULL || fields == NULL)
		return 0;
	switch (fields->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		return CellContainsPointQ8(&input->geometry, fields->entry_cell,
			&fields->approach_witness) &&
			CellContainsPointQ8(&input->geometry, fields->entry_cell,
				&fields->entry_witness) &&
			CellContainsPointQ8(&input->geometry, fields->exit_cell,
				&fields->exit_witness);
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		return 1;
	}
	return 0;
}

static int AuthorityTrainEndpointFactValid(
	const sg_rune_compact_mechanisms_view_t *view,
	const sg_rune_compact_mechanism_authority_t *authority,
	const authority_transition_fields_t *fields)
{
	uint32_t local;

	if (view == NULL || authority == NULL || fields == NULL ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN ||
		fields->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT ||
		view->topology_edges == NULL)
		return 0;
	for (local = 0U; local < authority->topology.count; local++)
	{
		uint32_t topology_index;
		const sg_rune_compact_mechanism_topology_edge_t *edge;

		if (!CountAdd(authority->topology.first, local, &topology_index) ||
			topology_index >= view->topology_edge_count)
			return 0;
		edge = &view->topology_edges[topology_index];
		if (edge->kind == SG_MECH_EDGE_TARGET &&
			edge->source.entity_ordinal == fields->source_endpoint &&
			edge->destination.entity_ordinal == fields->destination_endpoint &&
			edge->fanout_ordinal == fields->fanout_ordinal)
			return 1;
	}
	return 0;
}

/* Teleporter destinations are topology facts, not arbitrary entity
 * references.  Join the transition's complete `(owner, destination, fanout)`
 * key to the exact TARGET edge retained in the owner's authenticated span so
 * a copied transition cannot redirect a teleporter to another destination. */
static int AuthorityTeleportTargetFactValid(
	const sg_rune_compact_mechanisms_view_t *view,
	const sg_rune_compact_mechanism_authority_t *authority,
	const authority_transition_fields_t *fields)
{
	uint32_t local;

	if (view == NULL || authority == NULL || fields == NULL ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ||
		fields->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT ||
		view->topology_edges == NULL ||
		fields->destination == SG_RUNE_COMPACT_INDEX_NONE ||
		fields->fanout_ordinal == UINT32_MAX)
		return 0;
	for (local = 0U; local < authority->topology.count; local++)
	{
		uint32_t topology_index;
		const sg_rune_compact_mechanism_topology_edge_t *edge;

		if (!CountAdd(authority->topology.first, local, &topology_index) ||
			topology_index >= view->topology_edge_count)
			return 0;
		edge = &view->topology_edges[topology_index];
		if (edge->kind == SG_MECH_EDGE_TARGET &&
			edge->source.entity_ordinal == authority->source.entity_ordinal &&
			edge->destination.entity_ordinal == fields->destination &&
			edge->fanout_ordinal == fields->fanout_ordinal)
			return 1;
	}
	return 0;
}

static int AuthorityTransitionKindValid(
	sg_rune_compact_mechanism_transition_kind_t kind);

static int AuthorityTransitionInactiveTailZero(
	const sg_rune_compact_mechanism_transition_t *transition)
{
	const unsigned char *bytes;
	size_t active_size;
	size_t index;

	if (transition == NULL)
		return 0;
	switch (transition->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		active_size = sizeof(transition->value.portal_state);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		active_size = sizeof(transition->value.teleport);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		active_size = sizeof(transition->value.push);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		active_size = sizeof(transition->value.transport);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		return 0;
	}
	bytes = (const unsigned char *)&transition->value;
	for (index = active_size; index < sizeof(transition->value); index++)
		if (bytes[index] != 0U)
			return 0;
	return 1;
}

static int AuthorityPortalBlockedFactsValid(
	const sg_rune_compact_mechanism_portal_state_t *portal)
{
	if (portal == NULL || portal->source_blocked > 1U ||
		portal->destination_blocked > 1U ||
		portal->source_blocked == portal->destination_blocked ||
		portal->reserved[0] != 0U || portal->reserved[1] != 0U)
		return 0;
	return 1;
}

static int DecodeAuthorityTransition(
	const sg_rune_compact_mechanism_transition_t *source,
	authority_transition_fields_t *output)
{
	uint32_t axis;

	if (source == NULL || output == NULL ||
		!AuthorityTransitionKindValid(source->kind) ||
		!AuthorityTransitionInactiveTailZero(source))
		return 0;
	memset(output, 0, sizeof(*output));
	output->kind = source->kind;
	output->entry_cell = source->entry_cell.value;
	output->exit_cell = source->exit_cell.value;
	output->source_state = source->source_state;
	output->destination_state = source->destination_state;
	output->elapsed_ms = source->elapsed_ms;
	output->portal = SG_RUNE_COMPACT_INDEX_NONE;
	output->destination = SG_RUNE_COMPACT_INDEX_NONE;
	output->mover_model = SG_BSP_ENTITY_MODEL_NONE;
	output->fanout_ordinal = UINT32_MAX;
	output->source_surface_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	output->source_endpoint = SG_RUNE_COMPACT_INDEX_NONE;
	output->destination_endpoint = SG_RUNE_COMPACT_INDEX_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		output->launch_velocity_bits[axis] = 0U;
		output->arrival_velocity_bits[axis] = 0U;
		output->source_player_world_bits[axis] = 0U;
		output->destination_player_world_bits[axis] = 0U;
		output->source_support_world_bits[axis] = 0U;
		output->destination_support_world_bits[axis] = 0U;
		output->source_mover_origin_bits[axis] = 0U;
		output->destination_mover_origin_bits[axis] = 0U;
		output->source_mover_axis_bits[axis][0] = 0U;
		output->source_mover_axis_bits[axis][1] = 0U;
		output->source_mover_axis_bits[axis][2] = 0U;
		output->destination_mover_axis_bits[axis][0] = 0U;
		output->destination_mover_axis_bits[axis][1] = 0U;
		output->destination_mover_axis_bits[axis][2] = 0U;
	}
	switch (source->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		if (!AuthorityPortalBlockedFactsValid(&source->value.portal_state))
			return 0;
		output->portal = source->value.portal_state.portal.value;
		output->mover_model = source->value.portal_state.mover_model;
		output->delay_ms = source->value.portal_state.delay_ms;
		output->dwell_ms = source->value.portal_state.dwell_ms;
		output->pause_ms = source->value.portal_state.pause_ms;
		output->travel_ms = source->value.portal_state.travel_ms;
		output->recovery_ms = source->value.portal_state.recovery_ms;
		output->source_blocked = source->value.portal_state.source_blocked;
		output->destination_blocked =
			source->value.portal_state.destination_blocked;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		output->destination = source->value.teleport.destination.entity_ordinal;
		output->fanout_ordinal = source->value.teleport.fanout_ordinal;
		output->approach_witness = source->value.teleport.approach_witness;
		output->entry_witness = source->value.teleport.entry_witness;
		output->exit_witness = source->value.teleport.exit_witness;
		for (axis = 0U; axis < 3U; axis++)
			output->arrival_velocity_bits[axis] =
				source->value.teleport.arrival_velocity_bits[axis];
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		output->approach_witness = source->value.push.approach_witness;
		output->entry_witness = source->value.push.entry_witness;
		output->exit_witness = source->value.push.exit_witness;
		for (axis = 0U; axis < 3U; axis++)
			output->launch_velocity_bits[axis] =
				source->value.push.launch_velocity_bits[axis];
		output->gravity_bits = source->value.push.gravity_bits;
		output->flight_ms = source->value.push.flight_ms;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		output->mover_model = source->value.transport.mover_model;
		output->source_surface_ordinal =
			source->value.transport.source_surface_ordinal;
		output->source_player_local = source->value.transport.source_player_local;
		output->destination_player_local =
			source->value.transport.destination_player_local;
		output->source_support_local = source->value.transport.source_support_local;
		output->destination_support_local =
			source->value.transport.destination_support_local;
		for (axis = 0U; axis < 3U; axis++)
		{
			output->source_player_world_bits[axis] =
				source->value.transport.source_player_world_bits[axis];
			output->destination_player_world_bits[axis] =
				source->value.transport.destination_player_world_bits[axis];
				output->source_support_world_bits[axis] =
					source->value.transport.source_support_world_bits[axis];
				output->destination_support_world_bits[axis] =
					source->value.transport.destination_support_world_bits[axis];
				output->source_mover_origin_bits[axis] =
					source->value.transport.source_mover_origin_bits[axis];
				output->destination_mover_origin_bits[axis] =
					source->value.transport.destination_mover_origin_bits[axis];
				{
					uint32_t column;

					for (column = 0U; column < 3U; column++)
					{
						output->source_mover_axis_bits[axis][column] =
							source->value.transport.source_mover_axis_bits[axis][column];
						output->destination_mover_axis_bits[axis][column] =
							source->value.transport.destination_mover_axis_bits[axis][column];
					}
				}
		}
		output->source_endpoint =
			source->value.transport.source_endpoint.entity_ordinal;
		output->destination_endpoint =
			source->value.transport.destination_endpoint.entity_ordinal;
		output->fanout_ordinal = source->value.transport.fanout_ordinal;
		output->swept_static_clear = source->value.transport.swept_static_clear;
		output->start_supported = source->value.transport.start_supported;
	output->end_supported = source->value.transport.end_supported;
	output->stance = source->value.transport.stance;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

static int AuthorityTransitionKindValid(
	sg_rune_compact_mechanism_transition_kind_t kind)
{
	return (uint32_t)kind <
		(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT;
}

/* A transport transition may cite only the exact all-model source root that
 * the geometry owner authenticated.  A coplanar world facet, or a source
 * surface with the right plane but a different brush side, is not evidence
 * that a mover owns that surface. */
static int AuthorityTransportSourceValid(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	const authority_transition_fields_t *fields)
{
	const sg_bsp_entity_semantic_t *entity;
	const sg_rune_compact_source_surface_t *surface;
	uint32_t entity_index;
	int team_model_valid;

	if (input == NULL || authority == NULL || fields == NULL ||
		fields->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT ||
		fields->source_surface_ordinal == SG_RUNE_COMPACT_INDEX_NONE ||
		fields->source_surface_ordinal >= input->geometry.source_surface_count ||
		!AuthorityEntityIndex(input->entities, authority->source, &entity_index))
		return 0;
	entity = &input->entities->entities[entity_index];
	surface = &input->geometry.source_surfaces[
		fields->source_surface_ordinal];
	if ((entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
		entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
		!CompactSourceSurfaceValid(&input->geometry,
			fields->source_surface_ordinal) ||
		fields->mover_model == SG_HOST_COLLISION_MODEL_WORLD ||
		surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL ||
		surface->cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->parent_surface != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->split_ordinal != 0U ||
		surface->source.model != fields->mover_model)
		return 0;
	/* A TEAM mover may authenticate a selected member model distinct from the
	 * root entity's model.  The exact entity-semantics TEAM closure and its
	 * echoed authority topology are the only accepted join; a bare in-range
	 * model number is not enough. */
	team_model_valid = AuthorityTeamMoverModelValid(input, authority,
		fields->mover_model);
	return team_model_valid > 0;
}

static int AuthorityTransportWorldBitsValid(
	const authority_transition_fields_t *fields)
{
	uint32_t axis;

	if (fields == NULL ||
		fields->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!Binary32CanonicalFinite(fields->source_player_world_bits[axis]) ||
			!Binary32CanonicalFinite(
				fields->destination_player_world_bits[axis]) ||
			!Binary32CanonicalFinite(
				fields->source_support_world_bits[axis]) ||
			!Binary32CanonicalFinite(
				fields->destination_support_world_bits[axis]) ||
			!Binary32CanonicalFinite(
				fields->source_mover_origin_bits[axis]) ||
			!Binary32CanonicalFinite(
				fields->destination_mover_origin_bits[axis]) ||
			!Binary32CanonicalFinite(
				fields->source_mover_axis_bits[axis][0]) ||
			!Binary32CanonicalFinite(
				fields->source_mover_axis_bits[axis][1]) ||
			!Binary32CanonicalFinite(
				fields->source_mover_axis_bits[axis][2]) ||
			!Binary32CanonicalFinite(
				fields->destination_mover_axis_bits[axis][0]) ||
			!Binary32CanonicalFinite(
				fields->destination_mover_axis_bits[axis][1]) ||
			!Binary32CanonicalFinite(
				fields->destination_mover_axis_bits[axis][2]))
			return 0;
	return 1;
}

static int AuthorityTransportPoseValid(
	const materializer_input_view_t *input,
	const authority_transition_fields_t *fields)
{
	const sg_rune_compact_hull_t *hull;
	int64_t expected_player_z;
	uint32_t expected_source_player_world[3];
	uint32_t expected_destination_player_world[3];
	uint32_t expected_source_support_world[3];
	uint32_t expected_destination_support_world[3];
	uint32_t axis;

	if (input == NULL || fields == NULL ||
		fields->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
		return 0;
	if (fields->stance >= SG_RUNE_STANCE_COUNT)
		return 0;
	hull = fields->stance == SG_RUNE_STANCE_STANDING ?
		&input->geometry.identity.standing_hull :
		&input->geometry.identity.crouching_hull;
	for (axis = 0U; axis < 3U; axis++)
		if (fields->source_player_local.value[axis] !=
				fields->destination_player_local.value[axis] ||
			fields->source_support_local.value[axis] !=
				fields->destination_support_local.value[axis])
			return 0;
	for (axis = 0U; axis < 2U; axis++)
		if (fields->source_player_local.value[axis] !=
				fields->source_support_local.value[axis] ||
			fields->destination_player_local.value[axis] !=
				fields->destination_support_local.value[axis])
			return 0;
	expected_player_z = (int64_t)fields->source_support_local.value[2] -
		(int64_t)hull->mins.value[2];
	if (expected_player_z < INT32_MIN || expected_player_z > INT32_MAX ||
		fields->source_player_local.value[2] != (int32_t)expected_player_z)
		return 0;
	expected_player_z = (int64_t)fields->destination_support_local.value[2] -
		(int64_t)hull->mins.value[2];
	if (expected_player_z < INT32_MIN || expected_player_z > INT32_MAX ||
		fields->destination_player_local.value[2] != (int32_t)expected_player_z)
		return 0;
	if (!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&fields->source_player_local,
			fields->source_mover_origin_bits,
			fields->source_mover_axis_bits,
			expected_source_player_world) ||
		!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&fields->destination_player_local,
			fields->destination_mover_origin_bits,
			fields->destination_mover_axis_bits,
			expected_destination_player_world) ||
		!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&fields->source_support_local,
			fields->source_mover_origin_bits,
			fields->source_mover_axis_bits,
			expected_source_support_world) ||
		!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&fields->destination_support_local,
			fields->destination_mover_origin_bits,
			fields->destination_mover_axis_bits,
			expected_destination_support_world))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (fields->source_player_world_bits[axis] !=
			expected_source_player_world[axis] ||
			fields->destination_player_world_bits[axis] !=
				expected_destination_player_world[axis] ||
			fields->source_support_world_bits[axis] !=
				expected_source_support_world[axis] ||
			fields->destination_support_world_bits[axis] !=
				expected_destination_support_world[axis])
			return 0;
	return 1;
}

static int AuthorityTransportWorldPointsValid(
	const materializer_input_view_t *input,
	const authority_transition_fields_t *fields)
{
	double source_player[3];
	double destination_player[3];
	double source_support[3];
	double destination_support[3];
	uint32_t axis;

	if (input == NULL || fields == NULL ||
		fields->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT ||
		fields->entry_cell >= input->geometry.cell_count ||
		fields->exit_cell >= input->geometry.cell_count)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		source_player[axis] = (double)BitsFloat(
			fields->source_player_world_bits[axis]);
		destination_player[axis] = (double)BitsFloat(
			fields->destination_player_world_bits[axis]);
		source_support[axis] = (double)BitsFloat(
			fields->source_support_world_bits[axis]);
		destination_support[axis] = (double)BitsFloat(
			fields->destination_support_world_bits[axis]);
	}
	return CellContainsPointDouble(&input->geometry, fields->entry_cell,
		 source_player) &&
		CellContainsPointDouble(&input->geometry, fields->entry_cell,
			source_support) &&
		CellContainsPointDouble(&input->geometry, fields->exit_cell,
			destination_player) &&
		CellContainsPointDouble(&input->geometry, fields->exit_cell,
			destination_support);
}

static int MapAuthorityTransitionKind(
	sg_rune_compact_mechanism_transition_kind_t source,
	sg_rune_compact_static_transition_kind_t *output)
{
	if (output == NULL)
		return 0;
	switch (source)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		*output = SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		*output = SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		*output = SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		*output = SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

static int MechanismAuthorityViewValid(
	const materializer_input_view_t *input,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_rune_compact_mechanisms_view_t *view;
	const sg_bsp_entity_semantics_t *entities;
	uint32_t index;

	if (input == NULL || input->mechanisms == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	view = input->mechanisms;
	entities = input->entities;
	if (!CompactIdentityEqual(&view->identity, &input->geometry.identity))
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	if (view->mechanism_count > SG_RUNE_COMPACT_MAX_MECHANISMS ||
		view->controller_count > SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS ||
		view->topology_edge_count > SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ||
		view->transition_count > SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS ||
		(view->mechanism_count != 0U && view->mechanisms == NULL) ||
		(view->controller_count != 0U && view->controllers == NULL) ||
		(view->topology_edge_count != 0U && view->topology_edges == NULL) ||
		(view->transition_count != 0U && view->transitions == NULL))
	{
		SetError(error,
			(view->mechanism_count > SG_RUNE_COMPACT_MAX_MECHANISMS ||
			 view->controller_count > SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS ||
			 view->topology_edge_count > SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ||
			 view->transition_count > SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS) ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	for (index = 0U; index < view->mechanism_count; index++)
	{
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[index];
		uint32_t entity_index;
		uint32_t other;
		sg_rune_compact_mechanism_kind_t static_kind;
		const uint32_t known_activation =
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN;

		if (!AuthorityEntityIndex(entities, authority->source, &entity_index) ||
			!EntityIsCanonicalMechanism(&entities->entities[entity_index]) ||
			EntityIsTeleporterDestination(&entities->entities[entity_index]) ||
			!AuthorityMechanismKindValid(authority->kind) ||
			!MapAuthorityMechanismKind(authority->kind, &static_kind) ||
			(sg_rune_mechanism_kind_t)static_kind !=
				entities->entities[entity_index].mechanism_kind ||
			authority->activation == 0U ||
			(authority->activation &
				(sg_rune_compact_mechanism_activation_mask_t)
				~known_activation) != 0U ||
			(authority->flags |
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN) !=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN ||
			((authority->activation &
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) != 0U &&
				authority->health <= 0) ||
			((authority->activation &
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) == 0U &&
				authority->health != 0) ||
			((authority->activation &
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) != 0U &&
				authority->required_item == SG_BSP_ENTITY_STRING_NONE) ||
			((authority->activation &
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) == 0U &&
				authority->required_item != SG_BSP_ENTITY_STRING_NONE) ||
			!EntityStringOffsetValid(entities, authority->required_item) ||
			authority->required_item !=
				entities->entities[entity_index].required_item ||
			!AuthorityStateValid(authority->initial_state) ||
			!AuthorityStateValid(authority->activated_state) ||
			!AuthorityStateValid(authority->reset_state) ||
			authority->activation_cell.value >= input->geometry.cell_count ||
			!AuthoritySpanValid(authority->controllers.first,
				authority->controllers.count, view->controller_count) ||
			!AuthoritySpanValid(authority->topology.first,
				authority->topology.count, view->topology_edge_count) ||
			!AuthoritySpanValid(authority->transitions.first,
				authority->transitions.count, view->transition_count))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
		if (!BoundsValidQ8(&authority->activation_bounds) ||
			!PointInBoundsQ8(&authority->activation_witness,
				&authority->activation_bounds) ||
			!CellContainsPointQ8(&input->geometry,
				authority->activation_cell.value,
				&authority->activation_witness))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
		for (other = 0U; other < index; other++)
			if (view->mechanisms[other].source.entity_ordinal ==
				authority->source.entity_ordinal)
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_AMBIGUOUS_BINDING,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
				return 0;
			}
		{
			uint32_t local;

			for (local = 0U; local < authority->controllers.count; local++)
			{
				uint32_t controller_index = SG_RUNE_COMPACT_INDEX_NONE;
				uint32_t controller_entity_index = SG_RUNE_COMPACT_INDEX_NONE;

				if (!CountAdd(authority->controllers.first, local,
					&controller_index) ||
					view->controllers[controller_index].mechanism != index ||
					!AuthorityEntityIndex(entities,
						view->controllers[controller_index].controller,
						&controller_entity_index))
				{
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
						index);
					return 0;
				}
				/* The same controller entity may legitimately contribute multiple
				 * facts.  Its topology ordinal and activation witness remain part of
				 * the fact identity and are preserved by the static table. */
			}

			for (local = 0U; local < authority->topology.count; local++)
			{
				uint32_t edge_index = SG_RUNE_COMPACT_INDEX_NONE;

				if (!CountAdd(authority->topology.first, local, &edge_index) ||
					edge_index >= view->topology_edge_count ||
					!AuthorityTopologyEdgeAccepted(view, index, edge_index,
						&view->topology_edges[edge_index], entities))
				{
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE,
						edge_index);
					return 0;
				}
			}
		}
		if ((authority->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH) &&
			authority->transitions.count == 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
		{
			uint32_t local;

			for (local = 0U; local < authority->transitions.count; local++)
			{
				uint32_t transition_index;
				const sg_rune_compact_mechanism_transition_t *transition;
				authority_transition_fields_t fields;
			int valid = AuthoritySpanValid(authority->transitions.first,
					authority->transitions.count, view->transition_count) &&
				CountAdd(authority->transitions.first, local,
					&transition_index);

				if (valid)
				{
					transition = &view->transitions[transition_index];
					valid = DecodeAuthorityTransition(transition, &fields) &&
						transition->mechanism == index &&
						AuthorityTransitionKindAllowed(input, authority,
							fields.kind) &&
						AuthorityTransitionStatesValid(input, authority, &fields) &&
						AuthorityTransitionWitnessesValid(input, &fields);
					if (valid && authority->kind ==
						SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT)
						valid = fields.fanout_ordinal == local &&
							AuthorityTeleportTargetFactValid(view, authority, &fields);
					if (valid && fields.kind ==
						SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
						valid = AuthorityTransportSourceValid(input, authority,
							&fields) && fields.swept_static_clear == 1U &&
							fields.start_supported == 1U &&
							fields.end_supported == 1U &&
							((authority->kind !=
								SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
							 fields.fanout_ordinal == UINT32_MAX &&
							 fields.source_endpoint == SG_RUNE_COMPACT_INDEX_NONE &&
								 fields.destination_endpoint ==
									SG_RUNE_COMPACT_INDEX_NONE) ||
							 (authority->kind ==
								SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
							 fields.fanout_ordinal != UINT32_MAX &&
							 EntityRefIsValid(fields.source_endpoint,
								entities->entity_count) &&
									 EntityRefIsValid(fields.destination_endpoint,
									entities->entity_count) &&
									fields.source_endpoint != fields.destination_endpoint)) &&
							AuthorityTransportWorldBitsValid(&fields) &&
							AuthorityTransportPoseValid(input, &fields) &&
							AuthorityTransportWorldPointsValid(input, &fields);
					if (valid && authority->kind ==
						SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
						valid = AuthorityTrainEndpointFactValid(view, authority,
							&fields);
				}
				if (!valid)
				{
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
						local);
					return 0;
				}
			}
		}
	}
	for (index = 0U; index < view->controller_count; index++)
	{
		const sg_rune_compact_mechanism_controller_t *controller =
			&view->controllers[index];
		uint32_t entity_index;

		if (controller->mechanism >= view->mechanism_count ||
			!AuthorityEntityIndex(entities, controller->controller, &entity_index) ||
			!EntityIsCanonicalMechanism(&entities->entities[entity_index]) ||
			EntityIsTeleporterDestination(&entities->entities[entity_index]) ||
			(controller->topology_edge != SG_RUNE_COMPACT_INDEX_NONE &&
				(controller->topology_edge >= view->topology_edge_count ||
				 controller->topology_edge <
					view->mechanisms[controller->mechanism].topology.first ||
				 controller->topology_edge -
					view->mechanisms[controller->mechanism].topology.first >=
					view->mechanisms[controller->mechanism].topology.count ||
					view->topology_edges[controller->topology_edge].source.entity_ordinal !=
					controller->controller.entity_ordinal)))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
	}
	for (index = 0U; index < view->topology_edge_count; index++)
	{
		const sg_rune_compact_mechanism_topology_edge_t *edge =
			&view->topology_edges[index];
		uint32_t source_index;
		uint32_t destination_index;

		if (!AuthorityEntityIndex(entities, edge->source, &source_index) ||
			!AuthorityEntityIndex(entities, edge->destination, &destination_index) ||
			(uint32_t)edge->kind > (uint32_t)SG_MECH_EDGE_ROUTE_TARGET ||
			edge->fanout_ordinal == UINT32_MAX)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
		(void)source_index;
		(void)destination_index;
	}
	for (index = 0U; index < view->transition_count; index++)
	{
		const sg_rune_compact_mechanism_transition_t *transition =
			&view->transitions[index];
		const sg_rune_compact_mechanism_authority_t *authority;
		authority_transition_fields_t fields;
		int physics_zero;
		uint32_t axis;

		if (!DecodeAuthorityTransition(transition, &fields) ||
			transition->mechanism >= view->mechanism_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
		authority = &view->mechanisms[transition->mechanism];
		if (authority->transitions.first > index ||
			index - authority->transitions.first >= authority->transitions.count ||
			!AuthorityTransitionKindAllowed(input, authority, fields.kind) ||
			!AuthorityTransitionStatesValid(input, authority, &fields) ||
			!AuthorityTransitionWitnessesValid(input, &fields) ||
			(authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT &&
				!AuthorityTeleportTargetFactValid(view, authority, &fields)) ||
			(authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
				!AuthorityTrainEndpointFactValid(view, authority, &fields)))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
		physics_zero = fields.gravity_bits == 0U && fields.flight_ms == 0U;
		for (axis = 0U; axis < 3U; axis++)
			physics_zero = physics_zero &&
				fields.launch_velocity_bits[axis] == 0U;
		if (fields.entry_cell >= input->geometry.cell_count ||
			fields.exit_cell >= input->geometry.cell_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
		if (fields.kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
		{
			if (fields.portal >= input->geometry.portal_count ||
				fields.destination != SG_RUNE_COMPACT_INDEX_NONE ||
				fields.fanout_ordinal != UINT32_MAX ||
				fields.mover_model == SG_BSP_ENTITY_MODEL_NONE ||
				fields.mover_model >=
					input->geometry.identity.source_counts.model_count ||
				!physics_zero || !AuthorityStateValid(fields.source_state) ||
				!AuthorityStateValid(fields.destination_state))
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, index);
				return 0;
			}
		}
		else if (fields.kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
		{
			uint32_t destination_index;

			if (fields.portal != SG_RUNE_COMPACT_INDEX_NONE ||
				fields.fanout_ordinal == UINT32_MAX ||
				fields.mover_model != SG_BSP_ENTITY_MODEL_NONE ||
				!physics_zero ||
				!AuthorityEntityIndex(entities,
					(sg_rune_compact_mechanism_entity_ref_t){
						fields.destination },
					&destination_index) ||
				!EntityIsTeleporterDestination(
					&entities->entities[destination_index]))
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
				return 0;
			}
		}
		else if (fields.kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH)
		{
			if (fields.portal != SG_RUNE_COMPACT_INDEX_NONE ||
				fields.destination != SG_RUNE_COMPACT_INDEX_NONE ||
				fields.fanout_ordinal != UINT32_MAX ||
				fields.mover_model != SG_BSP_ENTITY_MODEL_NONE ||
				fields.flight_ms == 0U ||
				!Binary32Nonnegative(fields.gravity_bits))
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
				return 0;
			}
		}
		else if (fields.kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
		{
			int endpoint_shape_valid = 0;

			if (authority->kind !=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
				endpoint_shape_valid = fields.fanout_ordinal == UINT32_MAX &&
					fields.source_endpoint == SG_RUNE_COMPACT_INDEX_NONE &&
					fields.destination_endpoint == SG_RUNE_COMPACT_INDEX_NONE;
			else if (authority->kind ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
				endpoint_shape_valid = fields.fanout_ordinal != UINT32_MAX &&
					EntityRefIsValid(fields.source_endpoint,
						entities->entity_count) &&
					EntityRefIsValid(fields.destination_endpoint,
						entities->entity_count) &&
					fields.source_endpoint != fields.destination_endpoint;
			if (fields.portal != SG_RUNE_COMPACT_INDEX_NONE ||
				fields.destination != SG_RUNE_COMPACT_INDEX_NONE ||
				fields.mover_model == SG_BSP_ENTITY_MODEL_NONE ||
				fields.mover_model >=
					input->geometry.identity.source_counts.model_count ||
				fields.source_surface_ordinal >=
					input->geometry.source_surface_count ||
				!AuthorityStateValid(fields.source_state) ||
				!AuthorityStateValid(fields.destination_state) ||
				!endpoint_shape_valid || !AuthorityTransportSourceValid(input,
															authority, &fields) ||
				!AuthorityTransportWorldBitsValid(&fields) ||
				fields.swept_static_clear != 1U ||
				fields.start_supported != 1U || fields.end_supported != 1U ||
				fields.stance >= SG_RUNE_STANCE_COUNT)
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
				return 0;
			}
		}
		else
			return 0;
		if (fields.kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH &&
			(!Binary32FiniteAllowSignedZero(fields.launch_velocity_bits[0]) ||
			 !Binary32FiniteAllowSignedZero(fields.launch_velocity_bits[1]) ||
			 !Binary32FiniteAllowSignedZero(fields.launch_velocity_bits[2]) ||
			 !Binary32Nonnegative(fields.gravity_bits)))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
	}
	for (index = 0U; index < entities->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity = &entities->entities[index];
		uint32_t authority_index;
		uint32_t found = 0U;

		if (!EntityIsCanonicalMechanism(entity) ||
			EntityIsTeleporterDestination(entity))
			continue;
		for (authority_index = 0U; authority_index < view->mechanism_count;
			authority_index++)
			if (view->mechanisms[authority_index].source.entity_ordinal ==
				entity->canonical_ordinal)
				found++;
		if (found != 1U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
	}
	return 1;
}

static int FindMechanismIndex(const mechanism_spec_t *specs, uint32_t count,
	uint32_t entity_index, uint32_t *index_out)
{
	uint32_t index;

	if (index_out == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (specs[index].entity_index == entity_index)
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static int FindMechanismIndexForPair(const mechanism_spec_t *specs,
	uint32_t count, uint32_t source_entity, uint32_t destination_entity,
	uint32_t fanout_ordinal, uint32_t *index_out)
{
	uint32_t index;

	if (specs == NULL || index_out == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (specs[index].entity_index == source_entity &&
			specs[index].teleporter_pair &&
			specs[index].destination_entity_index == destination_entity &&
			specs[index].fanout_ordinal == fanout_ordinal)
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static int ExistingActivationLandmark(const sg_bsp_entity_semantic_t *entity,
	sg_rune_compact_mechanism_kind_t mechanism_kind)
{
	sg_rune_compact_landmark_kind_t kind;

	if (entity == NULL || (entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) == 0U ||
		!MapLandmarkKind(entity->landmark_kind, mechanism_kind, &kind))
		return 0;
	return ActivationLandmarkMatchesMechanism(kind, mechanism_kind);
}

static int MakeLandmarkSpec(const sg_bsp_entity_semantics_t *entities,
	uint32_t entity_index, sg_rune_compact_mechanism_kind_t mechanism_kind,
	landmark_spec_t *spec)
{
	const sg_bsp_entity_semantic_t *entity;
	sg_rune_compact_landmark_kind_t kind;
	uint16_t variant = 0U;
	uint32_t linked = SG_RUNE_COMPACT_INDEX_NONE;

	if (entities == NULL || spec == NULL || entity_index >= entities->entity_count)
		return 0;
	entity = &entities->entities[entity_index];
	if ((entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) == 0U ||
		!MapLandmarkKind(entity->landmark_kind, mechanism_kind, &kind))
		return 0;
	if (kind == SG_RUNE_COMPACT_LANDMARK_FLAG)
		variant = (entity->flags & SG_BSP_ENTITY_FLAG_BLUE) != 0U ? 1U : 0U;
	if (kind == SG_RUNE_COMPACT_LANDMARK_BUTTON ||
		kind == SG_RUNE_COMPACT_LANDMARK_TRIGGER ||
		kind == SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING ||
		kind == SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY)
	{
		if ((entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND) == 0U ||
			!ActivationLandmarkMatchesMechanism(kind, mechanism_kind))
			return 0;
		linked = entity_index;
	}
	spec->entity_index = entity_index;
	spec->canonical_ordinal = entity->canonical_ordinal;
	spec->kind = kind;
	spec->variant = variant;
	spec->linked_entity_index = linked;
	spec->linked_destination_entity_index = SG_RUNE_COMPACT_INDEX_NONE;
	spec->linked_fanout_ordinal = UINT32_MAX;
	spec->linked_pair = 0;
	spec->use_witness = kind == SG_RUNE_COMPACT_LANDMARK_BUTTON ||
		kind == SG_RUNE_COMPACT_LANDMARK_TRIGGER ||
		kind == SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING ||
		kind == SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
	return 1;
}

static int AddSyntheticActivationLandmark(
	const sg_bsp_entity_semantic_t *entity, uint32_t entity_index,
	landmark_spec_t *spec)
{
	sg_rune_compact_landmark_kind_t kind;

	if (entity == NULL || spec == NULL)
		return 0;
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_BUTTON)
		kind = SG_RUNE_COMPACT_LANDMARK_BUTTON;
	else if (entity->mechanism_kind == SG_RUNE_MECHANISM_TRIGGER)
		kind = SG_RUNE_COMPACT_LANDMARK_TRIGGER;
	else if (entity->mechanism_kind == SG_RUNE_MECHANISM_PUSH)
		kind = SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING;
	else
		kind = SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
	spec->entity_index = entity_index;
	spec->canonical_ordinal = entity->canonical_ordinal;
	spec->kind = kind;
	spec->variant = 0U;
	spec->linked_entity_index = entity_index;
	spec->linked_destination_entity_index = SG_RUNE_COMPACT_INDEX_NONE;
	spec->linked_fanout_ordinal = UINT32_MAX;
	spec->linked_pair = 0;
	spec->use_witness = 1;
	return 1;
}

static int AddTeleporterDestinationLandmark(
	const sg_bsp_entity_semantic_t *entity, uint32_t entity_index,
	landmark_spec_t *spec)
{
	if (entity == NULL || spec == NULL)
		return 0;
	spec->entity_index = entity_index;
	spec->canonical_ordinal = entity->canonical_ordinal;
	spec->kind = SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION;
	spec->variant = 0U;
	spec->linked_entity_index = SG_RUNE_COMPACT_INDEX_NONE;
	spec->linked_destination_entity_index = entity_index;
	spec->linked_fanout_ordinal = UINT32_MAX;
	spec->linked_pair = 1;
	spec->use_witness = 1;
	return 1;
}

static int PortalMechanismKindForMechanism(
	const materializer_input_view_t *input, const mechanism_spec_t *mechanism,
	sg_rune_compact_portal_mechanism_kind_t *kind_out)
{
	const sg_rune_compact_mechanism_authority_t *authority;

	if (input == NULL || input->mechanisms == NULL || mechanism == NULL ||
		kind_out == NULL || mechanism->authority_index >=
			input->mechanisms->mechanism_count)
		return 0;
	authority = &input->mechanisms->mechanisms[mechanism->authority_index];
	switch (mechanism->value.kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_DOOR:
		*kind_out = SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_LIFT:
	case SG_RUNE_COMPACT_MECHANISM_TRAIN:
		*kind_out = SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_ROTATOR:
		if (!FiniteAngularDoorAuthority(input, authority))
			return 0;
		*kind_out = SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_BUTTON:
		/* A button is normally only an activation landmark.  A brush button
		 * which the authenticated authority joined to a portal-state transition
		 * is also the physical blocker for that portal.  Preserve that fact in
		 * the same BLOCKS binding used by a door; do not emit a binding for a
		 * button with no resolved mover transition. */
		if (mechanism->transition_portal == SG_RUNE_COMPACT_INDEX_NONE ||
			authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
			authority->transitions.count == 0U)
			return 0;
		*kind_out = SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TELEPORT:
	case SG_RUNE_COMPACT_MECHANISM_PUSH:
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRIGGER:
	case SG_RUNE_COMPACT_MECHANISM_KIND_COUNT:
		break;
	}
	return 0;
}

static int FacetIncidentCell(const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t facet_index, uint32_t incidence_offset, uint32_t *cell_out)
{
	const sg_rune_compact_facet_t *facet;
	uint32_t incidence;

	if (geometry == NULL || cell_out == NULL || facet_index >= geometry->facet_count)
		return 0;
	facet = &geometry->facets[facet_index];
	if (incidence_offset >= facet->incidences.count)
		return 0;
	incidence = facet->incidences.first + incidence_offset;
	if (incidence >= geometry->incidence_count)
		return 0;
	*cell_out = geometry->incidences[incidence].cell.value;
	return *cell_out < geometry->cell_count;
}

static sg_rune_stance_validity_t FacetHookableStances(
	const sg_rune_compact_static_geometry_view_t *geometry, uint32_t facet_index)
{
	const sg_rune_compact_facet_t *facet;
	sg_rune_stance_validity_t stances = SG_RUNE_STANCE_VALID_ALL;
	uint32_t offset;

	if (geometry == NULL || facet_index >= geometry->facet_count)
		return 0U;
	facet = &geometry->facets[facet_index];
	if (facet->incidences.count == 0U)
		return stances;
	for (offset = 0U; offset < facet->incidences.count; offset++)
	{
		uint32_t cell;

		if (!FacetIncidentCell(geometry, facet_index, offset, &cell))
			return 0U;
		stances = (sg_rune_stance_validity_t)(stances &
			geometry->cells[cell].valid_stances);
	}
	return stances;
}

static int IsHazardousCell(const sg_rune_compact_cell_t *cell)
{
	return cell != NULL &&
		((cell->semantics & SG_RUNE_COMPACT_CELL_HAZARD) != 0U ||
		(cell->contents & (SG_RUNE_COMPACT_CONTENTS_LAVA |
			SG_RUNE_COMPACT_CONTENTS_SLIME)) != 0U);
}

static void MaterializerFreeArrays(
	sg_rune_compact_static_materializer_t *materializer)
{
	if (materializer == NULL)
		return;
	free(materializer->portal_mechanisms);
	free(materializer->facet_annotations);
	free(materializer->transitions);
	free(materializer->authority_transition_static);
	free(materializer->static_mechanism_authority);
	free(materializer->landmark_cells);
	free(materializer->landmarks);
	free(materializer->mechanism_edges);
	free(materializer->mechanism_controllers);
	free(materializer->mechanisms);
	materializer->portal_mechanisms = NULL;
	materializer->facet_annotations = NULL;
	materializer->transitions = NULL;
	materializer->authority_transition_static = NULL;
	materializer->authority_transition_count = 0U;
	materializer->static_mechanism_authority = NULL;
	materializer->static_mechanism_count = 0U;
	materializer->landmark_cells = NULL;
	materializer->landmarks = NULL;
	materializer->mechanism_edges = NULL;
	materializer->mechanism_controllers = NULL;
	materializer->mechanisms = NULL;
}

static void DestroyCandidate(sg_rune_compact_static_materializer_t *candidate)
{
	if (candidate == NULL)
		return;
	MaterializerFreeArrays(candidate);
	free(candidate);
}

static int ResolveController(
	const materializer_input_view_t *input,
	uint32_t authority_index, uint32_t mechanism_entity,
	uint32_t *controller_out)
{
	const sg_rune_compact_mechanisms_view_t *view;
	const sg_rune_compact_mechanism_authority_t *authority;
	uint32_t controller = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t local;

	if (input == NULL || input->mechanisms == NULL ||
		input->entities == NULL || controller_out == NULL || authority_index >=
		input->mechanisms->mechanism_count || mechanism_entity >=
		input->entities->entity_count)
		return 0;
	view = input->mechanisms;
	authority = &view->mechanisms[authority_index];
	if (authority->source.entity_ordinal != mechanism_entity)
		return 0;
	if (authority->controllers.count == 0U)
	{
		/* An empty controller span means the mechanism is self-activated.  The
		 * source is the only safe primary-controller fallback; target edges are
		 * never searched here, so an absent controller fact cannot invent one. */
		controller = mechanism_entity;
	}
	else
	{
		/* The primary field is only a compatibility convenience.  Select the
		 * canonical minimum while the complete controller span is materialized
		 * separately; no controller fact is discarded. */
		for (local = 0U; local < authority->controllers.count; local++)
		{
			uint32_t record_index;
			uint32_t candidate;
			const sg_rune_compact_mechanism_controller_t *record;

			if (!CountAdd(authority->controllers.first, local, &record_index))
				return 0;
			record = &view->controllers[record_index];
			if (record->mechanism != authority_index ||
				!AuthorityEntityIndex(input->entities, record->controller,
					&candidate))
				return 0;
			if (controller == SG_RUNE_COMPACT_INDEX_NONE || candidate < controller)
				controller = candidate;
		}
	}
	if (controller == SG_RUNE_COMPACT_INDEX_NONE || controller >=
		input->entities->entity_count ||
		!EntityIsCanonicalMechanism(&input->entities->entities[controller]) ||
		EntityIsTeleporterDestination(&input->entities->entities[controller]))
		return 0;
	*controller_out = controller;
	return 1;
}

static int FindPushLanding(
	const materializer_input_view_t *input,
	uint32_t authority_index, uint32_t transition_index,
	sg_rune_q8_vec3_t *landing_origin, uint32_t *landing_cell)
{
	const sg_rune_compact_mechanisms_view_t *view;
	const sg_rune_compact_mechanism_authority_t *authority;
	const sg_rune_compact_mechanism_transition_t *transition;
	authority_transition_fields_t fields;

	if (input == NULL || input->mechanisms == NULL || landing_origin == NULL ||
		landing_cell == NULL || authority_index >= input->mechanisms->mechanism_count ||
		transition_index >= input->mechanisms->transition_count)
		return 0;
	view = input->mechanisms;
	authority = &view->mechanisms[authority_index];
	transition = &view->transitions[transition_index];
	if (!DecodeAuthorityTransition(transition, &fields) ||
		transition->mechanism != authority_index ||
		fields.kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH ||
		fields.portal != SG_RUNE_COMPACT_INDEX_NONE ||
		fields.destination != SG_RUNE_COMPACT_INDEX_NONE ||
		fields.entry_cell >= input->geometry.cell_count ||
		fields.exit_cell >= input->geometry.cell_count ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH)
		return 0;
	/* A push transition's exit witness is the only accepted landing witness.
	 * The launch trigger AABB is never reused as a landing location. */
	if (!CellContainsPointQ8(&input->geometry, fields.exit_cell,
		&fields.exit_witness))
		return 0;
	*landing_origin = fields.exit_witness;
	*landing_cell = fields.exit_cell;
	return 1;
}


static int AuthorityTransitionAt(
	const sg_rune_compact_mechanisms_view_t *view, uint32_t authority_index,
	uint32_t local_index, uint32_t *transition_index_out)
{
	const sg_rune_compact_mechanism_authority_t *authority;
	uint32_t transition_index;

	if (view == NULL || transition_index_out == NULL || authority_index >=
		view->mechanism_count || view->mechanisms == NULL)
		return 0;
	authority = &view->mechanisms[authority_index];
	if (local_index >= authority->transitions.count ||
		authority->transitions.first > view->transition_count ||
		local_index > view->transition_count - authority->transitions.first)
		return 0;
	transition_index = authority->transitions.first + local_index;
	if (transition_index >= view->transition_count)
		return 0;
	*transition_index_out = transition_index;
	return 1;
}

static int BuildMechanisms(
	const materializer_input_view_t *input,
	sg_rune_compact_static_materializer_t *candidate,
	mechanism_spec_t **specs_out, uint32_t *count_out,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_rune_compact_mechanisms_view_t *view = input->mechanisms;
	const sg_bsp_entity_semantics_t *entities = input->entities;
	uint32_t count = 0U;
	uint32_t authority_index;
	mechanism_spec_t *specs;
	size_t bytes;

	for (authority_index = 0U; authority_index < view->mechanism_count;
		authority_index++)
	{
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[authority_index];
		/* A teleporter fanout owns one static mechanism record per resolved
		 * destination.  Other authorities own one record whose transition span
		 * retains every authenticated fact (for example, a door with several
		 * portal states or a mover with several transport surfaces). */
		uint32_t output_count = authority->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ?
			authority->transitions.count : 1U;

		if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT &&
			output_count == 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				authority_index);
			return 0;
		}
		if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH &&
			authority->transitions.count == 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				authority_index);
			return 0;
		}
		if (!CountAdd(count, output_count, &count))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				authority_index);
			return 0;
		}
	}
	if (count > SG_RUNE_COMPACT_MAX_MECHANISMS ||
		!SizeMultiply(count, sizeof(*specs), &bytes))
	{
		SetError(error,
			count > SG_RUNE_COMPACT_MAX_MECHANISMS ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	specs = count == 0U ? NULL : MaterializerCalloc(1U, bytes);
	if (count != 0U && specs == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	count = 0U;
	for (authority_index = 0U; authority_index < view->mechanism_count;
		authority_index++)
	{
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[authority_index];
		const uint32_t entity_index = authority->source.entity_ordinal;
		const sg_bsp_entity_semantic_t *entity = &entities->entities[entity_index];
		sg_rune_q8_vec3_t entity_origin;
		sg_rune_q8_bounds_t entity_bounds;
		int entity_has_bounds;
		uint32_t entity_cell;
		uint32_t controller;
		sg_rune_compact_mechanism_kind_t static_kind;
		sg_rune_compact_static_activation_mask_t activation_mask;
		sg_rune_compact_mechanism_state_t initial_state;
		sg_rune_compact_mechanism_state_t activated_state;
		sg_rune_compact_mechanism_state_t reset_state;
		uint32_t local_count = authority->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ?
			authority->transitions.count : 1U;
		uint32_t local_index;

		if (!FindEntityWitness(&input->geometry, entity, &entity_origin,
			&entity_bounds, &entity_has_bounds, &entity_cell) ||
			!ResolveController(input, authority_index, entity_index, &controller) ||
			!MapAuthorityMechanismKind(authority->kind, &static_kind) ||
			!MapActivationMask(authority->activation, &activation_mask))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				entity_index);
			return 0;
		}
		if (local_count == 0U)
			local_count = 1U;
		for (local_index = 0U; local_index < local_count; local_index++)
		{
			const sg_rune_compact_mechanism_transition_t *transition = NULL;
			uint32_t transition_index = SG_RUNE_COMPACT_INDEX_NONE;
			uint32_t first_cell = authority->activation_cell.value;
			uint32_t last_cell = first_cell;
			uint32_t transition_portal = SG_RUNE_COMPACT_INDEX_NONE;
			uint32_t transition_model = SG_BSP_ENTITY_MODEL_NONE;
			authority_transition_fields_t fields;
			mechanism_spec_t *spec;
			int mover_model_valid;

			if (authority->transitions.count != 0U)
			{
				if (!AuthorityTransitionAt(view, authority_index, local_index,
					&transition_index))
				{
					free(specs);
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
						authority_index);
					return 0;
				}
				transition = &view->transitions[transition_index];
				if (!DecodeAuthorityTransition(transition, &fields))
				{
					free(specs);
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
						transition_index);
					return 0;
				}
				first_cell = fields.entry_cell;
				last_cell = fields.exit_cell;
				if (fields.kind ==
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
				{
					if (!FindAuthorityMoverTransition(input, transition, &first_cell,
						&last_cell, &transition_portal, &transition_model))
					{
						free(specs);
						SetError(error,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
							transition_index);
						return 0;
					}
					mover_model_valid = AuthorityTeamMoverModelValid(input,
						authority, transition_model);
					if (mover_model_valid <= 0)
					{
						free(specs);
						SetError(error,
							mover_model_valid < 0 ?
								SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY :
								SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
							transition_index);
						return 0;
					}
				}
				else if (fields.kind ==
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
				{
					if (authority->kind !=
						SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ||
						!EntityIsTeleporterDestination(&entities->entities[
							fields.destination]))
					{
						free(specs);
						SetError(error,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
							entity_index);
						return 0;
					}
				}
				else if (fields.kind ==
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
				{
					transition_model = fields.mover_model;
					if (!AuthorityTransportSourceValid(input, authority, &fields))
					{
						free(specs);
						SetError(error,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
							transition_index);
						return 0;
					}
				}
			}
			else if (authority->kind ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT)
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
					authority_index);
				return 0;
			}
			spec = &specs[count++];
			memset(spec, 0, sizeof(*spec));
			spec->entity_index = entity_index;
		 spec->canonical_ordinal = entity->canonical_ordinal;
		 spec->controller_ordinal =
			entities->entities[controller].canonical_ordinal;
		 spec->authority_index = authority_index;
		 spec->authority_transition_index = transition_index;
		 spec->authority_transition_count = authority->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ? 1U :
			authority->transitions.count;
			spec->value.source.entity_ordinal = entity->canonical_ordinal;
			spec->value.entry_cell.value = first_cell;
			spec->value.exit_cell.value = last_cell;
			spec->value.activation_landmark.value = SG_RUNE_COMPACT_INDEX_NONE;
			spec->value.controllers.first = 0U;
			spec->value.controllers.count = 0U;
			spec->value.transitions.first = 0U;
			spec->value.transitions.count = 0U;
			spec->transition_portal = transition_portal;
			spec->destination_entity_index = SG_RUNE_COMPACT_INDEX_NONE;
			spec->fanout_ordinal = UINT32_MAX;
			spec->activation_cell = authority->activation_cell.value;
			spec->value.transition_destination.entity_ordinal =
				SG_RUNE_COMPACT_INDEX_NONE;
			spec->value.transition_fanout_ordinal = UINT32_MAX;
		if (entity_has_bounds)
			spec->value.bounds = entity_bounds;
		else if (!PointBounds(&entity_origin, &spec->value.bounds))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, entity_index);
			return 0;
		}
		spec->value.kind = static_kind;
		if (static_kind == SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
			FiniteAngularDoorAuthority(input, authority))
			spec->value.flags |=
				SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR;
		spec->value.activation_mask = activation_mask;
		spec->value.damage = authority->damage;
		spec->value.health = authority->health;
		spec->value.required_item = authority->required_item;
		if (!MapAuthorityState(authority->initial_state, &initial_state) ||
			!MapAuthorityState(authority->activated_state, &activated_state) ||
			!MapAuthorityState(authority->reset_state, &reset_state))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, entity_index);
			return 0;
		}
		spec->value.initial_state = initial_state;
		spec->value.activated_state = activated_state;
		spec->value.reset_state = reset_state;
		spec->value.recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
		/* Disabled reset is the authority's canonical one-shot outcome.  This
		 * avoids reconstructing trigger_once semantics from entity text. */
		if (reset_state == SG_RUNE_COMPACT_MECHANISM_STATE_DISABLED)
			spec->value.flags |= SG_RUNE_COMPACT_MECHANISM_ONE_SHOT;
		if ((authority->flags &
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) != 0U)
			spec->value.flags |= SG_RUNE_COMPACT_MECHANISM_ONE_SHOT;
		if ((authority->flags &
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE) != 0U)
			spec->value.flags |= SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE;
		spec->value.delay_ms = authority->delay_ms;
		spec->value.dwell_ms = authority->dwell_ms;
		spec->value.travel_ms = authority->travel_ms;
		spec->value.wait_ms = authority->pause_ms;
		if (authority->recovery_ms != 0U)
		{
			spec->value.reset_ms = authority->recovery_ms;
			spec->value.recovery =
				SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET;
		}
		if ((activation_mask & (sg_rune_compact_static_activation_mask_t)
			~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U &&
			(!BoundsValidQ8(&authority->activation_bounds) ||
				!PointInBoundsQ8(&authority->activation_witness,
					&authority->activation_bounds) ||
				!CellContainsPointQ8(&input->geometry,
					authority->activation_cell.value,
					&authority->activation_witness)))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, entity_index);
			return 0;
		}
		if ((activation_mask & (sg_rune_compact_static_activation_mask_t)
			~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U)
		{
			spec->has_activation_witness = 1;
			spec->activation_origin = authority->activation_witness;
			spec->activation_bounds = authority->activation_bounds;
		}
		if (transition != NULL && (fields.kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
			fields.kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT))
		{
			spec->bsp_model = transition_model;
			spec->value.flags |= SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE;
		}
		else
			spec->bsp_model = entity->bsp_model;
		if (transition != NULL && fields.kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
		{
			spec->destination_entity_index = fields.destination;
			spec->fanout_ordinal = fields.fanout_ordinal;
			spec->teleporter_pair = 1;
		}
		/* Each teleport fanout is materialized as its own one-transition
		 * mechanism spec.  The authority span may contain several fanouts, but
		 * this spec still has one authoritative transition.  Use the spec span
		 * rather than the authority span when copying the convenience fields. */
		if (transition != NULL && spec->authority_transition_count == 1U)
		{
			uint32_t axis;

			spec->value.transition_destination.entity_ordinal =
				fields.destination;
			spec->value.transition_fanout_ordinal = fields.fanout_ordinal;
			spec->value.gravity_bits = fields.gravity_bits;
			spec->value.flight_ms = fields.flight_ms;
			for (axis = 0U; axis < 3U; axis++)
				spec->value.launch_velocity_bits[axis] =
					fields.launch_velocity_bits[axis];
		}
		if (transition != NULL && fields.kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH)
		{
			if (!FindPushLanding(input, authority_index, transition_index,
				&spec->landing_origin, &spec->landing_cell) ||
				!PointBounds(&spec->landing_origin, &spec->landing_bounds))
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
					entity_index);
				return 0;
			}
			spec->has_landing = 1;
		}
		}
	}
	if (count > 1U)
		qsort(specs, count, sizeof(*specs), CompareMechanismSpec);
	for (authority_index = 1U; authority_index < count; authority_index++)
		if (MechanismSpecEqual(&specs[authority_index - 1U],
			&specs[authority_index]))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				authority_index);
			return 0;
		}
	if (count != 0U)
	{
		size_t output_bytes;

		if (!SizeMultiply(count, sizeof(*candidate->mechanisms), &output_bytes))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
			return 0;
		}
		candidate->mechanisms = MaterializerCalloc(1U, output_bytes);
		if (candidate->mechanisms == NULL)
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
			return 0;
		}
		candidate->static_mechanism_authority = MaterializerCalloc((size_t)count,
			sizeof(*candidate->static_mechanism_authority));
		if (candidate->static_mechanism_authority == NULL)
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
			return 0;
		}
		for (authority_index = 0U; authority_index < count; authority_index++)
		{
			if (specs[authority_index].authority_index >=
				input->mechanisms->mechanism_count)
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
					authority_index);
				return 0;
			}
			candidate->mechanisms[authority_index] = specs[authority_index].value;
			candidate->static_mechanism_authority[authority_index] =
				specs[authority_index].authority_index;
		}
		candidate->static_mechanism_count = count;
	}
	candidate->view.mechanisms = candidate->mechanisms;
	candidate->view.mechanism_count = count;
	*specs_out = specs;
	*count_out = count;
	return 1;
}

static int CompareControllerSpec(const void *left_pointer,
	const void *right_pointer)
{
	const controller_spec_t *left = left_pointer;
	const controller_spec_t *right = right_pointer;
	int comparison = left->mechanism_index < right->mechanism_index ? -1 :
		left->mechanism_index > right->mechanism_index ? 1 : 0;

	if (comparison == 0)
		comparison = CompareControllerValue(&left->value, &right->value);
	return comparison;
}

static int BuildMechanismControllers(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	sg_rune_compact_static_materializer_t *candidate,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_rune_compact_mechanisms_view_t *view = input->mechanisms;
	controller_spec_t *specs = NULL;
	uint32_t total = 0U;
	uint32_t mechanism_index;
	uint32_t cursor = 0U;
	size_t bytes;

	if (mechanisms == NULL && mechanism_count != 0U)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	for (mechanism_index = 0U; mechanism_index < mechanism_count;
		mechanism_index++)
	{
		const uint32_t authority_index = mechanisms[mechanism_index].authority_index;
		const sg_rune_compact_mechanism_authority_t *authority;

		if (authority_index >= view->mechanism_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				mechanism_index);
			return 0;
		}
		authority = &view->mechanisms[authority_index];
		if (!CountAdd(total, authority->controllers.count, &total))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
				mechanism_index);
			return 0;
		}
	}
	if (total > SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS ||
		!SizeMultiply((size_t)total, sizeof(*specs), &bytes))
	{
		SetError(error,
			total > SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	if (total != 0U)
	{
		specs = MaterializerCalloc(1U, bytes);
		if (specs == NULL)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
			return 0;
		}
	}
	for (mechanism_index = 0U; mechanism_index < mechanism_count;
		mechanism_index++)
	{
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[mechanisms[mechanism_index].authority_index];
		uint32_t local;

		candidate->mechanisms[mechanism_index].controllers.first = cursor;
		candidate->mechanisms[mechanism_index].controllers.count =
			authority->controllers.count;
		for (local = 0U; local < authority->controllers.count; local++)
		{
			uint32_t authority_controller_index;
			uint32_t controller_entity_index;
			const sg_rune_compact_mechanism_controller_t *authority_controller;

			if (!CountAdd(authority->controllers.first, local,
				&authority_controller_index) || authority_controller_index >=
				view->controller_count)
				goto malformed_controller;
			authority_controller = &view->controllers[authority_controller_index];
			if (!AuthorityEntityIndex(input->entities, authority_controller->controller,
				&controller_entity_index))
				goto malformed_controller;
			(void)controller_entity_index;
			if (specs == NULL || cursor >= total)
				goto malformed_controller;
			specs[cursor].mechanism_index = mechanism_index;
			specs[cursor].value.controller.entity_ordinal =
				authority_controller->controller.entity_ordinal;
			specs[cursor].value.topology_edge =
				authority_controller->topology_edge;
			specs[cursor].value.spatiality =
				authority_controller->spatiality;
			specs[cursor].value.activation_cell =
				authority_controller->activation_cell;
			specs[cursor].value.activation_witness =
				authority_controller->activation_witness;
			specs[cursor].value.activation_bounds =
				authority_controller->activation_bounds;
			cursor++;
		}
	}
	if (total > 1U)
		qsort(specs, total, sizeof(*specs), CompareControllerSpec);
	/* Rebuild spans after sorting.  Every output mechanism owns a contiguous
	 * canonical slice, including each independent teleporter fanout record. */
	cursor = 0U;
	for (mechanism_index = 0U; mechanism_index < mechanism_count;
		mechanism_index++)
	{
		uint32_t first = cursor;

		while (cursor < total && specs[cursor].mechanism_index == mechanism_index)
			cursor++;
		candidate->mechanisms[mechanism_index].controllers.first = first;
		candidate->mechanisms[mechanism_index].controllers.count = cursor - first;
	}
	if (cursor != total)
		goto malformed_controller;
	if (total != 0U)
	{
		size_t output_bytes;

		if (!SizeMultiply((size_t)total,
			sizeof(*candidate->mechanism_controllers), &output_bytes))
			goto overflow_controller;
		candidate->mechanism_controllers = MaterializerCalloc(1U, output_bytes);
		if (candidate->mechanism_controllers == NULL)
			goto oom_controller;
		for (cursor = 0U; cursor < total; cursor++)
			candidate->mechanism_controllers[cursor] = specs[cursor].value;
	}
	free(specs);
	candidate->view.mechanism_controllers = candidate->mechanism_controllers;
	candidate->view.mechanism_controller_count = total;
	return 1;

malformed_controller:
	free(specs);
	SetError(error,
		SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
		SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, mechanism_index);
	return 0;
overflow_controller:
	free(specs);
	SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
		SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, mechanism_index);
	return 0;
oom_controller:
	free(specs);
	SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
		SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, mechanism_index);
	return 0;
}

typedef struct projected_transition_provenance_s
{
	sg_rune_compact_static_transition_t transition;
	uint32_t authority_transition;
} projected_transition_provenance_t;

static int CompareProjectedTransitionWithProvenance(const void *left_pointer,
	const void *right_pointer)
{
	const projected_transition_provenance_t *left = left_pointer;
	const projected_transition_provenance_t *right = right_pointer;
	const int comparison = SG_RuneCompactStaticTransitionCompareCanonical(
		&left->transition, &right->transition);

	if (comparison != 0)
		return comparison;
	return left->authority_transition < right->authority_transition ? -1 :
		left->authority_transition > right->authority_transition ? 1 : 0;
}

/* qsort cannot carry a parallel provenance sidecar.  Sort paired records and
 * copy the checked canonical slice back together, so the construction stays
 * O(n log n) even at the protocol transition bound. */
static int SortProjectedTransitionsWithProvenance(
	sg_rune_compact_static_transition_t *transitions, uint32_t *provenance,
	uint32_t first, uint32_t count)
{
	projected_transition_provenance_t *records;
	size_t bytes;
	uint32_t offset;

	if (count < 2U)
		return 1;
	if (!SizeMultiply((size_t)count, sizeof(*records), &bytes))
		return 0;
	records = MaterializerCalloc(1U, bytes);
	if (records == NULL)
		return 0;
	for (offset = 0U; offset < count; offset++)
	{
		records[offset].transition = transitions[first + offset];
		records[offset].authority_transition = provenance[first + offset];
	}
	qsort(records, (size_t)count, sizeof(*records),
		CompareProjectedTransitionWithProvenance);
	for (offset = 0U; offset < count; offset++)
	{
		transitions[first + offset] = records[offset].transition;
		provenance[first + offset] = records[offset].authority_transition;
	}
	free(records);
	return 1;
}

static int BuildTransitions(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	sg_rune_compact_static_materializer_t *candidate,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_rune_compact_mechanisms_view_t *view = input->mechanisms;
	uint32_t total = 0U;
	uint32_t index;
	uint32_t cursor = 0U;
	uint32_t *provenance = NULL;
	size_t bytes;

	for (index = 0U; index < mechanism_count; index++)
		if (mechanisms[index].authority_transition_count != 0U &&
			(mechanisms[index].authority_transition_index ==
				SG_RUNE_COMPACT_INDEX_NONE ||
			 !CountAdd(total, mechanisms[index].authority_transition_count, &total)))
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
	if (total != view->transition_count)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	if (total > SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS ||
		!SizeMultiply((size_t)total,
			sizeof(*candidate->transitions), &bytes))
	{
		SetError(error,
			total > SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	if (total != 0U)
	{
		provenance = MaterializerCalloc((size_t)total, sizeof(*provenance));
		candidate->authority_transition_static = MaterializerCalloc(
			(size_t)view->transition_count,
			sizeof(*candidate->authority_transition_static));
		if (provenance == NULL || candidate->authority_transition_static == NULL)
		{
			free(provenance);
			free(candidate->authority_transition_static);
			candidate->authority_transition_static = NULL;
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
			return 0;
		}
		for (index = 0U; index < view->transition_count; index++)
			candidate->authority_transition_static[index] =
				SG_RUNE_COMPACT_INDEX_NONE;
		candidate->authority_transition_count = view->transition_count;
		candidate->transitions = MaterializerCalloc(1U, bytes);
		if (candidate->transitions == NULL)
		{
			free(provenance);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
			return 0;
		}
	}
	for (index = 0U; index < mechanism_count; index++)
	{
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[mechanisms[index].authority_index];
		const uint32_t authority_transition_index =
			mechanisms[index].authority_transition_index;
		const uint32_t transition_count =
			mechanisms[index].authority_transition_count;
		uint32_t local;

		candidate->mechanisms[index].transitions.first = cursor;
		candidate->mechanisms[index].transitions.count = 0U;
		for (local = 0U; local < transition_count; local++)
		{
			uint32_t source_index;
				authority_transition_fields_t fields;
			sg_rune_compact_static_transition_t *destination;
			sg_rune_compact_static_transition_kind_t static_kind;
			uint32_t axis;

			if (!CountAdd(authority_transition_index, local, &source_index) ||
				source_index >= view->transition_count ||
				candidate->transitions == NULL || cursor >= total ||
				!DecodeAuthorityTransition(
					&view->transitions[source_index], &fields) ||
				view->transitions[source_index].mechanism !=
					mechanisms[index].authority_index ||
				!AuthorityTransitionKindAllowed(input,
					&view->mechanisms[mechanisms[index].authority_index],
					fields.kind) ||
				!MapAuthorityTransitionKind(fields.kind, &static_kind))
			{
				free(provenance);
				free(candidate->transitions);
				candidate->transitions = NULL;
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
				return 0;
			}
			destination = &candidate->transitions[cursor];
			memset(destination, 0, sizeof(*destination));
			destination->mechanism.value = index;
			destination->kind = static_kind;
			destination->entry_cell.value = fields.entry_cell;
			destination->exit_cell.value = fields.exit_cell;
			destination->source_state = (sg_rune_compact_mechanism_state_t)
				fields.source_state;
			destination->destination_state =
				(sg_rune_compact_mechanism_state_t)fields.destination_state;
			destination->elapsed_ms = fields.elapsed_ms;
			switch (fields.kind)
			{
			case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
				destination->value.portal_state.portal.value = fields.portal;
				destination->value.portal_state.mover_model = fields.mover_model;
				/* Timing is an authority-level schedule.  The transition result
				 * proves the host's elapsed observation, while the mechanism owner
				 * is the sole authority for delay/dwell/pause/travel/recovery.
				 * This also prevents a stale or partially populated transition
				 * payload from erasing dwell semantics during projection. */
				destination->value.portal_state.delay_ms = authority->delay_ms;
				destination->value.portal_state.dwell_ms = authority->dwell_ms;
					destination->value.portal_state.pause_ms = authority->pause_ms;
					destination->value.portal_state.travel_ms = authority->travel_ms;
					destination->value.portal_state.recovery_ms = authority->recovery_ms;
					destination->value.portal_state.source_blocked =
						fields.source_blocked;
					destination->value.portal_state.destination_blocked =
						fields.destination_blocked;
					break;
			case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
				destination->value.teleport.destination.entity_ordinal =
					fields.destination;
				destination->value.teleport.fanout_ordinal = fields.fanout_ordinal;
				destination->value.teleport.approach_witness = fields.approach_witness;
				destination->value.teleport.entry_witness = fields.entry_witness;
				destination->value.teleport.exit_witness = fields.exit_witness;
				for (axis = 0U; axis < 3U; axis++)
					destination->value.teleport.arrival_velocity_bits[axis] =
						fields.arrival_velocity_bits[axis];
				break;
			case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
				destination->value.push.approach_witness = fields.approach_witness;
				destination->value.push.entry_witness = fields.entry_witness;
				destination->value.push.exit_witness = fields.exit_witness;
				for (axis = 0U; axis < 3U; axis++)
					destination->value.push.launch_velocity_bits[axis] =
						fields.launch_velocity_bits[axis];
				destination->value.push.gravity_bits = fields.gravity_bits;
				destination->value.push.flight_ms = fields.flight_ms;
				break;
			case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
				destination->value.transport.mover_model = fields.mover_model;
				destination->value.transport.source_surface_ordinal =
					fields.source_surface_ordinal;
				destination->value.transport.source_player_local =
					fields.source_player_local;
				destination->value.transport.destination_player_local =
					fields.destination_player_local;
				destination->value.transport.source_support_local =
					fields.source_support_local;
				destination->value.transport.destination_support_local =
					fields.destination_support_local;
				for (axis = 0U; axis < 3U; axis++)
				{
					destination->value.transport.source_player_world_bits[axis] =
						fields.source_player_world_bits[axis];
					destination->value.transport.destination_player_world_bits[axis] =
						fields.destination_player_world_bits[axis];
						destination->value.transport.source_support_world_bits[axis] =
							fields.source_support_world_bits[axis];
						destination->value.transport.destination_support_world_bits[axis] =
							fields.destination_support_world_bits[axis];
						destination->value.transport.source_mover_origin_bits[axis] =
							fields.source_mover_origin_bits[axis];
						destination->value.transport.destination_mover_origin_bits[axis] =
							fields.destination_mover_origin_bits[axis];
						{
							uint32_t column;

							for (column = 0U; column < 3U; column++)
							{
								destination->value.transport.source_mover_axis_bits[axis][column] =
									fields.source_mover_axis_bits[axis][column];
								destination->value.transport.destination_mover_axis_bits[axis][column] =
									fields.destination_mover_axis_bits[axis][column];
							}
						}
				}
				destination->value.transport.source_endpoint.entity_ordinal =
					fields.source_endpoint;
				destination->value.transport.destination_endpoint.entity_ordinal =
					fields.destination_endpoint;
				destination->value.transport.fanout_ordinal = fields.fanout_ordinal;
				destination->value.transport.swept_static_clear =
					fields.swept_static_clear;
				destination->value.transport.start_supported =
					fields.start_supported;
				destination->value.transport.end_supported = fields.end_supported;
				destination->value.transport.stance = fields.stance;
				break;
			case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
				free(provenance);
				free(candidate->transitions);
				candidate->transitions = NULL;
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
				return 0;
			}
			candidate->mechanisms[index].transitions.count++;
			provenance[cursor] = source_index;
			cursor++;
		}
		if (candidate->mechanisms[index].transitions.count > 1U)
		{
			uint32_t local_index;
			const uint32_t first = candidate->mechanisms[index].transitions.first;
			const uint32_t count = candidate->mechanisms[index].transitions.count;

			/* Authority transition ordering is intentionally not the wire/static
			 * ordering.  Sort the projected records with the one comparator shared
			 * by the static validator, then reject exact duplicates rather than
			 * allowing qsort's unspecified equal ordering to leak into output. */
			if (!SortProjectedTransitionsWithProvenance(candidate->transitions,
				provenance, first, count))
			{
				free(provenance);
				free(candidate->transitions);
				candidate->transitions = NULL;
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
					index);
				return 0;
			}
			for (local_index = 1U; local_index < count; local_index++)
				if (SG_RuneCompactStaticTransitionCompareCanonical(
					&candidate->transitions[first + local_index - 1U],
					&candidate->transitions[first + local_index]) >= 0)
				{
					free(provenance);
					free(candidate->transitions);
					candidate->transitions = NULL;
					SetError(error,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
						index);
					return 0;
				}
		}
		for (local = 0U; local < candidate->mechanisms[index].transitions.count;
			local++)
		{
			const uint32_t static_index =
				candidate->mechanisms[index].transitions.first + local;
			const uint32_t source_index = provenance[static_index];

			if (source_index >= candidate->authority_transition_count ||
				candidate->authority_transition_static[source_index] !=
					SG_RUNE_COMPACT_INDEX_NONE)
			{
				free(provenance);
				free(candidate->transitions);
				candidate->transitions = NULL;
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
					index);
				return 0;
			}
			candidate->authority_transition_static[source_index] = static_index;
		}
	}
	if (cursor != total)
	{
		free(provenance);
		free(candidate->transitions);
		candidate->transitions = NULL;
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, cursor);
		return 0;
	}
	for (index = 0U; index < candidate->authority_transition_count; index++)
		if (candidate->authority_transition_static[index] ==
			SG_RUNE_COMPACT_INDEX_NONE)
		{
			free(provenance);
			free(candidate->transitions);
			candidate->transitions = NULL;
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
	free(provenance);
	candidate->view.transitions = candidate->transitions;
	candidate->view.transition_count = total;
	return 1;
}

static int BuildLandmarkSpecs(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	landmark_spec_t **specs_out, uint32_t *count_out,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_bsp_entity_semantics_t *entities = input->entities;
	uint32_t entity_index;
	uint32_t count = 0U;
	uint32_t capacity;
	landmark_spec_t *specs;
	size_t bytes;

	if (specs_out == NULL || count_out == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
		return 0;
	}
	if (entities->entity_count > UINT32_MAX / 3U ||
		mechanism_count > UINT32_MAX - entities->entity_count * 3U)
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
		return 0;
	}
	capacity = entities->entity_count * 3U + mechanism_count;
	if (capacity > SG_RUNE_COMPACT_MAX_LANDMARKS ||
		!SizeMultiply(capacity, sizeof(*specs), &bytes))
	{
		SetError(error,
			capacity > SG_RUNE_COMPACT_MAX_LANDMARKS ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
		return 0;
	}
	specs = capacity == 0U ? NULL : MaterializerCalloc(1U, bytes);
	if (capacity != 0U && specs == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
		return 0;
	}
	for (entity_index = 0U; entity_index < entities->entity_count;
		entity_index++)
	{
		const sg_bsp_entity_semantic_t *entity =
			&entities->entities[entity_index];
		uint32_t mechanism_index = SG_RUNE_COMPACT_INDEX_NONE;
		int canonical = (entity->flags & (SG_BSP_ENTITY_HAS_MECHANISM |
			SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND)) ==
			(SG_BSP_ENTITY_HAS_MECHANISM |
				SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND);
		sg_rune_compact_mechanism_kind_t mechanism_kind =
			SG_RUNE_COMPACT_MECHANISM_KIND_COUNT;
		int has_existing = 0;

		if (EntityIsTeleporterDestination(entity))
		{
			uint32_t pair_index;

			for (pair_index = 0U; pair_index < mechanism_count; pair_index++)
				if (mechanisms[pair_index].teleporter_pair &&
					mechanisms[pair_index].destination_entity_index == entity_index)
				{
					if (count >= capacity ||
						!AddTeleporterDestinationLandmark(entity, entity_index,
							&specs[count]))
					{
						free(specs);
						SetError(error,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
							entity_index);
						return 0;
					}
					specs[count].linked_entity_index =
						mechanisms[pair_index].entity_index;
					specs[count].linked_destination_entity_index = entity_index;
					specs[count].linked_fanout_ordinal =
						mechanisms[pair_index].fanout_ordinal;
					specs[count].linked_pair = 1;
					specs[count].variant = (uint16_t)
						(mechanisms[pair_index].fanout_ordinal <= UINT16_MAX ?
							mechanisms[pair_index].fanout_ordinal : 0U);
					count++;
				}
			continue;
		}
		if (canonical && !FindMechanismIndex(mechanisms, mechanism_count,
			entity_index, &mechanism_index))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, entity_index);
			return 0;
		}
		if (canonical)
			mechanism_kind = mechanisms[mechanism_index].value.kind;
		if ((entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) != 0U)
		{
			if (!MakeLandmarkSpec(entities, entity_index, mechanism_kind,
				&specs[count]))
			{
				/* A trigger/button landmark without a canonical mechanism would
				 * have no legal static reference.  Other malformed landmark kinds
				 * are likewise rejected instead of being silently dropped. */
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
					entity_index);
				return 0;
			}
			if (count >= capacity)
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
					entity_index);
				return 0;
			}
			has_existing = canonical && ExistingActivationLandmark(entity,
				mechanism_kind);
			count++;
		}
		if (canonical && EntityIsTeleporterSource(entity))
		{
			uint32_t pair_index;

			/* A teleporter source has one entry landmark per authenticated
			 * source->destination pairing.  The destination marker itself is
			 * never emitted as a traversal mechanism. */
			for (pair_index = 0U; pair_index < mechanism_count; pair_index++)
				if (mechanisms[pair_index].entity_index == entity_index &&
					mechanisms[pair_index].teleporter_pair)
				{
					if (count >= capacity || !AddSyntheticActivationLandmark(entity,
						entity_index, &specs[count]))
					{
						free(specs);
						SetError(error,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
							entity_index);
						return 0;
					}
					specs[count].linked_destination_entity_index =
						mechanisms[pair_index].destination_entity_index;
					specs[count].linked_fanout_ordinal =
						mechanisms[pair_index].fanout_ordinal;
					specs[count].linked_pair = 1;
					specs[count].variant = (uint16_t)
						(mechanisms[pair_index].fanout_ordinal <= UINT16_MAX ?
							mechanisms[pair_index].fanout_ordinal : 0U);
					count++;
				}
		}
		else if (canonical && !has_existing &&
			((mechanisms[mechanism_index].value.activation_mask &
				(sg_rune_compact_static_activation_mask_t)
				~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U ||
			(mechanisms[mechanism_index].value.kind ==
				SG_RUNE_COMPACT_MECHANISM_PUSH &&
				mechanisms[mechanism_index].has_landing)))
		{
			if (count >= capacity || !AddSyntheticActivationLandmark(entity,
				entity_index, &specs[count]))
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
					entity_index);
				return 0;
			}
			count++;
		}
	}
	if (count > 1U)
		qsort(specs, count, sizeof(*specs), CompareLandmarkSpec);
	for (entity_index = 1U; entity_index < count; entity_index++)
		if (LandmarkSpecEqual(&specs[entity_index - 1U], &specs[entity_index]))
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
				entity_index);
			return 0;
		}
	*specs_out = specs;
	*count_out = count;
	return 1;
}

static int FindLandmarkSpecIndex(const landmark_spec_t *specs,
	uint32_t count, uint32_t entity_index,
	sg_rune_compact_landmark_kind_t kind, uint16_t variant,
	uint32_t *index_out)
{
	uint32_t index;

	if (index_out == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (specs[index].entity_index == entity_index &&
			specs[index].kind == kind && specs[index].variant == variant)
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static int FindLandmarkSpecIndexForPair(const landmark_spec_t *specs,
	uint32_t count, uint32_t source_entity,
	uint32_t destination_entity, uint32_t fanout_ordinal,
	uint32_t *index_out)
{
	uint32_t index;

	if (specs == NULL || index_out == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (specs[index].entity_index == source_entity &&
			specs[index].linked_pair &&
			specs[index].kind == SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY &&
			specs[index].linked_destination_entity_index == destination_entity &&
			specs[index].linked_fanout_ordinal == fanout_ordinal)
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static int ActivationLandmarkKind(
	sg_rune_compact_mechanism_kind_t mechanism_kind,
	sg_rune_compact_landmark_kind_t *kind_out)
{
	if (kind_out == NULL)
		return 0;
	switch (mechanism_kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_BUTTON:
		*kind_out = SG_RUNE_COMPACT_LANDMARK_BUTTON;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_TRIGGER:
		*kind_out = SG_RUNE_COMPACT_LANDMARK_TRIGGER;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_PUSH:
		*kind_out = SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_DOOR:
	case SG_RUNE_COMPACT_MECHANISM_LIFT:
	case SG_RUNE_COMPACT_MECHANISM_TRAIN:
	case SG_RUNE_COMPACT_MECHANISM_TELEPORT:
	case SG_RUNE_COMPACT_MECHANISM_ROTATOR:
		*kind_out = SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
		return 1;
	case SG_RUNE_COMPACT_MECHANISM_KIND_COUNT:
		break;
	}
	return 0;
}

static int LandmarkWitness(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	const landmark_spec_t *spec, sg_rune_q8_vec3_t *origin_out,
	sg_rune_q8_bounds_t *bounds_out, int *has_bounds_out,
	uint32_t *mechanism_out)
{
	const sg_bsp_entity_semantic_t *entity;
	uint32_t mechanism_index = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t witness_cell;

	if (input == NULL || spec == NULL ||
		origin_out == NULL || bounds_out == NULL || has_bounds_out == NULL ||
		mechanism_out == NULL || spec->entity_index >=
		input->entities->entity_count || (mechanism_count != 0U &&
		mechanisms == NULL))
		return 0;
	entity = &input->entities->entities[spec->entity_index];
	if (spec->linked_entity_index != SG_RUNE_COMPACT_INDEX_NONE)
	{
		if (spec->linked_pair)
		{
			if (!FindMechanismIndexForPair(mechanisms, mechanism_count,
				spec->linked_entity_index, spec->linked_destination_entity_index,
				spec->linked_fanout_ordinal, &mechanism_index))
				return 0;
		}
		else if (!FindMechanismIndex(mechanisms, mechanism_count,
			spec->linked_entity_index, &mechanism_index))
			return 0;
	}
	if (spec->kind == SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING &&
		mechanism_index != SG_RUNE_COMPACT_INDEX_NONE &&
		mechanisms[mechanism_index].has_landing)
	{
		*origin_out = mechanisms[mechanism_index].landing_origin;
		*bounds_out = mechanisms[mechanism_index].landing_bounds;
		*has_bounds_out = 1;
	}
	else if (spec->use_witness && mechanism_index != SG_RUNE_COMPACT_INDEX_NONE &&
		spec->kind != SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION &&
		mechanisms[mechanism_index].has_activation_witness)
	{
		*origin_out = mechanisms[mechanism_index].activation_origin;
		*bounds_out = mechanisms[mechanism_index].activation_bounds;
		*has_bounds_out = 1;
	}
	else if (!FindEntityWitness(&input->geometry, entity, origin_out,
		bounds_out, has_bounds_out, &witness_cell))
		return 0;
	*mechanism_out = mechanism_index;
	return 1;
}

static int BuildLandmarks(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	const landmark_spec_t *specs, uint32_t count,
	sg_rune_compact_static_materializer_t *candidate,
	sg_rune_compact_static_materializer_error_t *error)
{
	uint32_t *cell_counts = NULL;
	uint32_t index;
	uint32_t cell_cursor = 0U;
	uint32_t total_cells = 0U;
	size_t count_bytes = 0U;

	if (count != 0U)
	{
		if (!SizeMultiply((size_t)count, sizeof(*cell_counts), &count_bytes))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
			return 0;
		}
		cell_counts = MaterializerCalloc(1U, count_bytes);
		if (cell_counts == NULL)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
			return 0;
		}
	}
	for (index = 0U; index < count; index++)
	{
		sg_rune_q8_vec3_t origin;
		sg_rune_q8_bounds_t bounds;
		uint32_t cells;
		int has_bounds;
		uint32_t mechanism_index;

		if (!LandmarkWitness(input, mechanisms, mechanism_count, &specs[index],
			&origin, &bounds, &has_bounds, &mechanism_index))
		{
			free(cell_counts);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
				index);
			return 0;
		}
		if (!CountCellsForBounds(&input->geometry, &bounds, &origin, &cells))
		{
			free(cell_counts);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, index);
			return 0;
		}
		if (!CountAdd(total_cells, cells, &total_cells))
		{
			free(cell_counts);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, index);
			return 0;
		}
		if (total_cells > SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS)
		{
			free(cell_counts);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, index);
			return 0;
		}
		cell_counts[index] = cells;
	}
	if (count != 0U)
	{
		size_t bytes;

		if (!SizeMultiply((size_t)count, sizeof(*candidate->landmarks), &bytes))
		{
			free(cell_counts);
			SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
			return 0;
		}
		candidate->landmarks = MaterializerCalloc(1U, bytes);
		if (candidate->landmarks == NULL)
		{
			free(cell_counts);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
			return 0;
		}
	}
	if (total_cells != 0U)
	{
		size_t bytes;

		if (!SizeMultiply((size_t)total_cells, sizeof(*candidate->landmark_cells),
				&bytes))
		{
			free(cell_counts);
			SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
			return 0;
		}
		candidate->landmark_cells = MaterializerCalloc(1U, bytes);
		if (candidate->landmark_cells == NULL)
		{
			free(cell_counts);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, 0U);
			return 0;
		}
	}
	for (index = 0U; index < count; index++)
	{
		const landmark_spec_t *spec = &specs[index];
		sg_rune_q8_vec3_t origin;
		sg_rune_q8_bounds_t bounds;
		sg_rune_compact_landmark_t *landmark =
			&candidate->landmarks[index];
		uint32_t mechanism_index;
		uint32_t filled;
		int has_bounds;
		int origin_owned;

		if (!LandmarkWitness(input, mechanisms, mechanism_count, spec, &origin,
			&bounds, &has_bounds, &mechanism_index))
			goto malformed_landmark;
		if (!FillCellsForBounds(&input->geometry, &bounds, &origin,
			candidate->landmark_cells + cell_cursor, cell_counts[index],
			&origin_owned) || !origin_owned)
			goto malformed_landmark;
		memset(landmark, 0, sizeof(*landmark));
		landmark->source.entity_ordinal =
			input->entities->entities[spec->entity_index].canonical_ordinal;
		landmark->cells.first = cell_cursor;
		landmark->cells.count = cell_counts[index];
		landmark->mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
		landmark->origin = origin;
		landmark->bounds = bounds;
		landmark->kind = spec->kind;
		landmark->variant = spec->variant;
		if (mechanism_index != SG_RUNE_COMPACT_INDEX_NONE)
			landmark->mechanism.value = mechanism_index;
		filled = cell_counts[index];
		if (!CountAdd(cell_cursor, filled, &cell_cursor))
			goto malformed_landmark;
		continue;

malformed_landmark:
		free(cell_counts);
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK, index);
		return 0;
	}
	free(cell_counts);
	candidate->view.landmarks = candidate->landmarks;
	candidate->view.landmark_count = count;
	candidate->view.landmark_cells = candidate->landmark_cells;
	candidate->view.landmark_cell_count = total_cells;

	for (index = 0U; index < mechanism_count; index++)
	{
		const sg_rune_compact_mechanism_t *mechanism =
			&candidate->mechanisms[index];
		sg_rune_compact_landmark_kind_t activation_kind;
		uint32_t landmark_index;

		if ((mechanism->activation_mask &
			(sg_rune_compact_static_activation_mask_t)
			~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) == 0U)
			continue;
		if (!ActivationLandmarkKind(mechanism->kind, &activation_kind) ||
			(mechanisms[index].teleporter_pair ?
				!FindLandmarkSpecIndexForPair(specs, count,
					mechanisms[index].entity_index,
					mechanisms[index].destination_entity_index,
					mechanisms[index].fanout_ordinal, &landmark_index) :
				!FindLandmarkSpecIndex(specs, count,
					mechanisms[index].entity_index, activation_kind, 0U,
					&landmark_index)))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
		candidate->mechanisms[index].activation_landmark.value = landmark_index;
	}
	return 1;
}

static int BuildMechanismEdges(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	sg_rune_compact_static_materializer_t *candidate,
	sg_rune_compact_static_materializer_error_t *error)
{
	const sg_rune_compact_mechanisms_view_t *view = input->mechanisms;
	edge_spec_t *specs = NULL;
	uint32_t index;
	uint32_t capacity = 0U;
	uint32_t count = 0U;
	size_t bytes;

	/* A teleporter authority can fan out into several static mechanisms.  Each
	 * mechanism receives its own canonical topology span, so the output may
	 * legitimately contain more edge records than the authority table. */
	for (index = 0U; index < mechanism_count; index++)
	{
		const uint32_t authority_index = mechanisms[index].authority_index;

		if (authority_index >= view->mechanism_count ||
			!CountAdd(capacity,
				view->mechanisms[authority_index].topology.count, &capacity))
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, index);
			return 0;
		}
	}
	if (capacity > SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ||
		!SizeMultiply((size_t)capacity, sizeof(*specs), &bytes))
	{
		SetError(error,
			capacity > SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, 0U);
		return 0;
	}
	if (capacity != 0U)
	{
		specs = MaterializerCalloc(1U, bytes);
		if (specs == NULL)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, 0U);
			return 0;
		}
	}
	for (index = 0U; index < mechanism_count; index++)
	{
		const uint32_t authority_index = mechanisms[index].authority_index;
		uint32_t start = count;
		uint32_t local;

		if (authority_index >= view->mechanism_count)
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, index);
			return 0;
		}
		for (local = 0U; local < view->mechanisms[authority_index].topology.count;
			local++)
		{
			uint32_t topology_index = SG_RUNE_COMPACT_INDEX_NONE;
			uint32_t output_edge_index;
			const sg_rune_compact_mechanism_topology_edge_t *edge;
			sg_rune_compact_mechanism_edge_kind_t kind;

			if (!CountAdd(view->mechanisms[authority_index].topology.first,
				local, &topology_index) || topology_index >=
				view->topology_edge_count || count >= capacity)
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE,
					topology_index);
				return 0;
			}
			edge = &view->topology_edges[topology_index];
			if (!AuthorityTopologyEdgeAccepted(view, authority_index,
				topology_index, edge, input->entities))
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE,
					topology_index);
				return 0;
			}
			if (!MapEdgeKind(edge->kind, &kind))
			{
				free(specs);
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE,
					topology_index);
				return 0;
			}
			output_edge_index = count;
			specs[count].value.source.entity_ordinal =
				edge->source.entity_ordinal;
			specs[count].value.destination.entity_ordinal =
				edge->destination.entity_ordinal;
			specs[count].value.fanout_ordinal = edge->fanout_ordinal;
			specs[count].value.kind = kind;
			/* Controllers carry authority-table ordinals until the canonical
			 * static edge table is assembled.  Translate each reference in the
			 * owning static mechanism while its exact source edge is present. */
			{
				const sg_rune_compact_mechanism_controller_span_t controller_span =
					candidate->mechanisms[index].controllers;
				uint32_t controller_offset;

				for (controller_offset = 0U;
					controller_offset < controller_span.count; controller_offset++)
				{
					const uint32_t controller_index = controller_span.first +
						controller_offset;

					if (controller_index >=
						candidate->view.mechanism_controller_count ||
						candidate->mechanism_controllers == NULL)
					{
						free(specs);
						SetError(error,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
							SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
							controller_index);
						return 0;
					}
					if (candidate->mechanism_controllers[controller_index].topology_edge ==
						topology_index)
						candidate->mechanism_controllers[controller_index].topology_edge =
							output_edge_index;
				}
			}
			count++;
		}
		/* Preserve every authenticated topology fact one-for-one.  Identical
		 * endpoints do not make distinct controller/edge provenance redundant. */
		candidate->mechanisms[index].topology.first = start;
		candidate->mechanisms[index].topology.count = count - start;
	}
	if (count != 0U)
	{
		if (!SizeMultiply((size_t)count, sizeof(*candidate->mechanism_edges),
			&bytes))
		{
			free(specs);
			SetError(error, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, 0U);
			return 0;
		}
		candidate->mechanism_edges = MaterializerCalloc(1U, bytes);
		if (candidate->mechanism_edges == NULL)
		{
			free(specs);
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE, 0U);
			return 0;
		}
		for (index = 0U; index < count; index++)
			candidate->mechanism_edges[index] = specs[index].value;
	}
	free(specs);
	candidate->view.mechanism_edges = candidate->mechanism_edges;
	candidate->view.mechanism_edge_count = count;
	return 1;
}

static int FacetHasVisibilitySurface(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_compact_facet_t *facet,
	const sg_configuration_semantics_t *configuration,
	const sg_static_visibility_t *visibility)
{
	uint32_t visibility_index;

	if (geometry == NULL || facet == NULL || configuration == NULL ||
		visibility == NULL)
		return 0;
	for (visibility_index = 0U;
		visibility_index < visibility->surface_count; visibility_index++)
	{
		const sg_static_visibility_surface_t *surface =
			&visibility->surfaces[visibility_index];
		const sg_configuration_hook_surface_t *semantic_surface;

		if (surface->semantic_surface >= configuration->hook_surface_count)
			continue;
		semantic_surface = &configuration->hook_surfaces[surface->semantic_surface];
		if (surface->model != semantic_surface->model ||
			surface->brush != semantic_surface->brush ||
			surface->brush_side != semantic_surface->brush_side)
			continue;
		if (CompactFacetMatchesHook(geometry, facet, configuration,
			semantic_surface))
			return 1;
	}
	return 0;
}

static int FacetHookAndSky(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_compact_facet_t *facet,
	const sg_configuration_semantics_t *configuration,
	sg_rune_stance_validity_t *hookable_stances,
	int *sky_out)
{
	uint32_t surface_index;

	if (geometry == NULL || facet == NULL || configuration == NULL ||
		hookable_stances == NULL ||
		sky_out == NULL)
		return 0;
	*hookable_stances = 0U;
	*sky_out = 0;
	for (surface_index = 0U;
		surface_index < configuration->hook_surface_count; surface_index++)
	{
		const sg_configuration_hook_surface_t *surface =
			&configuration->hook_surfaces[surface_index];

		if (!CompactFacetMatchesHook(geometry, facet, configuration, surface))
			continue;
		if ((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY) != 0U)
			*sky_out = 1;
		if ((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) != 0U)
		{
			*hookable_stances = (sg_rune_stance_validity_t)(
				*hookable_stances | SG_RUNE_STANCE_VALID_ALL);
		}
	}
	return 1;
}

static void FacetBoundaryAttributes(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t facet_index, sg_rune_compact_facet_attributes_t *attributes)
{
	const sg_rune_compact_facet_t *facet = &geometry->facets[facet_index];
	uint32_t offset;

	for (offset = 0U; offset < facet->incidences.count; offset++)
	{
		const uint32_t incidence_index = facet->incidences.first + offset;
		const sg_rune_compact_incidence_t *incidence =
			&geometry->incidences[incidence_index];
		const sg_rune_compact_facet_attributes_t cover =
			incidence->side == SG_RUNE_FACET_NEGATIVE_SIDE ?
				SG_RUNE_COMPACT_FACET_COVER_NEGATIVE :
				SG_RUNE_COMPACT_FACET_COVER_POSITIVE;
		const sg_rune_compact_facet_attributes_t exposure =
			incidence->side == SG_RUNE_FACET_NEGATIVE_SIDE ?
				SG_RUNE_COMPACT_FACET_EXPOSURE_NEGATIVE :
				SG_RUNE_COMPACT_FACET_EXPOSURE_POSITIVE;

		if (incidence->boundary == SG_RUNE_BOUNDARY_CLOSED)
			*attributes = (sg_rune_compact_facet_attributes_t)(*attributes | cover);
		else
			*attributes = (sg_rune_compact_facet_attributes_t)(*attributes |
				exposure);
	}
}

static void FacetCellAttributes(
	const sg_rune_compact_static_geometry_view_t *geometry,
	uint32_t facet_index, sg_rune_compact_facet_attributes_t *attributes)
{
	const sg_rune_compact_facet_t *facet = &geometry->facets[facet_index];
	uint32_t offset;

	for (offset = 0U; offset < facet->incidences.count; offset++)
	{
		uint32_t cell;

		if (!FacetIncidentCell(geometry, facet_index, offset, &cell))
			continue;
		if (IsHazardousCell(&geometry->cells[cell]))
			*attributes = (sg_rune_compact_facet_attributes_t)(*attributes |
				SG_RUNE_COMPACT_FACET_HAZARD);
		if ((geometry->cells[cell].semantics &
			SG_RUNE_COMPACT_CELL_SKY_BOUNDARY) != 0U ||
			(geometry->cells[cell].contents & SG_RUNE_COMPACT_CONTENTS_SKY) != 0U)
			*attributes = (sg_rune_compact_facet_attributes_t)(*attributes |
				SG_RUNE_COMPACT_FACET_SKY);
	}
}

static void ConfigurationCellAttributes(
	const materializer_input_view_t *input,
	uint32_t facet_index, sg_rune_compact_facet_attributes_t *attributes)
{
	const sg_rune_compact_facet_t *facet = &input->geometry.facets[facet_index];
	uint32_t region_index;

	for (region_index = 0U;
		region_index < input->configuration->region_count; region_index++)
	{
		const sg_configuration_semantic_region_t *region =
			&input->configuration->regions[region_index];
		uint32_t face_offset;

		if ((region->flags & (SG_CONFIGURATION_SEMANTIC_REGION_HAZARD |
			SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
			SG_CONFIGURATION_SEMANTIC_REGION_SLIME)) != 0U)
			for (face_offset = 0U;
				face_offset < facet->incidences.count; face_offset++)
			{
				uint32_t incident_cell;

				if (FacetIncidentCell(&input->geometry, facet_index, face_offset,
					&incident_cell) && CompactCellMappedToConfigurationCell(
					&input->geometry, region->cell, incident_cell))
					*attributes = (sg_rune_compact_facet_attributes_t)(*attributes |
						SG_RUNE_COMPACT_FACET_HAZARD);
			}
	}
}

static int BuildFacetAnnotations(
	const materializer_input_view_t *input,
	sg_rune_compact_static_materializer_t *candidate,
	sg_rune_compact_static_materializer_error_t *error)
{
	uint32_t facet_index;
	uint32_t count = 0U;
	size_t bytes;

	for (facet_index = 0U; facet_index < input->geometry.facet_count;
		facet_index++)
		{
			const sg_rune_compact_facet_t *facet =
				&input->geometry.facets[facet_index];
			sg_rune_compact_facet_attributes_t attributes = 0U;
			sg_rune_stance_validity_t hookable_stances = 0U;
			int sky = 0;
		int hook_or_sky;

		/* A constraint-only plane has no area and therefore carries no
		 * surface annotation.  Its exact plane/source remains in geometry. */
		if (facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY)
			continue;
		hook_or_sky = FacetHookAndSky(&input->geometry, facet,
			input->configuration,
			&hookable_stances, &sky);
		if (!hook_or_sky)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, facet_index);
			return 0;
		}
		FacetBoundaryAttributes(&input->geometry, facet_index, &attributes);
		FacetCellAttributes(&input->geometry, facet_index, &attributes);
		ConfigurationCellAttributes(input, facet_index, &attributes);
		if (sky)
			attributes = (sg_rune_compact_facet_attributes_t)(attributes |
				SG_RUNE_COMPACT_FACET_SKY);
		if (hookable_stances != 0U)
		{
			const sg_rune_stance_validity_t cell_stances =
				FacetHookableStances(&input->geometry, facet_index);

			hookable_stances = (sg_rune_stance_validity_t)(hookable_stances &
				cell_stances);
			if (hookable_stances != 0U)
				attributes = (sg_rune_compact_facet_attributes_t)(attributes |
					SG_RUNE_COMPACT_FACET_HOOKABLE);
			else
				attributes = (sg_rune_compact_facet_attributes_t)(attributes &
					(sg_rune_compact_facet_attributes_t)
					~SG_RUNE_COMPACT_FACET_HOOKABLE);
		}
		if (FacetHasVisibilitySurface(&input->geometry, facet,
			input->configuration,
			input->visibility))
			attributes = (sg_rune_compact_facet_attributes_t)(attributes |
				SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY);
		if ((attributes & SG_RUNE_COMPACT_FACET_SKY) != 0U)
		{
			attributes = (sg_rune_compact_facet_attributes_t)(attributes &
				(sg_rune_compact_facet_attributes_t)
				~SG_RUNE_COMPACT_FACET_HOOKABLE);
			hookable_stances = 0U;
		}
		if (attributes != 0U)
		{
			if (!CountAdd(count, 1U, &count))
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, facet_index);
				return 0;
			}
		}
	}
	if (count > SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS ||
		!SizeMultiply((size_t)count, sizeof(*candidate->facet_annotations),
			&bytes))
	{
		SetError(error,
			count > SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, 0U);
		return 0;
	}
	if (count != 0U)
	{
		candidate->facet_annotations = MaterializerCalloc(1U, bytes);
		if (candidate->facet_annotations == NULL)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, 0U);
			return 0;
		}
	}
	/* Keep the first-pass count available while the second pass fills the
	 * transaction-local array.  It is published as a view only after filling. */
	candidate->view.facet_annotation_count = count;
	count = 0U;
	for (facet_index = 0U; facet_index < input->geometry.facet_count;
		facet_index++)
	{
		const sg_rune_compact_facet_t *facet =
			&input->geometry.facets[facet_index];
			sg_rune_compact_facet_attributes_t attributes = 0U;
			sg_rune_stance_validity_t hookable_stances = 0U;
			int sky = 0;
			uint32_t source_surface = SG_RUNE_COMPACT_INDEX_NONE;

		if (facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY)
			continue;
		if (!FacetHookAndSky(&input->geometry, facet, input->configuration,
			&hookable_stances,
			&sky))
			continue;
		FacetBoundaryAttributes(&input->geometry, facet_index, &attributes);
		FacetCellAttributes(&input->geometry, facet_index, &attributes);
		ConfigurationCellAttributes(input, facet_index, &attributes);
		if (sky)
			attributes = (sg_rune_compact_facet_attributes_t)(attributes |
				SG_RUNE_COMPACT_FACET_SKY);
		if (hookable_stances != 0U)
			hookable_stances = (sg_rune_stance_validity_t)(hookable_stances &
				FacetHookableStances(&input->geometry, facet_index));
		if (hookable_stances != 0U &&
			(attributes & SG_RUNE_COMPACT_FACET_SKY) == 0U)
			attributes = (sg_rune_compact_facet_attributes_t)(attributes |
				SG_RUNE_COMPACT_FACET_HOOKABLE);
		else
			hookable_stances = 0U;
		if (FacetHasVisibilitySurface(&input->geometry, facet,
			input->configuration,
			input->visibility))
			attributes = (sg_rune_compact_facet_attributes_t)(attributes |
				SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY);
		if (attributes == 0U)
			continue;
		/* Surface facts are keyed by the authenticated world source root.  A
		 * model-local root is not a compact world facet and must never be
		 * projected into this annotation table. */
		if (FindSourceSurfaceIndexForFacet(&input->geometry, facet,
			&source_surface))
		{
			const sg_rune_compact_source_surface_t *source =
				&input->geometry.source_surfaces[source_surface];

			if (source->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ||
				source->source.model != SG_HOST_COLLISION_MODEL_WORLD)
			{
				SetError(error,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, facet_index);
				return 0;
			}
		}
		if (candidate->facet_annotations == NULL ||
			count >= candidate->view.facet_annotation_count)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, facet_index);
			return 0;
		}
		candidate->facet_annotations[count].facet.value = facet_index;
		candidate->facet_annotations[count].attributes = attributes;
		candidate->facet_annotations[count].hookable_stances = hookable_stances;
		candidate->facet_annotations[count].source_surface = source_surface;
		candidate->facet_annotations[count].source_frame =
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
		count++;
	}
	if (count != candidate->view.facet_annotation_count)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET, count);
		return 0;
	}
	candidate->view.facet_annotations = candidate->facet_annotations;
	candidate->view.facet_annotation_count = count;
	return 1;
}

static int PortalTransitionCells(
	const sg_rune_compact_static_geometry_view_t *geometry,
	const sg_rune_compact_portal_t *portal, uint32_t *entry_cell_out,
	uint32_t *exit_cell_out)
{
	const sg_rune_compact_incidence_t *negative;
	const sg_rune_compact_incidence_t *positive;

	if (geometry == NULL || portal == NULL || entry_cell_out == NULL ||
		exit_cell_out == NULL || portal->negative_incidence.value >=
		geometry->incidence_count || portal->positive_incidence.value >=
		geometry->incidence_count)
		return 0;
	negative = &geometry->incidences[portal->negative_incidence.value];
	positive = &geometry->incidences[portal->positive_incidence.value];
	if (negative->cell.value >= geometry->cell_count ||
		positive->cell.value >= geometry->cell_count ||
		negative->cell.value == positive->cell.value)
		return 0;
	switch (portal->direction)
	{
	case SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE:
		*entry_cell_out = negative->cell.value;
		*exit_cell_out = positive->cell.value;
		break;
	case SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE:
		*entry_cell_out = positive->cell.value;
		*exit_cell_out = negative->cell.value;
		break;
	case SG_RUNE_PORTAL_CONTINUITY_BOTH:
		/* BOTH is a bidirectional capability.  Its canonical static endpoint
		 * ordering is the authenticated negative-to-positive incidence order;
		 * the runtime may use the reverse direction too. */
		*entry_cell_out = negative->cell.value;
		*exit_cell_out = positive->cell.value;
		break;
	case SG_RUNE_PORTAL_CONTINUITY_COUNT:
		return 0;
	}
	return *entry_cell_out != *exit_cell_out;
}

static int FindAuthorityMoverTransition(
	const materializer_input_view_t *input,
	const sg_rune_compact_mechanism_transition_t *transition,
	uint32_t *entry_cell_out, uint32_t *exit_cell_out,
	uint32_t *portal_out, uint32_t *model_out)
{
	const sg_rune_compact_portal_t *portal;
	authority_transition_fields_t fields;
	uint32_t entry_cell = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t exit_cell = SG_RUNE_COMPACT_INDEX_NONE;

	if (input == NULL || transition == NULL || entry_cell_out == NULL ||
		exit_cell_out == NULL || portal_out == NULL || model_out == NULL ||
		!DecodeAuthorityTransition(transition, &fields) ||
		fields.kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
		fields.portal >= input->geometry.portal_count)
		return 0;
	portal = &input->geometry.portals[fields.portal];
	if (!PortalTransitionCells(&input->geometry, portal, &entry_cell,
		&exit_cell) || entry_cell != fields.entry_cell ||
		exit_cell != fields.exit_cell ||
		fields.mover_model == SG_BSP_ENTITY_MODEL_NONE ||
		fields.mover_model >=
			input->geometry.identity.source_counts.model_count)
		return 0;
	*entry_cell_out = entry_cell;
	*exit_cell_out = exit_cell;
	*portal_out = fields.portal;
	*model_out = fields.mover_model;
	return 1;
}

static int CompareProjectedPortalMechanism(const void *left_pointer,
	const void *right_pointer)
{
	const sg_rune_compact_portal_mechanism_t *left = left_pointer;
	const sg_rune_compact_portal_mechanism_t *right = right_pointer;
	int comparison = left->mechanism.value < right->mechanism.value ? -1 :
		left->mechanism.value > right->mechanism.value ? 1 : 0;

	if (comparison == 0)
		comparison = left->portal.value < right->portal.value ? -1 :
			left->portal.value > right->portal.value ? 1 : 0;
	if (comparison == 0)
		comparison = (uint32_t)left->kind < (uint32_t)right->kind ? -1 :
			(uint32_t)left->kind > (uint32_t)right->kind ? 1 : 0;
	return comparison;
}

static int BuildPortalMechanisms(
	const materializer_input_view_t *input,
	const mechanism_spec_t *mechanisms, uint32_t mechanism_count,
	sg_rune_compact_static_materializer_t *candidate,
	sg_rune_compact_static_materializer_error_t *error)
{
	uint32_t transition_index;
	uint32_t count = 0U;
	uint32_t output_index = 0U;
	size_t output_bytes;

	/* Portal bindings are one record per authenticated (root, portal) fact.
	 * Count and then fill a transition-sized projection, sort it by root first,
	 * and reject duplicate keys.  This remains linear in the transition table
	 * apart from the deterministic sort and does not scan a portal against every
	 * transition. */
	if (input == NULL || candidate == NULL ||
		(input->geometry.portal_count != 0U &&
			input->geometry.portal_count > SG_RUNE_COMPACT_MAX_PORTALS) ||
		(candidate->view.transition_count != 0U && candidate->transitions == NULL))
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, 0U);
		return 0;
	}
	for (transition_index = 0U;
		transition_index < candidate->view.transition_count;
		transition_index++)
	{
		const sg_rune_compact_static_transition_t *transition =
			&candidate->transitions[transition_index];
		sg_rune_compact_portal_mechanism_kind_t kind;

		if (transition->kind >= SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
				transition_index);
			return 0;
		}
		if (transition->kind != SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE)
			continue;
		if (transition->value.portal_state.portal.value >=
				input->geometry.portal_count ||
			transition->mechanism.value >= mechanism_count ||
			!PortalMechanismKindForMechanism(input,
				&mechanisms[transition->mechanism.value], &kind))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
				transition_index);
			return 0;
		}
		if (!CountAdd(count, 1U, &count))
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
				transition_index);
			return 0;
		}
	}
	if (count > SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS ||
		!SizeMultiply((size_t)count, sizeof(*candidate->portal_mechanisms),
			&output_bytes))
	{
		SetError(error,
			count > SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS ?
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED :
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, 0U);
		return 0;
	}
	if (count != 0U)
	{
		candidate->portal_mechanisms = MaterializerCalloc(1U, output_bytes);
		if (candidate->portal_mechanisms == NULL)
		{
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, 0U);
			return 0;
		}
	}
	for (transition_index = 0U;
		transition_index < candidate->view.transition_count;
		transition_index++)
	{
		const sg_rune_compact_static_transition_t *transition =
			&candidate->transitions[transition_index];
		sg_rune_compact_portal_mechanism_kind_t kind;

		if (transition->kind != SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE)
			continue;
		if (output_index >= count ||
			transition->value.portal_state.portal.value >=
				input->geometry.portal_count ||
			transition->mechanism.value >= mechanism_count ||
			!PortalMechanismKindForMechanism(input,
				&mechanisms[transition->mechanism.value], &kind))
		{
			free(candidate->portal_mechanisms);
			candidate->portal_mechanisms = NULL;
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
				transition_index);
			return 0;
		}
		candidate->portal_mechanisms[output_index].portal.value =
			transition->value.portal_state.portal.value;
		candidate->portal_mechanisms[output_index].mechanism.value =
			transition->mechanism.value;
		candidate->portal_mechanisms[output_index].kind = kind;
		output_index++;
	}
	if (output_index != count)
	{
		free(candidate->portal_mechanisms);
		candidate->portal_mechanisms = NULL;
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL, output_index);
		return 0;
	}
	if (count > 1U)
		qsort(candidate->portal_mechanisms, (size_t)count,
			sizeof(*candidate->portal_mechanisms), CompareProjectedPortalMechanism);
	for (transition_index = 1U; transition_index < count; transition_index++)
		if (candidate->portal_mechanisms[transition_index - 1U].mechanism.value ==
			candidate->portal_mechanisms[transition_index].mechanism.value &&
			candidate->portal_mechanisms[transition_index - 1U].portal.value ==
			candidate->portal_mechanisms[transition_index].portal.value)
		{
			free(candidate->portal_mechanisms);
			candidate->portal_mechanisms = NULL;
			SetError(error,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_AMBIGUOUS_BINDING,
				SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL,
				transition_index);
			return 0;
		}
	candidate->view.portal_mechanisms = candidate->portal_mechanisms;
	candidate->view.portal_mechanism_count = count;
	return 1;
}

static int BuildMaterializer(
	const materializer_input_view_t *input,
	sg_rune_compact_static_materializer_t **candidate_out,
	sg_rune_compact_static_materializer_error_t *error)
{
	sg_rune_compact_static_materializer_t *candidate;
	mechanism_spec_t *mechanisms = NULL;
	landmark_spec_t *landmarks = NULL;
	uint32_t mechanism_count = 0U;
	uint32_t landmark_count = 0U;

	candidate = MaterializerCalloc(1U, sizeof(*candidate));
	if (candidate == NULL)
	{
		SetError(error,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	candidate->identity = input->geometry.identity;
	if (!BuildMechanisms(input, candidate, &mechanisms, &mechanism_count,
		error) ||
		!BuildMechanismControllers(input, mechanisms, mechanism_count, candidate,
			error) ||
		!BuildTransitions(input, mechanisms, mechanism_count, candidate, error) ||
		!BuildLandmarkSpecs(input, mechanisms, mechanism_count, &landmarks,
			&landmark_count, error) ||
		!BuildLandmarks(input, mechanisms, mechanism_count, landmarks,
			landmark_count, candidate, error) ||
		!BuildMechanismEdges(input, mechanisms, mechanism_count, candidate,
			error) ||
		!BuildFacetAnnotations(input, candidate, error) ||
		!BuildPortalMechanisms(input, mechanisms, mechanism_count, candidate,
			error))
	{
		free(landmarks);
		free(mechanisms);
		DestroyCandidate(candidate);
		return 0;
	}
	free(landmarks);
	free(mechanisms);
	*candidate_out = candidate;
	return 1;
}

int SG_RuneCompactStaticMaterializerBuild(
	const sg_rune_compact_static_materializer_input_t *input,
	sg_rune_compact_static_materializer_t **materializer_out,
	sg_rune_compact_static_materializer_error_t *error_out)
{
	materializer_input_view_t resolved;
	sg_rune_compact_mechanisms_view_t mechanisms_view;

	if (materializer_out != NULL)
		*materializer_out = NULL;
	SetError(error_out, SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_NONE,
		SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
	if (materializer_out == NULL || input == NULL || input->mechanisms == NULL)
	{
		SetError(error_out,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL, 0U);
		return 0;
	}
	memset(&mechanisms_view, 0, sizeof(mechanisms_view));
	if (!SG_RuneCompactMechanismsRead(input->mechanisms, &mechanisms_view))
	{
		SetError(error_out,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM, 0U);
		return 0;
	}
	memset(&resolved, 0, sizeof(resolved));
	resolved.geometry = input->geometry;
	resolved.entities = input->entities;
	resolved.configuration = input->configuration;
	resolved.visibility = input->visibility;
	resolved.mechanisms = &mechanisms_view;
	if (!GeometryInputValid(&resolved, error_out))
		return 0;
	return BuildMaterializer(&resolved, materializer_out, error_out);
}

int SG_RuneCompactStaticMaterializerRead(
	const sg_rune_compact_static_materializer_t *materializer,
	sg_rune_compact_static_t *static_out)
{
	if (materializer == NULL || static_out == NULL)
		return 0;
	*static_out = materializer->view;
	return 1;
}

int SG_RuneCompactStaticMaterializerReadBound(
	const sg_rune_compact_static_materializer_t *materializer,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_static_t *static_out)
{
	if (materializer == NULL || identity_out == NULL || static_out == NULL)
		return 0;
	*identity_out = materializer->identity;
	*static_out = materializer->view;
	return 1;
}

int SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t authority_transition_index, uint32_t *static_transition_index_out)
{
	if (materializer == NULL || static_transition_index_out == NULL ||
		authority_transition_index >= materializer->authority_transition_count ||
		materializer->authority_transition_static == NULL)
		return 0;
	if (materializer->authority_transition_static[authority_transition_index] ==
		SG_RUNE_COMPACT_INDEX_NONE ||
		materializer->authority_transition_static[authority_transition_index] >=
			materializer->view.transition_count)
		return 0;
	*static_transition_index_out =
		materializer->authority_transition_static[authority_transition_index];
	return 1;
}

int SG_RuneCompactStaticMaterializerStaticMechanismAuthorityIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t static_mechanism_index, uint32_t *authority_mechanism_index_out)
{
	if (materializer == NULL || authority_mechanism_index_out == NULL ||
		static_mechanism_index >= materializer->static_mechanism_count ||
		materializer->static_mechanism_authority == NULL)
		return 0;
	if (materializer->static_mechanism_authority[static_mechanism_index] ==
		SG_RUNE_COMPACT_INDEX_NONE)
		return 0;
	*authority_mechanism_index_out =
		materializer->static_mechanism_authority[static_mechanism_index];
	return 1;
}

void SG_RuneCompactStaticMaterializerDestroy(
	sg_rune_compact_static_materializer_t *materializer)
{
	DestroyCandidate(materializer);
}

const char *SG_RuneCompactStaticMaterializerErrorString(
	sg_rune_compact_static_materializer_error_code_t code)
{
	switch (code)
	{
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE:
		return "invalid source";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS:
		return "invalid static semantics";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_AMBIGUOUS_BINDING:
		return "ambiguous binding";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW:
		return "overflow";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_CODE_COUNT:
		break;
	}
	return "unknown compact static materializer error";
}
