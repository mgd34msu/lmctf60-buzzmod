#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_tactic_policy.h"
#include "slipgate/sg_weapon_effect_profile.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct probe_context_s
{
	sg_tactic_candidate_t candidate;
	uint32_t calls;
	int available;
	int authenticated;
} probe_context_t;

typedef struct authority_context_s
{
	int allow_modifiers;
	int allow_mechanism;
	int allow_block;
} authority_context_t;

static authority_context_t authority_context = { 1, 1, 1 };
static int require_authorized_test_cost;

static uint64_t Mix(uint64_t state, uint64_t value)
{
	return (state ^ value) * UINT64_C(1099511628211);
}

static uint64_t FrameToken(const sg_tactic_frame_capability_t *frame)
{
	uint64_t token = UINT64_C(1469598103934665603);

	token = Mix(token, frame->subject.client_id);
	token = Mix(token, frame->subject.spawn_generation);
	token = Mix(token, frame->model_identity);
	token = Mix(token, frame->rune_identity);
	token = Mix(token, frame->topology_revision);
	token = Mix(token, frame->field_handle.service_identity);
	token = Mix(token, frame->field_handle.service_generation);
	token = Mix(token, frame->field_handle.target_id);
	token = Mix(token, frame->field_handle.target_generation);
	token = Mix(token, frame->field_handle.field_generation);
	token = Mix(token, frame->frame_sequence);
	token = Mix(token, frame->observed_at_ms);
	token = Mix(token, frame->localized.cell.value);
	token = Mix(token, (uint64_t)frame->localized.stance);
	token = Mix(token, (uint64_t)frame->localized.hook_phase);
	token = Mix(token, frame->owner_epoch);
	return token | UINT64_C(1);
}

static int ValidateFrame(const void *context, const sg_tactic_request_t *request,
	const sg_tactic_frame_capability_t *frame)
{
	(void)context;
	return request != NULL && frame == request->frame &&
		frame->token == FrameToken(frame);
}

static int ValidateModifier(const void *context,
	const sg_tactic_request_t *request, const sg_tactic_modifier_t *modifier)
{
	const authority_context_t *authority = context;

	return authority != NULL && authority->allow_modifiers != 0 &&
		request != NULL && modifier != NULL && request->frame != NULL &&
		modifier->source_id != 0U;
}

static int ValidateMechanism(const void *context,
	const sg_tactic_request_t *request,
	const sg_tactic_mechanism_request_t *mechanism)
{
	const authority_context_t *authority = context;

	return authority != NULL && authority->allow_mechanism != 0 &&
		request != NULL && mechanism != NULL && request->frame != NULL;
}

static int ValidateTemporaryBlock(const void *context,
	const sg_tactic_request_t *request,
	const sg_tactic_temporary_block_evidence_t *block)
{
	const authority_context_t *authority = context;

	return authority != NULL && authority->allow_block != 0 &&
		request != NULL && block != NULL && request->frame != NULL;
}

static int ValidateProbe(const void *context, const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptor,
	const sg_tactic_candidate_t *candidate,
	sg_rune_compact_field_cost_t nominal_cost)
{
	const authority_context_t *authority = context;
	const probe_context_t *probe = descriptor != NULL ? descriptor->context : NULL;
	const uint64_t continuation = request != NULL ?
		request->gradient.transition.v12.next_cost_to_go.units : 0U;
	const uint64_t expected_nominal = candidate != NULL &&
		candidate->capability == SG_TACTIC_CAPABILITY_WAIT && request != NULL ?
		request->gradient.transition.v12.cost_to_go.units :
		continuation + (candidate != NULL ? candidate->local_cost.units : 0U);

	return authority != NULL && request != NULL && descriptor != NULL &&
		probe != NULL && probe->authenticated != 0 && candidate != NULL &&
		memcmp(candidate, &probe->candidate, sizeof(*candidate)) == 0 &&
		(require_authorized_test_cost == 0 ||
		 candidate->local_cost.units == UINT64_C(200)) &&
		candidate->local_cost.units <
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE - continuation &&
		nominal_cost.units == expected_nominal;
}

