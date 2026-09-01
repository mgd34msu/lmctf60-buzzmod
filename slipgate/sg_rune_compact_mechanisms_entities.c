#include "sg_rune_compact_mechanisms_entities.h"

#include "sg_rune_compact_builder_owner.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SG_TRIGGER_PUSH_ONCE UINT32_C(1)
#define SG_DOOR_START_OPEN UINT32_C(1)
#define SG_DOOR_TOGGLE UINT32_C(32)

typedef struct entity_work_s
{
	sg_rune_compact_mechanisms_entities_t result;
	uint32_t mechanism_capacity;
	uint32_t controller_capacity;
	uint32_t topology_capacity;
} entity_work_t;

#if defined(SG_RUNE_COMPACT_MECHANISMS_ENTITIES_TESTING)
static size_t test_fail_after = SIZE_MAX;
static size_t test_allocation_count;

void SG_RuneCompactMechanismEntitiesTestFailAfter(size_t allocation)
{
	test_fail_after = allocation;
	test_allocation_count = 0U;
}

size_t SG_RuneCompactMechanismEntitiesTestAllocationCount(void)
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

static void *EntityAllocate(size_t bytes)
{
#if defined(SG_RUNE_COMPACT_MECHANISMS_ENTITIES_TESTING)
	if (test_allocation_count == test_fail_after)
	{
		test_allocation_count++;
		return NULL;
	}
	test_allocation_count++;
#endif
	return malloc(bytes);
}

static int Grow(void **values, uint32_t *capacity, uint32_t required,
	size_t width, sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t next;
	size_t bytes;
	void *replacement;

	if (required <= *capacity)
		return 1;
	next = *capacity != 0U ? *capacity : 8U;
	while (next < required)
	{
		if (next > UINT32_MAX / 2U)
		{
			next = UINT32_MAX;
			break;
		}
		next *= 2U;
	}
	if (next < required ||
		(width != 0U && (size_t)next > SIZE_MAX / width))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, required);
		return 0;
	}
	bytes = (size_t)next * width;
	replacement = EntityAllocate(bytes);
	if (replacement == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, required);
		return 0;
	}
	if (*values != NULL)
		memcpy(replacement, *values, (size_t)*capacity * width);
	free(*values);
	*values = replacement;
	*capacity = next;
	return 1;
}

void SG_RuneCompactMechanismEntitiesRelease(
	sg_rune_compact_mechanisms_entities_t *entities)
{
	if (entities == NULL)
		return;
	free(entities->mechanisms);
	free(entities->controllers);
	free(entities->topology_edges);
	memset(entities, 0, sizeof(*entities));
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

/* Pickup activation is implicit in the BSP item semantic: the item-touch
 * callback is the executable controller even when the generic activation
 * bit belongs only to a trigger class. */
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

static int Milliseconds(float value, int allow_one_shot, uint32_t *value_out,
	int *one_shot_out)
{
	uint32_t converted;

	if (!isfinite(value))
		return 0;
	if (allow_one_shot && value < 0.0f)
	{
		*value_out = 0U;
		*one_shot_out = 1;
		return 1;
	}
	if (value < 0.0f || (double)value > (double)UINT32_MAX)
		return 0;
	converted = (uint32_t)value;
	if ((float)converted != value)
		return 0;
	*value_out = converted;
	return 1;
}

static int StringReferenceValid(const sg_bsp_entity_semantics_t *semantics,
	uint32_t reference)
{
	const void *terminator;

	if (reference == SG_BSP_ENTITY_STRING_NONE)
		return 1;
	if (semantics->strings == NULL || reference >= semantics->string_bytes)
		return 0;
	terminator = memchr(semantics->strings + reference, '\0',
		(size_t)(semantics->string_bytes - reference));
	return terminator != NULL;
}

static int EntityShapeValid(const sg_rune_compact_builder_owner_view_t *owner,
	sg_rune_compact_mechanisms_error_t *error)
{
	const sg_bsp_entity_semantics_t *semantics = owner->entity_semantics;
	uint32_t index;

	if (semantics == NULL ||
		(semantics->entities == NULL) != (semantics->entity_count == 0U) ||
		(semantics->edges == NULL) != (semantics->edge_count == 0U) ||
		(semantics->strings == NULL) != (semantics->string_bytes == 0U) ||
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
				.source_entity_ordinal >= entity->source_entity_ordinal) ||
			((entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND) != 0U &&
				((entity->flags & SG_BSP_ENTITY_HAS_MECHANISM) == 0U ||
				 entity->mechanism_kind < SG_RUNE_MECHANISM_DOOR ||
				 entity->mechanism_kind >= SG_RUNE_MECHANISM_KIND_COUNT)) ||
			((entity->flags & SG_BSP_ENTITY_HAS_MECHANISM) != 0U &&
				(entity->mechanism_role < SG_MECH_NODE_TRIGGER ||
				 entity->mechanism_role > SG_MECH_NODE_TARGET_LASER)) ||
			((entity->flags & SG_BSP_ENTITY_HAS_LANDMARK) != 0U &&
				(entity->landmark_kind < SG_RUNE_LANDMARK_FLAG_STAND ||
				 entity->landmark_kind >= SG_RUNE_LANDMARK_KIND_COUNT)) ||
			!StringReferenceValid(semantics, entity->required_item))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			return 0;
		}
	}
	return 1;
}

