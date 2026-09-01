#include "sg_rune_compact_static.h"

#include "sg_rune_compact_binary32.h"
#include "sg_bsp_entity_semantics.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static uint32_t StaticFloatBits(float value);

static void SetError(sg_rune_compact_static_error_t *error,
	sg_rune_compact_static_error_code_t code,
	sg_rune_compact_static_record_domain_t domain, uint32_t record)
{
	if (error == NULL)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static int ArrayPresent(const void *values, uint32_t count)
{
	return count == 0U || values != NULL;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

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

static int BoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PointInBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int ControllerLocationZero(
	const sg_rune_compact_static_mechanism_controller_t *controller)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (controller->activation_witness.value[axis] != 0 ||
			controller->activation_bounds.mins.value[axis] != 0 ||
			controller->activation_bounds.maxs.value[axis] != 0)
			return 0;
	return 1;
}

static int BoundsOverlap(const sg_rune_q8_bounds_t *left,
	const sg_rune_q8_bounds_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (left->maxs.value[axis] <= right->mins.value[axis] ||
			left->mins.value[axis] >= right->maxs.value[axis])
			return 0;
	return 1;
}

static int MechanismKindValid(sg_rune_compact_mechanism_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_MECHANISM_DOOR &&
		kind < SG_RUNE_COMPACT_MECHANISM_KIND_COUNT;
}

static int StaticActivationMaskValid(
	sg_rune_compact_static_activation_mask_t mask)
{
	return mask != 0U &&
		(mask & (sg_rune_compact_static_activation_mask_t)
			~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_KNOWN) == 0U;
}

static int Binary32Finite(uint32_t bits)
{
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent != UINT32_C(0xff) && bits != UINT32_C(0x80000000);
}

static int Binary32CanonicalNonnegative(uint32_t bits)
{
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent != UINT32_C(0xff) &&
		(bits & UINT32_C(0x80000000)) == 0U;
}

static int StaticTransitionInactiveTailZero(
	const sg_rune_compact_static_transition_t *transition)
{
	const unsigned char *bytes;
	size_t active_size;
	size_t index;

	if (transition == NULL)
		return 0;
	switch (transition->kind)
	{
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
		active_size = sizeof(transition->value.portal_state);
		break;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
		active_size = sizeof(transition->value.teleport);
		break;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
		active_size = sizeof(transition->value.push);
		break;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		active_size = sizeof(transition->value.transport);
		break;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
		return 0;
	}
	bytes = (const unsigned char *)&transition->value;
	for (index = active_size; index < sizeof(transition->value); index++)
		if (bytes[index] != 0U)
			return 0;
	return 1;
}

/* Transition launch vectors preserve host AngleVectors' signed-zero bits.
 * Other static binary fields continue to use Binary32Finite. */
static int TransitionLaunchBinary32Finite(uint32_t bits)
{
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent != UINT32_C(0xff);
}

static float StaticBitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

int SG_RuneCompactStaticTransportDeriveWorldPointBits(
	const sg_rune_q8_vec3_t *local, const uint32_t mover_origin_bits[3],
	const uint32_t mover_axis_bits[3][3], uint32_t world_bits_out[3])
{
	float origin[3];
	float local_float[3];
	float world[3];
	float axis[3][3];
	uint32_t row;
	uint32_t column;
	uint32_t coordinate;

	if (local == NULL || mover_origin_bits == NULL ||
		mover_axis_bits == NULL || world_bits_out == NULL)
		return 0;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
	{
		if (!Binary32Finite(mover_origin_bits[coordinate]))
			return 0;
		origin[coordinate] = StaticBitsFloat(mover_origin_bits[coordinate]);
		local_float[coordinate] = (float)local->value[coordinate] * 0.125f;
	}
	for (row = 0U; row < 3U; row++)
		for (column = 0U; column < 3U; column++)
		{
			if (!Binary32Finite(mover_axis_bits[row][column]))
				return 0;
			axis[row][column] = StaticBitsFloat(mover_axis_bits[row][column]);
		}
	if (!SG_RuneCompactBinary32TransformPoint(local_float, origin,
		(const float (*)[3])axis, world))
		return 0;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
	{
		if (!isfinite(world[coordinate]))
			return 0;
		world_bits_out[coordinate] = StaticFloatBits(world[coordinate]);
		if (!Binary32Finite(world_bits_out[coordinate]))
			return 0;
	}
	return 1;
}

static int StaticPlaneValue(const sg_rune_binary32_plane_t *plane,
	const sg_rune_q8_vec3_t *point, double *value_out)
{
	double value;
	uint32_t axis;
	int has_normal = 0;

	if (plane == NULL || point == NULL || value_out == NULL ||
		!Binary32Finite(plane->distance_bits))
		return 0;
	value = -(double)StaticBitsFloat(plane->distance_bits);
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!Binary32Finite(plane->normal_bits[axis]))
			return 0;
		if ((plane->normal_bits[axis] & UINT32_C(0x7fffffff)) != 0U)
			has_normal = 1;
		value += (double)StaticBitsFloat(plane->normal_bits[axis]) *
			(double)point->value[axis] / 8.0;
	}
	if (!has_normal || !isfinite(value))
		return 0;
	*value_out = value;
	return 1;
}

/* A transition witness is an authenticated point, not merely a point inside
 * the cell's AABB.  Re-evaluate every cell half-space so a copied output
 * cannot relocate a teleport/push endpoint into an overlapping neighbour. */
static int StaticCellContainsPoint(const sg_rune_compact_model_t *model,
	uint32_t cell_index, const sg_rune_q8_vec3_t *point)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t local;

	if (model == NULL || point == NULL || cell_index >= model->cell_count ||
		model->cells == NULL || model->cell_incidences == NULL ||
		model->incidences == NULL || model->facets == NULL)
		return 0;
	cell = &model->cells[cell_index];
	if (!BoundsValid(&cell->bounds) || !PointInBounds(point, &cell->bounds) ||
		cell->incidences.count == 0U ||
		cell->incidences.first > model->cell_incidence_count ||
		cell->incidences.count > model->cell_incidence_count -
			cell->incidences.first)
		return 0;
	for (local = 0U; local < cell->incidences.count; local++)
	{
		const uint32_t reference = cell->incidences.first + local;
		const uint32_t incidence_index = model->cell_incidences[reference].value;
		const sg_rune_compact_incidence_t *incidence;
		const sg_rune_compact_facet_t *facet;
		double value;

		if (incidence_index >= model->incidence_count)
			return 0;
		incidence = &model->incidences[incidence_index];
		if (incidence->cell.value != cell_index ||
			incidence->facet.value >= model->facet_count ||
			(uint32_t)incidence->side >= (uint32_t)SG_RUNE_FACET_SIDE_COUNT ||
			(uint32_t)incidence->boundary >=
				(uint32_t)SG_RUNE_BOUNDARY_OWNERSHIP_COUNT)
			return 0;
		facet = &model->facets[incidence->facet.value];
		if (!StaticPlaneValue(&facet->plane, point, &value))
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
		else if (incidence->boundary == SG_RUNE_BOUNDARY_CLOSED)
		{
			if (value < -1.0e-7)
				return 0;
		}
		else if (value <= 1.0e-7)
			return 0;
	}
	return 1;
}

static int StaticPlaneValueWorld(const sg_rune_binary32_plane_t *plane,
	const double point[3], double *value_out)
{
	double value;
	uint32_t axis;
	int has_normal = 0;

	if (plane == NULL || point == NULL || value_out == NULL ||
		!Binary32Finite(plane->distance_bits))
		return 0;
	value = -(double)StaticBitsFloat(plane->distance_bits);
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!Binary32Finite(plane->normal_bits[axis]))
			return 0;
		if ((plane->normal_bits[axis] & UINT32_C(0x7fffffff)) != 0U)
			has_normal = 1;
		value += (double)StaticBitsFloat(plane->normal_bits[axis]) *
			point[axis];
	}
	if (!has_normal || !isfinite(value))
		return 0;
	*value_out = value;
	return 1;
}

/* World endpoint witnesses retain exact binary32 values rather than Q8
 * coordinates.  Check the cell half-spaces directly, including the cell
 * bounds, so an overlapping AABB cannot make a copied endpoint valid. */
