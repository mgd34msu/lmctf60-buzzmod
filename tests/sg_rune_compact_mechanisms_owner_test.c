#include "../slipgate/sg_rune_compact_mechanisms.h"
#include "../slipgate/sg_rune_compact_mechanisms_build.h"
#include "../slipgate/sg_rune_compact_builder_owner.h"
#include "../slipgate/sg_rune_compact_mechanisms_transitions.h"

#include <stdio.h>
#include <stdlib.h>
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
	sg_rune_compact_builder_view_t view;
	sg_rune_compact_builder_owner_view_t owner_view;
};

struct sg_rune_compact_geometry_s
{
	sg_rune_compact_geometry_view_t view;
};

typedef enum mutation_e
{
	MUTATION_NONE = 0,
	MUTATION_BUILD_FAILURE,
	MUTATION_NULL_ARRAY,
	MUTATION_BAD_SPAN,
	MUTATION_ORPHAN,
	MUTATION_DUPLICATE_MECHANISM,
	MUTATION_DUPLICATE_EDGE,
	MUTATION_BAD_AUTHORITY_ENUM,
	MUTATION_BAD_EDGE_ENUM,
	MUTATION_BAD_CELL,
	MUTATION_BAD_PORTAL,
	MUTATION_BAD_TRANSITION_SHAPE,
	MUTATION_CONTROLLER_WRONG_PROVENANCE,
	MUTATION_CONTROLLER_WRONG_ACTIVATION,
	MUTATION_BAD_PHYSICS,
	MUTATION_INVENTORY_OFFSET_ZERO,
	MUTATION_INVENTORY_SENTINEL,
	MUTATION_INVENTORY_OUT_OF_RANGE,
	MUTATION_FACT_AUTHORITY_KIND,
	MUTATION_FACT_DESTINATION,
	MUTATION_FACT_FANOUT,
	MUTATION_FACT_WITNESS,
	MUTATION_FACT_MOVER_MODEL,
	MUTATION_FACT_PUSH_LAUNCH,
	MUTATION_FACT_PUSH_GRAVITY,
	MUTATION_FACT_PUSH_FLIGHT,
	MUTATION_TRANSPORT_NOT_SWEPT,
	MUTATION_TRANSPORT_START_UNSUPPORTED,
	MUTATION_TRANSPORT_END_UNSUPPORTED,
	MUTATION_TRANSPORT_INVALID_STANCE,
	MUTATION_PORTAL_ZERO_ELAPSED,
	MUTATION_PORTAL_WRONG_ELAPSED,
	MUTATION_AUTHORITY_TRAVEL_MISMATCH,
	MUTATION_PORTAL_SAME_STATE,
	MUTATION_PORTAL_SOURCE_BLOCKED_NONBOOLEAN,
	MUTATION_PORTAL_BLOCKED_EQUAL,
	MUTATION_PORTAL_BLOCKED_RESERVED,
	MUTATION_LIFT_SAME_STATE,
	MUTATION_TRAIN_SAME_ENDPOINT,
	MUTATION_TELEPORT_NONZERO_VELOCITY,
	MUTATION_TELEPORT_NEGATIVE_ZERO_VELOCITY,
	MUTATION_TRANSPORT_NEGATIVE_ZERO_WORLD,
	MUTATION_TRANSPORT_NEGATIVE_ZERO_TRANSFORM,
	MUTATION_TRANSPORT_NEGATIVE_ZERO_AXIS
} mutation_t;

typedef struct fixture_s
{
	struct sg_rune_compact_builder_s builder;
	struct sg_rune_compact_geometry_s geometry;
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_portal_t portals[1];
	sg_rune_compact_mechanism_authority_t mechanisms[5];
	sg_rune_compact_mechanism_controller_t controllers[1];
	sg_rune_compact_mechanism_topology_edge_t topology[2];
	sg_rune_compact_mechanism_transition_t transitions[5];
	sg_bsp_entity_semantics_t entity_semantics;
	sg_bsp_entity_semantic_t entities[6];
	sg_bsp_entity_semantic_edge_t edges[2];
	char entity_strings[8];
} fixture_t;

