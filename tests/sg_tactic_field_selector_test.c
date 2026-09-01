#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_compact_field_service_private.h"
#include "slipgate/sg_rune_compact_spatial_index.h"
#include "slipgate/sg_strategy_runtime_bridge_private.h"
#include "slipgate/sg_tactic_runtime.h"
#include "slipgate/sg_tactic_runtime_private.h"

static int failures;
static sg_host_collision_pose_t issued_pose;

struct sg_compact_localization_observation_s {
	uint32_t guard;
};

typedef struct issued_observation_s {
	struct sg_compact_localization_observation_s capability;
	sg_compact_localization_observation_view_t view;
} issued_observation_t;

static issued_observation_t issued_observation;

struct sg_strategy_runtime_bot_observation_s {
	uint32_t guard;
};

typedef struct issued_bot_observation_s {
	struct sg_strategy_runtime_bot_observation_s capability;
	sg_strategy_runtime_bot_observation_view_t view;
} issued_bot_observation_t;

static issued_bot_observation_t issued_bot_observation;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

enum { TEST_FRAME = 17, TEST_RUNE_IDENTITY = 12,
	TEST_TOPOLOGY_REVISION = 7 };

typedef enum scenario_kind_e {
	SCENARIO_PORTAL = 0, SCENARIO_STANCE, SCENARIO_MOVER,
	SCENARIO_EXTERNAL_FORCE, SCENARIO_HOOK_BOLT, SCENARIO_HOOK_BODY,
	SCENARIO_HOOK_PULL, SCENARIO_HOOK_RELEASE, SCENARIO_HOOK_COAST,
	SCENARIO_HOOK_RELAUNCH, SCENARIO_TELEPORT, SCENARIO_COUNT
} scenario_kind_t;

typedef struct fixture_s {
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_portal_t portal;
	sg_rune_movement_capability_t capability;
	sg_rune_compact_movement_state_t states[2];
	sg_rune_compact_movement_fiber_t fiber;
	sg_rune_compact_movement_hook_target_t hook_target;
	sg_rune_compact_response_patch_t patch;
	sg_rune_compact_response_fact_t fact;
	sg_rune_analytic_function_index_t function_refs[3];
	sg_rune_analytic_function_t functions[3];
	sg_rune_analytic_constant_t constants[2];
	sg_rune_analytic_affine_t affine;
	sg_rune_analytic_scalar_bits_t slope;
	sg_rune_analytic_input_dimension_t input_dimension;
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_mechanism_transition_t transition;
	sg_rune_compact_field_mechanism_phase_t mechanism_phase;
	sg_rune_compact_field_mechanism_snapshot_t mechanism_snapshot;
	sg_rune_compact_field_portal_root_snapshot_t portal_root_snapshot;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
	sg_rune_compact_spatial_index_t *spatial_index;
	sg_compact_localization_binding_t localization;
	sg_host_law_runtime_authority_t host_authority;
	sg_rune_compact_field_service_t *service;
	sg_rune_compact_field_target_t target;
	sg_rune_compact_field_handle_t handle;
} fixture_t;

typedef struct probe_capture_s {
	sg_rune_compact_field_exact_probe_t probe;
	uint32_t calls;
} probe_capture_t;

static uint32_t Bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

int SG_RuneCompactModelValidateBound(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out)
{
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	return model != NULL && expected_identity != NULL &&
		model->version == SG_RUNE_COMPACT_MODEL_VERSION &&
		model->schema_tag == SG_RUNE_COMPACT_MODEL_SCHEMA_TAG &&
		memcmp(&model->identity, expected_identity,
			sizeof(*expected_identity)) == 0;
}

