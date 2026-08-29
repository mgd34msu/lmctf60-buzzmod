#include <stdio.h>

#include "slipgate/sg_rune_dynamics_model.h"

typedef sg_field_status_t (*field_create_fn)(
	const sg_rune_runtime_snapshot_t *, sg_field_service_t **);
typedef sg_field_status_t (*field_resolve_fn)(sg_field_service_t *,
	const sg_destination_terminal_t *, uint64_t, sg_field_handle_t *);
typedef sg_field_status_t (*field_refresh_fn)(sg_field_service_t *,
	const sg_field_handle_t *, const sg_destination_terminal_t *, uint64_t,
	sg_field_handle_t *);
typedef sg_field_status_t (*field_query_fn)(const sg_field_service_t *,
	const sg_field_handle_t *, const sg_localized_field_state_t *,
	const sg_field_environment_t *, sg_field_guidance_t *);

_Static_assert(_Generic(&SG_FieldServiceCreate, field_create_fn: 1,
	default: 0), "field create signature changed");
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

static sg_rune_stable_id_t Stable(uint32_t ordinal)
{
	return (sg_rune_stable_id_t){
		.source_set_identity = 1U,
		.high = (uint64_t)SG_RUNE_ORDER_CELL << 32,
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
	mode.value.supported.support_surface.value = Stable(1U);
	return mode;
}

static sg_rune_state_chart_t Chart(void)
{
	sg_rune_state_chart_t chart = { 0 };

	chart.id.value = Stable(2U);
	chart.configuration_cell.value = Stable(3U);
	chart.mode = SupportedMode();
	chart.embedding.position = Interval3(-64.0f, 64.0f);
	chart.embedding.velocity = Interval3(-320.0f, 320.0f);
	chart.embedding.elapsed_ms = Interval(0.0f, 100.0f);
	chart.embedding.dimension_count = SG_RUNE_STATE_DIMENSION_COUNT;
	chart.state_vertices = (sg_rune_state_vertex_span_t){ 0U, 8U };
	chart.simplices = (sg_rune_state_simplex_span_t){ 0U, 4U };
	chart.response_patches = (sg_rune_response_patch_span_t){ 0U, 2U };
	chart.coverage_proof.value = Stable(4U);
	return chart;
}

static void TestModeVariants(void)
{
	sg_rune_state_mode_t mode = SupportedMode();

	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_WATER;
	mode.value.water.medium = SG_RUNE_MEDIUM_WATER;
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_EMPTY;
	CHECK(!SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_AIRBORNE;
	mode.value.airborne.void_relation = SG_RUNE_VOID_CLEAR;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_HOOK_BOLT;
	mode.value.hook_bolt.visibility_relation.value = Stable(5U);
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_HOOK_PULL;
	mode.value.hook_pull.anchor_surface.value = Stable(6U);
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_HOOK_COAST;
	mode.value.hook_coast.void_relation = SG_RUNE_VOID_ADJACENT;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_MOVER_RELATIVE;
	mode.value.mover_relative.mover.value = Stable(7U);
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_KIND_COUNT;
	CHECK(!SG_RuneStateModeValid(&mode));
}

static void TestStaticModelShapes(void)
{
	sg_rune_state_chart_t chart = Chart();
	sg_rune_control_fiber_t fiber = {
		.id = { { 0 } },
		.source_chart = { { 0 } },
		.domain = { { 0 } },
		.condition = { { 0 } },
		.coverage_proof = { { 0 } }
	};
	sg_rune_response_patch_t patch = { 0 };
	sg_rune_boundary_transfer_t transfer = { 0 };

	CHECK(SG_RuneStateChartShapeValid(&chart));
	chart.embedding.dimension_count--;
	CHECK(!SG_RuneStateChartShapeValid(&chart));

	fiber.id.value = Stable(8U);
	fiber.source_chart.value = Stable(2U);
	fiber.domain.value = Stable(9U);
	fiber.condition.value = Stable(10U);
	fiber.coverage_proof.value = Stable(11U);
	CHECK(SG_RuneControlFiberShapeValid(&fiber));
	fiber.domain.value.source_set_identity = 0U;
	CHECK(!SG_RuneControlFiberShapeValid(&fiber));

	patch.id.value = Stable(12U);
	patch.source_chart.value = Stable(2U);
	patch.source_simplex.value = Stable(13U);
	patch.controls = (sg_rune_control_fiber_span_t){ 0U, 1U };
	patch.flow.position = Interval3(-1.0f, 1.0f);
	patch.flow.velocity = Interval3(-2.0f, 2.0f);
	patch.flow.elapsed_ms = Interval(1.0f, 2.0f);
	patch.running_cost = (sg_rune_cost_bounds_t){ 1000U, 2000U };
	patch.destination_domains = (sg_rune_state_domain_span_t){ 0U, 1U };
	patch.flow_proof.value = Stable(14U);
	CHECK(SG_RuneResponsePatchShapeValid(&patch));
	patch.running_cost.lower_us = 0U;
	CHECK(!SG_RuneResponsePatchShapeValid(&patch));

	transfer.id.value = Stable(15U);
	transfer.source_chart.value = Stable(2U);
	transfer.source_domain.value = Stable(9U);
	transfer.condition.value = Stable(10U);
	transfer.destination_chart.value = Stable(16U);
	transfer.destination_domain.value = Stable(17U);
	transfer.reset_enclosure = patch.flow;
	transfer.transfer_proof.value = Stable(18U);
	CHECK(SG_RuneBoundaryTransferShapeValid(&transfer));
	transfer.destination_domain.value.source_set_identity = 0U;
	CHECK(!SG_RuneBoundaryTransferShapeValid(&transfer));
}

static sg_field_handle_t FieldHandle(void)
{
	return (sg_field_handle_t){ 1U, 2U, 3U, 4U, 5U };
}

static void TestRuntimeShapes(void)
{
	sg_rune_field_error_contract_t error = {
		100U, 200U, 50U,
		{
			{ -0.125f, 0.125f },
			{ -0.25f, 0.25f },
			{ -0.5f, 0.5f }
		},
		{
			{ -1.0f, 1.0f },
			{ -2.0f, 2.0f },
			{ -3.0f, 3.0f }
		},
		{ -0.5f, 0.5f }
	};
	sg_localized_field_state_t state = { 0 };
	sg_field_environment_t environment = {
		.rune_identity = 2U,
		.topology_revision = 3U,
		.environment_revision = 4U,
		.sampled_at_ms = 5U,
		.authenticated = 1U
	};

	CHECK(SG_RuneFieldErrorContractValid(&error));
	error.position_error.x.min_value = 1.0f;
	CHECK(!SG_RuneFieldErrorContractValid(&error));

	state.rune_identity = 2U;
	state.topology_revision = 3U;
	state.pose_revision = 4U;
	state.sampled_at_ms = 5U;
	state.chart.value = Stable(2U);
	state.mode = SupportedMode();
	CHECK(SG_LocalizedFieldStateValid(&state));
	state.elapsed_ms = -1.0f;
	CHECK(!SG_LocalizedFieldStateValid(&state));
	CHECK(SG_FieldEnvironmentValid(&environment));
	environment.reserved[6] = 1U;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	CHECK(SG_FieldHandleValid(&(sg_field_handle_t){ 1U, 2U, 3U, 4U, 5U }));
}

static sg_rune_field_region_t Region(uint32_t ordinal, uint32_t parent,
	uint32_t level)
{
	sg_rune_field_region_t region = { 0 };

	region.id.value = Stable(ordinal);
	region.parent_region = parent;
	region.level = level;
	region.charts = (sg_rune_state_chart_span_t){ ordinal - 20U, 1U };
	region.state_domains =
		(sg_rune_state_domain_span_t){ ordinal - 20U, 1U };
	region.response_patches =
		(sg_rune_response_patch_span_t){ ordinal - 20U, 1U };
	region.coverage_proof.value = Stable(ordinal + 10U);
	return region;
}

static void TestRegionHierarchy(void)
{
	sg_rune_field_region_t regions[4] = {
		Region(20U, SG_RUNE_FIELD_NO_REGION, 0U),
		Region(21U, 0U, 1U),
		Region(22U, 0U, 1U),
		Region(23U, 1U, 2U)
	};
	uint32_t children[3] = { 1U, 2U, 3U };
	uint32_t chart_leaves[3] = { 3U, 3U, 2U };
	sg_rune_field_region_hierarchy_t hierarchy = {
		.regions = regions,
		.region_count = 4U,
		.children = children,
		.child_count = 3U,
		.chart_leaf_regions = chart_leaves,
		.chart_count = 3U,
		.state_domain_count = 3U,
		.response_patch_count = 3U,
		.hierarchy_proof = { { 0 } }
	};

	regions[0].children = (sg_rune_field_region_span_t){ 0U, 2U };
	regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 3U };
	regions[0].state_domains = (sg_rune_state_domain_span_t){ 0U, 3U };
	regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 3U };
	regions[1].charts = (sg_rune_state_chart_span_t){ 0U, 2U };
	regions[1].state_domains = (sg_rune_state_domain_span_t){ 0U, 2U };
	regions[1].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 2U };
	regions[1].children.first = 2U;
	regions[1].children.count = 1U;
	regions[2].charts = (sg_rune_state_chart_span_t){ 2U, 1U };
	regions[2].state_domains = (sg_rune_state_domain_span_t){ 2U, 1U };
	regions[2].response_patches =
		(sg_rune_response_patch_span_t){ 2U, 1U };
	regions[2].children.first = 3U;
	regions[3].charts = (sg_rune_state_chart_span_t){ 0U, 2U };
	regions[3].state_domains = (sg_rune_state_domain_span_t){ 0U, 2U };
	regions[3].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 2U };
	regions[3].children.first = 3U;
	hierarchy.hierarchy_proof.value = Stable(40U);
	CHECK(SG_RuneFieldRegionShapeValid(&regions[0]));
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));
	children[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	children[1] = 2U;
	regions[3].parent_region = 3U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[3].parent_region = 1U;
	chart_leaves[0] = 0U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
}