static fixture_t source_fixture;
static mutation_t current_mutation;
static uint32_t transition_validation_calls;

static sg_rune_q8_bounds_t Bounds(int32_t minimum, int32_t maximum)
{
	sg_rune_q8_bounds_t bounds;
	uint32_t axis;

	memset(&bounds, 0, sizeof(bounds));
	for (axis = 0U; axis < 3U; axis++)
	{
		bounds.mins.value[axis] = minimum;
		bounds.maxs.value[axis] = maximum;
	}
	return bounds;
}

static void InitializeFixture(fixture_t *fixture)
{
	sg_rune_compact_identity_t *identity;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	identity = &fixture->builder.view.identity;
	identity->bsp_sha256[0] = 1U;
	identity->bsp_bytes = 1U;
	identity->source_counts.model_count = 2U;
	identity->source_counts.entity_count = 6U;
	fixture->geometry.view.identity = *identity;
	fixture->builder.owner_view.identity = *identity;
	fixture->builder.owner_view.entity_semantics = &fixture->entity_semantics;
	fixture->entity_semantics.source_set_identity = UINT64_C(0x1020);
	fixture->entity_semantics.world.source_set_identity = UINT64_C(0x1020);
	fixture->entity_semantics.entities = fixture->entities;
	fixture->entity_semantics.entity_count = 6U;
	fixture->entity_semantics.edges = fixture->edges;
	fixture->entity_semantics.edge_count = 2U;
	fixture->entity_semantics.strings = fixture->entity_strings;
	fixture->entity_semantics.string_bytes = sizeof(fixture->entity_strings);
	memcpy(fixture->entity_strings, "key\0x\0", 6U);
	for (index = 0U; index < 6U; index++)
	{
		fixture->entities[index].source_set_identity =
			fixture->entity_semantics.source_set_identity;
		fixture->entities[index].canonical_ordinal = index;
		fixture->entities[index].source_entity_ordinal = index;
		fixture->entities[index].required_item = SG_BSP_ENTITY_STRING_NONE;
		fixture->entities[index].bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	}
	fixture->entities[0].flags = SG_BSP_ENTITY_HAS_MECHANISM |
		SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND |
		SG_BSP_ENTITY_AUTO_ACTIVATED;
	fixture->entities[0].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
	fixture->entities[0].mechanism_role = SG_MECH_NODE_DOOR_MASTER;
	fixture->entities[1].flags = SG_BSP_ENTITY_TOUCH_ACTIVATED;
	fixture->entities[1].mechanism_role = SG_MECH_NODE_TRIGGER;
	fixture->edges[0].source = 1U;
	fixture->edges[0].destination = 0U;
	fixture->edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[1] = fixture->edges[0];
	fixture->edges[1].kind = SG_MECH_EDGE_KILLTARGET;
	fixture->geometry.view.cells = fixture->cells;
	fixture->geometry.view.cell_count = 2U;
	fixture->cells[0].bounds = Bounds(-16, 16);
	fixture->cells[1].bounds = Bounds(17, 32);
	fixture->geometry.view.incidences = fixture->incidences;
	fixture->geometry.view.incidence_count = 2U;
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[1].cell.value = 1U;
	fixture->geometry.view.portals = fixture->portals;
	fixture->geometry.view.portal_count = 1U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;

	fixture->mechanisms[0].source.entity_ordinal = 0U;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture->mechanisms[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	fixture->mechanisms[0].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanisms[0].activation_cell.value = 0U;
	fixture->mechanisms[0].activation_bounds = Bounds(-8, 8);
	fixture->mechanisms[0].controllers =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanisms[0].topology =
		(sg_rune_compact_mechanism_span_t){ 0U, 2U };
	fixture->mechanisms[0].transitions =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanisms[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanisms[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanisms[0].travel_ms = 100U;

	fixture->mechanisms[1] = fixture->mechanisms[0];
	fixture->mechanisms[1].source.entity_ordinal = 2U;
	fixture->mechanisms[1].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT;
	/* Stock trigger_teleport is a stateless always-active capability. */
	fixture->mechanisms[1].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanisms[1].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanisms[1].activation_cell.value = 1U;
	fixture->mechanisms[1].activation_witness.value[0] = 20;
	fixture->mechanisms[1].activation_witness.value[1] = 20;
	fixture->mechanisms[1].activation_witness.value[2] = 20;
	fixture->mechanisms[1].activation_bounds = Bounds(17, 32);
	fixture->mechanisms[1].controllers =
		(sg_rune_compact_mechanism_span_t){ 1U, 0U };
	fixture->mechanisms[1].topology =
		(sg_rune_compact_mechanism_span_t){ 2U, 0U };
	fixture->mechanisms[1].transitions =
		(sg_rune_compact_mechanism_span_t){ 1U, 1U };
	fixture->mechanisms[1].travel_ms =
		SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
	fixture->mechanisms[2] = fixture->mechanisms[1];
	fixture->mechanisms[2].source.entity_ordinal = 3U;
	fixture->mechanisms[2].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH;
	/* Stock trigger_push likewise has no inactive portal/controller state. */
	fixture->mechanisms[2].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanisms[2].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanisms[2].transitions =
		(sg_rune_compact_mechanism_span_t){ 2U, 1U };
	fixture->mechanisms[2].travel_ms =
		SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
	fixture->mechanisms[3] = fixture->mechanisms[2];
	fixture->mechanisms[3].source.entity_ordinal = 4U;
	fixture->mechanisms[3].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT;
	fixture->mechanisms[3].transitions =
		(sg_rune_compact_mechanism_span_t){ 3U, 1U };
	fixture->mechanisms[4] = fixture->mechanisms[3];
	fixture->mechanisms[4].source.entity_ordinal = 5U;
	fixture->mechanisms[4].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN;
	fixture->mechanisms[4].transitions =
		(sg_rune_compact_mechanism_span_t){ 4U, 1U };

	fixture->controllers[0].mechanism = 0U;
	fixture->controllers[0].controller.entity_ordinal = 1U;
	fixture->controllers[0].topology_edge = 0U;
	fixture->controllers[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	fixture->controllers[0].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->controllers[0].activation_cell.value = 0U;
	fixture->controllers[0].activation_bounds = Bounds(-8, 8);

	fixture->topology[0].source.entity_ordinal = 1U;
	fixture->topology[0].destination.entity_ordinal = 0U;
	fixture->topology[0].kind = SG_MECH_EDGE_TARGET;
	fixture->topology[0].fanout_ordinal = 0U;
	fixture->topology[1] = fixture->topology[0];
	fixture->topology[1].kind = SG_MECH_EDGE_KILLTARGET;
	fixture->topology[1].fanout_ordinal = 1U;

	for (index = 0U; index < 4U; index++)
	{
		fixture->transitions[index].mechanism = index;
		fixture->transitions[index].kind =
			(sg_rune_compact_mechanism_transition_kind_t)index;
		fixture->transitions[index].entry_cell.value = 0U;
		fixture->transitions[index].exit_cell.value = 1U;
		fixture->transitions[index].source_state =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
		fixture->transitions[index].destination_state =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	}
	fixture->transitions[0].value.portal_state.portal.value = 0U;
	fixture->transitions[0].value.portal_state.mover_model = 1U;
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;
	fixture->transitions[0].elapsed_ms = 100U;
	fixture->transitions[0].value.portal_state.travel_ms = 100U;
	fixture->transitions[1].value.teleport.destination.entity_ordinal = 1U;
	fixture->transitions[1].value.teleport.fanout_ordinal = 0U;
	fixture->transitions[1].value.teleport.exit_witness.value[0] = 20;
	fixture->transitions[1].value.teleport.exit_witness.value[1] = 20;
	fixture->transitions[1].value.teleport.exit_witness.value[2] = 20;
	fixture->transitions[1].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[1].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[2].value.push.exit_witness.value[0] = 20;
	fixture->transitions[2].value.push.exit_witness.value[1] = 20;
	fixture->transitions[2].value.push.exit_witness.value[2] = 20;
	fixture->transitions[2].value.push.launch_velocity_bits[0] =
		UINT32_C(0x3f800000);
	fixture->transitions[2].value.push.launch_velocity_bits[1] =
		UINT32_C(0x80000000);
	fixture->transitions[2].value.push.gravity_bits = UINT32_C(0x42c80000);
	fixture->transitions[2].value.push.flight_ms = 100U;
	fixture->transitions[2].elapsed_ms = 100U;
	fixture->transitions[2].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[2].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[3].elapsed_ms = 100U;
	fixture->transitions[3].value.transport.mover_model = 1U;
	fixture->transitions[3].value.transport.source_surface_ordinal = 0U;
	fixture->transitions[3].value.transport.source_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[3].value.transport.destination_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[3].value.transport.fanout_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[3].value.transport.swept_static_clear = 1U;
	fixture->transitions[3].value.transport.start_supported = 1U;
	fixture->transitions[3].value.transport.end_supported = 1U;
	fixture->mechanisms[3].travel_ms = 100U;
	fixture->transitions[4] = fixture->transitions[3];
	fixture->transitions[4].mechanism = 4U;
	fixture->transitions[4].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[4].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[4].value.transport.source_endpoint.entity_ordinal = 0U;
	fixture->transitions[4].value.transport.destination_endpoint.entity_ordinal = 1U;
	fixture->transitions[4].value.transport.fanout_ordinal = 0U;
	fixture->mechanisms[4].travel_ms = 100U;
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	if (builder == NULL || view_out == NULL)
		return 0;
	*view_out = builder->view;
	return 1;
}

int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	if (builder == NULL || view_out == NULL)
		return 0;
	*view_out = builder->owner_view;
	return 1;
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out)
{
	if (geometry == NULL || view_out == NULL)
		return 0;
	*view_out = geometry->view;
	return 1;
}

int SG_RuneCompactIdentityMatches(const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		memcmp(actual, expected, sizeof(*actual)) == 0;
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
	(void)builder;
	(void)geometry;
	transition_validation_calls++;
	if (mechanisms == NULL || transitions == NULL || mechanism_count != 5U ||
		transition_count != 5U ||
		mechanisms[0].kind != source_fixture.mechanisms[0].kind ||
		memcmp(transitions, source_fixture.transitions,
			sizeof(source_fixture.transitions)) != 0)
	{
		error_out->code = SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE;
		error_out->domain = SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION;
		error_out->record = 0U;
		return 0;
	}
	return 1;
}

static int Clone(void **destination, const void *source, size_t bytes)
{
	*destination = malloc(bytes);
	if (*destination == NULL)
		return 0;
	memcpy(*destination, source, bytes);
	return 1;
}

int SG_RuneCompactMechanismsBuildCandidate(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_mechanisms_candidate_t *candidate_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	(void)builder;
	(void)geometry;
	memset(candidate_out, 0, sizeof(*candidate_out));
	if (current_mutation == MUTATION_BUILD_FAILURE)
	{
		error_out->code = SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION;
		return 0;
	}
	if (!Clone((void **)&candidate_out->mechanisms, source_fixture.mechanisms,
			sizeof(source_fixture.mechanisms)) ||
		!Clone((void **)&candidate_out->controllers, source_fixture.controllers,
			sizeof(source_fixture.controllers)) ||
		!Clone((void **)&candidate_out->topology_edges, source_fixture.topology,
			sizeof(source_fixture.topology)) ||
		!Clone((void **)&candidate_out->transitions, source_fixture.transitions,
			sizeof(source_fixture.transitions)))
	{
		SG_RuneCompactMechanismsReleaseCandidate(candidate_out);
		error_out->code = SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	candidate_out->mechanism_count = 5U;
	candidate_out->controller_count = 1U;
	candidate_out->topology_edge_count = 2U;
	candidate_out->transition_count = 5U;
	switch (current_mutation)
	{
	case MUTATION_NULL_ARRAY:
		candidate_out->mechanism_count = 0U;
		break;
	case MUTATION_BAD_SPAN:
		candidate_out->mechanisms[0].controllers.first = 1U;
		break;
	case MUTATION_ORPHAN:
		candidate_out->mechanisms[0].topology.count = 1U;
		candidate_out->mechanisms[1].topology.first = 1U;
		break;
	case MUTATION_DUPLICATE_MECHANISM:
		candidate_out->mechanisms[1].source.entity_ordinal = 0U;
		break;
	case MUTATION_DUPLICATE_EDGE:
		candidate_out->topology_edges[1] = candidate_out->topology_edges[0];
		break;
	case MUTATION_BAD_AUTHORITY_ENUM:
		candidate_out->mechanisms[0].kind =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT;
		break;
	case MUTATION_BAD_EDGE_ENUM:
		candidate_out->topology_edges[0].kind = (sg_mech_edge_kind_t)0;
		break;
	case MUTATION_BAD_CELL:
		candidate_out->mechanisms[0].activation_cell.value = 2U;
		break;
	case MUTATION_BAD_PORTAL:
		candidate_out->transitions[0].value.portal_state.portal.value = 1U;
		break;
	case MUTATION_BAD_TRANSITION_SHAPE:
		candidate_out->transitions[1].value.teleport.destination.entity_ordinal =
			SG_RUNE_COMPACT_INDEX_NONE;
		break;
	case MUTATION_CONTROLLER_WRONG_PROVENANCE:
		/* The second topology edge is a KILLTARGET relation.  A controller
		 * record may only authenticate an executable inbound controller edge. */
		candidate_out->controllers[0].topology_edge = 1U;
		break;
	case MUTATION_CONTROLLER_WRONG_ACTIVATION:
		candidate_out->controllers[0].activation =
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
		break;
	case MUTATION_BAD_PHYSICS:
		candidate_out->transitions[2].value.push.gravity_bits =
			UINT32_C(0x7f800000);
		break;
	case MUTATION_INVENTORY_OFFSET_ZERO:
		candidate_out->mechanisms[0].activation |=
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
		candidate_out->mechanisms[0].required_item = 0U;
		break;
	case MUTATION_INVENTORY_SENTINEL:
		candidate_out->mechanisms[0].activation |=
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
		candidate_out->mechanisms[0].required_item =
			SG_BSP_ENTITY_STRING_NONE;
		break;
	case MUTATION_INVENTORY_OUT_OF_RANGE:
		candidate_out->mechanisms[0].activation |=
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY;
		candidate_out->mechanisms[0].required_item =
			source_fixture.entity_semantics.string_bytes;
		break;
	case MUTATION_FACT_AUTHORITY_KIND:
		candidate_out->mechanisms[0].kind =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON;
		break;
	case MUTATION_FACT_DESTINATION:
		candidate_out->transitions[1].value.teleport.destination.entity_ordinal =
			3U;
		break;
	case MUTATION_FACT_FANOUT:
		candidate_out->transitions[1].value.teleport.fanout_ordinal = 99U;
		break;
	case MUTATION_FACT_WITNESS:
		candidate_out->transitions[1].value.teleport.approach_witness.value[0] = 1;
		break;
	case MUTATION_FACT_MOVER_MODEL:
		candidate_out->transitions[0].value.portal_state.mover_model = 0U;
		break;
	case MUTATION_FACT_PUSH_LAUNCH:
		candidate_out->transitions[2].value.push.launch_velocity_bits[0] =
			UINT32_C(0x40000000);
		break;
	case MUTATION_FACT_PUSH_GRAVITY:
		candidate_out->transitions[2].value.push.gravity_bits =
			UINT32_C(0x42480000);
		break;
	case MUTATION_FACT_PUSH_FLIGHT:
		candidate_out->transitions[2].value.push.flight_ms = 200U;
		break;
	case MUTATION_TRANSPORT_NOT_SWEPT:
		candidate_out->transitions[3].value.transport.swept_static_clear = 0U;
		break;
	case MUTATION_TRANSPORT_START_UNSUPPORTED:
		candidate_out->transitions[3].value.transport.start_supported = 0U;
		break;
	case MUTATION_TRANSPORT_END_UNSUPPORTED:
		candidate_out->transitions[3].value.transport.end_supported = 0U;
		break;
	case MUTATION_TRANSPORT_INVALID_STANCE:
		candidate_out->transitions[3].value.transport.stance =
			SG_RUNE_STANCE_COUNT;
		break;
	case MUTATION_PORTAL_ZERO_ELAPSED:
		candidate_out->transitions[0].elapsed_ms = 0U;
		break;
	case MUTATION_PORTAL_WRONG_ELAPSED:
		candidate_out->transitions[0].elapsed_ms++;
		break;
	case MUTATION_AUTHORITY_TRAVEL_MISMATCH:
		candidate_out->mechanisms[0].travel_ms++;
		break;
	case MUTATION_PORTAL_SAME_STATE:
		candidate_out->transitions[0].destination_state =
			candidate_out->transitions[0].source_state;
		break;
	case MUTATION_PORTAL_SOURCE_BLOCKED_NONBOOLEAN:
		candidate_out->transitions[0].value.portal_state.source_blocked = 2U;
		break;
	case MUTATION_PORTAL_BLOCKED_EQUAL:
		candidate_out->transitions[0].value.portal_state.destination_blocked =
			candidate_out->transitions[0].value.portal_state.source_blocked;
		break;
	case MUTATION_PORTAL_BLOCKED_RESERVED:
		candidate_out->transitions[0].value.portal_state.reserved[1] = 1U;
		break;
	case MUTATION_LIFT_SAME_STATE:
		candidate_out->transitions[3].destination_state =
			candidate_out->transitions[3].source_state;
		break;
	case MUTATION_TRAIN_SAME_ENDPOINT:
		candidate_out->transitions[4].value.transport.destination_endpoint =
			candidate_out->transitions[4].value.transport.source_endpoint;
		break;
	case MUTATION_TELEPORT_NONZERO_VELOCITY:
		candidate_out->transitions[1].value.teleport.arrival_velocity_bits[0] =
			UINT32_C(0x3f800000);
		break;
	case MUTATION_TELEPORT_NEGATIVE_ZERO_VELOCITY:
		candidate_out->transitions[1].value.teleport.arrival_velocity_bits[0] =
			UINT32_C(0x80000000);
		break;
	case MUTATION_TRANSPORT_NEGATIVE_ZERO_WORLD:
		candidate_out->transitions[3].value.transport
			.source_player_world_bits[0] = UINT32_C(0x80000000);
		break;
	case MUTATION_TRANSPORT_NEGATIVE_ZERO_TRANSFORM:
		candidate_out->transitions[3].value.transport
			.source_mover_origin_bits[0] = UINT32_C(0x80000000);
		break;
	case MUTATION_TRANSPORT_NEGATIVE_ZERO_AXIS:
		candidate_out->transitions[3].value.transport
			.source_mover_axis_bits[0][0] = UINT32_C(0x80000000);
		break;
	case MUTATION_NONE:
	case MUTATION_BUILD_FAILURE:
		break;
	}
	return 1;
}

void SG_RuneCompactMechanismsReleaseCandidate(
	sg_rune_compact_mechanisms_candidate_t *candidate)
{
	if (candidate == NULL)
		return;
	free(candidate->mechanisms);
	free(candidate->controllers);
	free(candidate->topology_edges);
	free(candidate->transitions);
	memset(candidate, 0, sizeof(*candidate));
}

static int Materialize(mutation_t mutation,
	sg_rune_compact_mechanisms_t **output,
	sg_rune_compact_mechanisms_error_t *error)
{
	current_mutation = mutation;
	return SG_RuneCompactMechanismsMaterialize(&source_fixture.builder,
		&source_fixture.geometry, output, error);
}

static int TestPublicationAndLifetime(void)
{
	sg_rune_compact_mechanisms_t *owner = NULL;
	sg_rune_compact_mechanisms_view_t view;
	sg_rune_compact_mechanisms_error_t error;

	CHECK(Materialize(MUTATION_NONE, &owner, &error));
	CHECK(owner != NULL);
	memset(&source_fixture.builder, 0, sizeof(source_fixture.builder));
	memset(&source_fixture.geometry, 0, sizeof(source_fixture.geometry));
	memset(source_fixture.mechanisms, 0, sizeof(source_fixture.mechanisms));
	CHECK(SG_RuneCompactMechanismsRead(owner, &view));
	CHECK(view.mechanism_count == 5U);
	CHECK(view.mechanisms[0].source.entity_ordinal == 0U);
	CHECK(view.mechanisms[2].source.entity_ordinal == 3U);
	CHECK(view.transition_count == 5U);
	CHECK(view.transitions[0].elapsed_ms == 100U);
	CHECK(view.transitions[1].source_state == view.transitions[1].destination_state);
	CHECK(view.transitions[2].source_state == view.transitions[2].destination_state);
	CHECK(view.transitions[4].source_state == view.transitions[4].destination_state);
	SG_RuneCompactMechanismsDestroy(owner);
	SG_RuneCompactMechanismsDestroy(NULL);
	return 1;
}

static int TestRejectedCandidatesLeaveOutputUntouched(void)
{
	mutation_t mutation;
	sg_rune_compact_mechanisms_t *const sentinel =
		(sg_rune_compact_mechanisms_t *)(uintptr_t)0x1234U;

	for (mutation = MUTATION_BUILD_FAILURE;
		mutation <= MUTATION_TRANSPORT_NEGATIVE_ZERO_AXIS; mutation++)
	{
		sg_rune_compact_mechanisms_t *output = sentinel;
		sg_rune_compact_mechanisms_error_t error;

		InitializeFixture(&source_fixture);
		if (mutation == MUTATION_INVENTORY_OFFSET_ZERO)
		{
			CHECK(Materialize(mutation, &output, &error));
			CHECK(output != sentinel);
			SG_RuneCompactMechanismsDestroy(output);
		}
		else
		{
			CHECK(!Materialize(mutation, &output, &error));
			CHECK(output == sentinel);
			CHECK(error.code != SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE);
		}
	}
	return 1;
}

static int TestPortalElapsedBinding(void)
{
	sg_rune_compact_mechanisms_t *output = NULL;
	sg_rune_compact_mechanisms_error_t error;

	InitializeFixture(&source_fixture);
	transition_validation_calls = 0U;
	CHECK(!Materialize(MUTATION_PORTAL_ZERO_ELAPSED, &output, &error));
	CHECK(output == NULL);
	CHECK(transition_validation_calls == 0U);
	InitializeFixture(&source_fixture);
	transition_validation_calls = 0U;
	CHECK(!Materialize(MUTATION_PORTAL_WRONG_ELAPSED, &output, &error));
	CHECK(output == NULL);
	CHECK(transition_validation_calls == 0U);
	return 1;
}

static int TestPortalBlockedStateRejectsBeforeRevalidation(void)
{
	const mutation_t mutations[] = {
		MUTATION_PORTAL_SOURCE_BLOCKED_NONBOOLEAN,
		MUTATION_PORTAL_BLOCKED_EQUAL,
		MUTATION_PORTAL_BLOCKED_RESERVED
	};
	uint32_t index;

	for (index = 0U; index < sizeof(mutations) / sizeof(mutations[0]); index++)
	{
		sg_rune_compact_mechanisms_t *output = NULL;
		sg_rune_compact_mechanisms_error_t error;

		InitializeFixture(&source_fixture);
		transition_validation_calls = 0U;
		CHECK(!Materialize(mutations[index], &output, &error));
		CHECK(output == NULL);
		CHECK(transition_validation_calls == 0U);
	}
	return 1;
}

static int TestTransportRejectsNegativeZeroBeforeRevalidation(void)
{
	sg_rune_compact_mechanisms_t *output = NULL;
	sg_rune_compact_mechanisms_error_t error;

	InitializeFixture(&source_fixture);
	transition_validation_calls = 0U;
	CHECK(!Materialize(MUTATION_TRANSPORT_NEGATIVE_ZERO_WORLD, &output,
		&error));
	CHECK(output == NULL);
	CHECK(transition_validation_calls == 0U);
	return 1;
}

static int TestTransportTransformRejectsNegativeZeroBeforeRevalidation(void)
{
	sg_rune_compact_mechanisms_t *output = NULL;
	sg_rune_compact_mechanisms_error_t error;

	InitializeFixture(&source_fixture);
	transition_validation_calls = 0U;
	CHECK(!Materialize(MUTATION_TRANSPORT_NEGATIVE_ZERO_TRANSFORM, &output,
		&error));
	CHECK(output == NULL);
	CHECK(transition_validation_calls == 0U);
	return 1;
}

static int TestTransportAxisRejectsNegativeZeroBeforeRevalidation(void)
{
	sg_rune_compact_mechanisms_t *output = NULL;
	sg_rune_compact_mechanisms_error_t error;

	InitializeFixture(&source_fixture);
	transition_validation_calls = 0U;
	CHECK(!Materialize(MUTATION_TRANSPORT_NEGATIVE_ZERO_AXIS, &output,
		&error));
	CHECK(output == NULL);
	CHECK(transition_validation_calls == 0U);
	return 1;
}

static int TestIdentityAndArguments(void)
{
	sg_rune_compact_mechanisms_t *const sentinel =
		(sg_rune_compact_mechanisms_t *)(uintptr_t)0x1234U;
	sg_rune_compact_mechanisms_t *output = sentinel;
	sg_rune_compact_mechanisms_error_t error;

	InitializeFixture(&source_fixture);
	source_fixture.geometry.view.identity.construction_id = 1U;
	CHECK(!Materialize(MUTATION_NONE, &output, &error));
	CHECK(output == sentinel);
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH);
	CHECK(!SG_RuneCompactMechanismsMaterialize(NULL, &source_fixture.geometry,
		&output, &error));
	CHECK(output == sentinel);
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT);
	return 1;
}

static int TestEveryOwnerAllocationFailure(void)
{
	sg_rune_compact_mechanisms_t *owner = NULL;
	sg_rune_compact_mechanisms_error_t error;
	size_t allocation_count;
	size_t ordinal;

	InitializeFixture(&source_fixture);
	SG_RuneCompactMechanismsTestFailAfter(SIZE_MAX);
	CHECK(Materialize(MUTATION_NONE, &owner, &error));
	allocation_count = SG_RuneCompactMechanismsTestAllocationCount();
	CHECK(allocation_count == 5U);
	SG_RuneCompactMechanismsDestroy(owner);
	for (ordinal = 0U; ordinal < allocation_count; ordinal++)
	{
		sg_rune_compact_mechanisms_t *const sentinel =
			(sg_rune_compact_mechanisms_t *)(uintptr_t)0x1234U;
		sg_rune_compact_mechanisms_t *output = sentinel;

		InitializeFixture(&source_fixture);
		SG_RuneCompactMechanismsTestFailAfter(ordinal);
		CHECK(!Materialize(MUTATION_NONE, &output, &error));
		CHECK(output == sentinel);
		CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY);
	}
	SG_RuneCompactMechanismsTestFailAfter(SIZE_MAX);
	return 1;
}

static int TestErrorStrings(void)
{
	int code;

	for (code = SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE;
		code < SG_RUNE_COMPACT_MECHANISMS_ERROR_CODE_COUNT; code++)
		CHECK(strcmp(SG_RuneCompactMechanismsErrorString(
			(sg_rune_compact_mechanisms_error_code_t)code), "") != 0);
	CHECK(strcmp(SG_RuneCompactMechanismsErrorString(
		(sg_rune_compact_mechanisms_error_code_t)999),
		"unknown compact mechanism error") == 0);
	return 1;
}

int main(void)
{
	InitializeFixture(&source_fixture);
	if (!TestPublicationAndLifetime() ||
		!TestRejectedCandidatesLeaveOutputUntouched() ||
		!TestPortalElapsedBinding() ||
		!TestPortalBlockedStateRejectsBeforeRevalidation() ||
		!TestTransportRejectsNegativeZeroBeforeRevalidation() ||
		!TestTransportTransformRejectsNegativeZeroBeforeRevalidation() ||
		!TestTransportAxisRejectsNegativeZeroBeforeRevalidation() ||
		!TestIdentityAndArguments() ||
		!TestEveryOwnerAllocationFailure() || !TestErrorStrings())
		return 1;
	puts("sg_rune_compact_mechanisms_owner_test: ok");
	return 0;
}
