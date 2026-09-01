#include "../slipgate/sg_rune_compact_composer.h"
#include "../slipgate/sg_rune_compact_wire.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rune_compact_model_fixture_main(void);
void *__wrap_calloc(size_t count, size_t size);

#define main rune_compact_model_fixture_main
#include "sg_rune_compact_model_test.c"
#undef main

struct sg_rune_compact_builder_s
{
	sg_rune_compact_builder_view_t view;
};

struct sg_rune_compact_geometry_s
{
	sg_rune_compact_geometry_view_t view;
};

struct sg_rune_compact_mechanisms_s
{
	sg_rune_compact_mechanisms_view_t view;
};

struct sg_rune_compact_static_materializer_s
{
	sg_rune_compact_identity_t identity;
	sg_rune_compact_static_t static_data;
};

struct sg_rune_compact_movement_fields_s
{
	sg_rune_compact_identity_t identity;
	sg_rune_compact_movement_fields_view_t view;
};

struct sg_rune_compact_weapon_field_s
{
	sg_rune_compact_weapon_field_view_t view;
};

struct sg_rune_compact_weapon_relations_s
{
	sg_rune_compact_weapon_relations_view_t view;
	sg_rune_compact_weapon_relations_view_t changed_view;
	int change_on_second_read;
};

typedef struct composer_fixture_s
{
	compact_fixture_t base;
	struct sg_rune_compact_builder_s builder;
	struct sg_rune_compact_geometry_s geometry;
	struct sg_rune_compact_mechanisms_s mechanisms;
	struct sg_rune_compact_static_materializer_s static_materializer;
	struct sg_rune_compact_movement_fields_s movement;
	struct sg_rune_compact_weapon_relations_s relations;
	struct sg_rune_compact_weapon_field_s weapon;
	sg_rune_compact_mechanism_t static_mechanisms[2];
	sg_rune_compact_static_mechanism_controller_t mechanism_controllers[1];
	sg_rune_compact_static_transition_t transitions[1];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[1];
	sg_rune_compact_mechanism_authority_t mechanism_authorities[1];
	sg_rune_compact_mechanism_controller_t authority_controllers[1];
	sg_rune_compact_mechanism_topology_edge_t authority_topology_edges[1];
	sg_rune_compact_mechanism_transition_t authority_transitions[1];
	sg_rune_movement_capability_t movement_capabilities[2];
	sg_rune_compact_movement_state_t movement_states[2];
	sg_rune_compact_movement_fiber_t movement_fibers[2];
	sg_rune_compact_movement_hook_target_t movement_hook_targets[1];
	sg_rune_analytic_function_t movement_functions[3];
	sg_rune_analytic_constant_t movement_constants[3];
	sg_rune_analytic_function_index_t movement_refs[24];
	sg_rune_compact_weapon_field_attachment_t weapon_attachments[1];
	sg_rune_compact_weapon_relation_span_t weapon_relation_spans[1];
	sg_rune_compact_response_ref_t weapon_relation_refs[1];
} composer_fixture_t;

static size_t fail_after = SIZE_MAX;
static size_t allocation_count;
static const sg_rune_compact_weapon_relations_t *relation_read_owner;
static uint32_t relation_read_count;

void *__real_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (allocation_count++ == fail_after)
		return NULL;
	return __real_calloc(count, size);
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	if (builder == NULL || view_out == NULL)
		return 0;
	*view_out = builder->view;
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

int SG_RuneCompactMechanismsRead(
	const sg_rune_compact_mechanisms_t *mechanisms,
	sg_rune_compact_mechanisms_view_t *view_out)
{
	if (mechanisms == NULL || view_out == NULL)
		return 0;
	*view_out = mechanisms->view;
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
	*static_out = materializer->static_data;
	return 1;
}

int SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t authority_transition, uint32_t *static_transition_out)
{
	if (materializer == NULL || static_transition_out == NULL ||
		authority_transition >= materializer->static_data.transition_count)
		return 0;
	*static_transition_out = authority_transition;
	return 1;
}

int SG_RuneCompactMovementFieldsReadBound(
	const sg_rune_compact_movement_fields_t *movement,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_movement_fields_view_t *view_out)
{
	if (movement == NULL || identity_out == NULL || view_out == NULL)
		return 0;
	*identity_out = movement->identity;
	*view_out = movement->view;
	return 1;
}

int SG_RuneCompactWeaponFieldReadBound(
	const sg_rune_compact_weapon_field_t *weapon,
	sg_rune_compact_weapon_field_view_t *view_out)
{
	if (weapon == NULL || view_out == NULL)
		return 0;
	*view_out = weapon->view;
	return 1;
}

