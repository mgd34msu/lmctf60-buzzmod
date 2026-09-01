#include "sg_rune_compact_mechanisms_transitions.h"

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_localize.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(SG_RUNE_COMPACT_MECHANISM_TRANSITIONS_TESTING)
static size_t test_fail_after = SIZE_MAX;
static size_t test_allocation_count;

void SG_RuneCompactMechanismTransitionsTestFailAfter(size_t allocation)
{
	test_fail_after = allocation;
	test_allocation_count = 0U;
}

size_t SG_RuneCompactMechanismTransitionsTestAllocationCount(void)
{
	return test_allocation_count;
}
#endif

static void SetError(sg_rune_compact_mechanisms_error_t *error,
	sg_rune_compact_mechanisms_error_code_t code,
	sg_rune_compact_mechanisms_record_domain_t domain, uint32_t record)
{
	if (error == NULL)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static void *TransitionAllocate(size_t bytes)
{
#if defined(SG_RUNE_COMPACT_MECHANISM_TRANSITIONS_TESTING)
	if (test_allocation_count == test_fail_after)
	{
		test_allocation_count++;
		return NULL;
	}
	test_allocation_count++;
#endif
	return malloc(bytes);
}

static int ArrayShapeValid(const void *records, uint32_t count)
{
	return (records != NULL) == (count != 0U);
}

static int AllocateArray(void **records_out, uint32_t count, size_t width,
	sg_rune_compact_mechanisms_error_t *error)
{
	size_t bytes;

	if (records_out == NULL)
		return 0;
	*records_out = NULL;
	if (count == 0U)
		return 1;
	if (width == 0U || (size_t)count > SIZE_MAX / width)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, count);
		return 0;
	}
	bytes = (size_t)count * width;
	*records_out = TransitionAllocate(bytes);
	if (*records_out == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, count);
		return 0;
	}
	memset(*records_out, 0, bytes);
	return 1;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int Binary32Canonical(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static int Q8FromFloat(float value, int32_t *value_out)
{
	double rounded;

	if (value_out == NULL || !isfinite(value))
		return 0;
	rounded = nearbyint((double)value * 8.0);
	if (!isfinite(rounded) || rounded < (double)INT32_MIN ||
		rounded > (double)INT32_MAX)
		return 0;
	*value_out = (int32_t)rounded;
	return 1;
}

static void GeometryModel(const sg_rune_compact_geometry_view_t *geometry,
	sg_rune_compact_model_t *model_out)
{
	memset(model_out, 0, sizeof(*model_out));
	model_out->cells = geometry->cells;
	model_out->cell_count = geometry->cell_count;
	model_out->facets = geometry->facets;
	model_out->facet_count = geometry->facet_count;
	model_out->incidences = geometry->incidences;
	model_out->incidence_count = geometry->incidence_count;
	model_out->cell_incidences = geometry->cell_incidences;
	model_out->cell_incidence_count = geometry->cell_incidence_count;
}

static int Locate(const sg_rune_compact_model_t *model,
	const sg_rune_q8_vec3_t *witness, sg_rune_compact_cell_index_t *cell_out)
{
	sg_rune_compact_location_t location;

	if (SG_RuneCompactLocalize(model, witness, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK)
		return 0;
	*cell_out = location.cell;
	return 1;
}

static int PointMatchesCell(const sg_rune_compact_model_t *model,
	const sg_rune_q8_vec3_t *witness, sg_rune_compact_cell_index_t wanted)
{
	sg_rune_compact_cell_index_t located;

	return Locate(model, witness, &located) && located.value == wanted.value;
}

/* The carried-support witness belongs to an immutable model-local root, not
 * merely to its transformed endpoint.  Keep this projected polygon proof in
 * lockstep with the terminal static contract: it binds the exact catalog
 * tuple, plane, and Q8 polygon without treating a coplanar world facet as
 * mover provenance. */
static int SourceSurfaceSupportsPoint(
	const sg_rune_compact_geometry_view_t *geometry, uint32_t surface_index,
	uint32_t mover_model, const sg_rune_q8_vec3_t *point)
{
	const sg_rune_compact_source_surface_t *surface;
	const sg_rune_q8_vec3_t *vertices;
	float normal[3];
	double plane_value;
	uint32_t drop_axis = 0U;
	uint32_t vertex;
	int has_normal = 0;
	int sign = 0;

	if (geometry == NULL || point == NULL || geometry->source_surfaces == NULL ||
		geometry->source_surface_vertices == NULL ||
		surface_index >= geometry->source_surface_count)
		return 0;
	surface = &geometry->source_surfaces[surface_index];
	if (surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL ||
		surface->cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->parent_surface != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->split_ordinal != 0U || surface->source.model != mover_model ||
		surface->source.model == SG_HOST_COLLISION_MODEL_WORLD ||
		surface->vertices.count < 3U || surface->vertices.first >
			geometry->source_surface_vertex_count || surface->vertices.count >
			geometry->source_surface_vertex_count - surface->vertices.first ||
		!Binary32Canonical(surface->plane.distance_bits))
		return 0;
	plane_value = -(double)BitsFloat(surface->plane.distance_bits);
	for (vertex = 0U; vertex < 3U; vertex++)
	{
		if (!Binary32Canonical(surface->plane.normal_bits[vertex]))
			return 0;
		normal[vertex] = BitsFloat(surface->plane.normal_bits[vertex]);
		if ((surface->plane.normal_bits[vertex] & UINT32_C(0x7fffffff)) != 0U)
			has_normal = 1;
		if (fabs((double)normal[vertex]) > fabs((double)normal[drop_axis]))
			drop_axis = vertex;
		plane_value += (double)normal[vertex] *
			(double)point->value[vertex] / 8.0;
	}
	if (!has_normal || !isfinite(plane_value) || fabs(plane_value) > 1.0e-6)
		return 0;
	vertices = &geometry->source_surface_vertices[surface->vertices.first];
	for (vertex = 0U; vertex < surface->vertices.count; vertex++)
	{
		const uint32_t next = (vertex + 1U) % surface->vertices.count;
		uint32_t a0;
		uint32_t a1;
		double ax;
		double ay;
		double bx;
		double by;
		double px;
		double py;
		double cross;

		if (drop_axis == 0U)
		{
			a0 = 1U;
			a1 = 2U;
		}
		else if (drop_axis == 1U)
		{
			a0 = 0U;
			a1 = 2U;
		}
		else
		{
			a0 = 0U;
			a1 = 1U;
		}
		ax = (double)vertices[vertex].value[a0] / 8.0;
		ay = (double)vertices[vertex].value[a1] / 8.0;
		bx = (double)vertices[next].value[a0] / 8.0;
		by = (double)vertices[next].value[a1] / 8.0;
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

static int ReplayTransportLocalPose(const sg_rune_compact_builder_t *builder,
	uint32_t mover_entity_ordinal,
	const sg_host_collision_world_transform_t *transform,
	const sg_rune_q8_vec3_t *local, const sg_rune_vec3_t *published,
	sg_rune_vec3_t *world_out)
{
	sg_rune_vec3_t replayed;
	sg_host_law_result_t replay_result;
	uint32_t axis;

	if (builder == NULL || transform == NULL || local == NULL ||
		published == NULL || world_out == NULL)
		return 0;
	memset(&replayed, 0, sizeof(replayed));
	replay_result = SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(builder,
		mover_entity_ordinal, transform, local, &replayed);
	if (replay_result.status != SG_HOST_LAW_OK)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(replayed.value[axis]) !=
				FloatBits(published->value[axis]))
			return 0;
	*world_out = replayed;
	return 1;
}

static int Binary32PointMatchesCell(const sg_rune_compact_model_t *model,
	const sg_rune_vec3_t *point, sg_rune_compact_cell_index_t wanted,
	sg_rune_stance_t stance)
{
	sg_rune_compact_location_t located;

	return stance < SG_RUNE_STANCE_COUNT &&
		SG_RuneCompactLocalizeBinary32(model, point, &located) ==
			SG_RUNE_COMPACT_LOCALIZE_OK &&
		located.cell.value == wanted.value &&
		(located.valid_stances &
			(SG_RUNE_STANCE_VALID_STANDING << stance)) != 0U;
}

static int TransportPoseRelationValid(
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_builder_mover_result_t *result)
{
	const sg_rune_compact_hull_t *hull;
	int64_t player_z;
	uint32_t axis;

	if (owner == NULL || result == NULL || result->stance >= SG_RUNE_STANCE_COUNT)
		return 0;
	hull = result->stance == SG_RUNE_STANCE_STANDING ?
		&owner->identity.standing_hull : &owner->identity.crouching_hull;
	for (axis = 0U; axis < 3U; axis++)
		if (result->source_player_local.value[axis] !=
				result->destination_player_local.value[axis] ||
			result->source_support_local.value[axis] !=
				result->destination_support_local.value[axis] ||
			(axis < 2U && (result->source_player_local.value[axis] !=
				result->source_support_local.value[axis] ||
				result->destination_player_local.value[axis] !=
					result->destination_support_local.value[axis])))
			return 0;
	player_z = (int64_t)result->source_support_local.value[2] -
		(int64_t)hull->mins.value[2] +
		SG_RUNE_COMPACT_SUPPORT_CLEARANCE_Q8;
	if (player_z < INT32_MIN || player_z > INT32_MAX ||
		result->source_player_local.value[2] != (int32_t)player_z)
		return 0;
	player_z = (int64_t)result->destination_support_local.value[2] -
		(int64_t)hull->mins.value[2] +
		SG_RUNE_COMPACT_SUPPORT_CLEARANCE_Q8;
	return player_z >= INT32_MIN && player_z <= INT32_MAX &&
		result->destination_player_local.value[2] == (int32_t)player_z;
}

static int TransportEndpointReplayValid(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_builder_mover_result_t *result)
{
	sg_rune_compact_model_t compact_model;
	sg_rune_vec3_t source_player_world;
	sg_rune_vec3_t destination_player_world;
	sg_rune_vec3_t source_support_world;
	sg_rune_vec3_t destination_support_world;

	if (builder == NULL || owner == NULL || geometry == NULL || result == NULL ||
		result->entry_cell.value >= geometry->cell_count ||
		result->exit_cell.value >= geometry->cell_count ||
		!TransportPoseRelationValid(owner, result) ||
		!SourceSurfaceSupportsPoint(geometry, result->source_surface_ordinal,
			result->mover_model, &result->source_support_local) ||
		!SourceSurfaceSupportsPoint(geometry, result->source_surface_ordinal,
			result->mover_model, &result->destination_support_local) ||
		!ReplayTransportLocalPose(builder, mover_entity_ordinal,
			&result->source_mover_transform, &result->source_player_local,
			&result->source_player_world, &source_player_world) ||
		!ReplayTransportLocalPose(builder, mover_entity_ordinal,
			&result->source_mover_transform, &result->source_support_local,
			&result->source_support_world, &source_support_world) ||
		!ReplayTransportLocalPose(builder, mover_entity_ordinal,
			&result->destination_mover_transform,
			&result->destination_player_local, &result->destination_player_world,
			&destination_player_world) ||
		!ReplayTransportLocalPose(builder, mover_entity_ordinal,
			&result->destination_mover_transform,
			&result->destination_support_local,
			&result->destination_support_world, &destination_support_world))
		return 0;
	GeometryModel(geometry, &compact_model);
	return Binary32PointMatchesCell(&compact_model, &source_player_world,
			result->entry_cell, result->stance) &&
		Binary32PointMatchesCell(&compact_model, &destination_player_world,
			result->exit_cell, result->stance);
}

static int SemanticShapeValid(
	const sg_rune_compact_builder_owner_view_t *owner,
	sg_rune_compact_mechanisms_error_t *error)
{
	const sg_bsp_entity_semantics_t *semantics;
	uint32_t index;

	if (owner == NULL || (semantics = owner->entity_semantics) == NULL ||
		!ArrayShapeValid(semantics->entities, semantics->entity_count) ||
		!ArrayShapeValid(semantics->edges, semantics->edge_count) ||
		semantics->entity_count != owner->identity.source_counts.entity_count ||
		semantics->world.source_set_identity != semantics->source_set_identity)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, 0U);
		return 0;
	}
	for (index = 0U; index < semantics->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity = &semantics->entities[index];

		if (entity->source_set_identity != semantics->source_set_identity ||
			entity->canonical_ordinal != index ||
			(index != 0U && semantics->entities[index - 1U]
				.source_entity_ordinal >= entity->source_entity_ordinal))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			return 0;
		}
	}
	for (index = 0U; index < semantics->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[index];

		if (edge->source >= semantics->entity_count ||
			edge->destination >= semantics->entity_count ||
			edge->kind < SG_MECH_EDGE_TARGET ||
			edge->kind > SG_MECH_EDGE_ROUTE_TARGET)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, index);
			return 0;
		}
	}
	return 1;
}