static int EdgeCompare(const void *left_value, const void *right_value)
{
	const sg_rune_compact_mechanism_topology_edge_t *left = left_value;
	const sg_rune_compact_mechanism_topology_edge_t *right = right_value;

	if (left->source.entity_ordinal != right->source.entity_ordinal)
		return left->source.entity_ordinal < right->source.entity_ordinal ? -1 : 1;
	if (left->destination.entity_ordinal != right->destination.entity_ordinal)
		return left->destination.entity_ordinal <
			right->destination.entity_ordinal ? -1 : 1;
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	if (left->fanout_ordinal != right->fanout_ordinal)
		return left->fanout_ordinal < right->fanout_ordinal ? -1 : 1;
	return 0;
}

static int ValidateEdges(const sg_bsp_entity_semantics_t *semantics,
	sg_rune_compact_mechanisms_error_t *error)
{
	sg_rune_compact_mechanism_topology_edge_t *ordered = NULL;
	size_t bytes;
	uint32_t index;
	int valid = 0;

	if (semantics->edge_count == 0U)
		return 1;
	if (semantics->edge_count != 0U &&
		sizeof(*ordered) > SIZE_MAX / (size_t)semantics->edge_count)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, semantics->edge_count);
		return 0;
	}
	bytes = (size_t)semantics->edge_count * sizeof(*ordered);
	ordered = EntityAllocate(bytes);
	if (ordered == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, 0U);
		return 0;
	}
	for (index = 0U; index < semantics->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *source = &semantics->edges[index];

		if (source->source >= semantics->entity_count ||
			source->destination >= semantics->entity_count ||
			source->kind < SG_MECH_EDGE_TARGET ||
			source->kind > SG_MECH_EDGE_ROUTE_TARGET ||
			!StringReferenceValid(semantics, source->name))
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, index);
			goto cleanup;
		}
		ordered[index].source.entity_ordinal = source->source;
		ordered[index].destination.entity_ordinal = source->destination;
		ordered[index].kind = source->kind;
		ordered[index].fanout_ordinal = source->fanout_ordinal;
	}
	qsort(ordered, semantics->edge_count, sizeof(*ordered), EdgeCompare);
	for (index = 1U; index < semantics->edge_count; index++)
		if (EdgeCompare(&ordered[index - 1U], &ordered[index]) == 0)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_AMBIGUOUS_BINDING,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, index);
			goto cleanup;
		}
	valid = 1;

cleanup:
	free(ordered);
	return valid;
}

static int ExecutableControllerEdge(sg_mech_edge_kind_t kind)
{
	return kind == SG_MECH_EDGE_TARGET || kind == SG_MECH_EDGE_PATH_TARGET;
}

