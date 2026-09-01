#include "../slipgate/sg_rune_compact_generation.h"
#include "../slipgate/sg_rune_compact_builder_owner.h"
#include "../slipgate/sg_rune_compact_weapon_relations.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 0; \
	} \
} while (0)

struct sg_rune_compact_builder_s { uint32_t unused; };
struct sg_rune_compact_geometry_s { uint32_t unused; };
struct sg_rune_compact_response_partition_s { uint32_t unused; };
struct sg_rune_compact_mechanisms_s { uint32_t unused; };
struct sg_rune_compact_static_materializer_s { uint32_t unused; };
struct sg_rune_compact_movement_fields_s { uint32_t unused; };
struct sg_rune_compact_weapon_relations_s { uint32_t unused; };
struct sg_rune_compact_weapon_field_s { uint32_t unused; };
struct sg_rune_compact_composer_s { uint32_t unused; };
struct sg_rune_compact_wire_decoded_s { uint32_t unused; };

typedef struct generation_mock_s
{
	sg_rune_compact_generation_stage_t fail_stage;
	sg_rune_compact_generation_stage_t mismatch_stage;
	sg_rune_compact_generation_stage_t entered[
		SG_RUNE_COMPACT_GENERATION_STAGE_COUNT];
	sg_rune_compact_generation_stage_t destroyed[
		SG_RUNE_COMPACT_GENERATION_STAGE_COUNT];
	sg_rune_compact_generation_stage_t progressed[
		SG_RUNE_COMPACT_GENERATION_STAGE_COUNT];
	uint32_t entered_count;
	uint32_t destroyed_count;
	uint32_t progressed_count;
	uint32_t publication_calls;
	uint32_t response_build_calls;
	const sg_rune_compact_response_partition_t *movement_response_owner;
	const sg_rune_compact_response_partition_t *relation_response_owner;
	int invalid_response_seal;
	int input_error;
} generation_mock_t;

static generation_mock_t mock;
static struct sg_rune_compact_builder_s builder;
static struct sg_rune_compact_geometry_s geometry;
static struct sg_rune_compact_response_partition_s response_partition;
static struct sg_rune_compact_mechanisms_s mechanisms;
static struct sg_rune_compact_static_materializer_s static_materializer;
static struct sg_rune_compact_movement_fields_s movement_fields;
static struct sg_rune_compact_weapon_relations_s relations;
static struct sg_rune_compact_weapon_field_s weapon_field;
static struct sg_rune_compact_composer_s composer;
static struct sg_rune_compact_wire_decoded_s decoded;
static sg_rune_compact_identity_t identity;
static sg_rune_compact_identity_t alternate_identity;
static sg_rune_compact_response_fragment_t response_fragment;
static sg_rune_compact_response_halfspace_t response_halfspace;
static sg_rune_compact_response_patch_t response_patch;
static sg_rune_q8_vec3_t response_vertex;
static sg_rune_compact_response_split_t response_split;
static sg_rune_compact_response_pair_t response_pair;
static sg_rune_compact_response_fact_t relation_fact;
static sg_rune_compact_response_candidate_group_t relation_candidate_group;
static sg_rune_compact_response_endpoint_group_t relation_source_group;
static sg_rune_compact_response_endpoint_group_t relation_target_group;
static uint32_t relation_source_member;
static uint32_t relation_target_member;
static sg_rune_compact_static_occluder_t relation_occluder;
static sg_rune_compact_model_t model;
static sg_rune_compact_analytic_t analytic;
static sg_host_law_view_t host_law;
static sg_rune_source_weapon_law_t weapon_law;
static sg_configuration_semantics_t semantics;
static sg_bsp_entity_semantics_t entities;
static sg_static_visibility_t visibility;
static sg_rune_compact_mechanism_authority_t mechanism_authorities[7];
static sg_rune_compact_mechanism_controller_t mechanism_controllers[8];
static sg_rune_compact_mechanism_topology_edge_t mechanism_topology_edges[9];
static sg_rune_compact_mechanism_transition_t mechanism_transitions[10];
static unsigned char construction_sentinel;
static sg_configuration_limits_t configuration_limits;
static sg_configuration_semantics_limits_t semantics_limits;
static sg_static_visibility_limits_t visibility_limits;
static sg_rune_compact_cell_t cell;
static sg_rune_compact_facet_t facet;
static sg_rune_compact_incidence_t incidence;
static sg_rune_compact_incidence_index_t cell_incidence;
static sg_rune_q8_vec3_t vertex;
static sg_rune_compact_portal_t portal;
static sg_rune_compact_source_surface_t source_surface;
static sg_rune_q8_vec3_t source_surface_vertex;
static sg_rune_compact_static_t static_view;
static sg_rune_weapon_profile_t compact_profile;
static sg_weapon_profile_t resolved_profile;

static int Enter(sg_rune_compact_generation_stage_t stage)
{
	mock.entered[mock.entered_count++] = stage;
	return stage != mock.fail_stage;
}

static void Require(int condition)
{
	if (!condition)
		mock.input_error = 1;
}

