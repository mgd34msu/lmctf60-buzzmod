#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_dynamics_model.h"

typedef sg_field_status_t (*field_create_fn)(
	const sg_rune_runtime_snapshot_t *, const sg_rune_dynamics_model_t *,
	sg_field_service_t **);
typedef sg_field_status_t (*field_resolve_fn)(sg_field_service_t *,
	const sg_destination_terminal_t *, const sg_field_environment_t *, uint64_t,
	sg_field_handle_t *);
typedef sg_field_status_t (*field_refresh_fn)(sg_field_service_t *,
	const sg_field_handle_t *, const sg_destination_terminal_t *,
	const sg_field_environment_t *, uint64_t, sg_field_handle_t *);
typedef sg_field_status_t (*field_query_fn)(const sg_field_service_t *,
	const sg_field_handle_t *, const sg_localized_field_state_t *,
	const sg_field_environment_t *, sg_field_option_t *, size_t,
	sg_field_guidance_t *);
typedef sg_field_status_t (*field_release_fn)(sg_field_service_t *,
	const sg_field_handle_t *);

_Static_assert(_Generic(&SG_FieldServiceCreate, field_create_fn: 1,
	default: 0), "field create must consume the aggregate dynamics model");
_Static_assert(_Generic(&SG_FieldServiceResolve, field_resolve_fn: 1,
	default: 0), "field resolve signature changed");
_Static_assert(_Generic(&SG_FieldServiceRefresh, field_refresh_fn: 1,
	default: 0), "field refresh signature changed");
_Static_assert(_Generic(&SG_FieldServiceQuery, field_query_fn: 1,
	default: 0), "field query signature changed");
_Static_assert(_Generic(&SG_FieldServiceRelease, field_release_fn: 1,
	default: 0), "field release signature changed");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_rune_stable_id_t Stable(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_stable_id_t){
		.source_set_identity = 1U,
		.high = (uint64_t)domain << 32,
		.low = (uint64_t)ordinal << 32
	};
}

static sg_rune_interval_t Interval(float minimum, float maximum)
{
	return (sg_rune_interval_t){ minimum, maximum };
}

static sg_rune_interval3_t Interval3(float minimum, float maximum)
{
	return (sg_rune_interval3_t){
		Interval(minimum, maximum),
		Interval(minimum, maximum),
		Interval(minimum, maximum)
	};
}

static sg_rune_state_mode_t SupportedMode(void)
{
	sg_rune_state_mode_t mode = { 0 };

	mode.kind = SG_RUNE_STATE_MODE_SUPPORTED;
	mode.value.supported.support_surface.value =
		Stable(SG_RUNE_ORDER_SURFACE, 1U);
	return mode;
}

typedef struct dynamics_fixture_s
{
	sg_rune_cell_t cell;
	sg_rune_surface_t surface;
	sg_rune_phase_basis_t phase;
	sg_rune_model_t rune_model;
	sg_phase_coordinate_t phase_coordinate;
	sg_rune_runtime_snapshot_t snapshot;
	sg_rune_state_vertex_t vertices[8];
	sg_rune_state_chart_t charts[1];
	sg_rune_state_simplex_t simplices[1];
	sg_rune_state_domain_t domains[1];
	sg_rune_control_fiber_t fibers[1];
	sg_rune_control_domain_t control_domains[1];
	sg_rune_response_patch_t patches[1];
	sg_rune_boundary_transfer_t transfers[1];
	sg_field_reach_atom_t reach_atoms[2];
	sg_field_reach_atom_ref_t outcome_destination_atoms[3];
	sg_field_guard_requirement_t guard_requirements[2];
	sg_field_guard_effect_t guard_effects[1];
	sg_field_outcome_t outcomes[3];
	sg_field_choice_t choices[2];
	sg_field_local_progress_kernel_t local_progress_kernels[1];
	sg_field_refinement_node_t refinement_nodes[2];
	uint32_t refinement_roots[2];
	sg_field_refinement_node_ref_t local_progress_sources[1];
	sg_field_choice_ref_t local_progress_choices[1];
	sg_field_progress_target_t local_progress_targets[2];
	sg_rune_field_region_t regions[1];
	uint32_t chart_leaf_regions[1];
	uint32_t domain_leaf_regions[1];
	uint32_t patch_leaf_regions[1];
	sg_rune_dynamics_model_t dynamics;
} dynamics_fixture_t;