static int StaticCellContainsWorldPoint(
	const sg_rune_compact_model_t *model, uint32_t cell_index,
	const uint32_t bits[3])
{
	const sg_rune_compact_cell_t *cell;
	double point[3];
	uint32_t local;

	if (model == NULL || bits == NULL || cell_index >= model->cell_count ||
		model->cells == NULL || model->cell_incidences == NULL ||
		model->incidences == NULL || model->facets == NULL)
		return 0;
	for (local = 0U; local < 3U; local++)
	{
		if (!Binary32Finite(bits[local]))
			return 0;
		point[local] = (double)StaticBitsFloat(bits[local]);
	}
	cell = &model->cells[cell_index];
	if (!BoundsValid(&cell->bounds) ||
		point[0] * 8.0 < (double)cell->bounds.mins.value[0] ||
		point[1] * 8.0 < (double)cell->bounds.mins.value[1] ||
		point[2] * 8.0 < (double)cell->bounds.mins.value[2] ||
		point[0] * 8.0 >= (double)cell->bounds.maxs.value[0] ||
		point[1] * 8.0 >= (double)cell->bounds.maxs.value[1] ||
		point[2] * 8.0 >= (double)cell->bounds.maxs.value[2] ||
		cell->incidences.count == 0U ||
		cell->incidences.first > model->cell_incidence_count ||
		cell->incidences.count > model->cell_incidence_count -
			cell->incidences.first)
		return 0;
	for (local = 0U; local < cell->incidences.count; local++)
	{
		const uint32_t reference = cell->incidences.first + local;
		const uint32_t incidence_index = model->cell_incidences[reference].value;
		const sg_rune_compact_incidence_t *incidence;
		const sg_rune_compact_facet_t *facet;
		double value;

		if (incidence_index >= model->incidence_count)
			return 0;
		incidence = &model->incidences[incidence_index];
		if (incidence->cell.value != cell_index ||
			incidence->facet.value >= model->facet_count ||
			(uint32_t)incidence->side >= (uint32_t)SG_RUNE_FACET_SIDE_COUNT ||
			(uint32_t)incidence->boundary >=
				(uint32_t)SG_RUNE_BOUNDARY_OWNERSHIP_COUNT)
			return 0;
		facet = &model->facets[incidence->facet.value];
		if (!StaticPlaneValueWorld(&facet->plane, point, &value))
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
		else if (incidence->boundary == SG_RUNE_BOUNDARY_CLOSED)
		{
			if (value < -1.0e-7)
				return 0;
		}
		else if (value <= 1.0e-7)
			return 0;
	}
	return 1;
}

/* A transport support witness is tied to the exact model-local root surface,
 * not merely to a coplanar world facet.  BSP faces are convex; an
 * orientation-independent projected edge test is enough to verify the Q8
 * point lies on the authenticated polygon. */
static int StaticSourceSurfaceSupportsPoint(
	const sg_rune_compact_model_t *model, uint32_t surface_index,
	uint32_t mover_model, const sg_rune_q8_vec3_t *point)
{
	const sg_rune_compact_source_surface_t *surface;
	const sg_rune_q8_vec3_t *vertices;
	float normal[3];
	double plane_value;
	uint32_t drop_axis = 0U;
	uint32_t vertex;
	int sign = 0;

	if (model == NULL || point == NULL || model->source_surfaces == NULL ||
		model->source_surface_vertices == NULL ||
		surface_index >= model->source_surface_count)
		return 0;
	surface = &model->source_surfaces[surface_index];
	if (surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL ||
		surface->cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->parent_surface != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->split_ordinal != 0U ||
		surface->source.model != mover_model ||
		surface->source.model == SG_HOST_COLLISION_MODEL_WORLD ||
		surface->vertices.count < 3U ||
		surface->vertices.first > model->source_surface_vertex_count ||
		surface->vertices.count > model->source_surface_vertex_count -
			surface->vertices.first)
		return 0;
	if (!StaticPlaneValue(&surface->plane, point, &plane_value) ||
		fabs(plane_value) > 1.0e-6)
		return 0;
	for (vertex = 0U; vertex < 3U; vertex++)
	{
		normal[vertex] = StaticBitsFloat(surface->plane.normal_bits[vertex]);
		if (!isfinite(normal[vertex]))
			return 0;
		if (fabs((double)normal[vertex]) > fabs((double)normal[drop_axis]))
			drop_axis = vertex;
	}
	vertices = &model->source_surface_vertices[surface->vertices.first];
	for (vertex = 0U; vertex < surface->vertices.count; vertex++)
	{
		const uint32_t next = (vertex + 1U) % surface->vertices.count;
		uint32_t a0;
		uint32_t a1;
		uint32_t b0;
		uint32_t b1;
		double ax;
		double ay;
		double bx;
		double by;
		double px;
		double py;
		double cross;

		if (drop_axis == 0U)
		{
			a0 = 1U; a1 = 2U;
		}
		else if (drop_axis == 1U)
		{
			a0 = 0U; a1 = 2U;
		}
		else
		{
			a0 = 0U; a1 = 1U;
		}
		b0 = a0;
		b1 = a1;
		ax = (double)vertices[vertex].value[a0] / 8.0;
		ay = (double)vertices[vertex].value[a1] / 8.0;
		bx = (double)vertices[next].value[b0] / 8.0;
		by = (double)vertices[next].value[b1] / 8.0;
		px = (double)point->value[a0] / 8.0;
		py = (double)point->value[a1] / 8.0;
		cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
		if (cross > 1.0e-7)
		{
			if (sign < 0)
				return 0;
			sign = 1;
		}
		else if (cross < -1.0e-7)
		{
			if (sign > 0)
				return 0;
			sign = -1;
		}
	}
	return sign != 0;
}

static uint32_t StaticFloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int StaticTransportTransformsCanonical(
	const sg_rune_compact_static_transport_t *transport)
{
	uint32_t axis;
	uint32_t row;
	uint32_t column;

	if (transport == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!Binary32Finite(transport->source_mover_origin_bits[axis]) ||
			!Binary32Finite(transport->destination_mover_origin_bits[axis]))
			return 0;
	for (row = 0U; row < 3U; row++)
		for (column = 0U; column < 3U; column++)
			if (!Binary32Finite(transport->source_mover_axis_bits[row][column]) ||
				!Binary32Finite(
					transport->destination_mover_axis_bits[row][column]))
				return 0;
	return 1;
}

static int StaticTransportPoseValid(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_transport_t *transport)
{
	const sg_rune_compact_hull_t *hull;
	int64_t expected_player_z;
	uint32_t expected_source_player_world[3];
	uint32_t expected_destination_player_world[3];
	uint32_t expected_source_support_world[3];
	uint32_t expected_destination_support_world[3];
	uint32_t axis;

	if (model == NULL || transport == NULL ||
		!StaticTransportTransformsCanonical(transport))
		return 0;
	if (transport->stance >= SG_RUNE_STANCE_COUNT)
		return 0;
	hull = transport->stance == SG_RUNE_STANCE_STANDING ?
		&model->identity.standing_hull : &model->identity.crouching_hull;
	for (axis = 0U; axis < 3U; axis++)
		if (transport->source_player_local.value[axis] !=
				transport->destination_player_local.value[axis] ||
			transport->source_support_local.value[axis] !=
				transport->destination_support_local.value[axis])
			return 0;
	for (axis = 0U; axis < 2U; axis++)
		if (transport->source_player_local.value[axis] !=
				transport->source_support_local.value[axis] ||
			transport->destination_player_local.value[axis] !=
				transport->destination_support_local.value[axis])
			return 0;
	expected_player_z = (int64_t)transport->source_support_local.value[2] -
		(int64_t)hull->mins.value[2];
	if (expected_player_z < INT32_MIN || expected_player_z > INT32_MAX ||
		transport->source_player_local.value[2] != (int32_t)expected_player_z)
		return 0;
	expected_player_z = (int64_t)transport->destination_support_local.value[2] -
		(int64_t)hull->mins.value[2];
	if (expected_player_z < INT32_MIN || expected_player_z > INT32_MAX ||
		transport->destination_player_local.value[2] !=
			(int32_t)expected_player_z)
		return 0;
	if (!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&transport->source_player_local,
			transport->source_mover_origin_bits,
			transport->source_mover_axis_bits,
			expected_source_player_world) ||
		!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&transport->destination_player_local,
			transport->destination_mover_origin_bits,
			transport->destination_mover_axis_bits,
			expected_destination_player_world) ||
		!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&transport->source_support_local,
			transport->source_mover_origin_bits,
			transport->source_mover_axis_bits,
			expected_source_support_world) ||
		!SG_RuneCompactStaticTransportDeriveWorldPointBits(
			&transport->destination_support_local,
				transport->destination_mover_origin_bits,
				transport->destination_mover_axis_bits,
				expected_destination_support_world))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (transport->source_player_world_bits[axis] !=
				expected_source_player_world[axis] ||
			transport->destination_player_world_bits[axis] !=
				expected_destination_player_world[axis] ||
			transport->source_support_world_bits[axis] !=
				expected_source_support_world[axis] ||
			transport->destination_support_world_bits[axis] !=
				expected_destination_support_world[axis])
			return 0;
	return 1;
}

