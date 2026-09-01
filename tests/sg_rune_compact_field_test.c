#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_compact_field.h"
#include "slipgate/sg_rune_compact_field_plan_private.h"
#include "slipgate/sg_rune_compact_mechanisms.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#if defined(SG_RUNE_COMPACT_FIELD_TEST_WRAP_CALLOC)
static int fail_calloc_after = -1;
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (fail_calloc_after == 0) {
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}
#endif

enum
{
	CELL_COUNT = 4,
	PORTAL_COUNT = 3,
	CAPABILITY_MAX = 24,
	FIBER_MAX = 24,
	HOOK_TARGET_MAX = 6,
	FUNCTION_MAX = 128,
	FUNCTION_REF_MAX = 192,
	CONSTANT_MAX = 128
};

typedef struct field_fixture_s
{
	sg_rune_compact_cell_t cells[CELL_COUNT];
	sg_rune_compact_incidence_t incidences[PORTAL_COUNT * 2U];
	sg_rune_compact_portal_t portals[PORTAL_COUNT];
	sg_rune_movement_capability_t capabilities[CAPABILITY_MAX];
	sg_rune_compact_movement_state_t states[9];
	sg_rune_compact_movement_fiber_t fibers[FIBER_MAX];
	sg_rune_compact_movement_hook_target_t hook_targets[HOOK_TARGET_MAX];
	sg_rune_analytic_function_index_t function_refs[FUNCTION_REF_MAX];
	sg_rune_analytic_function_t functions[FUNCTION_MAX];
	sg_rune_analytic_constant_t constants[CONSTANT_MAX];
	sg_rune_analytic_affine_t affines[4];
	sg_rune_analytic_scalar_bits_t slopes[4];
	sg_rune_analytic_input_dimension_t input_dimensions[4];
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_response_patch_t patches[HOOK_TARGET_MAX];
	sg_rune_compact_response_fact_t facts[HOOK_TARGET_MAX];
	sg_rune_compact_mechanism_transition_t transitions[3];
	sg_rune_compact_mechanism_t static_mechanisms[2];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[2];
	sg_rune_compact_static_t static_data;
	sg_rune_compact_field_mechanism_phase_t phases[2];
	sg_rune_compact_field_mechanism_snapshot_t mechanism_snapshot;
	sg_rune_compact_field_portal_root_t roots[2];
	sg_rune_compact_field_portal_root_snapshot_t root_snapshot;
	sg_rune_compact_model_t model;
	uint32_t capability_count;
	uint32_t fiber_count;
	uint32_t hook_target_count;
	uint32_t function_count;
	uint32_t function_ref_count;
	uint32_t constant_count;
	uint32_t affine_count;
	uint32_t input_count;
} field_fixture_t;

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