static void BuildFixture(dynamics_fixture_t *fixture)
{
	size_t index;
	sg_rune_state_chart_t *chart;
	sg_rune_response_patch_t *patch;
	sg_rune_boundary_transfer_t *transfer;

	memset(fixture, 0, sizeof(*fixture));
	fixture->rune_model.version = SG_RUNE_MODEL_VERSION;
	fixture->rune_model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->rune_model.flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->rune_model.identity.source_set_identity = 1U;
	fixture->rune_model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->rune_model.cells = &fixture->cell;
	fixture->rune_model.cell_count = 1U;
	fixture->cell.id.value = Stable(SG_RUNE_ORDER_CELL, 1U);
	fixture->surface.id.value = Stable(SG_RUNE_ORDER_SURFACE, 1U);
	fixture->rune_model.surfaces = &fixture->surface;
	fixture->rune_model.surface_count = 1U;
	fixture->rune_model.phases = &fixture->phase;
	fixture->rune_model.phase_count = 1U;
	fixture->snapshot.identity = 2U;
	fixture->snapshot.topology_revision = 3U;
	fixture->snapshot.cell_count = 1U;
	fixture->snapshot.phase_count = 1U;
	fixture->snapshot.region_count = 1U;
	fixture->snapshot.model = &fixture->rune_model;
	fixture->snapshot.phases = &fixture->phase_coordinate;

	for (index = 0U; index < 8U; index++)
	{
		fixture->vertices[index].id.value =
			Stable(SG_RUNE_ORDER_STATE_VERTEX, (uint32_t)index + 1U);
		fixture->vertices[index].chart.value =
			Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	}
	fixture->vertices[1].position.value[0] = 1.0f;
	fixture->vertices[2].position.value[1] = 1.0f;
	fixture->vertices[3].position.value[2] = 1.0f;
	fixture->vertices[4].velocity.value[0] = 1.0f;
	fixture->vertices[5].velocity.value[1] = 1.0f;
	fixture->vertices[6].velocity.value[2] = 1.0f;
	fixture->vertices[7].elapsed_ms = 1.0f;
	chart = &fixture->charts[0];
	chart->id.value = Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	chart->configuration_cell.value = Stable(SG_RUNE_ORDER_CELL, 1U);
	chart->mode = SupportedMode();
	chart->embedding.position = Interval3(-64.0f, 64.0f);
	chart->embedding.velocity = Interval3(-320.0f, 320.0f);
	chart->embedding.elapsed_ms = Interval(0.0f, 100.0f);
	chart->embedding.dimension_count = SG_RUNE_STATE_DIMENSION_COUNT;
	chart->state_vertices = (sg_rune_state_vertex_span_t){ 0U, 8U };
	chart->simplices = (sg_rune_state_simplex_span_t){ 0U, 1U };
	chart->state_domains = (sg_rune_state_domain_span_t){ 0U, 1U };
	chart->control_fibers = (sg_rune_control_fiber_span_t){ 0U, 1U };
	chart->response_patches = (sg_rune_response_patch_span_t){ 0U, 1U };
	chart->boundary_transfers =
		(sg_rune_boundary_transfer_span_t){ 0U, 1U };
	chart->coverage_proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);

	fixture->simplices[0].id.value =
		Stable(SG_RUNE_ORDER_STATE_SIMPLEX, 1U);
	fixture->simplices[0].chart = chart->id;
	fixture->simplices[0].vertices =
		(sg_rune_state_vertex_span_t){ 0U, 8U };
	fixture->domains[0].id.value = Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	fixture->domains[0].chart = chart->id;
	fixture->domains[0].simplices =
		(sg_rune_state_simplex_span_t){ 0U, 1U };
	fixture->fibers[0].id.value = Stable(SG_RUNE_ORDER_CONTROL_FIBER, 1U);
	fixture->fibers[0].source_chart = chart->id;
	fixture->fibers[0].domain.value =
		Stable(SG_RUNE_ORDER_CONTROL_DOMAIN, 1U);
	fixture->fibers[0].condition.value =
		Stable(SG_RUNE_ORDER_GUARD_CONDITION, 1U);
	fixture->fibers[0].coverage_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 2U);
	fixture->control_domains[0].id.value =
		Stable(SG_RUNE_ORDER_CONTROL_DOMAIN, 1U);
	fixture->control_domains[0].source_chart = chart->id;
	fixture->control_domains[0].forward_move = Interval(-400.0f, 400.0f);
	fixture->control_domains[0].side_move = Interval(-400.0f, 400.0f);
	fixture->control_domains[0].up_move = Interval(-400.0f, 400.0f);
	fixture->control_domains[0].allowed_buttons = UINT32_MAX;
	fixture->control_domains[0].admissibility_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 7U);

	patch = &fixture->patches[0];
	patch->id.value = Stable(SG_RUNE_ORDER_RESPONSE_PATCH, 1U);
	patch->source_chart = chart->id;
	patch->source_simplex = fixture->simplices[0].id;
	patch->controls = (sg_rune_control_fiber_span_t){ 0U, 1U };
	patch->flow.position = Interval3(-1.0f, 1.0f);
	patch->flow.velocity = Interval3(-2.0f, 2.0f);
	patch->flow.elapsed_ms = Interval(1.0f, 2.0f);
	patch->running_cost = (sg_rune_cost_bounds_t){ 1000U, 2000U };
	patch->destination_domains =
		(sg_rune_state_domain_span_t){ 0U, 1U };
	patch->flow_proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 3U);

	transfer = &fixture->transfers[0];
	transfer->id.value = Stable(SG_RUNE_ORDER_BOUNDARY_TRANSFER, 1U);
	transfer->source_chart = chart->id;
	transfer->source_domain = fixture->domains[0].id;
	transfer->condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 2U);
	transfer->destination_chart = chart->id;
	transfer->destination_domain = fixture->domains[0].id;
	transfer->reset_enclosure = patch->flow;
	transfer->transfer_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 4U);

	for (index = 0U; index < 2U; index++)
	{
		fixture->reach_atoms[index].id.value =
			Stable(SG_RUNE_ORDER_FIELD_REACH_ATOM, (uint32_t)index + 1U);
		fixture->reach_atoms[index].domain = fixture->domains[0].id;
		fixture->reach_atoms[index].simplices =
			(sg_rune_state_simplex_span_t){ 0U, 1U };
		fixture->reach_atoms[index].state_bounds = patch->flow;
		fixture->reach_atoms[index].partition_proof.value =
			Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 20U);
		fixture->refinement_nodes[index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, (uint32_t)index + 1U);
		fixture->refinement_nodes[index].parent = UINT32_MAX;
		fixture->refinement_nodes[index].atom = fixture->reach_atoms[index].id;
		fixture->refinement_nodes[index].state_bounds = patch->flow;
		fixture->refinement_nodes[index].interpolation_error.position =
			Interval3(0.0f, 0.0f);
		fixture->refinement_nodes[index].interpolation_error.velocity =
			Interval3(0.0f, 0.0f);
		fixture->refinement_nodes[index].interpolation_error.elapsed_ms =
			Interval(0.0f, 0.0f);
		fixture->refinement_nodes[index].proof.value =
			Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 30U);
		fixture->refinement_roots[index] = (uint32_t)index;
	}
	fixture->reach_atoms[1].state_bounds.position = Interval3(-2.0f, 2.0f);
	fixture->reach_atoms[1].state_bounds.velocity = Interval3(-3.0f, 3.0f);
	fixture->reach_atoms[1].state_bounds.elapsed_ms = Interval(0.0f, 3.0f);
	fixture->refinement_nodes[1].state_bounds =
		fixture->reach_atoms[1].state_bounds;
	fixture->guard_effects[0].condition = transfer->condition;
	fixture->guard_effects[0].required_before = SG_FIELD_GUARD_FALSE;
	fixture->guard_effects[0].resulting_after = SG_FIELD_GUARD_TRUE;
	fixture->guard_effects[0].controllable = 1U;
	fixture->guard_requirements[0].condition = fixture->fibers[0].condition;
	fixture->guard_requirements[0].required = SG_FIELD_GUARD_TRUE;
	fixture->guard_requirements[1].condition = fixture->transfers[0].condition;
	fixture->guard_requirements[1].required = SG_FIELD_GUARD_FALSE;
	for (index = 0U; index < 3U; index++)
	{
		uint32_t row;
		fixture->outcomes[index].id.value =
			Stable(SG_RUNE_ORDER_FIELD_OUTCOME, (uint32_t)index + 1U);
		for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
			fixture->outcomes[index].endpoint.coefficient[row][row] = 1.0;
		fixture->outcomes[index].endpoint.exact_rank =
			SG_RUNE_STATE_DIMENSION_COUNT;
		fixture->outcomes[index].endpoint.operator_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 60U);
		fixture->outcomes[index].endpoint.image_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 70U);
		fixture->outcomes[index].endpoint.cover_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 80U);
		fixture->outcomes[index].remainder.position = Interval3(0.0f, 0.0f);
		fixture->outcomes[index].remainder.velocity = Interval3(0.0f, 0.0f);
		fixture->outcomes[index].remainder.elapsed_ms = Interval(0.0f, 0.0f);
		fixture->outcome_destination_atoms[index] =
			fixture->reach_atoms[1].id;
		fixture->outcomes[index].destination_cover =
			(sg_field_reach_atom_ref_span_t){ (uint32_t)index, 1U };
		fixture->outcomes[index].absolute_time_advance =
			(sg_rune_time_advance_t){ index == 2U ? 0U : 1U,
				index == 2U ? 0U : 1U };
		fixture->outcomes[index].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 40U);
	}
	fixture->outcomes[2].guard_effects =
		(sg_field_guard_effect_span_t){ 0U, 1U };
	fixture->choices[0].id.value = Stable(SG_RUNE_ORDER_FIELD_CHOICE, 1U);
	fixture->choices[0].kind = SG_FIELD_CHOICE_CONTROL;
	fixture->choices[0].authority.control = fixture->fibers[0].id;
	fixture->choices[0].guard_requirements =
		(sg_field_guard_requirement_span_t){ 0U, 1U };
	fixture->choices[0].source_atom = fixture->reach_atoms[0].id;
	fixture->choices[0].outcomes = (sg_field_outcome_span_t){ 0U, 2U };
	fixture->choices[0].cost = (sg_rune_cost_bounds_t){ 0U, 10U };
	fixture->choices[0].proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 50U);
	fixture->choices[1].id.value = Stable(SG_RUNE_ORDER_FIELD_CHOICE, 2U);
	fixture->choices[1].kind = SG_FIELD_CHOICE_TRANSFER;
	fixture->choices[1].authority.transfer = fixture->transfers[0].id;
	fixture->choices[1].guard_requirements =
		(sg_field_guard_requirement_span_t){ 1U, 1U };
	fixture->choices[1].source_atom = fixture->reach_atoms[0].id;
	fixture->choices[1].outcomes = (sg_field_outcome_span_t){ 2U, 1U };
	fixture->choices[1].cost = (sg_rune_cost_bounds_t){ 0U, 0U };
	fixture->choices[1].proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 51U);
	fixture->local_progress_kernels[0].id.value =
		Stable(SG_RUNE_ORDER_FIELD_LOCAL_PROGRESS, 1U);
	fixture->local_progress_kernels[0].source_atom = fixture->reach_atoms[0].id;
	fixture->local_progress_kernels[0].covered_sources =
		(sg_field_refinement_node_ref_span_t){ 0U, 1U };
	fixture->local_progress_kernels[0].target_atom = fixture->reach_atoms[1].id;
	fixture->local_progress_kernels[0].terminal_parameters.anchor_bounds =
		fixture->reach_atoms[1].state_bounds;
	fixture->local_progress_kernels[0].terminal_parameters.position_offset_bounds =
		Interval3(-0.5f, 0.5f);
	fixture->local_progress_kernels[0].terminal_parameters.velocity_bounds =
		Interval3(-3.0f, 3.0f);
	fixture->local_progress_kernels[0].terminal_parameters.local_elapsed_bounds =
		Interval(0.0f, 3.0f);
	fixture->local_progress_kernels[0].terminal_parameters.proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 54U);
	fixture->local_progress_kernels[0].admissible_choices =
		(sg_field_choice_ref_span_t){ 0U, 1U };
	fixture->local_progress_kernels[0].whole_outcome_targets =
		(sg_field_progress_target_span_t){ 0U, 2U };
	fixture->local_progress_kernels[0].finite_rank = 1U;
	fixture->local_progress_kernels[0].state_lyapunov[0] = 1.0f;
	fixture->local_progress_kernels[0].anchor_lyapunov[0] = -1.0f;
	fixture->local_progress_kernels[0].minimum_lyapunov_decrease = 0.125f;
	fixture->local_progress_kernels[0].proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 52U);
	fixture->local_progress_sources[0] = fixture->refinement_nodes[0].id;
	fixture->local_progress_choices[0] = fixture->choices[0].id;
	fixture->local_progress_targets[0].outcome = fixture->outcomes[0].id;
	fixture->local_progress_targets[0].atom = fixture->reach_atoms[1].id;
	fixture->local_progress_targets[1].outcome = fixture->outcomes[1].id;
	fixture->local_progress_targets[1].atom = fixture->reach_atoms[1].id;

	fixture->regions[0].id.value = Stable(SG_RUNE_ORDER_FIELD_REGION, 1U);
	fixture->regions[0].parent_region = SG_RUNE_FIELD_NO_REGION;
	fixture->regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	fixture->regions[0].state_domains =
		(sg_rune_state_domain_span_t){ 0U, 1U };
	fixture->regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 1U };
	fixture->regions[0].coverage_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 5U);

	fixture->dynamics.version = SG_RUNE_DYNAMICS_MODEL_VERSION;
	fixture->dynamics.id.value = Stable(SG_RUNE_ORDER_DYNAMICS_MODEL, 1U);
	fixture->dynamics.rune_identity = fixture->snapshot.identity;
	fixture->dynamics.topology_revision = fixture->snapshot.topology_revision;
	fixture->dynamics.state_vertices = fixture->vertices;
	fixture->dynamics.state_vertex_count = 8U;
	fixture->dynamics.state_charts = fixture->charts;
	fixture->dynamics.state_chart_count = 1U;
	fixture->dynamics.state_simplices = fixture->simplices;
	fixture->dynamics.state_simplex_count = 1U;
	fixture->dynamics.state_domains = fixture->domains;
	fixture->dynamics.state_domain_count = 1U;
	fixture->dynamics.control_fibers = fixture->fibers;
	fixture->dynamics.control_fiber_count = 1U;
	fixture->dynamics.control_domains = fixture->control_domains;
	fixture->dynamics.control_domain_count = 1U;
	fixture->dynamics.response_patches = fixture->patches;
	fixture->dynamics.response_patch_count = 1U;
	fixture->dynamics.boundary_transfers = fixture->transfers;
	fixture->dynamics.boundary_transfer_count = 1U;
	fixture->dynamics.reach_atoms = fixture->reach_atoms;
	fixture->dynamics.reach_atom_count = 2U;
	fixture->dynamics.outcome_destination_atoms =
		fixture->outcome_destination_atoms;
	fixture->dynamics.outcome_destination_atom_count = 3U;
	fixture->dynamics.guard_requirements = fixture->guard_requirements;
	fixture->dynamics.guard_requirement_count = 2U;
	fixture->dynamics.guard_effects = fixture->guard_effects;
	fixture->dynamics.guard_effect_count = 1U;
	fixture->dynamics.outcomes = fixture->outcomes;
	fixture->dynamics.outcome_count = 3U;
	fixture->dynamics.choices = fixture->choices;
	fixture->dynamics.choice_count = 2U;
	fixture->dynamics.local_progress_kernels = fixture->local_progress_kernels;
	fixture->dynamics.local_progress_kernel_count = 1U;
	fixture->dynamics.local_progress_sources = fixture->local_progress_sources;
	fixture->dynamics.local_progress_source_count = 1U;
	fixture->dynamics.local_progress_choices = fixture->local_progress_choices;
	fixture->dynamics.local_progress_choice_count = 1U;
	fixture->dynamics.local_progress_targets = fixture->local_progress_targets;
	fixture->dynamics.local_progress_target_count = 2U;
	fixture->dynamics.refinement_tree.id.value =
		Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_TREE, 1U);
	fixture->dynamics.refinement_tree.nodes = fixture->refinement_nodes;
	fixture->dynamics.refinement_tree.node_count = 2U;
	fixture->dynamics.refinement_tree.atom_roots = fixture->refinement_roots;
	fixture->dynamics.refinement_tree.atom_count = 2U;
	fixture->dynamics.refinement_tree.proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 53U);
	fixture->dynamics.hierarchy.id.value =
		Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	fixture->dynamics.hierarchy.regions = fixture->regions;
	fixture->dynamics.hierarchy.region_count = 1U;
	fixture->dynamics.hierarchy.chart_leaf_regions =
		fixture->chart_leaf_regions;
	fixture->dynamics.hierarchy.state_domain_leaf_regions =
		fixture->domain_leaf_regions;
	fixture->dynamics.hierarchy.response_patch_leaf_regions =
		fixture->patch_leaf_regions;
	fixture->dynamics.hierarchy.chart_count = 1U;
	fixture->dynamics.hierarchy.state_domain_count = 1U;
	fixture->dynamics.hierarchy.response_patch_count = 1U;
	fixture->dynamics.hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 6U);
	fixture->dynamics.error_contract.id.value =
		Stable(SG_RUNE_ORDER_FIELD_ERROR_CONTRACT, 1U);
	fixture->dynamics.error_contract.cost_quantum_us = 100U;
	fixture->dynamics.error_contract.maximum_value_width_us = 200U;
	fixture->dynamics.error_contract.maximum_bellman_residual_us = 50U;
	fixture->dynamics.error_contract.position_error = Interval3(-0.5f, 0.5f);
	fixture->dynamics.error_contract.velocity_error = Interval3(-1.0f, 1.0f);
	fixture->dynamics.error_contract.time_error = Interval(-0.5f, 0.5f);
}

