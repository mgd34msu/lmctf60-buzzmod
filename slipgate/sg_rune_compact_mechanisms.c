#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_mechanisms_build.h"
#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_mechanisms_transitions.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SG_RUNE_COMPACT_MECHANISMS_STATE UINT64_C(0x4d4543484f574e52)

struct sg_rune_compact_mechanisms_s
{
	uint64_t state;
	uint64_t state_inverse;
	const struct sg_rune_compact_mechanisms_s *self;
	sg_rune_compact_mechanism_authority_t *owned_mechanisms;
	sg_rune_compact_mechanism_controller_t *owned_controllers;
	sg_rune_compact_mechanism_topology_edge_t *owned_topology_edges;
	sg_rune_compact_mechanism_transition_t *owned_transitions;
	sg_rune_compact_mechanisms_view_t view;
};

#if defined(SG_RUNE_COMPACT_MECHANISMS_TESTING)
static size_t test_fail_after = SIZE_MAX;
static size_t test_allocation_count;

void SG_RuneCompactMechanismsTestFailAfter(size_t allocation)
{
	test_fail_after = allocation;
	test_allocation_count = 0U;
}

size_t SG_RuneCompactMechanismsTestAllocationCount(void)
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

static void *OwnerAllocate(size_t bytes)
{
#if defined(SG_RUNE_COMPACT_MECHANISMS_TESTING)
	if (test_allocation_count == test_fail_after)
	{
		test_allocation_count++;
		return NULL;
	}
	test_allocation_count++;
#endif
	return malloc(bytes);
}

static int SizeMultiply(size_t count, size_t width, size_t *bytes_out)
{
	if (bytes_out == NULL || (width != 0U && count > SIZE_MAX / width))
		return 0;
	*bytes_out = count * width;
	return 1;
}

static int ArrayShapeValid(const void *records, uint32_t count)
{
	return (records != NULL) == (count != 0U);
}

static int BoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PointInBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int LocationZero(const sg_rune_q8_vec3_t *witness,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (witness->value[axis] != 0 || bounds->mins.value[axis] != 0 ||
			bounds->maxs.value[axis] != 0)
			return 0;
	return 1;
}