static int StaticTransitionKindValid(sg_rune_compact_static_transition_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE &&
		kind < SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT;
}

static int StaticMechanismStateValid(sg_rune_compact_mechanism_state_t state)
{
	return (uint32_t)state <
		(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT;
}

/* Every emitted transition is a projection of one authenticated mechanism
 * fact.  State pairs therefore retain the authority's exact state values;
 * merely requiring a change would allow a copied or forged transition to
 * select an unrelated state pair.  Stateless transport facts are explicitly
 * active-to-active, while an automatically running train has no activation
 * edge to project. */
static int StaticTransitionStatesValid(
	const sg_rune_compact_mechanism_t *mechanism,
	sg_rune_compact_static_transition_kind_t kind,
	sg_rune_compact_mechanism_state_t source,
	sg_rune_compact_mechanism_state_t destination)
{
	int portal_states_valid;

	if (mechanism == NULL)
		return 0;
	switch (kind)
	{
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
		portal_states_valid = source == mechanism->initial_state &&
			destination == mechanism->activated_state;
		if (!portal_states_valid &&
			(mechanism->flags & SG_RUNE_COMPACT_MECHANISM_ONE_SHOT) == 0U &&
			source == mechanism->activated_state)
		{
			if (mechanism->initial_state != mechanism->activated_state &&
				mechanism->reset_state == mechanism->activated_state)
				portal_states_valid = destination == mechanism->initial_state;
			else if (mechanism->reset_state != mechanism->activated_state)
				portal_states_valid = destination == mechanism->reset_state;
		}
		return portal_states_valid;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
		return source == SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE &&
			destination == SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_LIFT)
			return source == mechanism->initial_state &&
				destination == mechanism->activated_state;
		if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRAIN)
		{
			if ((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U)
				return source == SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE &&
					destination == SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
			return source == mechanism->initial_state &&
				destination == mechanism->activated_state;
		}
		return 0;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

static int CompareU32Array(const uint32_t left[3], const uint32_t right[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		int comparison = CompareU32(left[axis], right[axis]);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int CompareU32Matrix(const uint32_t left[3][3],
	const uint32_t right[3][3])
{
	uint32_t row;

	for (row = 0U; row < 3U; row++)
	{
		int comparison = CompareU32Array(left[row], right[row]);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int CompareStaticTransitionValue(
	const sg_rune_compact_static_transition_t *left,
	const sg_rune_compact_static_transition_t *right)
{
	int comparison;

	if (left->kind != right->kind)
		return CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
		comparison = CompareU32(left->value.portal_state.mover_model,
			right->value.portal_state.mover_model);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.delay_ms,
				right->value.portal_state.delay_ms);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.dwell_ms,
				right->value.portal_state.dwell_ms);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.pause_ms,
				right->value.portal_state.pause_ms);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.travel_ms,
				right->value.portal_state.travel_ms);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.recovery_ms,
				right->value.portal_state.recovery_ms);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.source_blocked,
				right->value.portal_state.source_blocked);
		if (comparison == 0)
			comparison = CompareU32(
				left->value.portal_state.destination_blocked,
				right->value.portal_state.destination_blocked);
		if (comparison == 0)
			comparison = CompareU32(left->value.portal_state.reserved[0],
				right->value.portal_state.reserved[0]);
		return comparison == 0 ? CompareU32(left->value.portal_state.reserved[1],
			right->value.portal_state.reserved[1]) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
		comparison = CompareU32(left->value.teleport.destination.entity_ordinal,
			right->value.teleport.destination.entity_ordinal);
		if (comparison == 0)
			comparison = CompareU32(left->value.teleport.fanout_ordinal,
				right->value.teleport.fanout_ordinal);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.teleport.approach_witness,
				&right->value.teleport.approach_witness);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.teleport.entry_witness,
				&right->value.teleport.entry_witness);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.teleport.exit_witness,
				&right->value.teleport.exit_witness);
		return comparison == 0 ? CompareU32Array(
			left->value.teleport.arrival_velocity_bits,
			right->value.teleport.arrival_velocity_bits) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
		comparison = CompareQ8Vec3(&left->value.push.approach_witness,
			&right->value.push.approach_witness);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.push.entry_witness,
				&right->value.push.entry_witness);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.push.exit_witness,
				&right->value.push.exit_witness);
		if (comparison == 0)
			comparison = CompareU32Array(left->value.push.launch_velocity_bits,
				right->value.push.launch_velocity_bits);
		if (comparison == 0)
			comparison = CompareU32(left->value.push.gravity_bits,
				right->value.push.gravity_bits);
		return comparison == 0 ? CompareU32(left->value.push.flight_ms,
			right->value.push.flight_ms) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		comparison = CompareU32(left->value.transport.mover_model,
			right->value.transport.mover_model);
		if (comparison == 0)
			comparison = CompareU32(left->value.transport.source_surface_ordinal,
				right->value.transport.source_surface_ordinal);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.transport.source_player_local,
				&right->value.transport.source_player_local);
		if (comparison == 0)
			comparison = CompareQ8Vec3(
				&left->value.transport.destination_player_local,
				&right->value.transport.destination_player_local);
		if (comparison == 0)
			comparison = CompareQ8Vec3(&left->value.transport.source_support_local,
				&right->value.transport.source_support_local);
		if (comparison == 0)
			comparison = CompareQ8Vec3(
				&left->value.transport.destination_support_local,
				&right->value.transport.destination_support_local);
		if (comparison == 0)
			comparison = CompareU32Array(
				left->value.transport.source_player_world_bits,
				right->value.transport.source_player_world_bits);
		if (comparison == 0)
			comparison = CompareU32Array(
				left->value.transport.destination_player_world_bits,
				right->value.transport.destination_player_world_bits);
		if (comparison == 0)
			comparison = CompareU32Array(
				left->value.transport.source_support_world_bits,
				right->value.transport.source_support_world_bits);
		if (comparison == 0)
			comparison = CompareU32Array(
				left->value.transport.destination_support_world_bits,
				right->value.transport.destination_support_world_bits);
		if (comparison == 0)
			comparison = CompareU32Array(
				left->value.transport.source_mover_origin_bits,
				right->value.transport.source_mover_origin_bits);
		if (comparison == 0)
			comparison = CompareU32Matrix(
				left->value.transport.source_mover_axis_bits,
				right->value.transport.source_mover_axis_bits);
		if (comparison == 0)
			comparison = CompareU32Array(
				left->value.transport.destination_mover_origin_bits,
				right->value.transport.destination_mover_origin_bits);
		if (comparison == 0)
			comparison = CompareU32Matrix(
				left->value.transport.destination_mover_axis_bits,
				right->value.transport.destination_mover_axis_bits);
		if (comparison == 0)
			comparison = CompareU32(
				left->value.transport.source_endpoint.entity_ordinal,
				right->value.transport.source_endpoint.entity_ordinal);
		if (comparison == 0)
			comparison = CompareU32(
				left->value.transport.destination_endpoint.entity_ordinal,
				right->value.transport.destination_endpoint.entity_ordinal);
		if (comparison == 0)
			comparison = CompareU32(left->value.transport.fanout_ordinal,
				right->value.transport.fanout_ordinal);
		if (comparison == 0)
			comparison = CompareU32(left->value.transport.swept_static_clear,
				right->value.transport.swept_static_clear);
		if (comparison == 0)
			comparison = CompareU32(left->value.transport.start_supported,
				right->value.transport.start_supported);
		if (comparison == 0)
			comparison = CompareU32(left->value.transport.end_supported,
				right->value.transport.end_supported);
		return comparison == 0 ? CompareU32(left->value.transport.stance,
			right->value.transport.stance) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
		break;
	}
	/* The caller validates tags before comparing records. */
	return 0;
}

