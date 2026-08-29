#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_dynamics_model.h"

typedef sg_field_status_t (*field_create_fn)(
	const sg_rune_runtime_snapshot_t *, const sg_rune_dynamics_model_t *,
	sg_field_service_t **);
typedef sg_field_status_t (*field_resolve_fn)(sg_field_service_t *,
	const sg_destination_terminal_t *, uint64_t, sg_field_handle_t *);
typedef sg_field_status_t (*field_refresh_fn)(sg_field_service_t *,
	const sg_field_handle_t *, const sg_destination_terminal_t *, uint64_t,
	sg_field_handle_t *);
typedef sg_field_status_t (*field_query_fn)(const sg_field_service_t *,
	const sg_field_handle_t *, const sg_localized_field_state_t *,
	const sg_field_environment_t *, sg_field_guidance_t *);

_Static_assert(_Generic(&SG_FieldServiceCreate, field_create_fn: 1,
	default: 0), "field create must consume the aggregate dynamics model");
_Static_assert(_Generic(&SG_FieldServiceResolve, field_resolve_fn: 1,
	default: 0), "field resolve signature changed");
_Static_assert(_Generic(&SG_FieldServiceRefresh, field_refresh_fn: 1,
	default: 0), "field refresh signature changed");
_Static_assert(_Generic(&SG_FieldServiceQuery, field_query_fn: 1,
	default: 0), "field query signature changed");

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
	sg_rune_phase_basis_t phase;
	sg_rune_model_t rune_model;
	sg_phase_coordinate_t phase_coordinate;
	sg_rune_runtime_snapshot_t snapshot;
	sg_rune_state_vertex_t vertices[8];
	sg_rune_state_chart_t charts[1];
	sg_rune_state_simplex_t simplices[1];
	sg_rune_state_domain_t domains[1];
	sg_rune_control_fiber_t fibers[1];
	sg_rune_response_patch_t patches[1];
	sg_rune_boundary_transfer_t transfers[1];
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
		fixture->vertices[index].position.value[0] = (float)index;
	}
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
	fixture->dynamics.response_patches = fixture->patches;
	fixture->dynamics.response_patch_count = 1U;
	fixture->dynamics.boundary_transfers = fixture->transfers;
	fixture->dynamics.boundary_transfer_count = 1U;
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
	fixture.dynamics.topology_revision++;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
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

static void TestGuidanceIntervals(void)
{
	sg_rune_field_descent_t descent = {
		.control = { { 0 } },
		.minimum_descent_us = 100U,
		.endpoint_cost = { 1000U, 2000U }
	};
	sg_field_guidance_t guidance = {
		.field = { 1U, 2U, 3U, 4U, 5U },
		.pose_revision = 6U,
		.sampled_at_ms = 7U,
		.kind = SG_FIELD_GUIDANCE_DESCENT
	};

	descent.control.value = Stable(SG_RUNE_ORDER_CONTROL_FIBER, 1U);
	guidance.value.descent.arrival_cost =
		(sg_rune_cost_bounds_t){ 2500U, 3000U };
	guidance.value.descent.residual_bound_us = 10U;
	guidance.value.descent.spatial_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.velocity_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.time_subgradient = Interval(-1.0f, 0.0f);
	guidance.value.descent.controls =
		(sg_rune_field_descent_span_t){ &descent, 1U };
	CHECK(SG_FieldGuidanceValid(&guidance));
	descent.endpoint_cost = (sg_rune_cost_bounds_t){ 1000U, 2450U };
	descent.minimum_descent_us = 100U;
	CHECK(!SG_FieldGuidanceValid(&guidance));
	descent.endpoint_cost = (sg_rune_cost_bounds_t){ 1000U, 2400U };
	descent.minimum_descent_us = 100U;
	CHECK(SG_FieldGuidanceValid(&guidance));
	descent.control.value = Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	CHECK(!SG_FieldGuidanceValid(&guidance));
	CHECK(SG_FieldHandleValid(&guidance.field));
}

int main(void)
{
	TestTypedIdsAndModes();
	TestAggregateOwnership();
	TestExactLeafOwnership();
	TestLinearHierarchy();
	TestGuidanceIntervals();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_dynamics_model_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_dynamics_model_test: ok");
	return 0;
}