static int IdentityEqual(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	return left != NULL && right != NULL &&
		memcmp(left, right, sizeof(*left)) == 0;
}

static void Destroyed(sg_rune_compact_generation_stage_t stage)
{
	mock.destroyed[mock.destroyed_count++] = stage;
}

static void ResetMock(sg_rune_compact_generation_stage_t fail_stage)
{
	memset(&mock, 0, sizeof(mock));
	memset(&identity, 0, sizeof(identity));
	memset(&alternate_identity, 0, sizeof(alternate_identity));
	memset(&response_fragment, 0, sizeof(response_fragment));
	memset(&response_halfspace, 0, sizeof(response_halfspace));
	memset(&response_patch, 0, sizeof(response_patch));
	memset(&response_vertex, 0, sizeof(response_vertex));
	memset(&response_split, 0, sizeof(response_split));
	memset(&response_pair, 0, sizeof(response_pair));
	memset(&relation_fact, 0, sizeof(relation_fact));
	memset(&relation_candidate_group, 0, sizeof(relation_candidate_group));
	memset(&relation_source_group, 0, sizeof(relation_source_group));
	memset(&relation_target_group, 0, sizeof(relation_target_group));
	memset(&relation_occluder, 0, sizeof(relation_occluder));
	memset(&model, 0, sizeof(model));
	memset(&analytic, 0, sizeof(analytic));
	memset(&host_law, 0, sizeof(host_law));
	memset(&weapon_law, 0, sizeof(weapon_law));
	memset(&semantics, 0, sizeof(semantics));
	memset(&entities, 0, sizeof(entities));
	memset(&visibility, 0, sizeof(visibility));
	memset(mechanism_authorities, 0, sizeof(mechanism_authorities));
	memset(mechanism_controllers, 0, sizeof(mechanism_controllers));
	memset(mechanism_topology_edges, 0, sizeof(mechanism_topology_edges));
	memset(mechanism_transitions, 0, sizeof(mechanism_transitions));
	memset(&configuration_limits, 0, sizeof(configuration_limits));
	memset(&semantics_limits, 0, sizeof(semantics_limits));
	memset(&visibility_limits, 0, sizeof(visibility_limits));
	memset(&cell, 0, sizeof(cell));
	memset(&facet, 0, sizeof(facet));
	memset(&incidence, 0, sizeof(incidence));
	memset(&cell_incidence, 0, sizeof(cell_incidence));
	memset(&vertex, 0, sizeof(vertex));
	memset(&portal, 0, sizeof(portal));
	memset(&source_surface, 0, sizeof(source_surface));
	memset(&source_surface_vertex, 0, sizeof(source_surface_vertex));
	memset(&static_view, 0, sizeof(static_view));
	memset(&compact_profile, 0, sizeof(compact_profile));
	memset(&resolved_profile, 0, sizeof(resolved_profile));
	mock.fail_stage = fail_stage;
	alternate_identity.bsp_checksum = 1U;
	identity.physics_abi_id = UINT64_C(11);
	identity.collision_law_id = UINT64_C(12);
	identity.pmove_law_id = UINT64_C(13);
	identity.gravity_law_id = UINT64_C(14);
	identity.hook_law_id = UINT64_C(15);
	identity.mechanism_law_id = UINT64_C(16);
	identity.weapon_law_id = UINT64_C(17);
	host_law.collision_law_id = identity.collision_law_id;
	host_law.pmove_law_id = identity.pmove_law_id;
	host_law.gravity_law_id = identity.gravity_law_id;
	host_law.hook_law_id = identity.hook_law_id;
	host_law.mechanism_law_id = identity.mechanism_law_id;
	response_fragment.parent_cell.value = 101U;
	response_fragment.boundary_incidences.first = 31U;
	response_fragment.boundary_incidences.count = 32U;
	response_fragment.valid_stances = 1U;
	response_patch.target_cell.value = 102U;
	response_patch.boundary_incidences.first = 41U;
	response_patch.boundary_incidences.count = 42U;
	response_patch.valid_stances = 2U;
	response_patch.vertex_count = 3U;
	response_pair.source_valid_stances = 1U;
	response_pair.target_valid_stances = 1U;
	mechanism_authorities[0].source.entity_ordinal = 101U;
	mechanism_authorities[6].source.entity_ordinal = 107U;
	mechanism_controllers[0].mechanism = 201U;
	mechanism_controllers[7].mechanism = 208U;
	mechanism_topology_edges[0].fanout_ordinal = 301U;
	mechanism_topology_edges[8].fanout_ordinal = 309U;
	relation_fact.source_fragment = 0U;
	relation_fact.target_patch = 0U;
	relation_fact.flags = (sg_rune_compact_static_relation_flags_t)3U;
	relation_fact.visibility = (sg_rune_compact_static_visibility_class_t)1U;
	relation_fact.visibility_reason =
		(sg_rune_compact_static_visibility_reason_t)2U;
	relation_fact.requires_exact_ray = 1U;
	relation_fact.requires_area_state = 1U;
	relation_fact.occluders.first = 51U;
	relation_fact.occluders.count = 52U;
	relation_source_group.member_count = 1U;
	relation_target_group.member_count = 1U;
	relation_candidate_group.requires_exact_ray = 1U;
	static_view.mechanism_count = 11U;
	static_view.mechanism_controller_count = 41U;
	static_view.mechanism_edge_count = 12U;
	static_view.transition_count = 42U;
	static_view.landmark_count = 13U;
	static_view.landmark_cell_count = 14U;
	static_view.facet_annotation_count = 15U;
	static_view.portal_mechanism_count = 16U;
	model.cell_count = 31U;
	model.facet_count = 32U;
	model.incidence_count = 33U;
	model.portal_count = 34U;
	model.movement_capability_count = 35U;
	model.movement_state_count = 40U;
	model.movement_fiber_count = 36U;
	model.movement_hook_target_count = 41U;
	model.movement_fiber_function_ref_count = 42U;
	model.weapon_kernel_count = 37U;
	model.weapon_attachment_count = 43U;
	model.weapon_relation_span_count = 45U;
	model.weapon_relation_ref_count = 44U;
	analytic.function_count = 39U;
	model.identity = identity;
	model.analytic = &analytic;
}