static int LandmarkKindValid(sg_rune_compact_landmark_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_LANDMARK_SPAWN &&
		kind < SG_RUNE_COMPACT_LANDMARK_KIND_COUNT;
}

static int PortalMechanismKindValid(sg_rune_compact_portal_mechanism_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS &&
		kind < SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT;
}

static int PortalMechanismMatchesSource(
	sg_rune_compact_portal_mechanism_kind_t binding_kind,
	const sg_rune_compact_mechanism_t *mechanism)
{
	if (mechanism == NULL)
		return 0;
	switch (binding_kind) {
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS:
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_DOOR ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_BUTTON ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_LIFT ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRAIN ||
			(mechanism->kind == SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
				(mechanism->flags &
					SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U);
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES:
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_LIFT ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRAIN;
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_TELEPORTS:
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_LAUNCHES:
		return 0;
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT:
		break;
	}
	return 0;
}

static int EntityRefValid(sg_rune_compact_entity_ref_t source,
	uint32_t entity_count)
{
	return source.entity_ordinal < entity_count;
}

static uint32_t MechanismControllerKey(
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanism_t *mechanism)
{
	if (mechanism->controllers.count == 0U)
		return SG_RUNE_COMPACT_INDEX_NONE;
	return static_data->mechanism_controllers[
		mechanism->controllers.first].controller.entity_ordinal;
}

static int MechanismCompare(const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanism_t *left,
	const sg_rune_compact_mechanism_t *right)
{
	int comparison = CompareU32(left->source.entity_ordinal,
		right->source.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32(MechanismControllerKey(static_data, left),
			MechanismControllerKey(static_data, right));
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32(left->entry_cell.value,
			right->entry_cell.value);
	if (comparison == 0)
		comparison = CompareU32(left->exit_cell.value, right->exit_cell.value);
	if (comparison == 0)
		comparison = CompareU32(left->transition_destination.entity_ordinal,
			right->transition_destination.entity_ordinal);
	if (comparison == 0)
		comparison = CompareU32(left->transition_fanout_ordinal,
			right->transition_fanout_ordinal);
	return comparison;
}

static int MechanismControllerCompare(
	const sg_rune_compact_static_mechanism_controller_t *left,
	const sg_rune_compact_static_mechanism_controller_t *right)
{
	int comparison = CompareU32(left->controller.entity_ordinal,
		right->controller.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32(left->topology_edge, right->topology_edge);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->spatiality,
			(uint32_t)right->spatiality);
	if (comparison == 0)
		comparison = CompareU32(left->activation_cell.value,
			right->activation_cell.value);
	if (comparison == 0)
		comparison = CompareQ8Vec3(&left->activation_witness,
			&right->activation_witness);
	if (comparison == 0)
		comparison = CompareQ8Bounds(&left->activation_bounds,
			&right->activation_bounds);
	return comparison;
}

int SG_RuneCompactStaticTransitionCompareCanonical(
	const sg_rune_compact_static_transition_t *left,
	const sg_rune_compact_static_transition_t *right)
{
	int comparison = CompareU32(left->mechanism.value, right->mechanism.value);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	/* Portal-state records are grouped by their exact portal within each
	 * mechanism.  Keep that key ahead of the common witnesses/timing so the
	 * mechanism-major transition spans can be merged linearly with the
	 * mechanism-major portal binding table. */
	if (comparison == 0 &&
		left->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE)
		comparison = CompareU32(left->value.portal_state.portal.value,
			right->value.portal_state.portal.value);
	if (comparison == 0)
		comparison = CompareU32(left->entry_cell.value, right->entry_cell.value);
	if (comparison == 0)
		comparison = CompareU32(left->exit_cell.value, right->exit_cell.value);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->source_state,
			(uint32_t)right->source_state);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->destination_state,
			(uint32_t)right->destination_state);
	if (comparison == 0)
		comparison = left->elapsed_ms < right->elapsed_ms ? -1 :
			left->elapsed_ms > right->elapsed_ms ? 1 : 0;
	if (comparison == 0)
		comparison = CompareStaticTransitionValue(left, right);
	return comparison;
}

static int PortalTransitionCellsMatch(const sg_rune_compact_model_t *model,
	sg_rune_compact_portal_index_t portal_index, uint32_t entry_cell,
	uint32_t exit_cell)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_incidence_t *negative;
	const sg_rune_compact_incidence_t *positive;

	if (model == NULL || portal_index.value >= model->portal_count ||
		model->portals == NULL || model->incidences == NULL)
		return 0;
	portal = &model->portals[portal_index.value];
	if (portal->negative_incidence.value >= model->incidence_count ||
		portal->positive_incidence.value >= model->incidence_count)
		return 0;
	negative = &model->incidences[portal->negative_incidence.value];
	positive = &model->incidences[portal->positive_incidence.value];
	if (negative->cell.value >= model->cell_count ||
		positive->cell.value >= model->cell_count ||
		negative->cell.value == positive->cell.value)
		return 0;
	switch (portal->direction)
	{
	case SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE:
		return entry_cell == negative->cell.value &&
			exit_cell == positive->cell.value;
	case SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE:
		return entry_cell == positive->cell.value &&
			exit_cell == negative->cell.value;
	case SG_RUNE_PORTAL_CONTINUITY_BOTH:
		/* The compact transition records one canonical direction.  Runtime
		 * traversal may reverse a BOTH portal without duplicating static facts. */
		return entry_cell == negative->cell.value &&
			exit_cell == positive->cell.value;
	case SG_RUNE_PORTAL_CONTINUITY_COUNT:
		break;
	}
	return 0;
}

static int StaticTransitionWitnessesValid(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_transition_t *transition)
{
	if (model == NULL || transition == NULL)
		return 0;
	switch (transition->kind)
	{
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
		return StaticCellContainsPoint(model, transition->entry_cell.value,
			&transition->value.teleport.approach_witness) &&
			StaticCellContainsPoint(model, transition->entry_cell.value,
				&transition->value.teleport.entry_witness) &&
			StaticCellContainsPoint(model, transition->exit_cell.value,
				&transition->value.teleport.exit_witness);
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
		return StaticCellContainsPoint(model, transition->entry_cell.value,
			&transition->value.push.approach_witness) &&
			StaticCellContainsPoint(model, transition->entry_cell.value,
				&transition->value.push.entry_witness) &&
			StaticCellContainsPoint(model, transition->exit_cell.value,
				&transition->value.push.exit_witness);
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
	case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
		return 1;
	}
	return 0;
}

static int StaticTrainEndpointFactValid(
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanism_t *mechanism,
	const sg_rune_compact_static_transport_t *transport)
{
	uint32_t local;

	if (static_data == NULL || mechanism == NULL || transport == NULL ||
		static_data->mechanism_edges == NULL ||
		mechanism->topology.first > static_data->mechanism_edge_count ||
		mechanism->topology.count > static_data->mechanism_edge_count -
			mechanism->topology.first)
		return 0;
	for (local = 0U; local < mechanism->topology.count; local++)
	{
		const sg_rune_compact_mechanism_edge_t *edge =
			&static_data->mechanism_edges[mechanism->topology.first + local];

		/* Train endpoints are the exact path-corner TARGET fact.  Other
		 * controller/ownership relations with matching endpoints do not prove
		 * train traversal and must not satisfy this join. */
		if (edge->kind == SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET &&
			edge->source.entity_ordinal ==
				transport->source_endpoint.entity_ordinal &&
			edge->destination.entity_ordinal ==
				transport->destination_endpoint.entity_ordinal &&
			edge->fanout_ordinal == transport->fanout_ordinal)
			return 1;
	}
	return 0;
}