static int Binary32Finite(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

static int Binary32Canonical(uint32_t bits)
{
	return Binary32Finite(bits) && bits != UINT32_C(0x80000000);
}

static int EntityValid(sg_rune_compact_mechanism_entity_ref_t reference,
	const sg_rune_compact_identity_t *identity)
{
	return reference.entity_ordinal < identity->source_counts.entity_count;
}

static int FiniteAngularDoorAuthority(
	const sg_rune_compact_builder_owner_view_t *builder_owner,
	const sg_rune_compact_mechanism_authority_t *mechanism)
{
	const sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *entity;
	uint32_t ordinal;

	if (builder_owner == NULL || mechanism == NULL ||
		mechanism->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR)
		return 0;
	semantics = builder_owner->entity_semantics;
	ordinal = mechanism->source.entity_ordinal;
	if (semantics == NULL || semantics->entities == NULL ||
		ordinal >= semantics->entity_count)
		return 0;
	entity = &semantics->entities[ordinal];
	return entity->source_set_identity == semantics->source_set_identity &&
		entity->canonical_ordinal == ordinal &&
		entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR &&
		SG_BspEntitySemanticHasFiniteAngularDoor(entity);
}

static int ContinuousRotatorAuthority(
	const sg_rune_compact_builder_owner_view_t *builder_owner,
	const sg_rune_compact_mechanism_authority_t *mechanism)
{
	const sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *entity;
	uint32_t ordinal;

	if (builder_owner == NULL || mechanism == NULL ||
		mechanism->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR)
		return 0;
	semantics = builder_owner->entity_semantics;
	ordinal = mechanism->source.entity_ordinal;
	if (semantics == NULL || semantics->entities == NULL ||
		ordinal >= semantics->entity_count)
		return 0;
	entity = &semantics->entities[ordinal];
	return entity->source_set_identity == semantics->source_set_identity &&
		entity->canonical_ordinal == ordinal &&
		entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR &&
		entity->angular_mover.kind ==
			SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
}

static int PortalStateAuthority(
	const sg_rune_compact_builder_owner_view_t *builder_owner,
	const sg_rune_compact_mechanism_authority_t *mechanism)
{
	return mechanism != NULL &&
		(mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
		 mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
		 FiniteAngularDoorAuthority(builder_owner, mechanism));
}

static int RequiredItemValid(uint32_t offset,
	const sg_bsp_entity_semantics_t *semantics)
{
	return semantics != NULL && offset != SG_BSP_ENTITY_STRING_NONE &&
		offset < semantics->string_bytes && semantics->strings != NULL &&
		semantics->strings[offset] != '\0' &&
		(offset == 0U || semantics->strings[offset - 1U] == '\0');
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

static int PickupLandmark(const sg_bsp_entity_semantic_t *entity)
{
	if (entity == NULL || (entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) == 0U)
		return 0;
	return entity->landmark_kind == SG_RUNE_LANDMARK_FLAG_STAND ||
		entity->landmark_kind == SG_RUNE_LANDMARK_ITEM ||
		entity->landmark_kind == SG_RUNE_LANDMARK_WEAPON ||
		entity->landmark_kind == SG_RUNE_LANDMARK_ARMOR ||
		entity->landmark_kind == SG_RUNE_LANDMARK_HEALTH ||
		entity->landmark_kind == SG_RUNE_LANDMARK_POWERUP;
}

static sg_rune_compact_mechanism_activation_mask_t ControllerActivationMask(
	const sg_bsp_entity_semantic_t *entity)
{
	sg_rune_compact_mechanism_activation_mask_t mask = ActivationMask(entity);

	if (PickupLandmark(entity))
		mask |= SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	return mask;
}

static sg_rune_compact_mechanism_controller_spatiality_t
ControllerSpatiality(const sg_bsp_entity_semantic_t *entity)
{
	if (PickupLandmark(entity))
		return SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	switch (entity->mechanism_role) {
	case SG_MECH_NODE_TRIGGER:
	case SG_MECH_NODE_BUTTON:
	case SG_MECH_NODE_AUTO_DOOR_TRIGGER:
	case SG_MECH_NODE_PLATFORM_TRIGGER:
	case SG_MECH_NODE_TELEPORT_TRIGGER:
	case SG_MECH_NODE_OTHER_TRIGGER:
	case SG_MECH_NODE_TRIGGER_HURT:
		return SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	default:
		return SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	}
}

static int ToggleAuthority(const sg_bsp_entity_semantic_t *entity)
{
	return entity->mechanism_kind == SG_RUNE_MECHANISM_DOOR ?
		(entity->spawnflags & UINT32_C(32)) != 0U :
		SG_BspEntitySemanticHasFiniteAngularDoor(entity) &&
		(entity->angular_mover.flags &
			SG_BSP_ENTITY_ANGULAR_MOVER_TOGGLE) != 0U;
}

static int ToggleMechanism(
	const sg_rune_compact_mechanism_authority_t *mechanism)
{
	return (mechanism->flags &
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) == 0U &&
		mechanism->initial_state != mechanism->activated_state &&
		mechanism->reset_state == mechanism->activated_state;
}

static int CellValid(sg_rune_compact_cell_index_t cell,
	const sg_rune_compact_geometry_view_t *geometry)
{
	return cell.value < geometry->cell_count;
}

static int PointInCell(const sg_rune_q8_vec3_t *point,
	sg_rune_compact_cell_index_t cell,
	const sg_rune_compact_geometry_view_t *geometry)
{
	return CellValid(cell, geometry) && geometry->cells != NULL &&
		PointInBounds(point, &geometry->cells[cell.value].bounds);
}

static int SpanValid(sg_rune_compact_mechanism_span_t span,
	uint32_t expected_first, uint32_t total, uint32_t *next_out)
{
	if (span.first != expected_first || span.first > total ||
		span.count > total - span.first)
		return 0;
	*next_out = span.first + span.count;
	return 1;
}

static int ControllerCompare(
	const sg_rune_compact_mechanism_controller_t *left,
	const sg_rune_compact_mechanism_controller_t *right)
{
	uint32_t axis;

	if (left->controller.entity_ordinal != right->controller.entity_ordinal)
		return left->controller.entity_ordinal < right->controller.entity_ordinal ? -1 : 1;
	if (left->topology_edge != right->topology_edge)
		return left->topology_edge < right->topology_edge ? -1 : 1;
	if (left->spatiality != right->spatiality)
		return left->spatiality < right->spatiality ? -1 : 1;
	if (left->activation_cell.value != right->activation_cell.value)
		return left->activation_cell.value < right->activation_cell.value ? -1 : 1;
	for (axis = 0U; axis < 3U; axis++)
		if (left->activation_witness.value[axis] !=
			right->activation_witness.value[axis])
			return left->activation_witness.value[axis] <
				right->activation_witness.value[axis] ? -1 : 1;
	return 0;
}

static int MoverTransition(
	const sg_rune_compact_mechanism_transition_t *transition)
{
	return transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
		transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
}

static int TimingAggregateValid(
	const sg_rune_compact_mechanisms_candidate_t *candidate,
	const sg_rune_compact_mechanism_authority_t *mechanism,
	uint32_t mechanism_index, sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t travel = SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
	uint32_t recovery = SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
	int uniform_travel = 1;
	int uniform_recovery = 1;
	int saw_travel = 0;
	int saw_recovery = 0;
	uint32_t index;

	for (index = mechanism->transitions.first;
		index < mechanism->transitions.first + mechanism->transitions.count;
		index++)
	{
		const sg_rune_compact_mechanism_transition_t *transition =
			&candidate->transitions[index];
		const uint32_t elapsed = (uint32_t)transition->elapsed_ms;

		if (!MoverTransition(transition))
			continue;
		if (transition->elapsed_ms == 0U || transition->elapsed_ms > UINT32_MAX)
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
	if (mechanism->travel_ms != (saw_travel && uniform_travel ? travel :
		SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED) ||
		mechanism->recovery_ms != (saw_recovery && uniform_recovery ? recovery :
		SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, mechanism_index);
		return 0;
	}
	return 1;
}

static int TopologyCompare(
	const sg_rune_compact_mechanism_topology_edge_t *left,
	const sg_rune_compact_mechanism_topology_edge_t *right)
{
	if (left->source.entity_ordinal != right->source.entity_ordinal)
		return left->source.entity_ordinal < right->source.entity_ordinal ? -1 : 1;
	if (left->destination.entity_ordinal != right->destination.entity_ordinal)
		return left->destination.entity_ordinal < right->destination.entity_ordinal ? -1 : 1;
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	if (left->fanout_ordinal != right->fanout_ordinal)
		return left->fanout_ordinal < right->fanout_ordinal ? -1 : 1;
	return 0;
}

static int ValidateMechanism(
	const sg_rune_compact_mechanisms_candidate_t *candidate,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_builder_owner_view_t *builder_owner,
	const sg_rune_compact_geometry_view_t *geometry, uint32_t mechanism_index,
	uint32_t *next_controller, uint32_t *next_topology,
	uint32_t *next_transition, sg_rune_compact_mechanisms_error_t *error)
{
	const sg_rune_compact_mechanism_authority_t *mechanism =
		&candidate->mechanisms[mechanism_index];
	const sg_bsp_entity_semantic_t *source_entity;
	uint32_t index;

	if (!EntityValid(mechanism->source, &builder->identity) ||
		(mechanism_index != 0U && candidate->mechanisms[mechanism_index - 1U]
			.source.entity_ordinal >= mechanism->source.entity_ordinal))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
		return 0;
	}
	if (builder_owner->entity_semantics == NULL ||
		builder_owner->entity_semantics->entities == NULL ||
		mechanism->source.entity_ordinal >=
			builder_owner->entity_semantics->entity_count)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
		return 0;
	}
	source_entity = &builder_owner->entity_semantics->entities[
		mechanism->source.entity_ordinal];
	if (mechanism->kind < SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
		mechanism->kind >= SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT ||
		mechanism->activation == 0U ||
		(mechanism->activation &
			(sg_rune_compact_mechanism_activation_mask_t)
			~(sg_rune_compact_mechanism_activation_mask_t)
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
		mechanism->initial_state >=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
		mechanism->activated_state >=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
		mechanism->reset_state >=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
		(mechanism->flags &
			(sg_rune_compact_mechanism_authority_flags_t)
			~(sg_rune_compact_mechanism_authority_flags_t)
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN) != 0U ||
		((mechanism->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) != 0U &&
			mechanism->health <= 0) ||
		((mechanism->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) == 0U &&
			mechanism->health != 0) ||
		((mechanism->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) != 0U &&
			!RequiredItemValid(mechanism->required_item,
				builder_owner->entity_semantics)) ||
		((mechanism->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) == 0U &&
			mechanism->required_item != SG_BSP_ENTITY_STRING_NONE) ||
		ToggleMechanism(mechanism) != ToggleAuthority(source_entity))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
		return 0;
	}
	if (!PointInCell(&mechanism->activation_witness,
			mechanism->activation_cell, geometry) ||
		!BoundsValid(&mechanism->activation_bounds) ||
		!PointInBounds(&mechanism->activation_witness,
			&mechanism->activation_bounds))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, mechanism_index);
		return 0;
	}
	if (!SpanValid(mechanism->controllers, *next_controller,
			candidate->controller_count, next_controller) ||
		!SpanValid(mechanism->topology, *next_topology,
			candidate->topology_edge_count, next_topology) ||
		!SpanValid(mechanism->transitions, *next_transition,
			candidate->transition_count, next_transition))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, mechanism_index);
		return 0;
	}
	for (index = mechanism->controllers.first; index < *next_controller; index++)
	{
		const sg_rune_compact_mechanism_controller_t *controller =
			&candidate->controllers[index];
		const sg_rune_compact_mechanism_topology_edge_t *edge;

		if (controller->mechanism != mechanism_index ||
			!EntityValid(controller->controller, &builder->identity) ||
			controller->topology_edge < mechanism->topology.first ||
			controller->topology_edge >= *next_topology)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			return 0;
		}
		edge = &candidate->topology_edges[controller->topology_edge];
		if (edge->source.entity_ordinal !=
				controller->controller.entity_ordinal ||
			edge->destination.entity_ordinal !=
				mechanism->source.entity_ordinal ||
			(edge->kind != SG_MECH_EDGE_TARGET &&
			 edge->kind != SG_MECH_EDGE_PATH_TARGET))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE,
				controller->topology_edge);
			return 0;
		}
		if (builder_owner->entity_semantics == NULL ||
			builder_owner->entity_semantics->entities == NULL ||
			controller->controller.entity_ordinal >=
				builder_owner->entity_semantics->entity_count)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			return 0;
		}
		{
			const sg_bsp_entity_semantic_t *source =
				&builder_owner->entity_semantics->entities[
					controller->controller.entity_ordinal];
			const sg_rune_compact_mechanism_controller_flags_t expected_flags =
				isfinite(source->dwell_ms) && source->dwell_ms < 0.0f
					? SG_RUNE_COMPACT_MECHANISM_CONTROLLER_ONE_SHOT : 0U;

			if (controller->activation == 0U ||
				(controller->activation &
					(sg_rune_compact_mechanism_activation_mask_t)
					~(sg_rune_compact_mechanism_activation_mask_t)
					SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
				controller->activation != ControllerActivationMask(source) ||
				controller->damage != source->damage ||
				controller->health != source->health ||
				controller->required_item != source->required_item ||
				controller->flags != expected_flags ||
				controller->spatiality != ControllerSpatiality(source) ||
				controller->spatiality >=
					SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
				controller->reserved[0] != 0U ||
				controller->reserved[1] != 0U ||
				controller->reserved[2] != 0U ||
				((controller->activation &
					SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) != 0U &&
					controller->health <= 0) ||
				((controller->activation &
					SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) == 0U &&
					controller->health != 0) ||
				((controller->activation &
					SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) != 0U &&
					!RequiredItemValid(controller->required_item,
						builder_owner->entity_semantics)) ||
				((controller->activation &
					SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) == 0U &&
					controller->required_item != SG_BSP_ENTITY_STRING_NONE))
			{
				SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
				return 0;
			}
		}
		if (controller->spatiality ==
				SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
			(controller->activation_cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
			 !LocationZero(&controller->activation_witness,
				&controller->activation_bounds)) :
			(!PointInCell(&controller->activation_witness,
				controller->activation_cell, geometry) ||
			 !BoundsValid(&controller->activation_bounds) ||
			 !PointInBounds(&controller->activation_witness,
				&controller->activation_bounds)))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, index);
			return 0;
		}
		if (index != mechanism->controllers.first &&
			ControllerCompare(&candidate->controllers[index - 1U],
				controller) >= 0)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_AMBIGUOUS_BINDING,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			return 0;
		}
	}
	for (index = mechanism->topology.first; index < *next_topology; index++)
	{
		const sg_rune_compact_mechanism_topology_edge_t *edge =
			&candidate->topology_edges[index];

		if (!EntityValid(edge->source, &builder->identity) ||
			!EntityValid(edge->destination, &builder->identity) ||
			edge->kind < SG_MECH_EDGE_TARGET ||
			edge->kind > SG_MECH_EDGE_ROUTE_TARGET)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, index);
			return 0;
		}
		if (index != mechanism->topology.first &&
			TopologyCompare(&candidate->topology_edges[index - 1U], edge) >= 0)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_AMBIGUOUS_BINDING,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, index);
			return 0;
		}
	}
	return 1;
}