static const sg_rune_compact_identity_t *IdentityForStage(
	sg_rune_compact_generation_stage_t stage)
{
	return mock.mismatch_stage == stage ? &alternate_identity : &identity;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		memcmp(actual, expected, sizeof(*actual)) == 0;
}

int SG_RuneCompactBuilderBuild(const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_builder_error_t *error_out)
{
	Require(input != NULL && input->construction ==
		(const sg_host_law_construction_t *)&construction_sentinel &&
		input->configuration_limits == &configuration_limits &&
		input->semantics_limits == &semantics_limits &&
		input->visibility_limits == &visibility_limits);
	if (builder_out != NULL)
		*builder_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	*builder_out = &builder;
	return 1;
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *value,
	sg_rune_compact_builder_view_t *view_out)
{
	if (value == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = identity;
	view_out->weapon_profiles = &compact_profile;
	view_out->resolved_weapon_profiles = &resolved_profile;
	view_out->weapon_profile_count = 1U;
	return 1;
}

int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *value,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	if (value == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER);
	view_out->host_law = &host_law;
	view_out->weapon_law = &weapon_law;
	view_out->semantics = &semantics;
	view_out->entity_semantics = &entities;
	view_out->visibility = &visibility;
	return 1;
}

void SG_RuneCompactBuilderDestroy(sg_rune_compact_builder_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER);
}

int SG_RuneCompactGeometryMaterialize(const sg_rune_compact_builder_t *value,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out)
{
	(void)value;
	(void)allocator;
	if (geometry_out != NULL)
		*geometry_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	*geometry_out = &geometry;
	return 1;
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *value,
	sg_rune_compact_geometry_view_t *view_out)
{
	if (value == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY);
	view_out->cells = &cell;
	view_out->cell_count = 1U;
	view_out->facets = &facet;
	view_out->facet_count = 1U;
	view_out->incidences = &incidence;
	view_out->incidence_count = 1U;
	view_out->cell_incidences = &cell_incidence;
	view_out->cell_incidence_count = 1U;
	view_out->vertices = &vertex;
	view_out->vertex_count = 1U;
	view_out->portals = &portal;
	view_out->portal_count = 1U;
	view_out->source_surfaces = &source_surface;
	view_out->source_surface_count = 1U;
	view_out->source_surface_vertices = &source_surface_vertex;
	view_out->source_surface_vertex_count = 1U;
	return 1;
}

void SG_RuneCompactGeometryDestroy(sg_rune_compact_geometry_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY);
}

int SG_RuneCompactResponsePartitionBuild(
	const sg_rune_compact_builder_t *builder_owner,
	const sg_rune_compact_geometry_t *geometry_owner,
	const sg_rune_compact_response_allocator_t *allocator,
	sg_rune_compact_response_partition_t **partition_out,
	sg_rune_compact_response_error_t *error_out)
{
	Require(builder_owner == &builder && geometry_owner == &geometry &&
		allocator == NULL);
	if (partition_out != NULL)
		*partition_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	mock.response_build_calls++;
	*partition_out = &response_partition;
	return 1;
}