static int SourceSurfaceCompare(
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

static int SourceSurfaceCatalogValid(
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_identity_t *identity,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t index;

	if (!ArrayShapeValid(geometry->source_surfaces,
		geometry->source_surface_count) ||
		!ArrayShapeValid(geometry->source_surface_vertices,
			geometry->source_surface_vertex_count))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		return 0;
	}
	for (index = 0U; index < geometry->source_surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *surface =
			&geometry->source_surfaces[index];
		uint32_t axis;

		if (surface->source.model >= identity->source_counts.model_count ||
			surface->source.brush >= identity->source_counts.brush_count ||
			surface->source.brush_side >=
				identity->source_counts.brush_side_count ||
			surface->source.plane >= identity->source_counts.plane_count ||
			(uint32_t)surface->frame >=
				(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT ||
			surface->frame != (surface->source.model == 0U ?
				SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
				SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) ||
			surface->cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
			surface->parent_surface != SG_RUNE_COMPACT_INDEX_NONE ||
			surface->split_ordinal != 0U || surface->vertices.count < 3U ||
			surface->vertices.first > geometry->source_surface_vertex_count ||
			surface->vertices.count > geometry->source_surface_vertex_count -
				surface->vertices.first ||
			!Binary32Canonical(surface->plane.distance_bits))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, index);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
			if (!Binary32Canonical(surface->plane.normal_bits[axis]))
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, index);
				return 0;
			}
		if (index != 0U && SourceSurfaceCompare(
			&geometry->source_surfaces[index - 1U], surface) >= 0)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, index);
			return 0;
		}
	}
	return 1;
}

static int GeometryShapeValid(const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_identity_t *identity,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t index;

	if (geometry == NULL || identity == NULL ||
		!ArrayShapeValid(geometry->cells, geometry->cell_count) ||
		!ArrayShapeValid(geometry->facets, geometry->facet_count) ||
		!ArrayShapeValid(geometry->incidences, geometry->incidence_count) ||
		!ArrayShapeValid(geometry->cell_incidences,
			geometry->cell_incidence_count) ||
		!ArrayShapeValid(geometry->vertices, geometry->vertex_count) ||
		!ArrayShapeValid(geometry->portals, geometry->portal_count) ||
		!SourceSurfaceCatalogValid(geometry, identity, error))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		return 0;
	}
	for (index = 0U; index < geometry->cell_count; index++)
		if (geometry->cells[index].source.model >=
			identity->source_counts.model_count)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, index);
			return 0;
		}
	for (index = 0U; index < geometry->portal_count; index++)
	{
		const sg_rune_compact_portal_t *portal = &geometry->portals[index];
		const sg_rune_compact_facet_t *facet;

		if (portal->facet.value >= geometry->facet_count ||
			portal->negative_incidence.value >= geometry->incidence_count ||
			portal->positive_incidence.value >= geometry->incidence_count ||
			portal->negative_incidence.value == portal->positive_incidence.value ||
			portal->direction >= SG_RUNE_PORTAL_CONTINUITY_COUNT)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, index);
			return 0;
		}
		facet = &geometry->facets[portal->facet.value];
		if (facet->vertices.count == 0U ||
			facet->vertices.first > geometry->vertex_count ||
			facet->vertices.count > geometry->vertex_count -
				facet->vertices.first ||
			geometry->incidences[portal->negative_incidence.value].cell.value >=
				geometry->cell_count ||
			geometry->incidences[portal->positive_incidence.value].cell.value >=
				geometry->cell_count)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, index);
			return 0;
		}
	}
	return 1;
}

static sg_rune_compact_mechanism_activation_mask_t ActivationMask(
	const sg_bsp_entity_semantic_t *entity)
{
	sg_rune_compact_mechanism_activation_mask_t mask = 0U;

	if ((entity->flags & SG_BSP_ENTITY_AUTO_ACTIVATED) != 0U)
		mask |= SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	if ((entity->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED) != 0U)
		mask |= SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	if ((entity->flags & SG_BSP_ENTITY_USE_ACTIVATED) != 0U)
		mask |= SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	if ((entity->flags & SG_BSP_ENTITY_DAMAGE_ACTIVATED) != 0U)
		mask |= SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE;
	if ((entity->flags & SG_BSP_ENTITY_INVENTORY_GATED) != 0U)
		mask |= SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
	return mask;
}

static int MechanismAuthorityValid(
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_model_t *model,
	sg_rune_compact_mechanisms_error_t *error, uint32_t index)
{
	const sg_bsp_entity_semantic_t *entity;

	if (mechanism == NULL || semantics == NULL ||
		mechanism->source.entity_ordinal >= semantics->entity_count)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
		return 0;
	}
	entity = &semantics->entities[mechanism->source.entity_ordinal];
	if ((entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND) == 0U ||
		entity->mechanism_kind != (sg_rune_mechanism_kind_t)mechanism->kind ||
		mechanism->kind >= SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT ||
		mechanism->activation == 0U || mechanism->activation !=
			ActivationMask(entity) ||
		!PointMatchesCell(model, &mechanism->activation_witness,
			mechanism->activation_cell))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
		return 0;
	}
	return 1;
}

static int MoverKind(sg_rune_compact_mechanism_authority_kind_t kind)
{
	return kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
}

static int MoverAuthority(const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity)
{
	return MoverKind(mechanism->kind) &&
		(mechanism->flags &
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE) != 0U &&
		(entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) != 0U &&
		entity->bsp_model != SG_BSP_ENTITY_MODEL_NONE;
}

static int MoverTransition(
	const sg_rune_compact_mechanism_transition_t *transition)
{
	return transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
		transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
}

static int ToggleMechanism(
	const sg_rune_compact_mechanism_authority_t *mechanism)
{
	return (mechanism->flags &
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) == 0U &&
		mechanism->initial_state != mechanism->activated_state &&
		mechanism->reset_state == mechanism->activated_state;
}