static void TestTypedIdsAndModes(void)
{
	sg_rune_state_mode_t mode = SupportedMode();

#define CHECK_TYPED_ID(validator, type, domain) do { \
	type value = { Stable(domain, 1U) }; \
	CHECK(validator(&value)); \
	value.value = Stable(SG_RUNE_ORDER_CELL, 1U); \
	CHECK(!validator(&value)); \
} while (0)
	CHECK_TYPED_ID(SG_RuneDynamicsModelIdValid,
		sg_rune_dynamics_model_id_t, SG_RUNE_ORDER_DYNAMICS_MODEL);
	CHECK_TYPED_ID(SG_RuneStateVertexIdValid,
		sg_rune_state_vertex_id_t, SG_RUNE_ORDER_STATE_VERTEX);
	CHECK_TYPED_ID(SG_RuneStateChartIdValid,
		sg_rune_state_chart_id_t, SG_RUNE_ORDER_STATE_CHART);
	CHECK_TYPED_ID(SG_RuneStateSimplexIdValid,
		sg_rune_state_simplex_id_t, SG_RUNE_ORDER_STATE_SIMPLEX);
	CHECK_TYPED_ID(SG_RuneStateDomainIdValid,
		sg_rune_state_domain_id_t, SG_RUNE_ORDER_STATE_DOMAIN);
	CHECK_TYPED_ID(SG_RuneControlFiberIdValid,
		sg_rune_control_fiber_id_t, SG_RUNE_ORDER_CONTROL_FIBER);
	CHECK_TYPED_ID(SG_RuneControlDomainIdValid,
		sg_rune_control_domain_id_t, SG_RUNE_ORDER_CONTROL_DOMAIN);
	CHECK_TYPED_ID(SG_RuneResponsePatchIdValid,
		sg_rune_response_patch_id_t, SG_RUNE_ORDER_RESPONSE_PATCH);
	CHECK_TYPED_ID(SG_RuneBoundaryTransferIdValid,
		sg_rune_boundary_transfer_id_t, SG_RUNE_ORDER_BOUNDARY_TRANSFER);
	CHECK_TYPED_ID(SG_RuneControlDomainRefValid,
		sg_rune_control_domain_ref_t, SG_RUNE_ORDER_CONTROL_DOMAIN);
	CHECK_TYPED_ID(SG_RuneGuardConditionRefValid,
		sg_rune_guard_condition_ref_t, SG_RUNE_ORDER_GUARD_CONDITION);
	CHECK_TYPED_ID(SG_RuneDynamicsProofRefValid,
		sg_rune_dynamics_proof_ref_t, SG_RUNE_ORDER_DYNAMICS_PROOF);
	CHECK_TYPED_ID(SG_RuneFieldRegionIdValid,
		sg_rune_field_region_id_t, SG_RUNE_ORDER_FIELD_REGION);
	CHECK_TYPED_ID(SG_RuneFieldHierarchyIdValid,
		sg_rune_field_hierarchy_id_t, SG_RUNE_ORDER_FIELD_HIERARCHY);
	CHECK_TYPED_ID(SG_RuneFieldErrorContractIdValid,
		sg_rune_field_error_contract_id_t,
		SG_RUNE_ORDER_FIELD_ERROR_CONTRACT);
	CHECK_TYPED_ID(SG_FieldChoiceIdValid,
		sg_field_choice_id_t, SG_RUNE_ORDER_FIELD_CHOICE);
	CHECK_TYPED_ID(SG_FieldOutcomeIdValid,
		sg_field_outcome_id_t, SG_RUNE_ORDER_FIELD_OUTCOME);
	CHECK_TYPED_ID(SG_FieldReachAtomIdValid,
		sg_field_reach_atom_id_t, SG_RUNE_ORDER_FIELD_REACH_ATOM);
	CHECK_TYPED_ID(SG_FieldLocalProgressIdValid,
		sg_field_local_progress_id_t, SG_RUNE_ORDER_FIELD_LOCAL_PROGRESS);
	CHECK_TYPED_ID(SG_FieldRefinementNodeIdValid,
		sg_field_refinement_node_id_t,
		SG_RUNE_ORDER_FIELD_REFINEMENT_NODE);
	CHECK_TYPED_ID(SG_FieldRefinementTreeIdValid,
		sg_field_refinement_tree_id_t,
		SG_RUNE_ORDER_FIELD_REFINEMENT_TREE);