static int StaticTeleportTargetFactValid(
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanism_t *mechanism,
	const sg_rune_compact_static_teleport_t *teleport)
{
	uint32_t local;

	if (static_data == NULL || mechanism == NULL || teleport == NULL ||
		static_data->mechanism_edges == NULL ||
		mechanism->topology.first > static_data->mechanism_edge_count ||
		mechanism->topology.count > static_data->mechanism_edge_count -
			mechanism->topology.first ||
		teleport->destination.entity_ordinal == SG_RUNE_COMPACT_INDEX_NONE ||
		teleport->fanout_ordinal == UINT32_MAX)
		return 0;
	for (local = 0U; local < mechanism->topology.count; local++)
	{
		const sg_rune_compact_mechanism_edge_t *edge =
			&static_data->mechanism_edges[mechanism->topology.first + local];

		if (edge->kind == SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET &&
			edge->source.entity_ordinal == mechanism->source.entity_ordinal &&
			edge->destination.entity_ordinal ==
				teleport->destination.entity_ordinal &&
			edge->fanout_ordinal == teleport->fanout_ordinal)
			return 1;
	}
	return 0;
}

static int MechanismEdgeCompare(const sg_rune_compact_mechanism_edge_t *left,
	const sg_rune_compact_mechanism_edge_t *right)
{
	int comparison = CompareU32(left->source.entity_ordinal,
		right->source.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32(left->destination.entity_ordinal,
			right->destination.entity_ordinal);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32(left->fanout_ordinal, right->fanout_ordinal);
	return comparison;
}

static int LandmarkCompare(const sg_rune_compact_landmark_t *left,
	const sg_rune_compact_landmark_t *right)
{
	int comparison = CompareU32(left->source.entity_ordinal,
		right->source.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->variant,
			(uint32_t)right->variant);
	return comparison;
}

static int PortalMechanismCompare(
	const sg_rune_compact_portal_mechanism_t *left,
	const sg_rune_compact_portal_mechanism_t *right)
{
	int comparison = CompareU32(left->mechanism.value, right->mechanism.value);

	if (comparison == 0)
		comparison = CompareU32(left->portal.value, right->portal.value);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind,
			(uint32_t)right->kind);
	return comparison;
}

static int ModelReferencesPresent(const sg_rune_compact_model_t *model)
{
	return model != NULL && model->cell_count != 0U && model->cells != NULL &&
		(model->facet_count == 0U || model->facets != NULL) &&
		(model->portal_count == 0U || model->portals != NULL) &&
		(model->incidence_count == 0U || model->incidences != NULL);
}

static int CountsValid(const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	if (static_data->mechanism_count > SG_RUNE_COMPACT_MAX_MECHANISMS ||
		static_data->mechanism_controller_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS ||
		static_data->mechanism_edge_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ||
		static_data->transition_count > SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS ||
		static_data->landmark_count > SG_RUNE_COMPACT_MAX_LANDMARKS ||
		static_data->landmark_cell_count >
			SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS ||
		static_data->facet_annotation_count >
			SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS ||
		static_data->portal_mechanism_count >
			SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_STATIC_RECORD_MODEL, 0U);
		return 0;
	}
	if (!ArrayPresent(static_data->mechanisms,
			static_data->mechanism_count) ||
		!ArrayPresent(static_data->mechanism_controllers,
			static_data->mechanism_controller_count) ||
		!ArrayPresent(static_data->mechanism_edges,
			static_data->mechanism_edge_count) ||
		!ArrayPresent(static_data->transitions,
			static_data->transition_count) ||
		!ArrayPresent(static_data->landmarks, static_data->landmark_count) ||
		!ArrayPresent(static_data->landmark_cells,
			static_data->landmark_cell_count) ||
		!ArrayPresent(static_data->facet_annotations,
			static_data->facet_annotation_count) ||
		!ArrayPresent(static_data->portal_mechanisms,
			static_data->portal_mechanism_count)) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidateMechanisms(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;
	uint32_t edge_cursor = 0U;

	for (index = 0U; index < static_data->mechanism_count; index++) {
		const sg_rune_compact_mechanism_t *mechanism =
			&static_data->mechanisms[index];

		if (mechanism->reserved[0] != 0U || mechanism->reserved[1] != 0U ||
			mechanism->reserved[2] != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (!EntityRefValid(mechanism->source,
				model->identity.source_counts.entity_count) ||
			!StaticActivationMaskValid(mechanism->activation_mask) ||
			mechanism->controllers.first >
				static_data->mechanism_controller_count ||
			mechanism->controllers.count >
				static_data->mechanism_controller_count -
				mechanism->controllers.first ||
			mechanism->transitions.first > static_data->transition_count ||
			mechanism->transitions.count > static_data->transition_count -
				mechanism->transitions.first ||
			mechanism->entry_cell.value >= model->cell_count ||
			mechanism->exit_cell.value >= model->cell_count ||
			(mechanism->activation_landmark.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 mechanism->activation_landmark.value >= static_data->landmark_count) ||
			!MechanismKindValid(mechanism->kind) ||
			(uint32_t)mechanism->initial_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->activated_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->reset_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->recovery >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_RECOVERY_COUNT ||
			((mechanism->activation_mask &
				(sg_rune_compact_static_activation_mask_t)
				~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U &&
			 mechanism->activation_landmark.value ==
				SG_RUNE_COMPACT_INDEX_NONE) ||
			((mechanism->activation_mask &
				(sg_rune_compact_static_activation_mask_t)
				~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) == 0U &&
			 mechanism->activation_landmark.value !=
				SG_RUNE_COMPACT_INDEX_NONE) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE) != 0U &&
			 mechanism->health <= 0) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE) == 0U &&
			 mechanism->health != 0) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY) != 0U &&
			 mechanism->required_item == SG_BSP_ENTITY_STRING_NONE) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY) == 0U &&
			 mechanism->required_item != SG_BSP_ENTITY_STRING_NONE) ||
			(mechanism->flags & (sg_rune_compact_mechanism_flags_t)
				~SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN) != 0U ||
			((mechanism->flags &
				SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U &&
				(mechanism->kind != SG_RUNE_COMPACT_MECHANISM_ROTATOR ||
				 (mechanism->flags &
					SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE) == 0U))) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (mechanism->topology.first != edge_cursor ||
			mechanism->topology.first > static_data->mechanism_edge_count ||
			mechanism->topology.count >
				static_data->mechanism_edge_count - mechanism->topology.first) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if ((mechanism->recovery ==
				SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET) !=
			(mechanism->reset_ms != 0U) ||
			((mechanism->flags & SG_RUNE_COMPACT_MECHANISM_ONE_SHOT) != 0U &&
			 (mechanism->reset_ms != 0U ||
			  mechanism->recovery != SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE))) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (!BoundsValid(&mechanism->bounds)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (index != 0U && MechanismCompare(static_data,
			&static_data->mechanisms[index - 1U], mechanism) >= 0) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		edge_cursor += mechanism->topology.count;
	}
	if (edge_cursor != static_data->mechanism_edge_count) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE, edge_cursor);
		return 0;
	}
	return 1;
}

static int ValidateMechanismControllers(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t mechanism_index;
	uint32_t cursor = 0U;

	for (mechanism_index = 0U;
		mechanism_index < static_data->mechanism_count; mechanism_index++)
	{
		const sg_rune_compact_mechanism_t *mechanism =
			&static_data->mechanisms[mechanism_index];
		const sg_rune_compact_mechanism_controller_span_t span =
			mechanism->controllers;
		uint32_t index;

		if (span.first != cursor || span.first >
			static_data->mechanism_controller_count || span.count >
			static_data->mechanism_controller_count - span.first)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_CONTROLLER,
				mechanism_index);
			return 0;
		}
		for (index = 0U; index < span.count; index++)
		{
			const uint32_t record = span.first + index;
			const sg_rune_compact_static_mechanism_controller_t *controller =
				&static_data->mechanism_controllers[record];
			int topology_provenance_valid =
				controller->topology_edge == SG_RUNE_COMPACT_INDEX_NONE;

			if (!topology_provenance_valid &&
				controller->topology_edge >= mechanism->topology.first &&
				controller->topology_edge - mechanism->topology.first <
					mechanism->topology.count &&
				controller->topology_edge < static_data->mechanism_edge_count)
			{
				const sg_rune_compact_mechanism_edge_t *edge =
					&static_data->mechanism_edges[controller->topology_edge];

				topology_provenance_valid =
					edge->source.entity_ordinal ==
						controller->controller.entity_ordinal;
			}

			if (!EntityRefValid(controller->controller,
				model->identity.source_counts.entity_count) ||
				!topology_provenance_valid ||
				controller->spatiality >=
					SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
				controller->reserved[0] != 0U ||
				controller->reserved[1] != 0U ||
				controller->reserved[2] != 0U ||
				(controller->spatiality ==
					SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
				 (controller->activation_cell.value !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				  !ControllerLocationZero(controller)) :
				 (controller->activation_cell.value >= model->cell_count ||
				  !BoundsValid(&controller->activation_bounds) ||
				  !PointInBounds(&controller->activation_witness,
					&controller->activation_bounds) ||
				  !PointInBounds(&controller->activation_witness,
					&model->cells[controller->activation_cell.value].bounds))) ||
				(index != 0U &&
					MechanismControllerCompare(
						&static_data->mechanism_controllers[record - 1U],
						controller) >= 0))
			{
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_CONTROLLER, record);
				return 0;
			}
		}
		cursor += span.count;
	}
	if (cursor != static_data->mechanism_controller_count)
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_CONTROLLER, cursor);
		return 0;
	}
	return 1;
}