static const sg_tactic_authority_t test_authority = {
	.context = &authority_context,
	.validate_frame = ValidateFrame,
	.validate_modifier = ValidateModifier,
	.validate_mechanism = ValidateMechanism,
	.validate_temporary_block = ValidateTemporaryBlock,
	.validate_probe = ValidateProbe
};

static int Probe(void *context, const sg_tactic_request_t *request,
	sg_tactic_candidate_t *candidate_out)
{
	probe_context_t *probe = context;

	(void)request;
	probe->calls++;
	if (probe->available == 0)
		return 0;
	*candidate_out = probe->candidate;
	return 1;
}

static sg_tactic_frame_capability_t Frame(void)
{
	sg_tactic_frame_capability_t frame = {
		.subject = { .client_id = 4U, .spawn_generation = 9U },
		.model_identity = UINT64_C(0x111),
		.rune_identity = UINT64_C(0x222),
		.topology_revision = UINT64_C(0x333),
		.field_handle = {
			.service_identity = UINT64_C(0x444),
			.service_generation = UINT64_C(0x555),
			.rune_identity = UINT64_C(0x222),
			.topology_revision = UINT64_C(0x333),
			.target_id = UINT64_C(0x666),
			.target_generation = UINT64_C(0x777),
			.field_generation = UINT64_C(0x888)
		},
		.frame_sequence = UINT64_C(0x999),
		.observed_at_ms = UINT64_C(800),
		.localized = {
			.cell = { 1U },
			.stance = SG_RUNE_COMPACT_FIELD_STANDING,
			.hook_phase = SG_HOST_HOOK_IDLE
		},
		.owner_epoch = UINT64_C(0xaaa)
	};

	frame.token = FrameToken(&frame);
	return frame;
}

static sg_tactic_transition_t Transition(
	sg_rune_compact_field_transition_kind_t kind,
	sg_rune_compact_field_stance_t target_stance,
	sg_host_hook_phase_t target_hook_phase)
{
	sg_tactic_transition_t transition = {
		.v12 = {
			.kind = kind,
			.cost_to_go = { .units = UINT64_C(1000) },
			.next_cost_to_go = { .units = UINT64_C(100) },
			.target_stance = target_stance
		},
		.target_hook_phase = target_hook_phase
	};

	if (kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL)
	{
		transition.v12.value.portal = (sg_rune_compact_field_portal_step_t){
			.local_cost = 1.0f,
			.next_cell = { 2U },
			.next_portal = { 6U }
		};
	}
	else if (kind == SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT)
	{
		transition.v12.value.direct = (sg_rune_compact_field_direct_step_t){
			.local_cost = 1.0f,
			.next_cell = { 3U }
		};
	}
	return transition;
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
			.transition = Transition(SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL,
				SG_RUNE_COMPACT_FIELD_STANDING, SG_HOST_HOOK_IDLE),
			.position_derivative = { -1.0f, 0.0f, 0.0f },
			.time_derivative = 1.0f,
			.descent_direction = { 1.0f, 0.0f, 0.0f }
		},
		.frame = frame,
		.legal_capability_mask = SG_TACTIC_CAPABILITY_MASK,
		.authority = &test_authority
	};
}

static sg_tactic_successor_state_t TransitionSuccessor(
	const sg_tactic_request_t *request)
{
	sg_tactic_successor_state_t successor = { 0 };

	CHECK(SG_TacticTransitionSuccessor(request->gradient.current_cell,
		&request->gradient.transition, &successor));
	return successor;
}

static sg_tactic_candidate_t Candidate(const sg_tactic_request_t *request,
	sg_tactic_capability_t capability, sg_tactic_phase_t phase,
	uint64_t local_cost)
{
	return (sg_tactic_candidate_t){
		.capability = capability,
		.successor = TransitionSuccessor(request),
		.predicted_phase = phase,
		.duration_seconds = 0.010f,
		.local_cost = { .units = local_cost }
	};
}

