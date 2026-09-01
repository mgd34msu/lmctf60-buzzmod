#include "../slipgate/sg_rune_compact_field_service.h"
#include "../slipgate/sg_rune_compact_field_service_private.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

typedef struct release_probe_context_s
{
	sg_rune_compact_field_service_t *service;
	sg_rune_compact_field_handle_t handle;
	sg_rune_compact_field_service_status_t release_status;
	uint32_t calls;
} release_probe_context_t;

#if defined(SG_RUNE_COMPACT_FIELD_SERVICE_TEST_WRAP_CALLOC)
static int fail_calloc_after = -1;
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (fail_calloc_after == 0)
	{
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int ReleaseDuringExactProbe(void *context,
	const sg_rune_compact_field_exact_probe_t *probe)
{
	release_probe_context_t *release = context;

	if (release == NULL || probe == NULL)
		return 0;
	release->calls++;
	release->release_status = SG_RuneCompactFieldServiceRelease(
		release->service, &release->handle);
	return 1;
}

int SG_RuneCompactModelValidateBound(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out)
{
	if (error_out != NULL)
	{
		error_out->code = SG_RUNE_COMPACT_ERROR_NONE;
		error_out->domain = SG_RUNE_COMPACT_RECORD_MODEL;
		error_out->record = 0U;
	}
	return model != NULL && expected_identity != NULL &&
		expected_identity == &model->identity;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL && actual == expected;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model,
	const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	(void)point;
	if (model == NULL || location_out == NULL || model->cell_count == 0U)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = 0U;
	location_out->valid_stances = SG_RUNE_STANCE_VALID_ALL;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

typedef struct service_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_portal_t portals[1];
	sg_rune_movement_capability_t movement_capabilities[2];
	sg_rune_compact_movement_state_t movement_states[1];
	sg_rune_compact_movement_fiber_t movement_fibers[2];
	sg_rune_analytic_function_index_t analytic_refs[6];
	sg_rune_analytic_function_t functions[3];
	sg_rune_analytic_constant_t constants[3];
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
} service_fixture_t;

static void InitFixture(service_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->cells[0].incidences.first = 0U;
	fixture->cells[0].incidences.count = 1U;
	fixture->cells[0].movement_fields.count = 1U;
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].incidences.first = 1U;
	fixture->cells[1].movement_fields.first = 1U;
	fixture->cells[1].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[1].cell.value = 1U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->movement_capabilities[0].cell.value = 0U;
	fixture->movement_capabilities[0].boundary_portal.value = 0U;
	fixture->movement_capabilities[0].kind = SG_RUNE_MOVEMENT_CAPABILITY_WALK;
	fixture->movement_capabilities[0].source_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_capabilities[0].destination_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_capabilities[0].fibers =
		(sg_rune_movement_fiber_span_t){ 0U, 1U };
	fixture->movement_capabilities[1] = fixture->movement_capabilities[0];
	fixture->movement_capabilities[1].cell.value = 1U;
	fixture->movement_capabilities[1].fibers.first = 1U;
	fixture->movement_states[0].stance = SG_RUNE_STANCE_VALID_STANDING;
	fixture->movement_states[0].support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	fixture->movement_states[0].water = SG_RUNE_MOVEMENT_WATER_DRY;
	fixture->movement_states[0].hook_phase = SG_HOST_HOOK_IDLE;
	fixture->movement_states[0].mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].capability.value = 0U;
	fixture->movement_fibers[0].kind = SG_RUNE_MOVEMENT_FIBER_PMOVE;
	fixture->movement_fibers[0].state_variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_SUPPORT |
		SG_RUNE_MOVEMENT_STATE_TIME;
	fixture->movement_fibers[0].source_state.value = 0U;
	fixture->movement_fibers[0].destination_state.value = 0U;
	fixture->movement_fibers[0].functions =
		(sg_rune_analytic_function_span_t){ 0U, 3U };
	fixture->movement_fibers[0].mechanism_transition.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].controller_action_controller.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[0].controller_action_target.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fibers[1] = fixture->movement_fibers[0];
	fixture->movement_fibers[1].capability.value = 1U;
	fixture->movement_fibers[1].functions.first = 3U;
	fixture->functions[0].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->functions[0].definition = 0U;
	fixture->functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[1].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->functions[1].definition = 1U;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->functions[2].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->functions[2].definition = 2U;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->constants[0].value.bits = Bits(1.0f);
	fixture->constants[1].value.bits = Bits(0.1f);
	fixture->constants[2].value.bits = Bits(1.0f);
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 3U;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 3U;
	fixture->analytic_refs[0].value = 0U;
	fixture->analytic_refs[1].value = 1U;
	fixture->analytic_refs[2].value = 2U;
	fixture->analytic_refs[3] = fixture->analytic_refs[0];
	fixture->analytic_refs[4] = fixture->analytic_refs[1];
	fixture->analytic_refs[5] = fixture->analytic_refs[2];
	fixture->model.identity.bsp_bytes = 1U;
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 2U;
	fixture->model.incidences = fixture->incidences;
	fixture->model.incidence_count = 2U;
	fixture->model.portals = fixture->portals;
	fixture->model.portal_count = 1U;
	fixture->model.movement_capabilities = fixture->movement_capabilities;
	fixture->model.movement_capability_count = 2U;
	fixture->model.movement_states = fixture->movement_states;
	fixture->model.movement_state_count = 1U;
	fixture->model.movement_fibers = fixture->movement_fibers;
	fixture->model.movement_fiber_count = 2U;
	fixture->model.movement_fiber_function_refs = fixture->analytic_refs;
	fixture->model.movement_fiber_function_ref_count = 6U;
	fixture->model.analytic = &fixture->analytic;
	fixture->model.static_data = &fixture->static_data;
}

