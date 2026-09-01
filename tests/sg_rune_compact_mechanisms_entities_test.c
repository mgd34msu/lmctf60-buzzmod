#include "../slipgate/sg_rune_compact_mechanisms_entities.h"

#include "../slipgate/sg_rune_compact_builder_owner.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#expression); \
		return 0; \
	} \
} while (0)

struct sg_rune_compact_builder_s
{
	sg_rune_compact_builder_owner_view_t owner;
};

typedef struct fixture_s
{
	struct sg_rune_compact_builder_s builder;
	sg_bsp_entity_semantics_t semantics;
	sg_bsp_entity_semantic_t entities[8];
	sg_bsp_entity_semantic_edge_t edges[12];
	char strings[16];
} fixture_t;

int SG_RuneCompactBuilderOwnerRead(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;

	if (source == NULL || view_out == NULL)
		return 0;
	*view_out = source->owner;
	return 1;
}

static sg_bsp_entity_semantic_edge_t Edge(uint32_t source,
	uint32_t destination, sg_mech_edge_kind_t kind, uint32_t fanout)
{
	sg_bsp_entity_semantic_edge_t edge;

	memset(&edge, 0, sizeof(edge));
	edge.source = source;
	edge.destination = destination;
	edge.kind = kind;
	edge.name = 4U;
	edge.fanout_ordinal = fanout;
	return edge;
}

static void Authority(sg_bsp_entity_semantic_t *entity,
	uint64_t identity, uint32_t canonical, uint32_t source,
	sg_rune_mechanism_kind_t kind, sg_mech_node_kind_t role,
	sg_bsp_entity_semantic_flags_t activation)
{
	memset(entity, 0, sizeof(*entity));
	entity->source_set_identity = identity;
	entity->canonical_ordinal = canonical;
	entity->source_entity_ordinal = source;
	entity->required_item = SG_BSP_ENTITY_STRING_NONE;
	entity->classname = SG_BSP_ENTITY_STRING_NONE;
	entity->targetname = SG_BSP_ENTITY_STRING_NONE;
	entity->spawned_classname = SG_BSP_ENTITY_STRING_NONE;
	entity->destination_map = SG_BSP_ENTITY_STRING_NONE;
	entity->bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	entity->flags = SG_BSP_ENTITY_HAS_MECHANISM |
		SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND | activation;
	entity->mechanism_kind = kind;
	entity->mechanism_role = role;
}

