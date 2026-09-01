#include <stdio.h>

#include "slipgate/sg_tactic_contract.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static int ValidateFrame(const void *context, const sg_tactic_request_t *request,
	const sg_tactic_frame_capability_t *frame)
{
	(void)context;
	return request != NULL && frame == request->frame &&
		frame->token == UINT64_C(91);
}

static int ValidateModifier(const void *context,
	const sg_tactic_request_t *request, const sg_tactic_modifier_t *modifier)
{
	(void)context;
	return request != NULL && modifier != NULL && modifier->source_id == 1U;
}

static int ValidateMechanism(const void *context,
	const sg_tactic_request_t *request,
	const sg_tactic_mechanism_request_t *mechanism)
{
	(void)context;
	return request != NULL && mechanism != NULL && mechanism->mechanism_id == 5U;
}

static int ValidateBlock(const void *context, const sg_tactic_request_t *request,
	const sg_tactic_temporary_block_evidence_t *block)
{
	(void)context;
	return request != NULL && block != NULL && block->handoff_id == 4U;
}

static int ValidateProbe(const void *context, const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptor,
	const sg_tactic_candidate_t *candidate,
	sg_rune_compact_field_cost_t nominal_cost)
{
	(void)context;
	return request != NULL && descriptor != NULL && candidate != NULL &&
		nominal_cost.units != SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
}

static const sg_tactic_authority_t authority = {
	.validate_frame = ValidateFrame,
	.validate_modifier = ValidateModifier,
	.validate_mechanism = ValidateMechanism,
	.validate_temporary_block = ValidateBlock,
	.validate_probe = ValidateProbe
};

static sg_tactic_frame_capability_t Frame(void)
{
	return (sg_tactic_frame_capability_t){
		.subject = { .client_id = 1U, .spawn_generation = 2U },
		.model_identity = 3U,
		.rune_identity = 4U,
		.topology_revision = 5U,
		.field_handle = {
			.service_identity = 6U,
			.service_generation = 7U,
			.rune_identity = 4U,
			.topology_revision = 5U,
			.target_id = 8U,
			.target_generation = 9U,
			.field_generation = 10U
		},
		.frame_sequence = 11U,
		.observed_at_ms = 12U,
		.localized = {
			.cell = { 1U },
			.stance = SG_RUNE_COMPACT_FIELD_STANDING,
			.hook_phase = SG_HOST_HOOK_IDLE
		},
		.owner_epoch = 13U,
		.token = UINT64_C(91)
	};
}

static sg_tactic_request_t Request(sg_tactic_frame_capability_t *frame)
{
	return (sg_tactic_request_t){
		.live = {
			.rune_identity = frame->rune_identity,
			.pose_revision = frame->frame_sequence,
			.now_ms = frame->observed_at_ms,
			.cell = { 1U },
			.phase = SG_TACTIC_PHASE_GROUND,
			.stance = SG_RUNE_STANCE_VALID_STANDING,
			.supported = 1U,
			.hook_phase = SG_HOST_HOOK_IDLE
		},
		.gradient = {
			.field_generation = frame->field_handle.field_generation,
			.pose_revision = frame->frame_sequence,
			.sampled_at_ms = frame->observed_at_ms,
			.current_cell = { 1U },
			.transition = {
				.v12 = {
					.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL,
					.cost_to_go = { .units = 100U },
					.next_cost_to_go = { .units = 1U },
					.target_stance = SG_RUNE_COMPACT_FIELD_STANDING,
					.value.portal = {
						.local_cost = 1.0f,
						.next_cell = { 2U },
						.next_portal = { 3U }
					}
				},
				.target_hook_phase = SG_HOST_HOOK_IDLE
			},
			.time_derivative = 1.0f
		},
		.frame = frame,
		.legal_capability_mask = SG_TACTIC_CAPABILITY_BIT(
			SG_TACTIC_CAPABILITY_WALK),
		.authority = &authority
	};
}