int SG_RuneCompactWeaponRelationsRead(
	const sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_weapon_relations_view_t *view_out)
{
	if (relations == NULL || view_out == NULL)
		return 0;
	if (relation_read_owner != relations) {
		relation_read_owner = relations;
		relation_read_count = 0U;
	}
	if (relations->change_on_second_read != 0 &&
		relation_read_count++ != 0U) {
		*view_out = relations->changed_view;
		return 1;
	}
	relation_read_count++;
	*view_out = relations->view;
	return 1;
}

static void SetConstant(sg_rune_analytic_function_t *function,
	sg_rune_analytic_constant_t *constant,
	sg_rune_analytic_output_meaning_t output, float value, uint32_t definition)
{
	function->definition = definition;
	function->output = output;
	function->form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	constant->value.bits = Bits(value);
}

static void InitBoundStatic(composer_fixture_t *fixture)
{
	sg_rune_compact_mechanism_t *mechanism = &fixture->static_mechanisms[0];
	sg_rune_compact_mechanism_t *rotator = &fixture->static_mechanisms[1];
	sg_rune_compact_static_transition_t *transition = &fixture->transitions[0];

	fixture->base.landmarks[0].source.entity_ordinal = 1U;
	fixture->base.landmarks[0].mechanism.value = 0U;
	fixture->base.landmarks[0].kind =
		SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
	mechanism->source.entity_ordinal = 1U;
	mechanism->entry_cell.value = 0U;
	mechanism->exit_cell.value = 1U;
	mechanism->activation_landmark.value = 0U;
	mechanism->bounds.mins = (sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	mechanism->bounds.maxs = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	mechanism->controllers =
		(sg_rune_compact_mechanism_controller_span_t){ 0U, 1U };
	mechanism->topology =
		(sg_rune_compact_mechanism_edge_span_t){ 0U, 0U };
	mechanism->transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 1U };
	mechanism->activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY;
	mechanism->required_item = 77U;
	mechanism->transition_destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	mechanism->transition_fanout_ordinal = UINT32_MAX;
	mechanism->kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	mechanism->initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	mechanism->activated_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	mechanism->reset_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	mechanism->recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	mechanism->travel_ms = 1U;
	fixture->mechanism_controllers[0].controller.entity_ordinal = 2U;
	fixture->mechanism_controllers[0].topology_edge =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanism_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->mechanism_controllers[0].activation_cell.value = 0U;
	fixture->mechanism_controllers[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 16, 16, 16 } };
	fixture->mechanism_controllers[0].activation_bounds =
		fixture->base.cells[0].bounds;
	transition->mechanism.value = 0U;
	transition->value.portal_state.portal.value = 0U;
	transition->value.portal_state.mover_model = 0U;
	transition->value.portal_state.travel_ms = 1U;
	transition->entry_cell.value = 0U;
	transition->exit_cell.value = 1U;
	transition->source_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	transition->destination_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	transition->elapsed_ms = 1U;
	transition->value.portal_state.source_blocked = 1U;
	transition->value.portal_state.destination_blocked = 0U;
	transition->kind = SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE;
	fixture->portal_mechanisms[0].portal.value = 0U;
	fixture->portal_mechanisms[0].mechanism.value = 0U;
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	rotator->source.entity_ordinal = 3U;
	rotator->entry_cell.value = 1U;
	rotator->exit_cell.value = 1U;
	rotator->activation_landmark.value = SG_RUNE_COMPACT_INDEX_NONE;
	rotator->bounds = fixture->base.cells[1].bounds;
	rotator->controllers =
		(sg_rune_compact_mechanism_controller_span_t){ 1U, 0U };
	rotator->topology =
		(sg_rune_compact_mechanism_edge_span_t){ 0U, 0U };
	rotator->transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 1U, 0U };
	rotator->activation_mask = SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO;
	rotator->required_item = SG_BSP_ENTITY_STRING_NONE;
	rotator->transition_destination.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	rotator->transition_fanout_ordinal = UINT32_MAX;
	rotator->kind = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	rotator->initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	rotator->activated_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	rotator->reset_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	rotator->recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture->static_materializer.static_data.mechanisms =
		fixture->static_mechanisms;
	fixture->static_materializer.static_data.mechanism_count = 2U;
	fixture->static_materializer.static_data.mechanism_controllers =
		fixture->mechanism_controllers;
	fixture->static_materializer.static_data.mechanism_controller_count = 1U;
	fixture->static_materializer.static_data.transitions = fixture->transitions;
	fixture->static_materializer.static_data.transition_count = 1U;
	fixture->static_materializer.static_data.portal_mechanisms =
		fixture->portal_mechanisms;
	fixture->static_materializer.static_data.portal_mechanism_count = 1U;
}