static sg_rune_compact_field_target_t Target(uint64_t id,
	uint64_t generation, sg_rune_compact_field_target_motion_t motion,
	uint32_t cell)
{
	sg_rune_compact_field_target_t target;

	memset(&target, 0, sizeof(target));
	target.target_id = id;
	target.target_generation = generation;
	target.motion = motion;
	target.semantic_destination.kind = SG_DESTINATION_WAYPOINT;
	target.semantic_destination.value.point.point_id = id;
	target.destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	target.destination.value.cell.value = cell;
	return target;
}

static void TestCacheAndMovingRefresh(service_fixture_t *fixture)
{
	sg_rune_compact_field_service_t *service = NULL;
	sg_rune_compact_field_service_stats_t stats;
	sg_rune_compact_field_handle_t first;
	sg_rune_compact_field_handle_t second;
	sg_rune_compact_field_handle_t moving;
	sg_rune_compact_field_handle_t moved;
	sg_rune_compact_field_handle_t same_cell;
	sg_rune_compact_field_target_t target;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	sg_rune_compact_field_service_stats_t before_same_cell;
	release_probe_context_t release_probe;
	uint32_t probe_count = 0U;

	CHECK(SG_RuneCompactFieldServiceCreate(&fixture->model,
		&fixture->model.identity, 41U, 7U, &service, NULL) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(service != NULL);
	target = Target(1U, 1U, SG_RUNE_COMPACT_FIELD_TARGET_STATIC, 1U);
	CHECK(SG_RuneCompactFieldServiceResolve(service, &target, &first) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceResolve(service, &target, &second) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.cached_field_count == 1U && stats.lease_count == 2U);
	CHECK(stats.clean_plan_builds == 1U && stats.static_cache_hits == 1U);
	memset(&context, 0, sizeof(context));
	context.support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	context.water = SG_RUNE_MOVEMENT_WATER_DRY;
	context.hook_phase = SG_HOST_HOOK_IDLE;
	context.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	context.frame_sequence = 1U;
	CHECK(SG_RuneCompactFieldServiceQuery(service, &first, &context,
		&result) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP &&
		result.value.step.value.portal.next_cell.value == 1U);
	CHECK(SG_RuneCompactFieldServiceInvalidateTarget(service, 1U) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceRelease(service, &first) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceRelease(service, &first) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_STALE);
	memset(&release_probe, 0, sizeof(release_probe));
	release_probe.service = service;
	release_probe.handle = second;
	CHECK(SG_RuneCompactFieldServiceVisitExactStepProbes(service, &second,
		&context, &result, ReleaseDuringExactProbe, &release_probe,
		&probe_count) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(probe_count == 1U && release_probe.calls == 1U);
	CHECK(release_probe.release_status == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceRelease(service, &second) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_STALE);
	CHECK(SG_RuneCompactFieldServiceResolve(service, &target, &first) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.clean_plan_builds == 2U && stats.cached_field_count == 1U);

	target = Target(2U, 1U, SG_RUNE_COMPACT_FIELD_TARGET_MOVING, 1U);
	CHECK(SG_RuneCompactFieldServiceResolve(service, &target, &moving) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	target = Target(2U, 2U, SG_RUNE_COMPACT_FIELD_TARGET_MOVING, 0U);
#if defined(SG_RUNE_COMPACT_FIELD_SERVICE_TEST_WRAP_CALLOC)
	fail_calloc_after = 0;
	CHECK(SG_RuneCompactFieldServiceRefresh(service, &moving, &target,
		&moved) == SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED);
	CHECK(SG_RuneCompactFieldServiceHandleCurrent(service, &moving, NULL,
		NULL, NULL));
	fail_calloc_after = -1;
#endif
	CHECK(SG_RuneCompactFieldServiceRefresh(service, &moving, &target,
		&moved) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceQuery(service, &moved, &context,
		&result) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_CELL_DESTINATION &&
		result.value.destination.value.cell.value == 0U);
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.moving_plan_rebuilds == 1U && stats.region_refreshes != 0U);
	CHECK(stats.incremental_plan_refreshes == 1U &&
		stats.incremental_affected_states != 0U &&
		stats.incremental_affected_leaf_regions == 1U &&
		stats.incremental_affected_coarse_regions == 1U &&
		stats.incremental_examined_transitions != 0U);
	CHECK(stats.clean_plan_builds == 3U);
	before_same_cell = stats;
	target.target_generation = 3U;
	target.destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
	memset(&target.destination.value.point, 0,
		sizeof(target.destination.value.point));
	CHECK(SG_RuneCompactFieldServiceRefresh(service, &moved, &target,
		&same_cell) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.incremental_plan_refreshes == 2U);
	CHECK(stats.clean_plan_builds == before_same_cell.clean_plan_builds);
	CHECK(stats.incremental_affected_states ==
		before_same_cell.incremental_affected_states);
	CHECK(stats.incremental_affected_leaf_regions ==
		before_same_cell.incremental_affected_leaf_regions);
	CHECK(stats.incremental_affected_coarse_regions ==
		before_same_cell.incremental_affected_coarse_regions);
	CHECK(stats.region_refreshes == before_same_cell.region_refreshes + 1U);
	CHECK(SG_RuneCompactFieldServiceRelease(service, &first) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceRelease(service, &same_cell) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	SG_RuneCompactFieldServiceDestroy(service);
}

static void TestProviderLease(service_fixture_t *fixture)
{
	sg_rune_compact_field_service_t *service = NULL;
	sg_rune_compact_field_service_provider_t provider;
	sg_rune_compact_field_service_target_request_t request;
	sg_rune_compact_field_service_target_request_t mismatch;
	sg_rune_compact_field_service_target_view_t view;
	sg_rune_compact_field_service_target_binding_t binding;
	sg_rune_compact_field_service_stats_t stats;

	CHECK(SG_RuneCompactFieldServiceCreate(&fixture->model,
		&fixture->model.identity, 43U, 8U, &service, NULL) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceProvider(service, &provider));
	request.commitment_id = 91U;
	request.target = Target(3U, 1U, SG_RUNE_COMPACT_FIELD_TARGET_STATIC, 1U);
	memset(&view, 0, sizeof(view));
	CHECK(provider.locator(provider.context, &request, &view));
	mismatch = request;
	mismatch.commitment_id++;
	memset(&binding, 0, sizeof(binding));
	CHECK(!provider.authority(provider.context, &mismatch, &view,
		&binding));
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.lease_count == 0U);
	provider.release_view(provider.context, view.opaque);
	memset(&view, 0, sizeof(view));
	CHECK(provider.locator(provider.context, &request, &view));
	CHECK(provider.authority(provider.context, &request, &view, &binding));
	CHECK(binding.commitment_id == request.commitment_id &&
		binding.target.target_id == request.target.target_id &&
		binding.accepted_view == view.opaque &&
		binding.coarse_region_epoch != 0U);
	provider.release_view(provider.context, binding.accepted_view);
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.lease_count == 0U);
	SG_RuneCompactFieldServiceDestroy(service);
}

static void TestDroppedFlagRefresh(service_fixture_t *fixture)
{
	sg_rune_compact_field_service_t *service = NULL;
	sg_destination_ref_t destination;
	sg_rune_compact_field_service_live_pose_t pose;
	sg_rune_compact_field_target_t first_target;
	sg_rune_compact_field_target_t moved_target;
	sg_destination_handle_t semantic_handle;
	sg_rune_compact_field_handle_t first;
	sg_rune_compact_field_handle_t moved;
	sg_rune_compact_field_service_stats_t stats;

	CHECK(SG_RuneCompactFieldServiceCreate(&fixture->model,
		&fixture->model.identity, 47U, 9U, &service, NULL) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_DESTINATION_FLAG;
	destination.value.flag.team = 1U;
	destination.value.flag.location = SG_DESTINATION_FLAG_CURRENT;
	memset(&pose, 0, sizeof(pose));
	pose.present = 1U;
	pose.generation = 1U;
	pose.observed_at_ms = 100U;
	CHECK(SG_RuneCompactFieldServiceResolveSemanticTarget(service, 4U,
		&destination, &pose, &first_target, &semantic_handle));
	CHECK(first_target.motion == SG_RUNE_COMPACT_FIELD_TARGET_MOVING &&
		semantic_handle.motion == SG_DESTINATION_MOVING);
	CHECK(SG_RuneCompactFieldServiceResolve(service, &first_target, &first) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	pose.generation = 2U;
	pose.position[0] = 1.0f;
	pose.observed_at_ms = 200U;
	CHECK(SG_RuneCompactFieldServiceResolveSemanticTarget(service, 4U,
		&destination, &pose, &moved_target, &semantic_handle));
	CHECK(SG_RuneCompactFieldServiceRefresh(service, &first, &moved_target,
		&moved) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	SG_RuneCompactFieldServiceStats(service, &stats);
	CHECK(stats.clean_plan_builds == 1U &&
		stats.incremental_plan_refreshes == 1U &&
		stats.incremental_affected_states == 0U &&
		stats.region_refreshes == 1U);
	CHECK(SG_RuneCompactFieldServiceRelease(service, &moved) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	SG_RuneCompactFieldServiceDestroy(service);
}

static void TestMultiRegionRepresentative(service_fixture_t *fixture)
{
	sg_rune_compact_landmark_t landmark;
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_field_service_t *service = NULL;
	sg_rune_compact_field_service_provider_t provider;
	sg_rune_compact_field_service_target_request_t request;
	sg_rune_compact_field_service_target_view_t view;
	sg_rune_compact_field_service_target_binding_t binding;

	memset(&landmark, 0, sizeof(landmark));
	landmark.kind = SG_RUNE_COMPACT_LANDMARK_HEALTH;
	landmark.cells.count = 2U;
	landmark_cells[0].value = 0U;
	landmark_cells[1].value = 1U;
	fixture->static_data.landmarks = &landmark;
	fixture->static_data.landmark_count = 1U;
	fixture->static_data.landmark_cells = landmark_cells;
	fixture->static_data.landmark_cell_count = 2U;
	fixture->cells[0].source.area = 0U;
	fixture->cells[1].source.area = 1U;
	CHECK(SG_RuneCompactFieldServiceCreate(&fixture->model,
		&fixture->model.identity, 53U, 10U, &service, NULL) ==
		SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(SG_RuneCompactFieldServiceProvider(service, &provider));
	memset(&request, 0, sizeof(request));
	request.commitment_id = 101U;
	request.target = Target(5U, 1U, SG_RUNE_COMPACT_FIELD_TARGET_STATIC, 0U);
	request.target.destination.kind = SG_RUNE_COMPACT_DESTINATION_ITEM;
	request.target.destination.value.item.value = 0U;
	CHECK(provider.locator(provider.context, &request, &view));
	CHECK(provider.authority(provider.context, &request, &view, &binding));
	CHECK(binding.coarse_region == 0U && binding.coarse_region_epoch != 0U);
	provider.release_view(provider.context, binding.accepted_view);
	SG_RuneCompactFieldServiceDestroy(service);
	fixture->static_data.landmarks = NULL;
	fixture->static_data.landmark_count = 0U;
	fixture->static_data.landmark_cells = NULL;
	fixture->static_data.landmark_cell_count = 0U;
}

int main(void)
{
	service_fixture_t fixture;

	InitFixture(&fixture);
	TestCacheAndMovingRefresh(&fixture);
	TestProviderLease(&fixture);
	TestDroppedFlagRefresh(&fixture);
	TestMultiRegionRepresentative(&fixture);
	CHECK(strcmp(SG_RuneCompactFieldServiceStatusString(
		SG_RUNE_COMPACT_FIELD_SERVICE_STALE), "stale") == 0);
	CHECK(strcmp(SG_RuneCompactFieldServiceStatusString(
		(sg_rune_compact_field_service_status_t)UINT32_MAX),
		"unknown compact field service status") == 0);
	if (failures != 0)
	{
		fprintf(stderr, "%d compact field service tests failed\n", failures);
		return 1;
	}
	puts("sg_rune_compact_field_service_test: PASS");
	return 0;
}