int SG_RuneCompactResponsePartitionRead(
	const sg_rune_compact_response_partition_t *partition,
	sg_rune_compact_response_partition_view_t *view_out)
{
	if (partition != &response_partition || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);
	view_out->source_fragments = &response_fragment;
	view_out->source_fragment_count = 1U;
	view_out->source_halfspaces = &response_halfspace;
	view_out->source_halfspace_count = 2U;
	view_out->target_patches = &response_patch;
	view_out->target_patch_count = 3U;
	view_out->target_vertices = &response_vertex;
	view_out->target_vertex_count = 4U;
	view_out->splits = &response_split;
	view_out->split_count = 5U;
	view_out->response_pairs = &response_pair;
	view_out->response_pair_count = 6U;
	view_out->candidate_groups = &relation_candidate_group;
	view_out->candidate_group_count = 1U;
	view_out->source_endpoint_groups = &relation_source_group;
	view_out->source_endpoint_group_count = 1U;
	view_out->source_endpoint_members = &relation_source_member;
	view_out->source_endpoint_member_count = 1U;
	view_out->target_endpoint_groups = &relation_target_group;
	view_out->target_endpoint_group_count = 1U;
	view_out->target_endpoint_members = &relation_target_member;
	view_out->target_endpoint_member_count = 1U;
	view_out->static_occluder_count = 1U;
	view_out->seal.version = SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION;
	view_out->seal.flags = SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED;
	view_out->seal.source_fragment_count = 1U;
	view_out->seal.target_patch_count = 3U;
	view_out->seal.split_count = 5U;
	view_out->seal.response_pair_count = 6U;
	return 1;
}

int SG_RuneCompactResponsePartitionSealValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	return mock.invalid_response_seal == 0 && view != NULL &&
		view->source_fragments == &response_fragment &&
		view->source_fragment_count == 1U &&
		view->source_halfspaces == &response_halfspace &&
		view->source_halfspace_count == 2U &&
		view->target_patches == &response_patch &&
		view->target_patch_count == 3U &&
		view->target_vertices == &response_vertex &&
		view->target_vertex_count == 4U && view->splits == &response_split &&
		view->split_count == 5U && view->response_pairs == &response_pair &&
		view->response_pair_count == 6U &&
		view->candidate_groups == &relation_candidate_group &&
		view->candidate_group_count == 1U &&
		view->source_endpoint_groups == &relation_source_group &&
		view->source_endpoint_group_count == 1U &&
		view->target_endpoint_groups == &relation_target_group &&
		view->target_endpoint_group_count == 1U &&
		view->seal.flags == SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED;
}

void SG_RuneCompactResponsePartitionDestroy(
	sg_rune_compact_response_partition_t *partition)
{
	if (partition != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);
}

int SG_RuneCompactMechanismsMaterialize(
	const sg_rune_compact_builder_t *builder_owner,
	const sg_rune_compact_geometry_t *geometry_owner,
	sg_rune_compact_mechanisms_t **mechanisms_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	Require(builder_owner == &builder && geometry_owner == &geometry);
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (mechanisms_out == NULL)
		return 0;
	*mechanisms_out = &mechanisms;
	return 1;
}

int SG_RuneCompactMechanismsRead(const sg_rune_compact_mechanisms_t *value,
	sg_rune_compact_mechanisms_view_t *view_out)
{
	if (value == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS);
	view_out->mechanisms = mechanism_authorities;
	view_out->mechanism_count = 7U;
	view_out->controllers = mechanism_controllers;
	view_out->controller_count = 8U;
	view_out->topology_edges = mechanism_topology_edges;
	view_out->topology_edge_count = 9U;
	view_out->transitions = mechanism_transitions;
	view_out->transition_count = 10U;
	return 1;
}

void SG_RuneCompactMechanismsDestroy(sg_rune_compact_mechanisms_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS);
}

int SG_RuneCompactStaticMaterializerBuild(
	const sg_rune_compact_static_materializer_input_t *input,
	sg_rune_compact_static_materializer_t **materializer_out,
	sg_rune_compact_static_materializer_error_t *error_out)
{
	Require(input != NULL && IdentityEqual(&input->geometry.identity, &identity) &&
		input->geometry.cells == &cell && input->geometry.cell_count == 1U &&
		input->geometry.facets == &facet && input->geometry.facet_count == 1U &&
		input->geometry.incidences == &incidence &&
		input->geometry.incidence_count == 1U &&
		input->geometry.cell_incidences == &cell_incidence &&
		input->geometry.cell_incidence_count == 1U &&
		input->geometry.vertices == &vertex && input->geometry.vertex_count == 1U &&
		input->geometry.portals == &portal && input->geometry.portal_count == 1U &&
		input->geometry.source_surfaces == &source_surface &&
		input->geometry.source_surface_count == 1U &&
		input->geometry.source_surface_vertices == &source_surface_vertex &&
		input->geometry.source_surface_vertex_count == 1U &&
		input->entities == &entities && input->configuration == &semantics &&
		input->visibility == &visibility && input->mechanisms == &mechanisms);
	if (materializer_out != NULL)
		*materializer_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_STATIC)) {
		if (error_out != NULL)
			error_out->code =
			SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	*materializer_out = &static_materializer;
	return 1;
}

int SG_RuneCompactStaticMaterializerReadBound(
	const sg_rune_compact_static_materializer_t *value,
	sg_rune_compact_identity_t *identity_out, sg_rune_compact_static_t *static_out)
{
	if (value == NULL || identity_out == NULL || static_out == NULL)
		return 0;
	*identity_out = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_STATIC);
	*static_out = static_view;
	return 1;
}

void SG_RuneCompactStaticMaterializerDestroy(
	sg_rune_compact_static_materializer_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_STATIC);
}