static void TestTypedV12Transitions(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_successor_state_t successor;

	CHECK(SG_TacticRequestValid(&request));
	CHECK(SG_TacticTransitionSuccessor(request.gradient.current_cell,
		&request.gradient.transition, &successor));
	CHECK(successor.cell.value == 2U);
	request.gradient.transition.v12.kind =
		SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT;
	request.gradient.transition.v12.value.direct =
		(sg_rune_compact_field_direct_step_t){ .local_cost = 2.0f,
			.next_cell = { 7U } };
	request.gradient.transition.v12.target_stance =
		SG_RUNE_COMPACT_FIELD_CROUCHING;
	request.gradient.transition.target_hook_phase = SG_HOST_HOOK_ATTACHED;
	CHECK(SG_TacticRequestValid(&request));
	CHECK(SG_TacticTransitionSuccessor(request.gradient.current_cell,
		&request.gradient.transition, &successor));
	CHECK(successor.cell.value == 7U);
	CHECK(successor.stance == SG_RUNE_COMPACT_FIELD_CROUCHING);
	CHECK(successor.hook_phase == SG_HOST_HOOK_ATTACHED);
	request.gradient.transition.v12.kind =
		SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE;
	CHECK(SG_TacticRequestValid(&request));
	CHECK(SG_TacticTransitionSuccessor(request.gradient.current_cell,
		&request.gradient.transition, &successor));
	CHECK(successor.cell.value == request.live.cell.value);
	request.gradient.transition.v12.kind =
		SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
	request.gradient.transition.v12.value.portal.next_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	CHECK(!SG_TacticRequestValid(&request));
}

static void TestCanonicalSourceAndLiveProbeDescent(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_rune_compact_field_cost_t nominal;

	/* The exact live probe may improve on the canonical source path, but it
	 * cannot merely reproduce its cost. */
	CHECK(SG_TacticLiveDescentValid(&request.gradient.transition,
		(sg_rune_compact_field_cost_t){ .units = 98U }, &nominal));
	CHECK(nominal.units == 99U);
	CHECK(!SG_TacticLiveDescentValid(&request.gradient.transition,
		(sg_rune_compact_field_cost_t){ .units = 99U }, &nominal));
}

static void TestFrameAndMechanismShape(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_mechanism_request_t mechanism = {
		.mechanism_revision = 1U,
		.handoff_id = 4U,
		.mechanism_id = 5U,
		.controller_id = 6U,
		.portal = { 3U },
		.entry_cell = { 1U },
		.exit_cell = { 2U },
		.trigger_id = 7U
	};
	sg_tactic_temporary_block_evidence_t block = {
		.observed_at_ms = frame.observed_at_ms,
		.mechanism_revision = 1U,
		.handoff_id = 4U,
		.portal = { 3U },
		.entry_cell = { 1U },
		.exit_cell = { 2U }
	};

	frame.subject.spawn_generation = 0U;
	CHECK(!SG_TacticRequestValid(&request));
	frame = Frame();
	request.frame = &frame;
	frame.field_handle.rune_identity = 99U;
	CHECK(!SG_TacticRequestValid(&request));
	frame = Frame();
	request.frame = &frame;
	frame.localized.stance = SG_RUNE_COMPACT_FIELD_CROUCHING;
	CHECK(!SG_TacticRequestValid(&request));
	frame = Frame();
	request.frame = &frame;
	request.mechanism = &mechanism;
	CHECK(SG_TacticRequestValid(&request));
	request.temporary_block = &block;
	CHECK(SG_TacticRequestValid(&request));
	request.mechanism = NULL;
	CHECK(!SG_TacticRequestValid(&request));
}

static void TestModifierAndResults(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_modifier_t modifier = {
		.kind = SG_TACTIC_MODIFIER_THREAT,
		.source_id = 1U,
		.cost_delta_units = -1,
		.capability_mask = SG_TACTIC_CAPABILITY_BIT(
			SG_TACTIC_CAPABILITY_WALK),
		.target_kind = SG_TACTIC_MODIFIER_TARGET_EXACT_SUCCESSOR,
		.target = {
			.cell = { 2U },
			.stance = SG_RUNE_COMPACT_FIELD_STANDING,
			.hook_phase = SG_HOST_HOOK_IDLE
		},
		.expires_at_ms = 13U,
		.active = 1U
	};
	sg_tactic_result_t result = {
		.status = SG_TACTIC_RESULT_PROGRESS,
		.capability = SG_TACTIC_CAPABILITY_WALK,
		.successor = modifier.target,
		.target_phase = SG_TACTIC_PHASE_GROUND,
		.nominal_cost = { .units = 2U },
		.progress = 0.5f
	};

	request.modifiers = &modifier;
	request.modifier_count = 1U;
	CHECK(SG_TacticRequestValid(&request));
	modifier.cost_delta_units = SG_TACTIC_MODIFIER_MAX_DEFORMATION_UNITS + 1;
	CHECK(!SG_TacticRequestValid(&request));
	CHECK(SG_TacticResultValid(&result));
	result.nominal_cost.units = SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	CHECK(!SG_TacticResultValid(&result));
}

int main(void)
{
	TestTypedV12Transitions();
	TestCanonicalSourceAndLiveProbeDescent();
	TestFrameAndMechanismShape();
	TestModifierAndResults();
	if (failures != 0)
	{
		fprintf(stderr, "sg_tactic_contract_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_tactic_contract_test: ok");
	return 0;
}
