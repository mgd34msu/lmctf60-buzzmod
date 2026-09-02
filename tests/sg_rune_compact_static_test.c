#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_bsp_entity_semantics.h"
#include "../slipgate/sg_rune_compact_static.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct static_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_portal_t portals[1];
	sg_rune_compact_mechanism_t mechanisms[2];
	sg_rune_compact_static_mechanism_controller_t mechanism_controllers[2];
	sg_rune_compact_mechanism_edge_t mechanism_edges[1];
	sg_rune_compact_static_transition_t transitions[2];
	sg_rune_compact_landmark_t landmarks[2];
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_facet_annotation_t facet_annotations[1];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[1];
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
} static_fixture_t;

static void InitFixture(static_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->cells[0].bounds.mins = (sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	fixture->cells[0].bounds.maxs = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->cells[1].bounds.mins = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->cells[1].bounds.maxs = (sg_rune_q8_vec3_t){ { 128, 64, 64 } };
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[1].cell.value = 1U;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;

	fixture->mechanisms[0].source.entity_ordinal = 7U;
	fixture->mechanisms[0].entry_cell.value = 0U;
	fixture->mechanisms[0].exit_cell.value = 1U;
	fixture->mechanisms[0].activation_landmark.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[0].bounds.mins = (sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanisms[0].bounds.maxs = (sg_rune_q8_vec3_t){ { 32, 32, 48 } };
	fixture->mechanisms[0].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 0U, 1U };
	fixture->mechanisms[0].controllers =
		(sg_rune_compact_mechanism_controller_span_t){ 0U, 1U };
	fixture->mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 1U };
	fixture->mechanisms[0].dwell_ms = 250U;
	fixture->mechanisms[0].travel_ms = 1000U;
	fixture->mechanisms[0].reset_ms = 2000U;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	fixture->mechanisms[0].initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].activated_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->mechanisms[0].reset_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].recovery =
		SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET;
	fixture->mechanisms[0].flags = 0U;
	fixture->mechanisms[0].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO;
	fixture->mechanisms[0].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanisms[0].transition_destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[0].transition_fanout_ordinal = UINT32_MAX;
	fixture->mechanisms[1] = fixture->mechanisms[0];
	fixture->mechanisms[1].source.entity_ordinal = 11U;
	fixture->mechanisms[1].entry_cell.value = 0U;
	fixture->mechanisms[1].exit_cell.value = 0U;
	fixture->mechanisms[1].activation_landmark.value = 0U;
	fixture->mechanisms[1].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH |
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_USE;
	fixture->mechanisms[1].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanisms[1].transition_destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[1].transition_fanout_ordinal = UINT32_MAX;
	fixture->mechanisms[1].controllers =
		(sg_rune_compact_mechanism_controller_span_t){ 1U, 1U };
	fixture->mechanisms[1].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 1U, 0U };
	fixture->mechanisms[1].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 1U, 0U };
	fixture->mechanisms[1].dwell_ms = 500U;
	fixture->mechanisms[1].travel_ms = 0U;
	fixture->mechanisms[1].reset_ms = 0U;
	fixture->mechanisms[1].kind = SG_RUNE_COMPACT_MECHANISM_BUTTON;
	fixture->mechanisms[1].initial_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[1].activated_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->mechanisms[1].reset_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[1].recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture->mechanisms[1].flags = 0U;
	fixture->mechanism_controllers[0].controller.entity_ordinal = 7U;
	fixture->mechanism_controllers[0].topology_edge = 0U;
	fixture->mechanism_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->mechanism_controllers[0].activation_cell.value = 0U;
	fixture->mechanism_controllers[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->mechanism_controllers[0].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanism_controllers[0].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->mechanism_controllers[1].controller.entity_ordinal = 11U;
	fixture->mechanism_controllers[1].topology_edge = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanism_controllers[1].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->mechanism_controllers[1].activation_cell.value = 0U;
	fixture->mechanism_controllers[1].activation_witness =
		(sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->mechanism_controllers[1].activation_bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanism_controllers[1].activation_bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->transitions[0].mechanism.value = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 1U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 100U;
	fixture->transitions[0].value.portal_state.portal.value = 0U;
	fixture->transitions[0].value.portal_state.mover_model = 1U;
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;
	fixture->transitions[0].value.portal_state.dwell_ms = 250U;
	fixture->transitions[0].value.portal_state.travel_ms = 1000U;
	fixture->transitions[0].value.portal_state.recovery_ms = 2000U;
	fixture->mechanism_edges[0].source.entity_ordinal = 7U;
	fixture->mechanism_edges[0].destination.entity_ordinal = 7U;
	fixture->mechanism_edges[0].kind =
		SG_RUNE_COMPACT_MECHANISM_EDGE_ACTIVATES;

	fixture->landmarks[0].source.entity_ordinal = 11U;
	fixture->landmarks[0].cells =
		(sg_rune_compact_landmark_cell_span_t){ 0U, 1U };
	fixture->landmarks[0].mechanism.value = 1U;
	fixture->landmarks[0].origin = (sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_BUTTON;
	fixture->landmarks[0].variant = 0U;
	fixture->landmarks[1] = fixture->landmarks[0];
	fixture->landmarks[1].source.entity_ordinal = 19U;
	fixture->landmarks[1].cells =
		(sg_rune_compact_landmark_cell_span_t){ 1U, 1U };
	fixture->landmarks[1].mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->landmarks[1].origin = (sg_rune_q8_vec3_t){ { 96, 24, 8 } };
	fixture->landmarks[1].bounds.mins =
		(sg_rune_q8_vec3_t){ { 88, 16, 0 } };
	fixture->landmarks[1].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 104, 32, 16 } };
	fixture->landmarks[1].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[1].variant = 1U;
	fixture->landmark_cells[0].value = 0U;
	fixture->landmark_cells[1].value = 1U;

	fixture->facet_annotations[0].facet.value = 0U;
	fixture->facet_annotations[0].attributes = SG_RUNE_COMPACT_FACET_HOOKABLE |
		SG_RUNE_COMPACT_FACET_COVER_NEGATIVE |
		SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY;
	fixture->facet_annotations[0].hookable_stances =
		SG_RUNE_STANCE_VALID_STANDING | SG_RUNE_STANCE_VALID_CROUCHING;
	fixture->facet_annotations[0].source_surface =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->facet_annotations[0].source_frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;

	fixture->portal_mechanisms[0].portal.value = 0U;
	fixture->portal_mechanisms[0].mechanism.value = 0U;
	fixture->portal_mechanisms[0].kind = SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;

	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 2U;
	fixture->static_data.mechanism_controllers = fixture->mechanism_controllers;
	fixture->static_data.mechanism_controller_count = 2U;
	fixture->static_data.mechanism_edges = fixture->mechanism_edges;
	fixture->static_data.mechanism_edge_count = 1U;
	fixture->static_data.transitions = fixture->transitions;
	fixture->static_data.transition_count = 1U;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 2U;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 2U;
	fixture->static_data.facet_annotations = fixture->facet_annotations;
	fixture->static_data.facet_annotation_count = 1U;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->static_data.portal_mechanism_count = 1U;

	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 2U;
	fixture->model.facets = fixture->facets;
	fixture->model.facet_count = 1U;
	fixture->model.incidences = fixture->incidences;
	fixture->model.incidence_count = 2U;
	fixture->model.portals = fixture->portals;
	fixture->model.portal_count = 1U;
	fixture->model.identity.source_counts.model_count = 2U;
	fixture->model.identity.source_counts.entity_count = 32U;
}