int SG_RuneCompactMovementFieldsBuild(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_t **fields_out,
	sg_rune_compact_movement_fields_error_t *error_out)
{
	Require(input != NULL && input->builder == &builder &&
		input->host_owner ==
			(const sg_host_law_construction_t *)&construction_sentinel &&
		input->geometry_owner == &geometry &&
		input->response_owner == &response_partition &&
		input->mechanisms_owner == &mechanisms &&
		input->static_owner == &static_materializer &&
		input->collision_scene == NULL);
	if (input != NULL)
		mock.movement_response_owner = input->response_owner;
	if (fields_out != NULL)
		*fields_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	*fields_out = &movement_fields;
	return 1;
}

int SG_RuneCompactMovementFieldsReadBound(
	const sg_rune_compact_movement_fields_t *value,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_movement_fields_view_t *view_out)
{
	if (value == NULL || identity_out == NULL || view_out == NULL)
		return 0;
	*identity_out = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT);
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT);
	view_out->capability_count = 17U;
	view_out->state_count = 18U;
	view_out->fiber_count = 19U;
	view_out->hook_target_count = 20U;
	view_out->fiber_function_ref_count = 21U;
	view_out->analytic.function_count = 22U;
	return 1;
}

void SG_RuneCompactMovementFieldsDestroy(sg_rune_compact_movement_fields_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT);
}

int SG_RuneCompactWeaponRelationsBuild(const sg_rune_compact_builder_t *owner,
	const sg_rune_compact_geometry_t *geometry_owner,
	sg_rune_compact_response_partition_t *response_owner,
	sg_rune_compact_weapon_relations_t **relations_out,
	sg_rune_compact_weapon_relations_error_t *error_out)
{
	Require(owner == &builder && geometry_owner == &geometry &&
		response_owner == &response_partition);
	mock.relation_response_owner = response_owner;
	if (relations_out != NULL)
		*relations_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_RELATION)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	*relations_out = &relations;
	return 1;
}

int SG_RuneCompactWeaponRelationsRead(
	const sg_rune_compact_weapon_relations_t *value,
	sg_rune_compact_weapon_relations_view_t *view_out)
{
	if (value == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_RELATION);
	view_out->response.source_fragments = &response_fragment;
	view_out->response.source_fragment_count = 1U;
	view_out->response.source_halfspaces = &response_halfspace;
	view_out->response.source_halfspace_count = 2U;
	view_out->response.target_patches = &response_patch;
	view_out->response.target_patch_count = 3U;
	view_out->response.target_vertices = &response_vertex;
	view_out->response.target_vertex_count = 4U;
	view_out->response.splits = &response_split;
	view_out->response.split_count = 5U;
	view_out->response.facts = &relation_fact;
	view_out->response.fact_count = 1U;
	view_out->response.candidate_groups = &relation_candidate_group;
	view_out->response.candidate_group_count = 1U;
	view_out->response.source_endpoint_groups = &relation_source_group;
	view_out->response.source_endpoint_group_count = 1U;
	view_out->response.source_endpoint_members = &relation_source_member;
	view_out->response.source_endpoint_member_count = 1U;
	view_out->response.target_endpoint_groups = &relation_target_group;
	view_out->response.target_endpoint_group_count = 1U;
	view_out->response.target_endpoint_members = &relation_target_member;
	view_out->response.target_endpoint_member_count = 1U;
	view_out->response.occluders = &relation_occluder;
	view_out->response.occluder_count = 1U;
	view_out->response.exact_live_prefire_trace_required = 1U;
	return 1;
}

void SG_RuneCompactWeaponRelationsDestroy(
	sg_rune_compact_weapon_relations_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_RELATION);
}

sg_rune_compact_weapon_field_status_t SG_RuneCompactWeaponFieldBuild(
	const sg_rune_compact_weapon_field_input_t *input,
	sg_rune_compact_weapon_field_t **field_out,
	sg_rune_compact_weapon_field_error_t *error_out)
{
	Require(input != NULL && input->identity != NULL &&
		IdentityEqual(input->identity, &identity) &&
		input->compact_profiles == &compact_profile &&
		input->resolved_profiles == &resolved_profile && input->profile_count == 1U &&
		input->weapon_law == &weapon_law &&
		input->physics_abi_id == identity.physics_abi_id &&
		input->weapon_law_id == identity.weapon_law_id &&
		input->relations_owner == &relations);
	if (field_out != NULL)
		*field_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON)) {
		if (error_out != NULL)
			error_out->status = SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT;
		return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT;
	}
	*field_out = &weapon_field;
	return SG_RUNE_COMPACT_WEAPON_FIELD_OK;
}

int SG_RuneCompactWeaponFieldReadBound(const sg_rune_compact_weapon_field_t *value,
	sg_rune_compact_weapon_field_view_t *view_out)
{
	if (value == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *IdentityForStage(
		SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON);
	view_out->kernel_count = 23U;
	view_out->attachment_count = 24U;
	view_out->relation_span_count = 28U;
	view_out->relation_ref_count = 25U;
	view_out->weapon_function_ref_count = 26U;
	view_out->analytic.function_count = 27U;
	return 1;
}

void SG_RuneCompactWeaponFieldDestroy(sg_rune_compact_weapon_field_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON);
}