static void InitComposerFixture(composer_fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	relation_read_owner = NULL;
	relation_read_count = 0U;
	InitFixture(&fixture->base);
	fixture->builder.view.identity = fixture->base.model.identity;
	fixture->builder.view.weapon_profiles = fixture->base.weapon_profiles;
	fixture->builder.view.weapon_profile_count =
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT;
	fixture->geometry.view.identity = fixture->base.model.identity;
	fixture->geometry.view.cells = fixture->base.cells;
	fixture->geometry.view.cell_count = 2U;
	fixture->geometry.view.facets = fixture->base.facets;
	fixture->geometry.view.facet_count = 1U;
	fixture->geometry.view.incidences = fixture->base.incidences;
	fixture->geometry.view.incidence_count = 2U;
	fixture->geometry.view.cell_incidences = fixture->base.cell_incidences;
	fixture->geometry.view.cell_incidence_count = 2U;
	fixture->geometry.view.vertices = fixture->base.vertices;
	fixture->geometry.view.vertex_count = 4U;
	fixture->geometry.view.portals = fixture->base.portals;
	fixture->geometry.view.portal_count = 1U;
	fixture->geometry.view.source_surfaces = fixture->base.source_surfaces;
	fixture->geometry.view.source_surface_count = 3U;
	fixture->geometry.view.source_surface_vertices =
		fixture->base.source_surface_vertices;
	fixture->geometry.view.source_surface_vertex_count = 12U;
	fixture->static_materializer.identity = fixture->base.model.identity;
	fixture->static_materializer.static_data = fixture->base.static_data;
	InitBoundStatic(fixture);
	fixture->mechanisms.view.identity = fixture->base.model.identity;
	fixture->mechanisms.view.mechanisms = fixture->mechanism_authorities;
	fixture->mechanisms.view.mechanism_count = 1U;
	fixture->mechanisms.view.controllers = fixture->authority_controllers;
	fixture->mechanisms.view.controller_count = 1U;
	fixture->mechanisms.view.topology_edges = fixture->authority_topology_edges;
	fixture->mechanisms.view.topology_edge_count = 1U;
	fixture->mechanisms.view.transitions = fixture->authority_transitions;
	fixture->mechanisms.view.transition_count = 1U;
	fixture->mechanism_authorities[0].source.entity_ordinal = 1U;
	fixture->mechanism_authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture->mechanism_authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	fixture->mechanism_authorities[0].activation_cell.value = 0U;
	fixture->mechanism_authorities[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 16, 16, 16 } };
	fixture->mechanism_authorities[0].activation_bounds =
		fixture->base.cells[0].bounds;
	fixture->mechanism_authorities[0].controllers =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanism_authorities[0].topology =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanism_authorities[0].transitions =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture->mechanism_authorities[0].required_item =
		SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanism_authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanism_authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->mechanism_authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanism_authorities[0].travel_ms = 1U;
	fixture->authority_controllers[0].mechanism = 0U;
	fixture->authority_controllers[0].controller.entity_ordinal = 2U;
	fixture->authority_controllers[0].topology_edge = 0U;
	fixture->authority_controllers[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE;
	fixture->authority_controllers[0].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->authority_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->authority_controllers[0].activation_cell.value = 0U;
	fixture->authority_controllers[0].activation_witness =
		(sg_rune_q8_vec3_t){ { 16, 16, 16 } };
	fixture->authority_controllers[0].activation_bounds =
		fixture->base.cells[0].bounds;
	fixture->authority_topology_edges[0].source.entity_ordinal = 2U;
	fixture->authority_topology_edges[0].destination.entity_ordinal = 1U;
	fixture->authority_topology_edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->authority_transitions[0].mechanism = 0U;
	fixture->authority_transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	fixture->authority_transitions[0].entry_cell.value = 0U;
	fixture->authority_transitions[0].exit_cell.value = 1U;
	fixture->authority_transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authority_transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authority_transitions[0].elapsed_ms = 1U;
	fixture->authority_transitions[0].value.portal_state.portal.value = 0U;
	fixture->authority_transitions[0].value.portal_state.mover_model = 0U;
	fixture->authority_transitions[0].value.portal_state.travel_ms = 1U;
	fixture->authority_transitions[0].value.portal_state.source_blocked = 1U;
	fixture->movement.identity = fixture->base.model.identity;
	fixture->movement.view.identity = fixture->base.model.identity;
	fixture->movement.view.capabilities = fixture->movement_capabilities;
	fixture->movement.view.capability_count = 2U;
	fixture->movement.view.states = fixture->movement_states;
	fixture->movement.view.state_count = 2U;
	fixture->movement.view.fibers = fixture->movement_fibers;
	fixture->movement.view.fiber_count = 2U;
	fixture->movement.view.hook_targets = fixture->movement_hook_targets;
	fixture->movement.view.hook_target_count = 1U;
	fixture->movement.view.fiber_function_refs = fixture->movement_refs;
	fixture->movement.view.fiber_function_ref_count = 24U;
	fixture->movement.view.physics_abi_id = fixture->base.model.identity.physics_abi_id;
	fixture->movement.view.collision_law_id =
		fixture->base.model.identity.collision_law_id;
	fixture->movement.view.pmove_law_id = fixture->base.model.identity.pmove_law_id;
	fixture->movement.view.gravity_law_id =
		fixture->base.model.identity.gravity_law_id;
	fixture->movement.view.hook_law_id = fixture->base.model.identity.hook_law_id;
	fixture->movement.view.mechanism_law_id =
		fixture->base.model.identity.mechanism_law_id;
	fixture->movement.view.pmove_abi = fixture->base.model.movement_pmove_abi;
	fixture->movement.view.pmove_behavior_fingerprint =
		fixture->base.model.movement_pmove_behavior_fingerprint;
	fixture->movement.view.host_level_generation =
		fixture->base.model.movement_host_level_generation;
	SetConstant(&fixture->movement_functions[0], &fixture->movement_constants[0],
		SG_RUNE_ANALYTIC_OUTPUT_COST, 1.0f, 0U);
	SetConstant(&fixture->movement_functions[1], &fixture->movement_constants[1],
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS, 2.0f, 1U);
	SetConstant(&fixture->movement_functions[2], &fixture->movement_constants[2],
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN, 4.0f, 2U);
	fixture->movement.view.analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->movement.view.analytic.functions = fixture->movement_functions;
	fixture->movement.view.analytic.function_count = 3U;
	fixture->movement.view.analytic.constants = fixture->movement_constants;
	fixture->movement.view.analytic.constant_count = 3U;
	for (index = 0U; index < 24U; index++)
		fixture->movement_refs[index].value = index % 3U;
	fixture->movement_capabilities[0].cell.value = 0U;
	fixture->movement_capabilities[0].boundary_portal.value = 0U;
	fixture->movement_capabilities[0].kind =
		SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;
	fixture->movement_capabilities[0].source_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_capabilities[0].destination_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_capabilities[0].fibers =
		(sg_rune_movement_fiber_span_t){ 0U, 1U };
	fixture->movement_capabilities[1].cell.value = 1U;
	fixture->movement_capabilities[1].boundary_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_capabilities[1].kind = SG_RUNE_MOVEMENT_CAPABILITY_WALK;
	fixture->movement_capabilities[1].source_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_capabilities[1].destination_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_capabilities[1].fibers =
		(sg_rune_movement_fiber_span_t){ 1U, 1U };
	fixture->movement_states[0].stance = SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_states[0].support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	fixture->movement_states[0].water = SG_RUNE_MOVEMENT_WATER_DRY;
	fixture->movement_states[0].hook_phase = SG_HOST_HOOK_IDLE;
	fixture->movement_states[0].mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_states[1] = fixture->movement_states[0];
	fixture->movement_states[1].hook_phase = SG_HOST_HOOK_IN_FLIGHT;
	fixture->movement_fibers[0].capability.value = 0U;
	fixture->movement_fibers[0].kind = SG_RUNE_MOVEMENT_FIBER_HOOK;
	fixture->movement_fibers[0].state_variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_HOOK |
		SG_RUNE_MOVEMENT_STATE_TIME;
	fixture->movement_fibers[0].source_state.value = 0U;
	fixture->movement_fibers[0].destination_state.value = 1U;
	fixture->movement_fibers[0].functions =
		(sg_rune_analytic_function_span_t){ 0U, 3U };
	fixture->movement_fibers[0].hook_targets =
		(sg_rune_movement_hook_target_span_t){ 0U, 1U };
	fixture->movement_fibers[0].mechanism_transition.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].controller_action_controller.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].controller_action_target.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].capability.value = 1U;
	fixture->movement_fibers[1].kind = SG_RUNE_MOVEMENT_FIBER_PMOVE;
	fixture->movement_fibers[1].state_variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_SUPPORT |
		SG_RUNE_MOVEMENT_STATE_TIME;
	fixture->movement_fibers[1].functions =
		(sg_rune_analytic_function_span_t){ 3U, 3U };
	fixture->movement_fibers[1].hook_targets =
		(sg_rune_movement_hook_target_span_t){ 1U, 0U };
	fixture->movement_fibers[1].mechanism_transition.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].controller_action_controller.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1].controller_action_target.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_hook_targets[0].fiber.value = 0U;
	fixture->movement_hook_targets[0].target_kind = SG_HOST_HOOK_TARGET_WORLD;
	fixture->movement_hook_targets[0].provenance =
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
	fixture->movement_hook_targets[0].response =
		(sg_rune_compact_response_ref_t){
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U };
	fixture->movement_hook_targets[0].visibility_class =
		SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL;
	fixture->movement_hook_targets[0].source_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_hook_targets[0].target_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_hook_targets[0].functions.bolt =
		(sg_rune_analytic_function_span_t){ 6U, 3U };
	fixture->movement_hook_targets[0].functions.body =
		(sg_rune_analytic_function_span_t){ 9U, 3U };
	fixture->movement_hook_targets[0].functions.pull =
		(sg_rune_analytic_function_span_t){ 12U, 3U };
	fixture->movement_hook_targets[0].functions.release =
		(sg_rune_analytic_function_span_t){ 15U, 3U };
	fixture->movement_hook_targets[0].functions.coast =
		(sg_rune_analytic_function_span_t){ 18U, 3U };
	fixture->movement_hook_targets[0].functions.relaunch =
		(sg_rune_analytic_function_span_t){ 21U, 3U };
	fixture->weapon.view.identity = fixture->base.model.identity;
	fixture->weapon.view.kernels = fixture->base.weapon_kernels;
	fixture->weapon.view.kernel_count = fixture->base.model.weapon_kernel_count;
	fixture->weapon_attachments[0].cell.value = 0U;
	fixture->weapon_attachments[0].source_surface = 0U;
	fixture->weapon_attachments[0].relation_class =
		SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT;
	fixture->weapon_attachments[0].relations =
		(sg_rune_compact_response_ref_span_t){ 0U, 1U };
	fixture->weapon_attachments[0].relation_span = 0U;
	fixture->weapon_relation_spans[0].references =
		(sg_rune_compact_response_ref_span_t){ 0U, 1U };
	fixture->weapon_relation_refs[0] = (sg_rune_compact_response_ref_t){
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U };
	fixture->weapon.view.attachments = fixture->weapon_attachments;
	fixture->weapon.view.attachment_count = 1U;
	fixture->weapon.view.relation_spans = fixture->weapon_relation_spans;
	fixture->weapon.view.relation_span_count = 1U;
	fixture->weapon.view.relation_refs = fixture->weapon_relation_refs;
	fixture->weapon.view.relation_ref_count = 1U;
	fixture->weapon.view.weapon_function_refs =
		fixture->base.weapon_function_refs;
	fixture->weapon.view.weapon_function_ref_count =
		fixture->base.model.weapon_function_ref_count;
	fixture->weapon.view.analytic = fixture->base.analytic;
	fixture->relations.view.version =
		SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION;
	fixture->relations.view.identity = fixture->base.model.identity;
	fixture->relations.view.response = fixture->base.model.response;
	fixture->weapon.view.response = &fixture->relations.view.response;
	fixture->relations.changed_view = fixture->relations.view;
	fixture->relations.changed_view.response.source_fragments = NULL;
}