static int ValidateTransitions(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t mechanism_index;
	uint32_t cursor = 0U;

	for (mechanism_index = 0U;
		mechanism_index < static_data->mechanism_count; mechanism_index++)
	{
		const sg_rune_compact_mechanism_t *mechanism =
			&static_data->mechanisms[mechanism_index];
		const sg_rune_compact_mechanism_transition_span_t span =
			mechanism->transitions;
		uint32_t index;

		if (span.first != cursor || span.first > static_data->transition_count ||
			span.count > static_data->transition_count - span.first)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, mechanism_index);
			return 0;
		}
		for (index = 0U; index < span.count; index++)
		{
			const uint32_t record = span.first + index;
			const sg_rune_compact_static_transition_t *transition =
				&static_data->transitions[record];
			const sg_rune_compact_mechanism_t *owner =
				&static_data->mechanisms[mechanism_index];
			int valid = transition->mechanism.value == mechanism_index &&
				StaticTransitionKindValid(transition->kind) &&
				transition->entry_cell.value < model->cell_count &&
				transition->exit_cell.value < model->cell_count &&
				StaticMechanismStateValid(transition->source_state) &&
				StaticMechanismStateValid(transition->destination_state) &&
				StaticTransitionInactiveTailZero(transition);

			if (!valid)
			{
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, record);
				return 0;
			}
			switch (transition->kind)
			{
			case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
			{
				const sg_rune_compact_static_portal_state_t *portal =
					&transition->value.portal_state;

				valid = (owner->kind == SG_RUNE_COMPACT_MECHANISM_DOOR ||
					owner->kind == SG_RUNE_COMPACT_MECHANISM_BUTTON ||
					(owner->kind == SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
						(owner->flags &
							SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U)) &&
					portal->portal.value < model->portal_count &&
					PortalTransitionCellsMatch(model, portal->portal,
						transition->entry_cell.value, transition->exit_cell.value) &&
					portal->mover_model != SG_BSP_ENTITY_MODEL_NONE &&
					portal->mover_model <
						model->identity.source_counts.model_count &&
					transition->elapsed_ms != 0U &&
					StaticTransitionStatesValid(owner, transition->kind,
						transition->source_state,
						transition->destination_state) &&
					portal->delay_ms == owner->delay_ms &&
					portal->dwell_ms == owner->dwell_ms &&
					portal->pause_ms == owner->wait_ms &&
					portal->travel_ms == owner->travel_ms &&
					portal->recovery_ms == owner->reset_ms &&
					portal->source_blocked <= 1U &&
					portal->destination_blocked <= 1U &&
					portal->source_blocked != portal->destination_blocked &&
					portal->reserved[0] == 0U &&
					portal->reserved[1] == 0U;
			}
				break;
			case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
			{
				const sg_rune_compact_static_teleport_t *teleport =
					&transition->value.teleport;
				uint32_t axis;

				valid = owner->kind == SG_RUNE_COMPACT_MECHANISM_TELEPORT &&
					StaticTransitionStatesValid(owner, transition->kind,
						transition->source_state,
						transition->destination_state) &&
					EntityRefValid(teleport->destination,
					model->identity.source_counts.entity_count) &&
					teleport->fanout_ordinal != UINT32_MAX &&
					StaticTeleportTargetFactValid(static_data, owner, teleport) &&
					transition->elapsed_ms == 0U &&
					StaticTransitionWitnessesValid(model, transition);
				for (axis = 0U; axis < 3U; axis++)
					valid = valid && teleport->arrival_velocity_bits[axis] == 0U;
			}
				break;
			case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
			{
				const sg_rune_compact_static_push_t *push =
					&transition->value.push;
				uint32_t axis;

				valid = owner->kind == SG_RUNE_COMPACT_MECHANISM_PUSH &&
					StaticTransitionStatesValid(owner, transition->kind,
						transition->source_state,
						transition->destination_state) &&
					transition->elapsed_ms == (uint64_t)push->flight_ms &&
					push->flight_ms != 0U &&
					Binary32CanonicalNonnegative(push->gravity_bits) &&
					StaticTransitionWitnessesValid(model, transition);
				for (axis = 0U; axis < 3U; axis++)
					valid = valid && TransitionLaunchBinary32Finite(
						push->launch_velocity_bits[axis]);
			}
				break;
			case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
			{
				const sg_rune_compact_static_transport_t *transport =
					&transition->value.transport;
				const sg_rune_compact_source_surface_t *surface = NULL;
				const int is_lift = owner->kind == SG_RUNE_COMPACT_MECHANISM_LIFT;
				const int is_train = owner->kind == SG_RUNE_COMPACT_MECHANISM_TRAIN;
				uint32_t axis;

				if (model->source_surfaces != NULL &&
					transport->source_surface_ordinal <
						model->source_surface_count)
					surface = &model->source_surfaces[
						transport->source_surface_ordinal];
				valid = (is_lift || is_train) &&
					transport->mover_model != SG_BSP_ENTITY_MODEL_NONE &&
					transport->mover_model <
						model->identity.source_counts.model_count &&
					transport->source_surface_ordinal !=
						SG_RUNE_COMPACT_INDEX_NONE && surface != NULL &&
					surface->frame == SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL &&
					surface->cell.value == SG_RUNE_COMPACT_INDEX_NONE &&
					surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE &&
					surface->split_ordinal == 0U &&
					surface->source.model == transport->mover_model &&
					transition->elapsed_ms != 0U &&
					transport->swept_static_clear == 1U &&
					transport->start_supported == 1U &&
					transport->end_supported == 1U;
				if (is_lift)
					valid = valid && transport->fanout_ordinal == UINT32_MAX &&
						transport->source_endpoint.entity_ordinal ==
							SG_RUNE_COMPACT_INDEX_NONE &&
						transport->destination_endpoint.entity_ordinal ==
							SG_RUNE_COMPACT_INDEX_NONE &&
						StaticTransitionStatesValid(owner, transition->kind,
							transition->source_state,
							transition->destination_state);
				else if (is_train)
					valid = valid && transport->fanout_ordinal != UINT32_MAX &&
						EntityRefValid(transport->source_endpoint,
							model->identity.source_counts.entity_count) &&
						EntityRefValid(transport->destination_endpoint,
							model->identity.source_counts.entity_count) &&
						transport->source_endpoint.entity_ordinal !=
							transport->destination_endpoint.entity_ordinal &&
						StaticTransitionStatesValid(owner, transition->kind,
							transition->source_state,
							transition->destination_state);
				if (is_train)
					valid = valid && StaticTrainEndpointFactValid(static_data,
						owner, transport);
				for (axis = 0U; axis < 3U; axis++)
					valid = valid && Binary32Finite(
						transport->source_player_world_bits[axis]) &&
						Binary32Finite(
							transport->destination_player_world_bits[axis]) &&
						Binary32Finite(
							transport->source_support_world_bits[axis]) &&
						Binary32Finite(
							transport->destination_support_world_bits[axis]);
				valid = valid &&
					StaticSourceSurfaceSupportsPoint(model,
						transport->source_surface_ordinal,
						transport->mover_model,
						&transport->source_support_local) &&
					StaticSourceSurfaceSupportsPoint(model,
						transport->source_surface_ordinal,
						transport->mover_model,
						&transport->destination_support_local) &&
					StaticCellContainsWorldPoint(model,
						transition->entry_cell.value,
						transport->source_player_world_bits) &&
					StaticCellContainsWorldPoint(model,
						transition->entry_cell.value,
						transport->source_support_world_bits) &&
					StaticCellContainsWorldPoint(model,
						transition->exit_cell.value,
						transport->destination_player_world_bits) &&
					StaticCellContainsWorldPoint(model,
						transition->exit_cell.value,
						transport->destination_support_world_bits);
				valid = valid && StaticTransportPoseValid(model, transport);
				valid = valid && transport->stance < SG_RUNE_STANCE_COUNT;
			}
				break;
			case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
				valid = 0;
				break;
			}
			if (!valid)
			{
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, record);
				return 0;
			}
			if (record != 0U && SG_RuneCompactStaticTransitionCompareCanonical(
					&static_data->transitions[record - 1U], transition) >= 0)
			{
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
					SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, record);
				return 0;
			}
		}
		if (span.count != 1U &&
			(mechanism->transition_destination.entity_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE ||
			mechanism->transition_fanout_ordinal != UINT32_MAX ||
			mechanism->gravity_bits != 0U || mechanism->flight_ms != 0U ||
			mechanism->launch_velocity_bits[0] != 0U ||
			mechanism->launch_velocity_bits[1] != 0U ||
			mechanism->launch_velocity_bits[2] != 0U))
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, mechanism_index);
			return 0;
		}
		if (span.count == 1U)
		{
			const sg_rune_compact_static_transition_t *transition =
				&static_data->transitions[span.first];
			uint32_t axis;

			uint32_t expected_destination = SG_RUNE_COMPACT_INDEX_NONE;
			uint32_t expected_fanout = UINT32_MAX;
			uint32_t expected_gravity = 0U;
			uint32_t expected_flight = 0U;
			uint32_t expected_launch[3] = { 0U, 0U, 0U };

				switch (transition->kind)
				{
				case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
					expected_destination = transition->value.teleport.destination.entity_ordinal;
					expected_fanout = transition->value.teleport.fanout_ordinal;
				break;
			case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
				expected_gravity = transition->value.push.gravity_bits;
				expected_flight = transition->value.push.flight_ms;
				expected_launch[0] = transition->value.push.launch_velocity_bits[0];
				expected_launch[1] = transition->value.push.launch_velocity_bits[1];
				expected_launch[2] = transition->value.push.launch_velocity_bits[2];
				break;
				case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
				case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
					/* Train fanout is a mechanism-level convenience fact.  A lift
					 * deliberately keeps the absent sentinel because its transport
					 * has no path-corner target fanout. */
					if (transition->kind ==
						SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT &&
						mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRAIN)
						expected_fanout =
							transition->value.transport.fanout_ordinal;
					break;
				case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
					break;
				}
			if (mechanism->transition_destination.entity_ordinal !=
				expected_destination ||
				mechanism->transition_fanout_ordinal != expected_fanout ||
				mechanism->entry_cell.value != transition->entry_cell.value ||
				mechanism->exit_cell.value != transition->exit_cell.value ||
				mechanism->gravity_bits != expected_gravity ||
				mechanism->flight_ms != expected_flight)
			{
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, mechanism_index);
				return 0;
			}
			for (axis = 0U; axis < 3U; axis++)
				if (mechanism->launch_velocity_bits[axis] !=
					expected_launch[axis])
				{
					SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
						SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, mechanism_index);
					return 0;
				}
		}
		cursor += span.count;
	}
	if (cursor != static_data->transition_count)
	{
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, cursor);
		return 0;
	}
	return 1;
}