int SG_RuneCompactModelValidateBound(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out)
{
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	return model != NULL && expected_identity != NULL &&
		model->version == SG_RUNE_COMPACT_MODEL_VERSION &&
		model->schema_tag == SG_RUNE_COMPACT_MODEL_SCHEMA_TAG &&
		SG_RuneCompactIdentityMatches(&model->identity, expected_identity);
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		memcmp(actual, expected, sizeof(*actual)) == 0;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model,
	const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	uint32_t cell;

	if (model == NULL || point == NULL || location_out == NULL)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	if (point->value[0] < 0 || point->value[0] >= 256)
		return SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
	cell = (uint32_t)point->value[0] / 64U;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = cell;
	location_out->valid_stances = model->cells[cell].valid_stances;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

static sg_rune_analytic_function_span_t AddConstantTriple(
	field_fixture_t *fixture, float cost)
{
	sg_rune_analytic_function_span_t span = {
		fixture->function_ref_count, 3U
	};
	const float values[3] = { cost, 0.25f, 1.0f };
	const sg_rune_analytic_output_meaning_t outputs[3] = {
		SG_RUNE_ANALYTIC_OUTPUT_COST,
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS,
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN
	};
	uint32_t offset;

	for (offset = 0U; offset < 3U; offset++) {
		const uint32_t function = fixture->function_count++;
		const uint32_t constant = fixture->constant_count++;

		fixture->functions[function].form =
			SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->functions[function].definition = constant;
		fixture->functions[function].output = outputs[offset];
		fixture->constants[constant].value.bits = Bits(values[offset]);
		fixture->function_refs[fixture->function_ref_count++].value = function;
	}
	return span;
}

static sg_rune_analytic_function_span_t AddMoverTriple(
	field_fixture_t *fixture, float bias)
{
	sg_rune_analytic_function_span_t span = {
		fixture->function_ref_count, 3U
	};
	const uint32_t function = fixture->function_count++;
	const uint32_t affine = fixture->affine_count++;
	const uint32_t input = fixture->input_count++;
	sg_rune_analytic_function_span_t tail;

	fixture->functions[function].form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	fixture->functions[function].definition = affine;
	fixture->functions[function].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[function].inputs =
		(sg_rune_analytic_input_span_t){ input, 1U };
	fixture->affines[affine].bias.bits = Bits(bias);
	fixture->affines[affine].slopes =
		(sg_rune_analytic_affine_slope_span_t){ input, 1U };
	fixture->slopes[input].bits = Bits(1.0f);
	fixture->input_dimensions[input] = SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE;
	fixture->function_refs[fixture->function_ref_count++].value = function;
	tail = AddConstantTriple(fixture, 0.0f);
	fixture->function_refs[span.first + 1U] = fixture->function_refs[tail.first + 1U];
	fixture->function_refs[span.first + 2U] = fixture->function_refs[tail.first + 2U];
	fixture->function_ref_count = span.first + 3U;
	return span;
}

static uint32_t AddCapability(field_fixture_t *fixture, uint32_t cell,
	uint32_t portal, sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t source_stance,
	sg_rune_stance_validity_t destination_stance,
	sg_rune_movement_fiber_kind_t fiber_kind, uint32_t source_state,
	uint32_t destination_state, uint32_t transition, float cost)
{
	const uint32_t capability_index = fixture->capability_count++;
	const uint32_t fiber_index = fixture->fiber_count++;
	sg_rune_movement_capability_t *capability =
		&fixture->capabilities[capability_index];
	sg_rune_compact_movement_fiber_t *fiber = &fixture->fibers[fiber_index];

	memset(capability, 0, sizeof(*capability));
	capability->cell.value = cell;
	capability->boundary_portal.value = portal;
	capability->kind = kind;
	capability->source_stances = source_stance;
	capability->destination_stances = destination_stance;
	capability->fibers = (sg_rune_movement_fiber_span_t){ fiber_index, 1U };
	memset(fiber, 0, sizeof(*fiber));
	fiber->capability.value = capability_index;
	fiber->kind = fiber_kind;
	fiber->state_variables = SG_RUNE_MOVEMENT_STATE_POSITION |
		SG_RUNE_MOVEMENT_STATE_VELOCITY | SG_RUNE_MOVEMENT_STATE_STANCE |
		SG_RUNE_MOVEMENT_STATE_TIME;
	if (kind <= SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL &&
		kind != SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		fiber->state_variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		fiber->state_variables |= SG_RUNE_MOVEMENT_STATE_WATER |
			SG_RUNE_MOVEMENT_STATE_CURRENT;
	if (kind >= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		kind <= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH)
		fiber->state_variables |= SG_RUNE_MOVEMENT_STATE_HOOK;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		fiber->state_variables |= SG_RUNE_MOVEMENT_STATE_MOVER;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE)
		fiber->state_variables |= SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE;
	fiber->source_state.value = source_state;
	fiber->destination_state.value = destination_state;
	fiber->functions = AddConstantTriple(fixture, cost);
	fiber->hook_targets.first = fixture->hook_target_count;
	fiber->mechanism_transition.value = transition;
	fiber->angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
	fiber->controller_action_controller.value = SG_RUNE_COMPACT_INDEX_NONE;
	fiber->controller_action_target.value = SG_RUNE_COMPACT_INDEX_NONE;
	return fiber_index;
}

static void AddHookCapability(field_fixture_t *fixture,
	sg_rune_movement_capability_kind_t kind, float selected_cost,
	sg_rune_movement_hook_target_class_t visibility)
{
	const uint32_t phase = (uint32_t)kind -
		(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;
	static const uint32_t source_states[6] = { 0U, 6U, 7U, 7U, 8U, 8U };
	static const uint32_t destination_states[6] = { 6U, 7U, 7U, 8U, 8U, 6U };
	const uint32_t fiber_index = AddCapability(fixture, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, kind, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_MOVEMENT_FIBER_HOOK,
		source_states[phase], destination_states[phase],
		SG_RUNE_COMPACT_INDEX_NONE, 90.0f);
	sg_rune_compact_movement_fiber_t *fiber = &fixture->fibers[fiber_index];
	sg_rune_compact_movement_hook_target_t *target =
		&fixture->hook_targets[fixture->hook_target_count];
	sg_rune_analytic_function_span_t *spans[6] = {
		&target->functions.bolt, &target->functions.body,
		&target->functions.pull, &target->functions.release,
		&target->functions.coast, &target->functions.relaunch
	};
	uint32_t index;

	memset(target, 0, sizeof(*target));
	target->fiber.value = fiber_index;
	target->provenance =
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
	target->response.kind = SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT;
	target->response.index = phase;
	target->visibility_class = visibility;
	target->source_stances = SG_RUNE_STANCE_VALID_STANDING;
	target->target_stances = SG_RUNE_STANCE_VALID_STANDING;
	for (index = 0U; index < 6U; index++)
		*spans[index] = AddConstantTriple(fixture,
			index == phase ? selected_cost : 80.0f + (float)index);
	fiber->hook_targets.count = 1U;
	fixture->patches[phase].target_cell.value = 3U;
	fixture->facts[phase].target_patch = phase;
	fixture->hook_target_count++;
}

static void FinalizeModel(field_fixture_t *fixture)
{
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = fixture->function_count;
	fixture->analytic.input_dimensions = fixture->input_dimensions;
	fixture->analytic.input_dimension_count = fixture->input_count;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = fixture->constant_count;
	fixture->analytic.affines = fixture->affines;
	fixture->analytic.affine_count = fixture->affine_count;
	fixture->analytic.affine_slopes = fixture->slopes;
	fixture->analytic.affine_slope_count = fixture->input_count;
	fixture->model.movement_capabilities = fixture->capabilities;
	fixture->model.movement_capability_count = fixture->capability_count;
	fixture->model.movement_states = fixture->states;
	fixture->model.movement_state_count = 9U;
	fixture->model.movement_fibers = fixture->fibers;
	fixture->model.movement_fiber_count = fixture->fiber_count;
	fixture->model.movement_hook_targets = fixture->hook_targets;
	fixture->model.movement_hook_target_count = fixture->hook_target_count;
	fixture->model.movement_fiber_function_refs = fixture->function_refs;
	fixture->model.movement_fiber_function_ref_count = fixture->function_ref_count;
	fixture->model.analytic = &fixture->analytic;
}

static void InitFixture(field_fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	for (index = 0U; index < CELL_COUNT; index++) {
		fixture->cells[index].bounds.mins.value[0] = (int32_t)(index * 64U);
		fixture->cells[index].bounds.maxs.value[0] =
			(int32_t)((index + 1U) * 64U);
		fixture->cells[index].bounds.maxs.value[1] = 32;
		fixture->cells[index].bounds.maxs.value[2] = 32;
		fixture->cells[index].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	}
	for (index = 0U; index < PORTAL_COUNT; index++) {
		fixture->incidences[index * 2U].cell.value = index;
		fixture->incidences[index * 2U + 1U].cell.value = index + 1U;
		fixture->portals[index].negative_incidence.value = index * 2U;
		fixture->portals[index].positive_incidence.value = index * 2U + 1U;
		fixture->portals[index].direction =
			SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE;
		fixture->portals[index].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	}
	fixture->states[0].stance = SG_RUNE_STANCE_VALID_STANDING;
	fixture->states[0].support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	fixture->states[0].water = SG_RUNE_MOVEMENT_WATER_DRY;
	fixture->states[0].hook_phase = SG_HOST_HOOK_IDLE;
	fixture->states[0].mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->states[1] = fixture->states[0];
	fixture->states[1].stance = SG_RUNE_STANCE_VALID_CROUCHING;
	fixture->states[2] = fixture->states[0];
	fixture->states[2].support = SG_RUNE_MOVEMENT_SUPPORT_MOVER;
	fixture->states[2].flags = SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
	fixture->states[2].mover_mechanism = 0U;
	fixture->states[3] = fixture->states[0];
	fixture->states[3].support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	fixture->states[3].flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE |
		SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE;
	fixture->states[4] = fixture->states[0];
	fixture->states[4].support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	fixture->states[4].water = SG_RUNE_MOVEMENT_WATER_SUBMERGED;
	fixture->states[4].flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	fixture->states[5] = fixture->states[0];
	fixture->states[5].support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	fixture->states[5].flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	fixture->states[6] = fixture->states[0];
	fixture->states[6].hook_phase = SG_HOST_HOOK_IN_FLIGHT;
	fixture->states[7] = fixture->states[0];
	fixture->states[7].hook_phase = SG_HOST_HOOK_ATTACHED;
	fixture->states[8] = fixture->states[0];
	fixture->states[8].hook_phase = SG_HOST_HOOK_COAST;
	fixture->model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	fixture->model.identity.schema_id = UINT64_C(12);
	fixture->model.identity.physics.frame_ms = 16U;
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = CELL_COUNT;
	fixture->model.incidences = fixture->incidences;
	fixture->model.incidence_count = PORTAL_COUNT * 2U;
	fixture->model.portals = fixture->portals;
	fixture->model.portal_count = PORTAL_COUNT;
	fixture->model.response.target_patches = fixture->patches;
	fixture->model.response.target_patch_count = HOOK_TARGET_MAX;
	fixture->model.response.facts = fixture->facts;
	fixture->model.response.fact_count = HOOK_TARGET_MAX;
	fixture->model.mechanism_authority_transitions = fixture->transitions;
	fixture->model.mechanism_authority_transition_count = 3U;
	fixture->model.mechanism_authority_count = 2U;
	fixture->static_data.mechanisms = fixture->static_mechanisms;
	fixture->static_data.mechanism_count = 2U;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->model.static_data = &fixture->static_data;
	fixture->phases[0] = (sg_rune_compact_field_mechanism_phase_t){ { 0U }, 2.0f };
	fixture->phases[1] = (sg_rune_compact_field_mechanism_phase_t){ { 1U }, 3.0f };
	fixture->mechanism_snapshot.model_identity = &fixture->model.identity;
	fixture->mechanism_snapshot.frame_sequence = 17U;
	fixture->mechanism_snapshot.phases = fixture->phases;
	fixture->mechanism_snapshot.phase_count = 2U;
}

static sg_rune_compact_field_local_context_t Context(
	const field_fixture_t *fixture, uint32_t cell)
{
	sg_rune_compact_field_local_context_t context;

	memset(&context, 0, sizeof(context));
	context.origin.value[0] = (int32_t)(cell * 64U + 32U);
	context.origin.value[1] = 16;
	context.origin.value[2] = 16;
	context.stance = SG_RUNE_COMPACT_FIELD_STANDING;
	context.support = SG_RUNE_MOVEMENT_SUPPORT_STATIC;
	context.water = SG_RUNE_MOVEMENT_WATER_DRY;
	context.hook_phase = SG_HOST_HOOK_IDLE;
	context.mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	context.frame_sequence = 17U;
	context.mechanisms = &fixture->mechanism_snapshot;
	context.portal_roots = fixture->root_snapshot.model_identity != NULL ?
		&fixture->root_snapshot : NULL;
	return context;
}

static sg_rune_compact_destination_t Destination(uint32_t cell)
{
	sg_rune_compact_destination_t destination;

	memset(&destination, 0, sizeof(destination));
	destination.kind = SG_RUNE_COMPACT_DESTINATION_CELL;
	destination.value.cell.value = cell;
	return destination;
}

static sg_rune_compact_field_t *CreateField(field_fixture_t *fixture)
{
	sg_rune_compact_field_t *field = NULL;

	CHECK(SG_RuneCompactFieldCreate(&fixture->model, &fixture->model.identity,
		&field, NULL) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(field != NULL);
	return field;
}

static sg_rune_compact_destination_plan_t *CreatePlan(
	const sg_rune_compact_field_t *field, uint32_t cell)
{
	const sg_rune_compact_destination_t destination = Destination(cell);
	sg_rune_compact_destination_plan_t *plan = NULL;

	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &plan) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CHECK(plan != NULL);
	return plan;
}

static sg_rune_compact_field_result_t Query(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context)
{
	sg_rune_compact_field_result_t result;

	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactFieldQuery(plan, context, &result) ==
		SG_RUNE_COMPACT_FIELD_OK);
	return result;
}

typedef struct exact_probe_capture_s
{
	sg_rune_compact_field_exact_probe_t probe;
	uint32_t calls;
} exact_probe_capture_t;

static int CaptureExactProbe(void *context,
	const sg_rune_compact_field_exact_probe_t *probe)
{
	exact_probe_capture_t *capture = context;

	if (capture == NULL || probe == NULL)
		return 0;
	capture->probe = *probe;
	capture->calls++;
	return 1;
}

static void TestTopologyCapabilitiesAndDescent(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	exact_probe_capture_t capture;
	uint32_t probe_count;

	InitFixture(&fixture);
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U, SG_RUNE_COMPACT_INDEX_NONE, 4.0f);
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_SWIM,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 4U, 4U, SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 5U, 5U, SG_RUNE_COMPACT_INDEX_NONE, 2.0f);
	(void)AddCapability(&fixture, 1U, 1U, SG_RUNE_MOVEMENT_CAPABILITY_JUMP,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 5U, SG_RUNE_COMPACT_INDEX_NONE, 5.0f);
	(void)AddCapability(&fixture, 2U, 2U, SG_RUNE_MOVEMENT_CAPABILITY_DROP,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 5U, 5U, SG_RUNE_COMPACT_INDEX_NONE, 6.0f);
	(void)AddCapability(&fixture, 2U, 1U, SG_RUNE_MOVEMENT_CAPABILITY_DROP,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 5U, 5U, SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	plan = CreatePlan(field, 3U);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL);
	CHECK(result.value.step.value.portal.next_portal.value == 0U);
	CHECK(result.value.step.value.portal.next_cell.value == 1U);
	CHECK(result.value.step.value.portal.local_cost == 4.0f);
	CHECK(result.value.step.next_cost_to_go.units <
		result.value.step.cost_to_go.units);
	memset(&capture, 0, sizeof(capture));
	probe_count = 0U;
	CHECK(SG_RuneCompactFieldPlanVisitExactStepProbes(plan, &context, &result,
		CaptureExactProbe, &capture, &probe_count) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(probe_count == 1U && capture.calls == 1U);
	CHECK(capture.probe.provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_PMOVE);
	CHECK(capture.probe.provenance.value.pmove.movement.field_arc == 0U);
	CHECK(capture.probe.provenance.value.pmove.movement.capability.value == 0U);
	CHECK(capture.probe.provenance.value.pmove.movement.fiber.value == 0U);
	context.support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	context.water = SG_RUNE_MOVEMENT_WATER_SUBMERGED;
	context.state_flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.value.portal.local_cost == 1.0f);
	context.water = SG_RUNE_MOVEMENT_WATER_DRY;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.value.portal.local_cost == 2.0f);
	SG_RuneCompactFieldPlanDestroy(plan);
	plan = CreatePlan(field, 0U);
	context = Context(&fixture, 2U);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_DISCONNECTED);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestStanceDestinationState(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	exact_probe_capture_t capture;
	uint32_t probe_count = 0U;

	InitFixture(&fixture);
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
		SG_RUNE_STANCE_VALID_CROUCHING, SG_RUNE_STANCE_VALID_CROUCHING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 1U, 1U, SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	plan = CreatePlan(field, 1U);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE);
	CHECK(result.value.step.target_stance == SG_RUNE_COMPACT_FIELD_CROUCHING);
	memset(&capture, 0, sizeof(capture));
	CHECK(SG_RuneCompactFieldPlanVisitExactStepProbes(plan, &context, &result,
		CaptureExactProbe, &capture, &probe_count) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(probe_count == 1U);
	CHECK(capture.calls == 1U);
	CHECK(capture.probe.provenance.kind ==
		SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE);
	CHECK(capture.probe.provenance.value.intrinsic_stance.cell.value == 0U);
	CHECK(capture.probe.provenance.value.intrinsic_stance.source_stance ==
		SG_RUNE_COMPACT_FIELD_STANDING);
	CHECK(capture.probe.provenance.value.intrinsic_stance.destination_stance ==
		SG_RUNE_COMPACT_FIELD_CROUCHING);
	CHECK(capture.probe.provenance.value.intrinsic_stance.frame_ms == 16U);
	CHECK(capture.probe.local_cost.units == UINT64_C(66));
	CHECK(capture.probe.travel_time_seconds == 0.016f);
	CHECK(result.value.step.next_cost_to_go.units +
		capture.probe.local_cost.units == result.value.step.cost_to_go.units);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestDirectMechanisms(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_cost_t source_cost;
	sg_rune_compact_field_result_t result;
	exact_probe_capture_t capture;
	uint32_t probe_count;
	uint32_t fiber;

	InitFixture(&fixture);
	fixture.transitions[0].mechanism = 0U;
	fixture.transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
	fixture.transitions[0].entry_cell.value = 0U;
	fixture.transitions[0].exit_cell.value = 2U;
	fixture.transitions[1].mechanism = 1U;
	fixture.transitions[1].kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH;
	fixture.transitions[1].entry_cell.value = 2U;
	fixture.transitions[1].exit_cell.value = 3U;
	fiber = AddCapability(&fixture, 0U, SG_RUNE_COMPACT_INDEX_NONE,
		SG_RUNE_MOVEMENT_CAPABILITY_MOVER, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION, 2U, 2U, 0U, 30.0f);
	fixture.fibers[fiber].functions = AddMoverTriple(&fixture, 1.0f);
	(void)AddCapability(&fixture, 2U, SG_RUNE_COMPACT_INDEX_NONE,
		SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION, 3U, 3U, 1U, 2.0f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	plan = CreatePlan(field, 3U);
	context = Context(&fixture, 0U);
	context.support = SG_RUNE_MOVEMENT_SUPPORT_MOVER;
	context.state_flags = SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
	context.mover_mechanism = 0U;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT);
	CHECK(result.value.step.value.direct.next_cell.value == 2U);
	CHECK(result.value.step.value.direct.local_cost == 3.0f);
	CHECK(result.value.step.next_cost_to_go.units <
		result.value.step.cost_to_go.units);
	CHECK(SG_RuneCompactFieldPlanCostAt(plan,
		SG_RUNE_COMPACT_FIELD_STANDING, 0U, &source_cost));
	fixture.phases[0].phase = -0.5f;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.value.direct.local_cost == 0.5f);
	CHECK(result.value.step.cost_to_go.units == source_cost.units);
	CHECK(result.value.step.next_cost_to_go.units +
		SG_RUNE_COMPACT_FIELD_COST_SCALE / UINT64_C(2) <
		result.value.step.cost_to_go.units);
	memset(&capture, 0, sizeof(capture));
	probe_count = 0U;
	CHECK(SG_RuneCompactFieldPlanVisitExactStepProbes(plan, &context, &result,
		CaptureExactProbe, &capture, &probe_count) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(probe_count == 1U && capture.calls == 1U);
	CHECK(capture.probe.provenance.kind ==
		SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION);
	CHECK(capture.probe.provenance.value.mechanism.movement.capability.value ==
		0U);
	CHECK(capture.probe.provenance.value.mechanism.movement.fiber.value == 0U);
	CHECK(capture.probe.provenance.value.mechanism.mechanism_transition.value ==
		0U);
	CHECK(capture.probe.provenance.value.mechanism.controller.value ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(capture.probe.provenance.value.mechanism.controller_target.value ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(capture.probe.provenance.value.mechanism.mechanism_kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT);
	context.mechanisms = NULL;
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED);
	context = Context(&fixture, 2U);
	context.support = SG_RUNE_MOVEMENT_SUPPORT_NONE;
	context.state_flags = SG_RUNE_MOVEMENT_STATE_AIRBORNE |
		SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	CHECK(result.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT);
	CHECK(result.value.step.value.direct.next_cell.value == 3U);
	CHECK(result.value.step.value.direct.local_cost == 2.0f);
	memset(&capture, 0, sizeof(capture));
	probe_count = 0U;
	CHECK(SG_RuneCompactFieldPlanVisitExactStepProbes(plan, &context, &result,
		CaptureExactProbe, &capture, &probe_count) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(probe_count == 1U && capture.calls == 1U);
	CHECK(capture.probe.provenance.kind ==
		SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION);
	CHECK(capture.probe.provenance.value.mechanism.movement.capability.value ==
		1U);
	CHECK(capture.probe.provenance.value.mechanism.mechanism_transition.value ==
		1U);
	CHECK(capture.probe.provenance.value.mechanism.mechanism_kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestHookSixPhaseTargets(void)
{
	uint32_t selected;

	for (selected = 0U; selected < 6U; selected++) {
		field_fixture_t fixture;
		sg_rune_compact_field_t *field;
		sg_rune_compact_destination_plan_t *plan;
		sg_rune_compact_field_local_context_t context;
		sg_rune_compact_field_result_t result;
		uint32_t phase;

		InitFixture(&fixture);
		for (phase = 0U; phase < 6U; phase++)
			AddHookCapability(&fixture,
				(sg_rune_movement_capability_kind_t)(
					(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT + phase),
				11.0f + (float)phase, phase == selected ?
					SG_RUNE_MOVEMENT_HOOK_TARGET_VISIBLE :
					SG_RUNE_MOVEMENT_HOOK_TARGET_BLOCKED);
		FinalizeModel(&fixture);
		field = CreateField(&fixture);
		plan = CreatePlan(field, 3U);
		context = Context(&fixture, 0U);
		context.hook_phase = fixture.states[
			fixture.fibers[selected].source_state.value].hook_phase;
		result = Query(plan, &context);
		CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
		CHECK(result.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT);
		CHECK(result.value.step.value.direct.next_cell.value == 3U);
		CHECK(result.value.step.value.direct.local_cost == 11.0f + (float)selected);
		{
			exact_probe_capture_t capture;
			uint32_t probe_count = 0U;

			memset(&capture, 0, sizeof(capture));
			CHECK(SG_RuneCompactFieldPlanVisitExactStepProbes(plan, &context,
				&result, CaptureExactProbe, &capture, &probe_count) ==
				SG_RUNE_COMPACT_FIELD_OK);
			CHECK(probe_count == 1U && capture.calls == 1U);
			CHECK(capture.probe.provenance.kind ==
				SG_RUNE_COMPACT_FIELD_PROBE_HOOK);
			CHECK(capture.probe.provenance.value.hook.movement.capability.value ==
				selected);
			CHECK(capture.probe.provenance.value.hook.movement.fiber.value ==
				selected);
			CHECK(capture.probe.provenance.value.hook.hook_target == selected);
			CHECK(capture.probe.provenance.value.hook.movement.movement_kind ==
				(sg_rune_movement_capability_kind_t)(
					(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT + selected));
		}
		SG_RuneCompactFieldPlanDestroy(plan);
		SG_RuneCompactFieldDestroy(field);
	}
	{
		field_fixture_t fixture;
		sg_rune_compact_field_t *field;
		sg_rune_compact_destination_plan_t *plan;
		sg_rune_compact_field_local_context_t context;
		sg_rune_compact_field_result_t result;

		InitFixture(&fixture);
		AddHookCapability(&fixture, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT,
			1.0f, SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL);
		FinalizeModel(&fixture);
		field = CreateField(&fixture);
		plan = CreatePlan(field, 3U);
		context = Context(&fixture, 0U);
		result = Query(plan, &context);
		CHECK(result.kind == SG_RUNE_COMPACT_FIELD_DISCONNECTED);
		SG_RuneCompactFieldPlanDestroy(plan);
		SG_RuneCompactFieldDestroy(field);
	}
}

static void TestPortalRoots(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_mechanism_index_t mechanism;

	InitFixture(&fixture);
	fixture.portal_mechanisms[0].portal.value = 0U;
	fixture.portal_mechanisms[0].mechanism.value = 0U;
	fixture.portal_mechanisms[0].kind = SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture.portal_mechanisms[1].portal.value = 0U;
	fixture.portal_mechanisms[1].mechanism.value = 1U;
	fixture.portal_mechanisms[1].kind = SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture.static_data.portal_mechanism_count = 2U;
	fixture.roots[0] = (sg_rune_compact_field_portal_root_t){
		{ 0U }, { 0U }, SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED };
	fixture.roots[1] = (sg_rune_compact_field_portal_root_t){
		{ 0U }, { 1U }, SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_BLOCKED };
	fixture.root_snapshot.model_identity = &fixture.model.identity;
	fixture.root_snapshot.frame_sequence = 17U;
	fixture.root_snapshot.roots = fixture.roots;
	fixture.root_snapshot.root_count = 2U;
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U, SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	CHECK(SG_RuneCompactFieldPortalRootCount(field) == 2U);
	CHECK(SG_RuneCompactFieldPortalRootAt(field, 1U, &portal, &mechanism));
	CHECK(portal.value == 0U && mechanism.value == 1U);
	plan = CreatePlan(field, 1U);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED);
	CHECK(result.value.requirements.state ==
		SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_BLOCKED);
	CHECK(result.value.requirements.mechanism_count == 2U);
	fixture.roots[1].state = SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_STEP);
	context.portal_roots = NULL;
	result = Query(plan, &context);
	CHECK(result.kind == SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED);
	CHECK(result.value.requirements.state ==
		SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN);
	SG_RuneCompactFieldPlanDestroy(plan);
	SG_RuneCompactFieldDestroy(field);
}

static void TestFixedPointAndBoundaries(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_field_result_t result;
	sg_rune_compact_field_result_t sentinel;
	sg_rune_compact_destination_t destination;
	sg_rune_compact_field_t *field_sentinel =
		(sg_rune_compact_field_t *)(uintptr_t)1U;
	sg_rune_compact_identity_t wrong;

	InitFixture(&fixture);
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U, SG_RUNE_COMPACT_INDEX_NONE, 0.0f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	plan = CreatePlan(field, 1U);
	context = Context(&fixture, 0U);
	result = Query(plan, &context);
	CHECK(result.value.step.cost_to_go.units ==
		result.value.step.next_cost_to_go.units + UINT64_C(1));
	CHECK(result.value.step.next_cost_to_go.units <
		result.value.step.cost_to_go.units);
	memset(&result, 0xa5, sizeof(result));
	sentinel = result;
	context.velocity[0] = INFINITY;
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT);
	CHECK(memcmp(&result, &sentinel, sizeof(result)) == 0);
	context = Context(&fixture, 0U);
	context.origin.value[0] = -1;
	CHECK(SG_RuneCompactFieldQuery(plan, &context, &result) ==
		SG_RUNE_COMPACT_FIELD_LOCALIZATION_FAILED);
	CHECK(memcmp(&result, &sentinel, sizeof(result)) == 0);
	SG_RuneCompactFieldPlanDestroy(plan);
	destination = Destination(CELL_COUNT);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &plan) ==
		SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION);
	SG_RuneCompactFieldDestroy(field);
	wrong = fixture.model.identity;
	wrong.schema_id++;
	CHECK(SG_RuneCompactFieldCreate(&fixture.model, &wrong, &field_sentinel,
		NULL) == SG_RUNE_COMPACT_FIELD_INVALID_MODEL);
	CHECK(field_sentinel == (sg_rune_compact_field_t *)(uintptr_t)1U);
#if defined(SG_RUNE_COMPACT_FIELD_TEST_WRAP_CALLOC)
	fail_calloc_after = 0;
	CHECK(SG_RuneCompactFieldCreate(&fixture.model, &fixture.model.identity,
		&field_sentinel, NULL) == SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED);
	fail_calloc_after = -1;
#endif
}

static void CheckPlanCostsEqual(
	const sg_rune_compact_destination_plan_t *left,
	const sg_rune_compact_destination_plan_t *right)
{
	uint32_t stance;
	uint32_t cell;

	for (stance = 0U;
		stance < (uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT; stance++)
		for (cell = 0U; cell < CELL_COUNT; cell++)
		{
			sg_rune_compact_field_cost_t left_cost;
			sg_rune_compact_field_cost_t right_cost;

			CHECK(SG_RuneCompactFieldPlanCostAt(left,
				(sg_rune_compact_field_stance_t)stance, cell, &left_cost));
			CHECK(SG_RuneCompactFieldPlanCostAt(right,
				(sg_rune_compact_field_stance_t)stance, cell, &right_cost));
			CHECK(left_cost.units == right_cost.units);
		}
}

static void TestIncrementalDestinationPlans(void)
{
	field_fixture_t fixture;
	sg_rune_compact_landmark_t landmark;
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *previous;
	sg_rune_compact_destination_plan_t *derived = NULL;
	sg_rune_compact_destination_plan_t *clean = NULL;
	sg_rune_compact_field_refresh_report_t report;
	sg_rune_compact_destination_t destination;
	uint32_t cell;

	InitFixture(&fixture);
	for (cell = 0U; cell < CELL_COUNT; cell++)
	{
		fixture.cells[cell].source.area = cell < 2U ? 0U : 1U;
		fixture.cells[cell].source.cluster = cell < 2U ? 10 : 20;
	}
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	(void)AddCapability(&fixture, 1U, 1U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	(void)AddCapability(&fixture, 2U, 2U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	CHECK(SG_RuneCompactFieldRegionCount(field) == 2U);
	previous = CreatePlan(field, 3U);

	destination = Destination(1U);
#if defined(SG_RUNE_COMPACT_FIELD_TEST_WRAP_CALLOC)
	{
		int allocation;

		for (allocation = 0; allocation < 16; allocation++)
		{
			fail_calloc_after = allocation;
			derived = (sg_rune_compact_destination_plan_t *)(uintptr_t)1U;
			CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination,
				&derived, &report) ==
				SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED);
			CHECK(derived == NULL);
		}
		fail_calloc_after = -1;
	}
#endif
	CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination, &derived,
		&report) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(derived != NULL);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &clean) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CheckPlanCostsEqual(derived, clean);
	CHECK(report.affected_state_count != 0U);
	CHECK(report.invalidated_state_count != 0U);
	CHECK(report.affected_leaf_region_count == 2U);
	CHECK(report.affected_coarse_region_count == 2U);
	SG_RuneCompactFieldPlanDestroy(clean);
	SG_RuneCompactFieldPlanDestroy(previous);
	previous = derived;
	derived = NULL;

	destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
	destination.value.point.value[0] = 96;
	destination.value.point.value[1] = 0;
	destination.value.point.value[2] = 0;
	CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination, &derived,
		&report) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(report.affected_state_count == 0U &&
		report.affected_leaf_region_count == 0U &&
		report.affected_coarse_region_count == 0U);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &clean) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CheckPlanCostsEqual(derived, clean);
	SG_RuneCompactFieldPlanDestroy(clean);
	SG_RuneCompactFieldPlanDestroy(previous);
	previous = derived;
	derived = NULL;

	memset(&landmark, 0, sizeof(landmark));
	landmark.kind = SG_RUNE_COMPACT_LANDMARK_HEALTH;
	landmark.cells.count = 2U;
	landmark_cells[0].value = 1U;
	landmark_cells[1].value = 3U;
	fixture.static_data.landmarks = &landmark;
	fixture.static_data.landmark_count = 1U;
	fixture.static_data.landmark_cells = landmark_cells;
	fixture.static_data.landmark_cell_count = 2U;
	destination.kind = SG_RUNE_COMPACT_DESTINATION_ITEM;
	destination.value.item.value = 0U;
	CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination, &derived,
		&report) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &clean) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CheckPlanCostsEqual(derived, clean);
	CHECK(report.decreased_state_count != 0U);
	SG_RuneCompactFieldPlanDestroy(clean);
	SG_RuneCompactFieldPlanDestroy(previous);
	SG_RuneCompactFieldPlanDestroy(derived);
	SG_RuneCompactFieldDestroy(field);
}