/* A caller may leave an authority duration unspecified while transition
 * construction discovers exact values.  Once it publishes a nonzero
 * aggregate, though, every corresponding host transition must agree. */
static int TimingAuthorityMatches(
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_transitions_result_t *result,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t mechanism_index;

	for (mechanism_index = 0U; mechanism_index < mechanism_count;
		mechanism_index++)
	{
		const sg_rune_compact_mechanism_authority_t *mechanism =
			&mechanisms[mechanism_index];
		const sg_rune_compact_mechanism_span_t span = result->spans[
			mechanism_index];
		uint32_t travel = 0U;
		uint32_t recovery = 0U;
		int uniform_travel = 1;
		int uniform_recovery = 1;
		int saw_travel = 0;
		int saw_recovery = 0;
		uint32_t index;

		for (index = span.first; index < span.first + span.count; index++)
		{
			const sg_rune_compact_mechanism_transition_t *transition =
				&result->transitions[index];
			const uint32_t elapsed = (uint32_t)transition->elapsed_ms;

			if (!MoverTransition(transition))
				continue;
			if (transition->elapsed_ms == 0U ||
				transition->elapsed_ms > UINT32_MAX)
			{
				SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
				return 0;
			}
			if (!saw_travel)
				travel = elapsed;
			else if (travel != elapsed)
				uniform_travel = 0;
			saw_travel = 1;
			if (!ToggleMechanism(mechanism) &&
				transition->source_state == mechanism->activated_state &&
				transition->destination_state == mechanism->reset_state)
			{
				if (!saw_recovery)
					recovery = elapsed;
				else if (recovery != elapsed)
					uniform_recovery = 0;
				saw_recovery = 1;
			}
		}
		if ((mechanism->travel_ms !=
			SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED &&
			(!saw_travel || !uniform_travel || mechanism->travel_ms != travel)) ||
			(mechanism->recovery_ms !=
			SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED &&
			(!saw_recovery || !uniform_recovery ||
				mechanism->recovery_ms != recovery)))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			return 0;
		}
	}
	return 1;
}

/* Compact portal facets describe only the world configuration partition and
 * therefore deliberately remain model zero.  Dynamic provenance is supplied
 * exclusively by immutable model-local catalog roots.  Their conversion and
 * overlap test stay behind the authenticated builder mover boundary. */
static int SourceSurfaceOwnedByMover(
	const sg_rune_compact_geometry_view_t *geometry, uint32_t surface_index,
	uint32_t mover_model)
{
	const sg_rune_compact_source_surface_t *surface;

	if (geometry == NULL || surface_index >= geometry->source_surface_count)
		return 0;
	surface = &geometry->source_surfaces[surface_index];
	return surface->source.model == mover_model &&
		surface->frame == SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL &&
		surface->cell.value == SG_RUNE_COMPACT_INDEX_NONE &&
		surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE &&
		surface->split_ordinal == 0U;
}

static int AddCount(uint32_t *total, uint32_t addition,
	sg_rune_compact_mechanisms_error_t *error, uint32_t record)
{
	if (total == NULL || addition > UINT32_MAX - *total)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, record);
		return 0;
	}
	*total += addition;
	return 1;
}

static int HostMoverQuery(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_bsp_entity_semantic_t *entity,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result_out, int *applicable_out,
	sg_rune_compact_mechanisms_error_t *error, uint32_t mechanism_index)
{
	sg_host_law_result_t law_result;
	sg_rune_compact_builder_owner_view_t owner;

	if (builder == NULL || geometry == NULL || geometry_view == NULL ||
		entity == NULL || request == NULL || result_out == NULL ||
		applicable_out == NULL || request->source_surface_ordinal >=
		geometry_view->source_surface_count ||
		(request->team_portal != 0 && request->team_portal != 1) ||
		(request->team_portal == 0 && request->team_master_entity_ordinal !=
			SG_RUNE_COMPACT_INDEX_NONE))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	/* The tagged transport tail is typed.  A lift has no route endpoints or
	 * fanout, so preserve INDEX_NONE rather than relying on the host to repair
	 * a malformed candidate request. */
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT &&
		entity->mechanism_kind == SG_RUNE_MECHANISM_LIFT &&
		(request->source_endpoint_entity_ordinal != SG_RUNE_COMPACT_INDEX_NONE ||
			request->destination_endpoint_entity_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE || request->route_fanout_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	memset(result_out, 0, sizeof(*result_out));
	law_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		request, result_out);
	if (law_result.status != SG_HOST_LAW_OK ||
		(result_out->applicable != 0 && result_out->applicable != 1) ||
		result_out->failure >= SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_COUNT)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	if (result_out->applicable == 0)
	{
		if (result_out->failure != SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			return 0;
		}
		*applicable_out = 0;
		return 1;
	}
	if (result_out->mode != request->mode || result_out->team_portal !=
		request->team_portal || result_out->team_master_entity_ordinal !=
		request->team_master_entity_ordinal || result_out->source_state !=
		request->source_state || result_out->destination_state !=
		request->destination_state || result_out->stance != request->stance ||
		result_out->mover_model !=
		entity->bsp_model || result_out->source_surface_ordinal !=
		request->source_surface_ordinal || result_out->source_vertex_count !=
		geometry_view->source_surfaces[request->source_surface_ordinal]
			.vertices.count || result_out->portal_ordinal != request->portal_ordinal ||
		result_out->source_endpoint_entity_ordinal !=
			request->source_endpoint_entity_ordinal ||
		result_out->destination_endpoint_entity_ordinal !=
			request->destination_endpoint_entity_ordinal ||
		result_out->route_fanout_ordinal != request->route_fanout_ordinal)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	/* A carried-support oracle can positively evaluate a request and prove
	 * that the rider is blocked, crushed, or cannot land.  After the complete
	 * request echo above authenticates that this is our candidate, omit it and
	 * continue the finite catalog scan. */
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT &&
		result_out->failure != SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE)
	{
		*applicable_out = 0;
		return 1;
	}
	if (result_out->failure != SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE)
	{
		if (result_out->entry_cell.value != request->entry_cell.value ||
			result_out->exit_cell.value != request->exit_cell.value ||
			result_out->elapsed_ms == 0U || result_out->start_supported != 0 ||
			result_out->end_supported != 0 || result_out->swept_static_clear != 0 ||
			(result_out->source_portal_blocked != 0 &&
				result_out->source_portal_blocked != 1) ||
			(result_out->destination_portal_blocked != 0 &&
				result_out->destination_portal_blocked != 1) ||
			result_out->source_portal_blocked ==
				result_out->destination_portal_blocked)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			return 0;
		}
	}
	else if (request->mode ==
		SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT)
	{
		uint32_t axis;
		uint32_t component;

		if (result_out->entry_cell.value >= geometry_view->cell_count ||
			result_out->exit_cell.value >= geometry_view->cell_count ||
			result_out->elapsed_ms == 0U || result_out->start_supported != 1 ||
			result_out->end_supported != 1 ||
			result_out->swept_static_clear != 1)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
		{
			if (!Binary32Canonical(FloatBits(
				result_out->source_player_world.value[axis])) ||
				!Binary32Canonical(FloatBits(
					result_out->destination_player_world.value[axis])) ||
				!Binary32Canonical(FloatBits(
					result_out->source_support_world.value[axis])) ||
				!Binary32Canonical(FloatBits(
					result_out->destination_support_world.value[axis])) ||
				!Binary32Canonical(FloatBits(
					result_out->source_mover_transform.origin[axis])) ||
				!Binary32Canonical(FloatBits(
					result_out->destination_mover_transform.origin[axis])))
			{
				SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					mechanism_index);
				return 0;
			}
			for (component = 0U; component < 3U; component++)
				if (!Binary32Canonical(FloatBits(result_out->source_mover_transform
					.axis[axis][component])) || !Binary32Canonical(FloatBits(
						result_out->destination_mover_transform
							.axis[axis][component])))
				{
					SetError(error,
						SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
						SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
						mechanism_index);
					return 0;
				}
		}
		if (!SG_RuneCompactBuilderOwnerRead(builder, &owner) ||
			!SG_RuneCompactIdentityMatches(&owner.identity,
				&geometry_view->identity) || !TransportEndpointReplayValid(builder,
				entity->canonical_ordinal, &owner, geometry_view, result_out))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
				mechanism_index);
			return 0;
		}
	}
	else
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	*applicable_out = 1;
	return 1;
}