static void CheckValid(const static_fixture_t *fixture)
{
	sg_rune_compact_static_error_t error;
	const int valid = SG_RuneCompactStaticValidate(&fixture->model,
		&fixture->static_data, &error);

	if (!valid)
		fprintf(stderr, "unexpected static error: code=%d domain=%d record=%u\n",
			(int)error.code, (int)error.domain, error.record);
	CHECK(valid);
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_NONE);
}

static void TestStaticReferences(void)
{
	static_fixture_t fixture;

	InitFixture(&fixture);
	CheckValid(&fixture);
	CHECK(fixture.landmarks[0].mechanism.value == 1U);
	CHECK(fixture.mechanisms[0].dwell_ms == 250U);
}

static void TestFacetAnnotations(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	InitFixture(&fixture);
	fixture.facet_annotations[0].hookable_stances = 0U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
	InitFixture(&fixture);
	fixture.facet_annotations[0].attributes = SG_RUNE_COMPACT_FACET_SKY |
		SG_RUNE_COMPACT_FACET_HOOKABLE;
	fixture.facet_annotations[0].hookable_stances = SG_RUNE_STANCE_VALID_STANDING;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

static void TestNoRuntimeAvailability(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	InitFixture(&fixture);
	fixture.mechanisms[0].flags |= UINT8_C(0x80);
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
	InitFixture(&fixture);
	fixture.landmarks[1].reserved = 1U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED);
}

static void TestFiniteAngularDoorPortalAuthority(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	InitFixture(&fixture);
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	fixture.mechanisms[0].flags =
		SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE |
		SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR;
	CheckValid(&fixture);

	/* A decoded rotating portal state has no raw entity semantics to consult;
	 * the finite schedule tag is therefore required by the static record. */
	InitFixture(&fixture);
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	fixture.mechanisms[0].flags = SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);

	InitFixture(&fixture);
	fixture.mechanisms[0].flags =
		SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE |
		SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