static void TestIncrementalRetainedEqualSupport(void)
{
	field_fixture_t fixture;
	sg_rune_compact_landmark_t landmarks[2];
	sg_rune_compact_cell_index_t landmark_cells[3];
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *previous = NULL;
	sg_rune_compact_destination_plan_t *derived = NULL;
	sg_rune_compact_destination_plan_t *clean = NULL;
	sg_rune_compact_field_refresh_report_t report;
	sg_rune_compact_field_cost_t before;
	sg_rune_compact_field_cost_t after;
	sg_rune_compact_destination_t destination;

	InitFixture(&fixture);
	fixture.incidences[4].cell.value = 0U;
	fixture.incidences[5].cell.value = 2U;
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	(void)AddCapability(&fixture, 0U, 2U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	memset(landmarks, 0, sizeof(landmarks));
	landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_HEALTH;
	landmarks[0].cells = (sg_rune_compact_landmark_cell_span_t){ 0U, 2U };
	landmarks[1].kind = SG_RUNE_COMPACT_LANDMARK_HEALTH;
	landmarks[1].cells = (sg_rune_compact_landmark_cell_span_t){ 2U, 1U };
	landmark_cells[0].value = 1U;
	landmark_cells[1].value = 2U;
	landmark_cells[2].value = 2U;
	fixture.static_data.landmarks = landmarks;
	fixture.static_data.landmark_count = 2U;
	fixture.static_data.landmark_cells = landmark_cells;
	fixture.static_data.landmark_cell_count = 3U;
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	destination = Destination(1U);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &previous) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CHECK(SG_RuneCompactFieldPlanCostAt(previous,
		SG_RUNE_COMPACT_FIELD_STANDING, 0U, &before));
	destination.kind = SG_RUNE_COMPACT_DESTINATION_ITEM;
	destination.value.item.value = 0U;
	CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination, &derived,
		&report) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &clean) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CheckPlanCostsEqual(derived, clean);
	CHECK(report.decreased_state_count != 0U);
	CHECK(SG_RuneCompactFieldPlanCostAt(derived,
		SG_RUNE_COMPACT_FIELD_STANDING, 3U, &after));
	CHECK(after.units == UINT64_MAX);
	SG_RuneCompactFieldPlanDestroy(clean);
	SG_RuneCompactFieldPlanDestroy(previous);
	previous = derived;
	derived = NULL;
	clean = NULL;

	destination.value.item.value = 1U;
	CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination, &derived,
		&report) == SG_RUNE_COMPACT_FIELD_OK);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &clean) ==
		SG_RUNE_COMPACT_FIELD_OK);
	CheckPlanCostsEqual(derived, clean);
	CHECK(SG_RuneCompactFieldPlanCostAt(derived,
		SG_RUNE_COMPACT_FIELD_STANDING, 0U, &after));
	CHECK(before.units == after.units);
	CHECK(report.invalidated_state_count != 0U);
	SG_RuneCompactFieldPlanDestroy(clean);
	SG_RuneCompactFieldPlanDestroy(derived);
	SG_RuneCompactFieldPlanDestroy(previous);
	SG_RuneCompactFieldDestroy(field);
}