static int BuildFixture(composer_fixture_t *fixture,
	sg_rune_compact_composer_t **composer_out,
	sg_rune_compact_composer_error_t *error_out)
{
	return SG_RuneCompactComposerBuild(&fixture->builder, &fixture->geometry,
		&fixture->mechanisms, &fixture->static_materializer, &fixture->movement,
		&fixture->relations, &fixture->weapon, composer_out, error_out);
}

static void CheckCanonicalWeaponContract(const sg_rune_compact_model_t *model)
{
	uint32_t index;

	CHECK(model->weapon_profile_count ==
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT);
	CHECK(model->weapon_kernel_count != 0U);
	for (index = 0U; index < model->weapon_kernel_count; index++) {
		const sg_rune_weapon_response_kernel_t *kernel =
			&model->weapon_kernels[index];
		sg_rune_weapon_event_law_t expected;

		CHECK(kernel->profile < model->weapon_profile_count);
		if (kernel->profile >= model->weapon_profile_count)
			continue;
		CHECK(SG_RuneCompactWeaponCanonicalEventLaw(
			model->weapon_profiles[kernel->profile].source_profile,
			kernel->family, &expected));
		CHECK(kernel->event_law.kind == expected.kind);
		CHECK(kernel->event_law.requirements == expected.requirements);
		if (kernel->family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT ||
			kernel->family == SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE) {
			CHECK(kernel->event_law.kind ==
				SG_RUNE_WEAPON_EVENT_GRENADE_BOUNCE_FUSE);
			CHECK((kernel->event_law.requirements &
				SG_RUNE_WEAPON_RUNTIME_FUSE_DEADLINE) != 0U);
		}
		if (kernel->family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT)
			CHECK(kernel->functions.count > 2U);
	}
}