int SG_RuneCompactComposerBuild(const sg_rune_compact_builder_t *builder_owner,
	const sg_rune_compact_geometry_t *geometry_owner,
	const sg_rune_compact_mechanisms_t *mechanisms_owner,
	const sg_rune_compact_static_materializer_t *static_owner,
	const sg_rune_compact_movement_fields_t *movement_owner,
	const sg_rune_compact_weapon_relations_t *relations_owner,
	const sg_rune_compact_weapon_field_t *weapon_owner,
	sg_rune_compact_composer_t **composer_out,
	sg_rune_compact_composer_error_t *error_out)
{
	Require(builder_owner == &builder && geometry_owner == &geometry &&
		mechanisms_owner == &mechanisms &&
		static_owner == &static_materializer && movement_owner == &movement_fields &&
		relations_owner == &relations &&
		weapon_owner == &weapon_field);
	if (composer_out != NULL)
		*composer_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	*composer_out = &composer;
	return 1;
}

const sg_rune_compact_model_t *SG_RuneCompactComposerModel(
	const sg_rune_compact_composer_t *value)
{
	if (value == NULL)
		return NULL;
	model.identity = *IdentityForStage(SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER);
	return &model;
}

void SG_RuneCompactComposerDestroy(sg_rune_compact_composer_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER);
}

int SG_RuneCompactArtifactEncode(const sg_rune_compact_model_t *source,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_compact_wire_error_t *error_out)
{
	Require(source == &model);
	if (image_out != NULL)
		*image_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_ENCODE)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL;
		return 0;
	}
	if (source == NULL || image_out == NULL || image_size_out == NULL)
		return 0;
	*image_out = malloc(1U);
	if (*image_out == NULL)
		return 0;
	*image_size_out = 1U;
	return 1;
}

int SG_RuneCompactWireDecode(const void *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_wire_decoded_t **decoded_out,
	sg_rune_compact_wire_error_t *error_out)
{
	Require(image != NULL && image_size == 1U && expected_identity != NULL &&
		IdentityEqual(expected_identity, &identity));
	if (decoded_out != NULL)
		*decoded_out = NULL;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE)) {
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT;
		return 0;
	}
	*decoded_out = &decoded;
	return 1;
}

void SG_RuneCompactWireDestroy(sg_rune_compact_wire_decoded_t *value)
{
	if (value != NULL)
		Destroyed(SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE);
}

sg_rune_compact_artifact_publication_result_t SG_RuneCompactArtifactPublish(
	const char *destination, const unsigned char *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_artifact_fs_ops_t *ops)
{
	sg_rune_compact_artifact_publication_result_t result;

	Require(destination != NULL && strcmp(destination, "mock.rune") == 0 &&
		image != NULL && image_size == 1U && expected_identity != NULL &&
		IdentityEqual(expected_identity, &identity));
	(void)ops;
	memset(&result, 0, sizeof(result));
	mock.publication_calls++;
	if (!Enter(SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION)) {
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_RENAME_FAILED;
		return result;
	}
	result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_DIRECTORY_SYNC_FAILED;
	result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DIRECTORY_SYNC;
	result.published = 1;
	return result;
}

static void Progress(void *context, sg_rune_compact_generation_stage_t stage,
	const sg_rune_compact_generation_counts_t *accepted)
{
	generation_mock_t *trace = context;

	if (accepted != NULL)
		trace->progressed[trace->progressed_count++] = stage;
}

static sg_rune_compact_generation_input_t Input(void)
{
	sg_rune_compact_generation_input_t input;

	memset(&input, 0, sizeof(input));
	input.builder_input.construction =
		(const sg_host_law_construction_t *)&construction_sentinel;
	input.builder_input.configuration_limits = &configuration_limits;
	input.builder_input.semantics_limits = &semantics_limits;
	input.builder_input.visibility_limits = &visibility_limits;
	input.destination = "mock.rune";
	input.progress = Progress;
	input.progress_context = &mock;
	return input;
}

static sg_rune_compact_generation_error_code_t FailureCode(
	sg_rune_compact_generation_stage_t stage)
{
	switch (stage) {
	case SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER:
		return SG_RUNE_COMPACT_GENERATION_ERROR_BUILDER_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY:
		return SG_RUNE_COMPACT_GENERATION_ERROR_GEOMETRY_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE:
		return SG_RUNE_COMPACT_GENERATION_ERROR_RESPONSE_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS:
		return SG_RUNE_COMPACT_GENERATION_ERROR_MECHANISMS_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_STATIC:
		return SG_RUNE_COMPACT_GENERATION_ERROR_STATIC_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT:
		return SG_RUNE_COMPACT_GENERATION_ERROR_MOVEMENT_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_RELATION:
		return SG_RUNE_COMPACT_GENERATION_ERROR_RELATION_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON:
		return SG_RUNE_COMPACT_GENERATION_ERROR_WEAPON_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER:
		return SG_RUNE_COMPACT_GENERATION_ERROR_COMPOSER_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_ENCODE:
		return SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_ENCODE_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE:
		return SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_DECODE_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION:
		return SG_RUNE_COMPACT_GENERATION_ERROR_PUBLICATION_REJECTED;
	case SG_RUNE_COMPACT_GENERATION_STAGE_NONE:
	case SG_RUNE_COMPACT_GENERATION_STAGE_COUNT:
		break;
	}
	return SG_RUNE_COMPACT_GENERATION_ERROR_INVALID_ARGUMENT;
}