static int AddTopology(entity_work_t *work,
	const sg_bsp_entity_semantic_edge_t *edge,
	sg_rune_compact_mechanisms_error_t *error)
{
	sg_rune_compact_mechanism_topology_edge_t *output;
	uint32_t required;

	if (work->result.topology_edge_count == UINT32_MAX)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, edge->source);
		return 0;
	}
	required = work->result.topology_edge_count + 1U;
	if (!Grow((void **)&work->result.topology_edges,
			&work->topology_capacity, required,
			sizeof(*work->result.topology_edges), error))
		return 0;
	output = &work->result.topology_edges[work->result.topology_edge_count++];
	output->source.entity_ordinal = edge->source;
	output->destination.entity_ordinal = edge->destination;
	output->kind = edge->kind;
	output->fanout_ordinal = edge->fanout_ordinal;
	return 1;
}

static int TopologyMatchesSemantic(
	const sg_rune_compact_mechanism_topology_edge_t *output,
	const sg_bsp_entity_semantic_edge_t *source)
{
	return output->source.entity_ordinal == source->source &&
		output->destination.entity_ordinal == source->destination &&
		output->kind == source->kind &&
		output->fanout_ordinal == source->fanout_ordinal;
}

static int ControllerCompare(const void *left_value, const void *right_value)
{
	const sg_rune_compact_mechanism_entity_controller_t *left = left_value;
	const sg_rune_compact_mechanism_entity_controller_t *right = right_value;

	if (left->controller.entity_ordinal != right->controller.entity_ordinal)
		return left->controller.entity_ordinal <
			right->controller.entity_ordinal ? -1 : 1;
	if (left->topology_edge != right->topology_edge)
		return left->topology_edge < right->topology_edge ? -1 : 1;
	return 0;
}

static int FillController(
	sg_rune_compact_mechanism_entity_controller_t *controller,
	uint32_t mechanism, const sg_bsp_entity_semantic_t *entity,
	uint32_t entity_index, uint32_t topology_edge,
	sg_rune_compact_mechanisms_error_t *error)
{
	int one_shot = 0;
	uint32_t unused_ms;

	memset(controller, 0, sizeof(*controller));
	controller->mechanism = mechanism;
	controller->controller.entity_ordinal = entity_index;
	controller->topology_edge = topology_edge;
	controller->activation = ControllerActivationMask(entity);
	controller->damage = entity->damage;
	controller->health = entity->health;
	controller->required_item = entity->required_item;
	controller->spatiality = ControllerSpatiality(entity);
	/* A negative dwell is an explicit no-repeat controller fact even when the
	 * controller is not itself a canonical mechanism authority. */
	if (!Milliseconds(entity->dwell_ms, 1, &unused_ms, &one_shot) ||
		controller->activation == 0U ||
		((controller->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) != 0U &&
			controller->health <= 0) ||
		((controller->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE) == 0U &&
			controller->health != 0) ||
		((controller->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) != 0U &&
			entity->required_item == SG_BSP_ENTITY_STRING_NONE) ||
		((controller->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) == 0U &&
			entity->required_item != SG_BSP_ENTITY_STRING_NONE))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, entity_index);
		return 0;
	}
	if (one_shot)
		controller->flags |= SG_RUNE_COMPACT_MECHANISM_CONTROLLER_ONE_SHOT;
	return 1;
}

static int AddController(entity_work_t *work, uint32_t mechanism,
	const sg_bsp_entity_semantic_t *entity, uint32_t controller,
	uint32_t topology_edge,
	sg_rune_compact_mechanisms_error_t *error)
{
	sg_rune_compact_mechanism_entity_controller_t *output;
	uint32_t required;

	if (work->result.controller_count == UINT32_MAX)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, controller);
		return 0;
	}
	required = work->result.controller_count + 1U;
	if (!Grow((void **)&work->result.controllers,
			&work->controller_capacity, required,
			sizeof(*work->result.controllers), error))
		return 0;
	output = &work->result.controllers[work->result.controller_count];
	if (!FillController(output, mechanism, entity, controller, topology_edge,
			error))
		return 0;
	work->result.controller_count++;
	return 1;
}