#undef CHECK_TYPED_ID
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_WATER;
	mode.value.water.medium = SG_RUNE_MEDIUM_WATER;
	mode.value.water.contents =
		SG_RUNE_CONTENTS_WATER | SG_RUNE_CONTENTS_CURRENT_90;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER |
		SG_RUNE_CONTENTS_LAVA;
	CHECK(!SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER |
		SG_RUNE_CONTENTS_SOLID;
	CHECK(!SG_RuneStateModeValid(&mode));
	mode.value.water.medium = SG_RUNE_MEDIUM_LAVA;
	mode.value.water.contents = SG_RUNE_CONTENTS_LAVA;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER;
	CHECK(!SG_RuneStateModeValid(&mode));
}

static void TestAggregateOwnership(void)
{
	dynamics_fixture_t fixture;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.dynamics.state_vertices = NULL;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.dynamics.state_vertices = fixture.vertices;
	fixture.vertices[0].chart.value = Stable(SG_RUNE_ORDER_STATE_CHART, 2U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.vertices[0].chart = fixture.charts[0].id;
	fixture.patches[0].source_simplex.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.patches[0].source_simplex = fixture.simplices[0].id;
	fixture.charts[0].configuration_cell.value =
		Stable(SG_RUNE_ORDER_CELL, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.charts[0].configuration_cell.value =
		Stable(SG_RUNE_ORDER_CELL, 1U);
	fixture.charts[0].mode.value.supported.support_surface.value =
		Stable(SG_RUNE_ORDER_SURFACE, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.charts[0].mode = SupportedMode();
	fixture.fibers[0].domain.value =
		Stable(SG_RUNE_ORDER_CONTROL_DOMAIN, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.fibers[0].domain = fixture.control_domains[0].id;
	fixture.dynamics.topology_revision++;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestChoiceOutcomeAndProgressContracts(void)
{
	dynamics_fixture_t fixture;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	/* AND is the complete outcome span of one choice. */
	fixture.choices[0].outcomes.count = 1U;
	fixture.local_progress_kernels[0].whole_outcome_targets.count = 1U;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.choices[0].outcomes.count = 2U;
	fixture.local_progress_kernels[0].whole_outcome_targets.count = 2U;
	fixture.outcomes[1].destination_cover.first = 3U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[1].destination_cover.first = 1U;
	/* Guard changes are explicit controllable effects, never future truth. */
	fixture.guard_effects[0].controllable = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.guard_effects[0].controllable = 1U;
	fixture.guard_effects[0].resulting_after = SG_FIELD_GUARD_UNKNOWN;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.guard_effects[0].resulting_after = SG_FIELD_GUARD_TRUE;
	/* Local progress must execute through a nonempty same-atom choice set. */
	fixture.local_progress_kernels[0].admissible_choices.count = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.local_progress_kernels[0].admissible_choices.count = 1U;
	fixture.local_progress_kernels[0].minimum_lyapunov_decrease = 0.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.local_progress_kernels[0].minimum_lyapunov_decrease = 0.125f;
	/* Zero absolute-time advance is legal and does not alter local elapsed. */
	fixture.outcomes[2].absolute_time_advance =
		(sg_rune_time_advance_t){ 0U, 0U };
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[2].absolute_time_advance =
		(sg_rune_time_advance_t){ 2U, 1U };
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[2].absolute_time_advance =
		(sg_rune_time_advance_t){ 0U, 0U };
	fixture.refinement_roots[1] = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestExecutableCoverageAndOperatorRanks(void)
{
	dynamics_fixture_t fixture;
	sg_rune_affine_state_operator_t saved;
	sg_destination_terminal_capture_t first = { 0 };
	sg_destination_terminal_capture_t second = { 0 };
	uint32_t rank;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	first.anchor.owner_identity = 901U;
	first.anchor.destination.kind = SG_DESTINATION_WAYPOINT;
	first.anchor.destination.value.point.point_id = 902U;
	first.anchor.destination_generation = 903U;
	second = first;
	second.anchor.owner_identity = 904U;
	second.anchor.destination.value.point.point_id = 905U;
	second.anchor.destination_generation = 906U;
	second.anchor.position[0] = 1.5f;
	CHECK(SG_FieldLocalProgressKernelAcceptsCapture(
		&fixture.local_progress_kernels[0], &first));
	CHECK(SG_FieldLocalProgressKernelAcceptsCapture(
		&fixture.local_progress_kernels[0], &second));
	second.anchor.position[0] = 3.0f;
	CHECK(!SG_FieldLocalProgressKernelAcceptsCapture(
		&fixture.local_progress_kernels[0], &second));
	saved = fixture.outcomes[0].endpoint;
	for (rank = 0U; rank <= SG_RUNE_STATE_DIMENSION_COUNT; rank++)
	{
		uint32_t diagonal;
		memset(fixture.outcomes[0].endpoint.coefficient, 0,
			sizeof(fixture.outcomes[0].endpoint.coefficient));
		for (diagonal = 0U; diagonal < rank; diagonal++)
			fixture.outcomes[0].endpoint.coefficient[diagonal][diagonal] = 1.0f;
		fixture.outcomes[0].endpoint.exact_rank = (uint8_t)rank;
		CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	}
	fixture.outcomes[0].endpoint.exact_rank = 2U;
	memset(fixture.outcomes[0].endpoint.coefficient, 0,
		sizeof(fixture.outcomes[0].endpoint.coefficient));
	fixture.outcomes[0].endpoint.coefficient[0][0] = 1.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[0].endpoint = saved;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_destination_atoms[0].value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_destination_atoms[0] = fixture.reach_atoms[1].id;
	fixture.local_progress_sources[0].value =
		Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.local_progress_sources[0] = fixture.refinement_nodes[0].id;
	fixture.local_progress_kernels[0].target_atom.value =
		Stable(SG_RUNE_ORDER_FIELD_REACH_ATOM, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.choices[0].guard_requirements.count = 2U;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.guard_requirements[1] = fixture.guard_requirements[0];
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestSimplexGeometry(void)
{
	dynamics_fixture_t fixture;
	sg_rune_state_vertex_t saved;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	saved = fixture.vertices[1];
	fixture.vertices[1].position = fixture.vertices[0].position;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.vertices[1] = saved;
	fixture.vertices[1].position.value[0] = 65.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.vertices[1] = saved;
	fixture.vertices[7].elapsed_ms = 0.0f;
	fixture.vertices[7].position.value[0] = 0.5f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestNearDegenerateSimplexGeometry(void)
{
	dynamics_fixture_t fixture;

	BuildFixture(&fixture);
	fixture.charts[0].embedding.position = Interval3(0.0f, 1.0f);
	fixture.charts[0].embedding.velocity = Interval3(0.0f, 1.0f);
	fixture.charts[0].embedding.elapsed_ms = Interval(0.0f, 1.0f);
	fixture.vertices[7].elapsed_ms = 1e-16f;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static sg_rune_field_region_t Region(uint32_t ordinal, uint32_t parent,
	uint32_t level)
{
	sg_rune_field_region_t region = { 0 };

	region.id.value = Stable(SG_RUNE_ORDER_FIELD_REGION, ordinal);
	region.parent_region = parent;
	region.level = level;
	region.coverage_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, ordinal);
	return region;
}

static void TestExactLeafOwnership(void)
{
	sg_rune_field_region_t regions[3] = {
		Region(1U, SG_RUNE_FIELD_NO_REGION, 0U),
		Region(2U, 0U, 1U),
		Region(3U, 0U, 1U)
	};
	uint32_t children[2] = { 1U, 2U };
	uint32_t chart_owners[2] = { 1U, 2U };
	uint32_t domain_owners[2] = { 1U, 2U };
	uint32_t patch_owners[2] = { 1U, 2U };
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };

	regions[0].children = (sg_rune_field_region_span_t){ 0U, 2U };
	regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 2U };
	regions[0].state_domains = (sg_rune_state_domain_span_t){ 0U, 2U };
	regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 2U };
	regions[1].children.first = 2U;
	regions[1].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	regions[1].state_domains = (sg_rune_state_domain_span_t){ 0U, 1U };
	regions[1].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 1U };
	regions[2].children.first = 2U;
	regions[2].charts = (sg_rune_state_chart_span_t){ 1U, 1U };
	regions[2].state_domains = (sg_rune_state_domain_span_t){ 1U, 1U };
	regions[2].response_patches =
		(sg_rune_response_patch_span_t){ 1U, 1U };
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = 3U;
	hierarchy.children = children;
	hierarchy.child_count = 2U;
	hierarchy.chart_leaf_regions = chart_owners;
	hierarchy.state_domain_leaf_regions = domain_owners;
	hierarchy.response_patch_leaf_regions = patch_owners;
	hierarchy.chart_count = 2U;
	hierarchy.state_domain_count = 2U;
	hierarchy.response_patch_count = 2U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 10U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].charts.count = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].charts.count = 3U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].charts.count = 2U;
	regions[0].state_domains.count = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].state_domains.count = 2U;
	regions[0].response_patches.count = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].response_patches.count = 2U;
	regions[2].charts.first = 0U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[2].charts.first = 1U;
	domain_owners[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	domain_owners[1] = 2U;
	patch_owners[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	patch_owners[1] = 2U;
	children[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
}

static void TestInternalHierarchySummaries(void)
{
	sg_rune_field_region_t regions[4] = {
		Region(1U, SG_RUNE_FIELD_NO_REGION, 0U),
		Region(2U, 0U, 1U), Region(3U, 1U, 2U), Region(4U, 0U, 1U)
	};
	uint32_t children[3] = { 1U, 3U, 2U };
	uint32_t owners[2] = { 2U, 3U };
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };
	size_t index;

	regions[0].children = (sg_rune_field_region_span_t){ 0U, 2U };
	regions[1].children = (sg_rune_field_region_span_t){ 2U, 1U };
	regions[2].children.first = 3U;
	regions[3].children.first = 3U;
	regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 2U };
	regions[1].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	regions[2].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	regions[3].charts = (sg_rune_state_chart_span_t){ 1U, 1U };
	for (index = 0U; index < 4U; index++)
	{
		regions[index].state_domains = (sg_rune_state_domain_span_t){
			regions[index].charts.first, regions[index].charts.count };
		regions[index].response_patches =
			(sg_rune_response_patch_span_t){ regions[index].charts.first,
				regions[index].charts.count };
	}
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = 4U;
	hierarchy.children = children;
	hierarchy.child_count = 3U;
	hierarchy.chart_leaf_regions = owners;
	hierarchy.state_domain_leaf_regions = owners;
	hierarchy.response_patch_leaf_regions = owners;
	hierarchy.chart_count = 2U;
	hierarchy.state_domain_count = 2U;
	hierarchy.response_patch_count = 2U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 10U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[1].charts.count = 2U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
}

static void TestLinearHierarchy(void)
{
	const size_t count = 2048U;
	sg_rune_field_region_t *regions = calloc(count, sizeof(*regions));
	uint32_t *children = calloc(count - 1U, sizeof(*children));
	uint32_t *chart_owners = calloc(count - 1U, sizeof(*chart_owners));
	uint32_t *domain_owners = calloc(count - 1U, sizeof(*domain_owners));
	uint32_t *patch_owners = calloc(count - 1U, sizeof(*patch_owners));
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };
	size_t index;

	CHECK(regions && children && chart_owners && domain_owners && patch_owners);
	if (!regions || !children || !chart_owners || !domain_owners ||
	    !patch_owners)
		goto cleanup;
	regions[0] = Region(1U, SG_RUNE_FIELD_NO_REGION, 0U);
	regions[0].children =
		(sg_rune_field_region_span_t){ 0U, (uint32_t)count - 1U };
	regions[0].charts =
		(sg_rune_state_chart_span_t){ 0U, (uint32_t)count - 1U };
	regions[0].state_domains =
		(sg_rune_state_domain_span_t){ 0U, (uint32_t)count - 1U };
	regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, (uint32_t)count - 1U };
	for (index = 1U; index < count; index++)
	{
		regions[index] = Region((uint32_t)index + 1U, 0U, 1U);
		regions[index].children.first = (uint32_t)count - 1U;
		regions[index].charts =
			(sg_rune_state_chart_span_t){ (uint32_t)index - 1U, 1U };
		regions[index].state_domains =
			(sg_rune_state_domain_span_t){ (uint32_t)index - 1U, 1U };
		regions[index].response_patches =
			(sg_rune_response_patch_span_t){ (uint32_t)index - 1U, 1U };
		children[index - 1U] = (uint32_t)index;
		chart_owners[index - 1U] = (uint32_t)index;
		domain_owners[index - 1U] = (uint32_t)index;
		patch_owners[index - 1U] = (uint32_t)index;
	}
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = count;
	hierarchy.children = children;
	hierarchy.child_count = count - 1U;
	hierarchy.chart_leaf_regions = chart_owners;
	hierarchy.state_domain_leaf_regions = domain_owners;
	hierarchy.response_patch_leaf_regions = patch_owners;
	hierarchy.chart_count = count - 1U;
	hierarchy.state_domain_count = count - 1U;
	hierarchy.response_patch_count = count - 1U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));