static void Initialize(fixture_t *fixture)
{
	static const uint32_t source_ordinals[8] = {
		0U, 2U, 5U, 9U, 12U, 17U, 21U, 29U
	};
	uint32_t index;
	uint64_t identity = UINT64_C(0x1020304050607080);

	memset(fixture, 0, sizeof(*fixture));
	memcpy(fixture->strings, "key\0edge\0", 9U);
	fixture->semantics.source_set_identity = identity;
	fixture->semantics.world.source_set_identity = identity;
	fixture->semantics.entities = fixture->entities;
	fixture->semantics.entity_count = 8U;
	fixture->semantics.edges = fixture->edges;
	fixture->semantics.edge_count = 12U;
	fixture->semantics.strings = fixture->strings;
	fixture->semantics.string_bytes = 9U;
	fixture->builder.owner.entity_semantics = &fixture->semantics;
	fixture->builder.owner.identity.source_counts.entity_count = 8U;

	for (index = 0U; index < 8U; index++)
	{
		fixture->entities[index].source_set_identity = identity;
		fixture->entities[index].source_entity_ordinal = source_ordinals[index];
		fixture->entities[index].canonical_ordinal = index;
		fixture->entities[index].required_item = SG_BSP_ENTITY_STRING_NONE;
		fixture->entities[index].classname = SG_BSP_ENTITY_STRING_NONE;
		fixture->entities[index].targetname = SG_BSP_ENTITY_STRING_NONE;
		fixture->entities[index].spawned_classname =
			SG_BSP_ENTITY_STRING_NONE;
		fixture->entities[index].destination_map = SG_BSP_ENTITY_STRING_NONE;
		fixture->entities[index].bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	}
	Authority(&fixture->entities[1], identity, 1U, source_ordinals[1],
		SG_RUNE_MECHANISM_TRIGGER, SG_MECH_NODE_TRIGGER,
		SG_BSP_ENTITY_TOUCH_ACTIVATED | SG_BSP_ENTITY_USE_ACTIVATED |
		SG_BSP_ENTITY_DELAY_DEFINED | SG_BSP_ENTITY_DWELL_DEFINED);
	fixture->entities[1].delay_ms = 250.0f;
	fixture->entities[1].dwell_ms = -1000.0f;
	Authority(&fixture->entities[2], identity, 2U, source_ordinals[2],
		SG_RUNE_MECHANISM_BUTTON, SG_MECH_NODE_BUTTON,
		SG_BSP_ENTITY_AUTO_ACTIVATED | SG_BSP_ENTITY_USE_ACTIVATED |
		SG_BSP_ENTITY_DAMAGE_ACTIVATED |
		SG_BSP_ENTITY_INVENTORY_GATED | SG_BSP_ENTITY_DWELL_DEFINED |
		SG_BSP_ENTITY_PAUSE_DEFINED | SG_BSP_ENTITY_INITIALLY_ACTIVE);
	fixture->entities[2].dwell_ms = 1750.0f;
	fixture->entities[2].pause_ms = 3000.0f;
	fixture->entities[2].damage = 12;
	fixture->entities[2].health = 40;
	fixture->entities[2].required_item = 0U;
	Authority(&fixture->entities[3], identity, 3U, source_ordinals[3],
		SG_RUNE_MECHANISM_DOOR, SG_MECH_NODE_DOOR_MASTER,
		SG_BSP_ENTITY_USE_ACTIVATED);
	Authority(&fixture->entities[4], identity, 4U, source_ordinals[4],
		SG_RUNE_MECHANISM_TRAIN, SG_MECH_NODE_PATH_CORNER, 0U);
	Authority(&fixture->entities[5], identity, 5U, source_ordinals[5],
		SG_RUNE_MECHANISM_TELEPORT, SG_MECH_NODE_TELEPORTER,
		SG_BSP_ENTITY_TOUCH_ACTIVATED);
	Authority(&fixture->entities[6], identity, 6U, source_ordinals[6],
		SG_RUNE_MECHANISM_PUSH, SG_MECH_NODE_PUSH,
		SG_BSP_ENTITY_TOUCH_ACTIVATED | SG_BSP_ENTITY_INITIALLY_ACTIVE);
	Authority(&fixture->entities[7], identity, 7U, source_ordinals[7],
		SG_RUNE_MECHANISM_PUSH, SG_MECH_NODE_PUSH,
		SG_BSP_ENTITY_TOUCH_ACTIVATED | SG_BSP_ENTITY_DWELL_DEFINED |
		SG_BSP_ENTITY_INITIALLY_ACTIVE);
	fixture->entities[7].dwell_ms = 5000.0f;
	fixture->entities[7].spawnflags = 1U;
	/* This is intentionally not a canonical mechanism.  It remains an
	 * executable controller for the door and must retain its own touch fact. */
	fixture->entities[0].flags = SG_BSP_ENTITY_TOUCH_ACTIVATED;
	fixture->entities[0].mechanism_role = SG_MECH_NODE_RELAY;

	fixture->edges[0] = Edge(1U, 3U, SG_MECH_EDGE_TARGET, 0U);
	fixture->edges[1] = Edge(1U, 0U, SG_MECH_EDGE_KILLTARGET, 2U);
	fixture->edges[2] = Edge(1U, 2U, SG_MECH_EDGE_OWNER, 0U);
	fixture->edges[3] = Edge(1U, 2U, SG_MECH_EDGE_TEAM, 0U);
	fixture->edges[4] = Edge(1U, 0U, SG_MECH_EDGE_PATH_TARGET, 4U);
	fixture->edges[5] = Edge(1U, 4U, SG_MECH_EDGE_MOVE_TARGET, 0U);
	fixture->edges[6] = Edge(1U, 5U, SG_MECH_EDGE_TARGET_ENT, 0U);
	fixture->edges[7] = Edge(1U, 5U, SG_MECH_EDGE_ENEMY, 0U);
	fixture->edges[8] = Edge(1U, 4U, SG_MECH_EDGE_ROUTE_TARGET, 7U);
	fixture->edges[9] = Edge(2U, 3U, SG_MECH_EDGE_TARGET, 3U);
	fixture->edges[10] = Edge(0U, 3U, SG_MECH_EDGE_PATH_TARGET, 5U);
	fixture->edges[11] = Edge(3U, 5U, SG_MECH_EDGE_TEAM, 9U);
}