static int ValidateTransition(
	const sg_rune_compact_mechanisms_candidate_t *candidate,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_builder_owner_view_t *builder_owner,
	const sg_rune_compact_geometry_view_t *geometry, uint32_t index,
	sg_rune_compact_mechanisms_error_t *error)
{
	const sg_rune_compact_mechanism_transition_t *transition =
		&candidate->transitions[index];
	const sg_rune_compact_mechanism_authority_t *mechanism;
	uint32_t axis;
	uint32_t component;

	if (transition->mechanism >= candidate->mechanism_count)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
		return 0;
	}
	mechanism = &candidate->mechanisms[transition->mechanism];
	if (index < mechanism->transitions.first ||
		index >= mechanism->transitions.first + mechanism->transitions.count ||
		transition->kind >= SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT ||
		transition->entry_cell.value >= geometry->cell_count ||
		transition->exit_cell.value >= geometry->cell_count ||
		transition->source_state >=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
		transition->destination_state >=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
		return 0;
	}
	if (transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
	{
		const sg_rune_compact_mechanism_portal_state_t *state =
			&transition->value.portal_state;
		const sg_rune_compact_portal_t *portal;
		uint32_t negative_cell;
		uint32_t positive_cell;

		if (!PortalStateAuthority(builder_owner, mechanism) ||
			transition->source_state == transition->destination_state ||
			transition->elapsed_ms == 0U || transition->elapsed_ms > UINT32_MAX ||
			state->portal.value >= geometry->portal_count ||
			state->mover_model >= builder->identity.source_counts.model_count ||
			state->delay_ms != mechanism->delay_ms ||
			state->dwell_ms != mechanism->dwell_ms ||
			state->pause_ms != mechanism->pause_ms ||
			state->travel_ms != (uint32_t)transition->elapsed_ms ||
			state->recovery_ms != mechanism->recovery_ms ||
			state->source_blocked > 1U || state->destination_blocked > 1U ||
			state->source_blocked == state->destination_blocked ||
			state->reserved[0] != 0U || state->reserved[1] != 0U)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, index);
			return 0;
		}
		portal = &geometry->portals[state->portal.value];
		if (geometry->incidences == NULL ||
			portal->negative_incidence.value >= geometry->incidence_count ||
			portal->positive_incidence.value >= geometry->incidence_count)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, index);
			return 0;
		}
		negative_cell = geometry->incidences[
			portal->negative_incidence.value].cell.value;
		positive_cell = geometry->incidences[
			portal->positive_incidence.value].cell.value;
		if (!((transition->entry_cell.value == negative_cell &&
				transition->exit_cell.value == positive_cell) ||
			(transition->entry_cell.value == positive_cell &&
				transition->exit_cell.value == negative_cell)))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL, index);
			return 0;
		}
	}
	else if (transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
	{
		const sg_rune_compact_mechanism_teleport_t *teleport =
			&transition->value.teleport;

		if (mechanism->kind !=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT ||
			!EntityValid(teleport->destination, &builder->identity) ||
			teleport->fanout_ordinal == SG_RUNE_COMPACT_INDEX_NONE ||
			!PointInCell(&teleport->approach_witness,
				transition->entry_cell, geometry) ||
			!PointInCell(&teleport->entry_witness,
				transition->entry_cell, geometry) ||
			!PointInCell(&teleport->exit_witness,
				transition->exit_cell, geometry))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
			if (teleport->arrival_velocity_bits[axis] != UINT32_C(0))
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					index);
				return 0;
			}
	}
	else if (transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH)
	{
		const sg_rune_compact_mechanism_push_t *push =
			&transition->value.push;

		if (mechanism->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH ||
			!PointInCell(&push->approach_witness, transition->entry_cell,
				geometry) ||
			!PointInCell(&push->entry_witness, transition->entry_cell,
				geometry) ||
			!PointInCell(&push->exit_witness, transition->exit_cell,
				geometry) ||
			push->flight_ms == 0U ||
			transition->elapsed_ms != (uint64_t)push->flight_ms ||
			!Binary32Canonical(push->gravity_bits) ||
			(push->gravity_bits & UINT32_C(0x80000000)) != 0U)
		{
			SetError(error,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
			if (!Binary32Finite(push->launch_velocity_bits[axis]))
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					index);
				return 0;
			}
	}
	else
	{
		const sg_rune_compact_mechanism_transport_t *transport =
			&transition->value.transport;

		if (!((mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
				mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
				mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
				mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN ||
				FiniteAngularDoorAuthority(builder_owner, mechanism) ||
				ContinuousRotatorAuthority(builder_owner, mechanism))) ||
			((mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
				mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
				mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
				FiniteAngularDoorAuthority(builder_owner, mechanism)) &&
				transition->source_state == transition->destination_state) ||
			transport->mover_model >=
				builder->identity.source_counts.model_count ||
			transport->source_surface_ordinal ==
				SG_RUNE_COMPACT_INDEX_NONE ||
			transition->elapsed_ms == 0U ||
			transport->swept_static_clear != 1U ||
			transport->start_supported != 1U ||
			transport->end_supported != 1U ||
			transport->stance >= SG_RUNE_STANCE_COUNT)
		{
			SetError(error,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
			return 0;
		}
		if (mechanism->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
		{
			if (!EntityValid(transport->source_endpoint,
					&builder->identity) ||
				!EntityValid(transport->destination_endpoint,
					&builder->identity) ||
				transport->source_endpoint.entity_ordinal ==
					transport->destination_endpoint.entity_ordinal ||
				transport->fanout_ordinal == SG_RUNE_COMPACT_INDEX_NONE)
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					index);
				return 0;
			}
		}
		else if (transport->source_endpoint.entity_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE ||
			transport->destination_endpoint.entity_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE ||
			transport->fanout_ordinal != SG_RUNE_COMPACT_INDEX_NONE)
		{
			SetError(error,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
		{
			if (!Binary32Canonical(
					transport->source_player_world_bits[axis]) ||
				!Binary32Canonical(
					transport->destination_player_world_bits[axis]) ||
				!Binary32Canonical(
					transport->source_support_world_bits[axis]) ||
				!Binary32Canonical(
					transport->destination_support_world_bits[axis]) ||
				!Binary32Canonical(
					transport->source_mover_origin_bits[axis]) ||
				!Binary32Canonical(
					transport->destination_mover_origin_bits[axis]))
			{
				SetError(error,
					SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					index);
				return 0;
			}
			for (component = 0U; component < 3U; component++)
				if (!Binary32Canonical(
						transport->source_mover_axis_bits[axis][component]) ||
					!Binary32Canonical(
						transport->destination_mover_axis_bits[axis][component]))
				{
					SetError(error,
						SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
						SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
						index);
					return 0;
				}
		}
	}
	return 1;
}

static int CandidateValid(
	const sg_rune_compact_mechanisms_candidate_t *candidate,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_builder_owner_view_t *builder_owner,
	const sg_rune_compact_geometry_view_t *geometry,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t mechanism;
	uint32_t transition;
	uint32_t next_controller = 0U;
	uint32_t next_topology = 0U;
	uint32_t next_transition = 0U;

	if (!ArrayShapeValid(candidate->mechanisms, candidate->mechanism_count) ||
		!ArrayShapeValid(candidate->controllers, candidate->controller_count) ||
		!ArrayShapeValid(candidate->topology_edges,
			candidate->topology_edge_count) ||
		!ArrayShapeValid(candidate->transitions, candidate->transition_count))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		return 0;
	}
	for (mechanism = 0U; mechanism < candidate->mechanism_count; mechanism++)
		if (!ValidateMechanism(candidate, builder, builder_owner, geometry,
				mechanism,
				&next_controller, &next_topology, &next_transition, error))
			return 0;
	if (next_controller != candidate->controller_count ||
		next_topology != candidate->topology_edge_count ||
		next_transition != candidate->transition_count)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		return 0;
	}
	for (transition = 0U; transition < candidate->transition_count; transition++)
		if (!ValidateTransition(candidate, builder, builder_owner, geometry,
				transition, error))
			return 0;
	for (mechanism = 0U; mechanism < candidate->mechanism_count; mechanism++)
		if (!TimingAggregateValid(candidate, &candidate->mechanisms[mechanism],
				mechanism, error))
			return 0;
	return 1;
}