cleanup:
	free(patch_owners);
	free(domain_owners);
	free(chart_owners);
	free(children);
	free(regions);
}

static void TestDeepLinearHierarchy(void)
{
	const size_t count = 2048U;
	sg_rune_field_region_t *regions = calloc(count, sizeof(*regions));
	uint32_t *children = calloc(count - 1U, sizeof(*children));
	uint32_t owner = (uint32_t)count - 1U;
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };
	size_t index;

	CHECK(regions && children);
	if (!regions || !children)
		goto cleanup;
	for (index = 0U; index < count; index++)
	{
		regions[index] = Region((uint32_t)index + 1U,
			index == 0U ? SG_RUNE_FIELD_NO_REGION : (uint32_t)index - 1U,
			(uint32_t)index);
		regions[index].children.first = (uint32_t)index;
		regions[index].children.count = index + 1U < count ? 1U : 0U;
		regions[index].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
		regions[index].state_domains =
			(sg_rune_state_domain_span_t){ 0U, 1U };
		regions[index].response_patches =
			(sg_rune_response_patch_span_t){ 0U, 1U };
		if (index + 1U < count)
			children[index] = (uint32_t)index + 1U;
	}
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = count;
	hierarchy.children = children;
	hierarchy.child_count = count - 1U;
	hierarchy.chart_leaf_regions = &owner;
	hierarchy.state_domain_leaf_regions = &owner;
	hierarchy.response_patch_leaf_regions = &owner;
	hierarchy.chart_count = 1U;
	hierarchy.state_domain_count = 1U;
	hierarchy.response_patch_count = 1U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));