static void TestSuccessfulComposition(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;
	const sg_rune_compact_model_t *model;

	InitComposerFixture(&fixture);
	fail_after = SIZE_MAX;
	allocation_count = 0U;
	{
		const int built = BuildFixture(&fixture, &composer, &error);

		CHECK(built);
		if (!built)
			return;
	}
	model = SG_RuneCompactComposerModel(composer);
	CHECK(model != NULL);
	if (model == NULL) {
		SG_RuneCompactComposerDestroy(composer);
		return;
	}
	CHECK(SG_RuneCompactModelValidateBound(model, &fixture.base.model.identity,
		NULL));
	CHECK(model->analytic->function_count == 8U);
	CHECK(model->analytic->functions[1].output ==
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS);
	CHECK(model->movement_capability_count == 2U);
	CHECK(model->movement_state_count == 2U);
	CHECK(model->movement_fiber_count == 2U);
	CHECK(model->movement_hook_target_count == 1U);
	CHECK(model->movement_fiber_function_ref_count == 24U);
	CHECK(model->movement_capabilities != fixture.movement_capabilities);
	CHECK(model->movement_states != fixture.movement_states);
	CHECK(model->movement_fibers != fixture.movement_fibers);
	CHECK(model->movement_hook_targets != fixture.movement_hook_targets);
	CHECK(model->movement_fiber_function_refs != fixture.movement_refs);
	CHECK(model->movement_fibers[0].kind == SG_RUNE_MOVEMENT_FIBER_HOOK);
	CHECK(model->movement_hook_targets[0].fiber.value == 0U);
	CHECK(model->movement_hook_targets[0].response.kind ==
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
	CHECK(model->movement_hook_targets[0].functions.relaunch.first == 21U);
	CHECK(model->weapon_function_refs[0].channel ==
		SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY);
	CHECK(model->weapon_attachment_count == 1U);
	CHECK(model->weapon_relation_span_count == 1U);
	CHECK(model->weapon_relation_ref_count == 1U);
	CHECK(model->weapon_attachments != fixture.weapon_attachments);
	CHECK(model->weapon_relation_spans != fixture.weapon_relation_spans);
	CHECK(model->weapon_relation_refs != fixture.weapon_relation_refs);
	CHECK(model->weapon_attachments[0].relations.count == 1U);
	CHECK(model->weapon_attachments[0].relation_span == 0U);
	CHECK(model->weapon_relation_spans[0].references.count == 1U);
	CHECK(model->weapon_relation_refs[0].kind ==
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
	CHECK(model->cells[0].movement_fields.count == 1U);
	CHECK(model->cells[1].movement_fields.count == 1U);
	CHECK(model->response.source_fragment_count ==
		fixture.relations.view.response.source_fragment_count);
	CHECK(model->response.target_patch_count ==
		fixture.relations.view.response.target_patch_count);
	CHECK(model->response.candidate_group_count ==
		fixture.relations.view.response.candidate_group_count);
	CHECK(model->response.source_fragments !=
		fixture.relations.view.response.source_fragments);
	CHECK(model->response.target_patches !=
		fixture.relations.view.response.target_patches);
	CHECK(model->response.candidate_groups !=
		fixture.relations.view.response.candidate_groups);
	CHECK(memcmp(model->response.source_fragments,
		fixture.relations.view.response.source_fragments,
		(size_t)model->response.source_fragment_count *
			sizeof(*model->response.source_fragments)) == 0);
	CHECK(model->movement_angular_schedule_count == 0U);
	CHECK(model->source_surface_count == fixture.geometry.view.source_surface_count);
	CHECK(model->source_surface_vertex_count ==
		fixture.geometry.view.source_surface_vertex_count);
	CHECK(model->source_surfaces != fixture.geometry.view.source_surfaces);
	CHECK(model->source_surface_vertices !=
		fixture.geometry.view.source_surface_vertices);
	CHECK(memcmp(model->source_surfaces, fixture.geometry.view.source_surfaces,
		(size_t)model->source_surface_count *
			sizeof(*model->source_surfaces)) == 0);
	CHECK(memcmp(model->source_surface_vertices,
		fixture.geometry.view.source_surface_vertices,
		(size_t)model->source_surface_vertex_count *
			sizeof(*model->source_surface_vertices)) == 0);
	CHECK(model->static_data->mechanism_count == 2U);
	CHECK(model->static_data->mechanism_controller_count == 1U);
	CHECK(model->static_data->transition_count == 1U);
	CHECK(model->static_data->mechanisms != fixture.static_mechanisms);
	CHECK(model->static_data->mechanism_controllers !=
		fixture.mechanism_controllers);
	CHECK(model->static_data->transitions != fixture.transitions);
	CHECK(model->static_data->mechanisms[0].activation_mask ==
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY);
	CHECK(model->static_data->mechanisms[0].required_item == 77U);
	CHECK(model->static_data->mechanism_controllers[0].controller.entity_ordinal
		== 2U);
	CHECK(model->static_data->transitions[0].kind ==
		SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE);
	CHECK(model->mechanism_authority_count == 1U);
	CHECK(model->mechanism_authority_controller_count == 1U);
	CHECK(model->mechanism_authority_topology_edge_count == 1U);
	CHECK(model->mechanism_authority_transition_count == 1U);
	CHECK(model->mechanism_authorities != fixture.mechanism_authorities);
	CHECK(model->mechanism_authority_controllers !=
		fixture.authority_controllers);
	CHECK(model->mechanism_authority_topology_edges !=
		fixture.authority_topology_edges);
	CHECK(model->mechanism_authority_transitions !=
		fixture.authority_transitions);
	CHECK(model->mechanism_authorities[0].source.entity_ordinal == 1U);
	CHECK(model->mechanism_authority_transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE);
	CheckCanonicalWeaponContract(model);
	SG_RuneCompactComposerDestroy(composer);
}

static void TestStaticBoundArrayRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.static_materializer.static_data.mechanism_controllers = NULL;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
	InitComposerFixture(&fixture);
	fixture.static_materializer.static_data.transitions = NULL;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestMechanismOwnerArrayRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.mechanisms.view.transitions = NULL;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestIdentityRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.weapon.view.identity.bsp_checksum++;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH);
	InitComposerFixture(&fixture);
	fixture.geometry.view.identity.bsp_checksum++;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH);
	InitComposerFixture(&fixture);
	fixture.relations.view.identity.bsp_checksum++;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH);
	InitComposerFixture(&fixture);
	fixture.mechanisms.view.identity.bsp_checksum++;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH);
}