static int ToggleMover(const sg_bsp_entity_semantic_t *entity)
{
	return entity->mechanism_kind == SG_RUNE_MECHANISM_DOOR ?
		(entity->spawnflags & SG_DOOR_TOGGLE) != 0U :
		SG_BspEntitySemanticHasFiniteAngularDoor(entity) &&
		(entity->angular_mover.flags &
			SG_BSP_ENTITY_ANGULAR_MOVER_TOGGLE) != 0U;
}

static int StartsActive(const sg_bsp_entity_semantic_t *entity)
{
	return (entity->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE) != 0U ||
		(entity->mechanism_kind == SG_RUNE_MECHANISM_DOOR &&
			(entity->spawnflags & SG_DOOR_START_OPEN) != 0U) ||
		(SG_BspEntitySemanticHasFiniteAngularDoor(entity) &&
			(entity->angular_mover.flags &
				SG_BSP_ENTITY_ANGULAR_MOVER_START_OPEN) != 0U);
}

static int FillAuthority(
	sg_rune_compact_mechanism_entity_authority_t *authority,
	const sg_bsp_entity_semantic_t *entity, uint32_t entity_index,
	sg_rune_compact_mechanisms_error_t *error)
{
	int one_shot = 0;

	memset(authority, 0, sizeof(*authority));
	authority->source.entity_ordinal = entity_index;
	authority->kind =
		(sg_rune_compact_mechanism_authority_kind_t)entity->mechanism_kind;
	authority->activation = ActivationMask(entity);
	if (!Milliseconds(entity->delay_ms, 0, &authority->delay_ms,
			&one_shot) ||
		!Milliseconds(entity->dwell_ms, 1, &authority->dwell_ms,
			&one_shot) ||
		!Milliseconds(entity->pause_ms, 0, &authority->pause_ms,
			&one_shot))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, entity_index);
		return 0;
	}
	if (entity->mechanism_role == SG_MECH_NODE_PUSH &&
		(entity->spawnflags & SG_TRIGGER_PUSH_ONCE) != 0U)
		one_shot = 1;
	authority->damage = entity->damage;
	authority->health = entity->health;
	authority->required_item = entity->required_item;
	if ((authority->activation &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY) != 0U)
	{
		if (entity->required_item == SG_BSP_ENTITY_STRING_NONE)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, entity_index);
			return 0;
		}
	}
	authority->initial_state = StartsActive(entity)
			? SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE
			: SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	authority->activated_state = ToggleMover(entity) &&
		authority->initial_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE
			? SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE
			: SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	authority->reset_state = authority->initial_state;
	if (ToggleMover(entity))
	{
		/* reset_state describes autonomous recovery.  A toggle has none: it
		 * remains at the state reached by its first activation. */
		authority->reset_state = authority->activated_state;
	}
	/* A finite mover's toggle flag is the authoritative no-auto-return rule.
	 * Its dwell input is not an independent trigger_once instruction. */
	if (one_shot && !ToggleMover(entity))
	{
		authority->flags |= SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT;
		authority->reset_state =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_DISABLED;
	}
	if (entity->mechanism_role == SG_MECH_NODE_DOOR_MASTER ||
		entity->mechanism_role == SG_MECH_NODE_DOOR_MEMBER ||
		entity->mechanism_role == SG_MECH_NODE_BUTTON ||
		entity->mechanism_role == SG_MECH_NODE_PLATFORM ||
		entity->mechanism_role == SG_MECH_NODE_TRAIN ||
		entity->mechanism_role == SG_MECH_NODE_OTHER_MOVER ||
		entity->mechanism_role == SG_MECH_NODE_SECRET_DOOR)
		authority->flags |=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE;
	return 1;
}