static void TestPortalLifecycleStates(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	/* An ordinary reversible mover returns from ACTIVE to RESET. */
	InitFixture(&fixture);
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.transitions[0].value.portal_state.destination_blocked = 1U;
	CheckValid(&fixture);

	/* Toggle movers return to INITIAL because RESET remains ACTIVE. */
	InitFixture(&fixture);
	fixture.mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.mechanisms[0].reset_ms = 0U;
	fixture.mechanisms[0].recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.transitions[0].value.portal_state.destination_blocked = 1U;
	fixture.transitions[0].value.portal_state.recovery_ms = 0U;
	CheckValid(&fixture);

	/* One-shot movers cannot publish a synthetic return transition. */
	InitFixture(&fixture);
	fixture.mechanisms[0].flags = SG_RUNE_COMPACT_MECHANISM_ONE_SHOT;
	fixture.mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_DISABLED;
	fixture.mechanisms[0].reset_ms = 0U;
	fixture.mechanisms[0].recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.transitions[0].value.portal_state.destination_blocked = 1U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

static void TestMechanismTopologyAndTiming(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	InitFixture(&fixture);
	fixture.mechanisms[0].topology.count = 0U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.mechanisms[0].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
	InitFixture(&fixture);
	fixture.mechanisms[0].flags = SG_RUNE_COMPACT_MECHANISM_ONE_SHOT;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
	InitFixture(&fixture);
	fixture.transitions[0].value.portal_state.mover_model =
		fixture.model.identity.source_counts.model_count;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
	InitFixture(&fixture);
	fixture.mechanism_edges[0].destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

static void TestRequiredItemOffsetAndSentinel(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	/* The first string-table byte is a valid required-item offset. */
	InitFixture(&fixture);
	fixture.mechanisms[1].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY;
	fixture.mechanisms[1].required_item = 0U;
	CheckValid(&fixture);

	/* UINT32_MAX is the only absent-item value. */
	InitFixture(&fixture);
	fixture.mechanisms[1].required_item = SG_BSP_ENTITY_STRING_NONE;
	CheckValid(&fixture);

	/* A non-inventory mechanism cannot carry an item offset. */
	InitFixture(&fixture);
	fixture.mechanisms[1].required_item = 0U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);

	/* Inventory activation cannot omit its exact string offset. */
	InitFixture(&fixture);
	fixture.mechanisms[1].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY;
	fixture.mechanisms[1].required_item = SG_BSP_ENTITY_STRING_NONE;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

static void TestCanonicalOrderAndReferences(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	InitFixture(&fixture);
	fixture.landmarks[1].source.entity_ordinal = 11U;
	fixture.landmarks[1].kind = SG_RUNE_COMPACT_LANDMARK_BUTTON;
	fixture.landmarks[1].variant = 0U;
	fixture.landmarks[1].mechanism.value = 1U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER);
	InitFixture(&fixture);
	fixture.portal_mechanisms[0].mechanism.value = 2U;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_TELEPORTS;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.landmarks[1].source.entity_ordinal =
		fixture.model.identity.source_counts.entity_count;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

static void TestControllerSpatiality(void)
{
	static_fixture_t fixture;
	sg_rune_compact_static_error_t error;

	InitFixture(&fixture);
	fixture.mechanism_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);

	InitFixture(&fixture);
	fixture.mechanism_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	fixture.mechanism_controllers[0].activation_cell.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	memset(&fixture.mechanism_controllers[0].activation_witness, 0,
		sizeof(fixture.mechanism_controllers[0].activation_witness));
	memset(&fixture.mechanism_controllers[0].activation_bounds, 0,
		sizeof(fixture.mechanism_controllers[0].activation_bounds));
	CheckValid(&fixture);

	fixture.mechanism_controllers[0].activation_witness.value[0] = 1;
	CHECK(!SG_RuneCompactStaticValidate(&fixture.model, &fixture.static_data,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS);
}

int main(void)
{
	TestStaticReferences();
	TestFacetAnnotations();
	TestNoRuntimeAvailability();
	TestFiniteAngularDoorPortalAuthority();
	TestPortalLifecycleStates();
	TestMechanismTopologyAndTiming();
	TestRequiredItemOffsetAndSentinel();
	TestCanonicalOrderAndReferences();
	TestControllerSpatiality();
	if (failures != 0) {
		fprintf(stderr, "%d compact static checks failed\n", failures);
		return 1;
	}
	puts("compact static checks passed");
	return 0;
}