static void TestSourceSurfaceRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.base.source_surfaces[1].vertices.first++;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED);

	InitComposerFixture(&fixture);
	fixture.base.source_surfaces[1].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED);

	InitComposerFixture(&fixture);
	fixture.base.source_surfaces[0].source.model =
		fixture.base.model.identity.source_counts.model_count;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED);

	InitComposerFixture(&fixture);
	fixture.base.source_surface_vertices[4] =
		fixture.base.source_surface_vertices[5];
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED);

	InitComposerFixture(&fixture);
	fixture.geometry.view.source_surfaces = NULL;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);

	InitComposerFixture(&fixture);
	fixture.geometry.view.source_surface_vertices = NULL;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestLimitRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.movement.view.capability_count =
		SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS + 1U;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED);
	InitComposerFixture(&fixture);
	fixture.weapon.view.attachment_count =
		SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS + 1U;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED);
	InitComposerFixture(&fixture);
	fixture.geometry.view.source_surface_count =
		SG_RUNE_COMPACT_MAX_SOURCE_SURFACES + 1U;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED);
	InitComposerFixture(&fixture);
	fixture.geometry.view.source_surface_vertex_count =
		SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES + 1U;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED);
}

static void TestUncoveredReferenceRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.movement_refs[5].value = UINT32_MAX;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestUncoveredWeaponReferenceRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;
	uint32_t reference;

	InitComposerFixture(&fixture);
	CHECK(fixture.weapon.view.weapon_function_ref_count != 0U);
	if (fixture.weapon.view.weapon_function_ref_count == 0U)
		return;
	reference = fixture.weapon.view.weapon_function_ref_count - 1U;
	fixture.base.weapon_function_refs[reference].function.value = UINT32_MAX;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestUncoveredHookReferenceRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.movement_refs[8].value = UINT32_MAX;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestUncoveredResponseReferenceRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.movement_hook_targets[0].response.index = UINT32_MAX;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT);
}