static void TestGuidanceVariants(void)
{
	sg_field_handle_t handle = FieldHandle();
	sg_rune_field_descent_t descent = {
		.control = { { 0 } },
		.minimum_descent_us = 100U,
		.endpoint_cost = { 1000U, 2000U }
	};
	sg_field_guidance_t guidance = {
		.field = { 1U, 2U, 3U, 4U, 5U },
		.pose_revision = 6U,
		.sampled_at_ms = 7U,
		.kind = SG_FIELD_GUIDANCE_TERMINAL,
		.value.terminal = { { 0U, 0U }, 1U }
	};

	CHECK(SG_FieldGuidanceValid(&guidance));
	guidance.kind = SG_FIELD_GUIDANCE_UNREACHABLE;
	guidance.value.unreachable.arrival_cost =
		(sg_rune_cost_bounds_t){ SG_RUNE_FIELD_COST_INFINITE,
			SG_RUNE_FIELD_COST_INFINITE };
	CHECK(SG_FieldGuidanceValid(&guidance));

	descent.control.value = Stable(19U);
	guidance.kind = SG_FIELD_GUIDANCE_DESCENT;
	guidance.value.descent.arrival_cost =
		(sg_rune_cost_bounds_t){ 2500U, 3000U };
	guidance.value.descent.residual_bound_us = 10U;
	guidance.value.descent.spatial_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.velocity_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.time_subgradient = Interval(-1.0f, 0.0f);
	guidance.value.descent.controls =
		(sg_rune_field_descent_span_t){ &descent, 1U };
	CHECK(SG_FieldGuidanceValid(&guidance));
	descent.minimum_descent_us = 0U;
	CHECK(!SG_FieldGuidanceValid(&guidance));
	descent.minimum_descent_us = 100U;
	descent.endpoint_cost.upper_us = 3000U;
	CHECK(!SG_FieldGuidanceValid(&guidance));
	CHECK(SG_FieldHandleValid(&guidance.field));
	CHECK(SG_FieldHandleValid(&handle));
}

int main(void)
{
	TestModeVariants();
	TestStaticModelShapes();
	TestRuntimeShapes();
	TestRegionHierarchy();
	TestGuidanceVariants();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_dynamics_model_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_dynamics_model_test: ok");
	return 0;
}