static int EvaluatePortalState(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, int team_portal,
	uint32_t team_master_entity_ordinal, uint32_t surface_index,
	uint32_t portal_index,
	sg_rune_compact_mechanism_authority_state_t source_state,
	sg_rune_compact_mechanism_authority_state_t destination_state,
	sg_rune_compact_builder_mover_result_t *result_out,
	int *applicable_out, sg_rune_compact_mechanisms_error_t *error,
	uint32_t mechanism_index)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_source_surface_t *surface;
	sg_rune_compact_builder_mover_request_t request;
	sg_rune_vec3_t *source_vertices = NULL;
	sg_rune_vec3_t *destination_vertices = NULL;
	size_t bytes;
	int valid = 0;

	if (geometry_view == NULL || mechanism == NULL || entity == NULL ||
		result_out == NULL || applicable_out == NULL || portal_index >=
		geometry_view->portal_count || !SourceSurfaceOwnedByMover(geometry_view,
		surface_index, entity->bsp_model))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, portal_index);
		return 0;
	}
	portal = &geometry_view->portals[portal_index];
	surface = &geometry_view->source_surfaces[surface_index];
	if (portal->negative_incidence.value >= geometry_view->incidence_count ||
		portal->positive_incidence.value >= geometry_view->incidence_count ||
		geometry_view->incidences[portal->negative_incidence.value].cell.value >=
			geometry_view->cell_count || geometry_view->incidences[
			portal->positive_incidence.value].cell.value >= geometry_view->cell_count ||
		surface->vertices.count > SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, portal_index);
		return 0;
	}
	bytes = (size_t)surface->vertices.count * sizeof(*source_vertices);
	source_vertices = TransitionAllocate(bytes);
	destination_vertices = TransitionAllocate(bytes);
	if (source_vertices == NULL || destination_vertices == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		goto cleanup;
	}
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE;
	request.team_portal = team_portal;
	request.team_master_entity_ordinal = team_portal ?
		team_master_entity_ordinal : SG_RUNE_COMPACT_INDEX_NONE;
	request.mover_entity_ordinal = entity->canonical_ordinal;
	request.source_state = source_state;
	request.destination_state = destination_state;
	request.source_surface_ordinal = surface_index;
	request.portal_ordinal = portal_index;
	request.entry_cell = geometry_view->incidences[
		portal->negative_incidence.value].cell;
	request.exit_cell = geometry_view->incidences[
		portal->positive_incidence.value].cell;
	request.source_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.destination_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.route_fanout_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.support_pose_mode = SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_EXPLICIT;
	request.source_world_vertices_out = source_vertices;
	request.destination_world_vertices_out = destination_vertices;
	request.world_vertex_capacity = surface->vertices.count;
	request.stance = SG_RUNE_STANCE_STANDING;
	valid = HostMoverQuery(builder, geometry, geometry_view, entity, &request,
		result_out, applicable_out, error, mechanism_index);

cleanup:
	free(source_vertices);
	free(destination_vertices);
	return valid;
}

static int EvaluateCarriedSupport(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t surface_index,
	uint32_t source_endpoint, uint32_t destination_endpoint,
	uint32_t route_fanout,
	sg_rune_compact_mechanism_authority_state_t source_state,
	sg_rune_compact_mechanism_authority_state_t destination_state,
	sg_rune_stance_t stance,
	sg_rune_compact_builder_mover_result_t *result_out, int *applicable_out,
	sg_rune_compact_mechanisms_error_t *error, uint32_t mechanism_index)
{
	sg_rune_compact_builder_mover_request_t request;

	if (mechanism == NULL || entity == NULL || result_out == NULL ||
		applicable_out == NULL || !SourceSurfaceOwnedByMover(geometry_view,
		surface_index, entity->bsp_model))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT;
	request.team_portal = 0;
	request.team_master_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.mover_entity_ordinal = mechanism->source.entity_ordinal;
	request.source_state = source_state;
	request.destination_state = destination_state;
	request.source_surface_ordinal = surface_index;
	request.portal_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.entry_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.exit_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.source_endpoint_entity_ordinal = source_endpoint;
	request.destination_endpoint_entity_ordinal = destination_endpoint;
	request.route_fanout_ordinal = route_fanout;
	request.support_pose_mode = SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL;
	request.stance = stance;
	return HostMoverQuery(builder, geometry, geometry_view, entity, &request,
		result_out, applicable_out, error, mechanism_index);
}

static int TeleportEdge(const sg_bsp_entity_semantic_edge_t *edge,
	const sg_bsp_entity_semantics_t *semantics, uint32_t source)
{
	return edge->source == source && edge->kind == SG_MECH_EDGE_TARGET &&
		edge->destination < semantics->entity_count &&
		semantics->entities[edge->destination].mechanism_kind ==
			SG_RUNE_MECHANISM_TELEPORT &&
		semantics->entities[edge->destination].mechanism_role ==
			SG_MECH_NODE_TELEPORT_DEST;
}

static int ProcessMover(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t mechanism_index,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error);

static int CountTransitions(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	uint32_t mechanism_index, uint32_t *count_out,
	sg_rune_compact_mechanisms_error_t *error)
{
	const sg_bsp_entity_semantic_t *entity =
		&semantics->entities[mechanism->source.entity_ordinal];
	uint32_t count = 0U;
	uint32_t index;

	if (MoverKind(mechanism->kind))
	{
		if (!MoverAuthority(mechanism, entity))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
			return 0;
		}
		if (entity->bsp_model >= geometry_view->identity.source_counts.model_count)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
			return 0;
		}
		if (!ProcessMover(builder, geometry, geometry_view, semantics,
			mechanisms, mechanism_count, mechanism, entity, mechanism_index,
			NULL, &count, error))
			return 0;
	}
	else if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT)
	{
		if (entity->mechanism_role != SG_MECH_NODE_TELEPORTER)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
			return 0;
		}
		for (index = 0U; index < semantics->edge_count; index++)
			if (TeleportEdge(&semantics->edges[index], semantics,
				mechanism->source.entity_ordinal) &&
				!AddCount(&count, 1U, error, mechanism_index))
				return 0;
	}
	else if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH)
	{
		if (entity->mechanism_role != SG_MECH_NODE_PUSH ||
			entity->physics_kind != SG_BSP_ENTITY_PHYSICS_PUSH)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
			return 0;
		}
		count = 1U;
	}
	*count_out = count;
	return 1;
}

static void InitializeTransition(sg_rune_compact_mechanism_transition_t *output,
	uint32_t mechanism, sg_rune_compact_mechanism_transition_kind_t kind)
{
	memset(output, 0, sizeof(*output));
	output->mechanism = mechanism;
	output->kind = kind;
	switch (kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		output->value.portal_state.portal.value = SG_RUNE_COMPACT_INDEX_NONE;
		output->value.portal_state.mover_model = SG_RUNE_COMPACT_INDEX_NONE;
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		output->value.teleport.destination.entity_ordinal =
			SG_RUNE_COMPACT_INDEX_NONE;
		output->value.teleport.fanout_ordinal = UINT32_MAX;
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		output->value.transport.mover_model = SG_RUNE_COMPACT_INDEX_NONE;
		output->value.transport.source_surface_ordinal =
			SG_RUNE_COMPACT_INDEX_NONE;
		output->value.transport.source_endpoint.entity_ordinal =
			SG_RUNE_COMPACT_INDEX_NONE;
		output->value.transport.destination_endpoint.entity_ordinal =
			SG_RUNE_COMPACT_INDEX_NONE;
		output->value.transport.fanout_ordinal = UINT32_MAX;
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
}

static int MoverCarriesSupport(
	sg_rune_compact_mechanism_authority_kind_t kind,
	const sg_bsp_entity_semantic_t *entity)
{
	return kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN ||
		(kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR && entity != NULL &&
			(entity->angular_mover.kind ==
				SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR ||
			 entity->angular_mover.kind ==
				SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR));
}

static int ContinuousRotator(const sg_bsp_entity_semantic_t *entity)
{
	return entity != NULL && entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR &&
		entity->angular_mover.kind ==
			SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
}

static int MoverPublishesPortalState(
	sg_rune_compact_mechanism_authority_kind_t kind,
	const sg_bsp_entity_semantic_t *entity)
{
	return kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
		kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
		(kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR &&
			SG_BspEntitySemanticHasFiniteAngularDoor(entity));
}

static int TeamRoot(const sg_bsp_entity_semantics_t *semantics,
	uint32_t entity_ordinal, uint32_t *root_out,
	sg_rune_compact_mechanisms_error_t *error, uint32_t mechanism_index)
{
	uint32_t current;
	uint32_t step;

	if (semantics == NULL || root_out == NULL || entity_ordinal >=
		semantics->entity_count)
		return 0;
	current = entity_ordinal;
	/* The finite canonical entity set is the termination proof.  Semantics
	 * normally stores a direct member -> master star, but hostile or stale
	 * source facts cannot smuggle in a cycle or a second parent. */
	for (step = 0U; step < semantics->entity_count; step++)
	{
		uint32_t edge_index;
		uint32_t parent = SG_RUNE_COMPACT_INDEX_NONE;

		for (edge_index = 0U; edge_index < semantics->edge_count;
			edge_index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge =
				&semantics->edges[edge_index];

			if (edge->kind != SG_MECH_EDGE_TEAM || edge->source != current)
				continue;
			if (parent != SG_RUNE_COMPACT_INDEX_NONE ||
				edge->destination == current)
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, edge_index);
				return 0;
			}
			parent = edge->destination;
		}
		if (parent == SG_RUNE_COMPACT_INDEX_NONE)
		{
			*root_out = current;
			return 1;
		}
		current = parent;
	}
	SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
		SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
	return 0;
}

static int DirectTeamMember(const sg_bsp_entity_semantics_t *semantics,
	uint32_t entity_ordinal, uint32_t root_ordinal)
{
	uint32_t edge_index;

	if (semantics == NULL || entity_ordinal >= semantics->entity_count ||
		root_ordinal >= semantics->entity_count)
		return 0;
	if (entity_ordinal == root_ordinal)
		return 1;
	for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge =
			&semantics->edges[edge_index];

		if (edge->kind == SG_MECH_EDGE_TEAM &&
			edge->source == entity_ordinal &&
			edge->destination == root_ordinal)
			return 1;
	}
	return 0;
}