static int AddAuthority(entity_work_t *work,
	const sg_bsp_entity_semantics_t *semantics, uint32_t entity_index,
	sg_rune_compact_mechanisms_error_t *error)
{
	const sg_bsp_entity_semantic_t *entity = &semantics->entities[entity_index];
	sg_rune_compact_mechanism_entity_authority_t *authority;
	uint32_t mechanism_index = work->result.mechanism_count;
	uint32_t topology_first = work->result.topology_edge_count;
	uint32_t controller_first = work->result.controller_count;
	uint32_t edge_index;
	uint32_t topology_index;
	uint32_t required;

	if (mechanism_index == UINT32_MAX)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, entity_index);
		return 0;
	}
	required = mechanism_index + 1U;
	if (!Grow((void **)&work->result.mechanisms,
			&work->mechanism_capacity, required,
			sizeof(*work->result.mechanisms), error))
		return 0;
	authority = &work->result.mechanisms[mechanism_index];
	if (!FillAuthority(authority, entity, entity_index, error))
		return 0;
	work->result.mechanism_count = required;
	for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[edge_index];

		if (edge->source == entity_index ||
			(edge->destination == entity_index &&
			 ExecutableControllerEdge(edge->kind)))
			if (!AddTopology(work, edge, error))
				return 0;
	}
	if (work->result.topology_edge_count - topology_first > 1U)
		qsort(&work->result.topology_edges[topology_first],
			(size_t)(work->result.topology_edge_count - topology_first),
			sizeof(*work->result.topology_edges), EdgeCompare);
	authority = &work->result.mechanisms[mechanism_index];
	authority->topology.first = topology_first;
	authority->topology.count =
		work->result.topology_edge_count - topology_first;
	for (topology_index = topology_first;
		topology_index < work->result.topology_edge_count; topology_index++)
	{
		const sg_rune_compact_mechanism_topology_edge_t *edge =
			&work->result.topology_edges[topology_index];

		if (edge->destination.entity_ordinal == entity_index &&
			ExecutableControllerEdge(edge->kind))
			if (!AddController(work, mechanism_index,
					&semantics->entities[edge->source.entity_ordinal],
					edge->source.entity_ordinal, topology_index, error))
				return 0;
	}
	if (work->result.controller_count - controller_first > 1U)
		qsort(&work->result.controllers[controller_first],
			(size_t)(work->result.controller_count - controller_first),
			sizeof(*work->result.controllers), ControllerCompare);
	authority = &work->result.mechanisms[mechanism_index];
	authority->controllers.first = controller_first;
	authority->controllers.count =
		work->result.controller_count - controller_first;
	for (edge_index = 0U; edge_index < semantics->edge_count; edge_index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[edge_index];
		int required_edge = edge->source == entity_index ||
			(edge->destination == entity_index &&
			 ExecutableControllerEdge(edge->kind));
		int found = 0;

		if (!required_edge)
			continue;
		for (topology_index = topology_first;
			topology_index < work->result.topology_edge_count; topology_index++)
			if (TopologyMatchesSemantic(
					&work->result.topology_edges[topology_index], edge))
			{
				found = 1;
				break;
			}
		if (!found)
		{
			SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE, edge_index);
			return 0;
		}
	}
	return 1;
}

int SG_RuneCompactMechanismEntitiesEnumerate(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_mechanisms_entities_t *entities_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	sg_rune_compact_builder_owner_view_t owner;
	entity_work_t work;
	uint32_t entity_index;
	int success = 0;

	memset(&owner, 0, sizeof(owner));
	memset(&work, 0, sizeof(work));
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	if (builder == NULL || entities_out == NULL ||
		!SG_RuneCompactBuilderOwnerRead(builder, &owner))
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	if (!EntityShapeValid(&owner, error_out) ||
		!ValidateEdges(owner.entity_semantics, error_out))
		goto cleanup;
	for (entity_index = 0U;
		entity_index < owner.entity_semantics->entity_count; entity_index++)
	{
		const sg_bsp_entity_semantic_t *entity =
			&owner.entity_semantics->entities[entity_index];

		if ((entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND) == 0U ||
			ActivationMask(entity) == 0U)
			continue;
		if (!AddAuthority(&work, owner.entity_semantics, entity_index,
				error_out))
			goto cleanup;
	}
	*entities_out = work.result;
	memset(&work.result, 0, sizeof(work.result));
	success = 1;

cleanup:
	SG_RuneCompactMechanismEntitiesRelease(&work.result);
	return success;
}