static const sg_rune_compact_mechanism_entity_authority_t *FindAuthority(
	const sg_rune_compact_mechanisms_entities_t *result, uint32_t entity)
{
	uint32_t index;

	for (index = 0U; index < result->mechanism_count; index++)
		if (result->mechanisms[index].source.entity_ordinal == entity)
			return &result->mechanisms[index];
	return NULL;
}

static int SpanHasEdge(const sg_rune_compact_mechanisms_entities_t *result,
	const sg_rune_compact_mechanism_entity_authority_t *authority,
	const sg_bsp_entity_semantic_edge_t *wanted, uint32_t *index_out)
{
	uint32_t index;

	for (index = authority->topology.first;
		index < authority->topology.first + authority->topology.count; index++)
	{
		const sg_rune_compact_mechanism_topology_edge_t *edge =
			&result->topology_edges[index];

		if (edge->source.entity_ordinal == wanted->source &&
			edge->destination.entity_ordinal == wanted->destination &&
			edge->kind == wanted->kind &&
			edge->fanout_ordinal == wanted->fanout_ordinal)
		{
			if (index_out != NULL)
				*index_out = index;
			return 1;
		}
	}
	return 0;
}

static const sg_rune_compact_mechanism_entity_controller_t *FindController(
	const sg_rune_compact_mechanisms_entities_t *result,
	const sg_rune_compact_mechanism_entity_authority_t *authority,
	uint32_t entity)
{
	uint32_t index;

	for (index = authority->controllers.first;
		index < authority->controllers.first + authority->controllers.count;
		index++)
		if (result->controllers[index].controller.entity_ordinal == entity)
			return &result->controllers[index];
	return NULL;
}

static int FailureLeavesOutputUntouched(fixture_t *fixture,
	sg_rune_compact_mechanisms_error_code_t expected);