static int TeamHasDirectMember(const sg_bsp_entity_semantics_t *semantics,
	uint32_t root_ordinal)
{
	uint32_t edge_index;

	if (semantics == NULL || root_ordinal >= semantics->entity_count)
		return 0;
	for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge =
			&semantics->edges[edge_index];

		if (edge->kind == SG_MECH_EDGE_TEAM &&
			edge->destination == root_ordinal)
			return 1;
	}
	return 0;
}

static int MechanismIndexForEntity(
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count, uint32_t entity_ordinal, uint32_t *index_out,
	sg_rune_compact_mechanisms_error_t *error, uint32_t mechanism_index)
{
	uint32_t index;
	uint32_t found = SG_RUNE_COMPACT_INDEX_NONE;

	if (mechanisms == NULL || index_out == NULL)
		return 0;
	for (index = 0U; index < mechanism_count; index++)
	{
		if (mechanisms[index].source.entity_ordinal != entity_ordinal)
			continue;
		if (found != SG_RUNE_COMPACT_INDEX_NONE)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
			return 0;
		}
		found = index;
	}
	if (found == SG_RUNE_COMPACT_INDEX_NONE)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, entity_ordinal);
		return 0;
	}
	*index_out = found;
	return 1;
}

static int AdvanceMoverTransition(
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error, uint32_t mechanism_index,
	sg_rune_compact_mechanism_transition_t **transition_out)
{
	uint32_t index;

	if (cursor == NULL || transition_out == NULL || !AddCount(cursor, 1U,
		error, mechanism_index))
		return 0;
	index = *cursor - 1U;
	*transition_out = output == NULL ? NULL : &output[index];
	return 1;
}

static int EmitPortalState(
	const sg_rune_compact_mechanism_authority_t *mechanism,
	uint32_t mechanism_index,
	const sg_rune_compact_builder_mover_result_t *result,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error)
{
	sg_rune_compact_mechanism_transition_t *transition;

	if (mechanism == NULL || result == NULL ||
		result->elapsed_ms == 0U || result->elapsed_ms > UINT32_MAX ||
		(result->source_portal_blocked != 0 &&
			result->source_portal_blocked != 1) ||
		(result->destination_portal_blocked != 0 &&
			result->destination_portal_blocked != 1) ||
		result->source_portal_blocked == result->destination_portal_blocked ||
		!AdvanceMoverTransition(output,
		cursor, error, mechanism_index, &transition))
		return 0;
	if (transition == NULL)
		return 1;
	InitializeTransition(transition, mechanism_index,
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE);
	transition->entry_cell = result->entry_cell;
	transition->exit_cell = result->exit_cell;
	transition->source_state = result->source_state;
	transition->destination_state = result->destination_state;
	transition->elapsed_ms = result->elapsed_ms;
	transition->value.portal_state.portal.value = result->portal_ordinal;
	transition->value.portal_state.mover_model = result->mover_model;
	transition->value.portal_state.delay_ms = mechanism->delay_ms;
	transition->value.portal_state.dwell_ms = mechanism->dwell_ms;
	transition->value.portal_state.pause_ms = mechanism->pause_ms;
	/* The host-certified elapsed duration is the transition travel fact.  A
	 * mechanism aggregate may be unspecified or may describe another panel. */
	transition->value.portal_state.travel_ms = (uint32_t)result->elapsed_ms;
	transition->value.portal_state.recovery_ms = mechanism->recovery_ms;
	transition->value.portal_state.source_blocked =
		(uint8_t)result->source_portal_blocked;
	transition->value.portal_state.destination_blocked =
		(uint8_t)result->destination_portal_blocked;
	return 1;
}

static int EmitTransport(uint32_t mechanism_index,
	const sg_rune_compact_builder_mover_result_t *result,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error)
{
	sg_rune_compact_mechanism_transition_t *transition;
	uint32_t axis;

	if (result == NULL || !AdvanceMoverTransition(output, cursor, error,
		mechanism_index, &transition))
		return 0;
	if (transition == NULL)
		return 1;
	InitializeTransition(transition, mechanism_index,
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT);
	transition->entry_cell = result->entry_cell;
	transition->exit_cell = result->exit_cell;
	transition->source_state = result->source_state;
	transition->destination_state = result->destination_state;
	transition->elapsed_ms = result->elapsed_ms;
	transition->value.transport.mover_model = result->mover_model;
	transition->value.transport.source_surface_ordinal =
		result->source_surface_ordinal;
	transition->value.transport.source_player_local =
		result->source_player_local;
	transition->value.transport.destination_player_local =
		result->destination_player_local;
	transition->value.transport.source_support_local =
		result->source_support_local;
	transition->value.transport.destination_support_local =
		result->destination_support_local;
	for (axis = 0U; axis < 3U; axis++)
	{
		uint32_t component;

		transition->value.transport.source_player_world_bits[axis] =
			FloatBits(result->source_player_world.value[axis]);
		transition->value.transport.destination_player_world_bits[axis] =
			FloatBits(result->destination_player_world.value[axis]);
		transition->value.transport.source_support_world_bits[axis] =
			FloatBits(result->source_support_world.value[axis]);
		transition->value.transport.destination_support_world_bits[axis] =
			FloatBits(result->destination_support_world.value[axis]);
		transition->value.transport.source_mover_origin_bits[axis] =
			FloatBits(result->source_mover_transform.origin[axis]);
		transition->value.transport.destination_mover_origin_bits[axis] =
			FloatBits(result->destination_mover_transform.origin[axis]);
		for (component = 0U; component < 3U; component++)
		{
			transition->value.transport.source_mover_axis_bits[axis][component] =
				FloatBits(result->source_mover_transform.axis[axis][component]);
			transition->value.transport.destination_mover_axis_bits[axis][component] =
				FloatBits(result->destination_mover_transform.axis[axis][component]);
		}
	}
	transition->value.transport.source_endpoint.entity_ordinal =
		result->source_endpoint_entity_ordinal;
	transition->value.transport.destination_endpoint.entity_ordinal =
		result->destination_endpoint_entity_ordinal;
	transition->value.transport.fanout_ordinal = result->route_fanout_ordinal;
	transition->value.transport.swept_static_clear = 1U;
	transition->value.transport.start_supported = 1U;
	transition->value.transport.end_supported = 1U;
	transition->value.transport.stance = (uint8_t)result->stance;
	return 1;
}

static int ProcessTransportCandidate(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t mechanism_index,
	uint32_t surface_index, uint32_t source_endpoint,
	uint32_t destination_endpoint, uint32_t fanout,
	sg_rune_compact_mechanism_authority_state_t source_state,
	sg_rune_compact_mechanism_authority_state_t destination_state,
	sg_rune_stance_t stance,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error)
{
	sg_rune_compact_builder_mover_result_t result;
	int applicable;

	if (!EvaluateCarriedSupport(builder, geometry, geometry_view, mechanism,
		entity, surface_index, source_endpoint, destination_endpoint, fanout,
		source_state, destination_state, stance,
		&result,
		&applicable, error, mechanism_index))
		return 0;
	return !applicable || EmitTransport(mechanism_index, &result, output,
		cursor, error);
}

/* Walk the finite canonical path-corner graph rather than assuming a single
 * train -> corner -> corner chain.  Nodes are enqueued once, but every
 * authenticated TARGET edge is emitted before its destination is deduplicated;
 * that preserves duplicate fanouts and the closing edge of a cycle. */
static int ProcessTrainSurface(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t mechanism_index,
	uint32_t surface_index, sg_rune_compact_mechanism_transition_t *output,
	uint32_t *cursor, sg_rune_compact_mechanisms_error_t *error)
{
	uint8_t *reachable = NULL;
	uint32_t *queue = NULL;
	uint32_t edge_index;
	uint32_t head = 0U;
	uint32_t tail = 0U;
	int valid = 0;

	if (semantics == NULL || mechanism == NULL || entity == NULL ||
		cursor == NULL || semantics->entity_count == 0U)
		return 0;
	if (!AllocateArray((void **)&reachable, semantics->entity_count,
		sizeof(*reachable), error) || !AllocateArray((void **)&queue,
		semantics->entity_count, sizeof(*queue), error))
		goto cleanup;
	for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[edge_index];

		if (edge->source != mechanism->source.entity_ordinal ||
			edge->kind != SG_MECH_EDGE_TARGET)
			continue;
		if (edge->destination >= semantics->entity_count ||
			semantics->entities[edge->destination].mechanism_role !=
				SG_MECH_NODE_PATH_CORNER)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, edge_index);
			goto cleanup;
		}
		if (reachable[edge->destination] == 0U)
		{
			if (tail == semantics->entity_count)
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY,
					edge->destination);
				goto cleanup;
			}
			reachable[edge->destination] = 1U;
			queue[tail++] = edge->destination;
		}
	}
	while (head < tail)
	{
		const uint32_t source_endpoint = queue[head++];

		for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge =
				&semantics->edges[edge_index];

			if (edge->source != source_endpoint ||
				edge->kind != SG_MECH_EDGE_TARGET)
				continue;
			if (edge->destination >= semantics->entity_count ||
				semantics->entities[edge->destination].mechanism_role !=
					SG_MECH_NODE_PATH_CORNER)
			{
				SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, edge_index);
				goto cleanup;
			}
			for (sg_rune_stance_t stance = SG_RUNE_STANCE_STANDING;
				stance < SG_RUNE_STANCE_COUNT; stance++)
				if (!ProcessTransportCandidate(builder, geometry, geometry_view,
					mechanism, entity, mechanism_index, surface_index,
					source_endpoint, edge->destination, edge->fanout_ordinal,
					mechanism->initial_state, mechanism->activated_state, stance,
					output, cursor, error))
					goto cleanup;
			if (reachable[edge->destination] == 0U)
			{
				if (tail == semantics->entity_count)
				{
					SetError(error,
						SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
						SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY,
						edge->destination);
					goto cleanup;
				}
				reachable[edge->destination] = 1U;
				queue[tail++] = edge->destination;
			}
		}
	}
	valid = 1;