cleanup:
	free(children);
	free(regions);
}

static void TestGuidanceIntervals(void)
{
	sg_field_option_t option = {
		.kind = SG_FIELD_OPTION_CONTROL,
		.value.control = {
			.control = { { 0 } },
			.minimum_descent_us = 100U,
			.endpoint_cost = { 1000U, 2000U }
		}
	};
	sg_field_guidance_t guidance = {
		.field = { 1U, 2U, 3U, 4U, 5U, 6U },
		.pose_revision = 6U,
		.sampled_at_ms = 7U,
		.kind = SG_FIELD_GUIDANCE_DESCENT
	};

	option.value.control.control.value =
		Stable(SG_RUNE_ORDER_CONTROL_FIBER, 1U);
	guidance.value.descent.arrival_cost =
		(sg_rune_cost_bounds_t){ 2500U, 3000U };
	guidance.value.descent.residual_bound_us = 10U;
	guidance.value.descent.spatial_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.velocity_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.time_subgradient = Interval(-1.0f, 0.0f);
	guidance.value.descent.position_error = Interval3(-0.5f, 0.5f);
	guidance.value.descent.velocity_error = Interval3(-1.0f, 1.0f);
	guidance.value.descent.time_error = Interval(-0.5f, 0.5f);
	guidance.value.descent.options = &option;
	guidance.value.descent.option_count = 1U;
	guidance.value.descent.required_option_capacity = 1U;
	CHECK(SG_FieldGuidanceValid(&guidance));
	option.value.control.endpoint_cost =
		(sg_rune_cost_bounds_t){ 1000U, 2450U };
	option.value.control.minimum_descent_us = 100U;
	CHECK(!SG_FieldGuidanceValid(&guidance));
	option.value.control.endpoint_cost =
		(sg_rune_cost_bounds_t){ 1000U, 2400U };
	option.value.control.minimum_descent_us = 100U;
	CHECK(SG_FieldGuidanceValid(&guidance));
	option.value.control.control.value =
		Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	CHECK(!SG_FieldGuidanceValid(&guidance));
	CHECK(SG_FieldHandleValid(&guidance.field));
}