static void TestRelationCurrentnessRejection(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;

	InitComposerFixture(&fixture);
	fixture.relations.change_on_second_read = 1;
	CHECK(!BuildFixture(&fixture, &composer, &error));
	CHECK(composer == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH);
}

static void TestDeterminism(void)
{
	composer_fixture_t first;
	composer_fixture_t second;
	sg_rune_compact_composer_t *first_composer = NULL;
	sg_rune_compact_composer_t *second_composer = NULL;
	sg_rune_compact_composer_error_t error;
	const sg_rune_compact_model_t *first_model;
	const sg_rune_compact_model_t *second_model;

	InitComposerFixture(&first);
	InitComposerFixture(&second);
	{
		const int first_built = BuildFixture(&first, &first_composer, &error);
		const int second_built = BuildFixture(&second, &second_composer,
			&error);

		CHECK(first_built);
		CHECK(second_built);
		if (!first_built || !second_built) {
			SG_RuneCompactComposerDestroy(second_composer);
			SG_RuneCompactComposerDestroy(first_composer);
			return;
		}
	}
	first_model = SG_RuneCompactComposerModel(first_composer);
	second_model = SG_RuneCompactComposerModel(second_composer);
	CHECK(first_model != NULL);
	CHECK(second_model != NULL);
	if (first_model == NULL || second_model == NULL) {
		SG_RuneCompactComposerDestroy(second_composer);
		SG_RuneCompactComposerDestroy(first_composer);
		return;
	}
	CHECK(first_model->analytic->function_count == second_model->analytic->function_count);
	CHECK(memcmp(first_model->analytic->functions, second_model->analytic->functions,
		(size_t)first_model->analytic->function_count *
			sizeof(*first_model->analytic->functions)) == 0);
	CHECK(memcmp(first_model->analytic->constants, second_model->analytic->constants,
		(size_t)first_model->analytic->constant_count *
			sizeof(*first_model->analytic->constants)) == 0);
	CHECK(memcmp(first_model->movement_fiber_function_refs,
		second_model->movement_fiber_function_refs,
		(size_t)first_model->movement_fiber_function_ref_count *
			sizeof(*first_model->movement_fiber_function_refs)) == 0);
	CHECK(memcmp(first_model->weapon_attachments,
		second_model->weapon_attachments,
		(size_t)first_model->weapon_attachment_count *
			 sizeof(*first_model->weapon_attachments)) == 0);
	CHECK(memcmp(first_model->weapon_relation_spans,
		second_model->weapon_relation_spans,
		(size_t)first_model->weapon_relation_span_count *
			sizeof(*first_model->weapon_relation_spans)) == 0);
	CHECK(memcmp(first_model->weapon_relation_refs,
		second_model->weapon_relation_refs,
		(size_t)first_model->weapon_relation_ref_count *
			sizeof(*first_model->weapon_relation_refs)) == 0);
	CHECK(memcmp(first_model->mechanism_authorities,
		second_model->mechanism_authorities,
		(size_t)first_model->mechanism_authority_count *
			sizeof(*first_model->mechanism_authorities)) == 0);
	CHECK(memcmp(first_model->weapon_function_refs,
		second_model->weapon_function_refs,
		(size_t)first_model->weapon_function_ref_count *
			sizeof(*first_model->weapon_function_refs)) == 0);
	SG_RuneCompactComposerDestroy(second_composer);
	SG_RuneCompactComposerDestroy(first_composer);
}