static int ValidateMechanismEdges(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t mechanism_index;

	for (mechanism_index = 0U; mechanism_index < static_data->mechanism_count;
		mechanism_index++) {
		const sg_rune_compact_mechanism_edge_span_t span =
			static_data->mechanisms[mechanism_index].topology;
		uint32_t index;

		for (index = span.first; index < span.first + span.count; index++) {
			const sg_rune_compact_mechanism_edge_t *edge =
				&static_data->mechanism_edges[index];

			if (!EntityRefValid(edge->source,
					model->identity.source_counts.entity_count) ||
				!EntityRefValid(edge->destination,
					model->identity.source_counts.entity_count) ||
				(uint32_t)edge->kind >=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT) {
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE, index);
				return 0;
			}
			if (index != span.first && MechanismEdgeCompare(
					&static_data->mechanism_edges[index - 1U], edge) > 0) {
				SetError(error,
					SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
					SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE, index);
				return 0;
			}
		}
	}
	return 1;
}

static int LandmarkMechanismValid(const sg_rune_compact_landmark_t *landmark,
	const sg_rune_compact_static_t *static_data)
{
	const sg_rune_compact_mechanism_t *mechanism;

	if (landmark->mechanism.value == SG_RUNE_COMPACT_INDEX_NONE)
		return landmark->kind != SG_RUNE_COMPACT_LANDMARK_BUTTON &&
			landmark->kind != SG_RUNE_COMPACT_LANDMARK_TRIGGER &&
			landmark->kind != SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
	if (landmark->mechanism.value >= static_data->mechanism_count)
		return 0;
	mechanism = &static_data->mechanisms[landmark->mechanism.value];
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_BUTTON)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_BUTTON &&
			landmark->source.entity_ordinal == mechanism->source.entity_ordinal;
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_TRIGGER)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRIGGER &&
			landmark->source.entity_ordinal == mechanism->source.entity_ordinal;
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TELEPORT;
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_PUSH;
	return 1;
}