static sg_tactic_candidate_t WaitCandidate(const sg_tactic_request_t *request)
{
	return (sg_tactic_candidate_t){
		.capability = SG_TACTIC_CAPABILITY_WAIT,
		.successor = {
			.cell = request->live.cell,
			.stance = SG_TacticFieldStanceFromLive(request->live.stance),
			.hook_phase = request->live.hook_phase
		},
		.predicted_phase = request->live.phase,
		.duration_seconds = 0.010f
	};
}

static sg_tactic_capability_descriptor_t Descriptor(
	sg_tactic_capability_t capability, uint32_t flags, uint16_t priority,
	probe_context_t *probe)
{
	return (sg_tactic_capability_descriptor_t){
		.capability = capability,
		.phase_mask = SG_TACTIC_PHASE_BIT(SG_TACTIC_PHASE_GROUND),
		.flags = flags,
		.priority = priority,
		.probe = Probe,
		.context = probe
	};
}

static sg_tactic_modifier_t Modifier(const sg_tactic_request_t *request,
	sg_tactic_modifier_kind_t kind, uint32_t source_id, int64_t delta,
	sg_tactic_capability_t capability)
{
	return (sg_tactic_modifier_t){
		.kind = kind,
		.source_id = source_id,
		.cost_delta_units = delta,
		.capability_mask = SG_TACTIC_CAPABILITY_BIT(capability),
		.target_kind = SG_TACTIC_MODIFIER_TARGET_EXACT_SUCCESSOR,
		.target = TransitionSuccessor(request),
		.expires_at_ms = request->live.now_ms + UINT64_C(1),
		.active = 1U
	};
}

static sg_tactic_mechanism_request_t Mechanism(void)
{
	return (sg_tactic_mechanism_request_t){
		.mechanism_revision = 3U,
		.handoff_id = 4U,
		.mechanism_id = 5U,
		.controller_id = 6U,
		.portal = { 6U },
		.entry_cell = { 1U },
		.exit_cell = { 2U },
		.trigger_id = 7U,
		.dwell_ms = 80U,
		.requires_live_trace = 1U
	};
}

static sg_tactic_temporary_block_evidence_t TemporaryBlock(
	const sg_tactic_request_t *request,
	const sg_tactic_mechanism_request_t *mechanism)
{
	return (sg_tactic_temporary_block_evidence_t){
		.observed_at_ms = request->live.now_ms,
		.mechanism_revision = mechanism->mechanism_revision,
		.handoff_id = mechanism->handoff_id,
		.portal = mechanism->portal,
		.entry_cell = mechanism->entry_cell,
		.exit_cell = mechanism->exit_cell
	};
}

static void TestV12TransitionUnionAndExactState(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_successor_state_t successor = { 0 };

	CHECK(SG_TacticRequestValid(&request));
	CHECK(SG_TacticTransitionSuccessor(request.gradient.current_cell,
		&request.gradient.transition, &successor));
	CHECK(successor.cell.value == 2U);
	CHECK(successor.stance == SG_RUNE_COMPACT_FIELD_STANDING);
	CHECK(successor.hook_phase == SG_HOST_HOOK_IDLE);

	request.gradient.transition = Transition(
		SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT,
		SG_RUNE_COMPACT_FIELD_CROUCHING, SG_HOST_HOOK_ATTACHED);
	CHECK(SG_TacticRequestValid(&request));
	CHECK(SG_TacticTransitionSuccessor(request.gradient.current_cell,
		&request.gradient.transition, &successor));
	CHECK(successor.cell.value == 3U);
	CHECK(successor.stance == SG_RUNE_COMPACT_FIELD_CROUCHING);
	CHECK(successor.hook_phase == SG_HOST_HOOK_ATTACHED);

	request.gradient.transition = Transition(
		SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE,
		SG_RUNE_COMPACT_FIELD_CROUCHING, SG_HOST_HOOK_IDLE);
	CHECK(SG_TacticRequestValid(&request));
	CHECK(SG_TacticTransitionSuccessor(request.gradient.current_cell,
		&request.gradient.transition, &successor));
	CHECK(successor.cell.value == 1U);
	CHECK(successor.stance == SG_RUNE_COMPACT_FIELD_CROUCHING);
	CHECK(successor.hook_phase == SG_HOST_HOOK_IDLE);
}