cleanup:
	free(reachable);
	free(queue);
	return valid;
}

static int ProcessPortalGroup(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t mechanism_index,
	sg_rune_compact_mechanism_authority_state_t source_state,
	sg_rune_compact_mechanism_authority_state_t destination_state,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t root_entity_ordinal;
	uint32_t root_mechanism_index;
	uint32_t portal_index;
	int team_portal;

	if (semantics == NULL || mechanisms == NULL || mechanism == NULL ||
		entity == NULL || cursor == NULL ||
		!TeamRoot(semantics, entity->canonical_ordinal, &root_entity_ordinal,
			error, mechanism_index) ||
		!MechanismIndexForEntity(mechanisms, mechanism_count,
			root_entity_ordinal, &root_mechanism_index, error, mechanism_index))
		return 0;
	/* A team member never publishes a second selected portal record.  Its own
	 * controller/topology span remains independently materialized; the root's
	 * canonical TEAM closure is the complete group authority. */
	if (root_mechanism_index != mechanism_index)
		return 1;
	team_portal = TeamHasDirectMember(semantics, root_entity_ordinal);
	for (portal_index = 0U; portal_index < geometry_view->portal_count;
		portal_index++)
	{
		uint32_t member_ordinal;
		int emitted = 0;

		/* Entity then catalog order makes the selected physical panel stable.
		 * The host still combines every direct TEAM member's endpoint occupancy;
		 * this loop chooses only the immutable catalog-root provenance. */
		for (member_ordinal = 0U;
			member_ordinal < semantics->entity_count && !emitted;
			member_ordinal++)
		{
			const sg_bsp_entity_semantic_t *member_entity;
			const sg_rune_compact_mechanism_authority_t *member_mechanism;
			uint32_t member_mechanism_index;
			uint32_t member_root;
			uint32_t surface_index;

			if (!DirectTeamMember(semantics, member_ordinal,
				root_entity_ordinal))
				continue;
			if (!TeamRoot(semantics, member_ordinal, &member_root, error,
				mechanism_index) || member_root != root_entity_ordinal ||
				!MechanismIndexForEntity(mechanisms, mechanism_count,
					member_ordinal, &member_mechanism_index, error,
					mechanism_index))
				return 0;
			member_entity = &semantics->entities[member_ordinal];
			member_mechanism = &mechanisms[member_mechanism_index];
			if (!MoverAuthority(member_mechanism, member_entity) ||
				!MoverPublishesPortalState(member_mechanism->kind,
					member_entity))
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY,
					member_mechanism_index);
				return 0;
			}
			for (surface_index = 0U;
				surface_index < geometry_view->source_surface_count;
				surface_index++)
			{
				sg_rune_compact_builder_mover_result_t result;
				int applicable;

				if (!SourceSurfaceOwnedByMover(geometry_view, surface_index,
					member_entity->bsp_model))
					continue;
				if (!EvaluatePortalState(builder, geometry, geometry_view,
					mechanism, member_entity, team_portal,
					root_entity_ordinal, surface_index, portal_index, source_state,
					destination_state, &result,
					&applicable, error, mechanism_index))
					return 0;
				if (!applicable)
					continue;
				if (!EmitPortalState(mechanism, mechanism_index, &result,
					output, cursor, error))
					return 0;
				emitted = 1;
				break;
			}
		}
	}
	return 1;
}

static int ProcessMover(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_geometry_view_t *geometry_view,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t mechanism_index,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t surface_index;

	if (builder == NULL || geometry == NULL || geometry_view == NULL ||
		semantics == NULL || mechanism == NULL || entity == NULL || cursor == NULL)
		return 0;
	/* An auto-start train and a START_ON continuous rotator carry while they
	 * remain ACTIVE.  Every other pusher needs the authenticated activation
	 * edge. */
	if (mechanism->initial_state == mechanism->activated_state &&
		mechanism->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
		!ContinuousRotator(entity))
		return 1;
	if (MoverPublishesPortalState(mechanism->kind, entity))
	{
		if (!ProcessPortalGroup(builder, geometry, geometry_view, semantics,
				mechanisms, mechanism_count, mechanism, entity, mechanism_index,
				mechanism->initial_state, mechanism->activated_state,
				output, cursor, error))
			return 0;
		/* Every reversible linear or finite angular mover has an authenticated
		 * second state transition.  For a toggle it is the next activation;
		 * otherwise it is autonomous recovery after dwell. */
		if ((mechanism->flags &
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) == 0U &&
			(ToggleMechanism(mechanism) ||
				mechanism->activated_state != mechanism->reset_state))
		{
			const sg_rune_compact_mechanism_authority_state_t return_state =
				ToggleMechanism(mechanism)
					? mechanism->initial_state : mechanism->reset_state;

			if (!ProcessPortalGroup(builder, geometry, geometry_view, semantics,
					mechanisms, mechanism_count, mechanism, entity,
					mechanism_index, mechanism->activated_state, return_state,
					output, cursor, error))
				return 0;
		}
	}
	if (!MoverCarriesSupport(mechanism->kind, entity))
		return 1;
	for (surface_index = 0U; surface_index < geometry_view->source_surface_count;
		surface_index++)
	{
		if (!SourceSurfaceOwnedByMover(geometry_view, surface_index,
			entity->bsp_model))
			continue;
		if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
		{
			if (!ProcessTrainSurface(builder, geometry, geometry_view, semantics,
				mechanism, entity, mechanism_index, surface_index, output, cursor,
				error))
				return 0;
		}
		else {
			sg_rune_stance_t stance;
			const sg_rune_compact_mechanism_authority_state_t source_state =
				ContinuousRotator(entity) ?
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE :
					mechanism->initial_state;
			const sg_rune_compact_mechanism_authority_state_t destination_state =
				ContinuousRotator(entity) ?
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE :
					mechanism->activated_state;

			for (stance = SG_RUNE_STANCE_STANDING;
				stance < SG_RUNE_STANCE_COUNT; stance++)
				if (!ProcessTransportCandidate(builder, geometry, geometry_view,
					mechanism, entity, mechanism_index, surface_index,
					SG_RUNE_COMPACT_INDEX_NONE,
					SG_RUNE_COMPACT_INDEX_NONE, UINT32_MAX, source_state,
					destination_state, stance, output, cursor, error))
					return 0;
		}
	}
	return 1;
}

static int TeleportArrival(const sg_bsp_entity_semantic_t *destination,
	float arrival[3], sg_rune_q8_vec3_t *witness_out)
{
	uint32_t axis;

	if (destination == NULL || arrival == NULL || witness_out == NULL)
		return 0;
	/* teleporter_touch VectorCopies dest origin, then mutates only Z.  Keep
	 * that float assignment order so a source-authenticated destination stays
	 * bit-identical before compact quantization/localization. */
	for (axis = 0U; axis < 3U; axis++)
		arrival[axis] = destination->origin.value[axis];
	arrival[2] += 10.0f;
	for (axis = 0U; axis < 3U; axis++)
		if (!Q8FromFloat(arrival[axis], &witness_out->value[axis]))
			return 0;
	return 1;
}

static int TeleportPlacementValid(
	const sg_rune_compact_builder_owner_view_t *owner,
	const float arrival[3])
{
	sg_host_collision_pose_t pose;

	if (owner == NULL || owner->collision == NULL || arrival == NULL)
		return 0;
	memset(&pose, 0, sizeof(pose));
	return SG_HostCollisionClassifyPose(owner->collision, NULL, arrival,
		SG_RUNE_STANCE_STANDING, &pose) && pose.valid;
}

static int AppendTeleport(const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_model_t *model,
	const sg_bsp_entity_semantics_t *semantics,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	uint32_t mechanism_index, sg_rune_compact_mechanism_transition_t *output,
	uint32_t *cursor, sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t edge_index;

	for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[edge_index];
		const sg_bsp_entity_semantic_t *destination;
		float arrival[3];
		sg_rune_q8_vec3_t witness;
		sg_rune_compact_cell_index_t cell;
		sg_rune_compact_mechanism_transition_t *transition;

		if (!TeleportEdge(edge, semantics, mechanism->source.entity_ordinal))
			continue;
		destination = &semantics->entities[edge->destination];
		if (!TeleportArrival(destination, arrival, &witness) ||
			!TeleportPlacementValid(owner, arrival))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, edge->destination);
			return 0;
		}
		if (!Locate(model, &witness, &cell))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, edge->destination);
			return 0;
		}
		transition = &output[(*cursor)++];
		InitializeTransition(transition, mechanism_index,
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT);
		transition->entry_cell = mechanism->activation_cell;
		transition->exit_cell = cell;
		transition->source_state = mechanism->initial_state;
		transition->destination_state = mechanism->activated_state;
		transition->value.teleport.destination.entity_ordinal = edge->destination;
		transition->value.teleport.fanout_ordinal = edge->fanout_ordinal;
		transition->value.teleport.approach_witness = mechanism->activation_witness;
		transition->value.teleport.entry_witness = mechanism->activation_witness;
		transition->value.teleport.exit_witness = witness;
	}
	return 1;
}