static uint32_t ExpectedDestroyCount(sg_rune_compact_generation_stage_t stop)
{
	static const sg_rune_compact_generation_stage_t stages[] = {
		SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE,
		SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER,
		SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON,
		SG_RUNE_COMPACT_GENERATION_STAGE_RELATION,
		SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT,
		SG_RUNE_COMPACT_GENERATION_STAGE_STATIC,
		SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS,
		SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE,
		SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY,
		SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER
	};
	uint32_t count = 0U;
	uint32_t index;

	for (index = 0U; index < (uint32_t)(sizeof(stages) / sizeof(stages[0]));
		index++)
		if (stages[index] < stop)
			count++;
	return count;
}

static int CheckDestroyed(sg_rune_compact_generation_stage_t stop)
{
	static const sg_rune_compact_generation_stage_t stages[] = {
		SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE,
		SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER,
		SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON,
		SG_RUNE_COMPACT_GENERATION_STAGE_RELATION,
		SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT,
		SG_RUNE_COMPACT_GENERATION_STAGE_STATIC,
		SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS,
		SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE,
		SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY,
		SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER
	};
	uint32_t expected = 0U;
	uint32_t index;

	CHECK(mock.destroyed_count == ExpectedDestroyCount(stop));
	for (index = 0U; index < (uint32_t)(sizeof(stages) / sizeof(stages[0]));
		index++)
		if (stages[index] < stop)
			CHECK(mock.destroyed[expected++] == stages[index]);
	return 1;
}

static int TestSuccessfulPublication(void)
{
	sg_rune_compact_generation_input_t input;
	sg_rune_compact_generation_result_t result;
	sg_rune_compact_generation_counts_t expected;
	uint32_t index;

	ResetMock(SG_RUNE_COMPACT_GENERATION_STAGE_COUNT);
	memset(&expected, 0, sizeof(expected));
	expected.geometry_cells = 1U;
	expected.geometry_facets = 1U;
	expected.geometry_incidences = 1U;
	expected.geometry_cell_incidences = 1U;
	expected.geometry_vertices = 1U;
	expected.geometry_portals = 1U;
	expected.response_fragments = 1U;
	expected.response_halfspaces = 2U;
	expected.response_patches = 3U;
	expected.response_vertices = 4U;
	expected.response_splits = 5U;
	expected.response_pairs = 6U;
	expected.response_candidate_groups = 1U;
	expected.response_source_endpoint_groups = 1U;
	expected.response_target_endpoint_groups = 1U;
	expected.mechanism_authorities = 7U;
	expected.mechanism_controllers = 8U;
	expected.mechanism_topology_edges = 9U;
	expected.mechanism_transitions = 10U;
	expected.static_mechanisms = 11U;
	expected.static_mechanism_controllers = 41U;
	expected.static_mechanism_edges = 12U;
	expected.static_transitions = 42U;
	expected.static_landmarks = 13U;
	expected.static_landmark_cells = 14U;
	expected.static_facet_annotations = 15U;
	expected.static_portal_mechanisms = 16U;
	expected.movement_capabilities = 17U;
	expected.movement_states = 18U;
	expected.movement_fibers = 19U;
	expected.movement_hook_targets = 20U;
	expected.movement_fiber_function_refs = 21U;
	expected.movement_analytic_functions = 22U;
	expected.relations = 1U;
	expected.relation_candidate_groups = 1U;
	expected.relation_occluders = 1U;
	expected.weapon_kernels = 23U;
	expected.weapon_attachments = 24U;
	expected.weapon_relation_spans = 28U;
	expected.weapon_relation_refs = 25U;
	expected.weapon_function_refs = 26U;
	expected.weapon_analytic_functions = 27U;
	expected.composer_cells = 31U;
	expected.composer_facets = 32U;
	expected.composer_incidences = 33U;
	expected.composer_portals = 34U;
	expected.composer_movement_capabilities = 35U;
	expected.composer_movement_states = 40U;
	expected.composer_movement_fibers = 36U;
	expected.composer_movement_hook_targets = 41U;
	expected.composer_movement_fiber_function_refs = 42U;
	expected.composer_weapon_kernels = 37U;
	expected.composer_weapon_attachments = 43U;
	expected.composer_weapon_relation_spans = 45U;
	expected.composer_weapon_relation_refs = 44U;
	expected.composer_analytic_functions = 39U;
	expected.encoded_bytes = 1U;
	input = Input();
	CHECK(SG_RuneCompactGenerationRun(&input, &result));
	CHECK(mock.input_error == 0);
	CHECK(result.error == SG_RUNE_COMPACT_GENERATION_ERROR_NONE);
	CHECK(result.stage == SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION);
	CHECK(result.published == 1);
	CHECK(result.durable == 0);
	CHECK(result.publication.diagnostic ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_DIRECTORY_SYNC_FAILED);
	CHECK(memcmp(&result.accepted, &expected, sizeof(expected)) == 0);
	CHECK(mock.entered_count == SG_RUNE_COMPACT_GENERATION_STAGE_COUNT - 1U);
	CHECK(mock.progressed_count == mock.entered_count);
	CHECK(mock.publication_calls == 1U);
	CHECK(mock.response_build_calls == 1U);
	CHECK(mock.movement_response_owner == &response_partition);
	CHECK(mock.relation_response_owner == &response_partition);
	for (index = 0U; index < mock.entered_count; index++) {
		CHECK(mock.entered[index] ==
			(sg_rune_compact_generation_stage_t)(index + 1U));
		CHECK(mock.progressed[index] == mock.entered[index]);
	}
	CHECK(CheckDestroyed(SG_RUNE_COMPACT_GENERATION_STAGE_COUNT));
	return 1;
}