static void TestEnvironmentGuardAndAbsoluteTimeContracts(void)
{
	sg_field_guard_state_t guards[2] = { 0 };
	sg_field_guard_state_t future[2] = { 0 };
	sg_field_event_slab_t slabs[2] = { 0 };
	sg_field_environment_t environment = { 0 };
	sg_localized_field_state_t state = { 0 };

	guards[0].condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 1U);
	guards[0].truth = SG_FIELD_GUARD_TRUE;
	guards[1].condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 2U);
	guards[1].truth = SG_FIELD_GUARD_FALSE;
	future[0] = guards[0];
	future[1] = guards[1];
	slabs[0].valid_from_ms = 40U;
	slabs[0].valid_until_ms = 100U;
	slabs[0].exogenous_guards = &future[0];
	slabs[0].exogenous_guard_count = 1U;
	slabs[0].schedule_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	slabs[1].valid_from_ms = 100U;
	slabs[1].valid_until_ms = 200U;
	slabs[1].exogenous_guards = &future[1];
	slabs[1].exogenous_guard_count = 1U;
	slabs[1].schedule_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 2U);
	environment.rune_identity = 1U;
	environment.topology_revision = 2U;
	environment.environment_revision = 3U;
	environment.sampled_at_ms = 50U;
	environment.authority_identity = 4U;
	environment.guards = guards;
	environment.guard_count = 2U;
	environment.event_slabs = slabs;
	environment.event_slab_count = 2U;
	environment.authenticated = 1U;
	CHECK(SG_FieldEnvironmentValid(&environment));
	future[1].truth = SG_FIELD_GUARD_UNKNOWN;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	future[1].truth = SG_FIELD_GUARD_FALSE;
	slabs[1].valid_from_ms = 101U;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	slabs[1].valid_from_ms = 100U;
	guards[1].condition = guards[0].condition;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	guards[1].condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 2U);
	state.rune_identity = 1U;
	state.topology_revision = 2U;
	state.pose_revision = 3U;
	state.sampled_at_ms = 50U;
	state.chart.value = Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	state.mode = SupportedMode();
	state.elapsed_ms = 1000.0f;
	CHECK(SG_LocalizedFieldStateValid(&state));
	CHECK(SG_FieldEnvironmentValid(&environment));
}

int main(void)
{
	TestTypedIdsAndModes();
	TestAggregateOwnership();
	TestChoiceOutcomeAndProgressContracts();
	TestExecutableCoverageAndOperatorRanks();
	TestSimplexGeometry();
	TestNearDegenerateSimplexGeometry();
	TestExactLeafOwnership();
	TestInternalHierarchySummaries();
	TestLinearHierarchy();
	TestDeepLinearHierarchy();
	TestGuidanceIntervals();
	TestEnvironmentGuardAndAbsoluteTimeContracts();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_dynamics_model_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_dynamics_model_test: ok");
	return 0;
}