static int CopyArray(void **destination, const void *source, uint32_t count,
	size_t width, sg_rune_compact_mechanisms_error_t *error)
{
	size_t bytes;
	void *copy;

	if (count == 0U)
	{
		*destination = NULL;
		return 1;
	}
	if (!SizeMultiply((size_t)count, width, &bytes))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, count);
		return 0;
	}
	copy = OwnerAllocate(bytes);
	if (copy == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, count);
		return 0;
	}
	memcpy(copy, source, bytes);
	*destination = copy;
	return 1;
}

static int OwnerValid(const sg_rune_compact_mechanisms_t *mechanisms)
{
	return mechanisms != NULL && mechanisms->self == mechanisms &&
		mechanisms->state == SG_RUNE_COMPACT_MECHANISMS_STATE &&
		mechanisms->state_inverse == ~SG_RUNE_COMPACT_MECHANISMS_STATE;
}

int SG_RuneCompactMechanismsMaterialize(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_mechanisms_t **mechanisms_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	sg_rune_compact_builder_view_t builder_view;
	sg_rune_compact_builder_owner_view_t builder_owner_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_mechanisms_candidate_t candidate;
	sg_rune_compact_mechanisms_t *result = NULL;
	int candidate_owned = 0;
	int success = 0;

	memset(&builder_view, 0, sizeof(builder_view));
	memset(&builder_owner_view, 0, sizeof(builder_owner_view));
	memset(&geometry_view, 0, sizeof(geometry_view));
	memset(&candidate, 0, sizeof(candidate));
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	if (builder == NULL || geometry == NULL || mechanisms_out == NULL ||
		!SG_RuneCompactBuilderRead(builder, &builder_view) ||
		!SG_RuneCompactBuilderOwnerRead(builder, &builder_owner_view) ||
		!SG_RuneCompactGeometryRead(geometry, &geometry_view))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	if (!SG_RuneCompactIdentityMatches(&builder_view.identity,
			&builder_owner_view.identity) ||
		!SG_RuneCompactIdentityMatches(&builder_view.identity,
			&geometry_view.identity))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER, 0U);
		goto cleanup;
	}
	if (builder_owner_view.entity_semantics == NULL ||
		(builder_owner_view.entity_semantics->string_bytes != 0U &&
			builder_owner_view.entity_semantics->strings == NULL))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER, 0U);
		goto cleanup;
	}
	if (!SG_RuneCompactMechanismsBuildCandidate(builder, geometry, &candidate,
			error_out))
	{
		if (error_out == NULL ||
			error_out->code == SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE)
			SetError(error_out,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	candidate_owned = 1;
	if (!CandidateValid(&candidate, &builder_view, &builder_owner_view,
			&geometry_view, error_out))
		goto cleanup;
	if (!SG_RuneCompactMechanismTransitionsValidate(builder, geometry,
			candidate.mechanisms, candidate.mechanism_count,
			candidate.transitions, candidate.transition_count, error_out))
		goto cleanup;
	result = OwnerAllocate(sizeof(*result));
	if (result == NULL)
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	memset(result, 0, sizeof(*result));
	result->view.identity = builder_view.identity;
	result->view.mechanism_count = candidate.mechanism_count;
	result->view.controller_count = candidate.controller_count;
	result->view.topology_edge_count = candidate.topology_edge_count;
	result->view.transition_count = candidate.transition_count;
	if (!CopyArray((void **)&result->owned_mechanisms, candidate.mechanisms,
			candidate.mechanism_count, sizeof(*candidate.mechanisms), error_out) ||
		!CopyArray((void **)&result->owned_controllers, candidate.controllers,
			candidate.controller_count, sizeof(*candidate.controllers), error_out) ||
		!CopyArray((void **)&result->owned_topology_edges,
			candidate.topology_edges, candidate.topology_edge_count,
			sizeof(*candidate.topology_edges), error_out) ||
		!CopyArray((void **)&result->owned_transitions, candidate.transitions,
			candidate.transition_count, sizeof(*candidate.transitions), error_out))
		goto cleanup;
	result->view.mechanisms = result->owned_mechanisms;
	result->view.controllers = result->owned_controllers;
	result->view.topology_edges = result->owned_topology_edges;
	result->view.transitions = result->owned_transitions;
	result->state = SG_RUNE_COMPACT_MECHANISMS_STATE;
	result->state_inverse = ~SG_RUNE_COMPACT_MECHANISMS_STATE;
	result->self = result;
	*mechanisms_out = result;
	result = NULL;
	success = 1;

cleanup:
	if (candidate_owned)
		SG_RuneCompactMechanismsReleaseCandidate(&candidate);
	SG_RuneCompactMechanismsDestroy(result);
	return success;
}

int SG_RuneCompactMechanismsRead(const sg_rune_compact_mechanisms_t *mechanisms,
	sg_rune_compact_mechanisms_view_t *view_out)
{
	if (!OwnerValid(mechanisms) || view_out == NULL)
		return 0;
	*view_out = mechanisms->view;
	return 1;
}

void SG_RuneCompactMechanismsDestroy(sg_rune_compact_mechanisms_t *mechanisms)
{
	if (mechanisms == NULL)
		return;
	free(mechanisms->owned_mechanisms);
	free(mechanisms->owned_controllers);
	free(mechanisms->owned_topology_edges);
	free(mechanisms->owned_transitions);
	memset(mechanisms, 0, sizeof(*mechanisms));
	free(mechanisms);
}

const char *SG_RuneCompactMechanismsErrorString(
	sg_rune_compact_mechanisms_error_code_t code)
{
	static const char *const messages[SG_RUNE_COMPACT_MECHANISMS_ERROR_CODE_COUNT] = {
		"none",
		"invalid argument",
		"identity mismatch",
		"invalid mechanism source",
		"invalid compact geometry reference",
		"ambiguous mechanism binding",
		"host mechanism evaluation failed",
		"size overflow",
		"out of memory"
	};

	if (code < SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE ||
		code >= SG_RUNE_COMPACT_MECHANISMS_ERROR_CODE_COUNT)
		return "unknown compact mechanism error";
	return messages[code];
}