int SG_RuneCompactIdentityMatches(const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		memcmp(actual, expected, sizeof(*actual)) == 0;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	uint32_t cell;
	if (model == NULL || point == NULL || location_out == NULL ||
		point->value[0] < 0 || point->value[0] >= 128)
		return SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
	cell = (uint32_t)point->value[0] / 64U;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = cell;
	location_out->valid_stances = model->cells[cell].valid_stances;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalizeIndexed(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	const sg_rune_compact_cell_index_t *candidate_cells,
	uint32_t candidate_count, sg_rune_compact_location_t *location_out)
{
	sg_rune_compact_location_t location;
	uint32_t index;
	sg_rune_compact_localize_status_t status;

	status = SG_RuneCompactLocalize(model, point, &location);
	if (status != SG_RUNE_COMPACT_LOCALIZE_OK)
		return status;
	for (index = 0U; index < candidate_count; index++)
		if (candidate_cells[index].value == location.cell.value) {
			*location_out = location;
			return SG_RUNE_COMPACT_LOCALIZE_OK;
		}
	return SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
}

static sg_host_law_result_t HostResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;
	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority)
{
	return authority != NULL && authority->epoch == 1U &&
		authority->epoch_complement == ~UINT64_C(1) ?
		HostResult(SG_HOST_LAW_OK) :
		HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawProductionSubjectCurrent(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject)
{
	return SG_HostLawProductionAuthorityCurrent(authority).status ==
		SG_HOST_LAW_OK && subject != NULL ? HostResult(SG_HOST_LAW_OK) :
		HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawProductionSubjectClassifyPose(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	(void)origin;
	if (SG_HostLawProductionSubjectCurrent(authority, subject).status !=
		SG_HOST_LAW_OK || pose_out == NULL)
		return HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
	*pose_out = issued_pose;
	pose_out->stance = stance;
	return HostResult(SG_HOST_LAW_OK);
}

static sg_localization_status_t ValidateObservation(void *context,
	const sg_host_law_runtime_authority_t *authority,
	const sg_compact_localization_observation_t *observation,
	sg_compact_localization_observation_view_t *view_out)
{
	issued_observation_t *issued = context;

	if (issued == NULL || authority == NULL || observation == NULL ||
		view_out == NULL || observation != &issued->capability ||
		issued->capability.guard != UINT32_C(0xc011ab1e) ||
		authority->epoch != 1U)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	*view_out = issued->view;
	return SG_LOCALIZATION_OK;
}

static int ValidateBotObservation(void *context,
	const sg_strategy_runtime_bot_observation_t *observation,
	sg_strategy_runtime_bot_observation_view_t *view_out)
{
	issued_bot_observation_t *issued = context;

	if (issued == NULL || observation == NULL || view_out == NULL ||
		observation != &issued->capability ||
		issued->capability.guard != UINT32_C(0xb07b07a1))
		return 0;
	*view_out = issued->view;
	return 1;
}

static int BotObservationCurrent(void *context,
	const sg_strategy_runtime_bot_observation_view_t *view)
{
	const issued_bot_observation_t *issued = context;

	return issued != NULL && view != NULL &&
		memcmp(view, &issued->view, sizeof(*view)) == 0;
}

static int CaptureProbe(void *context,
	const sg_rune_compact_field_exact_probe_t *probe)
{
	probe_capture_t *capture = context;
	if (capture == NULL || probe == NULL)
		return 0;
	capture->probe = *probe;
	capture->calls++;
	return 1;
}

static int IsHook(scenario_kind_t scenario)
{
	return scenario >= SCENARIO_HOOK_BOLT &&
		scenario <= SCENARIO_HOOK_RELAUNCH;
}

static sg_host_hook_phase_t HookSource(scenario_kind_t scenario)
{
	static const sg_host_hook_phase_t phases[6] = {
		SG_HOST_HOOK_IDLE, SG_HOST_HOOK_IN_FLIGHT,
		SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_ATTACHED,
		SG_HOST_HOOK_COAST, SG_HOST_HOOK_COAST
	};
	return phases[(uint32_t)scenario - (uint32_t)SCENARIO_HOOK_BOLT];
}

static sg_host_hook_phase_t HookDestination(scenario_kind_t scenario)
{
	static const sg_host_hook_phase_t phases[6] = {
		SG_HOST_HOOK_IN_FLIGHT, SG_HOST_HOOK_ATTACHED,
		SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_COAST,
		SG_HOST_HOOK_COAST, SG_HOST_HOOK_IN_FLIGHT
	};
	return phases[(uint32_t)scenario - (uint32_t)SCENARIO_HOOK_BOLT];
}

static void InitIdentity(sg_rune_compact_identity_t *identity)
{
	uint32_t index;
	memset(identity, 0, sizeof(*identity));
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		identity->bsp_sha256[index] = (uint8_t)(index + 1U);
	identity->bsp_bytes = 4096U;
	identity->bsp_checksum = 17U;
	identity->entity_crc32 = 19U;
	identity->physics_abi_id = 23U;
	identity->collision_law_id = 29U;
	identity->pmove_law_id = 31U;
	identity->gravity_law_id = 37U;
	identity->hook_law_id = 41U;
	identity->mechanism_law_id = 43U;
	identity->schema_id = 12U;
	identity->physics.frame_ms = 16U;
}

static void InitHostAuthority(fixture_t *fixture)
{
	sg_host_law_view_t *view;
	memset(&fixture->host_authority, 0, sizeof(fixture->host_authority));
	fixture->host_authority.version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	fixture->host_authority.epoch = 1U;
	fixture->host_authority.epoch_complement = ~UINT64_C(1);
	view = &fixture->host_authority.view;
	view->version = SG_HOST_LAW_PUBLICATION_VERSION;
	view->collision_law_id = fixture->model.identity.collision_law_id;
	view->pmove_law_id = fixture->model.identity.pmove_law_id;
	view->gravity_law_id = fixture->model.identity.gravity_law_id;
	view->hook_law_id = fixture->model.identity.hook_law_id;
	view->mechanism_law_id = fixture->model.identity.mechanism_law_id;
	memcpy(view->bsp_identity.bytes, fixture->model.identity.bsp_sha256,
		sizeof(view->bsp_identity.bytes));
	view->bsp_bytes = fixture->model.identity.bsp_bytes;
	view->pmove_abi.identity = fixture->model.identity.physics_abi_id;
	view->static_identity.bsp_identity = view->bsp_identity;
	view->static_identity.bsp_bytes = fixture->model.identity.bsp_bytes;
	view->static_identity.engine_checksum = fixture->model.identity.bsp_checksum;
	view->static_identity.entity_crc32 = fixture->model.identity.entity_crc32;
	view->static_identity.host_physics_epoch = 1U;
	view->static_identity.physics_abi_id = fixture->model.identity.physics_abi_id;
	view->static_identity.physics.frame_ms = 16U;
}

static int BuildSpatialIndex(fixture_t *fixture)
{
	sg_rune_compact_spatial_cell_input_t cells[2];
	sg_rune_compact_spatial_face_input_t faces[12];
	sg_rune_compact_spatial_topology_input_t topology;
	sg_rune_compact_spatial_error_t error;
	uint32_t cell;
	memset(cells, 0, sizeof(cells));
	memset(faces, 0, sizeof(faces));
	memset(&topology, 0, sizeof(topology));
	for (cell = 0U; cell < 2U; cell++) {
		uint32_t axis;
		cells[cell].first_face = cell * 6U;
		cells[cell].face_count = 6U;
		for (axis = 0U; axis < 3U; axis++) {
			const float minimum =
				(float)fixture->cells[cell].bounds.mins.value[axis] * 0.125f;
			const float maximum =
				(float)fixture->cells[cell].bounds.maxs.value[axis] * 0.125f;
			const uint32_t first = cell * 6U + axis * 2U;
			cells[cell].bounds.mins.value[axis] = minimum;
			cells[cell].bounds.maxs.value[axis] = maximum;
			faces[first].bounds = cells[cell].bounds;
			faces[first].normal[axis] = -1.0f;
			faces[first].distance = -minimum;
			faces[first].source_boundary = first;
			faces[first].ownership = SG_RUNE_BOUNDARY_CLOSED;
			faces[first + 1U].bounds = cells[cell].bounds;
			faces[first + 1U].normal[axis] = 1.0f;
			faces[first + 1U].distance = maximum;
			faces[first + 1U].source_boundary = first + 1U;
			faces[first + 1U].ownership = SG_RUNE_BOUNDARY_CLOSED;
		}
	}
	topology.cells = cells;
	topology.cell_count = 2U;
	topology.faces = faces;
	topology.face_count = 12U;
	memset(&error, 0, sizeof(error));
	return SG_RuneCompactSpatialIndexBuildTopology(&topology, NULL,
		&fixture->spatial_index, &error);
}

static void InitFixture(fixture_t *fixture, scenario_kind_t scenario)
{
	sg_rune_movement_capability_kind_t movement_kind =
		SG_RUNE_MOVEMENT_CAPABILITY_WALK;
	sg_rune_movement_fiber_kind_t fiber_kind = SG_RUNE_MOVEMENT_FIBER_PMOVE;
	uint32_t cell;
	memset(fixture, 0, sizeof(*fixture));
	for (cell = 0U; cell < 2U; cell++) {
		fixture->cells[cell].bounds.mins.value[0] = (int32_t)(cell * 64U);
		fixture->cells[cell].bounds.maxs.value[0] =
			(int32_t)((cell + 1U) * 64U);
		fixture->cells[cell].bounds.maxs.value[1] = 32;
		fixture->cells[cell].bounds.maxs.value[2] = 32;
		fixture->cells[cell].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	}
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[1].cell.value = 1U;
	fixture->portal.negative_incidence.value = 0U;
	fixture->portal.positive_incidence.value = 1U;
	fixture->portal.direction = SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE;
	fixture->portal.valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->states[0].stance = scenario == SCENARIO_STANCE ?
		SG_RUNE_STANCE_VALID_CROUCHING : SG_RUNE_STANCE_VALID_STANDING;
	fixture->states[0].support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	fixture->states[0].water = SG_RUNE_MOVEMENT_WATER_DRY;
	fixture->states[0].hook_phase = SG_HOST_HOOK_IDLE;
	fixture->states[0].mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->states[1] = fixture->states[0];
	if (scenario == SCENARIO_MOVER) {
		movement_kind = SG_RUNE_MOVEMENT_CAPABILITY_MOVER;
		fiber_kind = SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
		fixture->states[0].support = SG_RUNE_MOVEMENT_SUPPORT_MOVER;
		fixture->states[0].flags = SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
		fixture->states[0].mover_mechanism = 0U;
		fixture->states[1] = fixture->states[0];
	} else if (scenario == SCENARIO_TELEPORT) {
		movement_kind = SG_RUNE_MOVEMENT_CAPABILITY_MOVER;
		fiber_kind = SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
	} else if (scenario == SCENARIO_EXTERNAL_FORCE) {
		movement_kind = SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE;
		fiber_kind = SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
		fixture->states[0].support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
		fixture->states[0].flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE |
			SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE;
		fixture->states[1] = fixture->states[0];
	} else if (IsHook(scenario)) {
		movement_kind = (sg_rune_movement_capability_kind_t)(
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT +
			(uint32_t)scenario - (uint32_t)SCENARIO_HOOK_BOLT);
		fiber_kind = SG_RUNE_MOVEMENT_FIBER_HOOK;
		fixture->states[0].hook_phase = HookSource(scenario);
		fixture->states[1].hook_phase = HookDestination(scenario);
	}
	fixture->capability.cell.value = 0U;
	fixture->capability.boundary_portal.value =
		(scenario == SCENARIO_PORTAL || scenario == SCENARIO_STANCE) ?
			0U : SG_RUNE_COMPACT_INDEX_NONE;
	fixture->capability.kind = movement_kind;
	fixture->capability.source_stances = scenario == SCENARIO_STANCE ?
		SG_RUNE_STANCE_VALID_CROUCHING : SG_RUNE_STANCE_VALID_STANDING;
	fixture->capability.destination_stances = fixture->capability.source_stances;
	fixture->capability.fibers = (sg_rune_movement_fiber_span_t){ 0U, 1U };
	fixture->fiber.capability.value = 0U;
	fixture->fiber.kind = fiber_kind;
	fixture->fiber.state_variables = SG_RUNE_MOVEMENT_STATE_POSITION |
		SG_RUNE_MOVEMENT_STATE_VELOCITY | SG_RUNE_MOVEMENT_STATE_STANCE |
		SG_RUNE_MOVEMENT_STATE_TIME;
	if (scenario == SCENARIO_MOVER)
		fixture->fiber.state_variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT |
			SG_RUNE_MOVEMENT_STATE_MOVER;
	else if (scenario == SCENARIO_TELEPORT)
		fixture->fiber.state_variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT |
			SG_RUNE_MOVEMENT_STATE_WATER;
	else if (scenario == SCENARIO_EXTERNAL_FORCE)
		fixture->fiber.state_variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT |
			SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE;
	else if (IsHook(scenario))
		fixture->fiber.state_variables |= SG_RUNE_MOVEMENT_STATE_HOOK;
	else
		fixture->fiber.state_variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	fixture->fiber.source_state.value = 0U;
	fixture->fiber.destination_state.value = 1U;
	fixture->fiber.functions = (sg_rune_analytic_function_span_t){ 0U, 3U };
	fixture->fiber.mechanism_transition.value =
		fiber_kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ? 0U :
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->fiber.angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->fiber.controller_action_controller.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->fiber.controller_action_target.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->functions[0].form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	fixture->functions[0].definition = 0U;
	fixture->functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[0].inputs = (sg_rune_analytic_input_span_t){ 0U, 1U };
	fixture->functions[1].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->functions[1].definition = 0U;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->functions[2].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->functions[2].definition = 1U;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->function_refs[0].value = 0U;
	fixture->function_refs[1].value = 1U;
	fixture->function_refs[2].value = 2U;
	fixture->affine.bias.bits = Bits(1.0f);
	fixture->affine.slopes = (sg_rune_analytic_affine_slope_span_t){ 0U, 1U };
	fixture->slope.bits = Bits(1.0f);
	fixture->input_dimension = SG_RUNE_ANALYTIC_INPUT_VELOCITY_X;
	fixture->constants[0].value.bits = Bits(0.25f);
	fixture->constants[1].value.bits = Bits(1.0f);
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 3U;
	fixture->analytic.input_dimensions = &fixture->input_dimension;
	fixture->analytic.input_dimension_count = 1U;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 2U;
	fixture->analytic.affines = &fixture->affine;
	fixture->analytic.affine_count = 1U;
	fixture->analytic.affine_slopes = &fixture->slope;
	fixture->analytic.affine_slope_count = 1U;
	if (IsHook(scenario)) {
		fixture->fiber.hook_targets =
			(sg_rune_movement_hook_target_span_t){ 0U, 1U };
		fixture->hook_target.fiber.value = 0U;
		fixture->hook_target.response.kind =
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT;
		fixture->hook_target.response.index = 0U;
		fixture->hook_target.provenance =
			SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
		fixture->hook_target.visibility_class = SG_RUNE_MOVEMENT_HOOK_TARGET_VISIBLE;
		fixture->hook_target.source_stances = SG_RUNE_STANCE_VALID_STANDING;
		fixture->hook_target.target_stances = SG_RUNE_STANCE_VALID_STANDING;
		fixture->hook_target.functions.bolt = fixture->fiber.functions;
		fixture->hook_target.functions.body = fixture->fiber.functions;
		fixture->hook_target.functions.pull = fixture->fiber.functions;
		fixture->hook_target.functions.release = fixture->fiber.functions;
		fixture->hook_target.functions.coast = fixture->fiber.functions;
		fixture->hook_target.functions.relaunch = fixture->fiber.functions;
		fixture->patch.target_cell.value = 1U;
		fixture->fact.target_patch = 0U;
	}
	if (fiber_kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION) {
		fixture->transition.mechanism = 0U;
		fixture->transition.kind = scenario == SCENARIO_EXTERNAL_FORCE ?
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH :
			scenario == SCENARIO_TELEPORT ?
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT :
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
		fixture->transition.entry_cell.value = 0U;
		fixture->transition.exit_cell.value = 1U;
		if (scenario == SCENARIO_MOVER)
			fixture->transition.value.transport.mover_model = 3U;
	}
	fixture->model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	InitIdentity(&fixture->model.identity);
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 2U;
	fixture->model.incidences = fixture->incidences;
	fixture->model.incidence_count = 2U;
	fixture->model.portals = &fixture->portal;
	fixture->model.portal_count = 1U;
	fixture->model.movement_capabilities = &fixture->capability;
	fixture->model.movement_capability_count = 1U;
	fixture->model.movement_states = fixture->states;
	fixture->model.movement_state_count = 2U;
	fixture->model.movement_fibers = &fixture->fiber;
	fixture->model.movement_fiber_count = 1U;
	fixture->model.movement_fiber_function_refs = fixture->function_refs;
	fixture->model.movement_fiber_function_ref_count = 3U;
	fixture->model.movement_hook_targets = &fixture->hook_target;
	fixture->model.movement_hook_target_count = IsHook(scenario) ? 1U : 0U;
	fixture->model.response.target_patches = &fixture->patch;
	fixture->model.response.target_patch_count = IsHook(scenario) ? 1U : 0U;
	fixture->model.response.facts = &fixture->fact;
	fixture->model.response.fact_count = IsHook(scenario) ? 1U : 0U;
	fixture->model.mechanism_authority_transitions = &fixture->transition;
	fixture->model.mechanism_authority_transition_count =
		fiber_kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ? 1U : 0U;
	fixture->model.mechanism_authority_count =
		fiber_kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ? 1U : 0U;
	fixture->model.analytic = &fixture->analytic;
	fixture->model.static_data = &fixture->static_data;
	if (fixture->model.mechanism_authority_count != 0U) {
		fixture->mechanism_phase.mechanism.value = 0U;
		fixture->mechanism_phase.phase = 0.0f;
		fixture->mechanism_snapshot.model_identity = &fixture->model.identity;
		fixture->mechanism_snapshot.frame_sequence = TEST_FRAME;
		fixture->mechanism_snapshot.phases = &fixture->mechanism_phase;
		fixture->mechanism_snapshot.phase_count = 1U;
	}
	fixture->target.target_id = 3U;
	fixture->target.target_generation = 1U;
	fixture->target.motion = SG_RUNE_COMPACT_FIELD_TARGET_STATIC;
	fixture->target.semantic_destination.kind = SG_DESTINATION_WAYPOINT;
	fixture->target.semantic_destination.value.point.point_id = 3U;
	fixture->target.destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	fixture->target.destination.value.cell.value = 1U;
	fixture->portal_root_snapshot.model_identity = &fixture->model.identity;
	fixture->portal_root_snapshot.frame_sequence = TEST_FRAME;
}

static sg_rune_compact_field_local_context_t Context(
	const fixture_t *fixture, scenario_kind_t scenario)
{
	sg_rune_compact_field_local_context_t context;
	memset(&context, 0, sizeof(context));
	context.origin.value[0] = 32;
	context.origin.value[1] = 16;
	context.origin.value[2] = 16;
	context.velocity[0] = -0.5f;
	context.stance = SG_RUNE_COMPACT_FIELD_STANDING;
	context.support = fixture->states[0].support;
	context.water = fixture->states[0].water;
	context.hook_phase = fixture->states[0].hook_phase;
	context.state_flags = fixture->states[0].flags;
	context.mover_mechanism = fixture->states[0].mover_mechanism;
	context.frame_sequence = TEST_FRAME;
	if (scenario == SCENARIO_STANCE) {
		context.support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
		context.hook_phase = SG_HOST_HOOK_IDLE;
		context.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	}
	return context;
}

static sg_localization_status_t ObserveLocalized(const fixture_t *fixture,
	const sg_rune_compact_field_local_context_t *context,
	sg_compact_localized_state_t *localized)
{
	sg_host_pmove_result_t result;
	sg_compact_localization_sample_t sample;
	sg_compact_localization_scratch_t scratch;
	uint32_t candidates[2];
	uint32_t axis;

	memset(&result, 0, sizeof(result));
	memset(&issued_pose, 0, sizeof(issued_pose));
	issued_pose.valid = 1;
	issued_pose.supported = context->support ==
		SG_RUNE_MOVEMENT_SUPPORT_NONE ? 0 : 1;
	issued_pose.support_is_mover = context->support ==
		SG_RUNE_MOVEMENT_SUPPORT_MOVER ? 1 : 0;
	issued_pose.gravity = 0.0f;
	issued_pose.physics_abi_id = fixture->model.identity.physics_abi_id;
	issued_pose.support.model_index = context->support ==
		SG_RUNE_MOVEMENT_SUPPORT_MOVER ? 3U : SG_HOST_COLLISION_MODEL_WORLD;
	issued_pose.support.instance_id = issued_pose.supported != 0 ? 1U : 0U;
	result.state.pm_type = PM_NORMAL;
	result.state.pm_flags = issued_pose.supported != 0 ? PMF_ON_GROUND : 0U;
	result.grounded = issued_pose.supported;
	result.support_model_index = issued_pose.support.model_index;
	result.support_instance_id = issued_pose.support.instance_id;
	result.evaluated_steps = 0U;
	result.elapsed_ms = fixture->model.identity.physics.frame_ms;
	result.physics_abi_id = fixture->model.identity.physics_abi_id;
	for (axis = 0U; axis < 3U; axis++) {
		result.state.origin[axis] =
			(int16_t)context->origin.value[axis];
		result.state.velocity[axis] =
			(int16_t)(context->velocity[axis] * 8.0f);
		result.origin[axis] = (float)result.state.origin[axis] * 0.125f;
		result.velocity[axis] = (float)result.state.velocity[axis] * 0.125f;
	}
	memset(&issued_observation, 0, sizeof(issued_observation));
	issued_observation.capability.guard = UINT32_C(0xc011ab1e);
	issued_observation.view.kind = SG_LOCALIZATION_OBSERVATION_PRESENT;
	issued_observation.view.subject.client_id = 5U;
	issued_observation.view.subject.spawn_generation = 9U;
	issued_observation.view.host_authority_epoch = 1U;
	issued_observation.view.frame_sequence = TEST_FRAME;
	issued_observation.view.observed_at_ms = 100U;
	issued_observation.view.model_stamp.identity = TEST_RUNE_IDENTITY;
	issued_observation.view.model_stamp.generation = TEST_TOPOLOGY_REVISION;
	issued_observation.view.model_stamp.frame_sequence = TEST_FRAME;
	issued_observation.view.pmove_result = &result;
	memset(&sample, 0, sizeof(sample));
	sample.observation = &issued_observation.capability;
	scratch.candidates = candidates;
	scratch.candidate_capacity = 2U;
	scratch.candidate_count = 0U;
	return SG_CompactLocalizationObserveWithScratch(&fixture->localization,
		&sample, NULL, &scratch, localized);
}

static void IssueBotObservation(
	const sg_compact_localized_state_t *localized,
	const sg_rune_compact_field_local_context_t *context)
{
	memset(&issued_bot_observation, 0, sizeof(issued_bot_observation));
	issued_bot_observation.capability.guard = UINT32_C(0xb07b07a1);
	issued_bot_observation.view.subject = localized->subject;
	issued_bot_observation.view.host_authority_epoch = 1U;
	issued_bot_observation.view.frame_sequence = localized->frame_sequence;
	issued_bot_observation.view.observed_at_ms = localized->localized_at_ms;
	issued_bot_observation.view.hook_phase = context->hook_phase;
	issued_bot_observation.view.hook_length = context->hook_length;
	issued_bot_observation.view.target_radius = context->target_radius;
}

static int BridgeSelectable(scenario_kind_t scenario)
{
	return scenario != SCENARIO_EXTERNAL_FORCE;
}

static int IssueStrategyProof(fixture_t *fixture,
	const sg_compact_localized_state_t *localized,
	sg_strategy_caller_t *caller, sg_strategy_caller_output_t *output,
	sg_strategy_caller_output_proof_t *proof,
	sg_strategy_runtime_caller_query_proof_t *query_proof,
	sg_rune_compact_field_result_t *field_result,
	sg_rune_compact_field_local_context_t *context)
{
	sg_strategy_runtime_plan_request_t request;
	sg_strategy_caller_plan_t plan;
	sg_strategy_goal_spec_t *goal;

	memset(&request, 0, sizeof(request));
	request.commitment_id = 1U;
	request.localized_player = localized;
	request.mechanisms = fixture->model.mechanism_authority_count != 0U ?
		&fixture->mechanism_snapshot : NULL;
	request.portal_roots = &fixture->portal_root_snapshot;
	request.bot_observation = &issued_bot_observation.capability;
	request.authority.rank = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
	request.authority.principal_kind = SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	request.authority.principal_id = 5U;
	request.spec.goal_count = 1U;
	goal = &request.spec.goals[0];
	goal->id = 1U;
	goal->kind = SG_STRATEGY_GOAL_DESTINATION;
	goal->priority = 1;
	goal->unavailable = SG_STRATEGY_UNAVAILABLE_WAIT;
	goal->choice_count = 1U;
	goal->choices[0].id = 3U;
	goal->choices[0].destination = fixture->target.semantic_destination;
	goal->failure.try_alternatives = 1U;
	goal->failure.max_attempts_per_choice = 1U;
	goal->failure.retry_wake.kind = SG_STRATEGY_RETRY_NONE;
	goal->failure.exhausted = SG_STRATEGY_FAILURE_SKIP_GOAL;
	request.execution_count = 1U;
	request.executions[0].goal_id = 1U;
	request.executions[0].target_id = 3U;
	request.executions[0].live_pose.present = 1U;
	request.executions[0].live_pose.generation = 1U;
	request.executions[0].live_pose.position[0] = 12.0f;
	request.executions[0].live_pose.position[1] = 2.0f;
	request.executions[0].live_pose.position[2] = 2.0f;
	request.executions[0].live_pose.observed_at_ms = localized->localized_at_ms;
	memset(&plan, 0, sizeof(plan));
	memset(caller, 0, sizeof(*caller));
	memset(output, 0, sizeof(*output));
	memset(proof, 0, sizeof(*proof));
	memset(query_proof, 0, sizeof(*query_proof));
	if (!SG_StrategyRuntimePlanResolve(&request, &plan))
		return 0;
	if (!SG_StrategyCallerInit(caller)) {
		SG_StrategyCallerPlanDiscard(&plan);
		return 0;
	}
	if (!SG_StrategyCallerSubmit(caller, &plan, 1U,
		localized->localized_at_ms, SG_STRATEGY_BLOCK_NONE, output)) {
		SG_StrategyCallerPlanDiscard(&plan);
		SG_StrategyCallerDestroy(caller);
		return 0;
	}
	return SG_StrategyRuntimeQueryCallerOutputWithContext(caller, output,
		localized, request.mechanisms, request.portal_roots,
		&issued_bot_observation.capability, field_result, context, proof,
		query_proof);
}

static void CheckProbeIdentity(scenario_kind_t scenario,
	const sg_rune_compact_field_exact_probe_t *probe)
{
	CHECK(probe->successor_cell.value ==
		(scenario == SCENARIO_STANCE ? 0U : 1U));
	if (scenario == SCENARIO_STANCE) {
		CHECK(probe->provenance.kind ==
			SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE);
		CHECK(probe->provenance.value.intrinsic_stance.frame_ms == 16U);
		CHECK(probe->transition_kind == SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE);
	} else if (IsHook(scenario)) {
		CHECK(probe->provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_HOOK);
		CHECK(probe->provenance.value.hook.movement.capability.value == 0U);
		CHECK(probe->provenance.value.hook.movement.fiber.value == 0U);
		CHECK(probe->provenance.value.hook.hook_target == 0U);
		CHECK(probe->provenance.value.hook.movement.source_state.hook_phase ==
			HookSource(scenario));
		CHECK(probe->provenance.value.hook.movement.destination_state.hook_phase ==
			HookDestination(scenario));
		CHECK(probe->provenance.value.hook.movement.movement_kind ==
			(sg_rune_movement_capability_kind_t)(
				(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT +
				(uint32_t)scenario - (uint32_t)SCENARIO_HOOK_BOLT));
	} else if (scenario == SCENARIO_MOVER ||
		scenario == SCENARIO_EXTERNAL_FORCE || scenario == SCENARIO_TELEPORT) {
		CHECK(probe->provenance.kind ==
			SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION);
		CHECK(probe->provenance.value.mechanism.movement.capability.value == 0U);
		CHECK(probe->provenance.value.mechanism.movement.fiber.value == 0U);
		CHECK(probe->provenance.value.mechanism.mechanism_transition.value == 0U);
		CHECK(probe->provenance.value.mechanism.mechanism_kind ==
			(scenario == SCENARIO_EXTERNAL_FORCE ?
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH :
				scenario == SCENARIO_TELEPORT ?
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT :
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT));
	} else {
		CHECK(probe->provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_PMOVE);
		CHECK(probe->provenance.value.pmove.movement.capability.value == 0U);
		CHECK(probe->provenance.value.pmove.movement.fiber.value == 0U);
		CHECK(probe->transition_kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL);
		CHECK(probe->portal.value == 0U);
	}
}

static sg_tactic_capability_t ExpectedCapability(scenario_kind_t scenario)
{
	if (IsHook(scenario))
		return SG_TACTIC_CAPABILITY_HOOK;
	if (scenario == SCENARIO_MOVER)
		return SG_TACTIC_CAPABILITY_MOVER;
	if (scenario == SCENARIO_TELEPORT)
		return SG_TACTIC_CAPABILITY_TELEPORT;
	return SG_TACTIC_CAPABILITY_WALK;
}

static void RunScenario(scenario_kind_t scenario)
{
	fixture_t fixture;
	sg_compact_localization_observation_owner_t owner;
	sg_strategy_runtime_bot_observation_owner_t bot_owner;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_local_context_t bridge_context;
	sg_rune_compact_field_result_t field_result;
	sg_rune_compact_field_result_t bridge_result;
	sg_compact_localized_state_t localized;
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_output_proof_t proof;
	sg_strategy_runtime_caller_query_proof_t query_proof;
	sg_tactic_runtime_step_input_t input;
	sg_tactic_runtime_prepared_step_t prepared;
	sg_tactic_result_t selection;
	probe_capture_t capture;
	uint32_t probe_count = 0U;
	sg_tactic_runtime_status_t runtime_status;
	int providers_cleared = 0;

	InitFixture(&fixture, scenario);
	CHECK(BuildSpatialIndex(&fixture));
	InitHostAuthority(&fixture);
	owner.context = &issued_observation;
	owner.validate = ValidateObservation;
	CHECK(SG_CompactLocalizationBind(&fixture.localization, &fixture.model,
		&fixture.model.identity, fixture.spatial_index, &owner,
		&fixture.host_authority, TEST_RUNE_IDENTITY,
		TEST_TOPOLOGY_REVISION) == SG_LOCALIZATION_OK);
	CHECK(SG_RuneCompactFieldServiceCreate(&fixture.model,
		&fixture.model.identity, TEST_RUNE_IDENTITY, TEST_TOPOLOGY_REVISION,
		&fixture.service, NULL) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	context = Context(&fixture, scenario);
	CHECK(ObserveLocalized(&fixture, &context, &localized) ==
		SG_LOCALIZATION_OK);
	CHECK(SG_CompactLocalizationStateCurrent(&fixture.localization,
		&localized.subject, &localized));
	IssueBotObservation(&localized, &context);
	bot_owner.context = &issued_bot_observation;
	bot_owner.validate = ValidateBotObservation;
	bot_owner.current = BotObservationCurrent;
	CHECK(SG_StrategyRuntimeCompactProviderInstall(fixture.service, &bot_owner));
	memset(&caller, 0, sizeof(caller));
	memset(&output, 0, sizeof(output));
	memset(&proof, 0, sizeof(proof));
	memset(&query_proof, 0, sizeof(query_proof));
	memset(&bridge_result, 0, sizeof(bridge_result));
	memset(&bridge_context, 0, sizeof(bridge_context));
	if (BridgeSelectable(scenario)) {
		const int issued = IssueStrategyProof(&fixture, &localized, &caller,
			&output, &proof, &query_proof, &bridge_result, &bridge_context);

		if (!issued)
			fprintf(stderr, "scenario %u failed strategy proof issuance\n",
				(uint32_t)scenario);
		CHECK(issued);
		fixture.handle = output.field_handle;
		fixture.target = output.compact_target;
		field_result = bridge_result;
		context = bridge_context;
	} else {
		CHECK(!IssueStrategyProof(&fixture, &localized, &caller, &output,
			&proof, &query_proof, &bridge_result, &bridge_context));
		SG_StrategyCallerDestroy(&caller);
		memset(&caller, 0, sizeof(caller));
	}
	if (!BridgeSelectable(scenario)) {
		CHECK(SG_RuneCompactFieldServiceResolve(fixture.service,
			&fixture.target, &fixture.handle) ==
			SG_RUNE_COMPACT_FIELD_SERVICE_OK);
		CHECK(SG_RuneCompactFieldServiceQuery(fixture.service,
			&fixture.handle, &context, &field_result) ==
			SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	}
	CHECK(field_result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	memset(&capture, 0, sizeof(capture));
	CHECK(SG_RuneCompactFieldServiceVisitExactStepProbes(fixture.service,
		&fixture.handle, &context, &field_result, CaptureProbe, &capture,
		&probe_count) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	CHECK(probe_count == 1U);
	CHECK(capture.calls == 1U);
	CheckProbeIdentity(scenario, &capture.probe);
	if (scenario == SCENARIO_STANCE)
		CHECK(field_result.value.step.next_cost_to_go.units +
			capture.probe.local_cost.units ==
			field_result.value.step.cost_to_go.units);
	else {
		CHECK(capture.probe.local_cost.units == 2048U);
		CHECK(field_result.value.step.next_cost_to_go.units +
			capture.probe.local_cost.units <
			field_result.value.step.cost_to_go.units);
	}
	if (BridgeSelectable(scenario)) {
		input.model = &fixture.model;
		input.strategy_caller = &caller;
		input.strategy_output = &output;
		input.strategy_proof = &proof;
		input.query_proof = &query_proof;
		input.localized = &localized;
		input.local_context = &context;
		input.field_result = &field_result;
		CHECK(SG_TacticRuntimeProviderInstall(&fixture.model, fixture.service,
			&fixture.localization, TEST_RUNE_IDENTITY,
			TEST_TOPOLOGY_REVISION));
		if (scenario == SCENARIO_MOVER) {
			sg_strategy_caller_output_proof_t no_proof;
			sg_strategy_runtime_caller_query_proof_t no_query_proof;
			sg_strategy_caller_output_t changed_output = output;
			sg_compact_localized_state_t changed_localized = localized;
			sg_rune_compact_field_local_context_t changed_context = context;
			sg_rune_compact_field_result_t changed_result = field_result;
			sg_tactic_runtime_step_input_t hostile = input;

			CHECK(SG_TacticRuntimePrepareStep(&input, &prepared) ==
				SG_TACTIC_RUNTIME_OK);
			CHECK(SG_TacticRuntimePreparedStepRelease(&prepared));
			CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
			CHECK(!SG_TacticRuntimePreparedStepRelease(&prepared));
			CHECK(SG_StrategyCallerOutputProofCurrent(&caller, &output,
				&proof));
			CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
				&output, &localized, caller.plan.mechanisms,
				caller.plan.portal_roots,
				&issued_bot_observation.capability, &field_result, &context,
				&proof, &query_proof));

			memset(&no_proof, 0, sizeof(no_proof));
			hostile.strategy_proof = &no_proof;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			memset(&no_query_proof, 0, sizeof(no_query_proof));
			hostile = input;
			hostile.query_proof = &no_query_proof;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			changed_context.hook_length = 1.0f;
			hostile = input;
			hostile.local_context = &changed_context;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			changed_context = context;
			changed_context.target_radius = 1.0f;
			hostile = input;
			hostile.local_context = &changed_context;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			changed_result.value.step.value.direct.local_cost += 1.0f;
			hostile = input;
			hostile.field_result = &changed_result;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			fixture.mechanism_phase.phase = 0.5f;
			CHECK(SG_TacticRuntimeSelectStep(&input, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			fixture.mechanism_phase.phase = 0.0f;
			changed_output.plan_id++;
			hostile = input;
			hostile.strategy_output = &changed_output;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			changed_localized.velocity[1] = 1.0f;
			hostile = input;
			hostile.localized = &changed_localized;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			CHECK(SG_TacticRuntimePrepareStep(&input, &prepared) ==
				SG_TACTIC_RUNTIME_OK);
			CHECK(SG_StrategyCallerOutputProofIssue(&caller, &output,
				&no_proof));
			prepared.strategy_proof = no_proof;
			CHECK(SG_TacticRuntimePreparedStepConsume(&prepared) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			CHECK(SG_StrategyRuntimeQueryCallerOutputWithContext(&caller,
				&output, &localized, caller.plan.mechanisms,
				caller.plan.portal_roots, &issued_bot_observation.capability,
				&field_result, &context, &proof, &query_proof));
		}
		if (scenario == SCENARIO_HOOK_BODY) {
			sg_rune_compact_field_local_context_t changed_context = context;
			sg_tactic_runtime_step_input_t hostile = input;

			changed_context.hook_length += 1.0f;
			hostile.local_context = &changed_context;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
		}
		if (scenario == SCENARIO_TELEPORT) {
			sg_tactic_runtime_step_input_t hostile = input;
			const float accepted_phase = fixture.mechanism_phase.phase;

			/* Keep the exact borrowed snapshot identity while changing one
			 * authenticated live phase after query-proof issuance. */
			fixture.mechanism_phase.phase = accepted_phase + 0.5f;
			CHECK(SG_TacticRuntimeSelectStep(&hostile, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			fixture.mechanism_phase.phase = accepted_phase;
		}
		if (scenario == SCENARIO_PORTAL) {
			unsigned char *inactive = (unsigned char *)
				&prepared.exact_probe.provenance.value;
			sg_tactic_result_t accepted_selection;
			size_t byte_index;

			runtime_status = SG_TacticRuntimePrepareStep(&input, &prepared);
			CHECK(runtime_status == SG_TACTIC_RUNTIME_OK);
			for (byte_index =
				sizeof(prepared.exact_probe.provenance.value.pmove);
				byte_index < sizeof(prepared.exact_probe.provenance.value);
				byte_index++)
				inactive[byte_index] = UINT8_C(0xa5);
			runtime_status = SG_TacticRuntimePreparedStepConsume(&prepared);
			CHECK(SG_TacticRuntimePreparedStepCurrent(&prepared));
			fixture.portal_root_snapshot.frame_sequence++;
			CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
			fixture.portal_root_snapshot.frame_sequence--;
			CHECK(SG_TacticRuntimePreparedStepCurrent(&prepared));
			selection = prepared.result;
			accepted_selection = selection;
			CHECK(SG_TacticRuntimePreparedStepRelease(&prepared));
			CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
			CHECK(!SG_TacticRuntimePreparedStepRelease(&prepared));
			CHECK(SG_TacticRuntimeSelectStep(&input, &selection) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			selection = accepted_selection;
		} else if (scenario == SCENARIO_HOOK_BOLT) {
			runtime_status = SG_TacticRuntimePrepareStep(&input, &prepared);
			CHECK(runtime_status == SG_TACTIC_RUNTIME_OK);
			SG_TacticRuntimeProviderClear(fixture.service);
			CHECK(!SG_TacticRuntimeProviderSnapshotCurrent(
				&prepared.provider));
			CHECK(SG_TacticRuntimeProviderInstall(&fixture.model,
				fixture.service, &fixture.localization, TEST_RUNE_IDENTITY,
				TEST_TOPOLOGY_REVISION));
			CHECK(SG_TacticRuntimePreparedStepConsume(&prepared) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
			runtime_status = SG_TacticRuntimeSelectStep(&input, &selection);
		} else if (scenario == SCENARIO_TELEPORT) {
			uint64_t accepted_digest;
			uint64_t accepted_cost;

			runtime_status = SG_TacticRuntimePrepareStep(&input, &prepared);
			CHECK(runtime_status == SG_TACTIC_RUNTIME_OK);
			if (runtime_status == SG_TACTIC_RUNTIME_OK)
				runtime_status = SG_TacticRuntimePreparedStepConsume(&prepared);
			if (runtime_status == SG_TACTIC_RUNTIME_OK) {
				accepted_digest = prepared.query_snapshot.mechanism_digest[0];
				prepared.query_snapshot.mechanism_digest[0] ^= UINT64_C(1);
				CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
				prepared.query_snapshot.mechanism_digest[0] = accepted_digest;
				CHECK(SG_TacticRuntimePreparedStepCurrent(&prepared));
				accepted_cost = prepared.field_result.value.step.cost_to_go.units;
				prepared.field_result.value.step.cost_to_go.units++;
				CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
				prepared.field_result.value.step.cost_to_go.units = accepted_cost;
				CHECK(SG_TacticRuntimePreparedStepCurrent(&prepared));
				fixture.mechanism_phase.phase += 0.5f;
				CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
				fixture.mechanism_phase.phase -= 0.5f;
				CHECK(SG_TacticRuntimePreparedStepCurrent(&prepared));
				selection = prepared.result;
				CHECK(SG_TacticRuntimePreparedStepRelease(&prepared));
				CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
				CHECK(!SG_TacticRuntimePreparedStepRelease(&prepared));
			}
		} else
			runtime_status = SG_TacticRuntimeSelectStep(&input, &selection);
		if (scenario == SCENARIO_STANCE)
			CHECK(runtime_status == SG_TACTIC_RUNTIME_PROBE_REJECTED);
		else {
			CHECK(runtime_status == SG_TACTIC_RUNTIME_OK);
			CHECK(selection.capability == ExpectedCapability(scenario));
			CHECK(selection.nominal_cost.units == 2048U);
			CHECK(selection.successor.cell.value == 1U);
		}
		if (scenario == SCENARIO_TELEPORT &&
			runtime_status == SG_TACTIC_RUNTIME_OK) {
			SG_TacticRuntimeProviderClear(fixture.service);
			SG_StrategyRuntimeCompactProviderClear(fixture.service);
			providers_cleared = 1;
			CHECK(!SG_TacticRuntimePreparedStepCurrent(&prepared));
			CHECK(SG_TacticRuntimePrepareStep(&input, &prepared) ==
				SG_TACTIC_RUNTIME_STALE_FRAME);
		}
		if (!providers_cleared)
			SG_TacticRuntimeProviderClear(fixture.service);
		SG_StrategyCallerDestroy(&caller);
	} else {
		input.model = &fixture.model;
		input.strategy_caller = &caller;
		input.strategy_output = &output;
		input.strategy_proof = &proof;
		input.query_proof = &query_proof;
		input.localized = &localized;
		input.local_context = &context;
		input.field_result = &field_result;
		CHECK(SG_TacticRuntimeProviderInstall(&fixture.model, fixture.service,
			&fixture.localization, TEST_RUNE_IDENTITY,
			TEST_TOPOLOGY_REVISION));
		CHECK(SG_TacticRuntimeSelectStep(&input, &selection) ==
			SG_TACTIC_RUNTIME_STALE_FRAME);
		SG_TacticRuntimeProviderClear(fixture.service);
		CHECK(SG_RuneCompactFieldServiceRelease(fixture.service,
			&fixture.handle) == SG_RUNE_COMPACT_FIELD_SERVICE_OK);
	}
	if (!providers_cleared)
		SG_StrategyRuntimeCompactProviderClear(fixture.service);
	SG_RuneCompactFieldServiceDestroy(fixture.service);
	SG_CompactLocalizationUnbind(&fixture.localization);
	SG_RuneCompactSpatialIndexDestroy(fixture.spatial_index);
}

int main(void)
{
	scenario_kind_t scenario;
	for (scenario = SCENARIO_PORTAL; scenario < SCENARIO_COUNT;
		scenario = (scenario_kind_t)((uint32_t)scenario + 1U))
		RunScenario(scenario);
	if (failures != 0) {
		fprintf(stderr, "sg_tactic_field_selector_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_tactic_field_selector_test: ok");
	return 0;
}