static void TestExactSuccessorContinuation(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	probe_context_t probe = {
		.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 200U),
		.available = 1,
		.authenticated = 1
	};
	sg_tactic_capability_descriptor_t descriptor = Descriptor(
		SG_TACTIC_CAPABILITY_WALK, 0U, 0U, &probe);
	sg_tactic_result_t result;

	CHECK(SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	CHECK(result.nominal_cost.units == 300U);
	CHECK(result.successor.cell.value == 2U);
	CHECK(result.successor.stance == SG_RUNE_COMPACT_FIELD_STANDING);

	probe.candidate.successor.cell = request.live.cell;
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_FAILURE);
	probe.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
		SG_TACTIC_PHASE_GROUND, 200U);
	probe.candidate.successor.hook_phase = SG_HOST_HOOK_ATTACHED;
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_FAILURE);
	probe.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
		SG_TACTIC_PHASE_GROUND, 900U);
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_FAILURE);
}

static void TestFrameAndProbeAuthentication(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	probe_context_t probe = {
		.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 200U),
		.available = 1,
		.authenticated = 1
	};
	sg_tactic_capability_descriptor_t descriptor = Descriptor(
		SG_TACTIC_CAPABILITY_WALK, 0U, 0U, &probe);
	sg_tactic_result_t result;

	CHECK(SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	frame.subject.spawn_generation++;
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	frame = Frame();
	request.frame = &frame;
	frame.field_handle.field_generation++;
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	frame = Frame();
	request.frame = &frame;
	frame.localized.hook_phase = SG_HOST_HOOK_ATTACHED;
	frame.token = FrameToken(&frame);
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	frame = Frame();
	request.frame = &frame;
	probe.authenticated = 0;
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	probe.authenticated = 1;
	require_authorized_test_cost = 1;
	probe.candidate.local_cost.units++;
	CHECK(!SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	require_authorized_test_cost = 0;
}

static void TestEveryEligibleDescriptorIsProbed(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	probe_context_t probes[3] = {
		{ Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 300U), 0U, 1, 1 },
		{ Candidate(&request, SG_TACTIC_CAPABILITY_JUMP,
			SG_TACTIC_PHASE_JUMP, 100U), 0U, 1, 1 },
		{ Candidate(&request, SG_TACTIC_CAPABILITY_SWIM,
			SG_TACTIC_PHASE_SWIM, 1U), 0U, 1, 1 }
	};
	sg_tactic_capability_descriptor_t descriptors[3] = {
		Descriptor(SG_TACTIC_CAPABILITY_WALK, 0U, 1U, &probes[0]),
		Descriptor(SG_TACTIC_CAPABILITY_JUMP, 0U, 1U, &probes[1]),
		Descriptor(SG_TACTIC_CAPABILITY_SWIM,
			SG_TACTIC_CAPABILITY_REQUIRES_WATER, 1U, &probes[2])
	};
	sg_tactic_result_t result;

	CHECK(SG_TacticSelectCapability(&request, descriptors, 3U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_JUMP);
	CHECK(result.nominal_cost.units == 200U);
	CHECK(probes[0].calls == 1U);
	CHECK(probes[1].calls == 1U);
	CHECK(probes[2].calls == 0U);
}

/* A rocket jump is offered by the RUNE, and taken only by a body that has
 * the launcher, a rocket, and more health than the blast takes after armor.
 * With nothing carried it is never probed; with everything it wins on cost. */
static void TestRocketJumpNeedsLauncherRoundsAndHealth(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	probe_context_t probes[2] = {
		{ Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 300U), 0U, 1, 1 },
		{ Candidate(&request, SG_TACTIC_CAPABILITY_ROCKET_JUMP,
			SG_TACTIC_PHASE_JUMP, 100U), 0U, 1, 1 }
	};
	sg_tactic_capability_descriptor_t descriptors[2] = {
		Descriptor(SG_TACTIC_CAPABILITY_WALK, 0U, 1U, &probes[0]),
		Descriptor(SG_TACTIC_CAPABILITY_ROCKET_JUMP, 0U, 1U, &probes[1])
	};
	sg_tactic_result_t result;

	request.live.gravity = 800.0f;
	CHECK(SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_WALK);
	CHECK(probes[1].calls == 0U);

	/* Launcher and rocket, but the blast takes 47 and 47 is all there is. */
	request.live.inventory.weapon_mask =
		UINT32_C(1) << SG_WEAPON_PROFILE_ROCKET_LAUNCHER;
	request.live.inventory.rocket_rounds = 1U;
	request.live.inventory.health = 47;
	CHECK(SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_WALK);
	CHECK(probes[1].calls == 0U);

	request.live.inventory.health = 48;
	CHECK(SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_ROCKET_JUMP);
	CHECK(probes[1].calls == 1U);

	/* Body armor makes it affordable at low health. */
	request.live.inventory.health = 10;
	request.live.inventory.armor_count = 100;
	request.live.inventory.armor_protection = 0.80f;
	CHECK(SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_ROCKET_JUMP);

	/* No rocket, no rocket jump. */
	request.live.inventory.rocket_rounds = 0U;
	CHECK(SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_WALK);
}