static void TestAllocationFailures(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t error;
	size_t ordinal;
	size_t successful_allocations;

	InitComposerFixture(&fixture);
	fail_after = SIZE_MAX;
	allocation_count = 0U;
	{
		const int built = BuildFixture(&fixture, &composer, &error);

		CHECK(built);
		if (!built) {
			fail_after = SIZE_MAX;
			return;
		}
	}
	successful_allocations = allocation_count;
	SG_RuneCompactComposerDestroy(composer);
	for (ordinal = 0U; ordinal < successful_allocations; ordinal++) {
		InitComposerFixture(&fixture);
		composer = (sg_rune_compact_composer_t *)(uintptr_t)1U;
		fail_after = ordinal;
		allocation_count = 0U;
		CHECK(!BuildFixture(&fixture, &composer, &error));
		CHECK(composer == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY);
	}
	fail_after = SIZE_MAX;
}

static void TestComposerWireRoundTrip(void)
{
	composer_fixture_t fixture;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_composer_error_t composer_error;
	sg_rune_compact_wire_error_t wire_error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	const sg_rune_compact_model_t *model;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	size_t written = 0U;

	InitComposerFixture(&fixture);
	fail_after = SIZE_MAX;
	allocation_count = 0U;
	if (!BuildFixture(&fixture, &composer, &composer_error)) {
		CHECK(0);
		goto done;
	}
	model = SG_RuneCompactComposerModel(composer);
	if (model == NULL || !SG_RuneCompactWireMeasure(model, &image_size,
		&wire_error)) {
		CHECK(0);
		goto done;
	}
	image = malloc(image_size);
	if (image == NULL || !SG_RuneCompactWireEncode(model, image, image_size,
		&written, &wire_error) || written != image_size ||
		!SG_RuneCompactWireInspect(image, image_size, NULL, &wire_error) ||
		!SG_RuneCompactWireDecode(image, image_size, &model->identity, &decoded,
			&wire_error))
		CHECK(0);

done:
	SG_RuneCompactWireDestroy(decoded);
	free(image);
	SG_RuneCompactComposerDestroy(composer);
}

int main(void)
{
	failures = 0;
	TestSuccessfulComposition();
	TestStaticBoundArrayRejection();
	TestMechanismOwnerArrayRejection();
	TestIdentityRejection();
	TestSourceSurfaceRejection();
	TestLimitRejection();
	TestUncoveredReferenceRejection();
	TestUncoveredWeaponReferenceRejection();
	TestUncoveredHookReferenceRejection();
	TestUncoveredResponseReferenceRejection();
	TestRelationCurrentnessRejection();
	TestDeterminism();
	TestAllocationFailures();
	TestComposerWireRoundTrip();
	if (failures != 0)
		return 1;
	printf("compact RUNE composer checks passed\n");
	return 0;
}