static int ValidateLandmarks(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;
	uint32_t cell_cursor = 0U;

	for (index = 0U; index < static_data->landmark_count; index++) {
		const sg_rune_compact_landmark_t *landmark =
			&static_data->landmarks[index];

		if (landmark->reserved != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		if (!EntityRefValid(landmark->source,
				model->identity.source_counts.entity_count) ||
			landmark->cells.first != cell_cursor || landmark->cells.count == 0U ||
			landmark->cells.first > static_data->landmark_cell_count ||
			landmark->cells.count >
				static_data->landmark_cell_count - landmark->cells.first ||
			!LandmarkKindValid(landmark->kind) ||
			!LandmarkMechanismValid(landmark, static_data)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		if (!BoundsValid(&landmark->bounds) ||
			!PointInBounds(&landmark->origin, &landmark->bounds)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		{
			uint32_t offset;
			int origin_owned = 0;

			for (offset = 0U; offset < landmark->cells.count; offset++) {
				const uint32_t reference = landmark->cells.first + offset;
				const uint32_t cell = static_data->landmark_cells[reference].value;

				if (cell >= model->cell_count ||
					(offset != 0U && static_data->landmark_cells[
						reference - 1U].value >= cell) ||
					!BoundsOverlap(&landmark->bounds, &model->cells[cell].bounds)) {
					SetError(error,
						SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
					return 0;
				}
				if (PointInBounds(&landmark->origin, &model->cells[cell].bounds))
					origin_owned = 1;
			}
			if (!origin_owned) {
				SetError(error,
					SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
				return 0;
			}
		}
		if (index != 0U && LandmarkCompare(&static_data->landmarks[index - 1U],
			landmark) >= 0) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		cell_cursor += landmark->cells.count;
	}
	if (cell_cursor != static_data->landmark_cell_count) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, cell_cursor);
		return 0;
	}
	return 1;
}

static int SourceSurfaceFacetIdentityValid(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_facet_t *facet,
	const sg_rune_compact_source_surface_t *surface)
{
	const sg_rune_compact_brush_side_source_t *source;

	if (model == NULL || facet == NULL || surface == NULL ||
		surface->parent_surface != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ||
		surface->source.model != SG_HOST_COLLISION_MODEL_WORLD ||
		facet->kind != SG_RUNE_COMPACT_FACET_POLYGON ||
		facet->source.kind != SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE)
		return 0;
	source = &facet->source.value.brush_side;
	return source->model == surface->source.model &&
		source->brush == surface->source.brush &&
		source->brush_side == surface->source.brush_side &&
		source->plane == surface->source.plane;
}

static int FacetAnnotationSourceValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_facet_annotation_t *annotation)
{
	const sg_rune_compact_source_surface_t *surface;

	if (model == NULL || annotation == NULL ||
		(uint32_t)annotation->source_frame >=
			(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT)
		return 0;
	if (annotation->source_surface == SG_RUNE_COMPACT_INDEX_NONE)
		return annotation->source_frame == SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	if (model->source_surfaces == NULL || annotation->source_surface >=
		model->source_surface_count)
		return 0;
	surface = &model->source_surfaces[annotation->source_surface];
	return surface->frame == annotation->source_frame &&
		SourceSurfaceFacetIdentityValid(model,
			&model->facets[annotation->facet.value], surface);
}

static int ValidateFacetAnnotations(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < static_data->facet_annotation_count; index++) {
		const sg_rune_compact_facet_annotation_t *annotation =
			&static_data->facet_annotations[index];

		if (annotation->reserved != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if (annotation->facet.value >= model->facet_count ||
			annotation->attributes == 0U ||
			(annotation->attributes & (sg_rune_compact_facet_attributes_t)
				~SG_RUNE_COMPACT_FACET_ATTRIBUTES_KNOWN) != 0U ||
			!FacetAnnotationSourceValid(model, annotation) ||
			(annotation->hookable_stances &
				(sg_rune_stance_validity_t)~SG_RUNE_STANCE_VALID_ALL) != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if ((annotation->attributes & SG_RUNE_COMPACT_FACET_HOOKABLE) == 0U &&
			annotation->hookable_stances != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if ((annotation->attributes & SG_RUNE_COMPACT_FACET_HOOKABLE) != 0U &&
			annotation->hookable_stances == 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if ((annotation->attributes & (SG_RUNE_COMPACT_FACET_HOOKABLE |
			SG_RUNE_COMPACT_FACET_SKY)) ==
			(SG_RUNE_COMPACT_FACET_HOOKABLE | SG_RUNE_COMPACT_FACET_SKY)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if (index != 0U && static_data->facet_annotations[index - 1U].facet.value >=
			annotation->facet.value) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
	}
	return 1;
}

static int ValidatePortalMechanisms(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < static_data->portal_mechanism_count; index++) {
		const sg_rune_compact_portal_mechanism_t *portal_mechanism =
			&static_data->portal_mechanisms[index];

		if (portal_mechanism->reserved[0] != 0U ||
			portal_mechanism->reserved[1] != 0U ||
			portal_mechanism->reserved[2] != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, index);
			return 0;
		}
		if (portal_mechanism->portal.value >= model->portal_count ||
			portal_mechanism->mechanism.value >= static_data->mechanism_count ||
			!PortalMechanismKindValid(portal_mechanism->kind) ||
			!PortalMechanismMatchesSource(portal_mechanism->kind,
				&static_data->mechanisms[
					portal_mechanism->mechanism.value])) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, index);
			return 0;
		}
		if (index != 0U && PortalMechanismCompare(
				&static_data->portal_mechanisms[index - 1U],
				portal_mechanism) >= 0) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, index);
			return 0;
		}
	}
	return 1;
}

/* A portal state is one canonical grouped fact.  The transition table and
 * the portal-mechanism table are two projections of that same fact.  Both are
 * mechanism-major, so a linear merge can validate the exact
 * (mechanism, portal) relation while allowing independent roots to share one
 * portal. */
static int NextPortalTransitionKey(
	const sg_rune_compact_static_t *static_data,
	uint32_t *mechanism_cursor, uint32_t *local_cursor,
	uint32_t *mechanism_out, uint32_t *portal_out, uint32_t *record_out)
{
	if (static_data == NULL || mechanism_cursor == NULL || local_cursor == NULL ||
		mechanism_out == NULL || portal_out == NULL || record_out == NULL)
		return -1;
	if ((static_data->mechanism_count != 0U && static_data->mechanisms == NULL) ||
		(static_data->transition_count != 0U && static_data->transitions == NULL) ||
		*mechanism_cursor > static_data->mechanism_count)
		return -1;
	while (*mechanism_cursor < static_data->mechanism_count)
	{
		const sg_rune_compact_mechanism_t *mechanism =
			&static_data->mechanisms[*mechanism_cursor];
		const sg_rune_compact_mechanism_transition_span_t span =
			mechanism->transitions;

		if (span.first > static_data->transition_count ||
			span.count > static_data->transition_count - span.first)
			return -1;
		if (*local_cursor >= span.count)
		{
			*mechanism_cursor += 1U;
			*local_cursor = 0U;
			continue;
		}
		{
			const uint32_t record = span.first + *local_cursor;
			const sg_rune_compact_static_transition_t *transition =
				&static_data->transitions[record];

			*local_cursor += 1U;
			if (transition->kind !=
				SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE)
				continue;
			*mechanism_out = *mechanism_cursor;
			*portal_out = transition->value.portal_state.portal.value;
			*record_out = record;
			return 1;
		}
	}
	return 0;
}

static int ValidatePortalTransitionBindings(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t transition_mechanism_cursor = 0U;
	uint32_t transition_local_cursor = 0U;
	uint32_t transition_mechanism = 0U;
	uint32_t transition_portal = 0U;
	uint32_t transition_record = 0U;
	uint32_t binding_index = 0U;
	int transition_result;

	if (model == NULL || static_data == NULL)
		return 0;
	/* There is no portal-indexed relation to validate when the model has no
	 * portals.  Handle the malformed non-empty projections explicitly before
	 * any summary lookup; this keeps the helper safe even when called in
	 * isolation by an analyzer or a future validator path. */
	if (model->portal_count == 0U)
	{
		if (static_data->portal_mechanism_count != 0U)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, 0U);
			return 0;
		}
		transition_result = NextPortalTransitionKey(static_data,
			&transition_mechanism_cursor, &transition_local_cursor,
			&transition_mechanism, &transition_portal, &transition_record);
		if (transition_result < 0)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, 0U);
			return 0;
		}
		if (transition_result > 0)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, transition_record);
			return 0;
		}
		return 1;
	}
	transition_result = NextPortalTransitionKey(static_data,
		&transition_mechanism_cursor, &transition_local_cursor,
		&transition_mechanism, &transition_portal, &transition_record);
	for (;;)
	{
		const sg_rune_compact_portal_mechanism_t *binding;
		int comparison;

		if (transition_result < 0)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, 0U);
			return 0;
		}
		if (transition_result == 0 &&
			binding_index == static_data->portal_mechanism_count)
			return 1;
		if (binding_index == static_data->portal_mechanism_count)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, transition_record);
			return 0;
		}
		binding = &static_data->portal_mechanisms[binding_index];
		if (transition_result == 0)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, binding_index);
			return 0;
		}
		if (transition_portal >= model->portal_count)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, transition_record);
			return 0;
		}
		comparison = CompareU32(transition_mechanism, binding->mechanism.value);
		if (comparison == 0)
			comparison = CompareU32(transition_portal, binding->portal.value);
		if (comparison < 0)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION, transition_record);
			return 0;
		}
		if (comparison > 0)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, binding_index);
			return 0;
		}
		if (binding->kind != SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS)
		{
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, binding_index);
			return 0;
		}
		binding_index++;
		transition_result = NextPortalTransitionKey(static_data,
			&transition_mechanism_cursor, &transition_local_cursor,
			&transition_mechanism, &transition_portal, &transition_record);
	}
}

int SG_RuneCompactStaticValidate(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error_out)
{
	if (error_out != NULL) {
		error_out->code = SG_RUNE_COMPACT_STATIC_ERROR_NONE;
		error_out->domain = SG_RUNE_COMPACT_STATIC_RECORD_MODEL;
		error_out->record = 0U;
	}
	if (!ModelReferencesPresent(model) || static_data == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_RECORD_MODEL, 0U);
		return 0;
	}
	if (!CountsValid(static_data, error_out) ||
		!ValidateMechanisms(model, static_data, error_out) ||
		!ValidateMechanismControllers(model, static_data, error_out) ||
		!ValidateTransitions(model, static_data, error_out) ||
		!ValidateMechanismEdges(model, static_data, error_out) ||
		!ValidateLandmarks(model, static_data, error_out) ||
		!ValidateFacetAnnotations(model, static_data, error_out) ||
		!ValidatePortalMechanisms(model, static_data, error_out) ||
		!ValidatePortalTransitionBindings(model, static_data, error_out))
		return 0;
	return 1;
}

const char *SG_RuneCompactStaticErrorString(
	sg_rune_compact_static_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_STATIC_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_STATIC_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_COMPACT_STATIC_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED:
		return "nonzero reserved field";
	case SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER:
		return "noncanonical order";
	case SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS:
		return "invalid static semantics";
	case SG_RUNE_COMPACT_STATIC_ERROR_CODE_COUNT:
		break;
	}
	return "unknown compact static error";
}