static void TestMechanismMetadataAndWait(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_mechanism_request_t mechanism = Mechanism();
	sg_tactic_temporary_block_evidence_t block = TemporaryBlock(&request,
		&mechanism);
	probe_context_t mechanism_probe = {
		.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_MECHANISM,
			SG_TACTIC_PHASE_MECHANISM, 200U),
		.available = 1,
		.authenticated = 1
	};
	probe_context_t walk_probe = {
		.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 200U),
		.available = 1,
		.authenticated = 1
	};
	probe_context_t wait_probe = {
		.candidate = WaitCandidate(&request),
		.available = 1,
		.authenticated = 1
	};
	sg_tactic_capability_descriptor_t mechanism_descriptor = Descriptor(
		SG_TACTIC_CAPABILITY_MECHANISM,
		SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY, 0U, &mechanism_probe);
	sg_tactic_capability_descriptor_t walk_descriptor = Descriptor(
		SG_TACTIC_CAPABILITY_WALK, 0U, 0U, &walk_probe);
	sg_tactic_capability_descriptor_t wait_descriptor = Descriptor(
		SG_TACTIC_CAPABILITY_WAIT, 0U, 0U, &wait_probe);
	sg_tactic_result_t result;

	request.mechanism = &mechanism;
	CHECK(SG_TacticSelectCapability(&request, &mechanism_descriptor, 1U,
		&result));
	CHECK(result.mechanism_handoff_valid == 1U);
	CHECK(result.exact_live_validation_required == 1U);
	CHECK(result.successor.cell.value == 2U);

	CHECK(!SG_TacticSelectCapability(&request, &wait_descriptor, 1U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_FAILURE);
	request.temporary_block = &block;
	CHECK(SG_TacticSelectCapability(&request, &wait_descriptor, 1U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_HOLD);
	CHECK(result.nominal_cost.units == 1000U);
	CHECK(result.successor.cell.value == 1U);

	request.temporary_block = NULL;
	mechanism.portal.value = 99U;
	CHECK(SG_TacticSelectCapability(&request, &walk_descriptor, 1U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_WALK);
	request.mechanism = NULL;
	request.temporary_block = &block;
	CHECK(!SG_TacticSelectCapability(&request, &walk_descriptor, 1U, &result));

	request.mechanism = &mechanism;
	request.temporary_block = &block;
	authority_context.allow_block = 0;
	CHECK(!SG_TacticSelectCapability(&request, &wait_descriptor, 1U, &result));
	authority_context.allow_block = 1;
}

static void TestModifiersRankOnlyAndPublishNominalCost(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	probe_context_t probes[2] = {
		{ Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 300U), 0U, 1, 1 },
		{ Candidate(&request, SG_TACTIC_CAPABILITY_JUMP,
			SG_TACTIC_PHASE_JUMP, 350U), 0U, 1, 1 }
	};
	sg_tactic_capability_descriptor_t descriptors[2] = {
		Descriptor(SG_TACTIC_CAPABILITY_WALK, 0U, 0U, &probes[0]),
		Descriptor(SG_TACTIC_CAPABILITY_JUMP, 0U, 0U, &probes[1])
	};
	sg_tactic_modifier_t modifier = Modifier(&request, SG_TACTIC_MODIFIER_COVER,
		1U, -INT64_C(1000), SG_TACTIC_CAPABILITY_JUMP);
	sg_tactic_result_t result;

	request.modifiers = &modifier;
	request.modifier_count = 1U;
	CHECK(SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_JUMP);
	CHECK(result.nominal_cost.units == 450U);

	probes[1].candidate.local_cost.units = 900U;
	CHECK(!SG_TacticSelectCapability(&request, descriptors, 2U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_FAILURE);
	modifier.cost_delta_units = SG_TACTIC_MODIFIER_MAX_DEFORMATION_UNITS + 1;
	CHECK(!SG_TacticRequestValid(&request));
	modifier.cost_delta_units = -INT64_C(1000);
	authority_context.allow_modifiers = 0;
	CHECK(!SG_TacticRequestValid(&request));
	authority_context.allow_modifiers = 1;
}

static void TestModifierSaturationDoesNotRejectCandidate(void)
{
	enum { modifier_count = 5000 };
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_modifier_t modifiers[modifier_count];
	probe_context_t probe = {
		.candidate = Candidate(&request, SG_TACTIC_CAPABILITY_WALK,
			SG_TACTIC_PHASE_GROUND, 200U),
		.available = 1,
		.authenticated = 1
	};
	sg_tactic_capability_descriptor_t descriptor = Descriptor(
		SG_TACTIC_CAPABILITY_WALK, 0U, 0U, &probe);
	sg_tactic_result_t result;
	uint32_t index;

	for (index = 0U; index < modifier_count; index++)
	{
		modifiers[index] = Modifier(&request,
			(sg_tactic_modifier_kind_t)(index %
				(uint32_t)SG_TACTIC_MODIFIER_KIND_COUNT), index + 1U,
			SG_TACTIC_MODIFIER_MAX_DEFORMATION_UNITS,
			SG_TACTIC_CAPABILITY_WALK);
	}
	request.modifiers = modifiers;
	request.modifier_count = modifier_count;
	CHECK(SG_TacticSelectCapability(&request, &descriptor, 1U, &result));
	CHECK(result.capability == SG_TACTIC_CAPABILITY_WALK);
	CHECK(result.nominal_cost.units == 300U);
}

static void TestFailureResults(void)
{
	sg_tactic_frame_capability_t frame = Frame();
	sg_tactic_request_t request = Request(&frame);
	sg_tactic_result_t result;

	CHECK(!SG_TacticSelectCapability(&request, NULL, 0U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_RETRY);
	CHECK(result.failure == SG_TACTIC_FAILURE_NO_LEGAL_CAPABILITY);
	CHECK(SG_TacticResultValid(&result));
	request.gradient.sampled_at_ms++;
	CHECK(!SG_TacticSelectCapability(&request, NULL, 0U, &result));
	CHECK(result.status == SG_TACTIC_RESULT_RETRY);
	CHECK(result.failure == SG_TACTIC_FAILURE_NO_GRADIENT);
	CHECK(SG_TacticResultValid(&result));
}

int main(void)
{
	TestV12TransitionUnionAndExactState();
	TestExactSuccessorContinuation();
	TestFrameAndProbeAuthentication();
	TestEveryEligibleDescriptorIsProbed();
	TestRocketJumpNeedsLauncherRoundsAndHealth();
	TestMechanismMetadataAndWait();
	TestModifiersRankOnlyAndPublishNominalCost();
	TestModifierSaturationDoesNotRejectCandidate();
	TestFailureResults();
	if (failures != 0)
	{
		fprintf(stderr, "sg_tactic_policy_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_tactic_policy_test: ok");
	return 0;
}