static int PmoveStateFromWitness(const sg_rune_q8_vec3_t *witness,
	sg_rune_stance_validity_t stances, float gravity, const float velocity[3],
	sg_host_pmove_request_t *request)
{
	uint32_t axis;
	double gravity_value;

	if (witness == NULL || velocity == NULL || request == NULL ||
		!isfinite(gravity))
		return 0;
	gravity_value = nearbyint((double)gravity);
	if (!isfinite(gravity_value) || gravity_value < (double)SHRT_MIN ||
		gravity_value > (double)SHRT_MAX || (float)gravity_value != gravity)
		return 0;
	memset(request, 0, sizeof(*request));
	request->state.pm_type = PM_NORMAL;
	request->state.gravity = (short)gravity_value;
	if ((stances & SG_RUNE_STANCE_VALID_STANDING) == 0U)
	{
		if ((stances & SG_RUNE_STANCE_VALID_CROUCHING) == 0U)
			return 0;
		request->state.pm_flags = PMF_DUCKED;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		const float fixed_velocity = velocity[axis] * 8.0f;

		if (witness->value[axis] < SHRT_MIN ||
			witness->value[axis] > SHRT_MAX ||
			!isfinite(fixed_velocity) || fixed_velocity < (float)SHRT_MIN ||
			fixed_velocity > (float)SHRT_MAX)
			return 0;
		/* ClientThink stores the stock trigger's float velocity in the
		 * network state by this same truncating conversion. */
		request->state.origin[axis] = (short)witness->value[axis];
		request->state.velocity[axis] = (short)fixed_velocity;
	}
	request->previous_state = request->state;
	return 1;
}

static int Binary32Finite(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

static int StockTriggerPushMovedir(const sg_bsp_entity_semantic_t *entity,
	float movedir[3])
{
	const float *angles;
	float angle;
	float sr;
	float sp;
	float sy;
	float cr;
	float cp;
	float cy;

	if (entity == NULL || movedir == NULL)
		return 0;
	angles = entity->angles.value;
	memset(movedir, 0, sizeof(float) * 3U);
	if (!isfinite(angles[PITCH]) || !isfinite(angles[YAW]) ||
		!isfinite(angles[ROLL]))
		return 0;
	/* InitTrigger only calls G_SetMovedir when angles is not vec3_origin. */
	if (angles[PITCH] == 0.0f && angles[YAW] == 0.0f &&
		angles[ROLL] == 0.0f)
		return 1;
	if (angles[PITCH] == 0.0f && angles[YAW] == -1.0f &&
		angles[ROLL] == 0.0f)
	{
		movedir[2] = 1.0f;
		return 1;
	}
	if (angles[PITCH] == 0.0f && angles[YAW] == -2.0f &&
		angles[ROLL] == 0.0f)
	{
		movedir[2] = -1.0f;
		return 1;
	}
	/* This is the selected gameplay AngleVectors float operation order, not
	 * entity-semantic move_direction (which was derived through doubles). */
	angle = (float)(angles[YAW] * (M_PI * 2 / 360));
	sy = (float)sin((double)angle);
	cy = (float)cos((double)angle);
	angle = (float)(angles[PITCH] * (M_PI * 2 / 360));
	sp = (float)sin((double)angle);
	cp = (float)cos((double)angle);
	angle = (float)(angles[ROLL] * (M_PI * 2 / 360));
	sr = (float)sin((double)angle);
	cr = (float)cos((double)angle);
	movedir[0] = cp * cy;
	movedir[1] = cp * sy;
	movedir[2] = -sp;
	(void)sr;
	(void)cr;
	return Binary32Finite(FloatBits(movedir[0])) &&
		Binary32Finite(FloatBits(movedir[1])) &&
		Binary32Finite(FloatBits(movedir[2]));
}

static int PmoveStateHistoryContains(const pmove_state_t *history,
	uint32_t count, const pmove_state_t *state)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		if (memcmp(&history[index], state, sizeof(*state)) == 0)
			return 1;
	return 0;
}

static int PmoveStateHistoryAppend(pmove_state_t **history_out,
	uint32_t *count_out, uint32_t *capacity_out, const pmove_state_t *state,
	sg_rune_compact_mechanisms_error_t *error, uint32_t record)
{
	pmove_state_t *expanded;
	uint32_t capacity;
	size_t bytes;

	if (history_out == NULL || count_out == NULL || capacity_out == NULL ||
		state == NULL)
		return 0;
	if (*count_out == *capacity_out)
	{
		if (*capacity_out == 0U)
			capacity = 8U;
		else if (*capacity_out > UINT32_MAX / 2U)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, record);
			return 0;
		}
		else
			capacity = *capacity_out * 2U;
		if (sizeof(**history_out) > SIZE_MAX / (size_t)capacity)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, record);
			return 0;
		}
		bytes = (size_t)capacity * sizeof(**history_out);
		expanded = TransitionAllocate(bytes);
		if (expanded == NULL)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, record);
			return 0;
		}
		if (*count_out != 0U)
			memcpy(expanded, *history_out,
				(size_t)*count_out * sizeof(**history_out));
		free(*history_out);
		*history_out = expanded;
		*capacity_out = capacity;
	}
	(*history_out)[(*count_out)++] = *state;
	return 1;
}

static int AppendPush(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t mechanism_index,
	sg_rune_compact_mechanism_transition_t *output, uint32_t *cursor,
	sg_rune_compact_mechanisms_error_t *error)
{
	const float gravity = BitsFloat(owner->identity.physics.gravity_bits);
	const float speed = entity->speed == 0.0f ? 1000.0f : entity->speed;
	const float scale = speed * 10.0f;
	float movedir[3];
	float launch[3];
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error;
	sg_host_law_result_t law_result;
	pmove_state_t *history = NULL;
	uint32_t history_count = 0U;
	uint32_t history_capacity = 0U;
	uint32_t flight_ms = 0U;
	sg_rune_q8_vec3_t landing;
	sg_rune_compact_cell_index_t exit_cell;
	sg_rune_compact_mechanism_transition_t *transition;
	uint32_t axis;

	if (!Binary32Canonical(owner->identity.physics.gravity_bits) ||
		gravity < 0.0f || owner->identity.physics.frame_ms == 0U ||
		owner->identity.physics.substep_ms == 0U ||
		owner->identity.physics.frame_ms % owner->identity.physics.substep_ms !=
			0U || !isfinite(speed) || !isfinite(scale) ||
		!StockTriggerPushMovedir(entity, movedir))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		/* VectorScale(movedir, speed * 10, velocity) preserves this operand
		 * order and signed zero from G_SetMovedir/AngleVectors. */
		launch[axis] = movedir[axis] * scale;
		if (!Binary32Finite(FloatBits(launch[axis])))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
				mechanism_index);
			return 0;
		}
	}
	if (!PmoveStateFromWitness(&mechanism->activation_witness,
		model->cells[mechanism->activation_cell.value].valid_stances, gravity,
		launch, &request))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	if (!PmoveStateHistoryAppend(&history, &history_count, &history_capacity,
		&request.state, error, mechanism_index))
		return 0;
	for (;;)
	{
		memset(&result, 0, sizeof(result));
		pmove_error = SG_HOST_PMOVE_ERROR_NONE;
		law_result = SG_RuneCompactBuilderOwnerPmove(builder, NULL, &request,
			&result, &pmove_error);
		if (law_result.status != SG_HOST_LAW_OK ||
			pmove_error != SG_HOST_PMOVE_ERROR_NONE ||
			result.elapsed_ms != owner->identity.physics.frame_ms ||
			result.evaluated_steps != owner->identity.physics.frame_ms /
				owner->identity.physics.substep_ms ||
			result.physics_abi_id != owner->identity.physics_abi_id ||
			result.gravity_law_id != 0U || FloatBits(result.gravity) !=
				owner->identity.physics.gravity_bits ||
			result.state.gravity != request.state.gravity ||
			result.state.pm_type != PM_NORMAL)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			goto failure;
		}
		if (result.elapsed_ms > UINT32_MAX - flight_ms)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			goto failure;
		}
		flight_ms += result.elapsed_ms;
		for (axis = 0U; axis < 3U; axis++)
		{
			landing.value[axis] = result.state.origin[axis];
			if (result.origin[axis] != (float)landing.value[axis] * 0.125f)
			{
				SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					mechanism_index);
				goto failure;
			}
		}
		if (result.grounded)
			break;
		/* Pmove is deterministic for one authenticated static map and finite
		 * network state.  A repeated airborne state is the exact terminal proof
		 * that no grounded landing will emerge; allocation exhaustion fails
		 * loudly instead of imposing a traversal budget. */
		if (PmoveStateHistoryContains(history, history_count, &result.state))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
			goto failure;
		}
		if (!PmoveStateHistoryAppend(&history, &history_count, &history_capacity,
			&result.state, error, mechanism_index))
			goto failure;
		request.state = result.state;
		request.previous_state = result.state;
	}
	if (!Locate(model, &landing, &exit_cell))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, mechanism_index);
		goto failure;
	}
	transition = &output[(*cursor)++];
	InitializeTransition(transition, mechanism_index,
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH);
	transition->entry_cell = mechanism->activation_cell;
	transition->exit_cell = exit_cell;
	transition->source_state = mechanism->initial_state;
	transition->destination_state = mechanism->activated_state;
	transition->elapsed_ms = flight_ms;
	transition->value.push.approach_witness = mechanism->activation_witness;
	transition->value.push.entry_witness = mechanism->activation_witness;
	transition->value.push.exit_witness = landing;
	for (axis = 0U; axis < 3U; axis++)
		transition->value.push.launch_velocity_bits[axis] =
			FloatBits(launch[axis]);
	transition->value.push.gravity_bits = owner->identity.physics.gravity_bits;
	transition->value.push.flight_ms = flight_ms;
	free(history);
	return 1;