static int TestFailureUnwinds(void)
{
	sg_rune_compact_generation_stage_t stage;

	for (stage = SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER;
		stage < SG_RUNE_COMPACT_GENERATION_STAGE_COUNT; stage++) {
		sg_rune_compact_generation_input_t input;
		sg_rune_compact_generation_result_t result;
		uint32_t index;

		ResetMock(stage);
		input = Input();
		CHECK(!SG_RuneCompactGenerationRun(&input, &result));
		CHECK(result.error == FailureCode(stage));
		CHECK(result.stage == stage);
		CHECK(result.published == 0);
		CHECK(result.durable == 0);
		CHECK(mock.entered_count == (uint32_t)stage);
		CHECK(mock.progressed_count == (uint32_t)stage - 1U);
		CHECK(mock.publication_calls ==
			(stage == SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION ? 1U : 0U));
		for (index = 0U; index < mock.progressed_count; index++)
			CHECK(mock.progressed[index] ==
				(sg_rune_compact_generation_stage_t)(index + 1U));
		CHECK(CheckDestroyed(stage));
	}
	return 1;
}

static int TestIdentityRejection(void)
{
	sg_rune_compact_generation_stage_t stage;

	for (stage = SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER;
		stage <= SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER; stage++) {
		sg_rune_compact_generation_input_t input;
		sg_rune_compact_generation_result_t result;

		ResetMock(SG_RUNE_COMPACT_GENERATION_STAGE_COUNT);
		mock.mismatch_stage = stage;
		input = Input();
		CHECK(!SG_RuneCompactGenerationRun(&input, &result));
		CHECK(result.error == SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH);
		CHECK(result.stage == stage);
		CHECK(result.published == 0);
		CHECK(mock.entered_count == (uint32_t)stage);
		CHECK(mock.progressed_count == (uint32_t)stage - 1U);
		CHECK(mock.publication_calls == 0U);
		CHECK(CheckDestroyed(
			(sg_rune_compact_generation_stage_t)(stage + 1U)));
	}
	return 1;
}

static int TestResponseSealRejection(void)
{
	sg_rune_compact_generation_input_t input;
	sg_rune_compact_generation_result_t result;

	ResetMock(SG_RUNE_COMPACT_GENERATION_STAGE_COUNT);
	mock.invalid_response_seal = 1;
	input = Input();
	CHECK(!SG_RuneCompactGenerationRun(&input, &result));
	CHECK(result.error == SG_RUNE_COMPACT_GENERATION_ERROR_RESPONSE_REJECTED);
	CHECK(result.stage == SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);
	CHECK(result.published == 0);
	CHECK(result.durable == 0);
	CHECK(mock.entered_count == SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);
	CHECK(mock.progressed_count ==
		SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE - 1U);
	CHECK(mock.publication_calls == 0U);
	CHECK(CheckDestroyed(SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS));
	return 1;
}

static int TestInvalidInput(void)
{
	sg_rune_compact_generation_input_t input;
	sg_rune_compact_generation_result_t result;

	ResetMock(SG_RUNE_COMPACT_GENERATION_STAGE_COUNT);
	input = Input();
	input.destination = NULL;
	CHECK(!SG_RuneCompactGenerationRun(&input, &result));
	CHECK(result.error == SG_RUNE_COMPACT_GENERATION_ERROR_INVALID_ARGUMENT);
	CHECK(result.stage == SG_RUNE_COMPACT_GENERATION_STAGE_NONE);
	CHECK(mock.entered_count == 0U);
	CHECK(mock.destroyed_count == 0U);
	return 1;
}

int main(void)
{
	if (!TestSuccessfulPublication() || !TestFailureUnwinds() ||
		!TestIdentityRejection() || !TestResponseSealRejection() ||
		!TestInvalidInput())
		return 1;
	puts("sg_rune_compact_generation_test: ok");
	return 0;
}