static int TestExactEnumeration(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanisms_entities_t result;
	sg_rune_compact_mechanisms_error_t error;
	const sg_rune_compact_mechanism_entity_authority_t *trigger;
	const sg_rune_compact_mechanism_entity_authority_t *button;
	const sg_rune_compact_mechanism_entity_authority_t *door;
	uint32_t edge_index;
	uint32_t index;

	Initialize(&fixture);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE);
	CHECK(result.mechanism_count == 6U);
	CHECK(result.mechanisms[0].source.entity_ordinal == 1U);
	CHECK(result.mechanisms[1].source.entity_ordinal == 2U);
	CHECK(result.mechanisms[2].source.entity_ordinal == 3U);
	CHECK(result.mechanisms[3].source.entity_ordinal == 5U);
	CHECK(result.mechanisms[4].source.entity_ordinal == 6U);
	CHECK(result.mechanisms[5].source.entity_ordinal == 7U);
	trigger = FindAuthority(&result, 1U);
	button = FindAuthority(&result, 2U);
	door = FindAuthority(&result, 3U);
	CHECK(trigger != NULL && button != NULL && door != NULL);
	CHECK(trigger->activation ==
		(SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH |
		 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE));
	CHECK(trigger->delay_ms == 250U && trigger->dwell_ms == 0U);
	CHECK((trigger->flags & SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) != 0U);
	CHECK(trigger->reset_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_DISABLED);
	CHECK(button->activation ==
		(SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO |
		 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE |
		 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE |
		 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY));
	CHECK(button->dwell_ms == 1750U && button->pause_ms == 3000U);
	CHECK(button->damage == 12 && button->health == 40);
	CHECK(button->required_item == 0U);
	CHECK(trigger->required_item == SG_BSP_ENTITY_STRING_NONE);
	CHECK((result.mechanisms[4].flags &
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) == 0U);
	CHECK(result.mechanisms[4].reset_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	CHECK((result.mechanisms[5].flags &
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) != 0U);
	CHECK(result.mechanisms[5].dwell_ms == 5000U);
	CHECK(result.mechanisms[5].reset_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_DISABLED);
	CHECK(button->initial_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	CHECK((button->flags &
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE) != 0U);
	for (index = 0U; index < 9U; index++)
		CHECK(SpanHasEdge(&result, trigger, &fixture.edges[index], NULL));
	CHECK(door->controllers.count == 3U);
	for (index = 0U; index < door->controllers.count; index++)
	{
		const sg_rune_compact_mechanism_entity_controller_t *controller =
			&result.controllers[door->controllers.first + index];
		const sg_rune_compact_mechanism_topology_edge_t *edge =
			&result.topology_edges[controller->topology_edge];

		CHECK(controller->mechanism == 2U);
		CHECK(controller->topology_edge >= door->topology.first);
		CHECK(controller->topology_edge <
			door->topology.first + door->topology.count);
		CHECK(edge->source.entity_ordinal ==
			controller->controller.entity_ordinal);
		CHECK(edge->destination.entity_ordinal == 3U);
		CHECK(edge->kind == SG_MECH_EDGE_TARGET ||
			edge->kind == SG_MECH_EDGE_PATH_TARGET);
	}
	CHECK(SpanHasEdge(&result, door, &fixture.edges[10], &edge_index));
	CHECK(result.topology_edges[edge_index].fanout_ordinal == 5U);
	{
		const sg_rune_compact_mechanism_entity_controller_t *controller =
			FindController(&result, door, 0U);

		CHECK(controller != NULL);
		CHECK(controller->activation ==
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH);
		CHECK(controller->damage == 0 && controller->health == 0);
		CHECK(controller->required_item == SG_BSP_ENTITY_STRING_NONE);
		CHECK(controller->flags == 0U);
		CHECK(controller->spatiality ==
			SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL);
	}
	{
		const sg_rune_compact_mechanism_entity_controller_t *controller =
			FindController(&result, door, 1U);

		CHECK(controller != NULL);
		CHECK(controller->spatiality ==
			SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL);
	}
	{
		const sg_rune_compact_mechanism_entity_controller_t *controller =
			FindController(&result, door, 2U);

		CHECK(controller != NULL);
		CHECK(controller->activation ==
			(SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO |
			 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE |
			 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE |
			 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY));
		CHECK(controller->damage == 12 && controller->health == 40 &&
			controller->required_item == 0U);
		CHECK(controller->spatiality ==
			SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL);
	}
	for (index = 1U; index < result.mechanism_count; index++)
	{
		CHECK(result.mechanisms[index].controllers.first ==
			result.mechanisms[index - 1U].controllers.first +
			result.mechanisms[index - 1U].controllers.count);
		CHECK(result.mechanisms[index].topology.first ==
			result.mechanisms[index - 1U].topology.first +
			result.mechanisms[index - 1U].topology.count);
	}
	SG_RuneCompactMechanismEntitiesRelease(&result);
	CHECK(result.mechanisms == NULL && result.mechanism_count == 0U);
	return 1;
}

static int TestContextualTimerIsNonspatial(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanisms_entities_t result;
	sg_rune_compact_mechanisms_error_t error;
	const sg_rune_compact_mechanism_entity_authority_t *door;
	const sg_rune_compact_mechanism_entity_controller_t *controller;

	Initialize(&fixture);
	fixture.entities[0].mechanism_role = SG_MECH_NODE_CONTEXTUAL;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	door = FindAuthority(&result, 3U);
	controller = FindController(&result, door, 0U);
	CHECK(door != NULL && controller != NULL);
	CHECK(controller->spatiality ==
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL);
	SG_RuneCompactMechanismEntitiesRelease(&result);
	return 1;
}

static int TestControllerActivationProvenance(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanisms_entities_t result;
	sg_rune_compact_mechanisms_error_t error;
	const sg_rune_compact_mechanism_entity_authority_t *door;
	const sg_rune_compact_mechanism_entity_controller_t *controller;

	Initialize(&fixture);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	door = FindAuthority(&result, 3U);
	controller = FindController(&result, door, 0U);
	CHECK(door != NULL && controller != NULL);
	/* A non-authority controller has no mechanism activation to borrow: this
	 * typed record is the only activation provenance for its path target. */
	CHECK(controller->activation ==
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH);
	SG_RuneCompactMechanismEntitiesRelease(&result);

	Initialize(&fixture);
	fixture.entities[0].flags = SG_BSP_ENTITY_HAS_LANDMARK;
	fixture.entities[0].landmark_kind = SG_RUNE_LANDMARK_ITEM;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	door = FindAuthority(&result, 3U);
	controller = FindController(&result, door, 0U);
	CHECK(door != NULL && controller != NULL);
	CHECK(controller->activation ==
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH);
	SG_RuneCompactMechanismEntitiesRelease(&result);

	Initialize(&fixture);
	fixture.entities[0].flags = 0U;
	CHECK(FailureLeavesOutputUntouched(&fixture,
		SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE));
	return 1;
}

static int TestToggleAuthorityStates(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanisms_entities_t result;
	sg_rune_compact_mechanisms_error_t error;
	const sg_rune_compact_mechanism_entity_authority_t *authority;

	Initialize(&fixture);
	fixture.entities[3].spawnflags = UINT32_C(32);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	authority = FindAuthority(&result, 3U);
	CHECK(authority != NULL);
	CHECK(authority->initial_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE);
	CHECK(authority->activated_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	/* This is deliberately not INACTIVE: a toggle stays open until another
	 * controller activation, so it cannot be mistaken for auto-recovery. */
	CHECK(authority->reset_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	SG_RuneCompactMechanismEntitiesRelease(&result);

	Initialize(&fixture);
	fixture.entities[3].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture.entities[3].angular_mover.kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR;
	fixture.entities[3].angular_mover.flags =
		SG_BSP_ENTITY_ANGULAR_MOVER_TOGGLE;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	authority = FindAuthority(&result, 3U);
	CHECK(authority != NULL && authority->kind ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR);
	CHECK(authority->reset_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	SG_RuneCompactMechanismEntitiesRelease(&result);
	return 1;
}

static int FailureLeavesOutputUntouched(fixture_t *fixture,
	sg_rune_compact_mechanisms_error_code_t expected)
{
	sg_rune_compact_mechanisms_entities_t sentinel;
	sg_rune_compact_mechanisms_entities_t output;
	sg_rune_compact_mechanisms_error_t error;

	memset(&sentinel, 0xa5, sizeof(sentinel));
	output = sentinel;
	CHECK(!SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture->builder, &output, &error));
	CHECK(error.code == expected);
	CHECK(memcmp(&output, &sentinel, sizeof(output)) == 0);
	return 1;
}

static int TestMalformedGraphs(void)
{
	fixture_t fixture;

	Initialize(&fixture);
	fixture.edges[11] = fixture.edges[0];
	CHECK(FailureLeavesOutputUntouched(&fixture,
		SG_RUNE_COMPACT_MECHANISMS_ERROR_AMBIGUOUS_BINDING));
	Initialize(&fixture);
	fixture.edges[0].destination = 8U;
	CHECK(FailureLeavesOutputUntouched(&fixture,
		SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE));
	Initialize(&fixture);
	fixture.entities[3].canonical_ordinal = 4U;
	CHECK(FailureLeavesOutputUntouched(&fixture,
		SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE));
	Initialize(&fixture);
	fixture.entities[3].source_entity_ordinal =
		fixture.entities[2].source_entity_ordinal;
	CHECK(FailureLeavesOutputUntouched(&fixture,
		SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE));
	return 1;
}

static int TestAllocationFailures(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanisms_entities_t result;
	sg_rune_compact_mechanisms_entities_t sentinel;
	sg_rune_compact_mechanisms_error_t error;
	size_t allocations;
	size_t index;

	Initialize(&fixture);
	memset(&result, 0, sizeof(result));
	SG_RuneCompactMechanismEntitiesTestFailAfter(SIZE_MAX);
	CHECK(SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, &result, &error));
	allocations = SG_RuneCompactMechanismEntitiesTestAllocationCount();
	CHECK(allocations > 0U);
	SG_RuneCompactMechanismEntitiesRelease(&result);
	memset(&sentinel, 0x3c, sizeof(sentinel));
	for (index = 0U; index < allocations; index++)
	{
		result = sentinel;
		SG_RuneCompactMechanismEntitiesTestFailAfter(index);
		CHECK(!SG_RuneCompactMechanismEntitiesEnumerate(
			(const sg_rune_compact_builder_t *)&fixture.builder,
			&result, &error));
		CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY);
		CHECK(memcmp(&result, &sentinel, sizeof(result)) == 0);
	}
	SG_RuneCompactMechanismEntitiesTestFailAfter(SIZE_MAX);
	return 1;
}

static int TestArguments(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanisms_entities_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismEntitiesEnumerate(NULL, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT);
	CHECK(!SG_RuneCompactMechanismEntitiesEnumerate(
		(const sg_rune_compact_builder_t *)&fixture.builder, NULL, &error));
	SG_RuneCompactMechanismEntitiesRelease(NULL);
	return 1;
}

int main(void)
{
	if (!TestExactEnumeration() || !TestControllerActivationProvenance() ||
		!TestContextualTimerIsNonspatial() ||
		!TestToggleAuthorityStates() ||
		!TestMalformedGraphs() ||
		!TestAllocationFailures() || !TestArguments())
		return 1;
	puts("compact mechanism entity enumeration tests passed");
	return 0;
}