failure:
	free(history);
	return 0;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareU64(uint64_t left, uint64_t right)
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

static int TransitionCompare(const void *left_value, const void *right_value)
{
	const sg_rune_compact_mechanism_transition_t *left = left_value;
	const sg_rune_compact_mechanism_transition_t *right = right_value;
	int comparison;

	comparison = CompareU32(left->mechanism, right->mechanism);
	if (comparison != 0)
		return comparison;

	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	if (left->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
	{
		if (left->value.portal_state.portal.value !=
			right->value.portal_state.portal.value)
			return left->value.portal_state.portal.value <
				right->value.portal_state.portal.value ? -1 : 1;
	}
	if (left->entry_cell.value != right->entry_cell.value)
		return left->entry_cell.value < right->entry_cell.value ? -1 : 1;
	if (left->exit_cell.value != right->exit_cell.value)
		return left->exit_cell.value < right->exit_cell.value ? -1 : 1;
	comparison = CompareU32((uint32_t)left->source_state,
		(uint32_t)right->source_state);
	if (comparison != 0)
		return comparison;
	comparison = CompareU32((uint32_t)left->destination_state,
		(uint32_t)right->destination_state);
	if (comparison != 0)
		return comparison;
	comparison = CompareU64(left->elapsed_ms, right->elapsed_ms);
	if (comparison != 0)
		return comparison;
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
	{
		const sg_rune_compact_mechanism_portal_state_t *left_state =
			&left->value.portal_state;
		const sg_rune_compact_mechanism_portal_state_t *right_state =
			&right->value.portal_state;

		comparison = CompareU32(left_state->mover_model,
			right_state->mover_model);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->delay_ms, right_state->delay_ms);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->dwell_ms, right_state->dwell_ms);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->pause_ms, right_state->pause_ms);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->travel_ms,
			right_state->travel_ms);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->recovery_ms,
			right_state->recovery_ms);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->source_blocked,
			right_state->source_blocked);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->destination_blocked,
			right_state->destination_blocked);
		if (comparison != 0)
			return comparison;
		comparison = CompareU32(left_state->reserved[0],
			right_state->reserved[0]);
		if (comparison != 0)
			return comparison;
		return CompareU32(left_state->reserved[1],
			right_state->reserved[1]);
	}
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
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
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
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
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		comparison = CompareU32(left->value.transport.mover_model,
			right->value.transport.mover_model);
		if (comparison == 0)
			comparison = CompareU32(left->value.transport.source_surface_ordinal,
				right->value.transport.source_surface_ordinal);
		if (comparison == 0)
			comparison = CompareQ8Vec3(
				&left->value.transport.source_player_local,
				&right->value.transport.source_player_local);
		if (comparison == 0)
			comparison = CompareQ8Vec3(
				&left->value.transport.destination_player_local,
				&right->value.transport.destination_player_local);
		if (comparison == 0)
			comparison = CompareQ8Vec3(
				&left->value.transport.source_support_local,
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
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

void SG_RuneCompactMechanismTransitionsRelease(
	sg_rune_compact_mechanism_transitions_result_t *result)
{
	if (result == NULL)
		return;
	free(result->transitions);
	free(result->spans);
	memset(result, 0, sizeof(*result));
}

int SG_RuneCompactMechanismTransitionsValidate(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_transition_t *transitions,
	uint32_t transition_count,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	sg_rune_compact_mechanism_transitions_result_t derived;
	uint32_t index;
	int valid = 0;

	memset(&derived, 0, sizeof(derived));
	if (!SG_RuneCompactMechanismTransitionsDerive(builder, geometry, mechanisms,
		mechanism_count, &derived, error_out))
		goto cleanup;
	if (!ArrayShapeValid(transitions, transition_count) ||
		transition_count != derived.transition_count)
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, transition_count);
		goto cleanup;
	}
	for (index = 0U; index < mechanism_count; index++)
		if (mechanisms[index].transitions.first != derived.spans[index].first ||
			mechanisms[index].transitions.count != derived.spans[index].count)
		{
			SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
			goto cleanup;
		}
	if (transition_count != 0U && memcmp(transitions, derived.transitions,
		(size_t)transition_count * sizeof(*transitions)) != 0)
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, 0U);
		goto cleanup;
	}
	valid = 1;

cleanup:
	SG_RuneCompactMechanismTransitionsRelease(&derived);
	return valid;
}

int SG_RuneCompactMechanismTransitionsDerive(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	sg_rune_compact_mechanism_transitions_result_t *result_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	sg_rune_compact_builder_owner_view_t owner;
	sg_rune_compact_builder_owner_view_t current_owner;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_model_t model;
	sg_rune_compact_mechanism_transitions_result_t result;
	uint32_t total = 0U;
	uint32_t mechanism_index;
	int success = 0;

	memset(&owner, 0, sizeof(owner));
	memset(&current_owner, 0, sizeof(current_owner));
	memset(&geometry_view, 0, sizeof(geometry_view));
	memset(&model, 0, sizeof(model));
	memset(&result, 0, sizeof(result));
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	if (builder == NULL || geometry == NULL || result_out == NULL ||
		!ArrayShapeValid(mechanisms, mechanism_count) ||
		!SG_RuneCompactBuilderOwnerRead(builder, &owner) ||
		!SG_RuneCompactGeometryRead(geometry, &geometry_view))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	if (!SG_RuneCompactIdentityMatches(&owner.identity, &geometry_view.identity))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER, 0U);
		goto cleanup;
	}
	if (!SemanticShapeValid(&owner, error_out) ||
		!GeometryShapeValid(&geometry_view, &owner.identity, error_out))
		goto cleanup;
	GeometryModel(&geometry_view, &model);
	for (mechanism_index = 0U; mechanism_index < mechanism_count;
		mechanism_index++)
	{
		uint32_t count;

		if (!MechanismAuthorityValid(&mechanisms[mechanism_index],
				owner.entity_semantics, &model, error_out, mechanism_index) ||
			!CountTransitions(builder, geometry, &geometry_view,
				owner.entity_semantics, mechanisms, mechanism_count,
				&mechanisms[mechanism_index], mechanism_index, &count,
				error_out) ||
			!AddCount(&total, count, error_out, mechanism_index))
			goto cleanup;
	}
	if (!AllocateArray((void **)&result.spans, mechanism_count,
			sizeof(*result.spans), error_out) ||
		!AllocateArray((void **)&result.transitions, total,
			sizeof(*result.transitions), error_out))
		goto cleanup;
	if (total != 0U && result.transitions == NULL)
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, total);
		goto cleanup;
	}
	result.mechanism_count = mechanism_count;
	result.transition_count = total;
	total = 0U;
	for (mechanism_index = 0U; mechanism_index < mechanism_count;
		mechanism_index++)
	{
		const sg_rune_compact_mechanism_authority_t *mechanism =
			&mechanisms[mechanism_index];
		const sg_bsp_entity_semantic_t *entity =
			&owner.entity_semantics->entities[
				mechanism->source.entity_ordinal];
		const uint32_t first = total;

		if (MoverAuthority(mechanism, entity) &&
			!ProcessMover(builder, geometry, &geometry_view,
				owner.entity_semantics, mechanisms, mechanism_count, mechanism,
				entity, mechanism_index,
				result.transitions, &total, error_out))
		{
			if (error_out == NULL || error_out->code ==
				SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE)
				SetError(error_out,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					mechanism_index);
			goto cleanup;
		}
		else if (mechanism->kind ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT &&
			!AppendTeleport(&owner, &model, owner.entity_semantics, mechanism,
				mechanism_index, result.transitions, &total, error_out))
			goto cleanup;
		else if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH &&
			!AppendPush(builder, &owner, &model, mechanism, entity,
				mechanism_index, result.transitions, &total, error_out))
			goto cleanup;
		result.spans[mechanism_index].first = first;
		result.spans[mechanism_index].count = total - first;
		if (result.spans[mechanism_index].count > 1U)
		{
			if (result.transitions == NULL)
			{
				SetError(error_out,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, total);
				goto cleanup;
			}
			qsort(&result.transitions[first],
				(size_t)result.spans[mechanism_index].count,
				sizeof(*result.transitions), TransitionCompare);
		}
	}
	if (total != result.transition_count)
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, total);
		goto cleanup;
	}
	if (!TimingAuthorityMatches(mechanisms, mechanism_count, &result,
			error_out))
		goto cleanup;
	/* All transition storage remains private until the builder is read again.
	 * This catches an authority identity revocation during a teleport-only (and
	 * therefore otherwise host-Pmove-free) derivation before publication. */
	if (!SG_RuneCompactBuilderOwnerRead(builder, &current_owner) ||
		!SG_RuneCompactIdentityMatches(&current_owner.identity, &owner.identity) ||
		!SG_RuneCompactIdentityMatches(&current_owner.identity,
			&geometry_view.identity))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER, 0U);
		goto cleanup;
	}
	*result_out = result;
	memset(&result, 0, sizeof(result));
	success = 1;

cleanup:
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return success;
}