static void TestIncrementalOverflowRollback(void)
{
	field_fixture_t fixture;
	sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *previous;
	sg_rune_compact_destination_plan_t *derived =
		(sg_rune_compact_destination_plan_t *)(uintptr_t)1U;
	sg_rune_compact_destination_plan_t *clean = NULL;
	sg_rune_compact_field_refresh_report_t report;
	sg_rune_compact_field_cost_t before;
	sg_rune_compact_field_cost_t after;
	sg_rune_compact_destination_t destination;

	InitFixture(&fixture);
	(void)AddCapability(&fixture, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 3.0e15f);
	(void)AddCapability(&fixture, 1U, 1U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
		SG_RUNE_STANCE_VALID_STANDING, SG_RUNE_STANCE_VALID_STANDING,
		SG_RUNE_MOVEMENT_FIBER_PMOVE, 0U, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, 3.0e15f);
	FinalizeModel(&fixture);
	field = CreateField(&fixture);
	previous = CreatePlan(field, 1U);
	CHECK(SG_RuneCompactFieldPlanCostAt(previous,
		SG_RUNE_COMPACT_FIELD_STANDING, 0U, &before));
	destination = Destination(2U);
	memset(&report, 0xff, sizeof(report));
	CHECK(SG_RuneCompactFieldPlanDerive(previous, &destination, &derived,
		&report) == SG_RUNE_COMPACT_FIELD_COST_OVERFLOW);
	CHECK(derived == NULL && report.affected_state_count == 0U &&
		report.examined_transition_count == 0U);
	CHECK(SG_RuneCompactFieldPlanCreate(field, &destination, &clean) ==
		SG_RUNE_COMPACT_FIELD_COST_OVERFLOW);
	CHECK(clean == NULL);
	CHECK(SG_RuneCompactFieldPlanCostAt(previous,
		SG_RUNE_COMPACT_FIELD_STANDING, 0U, &after));
	CHECK(before.units == after.units);
	SG_RuneCompactFieldPlanDestroy(previous);
	SG_RuneCompactFieldDestroy(field);
}

int main(void)
{
	TestTopologyCapabilitiesAndDescent();
	TestStanceDestinationState();
	TestDirectMechanisms();
	TestHookSixPhaseTargets();
	TestPortalRoots();
	TestFixedPointAndBoundaries();
	TestIncrementalDestinationPlans();
	TestIncrementalRetainedEqualSupport();
	TestIncrementalOverflowRollback();
	CHECK(strcmp(SG_RuneCompactFieldStatusString(
		SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED),
		"mechanism phase required") == 0);
	CHECK(strcmp(SG_RuneCompactFieldStatusString(
		(sg_rune_compact_field_status_t)UINT32_MAX),
		"unknown compact field status") == 0);
	if (failures != 0) {
		fprintf(stderr, "%d compact field tests failed\n", failures);
		return 1;
	}
	puts("sg_rune_compact_field_test: PASS");
	return 0;
}
